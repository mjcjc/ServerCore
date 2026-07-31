#include "PacketHandler.h"


static std::atomic<int> UserIdCount{ 1 };


void PacketHandler::HandlePacket(
    Session* session,
    const char* packet)
{
    if (session == nullptr || packet == nullptr)
    {
        return;
    }

    const PacketHeader* header =
        reinterpret_cast<const PacketHeader*>(packet);

    switch (static_cast<PacketType>(header->type))
    {
    case PacketType::LOGIN_REQUEST:
    {
        const LoginRequest* loginRequest =
            reinterpret_cast<const LoginRequest*>(packet);

        HandleLogin(session, loginRequest);
        break;
    }

    case PacketType::CHAT_REQUEST:
    {
        const ChatRequest* chatRequest =
            reinterpret_cast<const ChatRequest*>(packet);

        HandleChat(session, chatRequest);
        break;
    }

    default:
    {
        printf(
            "[오류] 알 수 없는 패킷 타입: %d\n",
            header->type
        );

        break;
    }
    }
}

void PacketHandler::HandleLogin(
    Session* session,
    const LoginRequest* packet)
{
    if (session->GetState() != SessionState::CONNECTED)
    {
        printf(
            "[로그인 실패] 이미 로그인된 세션입니다.\n"
        );

        return;
    }

    if (packet->header.size != sizeof(LoginRequest))
    {
        printf(
            "[로그인 실패] 잘못된 패킷 크기입니다.\n"
        );

        return;
    }

    size_t nicknameLength = strnlen_s(
        packet->nickname,
        sizeof(packet->nickname)
    );

    LoginResponse response{};

    response.header.size =
        static_cast<uint16_t>(sizeof(LoginResponse));

    response.header.type =
        static_cast<uint16_t>(
            PacketType::LOGIN_RESPONSE
            );

    if (nicknameLength == 0 ||
        nicknameLength >= sizeof(packet->nickname))
    {
        response.success = 0;
        response.userId = 0;

        session->Send(
            reinterpret_cast<const char*>(&response),
            sizeof(response)
        );

        printf(
            "[로그인 실패] 잘못된 닉네임입니다.\n"
        );

        return;
    }

    std::string requestedNickname(
        packet->nickname,
        nicknameLength
    );

    if (!session->GetServer()->TrySetNickname(
        session->GetSocket(),
        requestedNickname))
    {
        response.success = 0;
        response.userId = 0;

        session->Send(
            reinterpret_cast<const char*>(&response),
            sizeof(response)
        );

        printf(
            "[로그인 실패] 이미 사용 중인 닉네임: %s\n",
            requestedNickname.c_str()
        );

        return;
    }

    session->SetUserId(UserIdCount.fetch_add(1));

    session->SetState(SessionState::LOGIN);

    response.success = 1;
    response.userId = session->GetId();

    if (!session->Send(
        reinterpret_cast<const char*>(&response),
        sizeof(response)))
    {
        printf(
            "[오류] 로그인 응답 전송 실패\n"
        );

        return;
    }

    printf(
        "[로그인 성공] ID=%d, 닉네임=%s\n",
        session->GetId(),
        session->GetNickname().c_str()
    );
}

void PacketHandler::HandleChat(
    Session* session,
    const ChatRequest* packet)
{
    if (session->GetState() != SessionState::LOGIN)
    {
        printf(
            "[채팅 거부] 로그인하지 않은 세션입니다.\n"
        );

        return;
    }

    if (packet->header.size != sizeof(ChatRequest))
    {
        printf(
            "[채팅 거부] 잘못된 패킷 크기입니다.\n"
        );

        return;
    }

    size_t messageLength = strnlen_s(
        packet->message,
        sizeof(packet->message)
    );

    if (messageLength == 0 ||
        messageLength >= sizeof(packet->message))
    {
        printf(
            "[채팅 거부] 잘못된 메시지입니다.\n"
        );

        return;
    }

    ChatResponse response{};

    response.header.size =
        static_cast<uint16_t>(
            sizeof(ChatResponse)
            );

    response.header.type =
        static_cast<uint16_t>(
            PacketType::CHAT_RESPONSE
            );
    response.userId = session->GetId();

    strncpy_s(
        response.nickname,
        sizeof(response.nickname),
        session->GetNickname().c_str(),
        _TRUNCATE
    );

    strncpy_s(
        response.message,
        sizeof(response.message),
        packet->message,
        _TRUNCATE
    );

    printf(
        "[받은 채팅] %s(%d): %s\n",
        response.nickname,
        response.userId,
        response.message
    );

    session->GetServer()->Broadcast(
        session,
        reinterpret_cast<const char*>(&response),
        sizeof(response)
    );
}