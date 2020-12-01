/**
 * @ Author: Matthieu Moinvaziri
 * @ Description: Scheduler unit tests
 */

#include <thread>
#include <atomic>

#include <gtest/gtest.h>

#include <Board/Scheduler.hpp>
#include <Protocol/Packet.hpp>

Net::Socket initUsbBroadcastSocket(void)
{
    // opening UDP broadcast socket
    int broadcast = 1;
    Net::Socket usbBroadcastSocket = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (usbBroadcastSocket < 0)
        throw std::runtime_error(std::strerror(errno));
    auto ret = ::setsockopt(
        usbBroadcastSocket,
        SOL_SOCKET,
        SO_BROADCAST,
        &broadcast,
        sizeof(broadcast)
    );
    if (ret < 0)
        throw std::runtime_error(std::strerror(errno));
    return usbBroadcastSocket;
}

void emitUsbBroadcastPacket(Net::Socket usbSocket)
{
    std::cout << "Server::emitBroadcastPacket" << std::endl;

    sockaddr_in usbBroadcastAddress {
        .sin_family = AF_INET,
        .sin_port = ::htons(420),
        .sin_addr = {
            .s_addr = ::inet_addr("169.254.255.255")
        }
    };

    Protocol::DiscoveryPacket packet;
    packet.boardID = static_cast<Protocol::BoardID>(420);
    packet.connectionType = Protocol::ConnectionType::USB;
    packet.distance = 0;

    auto ret = ::sendto(
        usbSocket,
        &packet,
        sizeof(packet),
        0,
        reinterpret_cast<const sockaddr *>(&usbBroadcastAddress),
        sizeof(usbBroadcastAddress)
    );
    if (ret < 0) {
        throw std::runtime_error(std::strerror(errno));
    }
}

TEST(Scheduler, ExternalTestTemplate)
{
    std::atomic<bool> started = false;
    Scheduler scheduler({});
    std::thread thd([&scheduler, &started] {
        started = true;
        scheduler.run();
    });

    // Wait until the thread is started
    while (!started);

    Net::Socket usbSocket = initUsbBroadcastSocket();
    emitUsbBroadcastPacket(usbSocket);

    Net::Socket tcpSocket;
    tcpSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (tcpSocket < 0)
        throw std::runtime_error(std::strerror(errno));

    sockaddr_in studioAddress = {
        .sin_family = AF_INET,
        .sin_port = ::htons(421),
        .sin_addr = {
            .s_addr = ::htonl(INADDR_ANY)
        }
    };
    auto ret = ::bind(
        tcpSocket,
        reinterpret_cast<const sockaddr *>(&studioAddress),
        sizeof(studioAddress)
    );
    if (ret < 0)
        throw std::runtime_error(std::strerror(errno));
    ret = ::listen(tcpSocket, 5);
    if (ret < 0)
        throw std::runtime_error(std::strerror(errno));

    sockaddr_in boardAddress;
    socklen_t boardAddressLen = sizeof(boardAddress);
    Net::Socket boardSocket = ::accept(
        tcpSocket,
        reinterpret_cast<sockaddr *>(&boardAddress),
        &boardAddressLen
    );
    if (boardSocket < 0)
        throw std::runtime_error(std::strerror(errno));

    // Stop and join scheduler
    scheduler.stop();
    if (thd.joinable())
        thd.join();
}

TEST(Scheduler, InternalTestTemplate)
{
    Scheduler scheduler({});

    // Execute internal tests
    scheduler.networkModule().tick(scheduler);
    // ...
}