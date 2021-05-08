/**
 * @ Author: Matthieu Moinvaziri
 * @ Description: Scheduler unit tests
 */

#include <thread>
#include <atomic>
#include <chrono>

#include <gtest/gtest.h>

#include <Board/Scheduler.hpp>
#include <Protocol/Packet.hpp>

static constexpr Net::Port LexoPort = 4242;

void printError(const std::string &location)
{
    std::cout << "[Studio]\t" << location << " failed: " << std::strerror(errno) << std::endl;
}

bool initBroadcastSocket(Net::Socket &broadcastSocket)
{
    // opening UDP broadcast socket
    int broadcast = 1;
    broadcastSocket = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (broadcastSocket < 0) {
        printError("initBroadcastSocket::socket");
        return false;
    }
    auto ret = ::setsockopt(
        broadcastSocket,
        SOL_SOCKET,
        SO_BROADCAST,
        &broadcast,
        sizeof(broadcast)
    );
    if (ret < 0) {
        printError("initBroadcastSocket::setsockopt");
        return false;
    }
    return true;
}

bool emitBroadcastPacket(Net::Socket &broadcastSocket)
{
    sockaddr_in usbBroadcastAddress;
    std::memset(&usbBroadcastAddress, 0, sizeof(usbBroadcastAddress));
    usbBroadcastAddress.sin_family = AF_INET;
    usbBroadcastAddress.sin_port = ::htons(LexoPort);
    usbBroadcastAddress.sin_addr.s_addr = ::inet_addr("127.0.0.1");

    Protocol::DiscoveryPacket packet;
    std::memset(&packet, 0, sizeof(packet));
    packet.magicKey = Protocol::SpecialLabMagicKey;
    packet.boardID = static_cast<Protocol::BoardID>(LexoPort);
    packet.connectionType = Protocol::ConnectionType::USB;
    packet.distance = 0;

    auto ret = ::sendto(
        broadcastSocket,
        &packet,
        sizeof(packet),
        0,
        reinterpret_cast<const sockaddr *>(&usbBroadcastAddress),
        sizeof(usbBroadcastAddress)
    );
    if (ret < 0) {
        printError("emitBroadcastPacket::sendto");
        return false;
    }
    return true;
}

bool initMasterSocket(Net::Socket &masterSocket)
{
    // open master socket
    masterSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (masterSocket < 0) {
        printError("initMasterSocket::socket");
        return false;
    }
    // set socket options for master socket
    int enable = 1;
    if (::setsockopt(masterSocket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        printError("initMasterSocket::setsockopt");
        return false;
    }
    sockaddr_in studioAddress;
    std::memset(&studioAddress, 0, sizeof(studioAddress));
    studioAddress.sin_family = AF_INET;
    studioAddress.sin_port = ::htons(LexoPort + 1);
    studioAddress.sin_addr.s_addr = ::htonl(INADDR_ANY);

    // bind master socket to local address
    auto ret = ::bind(
        masterSocket,
        reinterpret_cast<const sockaddr *>(&studioAddress),
        sizeof(studioAddress)
    );
    if (ret < 0) {
        printError("initMasterSocket::bind");
        close(masterSocket);
        return false;
    }
    ret = ::listen(masterSocket, 5);
    if (ret < 0) {
        printError("initMasterSocket::listen");
        return false;
    }
    return true;
}

bool waitForBoardConnection(const Net::Socket masterSocket, Net::Socket &boardSocket)
{
    sockaddr_in boardAddress;
    std::memset(&boardAddress, 0, sizeof(boardAddress));
    socklen_t boardAddressLen = sizeof(boardAddress);

    boardSocket = ::accept(
        masterSocket,
        reinterpret_cast<sockaddr *>(&boardAddress),
        &boardAddressLen
    );
    if (boardSocket < 0) {
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
    if (ret < 0) {
        printError("waitForBoardIDRequest::recv");
        return false;
    }
    ReadablePacket requestFromBoard(std::begin(buffer), std::end(buffer));
    if (requestFromBoard.protocolType() == ProtocolType::Connection &&
            requestFromBoard.commandAs<ConnectionCommand>() == ConnectionCommand::IDAssignment) {
        return true;
    }
    return false;
}

bool sendBoardIDAssignment(const Net::Socket boardSocket)
{
    using namespace Protocol;

    char IDAssignmentBuffer[sizeof(WritablePacket::Header) + sizeof(BoardID)];
    WritablePacket IDAssignment(std::begin(IDAssignmentBuffer), std::end(IDAssignmentBuffer));
    IDAssignment.prepare(ProtocolType::Connection, ConnectionCommand::IDAssignment);
    BoardID id = 123;
    IDAssignment << id;
    const auto ret = ::send(boardSocket, &IDAssignmentBuffer, IDAssignment.totalSize(), 0);
    if (ret < 0) {
        printError("sendIDAssignmentRequest::send");
        return false;
    }
    return true;
}

void slaveBoardEntry(void)
{
    std::cout << "[Slave]\tSlave board thread launched" << std::endl;

    using namespace Protocol;

    sockaddr_in masterBoardAddress;
    std::memset(&masterBoardAddress, 0, sizeof(masterBoardAddress));
    masterBoardAddress.sin_family = AF_INET;
    masterBoardAddress.sin_port = ::htons(420);
    masterBoardAddress.sin_addr.s_addr = ::inet_addr("0.0.0.0");

    Net::Socket masterBoardSocket = ::socket(AF_INET, SOCK_STREAM, 0);
    auto ret = ::connect(
        masterBoardSocket,
        reinterpret_cast<const sockaddr *>(&masterBoardAddress),
        sizeof(masterBoardAddress)
    );
    if (ret < 0) {
        std::cout << "[Slave]\tslaveBoardEntry::connect failed: " << strerror(errno) << std::endl;
        return;
    }

    // Self ID request test

    char requestBuffer[sizeof(WritablePacket::Header) + sizeof(BoardID)];
    std::memset(&requestBuffer, 0, sizeof(requestBuffer));
    WritablePacket requestPacket(std::begin(requestBuffer), std::end(requestBuffer));
    requestPacket.prepare(ProtocolType::Connection, ConnectionCommand::IDAssignment);
    requestPacket << BoardID(0u);

    if (!send(masterBoardSocket, &requestBuffer, requestPacket.totalSize(), 0)) {
        std::cout << "[Slave]\tslaveBoardEntry::send failed: " << std::strerror(errno) << std::endl;
        return;
    }

    // Waiting for an ID assignment response

    std::cout << "[Slave]\tWaiting for an ID assignment response..." << std::endl;

    char responseBuffer[256];
    std::memset(&responseBuffer, 0, sizeof(responseBuffer));
    const auto dataSize = ::read(masterBoardSocket, &responseBuffer, sizeof(responseBuffer));
    if (dataSize < 0) {
        std::cout << "[Slave]\tinitNewMasterConnection::read failed: " << std::strerror(errno) << std::endl;
        return;
    }

    std::cout << "[Slave]\tID assignment response received !" << std::endl;

    close(masterBoardSocket);

    // while (1) { }

    // Slave ID request forwarding test

    // char fakeTransferBuffer[sizeof(WritablePacket::Header) + 2 * sizeof(BoardID)];

    // WritablePacket slaveRequest(std::begin(fakeTransferBuffer), std::end(fakeTransferBuffer));
    // slaveRequest.prepare(ProtocolType::Connection, ConnectionCommand::IDAssignment);
    // slaveRequest.pushFootprint(2);
    // slaveRequest.pushFootprint(42);

    // if (!send(masterBoardSocket, &fakeTransferBuffer, slaveRequest.totalSize(), 0)) {
    //     std::cout << "[Slave]\tslaveBoardEntry::send failed: " << std::strerror(errno) << std::endl;
    //     return;
    // }

    std::cout << "[Slave]\tExit slave board thread" << std::endl;
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

    Net::Socket broadcastSocket { -1 };
    Net::Socket masterSocket { -1 };
    Net::Socket boardSocket { -1 };

    ASSERT_TRUE(initBroadcastSocket(broadcastSocket));
    ASSERT_TRUE(emitBroadcastPacket(broadcastSocket));
    ASSERT_TRUE(initMasterSocket(masterSocket));
    ASSERT_TRUE(waitForBoardConnection(masterSocket, boardSocket));
    ASSERT_TRUE(waitForBoardIDRequest(boardSocket));
    ASSERT_TRUE(sendBoardIDAssignment(boardSocket));

    std::thread slaveBoard(slaveBoardEntry);

    using namespace Protocol;

    char buffer[256];
    std::memset(&buffer, 0, sizeof(buffer));

    const auto ret = ::recv(boardSocket, &buffer, 1024, 0);
    std::cout << "[Test]\tReceived " << ret << " bytes from board" << std::endl;

    ReadablePacket requestPacket(&buffer, &buffer + ret);

    char responseBuffer[256];
    std::memset(&responseBuffer, 0, sizeof(responseBuffer));

    WritablePacket responsePacket(&responseBuffer, &responseBuffer + requestPacket.totalSize());
    responsePacket = requestPacket;

    responsePacket.popFrontStack();

    *(responsePacket.data()) = BoardID(92);

    send(boardSocket, &responseBuffer, responsePacket.totalSize(), 0);

    std::chrono::seconds delay(5);
    std::this_thread::sleep_for(delay);

    close(boardSocket);
    close(masterSocket);
    close(broadcastSocket);

    // Stop and join slave board thread
    std::cout << "[Test]\tStopping slave board thread" << std::endl;
    if (slaveBoard.joinable()) {
        slaveBoard.join();
        std::cout << "[Test]\tSlave board thread has exit" << std::endl;
    }

    // Stop and join master board thread
    std::cout << "[Test]\tStopping master board thread" << std::endl;
    scheduler.stop();
    if (thd.joinable()) {
        thd.join();
        std::cout << "[Test]\tMaster board thread has exit" << std::endl;
    }
}
