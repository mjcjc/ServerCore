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
# MasterServer 등록 구조

`MasterServer`는 `127.0.0.1:6000`에서 GameServer 관리 연결을 받는다.
각 GameServer는 시작할 때 자신의 ID, 접속 주소, 수용 인원을 등록하고 2초마다
현재 접속자 수를 Heartbeat로 보낸다. MasterServer는 5초 동안 Heartbeat가 없으면
해당 서버를 비활성 상태로 변경한다.

`TCPClient`는 GameServer에 직접 접속하지 않고 먼저 MasterServer에 배정을 요청한다.
MasterServer는 활성 상태이면서 수용 인원이 남은 서버 중 현재 접속자 수가 가장
적은 서버를 선택해 ID, IP, 포트를 반환한다. 접속자 수가 같으면 서버 ID가 작은
서버를 우선한다.

```text
TCPClient ── 배정 요청 ──> MasterServer
TCPClient <── 1번 서버(127.0.0.1:7001) ── MasterServer
TCPClient ── 로그인/채팅 접속 ──> GameServer 1
```

```text
GameServer 1 (7001) ─┐
GameServer 2 (7002) ─┼─ 등록 / Heartbeat ─> MasterServer (6000)
GameServer 3 (7003) ─┘
```

빌드 후 전체 로컬 클러스터 실행:

```powershell
.\scripts\Start-ServerCluster.ps1
```

이미 실행 중인 포트는 중복 실행하지 않는다. 전체 로컬 클러스터를 종료하려면:

```powershell
.\scripts\Stop-ServerCluster.ps1
```
