#include "..\Common.h"
#include "DBConnect.h"
#include "MasterClient.h"
#include "NetworkCore.h"
#include "ServerConfig.h"

int main(int argc, char* argv[])
{
    setvbuf(stdout, nullptr, _IONBF, 0);

    ServerConfig serverConfig;
    std::string configError;

    if (!ServerConfig::Parse(argc, argv, serverConfig, configError))
    {
        printf("%s\n", configError.c_str());
        return 1;
    }

    DBConfig dbConfig;
    DBConnect database;

    if (!serverConfig.skipDatabase && !DBConfig::LoadFromEnvironment(dbConfig, configError))
    {
        printf("[DB] Configuration failed: %s\n", configError.c_str());
        return 1;
    }

    if (!serverConfig.skipDatabase && !database.Connect(dbConfig))
    {
        printf("[DB] MySQL connection failed: %s\n", database.GetLastError().c_str());
        return 1;
    }

    if (serverConfig.skipDatabase)
    {
        printf("[GameServer %u][DB] Skipped for local multi-process test.\n",
               serverConfig.serverId);
    }
    else
    {
        printf("[GameServer %u][DB] MySQL connection succeeded: %s/%s\n",
               serverConfig.serverId,
               dbConfig.endpoint.c_str(),
               dbConfig.schema.c_str());
    }

    NetworkCore server(serverConfig);

    server.Initialize();
    server.bindSock();
    server.listenSock();

    printf("[GameServer %u] Started. Bind=%s, Port=%u, Workers=%u\n",
           serverConfig.serverId,
           serverConfig.bindIp.c_str(),
           serverConfig.port,
           serverConfig.workerCount);

    MasterClient masterClient(serverConfig, server);
    masterClient.Start();

    server.Run();
    masterClient.Stop();
    server.closeSock();

    return 0;
}
