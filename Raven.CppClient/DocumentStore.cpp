#include "stdafx.h"
#include "DocumentStore.h"
#include "TasksExecutor.h"
#include "DocumentSession.h"
#include "SessionOptions.h"
#include "MaintenanceOperationExecutor.h"
#include "MultiDatabaseHiLoIdGenerator.h"
#include "GetCppClassName.h"

namespace ravendb::client::documents
{
	DocumentStore::~DocumentStore() = default;

	std::shared_ptr<DocumentStore> DocumentStore::create()
	{
		auto object = std::shared_ptr<DocumentStore>(new DocumentStore(), Deleter());
		object->_weak_this = object;
		object->initiate_operation_executors();
		return object;
	}

	std::shared_ptr<DocumentStore> DocumentStore::create(std::string url, std::string database)
	{
		auto object = std::shared_ptr<DocumentStore>(new DocumentStore(std::move(url), std::move(database)), Deleter());
		object->_weak_this = object;
		object->initiate_operation_executors();
		return object;
	}

	std::shared_ptr<DocumentStore> DocumentStore::create(std::vector<std::string> urls, std::string database)
	{
		auto object = std::shared_ptr<DocumentStore>(new DocumentStore(std::move(urls), std::move(database)), Deleter());
		object->_weak_this = object;
		object->initiate_operation_executors();
		return object;
	}

	std::shared_ptr<impl::IExecutorService> DocumentStore::get_executor_service() const
	{
		return _executor_service;
	}

	void DocumentStore::set_executor_service(std::shared_ptr<impl::IExecutorService> executor_service)
	{
		if(!executor_service)
		{
			throw std::invalid_argument("'executor_service' shouldn't be empty");
		}
		_executor_service = executor_service;
	}

	void DocumentStore::set_default_executor_service(uint32_t num_of_threads)
	{
		_executor_service = std::make_shared<impl::TasksExecutor>(num_of_threads);
	}

	std::shared_ptr<impl::TasksScheduler> DocumentStore::get_scheduler() const
	{
		return _scheduler;
	}

	void DocumentStore::Deleter::operator()(DocumentStore* ptr) const
	{
		try
		{
			ptr->close();
		}
		catch (...)
		{}

		delete ptr;
	}

	void DocumentStore::initiate_operation_executors()
	{
		_maintenance_operation_executor = operations::MaintenanceOperationExecutor::create(
			std::static_pointer_cast<DocumentStore>(_weak_this.lock()));
		_operation_executor = operations::OperationExecutor::create(_weak_this.lock());
	}

	DocumentStore::DocumentStore() = default;

	DocumentStore::DocumentStore(std::string url, std::string database)
		: DocumentStore(std::vector<std::string>{std::move(url)}, std::move(database))
	{}

	DocumentStore::DocumentStore(std::vector<std::string> urls, std::string database)
	{
		set_urls(std::move(urls));
		set_database(std::move(database));
	}

	std::string DocumentStore::get_identifier() const
	{
		if(!_identifier.empty())
		{
			return _identifier;
		}

		std::ostringstream result{};
		std::copy(urls.cbegin(), urls.cend(), std::ostream_iterator<std::string>(result, ","));		
		if(!database.empty())
		{
			result << " (DB: " << database << ")";
		}

		return result.str();
	}

	void DocumentStore::set_identifier(std::string identifier)
	{
		_identifier = std::move(identifier);
	}

	void DocumentStore::close()
	{
		if(disposed)
		{
			return;
		}

		//TODO implement the rest

		try
		{
			if (_multi_db_hilo)
			{
				_multi_db_hilo->return_unused_range(*this);
			}
		}catch (...)
		{}// ignore

		disposed = true;
	}

	session::DocumentSession DocumentStore::open_session()
	{
		return open_session(session::SessionOptions());
	}
	class session::DocumentSession DocumentStore::open_session(const std::string& database)
	{
		return open_session(session::SessionOptions{ database });
	}
	session::DocumentSession DocumentStore::open_session(const session::SessionOptions& options)
	{
		assert_initialized();
		ensure_not_closed();

		auto session_impl = session::DocumentSessionImpl::create(_weak_this.lock(), options);
		//TODO
		//register_events(session_impl);
		//after_session_created(session_impl);
		return session::DocumentSession(session_impl);
	}

	std::shared_ptr<http::RequestExecutor> DocumentStore::get_request_executor(const std::string& database) const
	{
		assert_initialized();

		auto&& db_name = database.empty() ? get_database() : database;

		{
			auto lock = std::shared_lock(_request_executors_mutex);
			if(auto it = _request_executors.find(db_name);
				it != _request_executors.end())
			{
				return it->second;
			}
		}
		{
			auto lock = std::unique_lock(_request_executors_mutex);
			if (auto it = _request_executors.find(db_name);
				it != _request_executors.end())
			{
				return it->second;
			}

			std::optional<std::string> db_name_optional{};
			if(!db_name.empty())
			{
				db_name_optional.emplace(db_name);
			}

			std::shared_ptr<http::RequestExecutor> executor{};

			if(!get_conventions()->is_disable_topology_updates())
			{
				executor = http::RequestExecutor::create(
					get_urls(), std::move(db_name_optional), _certificate_details, _scheduler, get_conventions(),
					_set_before_perform, _set_after_perform);
			}
			else
			{
				executor = http::RequestExecutor::create_for_single_node_with_configuration_updates(
					get_urls()[0], std::move(db_name_optional), _certificate_details, _scheduler, get_conventions(),
					_set_before_perform, _set_after_perform);
			}
			_request_executors.insert_or_assign(db_name, executor);

			return executor;
		}
	}

	void DocumentStore::assert_valid_configuration() const
	{
		if(urls.empty())
		{
			throw std::invalid_argument("Document store URLs cannot be empty");
		}
	}

	std::shared_ptr<IDocumentStore> DocumentStore::initialize()
	{
		if (is_initialized)
		{
			return _weak_this.lock();
		}
		assert_valid_configuration();

		try
		{
			//executor_service was no explicitly set -> using defaults
			if (!_executor_service)
			{
				set_default_executor_service();
			}
			_scheduler = std::make_shared<impl::TasksScheduler>(_executor_service);

			if (!get_conventions()->get_document_id_generator())// don't overwrite what the user is doing
			{
				_multi_db_hilo = std::make_unique<identity::MultiDatabaseHiLoIdGenerator>(
					std::static_pointer_cast<DocumentStore>(_weak_this.lock()), get_conventions());

				get_conventions()->set_document_id_generator([this]
				(const std::string& db_name, std::shared_ptr<void> entity, std::type_index type)->std::string
				{
					auto&& res = _multi_db_hilo->generate_document_id(db_name, type, entity);
					if (!res)
					{
						throw std::runtime_error("Could not generate id for the entity of type"
							+ impl::utils::GetCppClassName()(type));
					}
					return *res;
				});
			}

			get_conventions()->freeze();
			is_initialized = true;
		}
		catch (...)
		{
			close();
			throw;
		}

		return _weak_this.lock();
	}

	std::shared_ptr<operations::MaintenanceOperationExecutor> DocumentStore::maintenance() const
	{
		return _maintenance_operation_executor;
	}

	std::shared_ptr<operations::OperationExecutor> DocumentStore::get_operations() const
	{
		return _operation_executor;
	}

}
