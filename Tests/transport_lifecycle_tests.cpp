#include "groovymister.h"

#include <atomic>
#include <cstring>
#include <thread>

namespace
{
int failures = 0;

void Check(bool condition)
{
    if (!condition)
        ++failures;
}

void CheckRioCompletionDraining()
{
    WSADATA winsock = {};
    Check(WSAStartup(MAKEWORD(2, 2), &winsock) == 0);
    SOCKET server = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    Check(server != INVALID_SOCKET);
    if (server == INVALID_SOCKET)
    {
        WSACleanup();
        return;
    }

    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    Check(bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    int addressSize = sizeof(address);
    Check(getsockname(server, reinterpret_cast<sockaddr*>(&address), &addressSize) == 0);
    const DWORD timeout = 2000;
    setsockopt(server, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));

    std::atomic<unsigned> blits = 0;
    std::atomic<unsigned> payloads = 0;
    std::thread endpoint([&]() {
        uint8_t packet[2048];
        sockaddr_in peer = {};
        int peerSize = sizeof(peer);
        for (;;)
        {
            const int received = recvfrom(
                server,
                reinterpret_cast<char*>(packet),
                sizeof(packet),
                0,
                reinterpret_cast<sockaddr*>(&peer),
                &peerSize);
            if (received <= 0)
                break;
            if (received == 1 && packet[0] == 1)
                break;
            if (received == 5 && packet[0] == 2)
            {
                uint8_t ack[13] = {};
                sendto(
                    server,
                    reinterpret_cast<const char*>(ack),
                    sizeof(ack),
                    0,
                    reinterpret_cast<sockaddr*>(&peer),
                    peerSize);
            }
            else if (received == 8 && packet[0] == 7)
            {
                uint32_t frame = 0;
                uint16_t syncLine = 0;
                std::memcpy(&frame, packet + 1, sizeof(frame));
                std::memcpy(&syncLine, packet + 6, sizeof(syncLine));
                uint8_t ack[13] = {};
                std::memcpy(ack, &frame, sizeof(frame));
                std::memcpy(ack + 4, &syncLine, sizeof(syncLine));
                std::memcpy(ack + 6, &frame, sizeof(frame));
                ack[12] = 0x84; // VRAM synced and queued.
                sendto(
                    server,
                    reinterpret_cast<const char*>(ack),
                    sizeof(ack),
                    0,
                    reinterpret_cast<sockaddr*>(&peer),
                    peerSize);
                ++blits;
            }
            else if (received == 12)
            {
                ++payloads;
            }
        }
    });

    constexpr uint32_t FrameCount = 300;
    {
        GroovyMister transport;
        Check(transport.CmdInit(
            "127.0.0.1", ntohs(address.sin_port), 0, 0, 0, 0, 1500) == 0);
        transport.CmdSwitchres(1.0, 2, 2, 2, 4, 2, 2, 2, 5, 0);
        for (uint32_t frame = 1; frame <= FrameCount; ++frame)
        {
            const ULONGLONG deadline = GetTickCount64() + 1000;
            while (!transport.CanWriteBlitBuffer(0) && GetTickCount64() < deadline)
                SwitchToThread();
            Check(transport.CanWriteBlitBuffer(0));
            std::memset(transport.getPBufferBlit(0), 42, 12);
            transport.CmdBlit(frame, 0, 2, 15000, 0);
            transport.WaitSync();
        }
        transport.CmdClose();
        transport.CmdClose();
    }

    endpoint.join();
    Check(blits == FrameCount);
    Check(payloads == FrameCount);
    closesocket(server);
    WSACleanup();
}
}

int RunTransportLifecycleTests()
{
    // INIT with no endpoint must time out, cancel its pending RIO operations,
    // and tolerate explicit plus destructor cleanup without touching resources twice.
    {
        GroovyMister transport;
        Check(transport.CmdInit("127.0.0.1", 65534, 1, 0, 0, 0, 1500) == -1);
        transport.CmdClose();
        transport.CmdClose();
    }
    {
        GroovyMister transport;
        Check(transport.CmdInit("127.0.0.1", 65534, 1, 0, 0, 0, 1500) == -1);
    }
    CheckRioCompletionDraining();
    return failures;
}
