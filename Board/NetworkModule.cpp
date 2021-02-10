/**
 * @ Author: Paul Creze
 * @ Description: Network module
 */

#include <iostream>
#include <cstring>
#include <cerrno>

#include "Scheduler.hpp"

NetworkModule::NetworkModule(void) : _networkBuffer(NetworkBufferSize)
{
    std::cout << "[Board]\tNetworkModule constructor" << std::endl;

    // Open UDP broadcast socket
    int broadcast = 1;
    _udpBroadcastSocket = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (_udpBroadcastSocket < 0)
        throw std::runtime_error(std::strerror(errno));
    auto ret = ::setsockopt(
        _udpBroadcastSocket,
        SOL_SOCKET,
        SO_BROADCAST,
        &broadcast,
        sizeof(broadcast)
    );
    if (ret < 0)
        throw std::runtime_error(std::strerror(errno));
    if (tryToBindUdp())
        _isBinded = true;

    // Open TCP slaves socket in non-blocking mode (O_NONBLOCK)
    _slavesSocket = socket(AF_INET, SOCK_STREAM | O_NONBLOCK, 0);
    if (_slavesSocket < 0)
        throw std::runtime_error(std::strerror(errno));
    sockaddr_in localAddress = {
        .sin_family = AF_INET,
        .sin_port = ::htons(420),
        .sin_addr = {
            .s_addr = ::htonl(INADDR_ANY)
        }
    };
    // bind localAddress to _slavesSocket
    ret = ::bind(
        _slavesSocket,
        reinterpret_cast<const sockaddr *>(&localAddress),
        sizeof(localAddress)
    );
    if (ret < 0) {
        close(_slavesSocket);
        throw std::runtime_error(std::strerror(errno));
    }
    ret = ::listen(_slavesSocket, 5);
    if (ret < 0)
        throw std::runtime_error(std::strerror(errno));

    // Initialize network buffer
    std::memset(_networkBuffer.data(), 0u, NetworkBufferSize);
}

NetworkModule::~NetworkModule(void)
{
    std::cout << "[Board]\tNetworkModule destructor" << std::endl;

    close(_udpBroadcastSocket);
    if (_masterSocket != -1) {
        shutdown(_masterSocket, SHUT_RDWR);
        close(_masterSocket);
    }
}

bool NetworkModule::tryToBindUdp(void)
{
    // Define UDP broadcast address
    const std::string &broadcastAddress = confTable.get("BroadcastAddress", "NotFound").c_str();
    sockaddr_in udpBroadcastAddress {
        .sin_family = AF_INET,
        .sin_port = ::htons(420),
        .sin_addr = {
            .s_addr = ::inet_addr(broadcastAddress == "NotFound" ? "127.0.0.1" : broadcastAddress.c_str())
        }
    };

    const auto ret = ::bind(
        _udpBroadcastSocket,
        reinterpret_cast<const sockaddr *>(&udpBroadcastAddress),
        sizeof(udpBroadcastAddress)
    );

    if (ret < 0 && (errno == 13 || errno == 98))
        throw std::runtime_error(std::strerror(errno));
    if (ret < 0) {
        std::cout << "[Board]\ttryToBindUdp: UDP broadcast address does not exist..." << std::endl;
    }
    return ret == 0;
}

void NetworkModule::notifyDisconnectionToClients(void)
{
    std::cout << "[Board]\tNetworkModule::notifyDisconnectionToClients" << std::endl;

    if (_clients.empty()) {
        std::cout << "[Board]\tNo client board to notify..." << std::endl;
        return;
    }
    for (const auto client : _clients) {
        close(client.socket);
    }
    _clients.clear();
}

void NetworkModule::processAssignmentFromMaster(Protocol::ReadablePacket &&assignmentPacket)
{
    std::cout << "[Board]\tNetworkModule::processAssignmentFromMaster" << std::endl;

    using namespace Protocol;

    if (assignmentPacket.footprintStackSize() == 1) {

        char buffer[256];
        std::memset(&buffer, 0, sizeof(buffer));
        WritablePacket forwardPacket(&buffer, &buffer + sizeof(WritablePacket::Header) + assignmentPacket.payload());
        forwardPacket = assignmentPacket;

        BoardID temporaryAssignedID = forwardPacket.popFrontStack();
        BoardID clientNewID = assignmentPacket.extract<BoardID>();

        std::cout << "[Board]\tID assignment packet is for direct client with temporary ID = " << (int)temporaryAssignedID << std::endl;

        for (auto &clientBoard : _clients) {
            if (clientBoard.id != temporaryAssignedID)
                continue;
            if (!::send(clientBoard.socket, &forwardPacket, forwardPacket.totalSize(), 0)) {
                std::cout << "[Board]\tprocessAssignmentFromMaster::send failed: " << std::strerror(errno) << std::endl;
                return;
            }
            clientBoard.id = clientNewID;
            std::cout << "[Board]\tDirect client get final ID of " << (int)clientBoard.id << " assigned by studio" << std::endl;
            return;
        }
    }
}

void NetworkModule::processMaster(Scheduler &scheduler)
{
    std::cout << "[Board]\tNetworkModule::processMaster" << std::endl;

    using namespace Protocol;

    char buffer[1024];
    std::memset(&buffer, 0, sizeof(buffer));
    // Proccess master input, read must be non-blocking
    const auto ret = ::read(_masterSocket, &buffer, sizeof(buffer));
    if (ret == 0) {
        std::cout << "[Board]\tDisconnected from master" << std::endl;
        close(_masterSocket);
        _masterSocket = -1;
        _boardID = 0u;
        _connectionType = Protocol::ConnectionType::None;
        _nodeDistance = 0u;
        scheduler.setState(Scheduler::State::Disconnected);
        notifyDisconnectionToClients();
    }
    else if (ret < 0) {
        std::cout << "[Board]\tError reading data from master" << std::endl;
        return;
    }
    std::cout << "[Board]\tReceived " << ret << " bytes from master" << std::endl;

    char *bufferPtr = &buffer[0];
    while (1) {
        auto *packetHeader = reinterpret_cast<ReadablePacket::Header *>(bufferPtr);
        if (packetHeader->magicKey != SpecialLabMagicKey) {
            std::cout << "[Board]\tNo new packet from master to process..." << std::endl;
            break;
        }
        std::size_t packetPayload = packetHeader->payload;

        /* Handle master packet command */
        if (packetHeader->protocolType == ProtocolType::Connection &&
                packetHeader->command == static_cast<std::uint16_t>(ConnectionCommand::IDAssignment)) {
            std::cout << "[Board]\tProcessing an assignment packet from master of size " << sizeof(ReadablePacket::Header) + packetPayload << std::endl;
            processAssignmentFromMaster(ReadablePacket(bufferPtr, bufferPtr + sizeof(ReadablePacket::Header) + packetPayload));
        }
        /* others cases ... */

        bufferPtr += sizeof(ReadablePacket::Header) + packetPayload;
    }
}

void NetworkModule::tick(Scheduler &scheduler) noexcept
{
    std::cout << "[Board]\tNetworkModule::tick" << std::endl;

    if (scheduler.state() != Scheduler::State::Connected) // If connected, process master and clients
        return;

    processMaster(scheduler);

    if (scheduler.state() != Scheduler::State::Connected) // If master has been disconnected, return
        return;

    proccessNewClientConnections(scheduler);

    readClients(scheduler); // 1
    processClientsData(scheduler); // 2
    if (_networkBuffer.size() == 0) {
        std::cout << "[Board]\tNo data to transfer to master" << std::endl;
        return;
    }
    transferToMaster(scheduler); // 3
}

void NetworkModule::discover(Scheduler &scheduler) noexcept
{
    std::cout << "[Board]\tNetworkModule::discover" << std::endl;

    // Check if the broadcast socket is binded
    if (!_isBinded) {
        if (!tryToBindUdp())
            return;
        _isBinded = true;
    }

    // Emit broadcast packet only if board is connected
    if (scheduler.state() == Scheduler::State::Connected)
        discoveryEmit(scheduler);
    discoveryScan(scheduler);
}

void NetworkModule::startIDRequestToMaster(const Endpoint &masterEndpoint, Scheduler &scheduler)
{
    using namespace Protocol;

    char requestBuffer[sizeof(WritablePacket::Header) + sizeof(BoardID)];
    WritablePacket requestPacket(std::begin(requestBuffer), std::end(requestBuffer));
    requestPacket.prepare(ProtocolType::Connection, ConnectionCommand::IDAssignment);
    requestPacket << BoardID(0u);

    // Send ID assignment to master
    std::cout << "[Board]\tSending ID assignment packet..." << std::endl;
    if (!send(_masterSocket, &requestBuffer, requestPacket.totalSize(), 0)) {
        std::cout << "[Board]\tinitNewMasterConnection::send failed: " << std::strerror(errno) << std::endl;
        return;
    }

    std::memset(&requestBuffer, 0, sizeof(requestBuffer));

    // Wait for ID assignation from master
    std::cout << "[Board]\tWaiting for ID assignment packet from master..." << std::endl;
    auto ret = ::read(_masterSocket, &requestBuffer, sizeof(requestBuffer));
    if (ret < 0) {
        std::cout << "[Board]\tinitNewMasterConnection::read failed: " << std::strerror(errno) << std::endl;
        return;
    }

    ReadablePacket responsePacket(std::begin(requestBuffer), std::end(requestBuffer));
    if (responsePacket.protocolType() != ProtocolType::Connection ||
            responsePacket.commandAs<ConnectionCommand>() != ConnectionCommand::IDAssignment) {
                std::cout << "[Board]\tInvalid ID assignment packet..." << std::endl;
                return;
    }

    _boardID = responsePacket.extract<BoardID>();
    std::cout << "[Board]\tAssigned BoardID from master: " << static_cast<int>(_boardID) << std::endl;

    // Only if ID assignment is done correctly
    _connectionType = masterEndpoint.connectionType;
    _nodeDistance = masterEndpoint.distance;
    scheduler.setState(Scheduler::State::Connected);

    fcntl(_masterSocket, F_SETFL, O_NONBLOCK);
}

void NetworkModule::initNewMasterConnection(const Endpoint &masterEndpoint, Scheduler &scheduler) noexcept
{
    std::cout << "[Board]\tNetworkModule::initNewMasterConnection" << std::endl;

    if (_masterSocket != -1) {
        ::close(_masterSocket);
        _masterSocket = -1;
    }
    _masterSocket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (_masterSocket < 0) {
        std::cout << "[Board]\tinitNewMasterConnection::socket failed: " << strerror(errno) << std::endl;
        return;
    }
    sockaddr_in masterAddress = {
        .sin_family = AF_INET,
        .sin_port = ::htons(421),
        .sin_addr = {
            .s_addr = masterEndpoint.address
        }
    };
    auto ret = ::connect(
        _masterSocket,
        reinterpret_cast<const sockaddr *>(&masterAddress),
        sizeof(masterAddress)
    );
    if (ret < 0) {
        std::cout << "[Board]\tinitNewMasterConnection::connect failed: " << strerror(errno) << std::endl;
        return;
    }

    std::cout << "[Board]\tConnected to studio master socket" << std::endl;
    startIDRequestToMaster(masterEndpoint, scheduler);
    return;
}

void NetworkModule::analyzeUdpEndpoints(const std::vector<Endpoint> &udpEndpoints, Scheduler &scheduler) noexcept
{
    std::cout << "[Board]\tNetworkModule::analyzeUdpEndpoints" << std::endl;

    std::size_t index = 0;
    std::size_t i = 0;

    for (const auto &endpoint : udpEndpoints) {
        if (udpEndpoints.at(index).connectionType != Protocol::ConnectionType::USB &&
            endpoint.connectionType == Protocol::ConnectionType::USB) {
            index = i;
        } else if (endpoint.distance < udpEndpoints.at(index).distance) {
            index = i;
        }
        i++;
    }
    if ((_connectionType != Protocol::ConnectionType::USB &&
        udpEndpoints.at(index).connectionType == Protocol::ConnectionType::USB) ||
        udpEndpoints.at(index).distance + 1 < _nodeDistance) {
        std::cout << "[Board]\tNew endpoint found for studio connection" << std::endl;
        initNewMasterConnection(udpEndpoints.at(index), scheduler);
    }
}

void NetworkModule::discoveryScan(Scheduler &scheduler)
{
    std::cout << "[Board]\tNetworkModule::discoveryScan" << std::endl;

    sockaddr_in udpSenderAddress;
    int udpSenderAddressLength = sizeof(udpSenderAddress);

    Protocol::DiscoveryPacket packet;
    std::vector<Endpoint> udpEndpoints;

    while (1) {
        const auto size = ::recvfrom(
            _udpBroadcastSocket,
            &packet,
            sizeof(Protocol::DiscoveryPacket),
            MSG_WAITALL | MSG_DONTWAIT,
            reinterpret_cast<sockaddr *>(&udpSenderAddress),
            reinterpret_cast<socklen_t *>(&udpSenderAddressLength)
        );
        std::cout << "[Board]\tNetworkModule::discoveryScan::recvfrom: " << size << std::endl;

        // Loop end condition, until read buffer is empty
        if (size < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            std::cout << "[Board]\tNetworkModule::discoveryScan: nothing remaining on the socket" << std::endl;
            if (scheduler.state() != Scheduler::State::Connected && !udpEndpoints.empty())
                analyzeUdpEndpoints(udpEndpoints, scheduler);
            return;
        } else if (size < 0)
            throw std::runtime_error(std::strerror(errno));

        // Ignore unknown and self broadcasted packets
        if (packet.magicKey != Protocol::SpecialLabMagicKey || packet.boardID == _boardID) {
            std::cout << "[Board]\tNetworkModule::discoveryScan: ignoring packet" << std::endl;
            continue;
        }

        // Debug
        char senderAddressString[100];
        std::memset(senderAddressString, 0, 100);
        ::inet_ntop(AF_INET, &(udpSenderAddress.sin_addr), senderAddressString, 100);
        std::cout << "[Board]\tNetworkModule::discoveryScan: UDP DiscoveryPacket received from " << senderAddressString << std::endl;

        Endpoint endpoint {
            .address = udpSenderAddress.sin_addr.s_addr,
            .connectionType = packet.connectionType,
            .distance = packet.distance
        };
        udpEndpoints.push_back(endpoint);
    }
}

void NetworkModule::discoveryEmit(Scheduler &scheduler) noexcept
{
    std::cout << "[Board]\tNetworkModule::discoveryEmit" << std::endl;

    Protocol::DiscoveryPacket packet = {
        .magicKey = Protocol::SpecialLabMagicKey,
        .boardID = _boardID,
        .connectionType = _connectionType,
        .distance = _nodeDistance
    };

    const std::string &broadcastAddress = confTable.get("BroadcastAddress", "NotFound").c_str();
    sockaddr_in udpBroadcastAddress {
        .sin_family = AF_INET,
        .sin_port = ::htons(420),
        .sin_addr = {
            .s_addr = ::inet_addr(broadcastAddress == "NotFound" ? "127.0.0.1" : broadcastAddress.c_str())
        }
    };
    const auto ret = ::sendto(
        _udpBroadcastSocket,
        &packet,
        sizeof(Protocol::DiscoveryPacket),
        0,
        reinterpret_cast<const sockaddr *>(&udpBroadcastAddress),
        sizeof(udpBroadcastAddress)
    );
    if (ret < 0) {
        std::cout << "[Board]\t NetworkModule::discoveryEmit::sendto failed: " << std::strerror(errno) << std::endl;
    }
}

void NetworkModule::proccessNewClientConnections(Scheduler &scheduler)
{
    std::cout << "[Board]\tNetworkModule::proccessNewClientConnections" << std::endl;

    sockaddr_in clientAddress { 0 };
    socklen_t boardAddressLen = sizeof(clientAddress);

    while (1) {
        // Accept a new board connection and retrieve the address
        Net::Socket clientSocket = ::accept(
            _slavesSocket,
            reinterpret_cast<sockaddr *>(&clientAddress),
            &boardAddressLen
        );
        // Loop end condition, until all pending connection are proccessed
        if (clientSocket < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            std::cout << "[Board]\tNo new client board connection to proccess..." << std::endl;
            return;
        } else if (clientSocket < 0)
            throw std::runtime_error(std::strerror(errno));

        std::cout << "[Board]\tNew board connection from " << inet_ntoa(clientAddress.sin_addr) << ":" << ntohs(clientAddress.sin_port) << std::endl;

        // Filling new client struct
        Client clientBoard = {
            .socket = clientSocket,
            .address = clientAddress.sin_addr.s_addr,
            .port = clientAddress.sin_port,
            .id = 0u
        };
        _clients.push(clientBoard);
    }
}

void NetworkModule::readClients(Scheduler &scheduler)
{
    std::cout << "[Board]\tNetworkModule::readClients" << std::endl;

    // Return if there is no client board(s)
    if (_clients.empty()) {
        std::cout << "[Board]\tNo connected board to proccess..." << std::endl;
        return;
    }

    std::size_t assignIndex { AssignOffset };
    std::size_t inputsIndex { InputsOffset };

    for (auto client = _clients.begin(); client != _clients.end(); ) {

        std::cout << "[Board]\tProccessing client: "
        << inet_ntoa(in_addr { client->address })
        << ":" << ntohs(client->port)
        << " with boardID = " << static_cast<int>(client->id) << std::endl;

        if (client->id == 0) {
            processClientAssignmentRequest(client, assignIndex);    // Client in "assign mode"
        } else
            readDataFromClient(client, inputsIndex);                // Client in "inputs mode"

        if (client != _clients.end())
            client++;
    }
}

void NetworkModule::processClientsData(Scheduler &scheduler)
{
    std::cout << "[Board]\tNetworkModule::processClientsData" << std::endl;

    /* Processing all data in 4 steps : */
    /* PRIORITY ORDER: slaves assigns, self assigns, slave events, self events */
    /* Note: All "Assigns" packets have to be process one by one to add footprint when "events" can be transfered directly */

    // 1 - Extract the "slaves assigns" from the "slaves data" area in the reception buffer & add footprint & store in transfer buffer.
    // 2 - Extract "self assigns" from "self assigns" area in the reception buffer & add footprint & store in transfer buffer.
    // 3 - Copy all the "slaves events" from the "slaves data" area (all remaining data) into the transfer buffer.
    // 4 - Copy all "self events" from the HardwareModule into the transfer buffer

    using namespace Protocol;

    std::uint8_t *transferBufferPtr = _networkBuffer.data();
    std::uint8_t *slavesDataPtr = _networkBuffer.data() + InputsOffset;

    // STEP 1 : "slaves assigns"

    std::size_t transferBufferSize = 0u;

    while (1) {
        auto *packetHeader = reinterpret_cast<ReadablePacket::Header *>(slavesDataPtr);
        std::size_t packetPayload = packetHeader->payload;

        if (packetHeader->magicKey != SpecialLabMagicKey ||
                !(packetHeader->protocolType == ProtocolType::Connection &&
                packetHeader->command == static_cast<std::uint16_t>(ConnectionCommand::IDAssignment))) {
            std::cout << "[Board]\tNo slave assign request to process..." << std::endl;
            break;
        }

        std::cout << "[Board]\tProcessing a new SLAVE assign request..." << std::endl;

        ReadablePacket clientPacket(slavesDataPtr, slavesDataPtr + sizeof(WritablePacket::Header) + packetPayload);
        WritablePacket forwardPacket(transferBufferPtr, transferBufferPtr + sizeof(WritablePacket::Header) + packetPayload + sizeof(BoardID));

        forwardPacket = clientPacket;
        forwardPacket.pushFootprint(_boardID);

        transferBufferSize += sizeof(WritablePacket::Header) + forwardPacket.payload();

        slavesDataPtr += sizeof(WritablePacket::Header) + packetPayload;
        transferBufferPtr += sizeof(WritablePacket::Header) + forwardPacket.payload();
    }

    // STEP 2 : "self assigns"

    std::uint8_t *selfAssignPtr = _networkBuffer.data() + AssignOffset;

    while (1) {
        auto *packetHeader = reinterpret_cast<ReadablePacket::Header *>(selfAssignPtr);
        std::size_t packetPayload = packetHeader->payload;

        if (packetHeader->magicKey != SpecialLabMagicKey ||
                !(packetHeader->protocolType == ProtocolType::Connection &&
                packetHeader->command == static_cast<std::uint16_t>(ConnectionCommand::IDAssignment))) {
            std::cout << "[Board]\tNo self assign request to process..." << std::endl;
            break;
        }

        std::cout << "[Board]\tProcessing a new SELF assign request..." << std::endl;

        ReadablePacket clientPacket(selfAssignPtr, selfAssignPtr + sizeof(WritablePacket::Header) + packetPayload);
        WritablePacket forwardPacket(transferBufferPtr, transferBufferPtr + sizeof(WritablePacket::Header) + packetPayload);

        forwardPacket = clientPacket;

        transferBufferPtr += sizeof(WritablePacket::Header) + forwardPacket.payload();
        transferBufferSize += sizeof(WritablePacket::Header) + forwardPacket.payload();

        selfAssignPtr += sizeof(WritablePacket::Header) + packetPayload;
    }

    _networkBuffer.setTransferSize(transferBufferSize);
}

void NetworkModule::transferToMaster(Scheduler &scheduler)
{
    std::cout << "[Board]\tNetworkModule::transferToMaster" << std::endl;

    // Transfer all data of the current tick to master endpoint
    auto ret = ::send(_masterSocket, _networkBuffer.data(), _networkBuffer.size(), 0);
    if (!ret && !(errno == EAGAIN || errno == EWOULDBLOCK)) {
        std::cout << "[Board]\ttransferToMaster::send failed: " << std::strerror(errno) << std::endl;
        return;
    }
    std::cout << "[Board]\tTransfered " << ret << " bytes to master endpoint" << std::endl;
    _networkBuffer.reset();
}

bool NetworkModule::readDataFromClient(Client *client, std::size_t &offset)
{
    std::uint8_t *networkBuffer = _networkBuffer.data() + offset;

    const auto ret = ::recv(client->socket, networkBuffer, 1024, MSG_DONTWAIT);

    if (ret < 0) {
        if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { // No data to proccess for this client
            std::cout << "[Board]\tNo data to process from this client..." << std::endl;
            return false;
        } else if (ret < 0) { // Error will reading from client socket
            std::cout << "[Board]\tError reading data from client" << std::endl;
            return false;
        }
    } else if (ret == 0) { // Client board disconnection detected
        std::cout << "[Board]\tClient board disconnection detected" << std::endl;
        _clients.erase(client);
        return false;
    }

    std::cout << "[Board]\tReceived " << ret << " bytes from client" << std::endl;
    offset += ret; // Increment specific index for next read operation

    return true;
}

void NetworkModule::processClientAssignmentRequest(Client *client, std::size_t &assignOffset)
{
    using namespace Protocol;

    std::uint8_t *requestPacketPtr = _networkBuffer.data() + assignOffset;

    if (!readDataFromClient(client, assignOffset)) {
        return;
    }

    auto *packetHeader = reinterpret_cast<ReadablePacket::Header *>(requestPacketPtr);
    std::size_t packetPayload = packetHeader->payload;

    if (packetHeader->magicKey != SpecialLabMagicKey ||
            !(packetHeader->protocolType == ProtocolType::Connection &&
            packetHeader->command == static_cast<std::uint16_t>(ConnectionCommand::IDAssignment))) {
                std::cout << "[Board]\tInvalid packet from a client in assignment mode !" << std::endl;
                return;
    }

    WritablePacket requestPacket(requestPacketPtr, requestPacketPtr + sizeof(WritablePacket::Header) + packetPayload + 2 * sizeof(BoardID));

    // Adding self board id to the stack
    requestPacket.pushFootprint(_boardID);

    // Add temporary board ID to the packet
    _selfAssignIndex++;
    requestPacket.pushFootprint(_selfAssignIndex);
    client->id = _selfAssignIndex;
    if (_selfAssignIndex == 255)
        _selfAssignIndex = 0;

    // Increasing the assign index by the size of 2 footprint
    assignOffset += 2;
}
