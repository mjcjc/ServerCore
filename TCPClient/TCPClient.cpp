#include "..\Common.h"
#include <atomic>
#include "SocketUtils.h"
#include"PacketHeader.h"
#include"PacketType.h"
char *SERVERIP = (char *)"121.169.113.207";
#define SERVERPORT 9000
#define BUFSIZE    512

int main(int argc, char* argv[])
{
	int retval;

	// 명령행 인수가 있으면 IP 주소로 사용
	if (argc > 1) SERVERIP = argv[1];

	// 윈속 초기화
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
		return 1;

	// 소켓 생성
	SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == INVALID_SOCKET) err_quit("socket()");

	// connect()
	struct sockaddr_in serveraddr;
	memset(&serveraddr, 0, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	inet_pton(AF_INET, SERVERIP, &serveraddr.sin_addr);
	serveraddr.sin_port = htons(SERVERPORT);

	retval = connect(sock, (struct sockaddr*)&serveraddr, sizeof(serveraddr));
	if (retval == SOCKET_ERROR) err_quit("connect()");

	// 데이터 통신에 사용할 변수
	char buf[BUFSIZE + 1];
	int len;

	std::atomic<bool> running = true;

	// 수신 스레드: 서버에서 오는 데이터를 계속 받음
	std::thread recv_thread([&]() {
		while (running)
		{
			PacketHeader header;

			// 1. 헤더 먼저 받기
			if (!SocketUtils::RecvAll(sock, (char*)&header, sizeof(PacketHeader)))
			{
				break;
			}

			// 2. 패킷 크기 검사
			if (header.size < sizeof(PacketHeader))
			{
				printf("\n[오류] 잘못된 패킷 크기입니다.\n");
				break;
			}

			if (header.size > BUFSIZE)
			{
				printf("\n[오류] 패킷 크기가 너무 큽니다.\n");
				break;
			}

			// 3. body 크기 계산
			int bodySize = header.size - sizeof(PacketHeader);

			char recvbuf[BUFSIZE + 1] = {};

			// 4. body 받기
			if (!SocketUtils::RecvAll(sock, recvbuf, bodySize))
			{
				break;
			}

			recvbuf[bodySize] = '\0';

			// 5. 패킷 타입 확인
			if (header.type == static_cast<uint16_t>(PacketType::MESSAGE))
			{
				printf("\n[받은 데이터] %s\n", recvbuf);
				printf("[보낼 데이터] ");
				fflush(stdout);
			}
			else
			{
				printf("\n[오류] 알 수 없는 패킷 타입입니다. type=%d\n", header.type);
			}
		}

		running = false;
		});
	// 송신 루프: 키보드 입력을 받아 서버로 보냄
	while (running) {
		// 데이터 입력
		printf("\n[보낼 데이터] ");
		if (fgets(buf, BUFSIZE + 1, stdin) == NULL)
			break;

		//// '\n' 문자 제거
		//len = (int)strlen(buf);

		//if (len > 0 && buf[len - 1] == '\n')
		//	buf[len - 1] = '\0';
		//PacketHeader header;
		//header.type = static_cast<uint16_t>(PacketType::MESSAGE);
		//header.size = static_cast<uint16_t>(strlen(buf) + sizeof(PacketHeader));

		//// [Header][Body] 형태의 전체 패킷 만들기
		//char packet[BUFSIZE] = {};

		//memcpy(packet, &header, sizeof(PacketHeader));
		//memcpy(packet + sizeof(PacketHeader), buf, bodySize);
		//// 빈 문자열이면 종료
		//if (strlen(buf) == 0)
		//	break;

		//// 데이터 보내기
		//retval = SocketUtils::SendAll(sock, (char*)&header, sizeof(header));
		//if (retval == SOCKET_ERROR) {
		//	err_display("send()");
		//	break;
		//}
		// 빈 문자열이면 종료
		if (strlen(buf) == 0)
			break;

		int bodySize = (int)strlen(buf);
		int packetSize = sizeof(PacketHeader) + bodySize;

		if (packetSize > BUFSIZE)
		{
			printf("[오류] 패킷 크기가 너무 큽니다.\n");
			continue;
		}

		PacketHeader header;
		header.type = static_cast<uint16_t>(PacketType::MESSAGE);
		header.size = static_cast<uint16_t>(packetSize);

		// [Header][Body] 형태의 전체 패킷 만들기
		char packet[BUFSIZE] = {};

		memcpy(packet, &header, sizeof(PacketHeader));
		memcpy(packet + sizeof(PacketHeader), buf, bodySize);

		// 전체 패킷 보내기
		if (!SocketUtils::SendAll(sock, packet, packetSize))
		{
			err_display("SendAll()");
			break;
		}
		printf("[TCP 클라이언트] %d바이트를 보냈습니다.\n", retval);

	}

	// 종료 처리
	running = false;

	// recv()에서 대기 중인 수신 스레드를 깨우기 위해 shutdown
	shutdown(sock, SD_BOTH);

	if (recv_thread.joinable())
		recv_thread.join();

	// 소켓 닫기
	closesocket(sock);

	// 윈속 종료
	WSACleanup();

	return 0;
}
