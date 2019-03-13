#include "pch.h"
#include "RavenTestDriver.h"
#include "DocumentConventions.h"
#include "raven_test_definitions.h"
#include "CreateDatabaseOperation.h"
#include "RequestExecutor.h"
#include "DeleteDatabasesOperation.h"

namespace ravendb::client::tests::driver
{
	const infrastructure::ConnectionDetailsHolder RavenTestDriver::secured_connection_details = 
		infrastructure::ConnectionDetailsHolder(
			infrastructure::SECURED_RE_DETAILS_FILE_NAME, true);

	const infrastructure::ConnectionDetailsHolder RavenTestDriver::unsecured_connection_details = 
		infrastructure::ConnectionDetailsHolder(
			infrastructure::UNSECURED_RE_DETAILS_FILE_NAME, false);

	std::shared_ptr<documents::IDocumentStore> RavenTestDriver::global_server{};

	std::shared_ptr<documents::IDocumentStore> RavenTestDriver::global_secured_server{};

	std::mutex RavenTestDriver::documents_stores_guard{};
	std::unordered_map<std::string, std::shared_ptr<documents::DocumentStore>> RavenTestDriver::document_stores{};

	std::atomic_uint64_t RavenTestDriver::index = 1;


	std::string RavenTestDriver::generate_database_name() const
	{
		return documents::conventions::DocumentConventions::default_get_collection_name(typeid(decltype(*this)));
	}

	std::shared_ptr<documents::IDocumentStore> RavenTestDriver::get_global_server(bool secured) const
	{
		return secured ? global_secured_server : global_server;
	}

	void RavenTestDriver::customise_store(std::shared_ptr<documents::DocumentStore> store)
	{}

	void RavenTestDriver::TearDown()
	{
		this->close();

		//TODO this is until listeners are implemented
		{
			auto guard = std::lock_guard(documents_stores_guard);

			for (auto&[identifier, store] : document_stores)
			{
				auto delete_database_operation = serverwide::operations::DeleteDatabasesOperation(store->get_database(), true);
				store->get_request_executor()->execute(delete_database_operation.get_command(store->get_conventions()));
			}
			document_stores.clear();
		}
		//TODO ---------------------------------------
	}

	RavenTestDriver::~RavenTestDriver() = default;

	bool RavenTestDriver::is_disposed() const
	{
		return disposed;
	}

	std::shared_ptr<documents::DocumentStore> RavenTestDriver::get_secured_document_store()
	{
		return get_document_store(generate_database_name(), true, {});
	}

	std::shared_ptr<documents::DocumentStore> RavenTestDriver::get_secured_document_store(const std::string& database)
	{
		return get_document_store(database, true, {});
	}

	std::shared_ptr<documents::DocumentStore> RavenTestDriver::get_document_store()
	{
		return get_document_store(generate_database_name());
	}

	std::shared_ptr<documents::DocumentStore> RavenTestDriver::get_document_store(const std::string& database)
	{
		return get_document_store(database, false, {});
	}

	std::shared_ptr<documents::DocumentStore> RavenTestDriver::get_document_store(const std::string& database,
		bool secured, const std::optional<std::chrono::milliseconds>& wait_for_indexing_timeout)
	{
		const std::string database_name = database + "_" + std::to_string(index.fetch_add(1));

		static std::mutex m{};
		if(!get_global_server(secured))
		{
			auto guard = std::lock_guard(m);
			if(!get_global_server(secured))
			{
				if (secured)
				{
					global_secured_server = documents::DocumentStore::create();
					auto server = std::static_pointer_cast<documents::DocumentStoreBase>(global_secured_server);
					server->set_urls({ secured_connection_details.get_url() });
					server->set_certificate_details(secured_connection_details.get_certificate_details());
				}
				else
				{
					global_server = documents::DocumentStore::create();
					auto server = std::static_pointer_cast<documents::DocumentStoreBase>(global_server);
					server->set_urls({ unsecured_connection_details.get_url() });
				}
				get_global_server(secured)->initialize();
			}
		}

		auto document_store = get_global_server(secured);
		auto database_record = serverwide::DatabaseRecord();
		database_record.database_name = database_name;

		customise_db_record(database_record);

		auto create_database_operation = serverwide::operations::CreateDatabaseOperation(database_record);
		//TODO implement using documentStore.maintenance().server().send(createDatabaseOperation);
		document_store->get_request_executor()->execute(
			create_database_operation.get_command(document_store->get_conventions()));

		auto store = documents::DocumentStore::create(document_store->get_urls(), database_name);
		if(secured)
		{
			store->set_certificate_details(secured_connection_details.get_certificate_details());
		}

		customise_store(store);

		//TODO hookLeakedConnectionCheck(store);

		store->initialize();

		//TODO  store.addAfterCloseListener(((sender, event)

		set_up_database(store);

		if(wait_for_indexing_timeout)
		{
			wait_for_indexing(store, database_name, wait_for_indexing_timeout);
		}

		{
			auto guard = std::lock_guard(documents_stores_guard);
			document_stores.insert_or_assign(store->get_identifier(), store);
		}

		return store;

	}

	void RavenTestDriver::wait_for_indexing(std::shared_ptr<documents::IDocumentStore> store,
		const std::optional<std::string>& database, const std::optional<std::chrono::milliseconds>& timeout)
	{
		//TODO implement
		std::this_thread::sleep_for(std::chrono::seconds(5));
	}

	void RavenTestDriver::close()
	{
		if(disposed)
		{
			return;
		}

		std::vector<std::string_view> exceptions{};
		{
			auto guard = std::lock_guard(documents_stores_guard);
			for(auto& [identifier, store] : document_stores)
			{
				try
				{
					store->close();
				}
				catch (std::exception& e)
				{
					exceptions.emplace_back(e.what());
				}
			}
		}
		disposed = true;

		std::ostringstream msgs{};
		std::transform(exceptions.cbegin(), exceptions.cend(), 
			std::ostream_iterator<std::string_view>(msgs, ", "), [](auto& str)
		{
			return str;
		});

		if(!exceptions.empty())
		{
			throw std::runtime_error(msgs.str());
		}
	}
}
