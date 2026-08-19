# Multi-process Game Servers

The same `TCPServer.exe` can run as independent game-server processes. Each
instance receives its identity and endpoint through command-line arguments.
Each process uses four IOCP workers by default; `--worker-count N` can tune it
without multiplying the machine's entire logical CPU count per process.

```powershell
TCPServer.exe --server-id 1 --bind-ip 127.0.0.1 --port 7001 --skip-db
TCPServer.exe --server-id 2 --bind-ip 127.0.0.1 --port 7002 --skip-db
TCPServer.exe --server-id 3 --bind-ip 127.0.0.1 --port 7003 --skip-db
```

`--skip-db` is intended only for isolated local network and multi-process
tests. Omit it when database configuration is available.

After building the x64 Debug configuration, all three instances can be started
with:

```powershell
.\scripts\Start-GameServers.ps1
```

The launcher also adds the locally installed MySQL Connector/C++ runtime
directory to child-process `PATH`. Use `-ConnectorDllDirectory` if Connector
9.5 is installed elsewhere.

Run the stress client against a specific instance:

```powershell
TCPStressClient.exe 127.0.0.1 7001 100 10 10 30
TCPStressClient.exe 127.0.0.1 7002 100 10 10 30
TCPStressClient.exe 127.0.0.1 7003 100 10 10 30
```

Run clients against all three instances concurrently:

```powershell
.\scripts\Test-MultiGameServers.ps1
```

The default test connects 20 virtual clients to each server, sends load from
four clients per server for five seconds, and writes separate result logs under
`test-results\multi-game-server`.

At this stage each process owns independent sessions and chat state. Dynamic
registration, health checks, and load-aware routing will be added later through
the Master Server.
