#include "ServerConfig.h"

#include <cerrno>
#include <cstdlib>
#include <limits>

namespace
{
    bool ParseUnsigned(const char* text, unsigned long maximum, unsigned long& value)
    {
        if (text == nullptr || *text == '\0' || *text == '-')
        {
            return false;
        }

        char* end = nullptr;
        errno = 0;
        const unsigned long parsed = std::strtoul(text, &end, 10);

        if (errno != 0 || end == text || *end != '\0' || parsed == 0 || parsed > maximum)
        {
            return false;
        }

        value = parsed;
        return true;
    }
}

bool ServerConfig::Parse(int argc, char* argv[], ServerConfig& config, std::string& error)
{
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];

        if (argument == "--skip-db")
        {
            config.skipDatabase = true;
            continue;
        }

        if (argument == "--help" || argument == "-h")
        {
            error = Usage();
            return false;
        }

        if (index + 1 >= argc)
        {
            error = "Missing value for argument: " + argument;
            return false;
        }

        const char* valueText = argv[++index];

        if (argument == "--server-id")
        {
            unsigned long value = 0;
            if (!ParseUnsigned(valueText, std::numeric_limits<std::uint32_t>::max(), value))
            {
                error = "Invalid server id: " + std::string(valueText);
                return false;
            }

            config.serverId = static_cast<std::uint32_t>(value);
        }
        else if (argument == "--world-id")
        {
            unsigned long value = 0;
            if (!ParseUnsigned(valueText, std::numeric_limits<std::uint32_t>::max(), value))
            {
                error = "Invalid world id: " + std::string(valueText);
                return false;
            }

            config.worldId = static_cast<std::uint32_t>(value);
        }
        else if (argument == "--channel-id")
        {
            unsigned long value = 0;
            if (!ParseUnsigned(valueText, std::numeric_limits<std::uint32_t>::max(), value))
            {
                error = "Invalid channel id: " + std::string(valueText);
                return false;
            }

            config.channelId = static_cast<std::uint32_t>(value);
        }
        else if (argument == "--port")
        {
            unsigned long value = 0;
            if (!ParseUnsigned(valueText, 65535, value))
            {
                error = "Invalid port: " + std::string(valueText);
                return false;
            }

            config.port = static_cast<std::uint16_t>(value);
        }
        else if (argument == "--bind-ip")
        {
            config.bindIp = valueText;
        }
        else if (argument == "--worker-count")
        {
            unsigned long value = 0;
            if (!ParseUnsigned(valueText, 256, value))
            {
                error = "Invalid worker count: " + std::string(valueText);
                return false;
            }

            config.workerCount = static_cast<std::uint32_t>(value);
        }
        else if (argument == "--master-ip")
        {
            config.masterIp = valueText;
        }
        else if (argument == "--master-port")
        {
            unsigned long value = 0;
            if (!ParseUnsigned(valueText, 65535, value))
            {
                error = "Invalid master port: " + std::string(valueText);
                return false;
            }

            config.masterPort = static_cast<std::uint16_t>(value);
        }
        else if (argument == "--world-server-ip")
        {
            config.worldServerIp = valueText;
        }
        else if (argument == "--world-server-port")
        {
            unsigned long value = 0;
            if (!ParseUnsigned(valueText, 65535, value))
            {
                error = "Invalid world server port: " + std::string(valueText);
                return false;
            }

            config.worldServerPort = static_cast<std::uint16_t>(value);
        }
        else if (argument == "--max-sessions")
        {
            unsigned long value = 0;
            if (!ParseUnsigned(valueText, std::numeric_limits<std::uint32_t>::max(), value))
            {
                error = "Invalid max sessions: " + std::string(valueText);
                return false;
            }

            config.maxSessions = static_cast<std::uint32_t>(value);
        }
        else
        {
            error = "Unknown argument: " + argument;
            return false;
        }
    }

    return true;
}

const char* ServerConfig::Usage()
{
    return "Usage: TCPServer.exe [--server-id N] [--world-id N] [--channel-id N] "
           "[--bind-ip IPv4] "
           "[--port N] [--worker-count N] [--master-ip IPv4] "
           "[--master-port N] [--world-server-ip IPv4] [--world-server-port N] "
           "[--max-sessions N] [--skip-db]";
}
