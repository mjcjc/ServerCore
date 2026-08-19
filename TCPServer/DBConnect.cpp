#include "DBConnect.h"

#include <mysql/jdbc.h>

#include <cstdlib>
#include <exception>

namespace
{
    bool ReadOptionalEnvironmentVariable(const char* name, std::string& outValue)
    {
        char* value = nullptr;
        std::size_t valueSize = 0;

        if (_dupenv_s(&value, &valueSize, name) != 0 || value == nullptr)
        {
            return false;
        }

        if (valueSize <= 1)
        {
            std::free(value);
            return false;
        }

        outValue.assign(value, valueSize - 1);
        std::free(value);
        return true;
    }
}

bool DBConfig::LoadFromEnvironment(DBConfig& outConfig, std::string& outError)
{
    ReadOptionalEnvironmentVariable("GAME_DB_ENDPOINT", outConfig.endpoint);
    ReadOptionalEnvironmentVariable("GAME_DB_USER", outConfig.user);
    ReadOptionalEnvironmentVariable("GAME_DB_NAME", outConfig.schema);

    if (!ReadOptionalEnvironmentVariable("GAME_DB_PASSWORD", outConfig.password))
    {
        outError = "GAME_DB_PASSWORD environment variable is not set.";
        return false;
    }

    outError.clear();
    return true;
}

DBConnect::DBConnect() = default;

DBConnect::~DBConnect()
{
    Disconnect();
}

bool DBConnect::Connect(const DBConfig& config)
{
    Disconnect();
    lastError.clear();

    try
    {
        sql::mysql::MySQL_Driver* driver = sql::mysql::get_mysql_driver_instance();

        connection.reset(driver->connect(config.endpoint, config.user, config.password));

        connection->setSchema(config.schema);

        if (!ValidateConnection())
        {
            Disconnect();
            return false;
        }

        return true;
    }
    catch (const sql::SQLException& exception)
    {
        lastError = exception.what();
    }
    catch (const std::exception& exception)
    {
        lastError = exception.what();
    }
    catch (...)
    {
        lastError = "Unknown database connection error.";
    }

    Disconnect();
    return false;
}

void DBConnect::Disconnect()
{
    if (connection)
    {
        try
        {
            if (!connection->isClosed())
            {
                connection->close();
            }
        }
        catch (...)
        {
            // Destruction must not throw.
        }

        connection.reset();
    }
}

bool DBConnect::IsConnected() const
{
    return connection != nullptr && !connection->isClosed();
}

const std::string& DBConnect::GetLastError() const
{
    return lastError;
}

bool DBConnect::ValidateConnection()
{
    std::unique_ptr<sql::Statement> statement(connection->createStatement());
    std::unique_ptr<sql::ResultSet> result(statement->executeQuery("SELECT 1"));

    if (!result->next() || result->getInt(1) != 1)
    {
        lastError = "MySQL SELECT 1 validation failed.";
        return false;
    }

    return true;
}
