#include "pch.h"
#include "definitions.h"

#include "GetDatabaseRecordOperation.h"
#include "GetDatabaseNamesOperation.h"
#include "GetDocumentsCommand.h"

namespace ravendb::client::tests
{
	TEST(RequestExecutorTests, CanGetDatabaseRecord)
	{
		auto test_suite_executor = GET_REQUEST_EXECUTOR();
		auto op = serverwide::operations::GetDatabaseRecordOperation(test_suite_executor->get_db_name());
		auto rec = test_suite_executor->get()->execute(op.get_command({}));

		ASSERT_EQ(rec.database_name, test_suite_executor->get_db_name());
		ASSERT_GT(rec.etag.value(), -1);
	}

	TEST(RequestExecutorTests, CanGetDatabaseName)
	{
		auto test_suite_executor = GET_REQUEST_EXECUTOR();
		auto op = serverwide::operations::GetDatabaseNamesOperation(0, 100);
		auto&& res = test_suite_executor->get()->execute(op.get_command({}));

		ASSERT_NE(std::find(res.begin(), res.end(), test_suite_executor->get_db_name()), res.end());
	}

	TEST(RequestExecutorTests, ThrowsWhenDatabaseDoesNotExist)
	{
		try
		{
			auto test_suite_executor = get_raw_request_executor(false, "no_such_db");

			documents::commands::GetDocumentsCommand cmd("users/1-A", {}, false);
			test_suite_executor->execute(cmd);
		}
		catch (RavenError& exception)
		{
			ASSERT_EQ(exception.get_error_type(), RavenError::ErrorType::database_does_not_exist);
			SUCCEED();
			return;
		}
		FAIL();
	}

	TEST(RequestExecutorTests, CanDeleteDatabase)
	{
		auto test_suite_executor = GET_REQUEST_EXECUTOR();
		{
			auto op = serverwide::operations::DeleteDatabasesOperation(test_suite_executor->get_db_name(),
				true, {}, std::chrono::seconds(10));
			auto&& res = test_suite_executor->get()->execute(op.get_command({}));
			ASSERT_GT(res.raft_command_index, 0);
		}
		{
			auto op = serverwide::operations::GetDatabaseNamesOperation(0, 100);
			auto&& res = test_suite_executor->get()->execute(op.get_command({}));
			ASSERT_EQ(std::find(res.begin(), res.end(), test_suite_executor->get_db_name()), res.end());
		}
	}
}
