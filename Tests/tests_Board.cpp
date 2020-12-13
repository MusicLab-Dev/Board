/**
 * @ Author: Matthieu Moinvaziri
 * @ Description: Scheduler unit tests
 */

#include <thread>
#include <atomic>

#include <gtest/gtest.h>

#include <Board/Scheduler.hpp>
#include <Protocol/Packet.hpp>

void printError(const std::string &location)
{
    std::cout << "[Studio]\t" << location << " failed: " << std::strerror(errno) << std::endl;
}

bool initBroadcastSocket(Net::Socket &broadcastSocket)
{
    // opening UDP broadcast socket
    int broadcast = 1;
    broadcastSocket = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (broadcastSocket < 0)
    {
        printError("initBroadcastSocket::socket");
        return false;
    }
    auto ret = ::setsockopt(
        broadcastSocket,
        SOL_SOCKET,
        SO_BROADCAST,
        &broadcast,
        sizeof(broadcast));
    if (ret < 0)
    {
        printError("initBroadcastSocket::setsockopt");
        return false;
    }
    return true;
}

bool emitBroadcastPacket(Net::Socket &broadcastSocket)
{
    sockaddr_in usbBroadcastAddress{
        .sin_family = AF_INET,
        .sin_port = ::htons(420),
        .sin_addr = {
            .s_addr = ::inet_addr("127.0.0.1")}};

    Protocol::DiscoveryPacket packet;
    packet.boardID = static_cast<Protocol::BoardID>(420);
    packet.connectionType = Protocol::ConnectionType::USB;
    packet.distance = 0;

    auto ret = ::sendto(
        broadcastSocket,
        &packet,
        sizeof(packet),
        0,
        reinterpret_cast<const sockaddr *>(&usbBroadcastAddress),
        sizeof(usbBroadcastAddress));
    if (ret < 0)
    {
        printError("emitBroadcastPacket::sendto");
        return false;
    }
    return true;
}

bool initMasterSocket(Net::Socket &masterSocket)
{
    // open master socket
    masterSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (masterSocket < 0)
    {
        printError("initMasterSocket::socket");
        return false;
    }
    // set socket options for master socket
    int enable = 1;
    if (::setsockopt(masterSocket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
    {
        printError("initMasterSocket::setsockopt");
        return false;
    }
    sockaddr_in studioAddress = {
        .sin_family = AF_INET,
        .sin_port = ::htons(421),
        .sin_addr = {
            .s_addr = ::htonl(INADDR_ANY)}};
    // bind master socket to local address
    auto ret = ::bind(
        masterSocket,
        reinterpret_cast<const sockaddr *>(&studioAddress),
        sizeof(studioAddress));
    if (ret < 0)
    {
        printError("initMasterSocket::bind");
        close(masterSocket);
        return false;
    }
    ret = ::listen(masterSocket, 5);
    if (ret < 0)
    {
        printError("initMasterSocket::listen");
        return false;
    }
    return true;
}

bool waitForBoardConnection(const Net::Socket masterSocket, Net::Socket &boardSocket)
{
    sockaddr_in boardAddress{0};
    socklen_t boardAddressLen = sizeof(boardAddress);

    boardSocket = ::accept(
        masterSocket,
        reinterpret_cast<sockaddr *>(&boardAddress),
        &boardAddressLen);
    if (boardSocket < 0)
    {
        printError("waitForBoardConnection::accept");
        return false;
    }
    return true;
}

bool waitForBoardIDRequest(const Net::Socket boardSocket)
{
    using namespace Protocol;

    char buffer[1024];
    const auto ret = ::recv(boardSocket, buffer, 1024, 0);
    if (ret < 0)
    {
        printError("waitForBoardIDRequest::recv");
        return false;
    }
    ReadablePacket requestFromBoard(std::begin(buffer), std::end(buffer));
    if (requestFromBoard.protocolType() == ProtocolType::Connection &&
        requestFromBoard.commandAs<ConnectionCommand>() == ConnectionCommand::IDRequest)
    {
        return true;
    }
    return false;
}

bool sendBoardIDAssignement(const Net::Socket boardSocket)
{
    using namespace Protocol;

    char IDAssignementBuffer[sizeof(WritablePacket::Header) + sizeof(BoardID)];
    WritablePacket IDAssignement(std::begin(IDAssignementBuffer), std::end(IDAssignementBuffer));
    IDAssignement.prepare(ProtocolType::Connection, ConnectionCommand::IDAssignement);
    BoardID id = 123;
    IDAssignement << id;
    const auto ret = ::send(boardSocket, &IDAssignementBuffer, IDAssignement.totalSize(), 0);
    if (ret < 0)
    {
        printError("sendIDAssignementRequest::send");
        return false;
    }
    return true;
}

TEST(Board, Connection)
{
    bool started = false;
    Scheduler scheduler({});
    std::thread thd([&scheduler, &started] {
        started = true;
        scheduler.run();
    });
    while (!started);

    // Studio simulation:

    Net::Socket broadcastSocket{-1};
    Net::Socket masterSocket{-1};
    Net::Socket boardSocket{-1};

    ASSERT_TRUE(initBroadcastSocket(broadcastSocket));
    ASSERT_TRUE(emitBroadcastPacket(broadcastSocket));
    ASSERT_TRUE(initMasterSocket(masterSocket));
    ASSERT_TRUE(waitForBoardConnection(masterSocket, boardSocket));
    ASSERT_TRUE(waitForBoardIDRequest(boardSocket));
    ASSERT_TRUE(sendBoardIDAssignement(boardSocket));

    close(boardSocket);
    close(masterSocket);
    close(broadcastSocket);

    // Stop and join BoardApp
    std::cout << "[Test]\tStopping board thread" << std::endl;
    scheduler.stop();
    if (thd.joinable()) {
        std::cout << "[Test]\tWaiting board thread..." << std::endl;
        thd.join();
        std::cout << "[Test]\tBoard thread has exit" << std::endl;
    }
}

TEST(Board, Disconnection)
{

}
