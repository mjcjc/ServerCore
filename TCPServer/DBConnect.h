#pragma once

#include <memory>
#include <string>

namespace sql
{
    class Connection;
}

struct DBConfig
{
    std::string endpoint = "tcp://127.0.0.1:3306";
    std::string user = "game_server_app";
    std::string password;
    std::string schema = "game_server";

    static bool LoadFromEnvironment(DBConfig& outConfig, std::string& outError);
};

class DBConnect
{
  public:
    DBConnect();
    ~DBConnect();

    DBConnect(const DBConnect&) = delete;
    DBConnect& operator=(const DBConnect&) = delete;

    bool Connect(const DBConfig& config);
    void Disconnect();

    bool IsConnected() const;
    const std::string& GetLastError() const;

  private:
    bool ValidateConnection();

    std::unique_ptr<sql::Connection> connection;
    std::string lastError;
};
