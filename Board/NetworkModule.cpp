/**
 * @ Author: Paul Creze
 * @ Description: Network module
 */

#include <iostream>
#include <cstring>
#include <cerrno>

#include <Protocol/NetworkLog.hpp>

#include "Scheduler.hpp"
#include "PinoutConfig.hpp"

NetworkModule::NetworkBuffer NetworkModule::_NetworkBuffer;

void NetworkModule::NetworkBuffer::writeTransfer(const Protocol::Internal::PacketBase &packet) noexcept
{
    std::copy(packet.rawDataBegin(), packet.rawDataEnd(), transferEnd());
    incrementTransferHead(packet.totalSize());
}

NetworkModule::NetworkModule(void)
{
    NETWORK_LOG("[Board]\tNetworkModule constructor");

    // Open UDP broadcast socket
    int broadcast = 1;
    _udpBroadcastSocket = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (_udpBroadcastSocket < 0)
        throw std::runtime_error(std::strerror(errno));
    setSocketReusable(_udpBroadcastSocket);
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
    setSocketReusable(_slavesSocket);

    sockaddr_in localAddress;
    localAddress.sin_family = AF_INET;
    localAddress.sin_port = ::htons(LexoPort);
    localAddress.sin_addr.s_addr = ::htonl(INADDR_ANY);

    // bind localAddress to _slavesSocket
    ret = ::bind(
        _slavesSocket,
        reinterpret_cast<const sockaddr *>(&localAddress),
        sizeof(localAddress)
    );
    if (ret < 0) {
        ::close(_slavesSocket);
        throw std::runtime_error(std::strerror(errno));
    }
    ret = ::listen(_slavesSocket, 5);
    if (ret < 0)
        throw std::runtime_error(std::strerror(errno));

    // Open UDP local socket
    _udpLocalSocket = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (_udpLocalSocket < 0)
        throw std::runtime_error(std::strerror(errno));
    setSocketReusable(_udpLocalSocket);

    sockaddr_in udpLocalAddress;
    udpLocalAddress.sin_family = AF_INET;
    udpLocalAddress.sin_port = ::htons(LexoPort);
    udpLocalAddress.sin_addr.s_addr = INADDR_ANY;

    // Bind UDP local socket to address
    ret = ::bind(
        _udpLocalSocket,
        reinterpret_cast<const sockaddr *>(&udpLocalAddress),
        sizeof(udpLocalAddress)
    );
    if (ret < 0 && (errno == 13 || errno == 98))
        throw std::runtime_error(std::strerror(errno));
    if (ret < 0) {
        NETWORK_LOG("[Board]\tBIND ERROR");
    }

    // Initialize network buffer
    _NetworkBuffer.reset();
}

NetworkModule::~NetworkModule(void)
{
    NETWORK_LOG("[Board]\tNetworkModule destructor");

    ::close(_udpBroadcastSocket);
    if (_masterSocket != -1) {
        ::shutdown(_masterSocket, SHUT_RDWR);
        ::close(_masterSocket);
    }
}

bool NetworkModule::tryToBindUdp(void)
{
    // Define UDP broadcast address
    const std::string &broadcastAddress = confTable.get("BroadcastAddress", "NotFound").c_str();

    sockaddr_in udpBroadcastAddress;
    udpBroadcastAddress.sin_family = AF_INET;
    udpBroadcastAddress.sin_port = ::htons(LexoPort);
    udpBroadcastAddress.sin_addr.s_addr = ::inet_addr(broadcastAddress == "NotFound" ? "127.0.0.1" : broadcastAddress.c_str());

    const auto ret = ::bind(
        _udpBroadcastSocket,
        reinterpret_cast<const sockaddr *>(&udpBroadcastAddress),
        sizeof(udpBroadcastAddress)
    );

    if (ret < 0 && (errno == 13 || errno == 98))
        throw std::runtime_error(std::strerror(errno));
    if (ret < 0) {
        NETWORK_LOG("[Board]\ttryToBindUdp: UDP broadcast address does not exist...");
    }
    return ret == 0;
}

void NetworkModule::notifyDisconnectionToClients(void)
{
    NETWORK_LOG("[Board]\tNetworkModule::notifyDisconnectionToClients");

    if (_clients.empty()) {
        NETWORK_LOG("[Board]\tNo client board to notify...");
        return;
    }
    for (const auto client : _clients) {
        ::close(client.socket);
    }
    _clients.clear();
}

void NetworkModule::processAssignmentFromMaster(Protocol::ReadablePacket &packet)
{
    NETWORK_LOG("[Board]\tNetworkModule::processAssignmentFromMaster");

    using namespace Protocol;

    if (packet.footprintStackSize() == 1) {

        char buffer[256];
        std::memset(&buffer, 0, sizeof(buffer));
        WritablePacket forwardPacket(&buffer, &buffer + sizeof(WritablePacket::Header) + packet.payload());
        forwardPacket = packet;

        BoardID temporaryAssignedID = forwardPacket.popFrontStack();
        BoardID clientNewID = packet.extract<BoardID>();

        NETWORK_LOG("[Board]\tID assignment packet is for direct client with temporary ID = ", static_cast<int>(temporaryAssignedID));

        for (auto &clientBoard : _clients) {
            if (clientBoard.id != temporaryAssignedID)
                continue;
            if (!::send(clientBoard.socket, forwardPacket.rawDataBegin(), forwardPacket.totalSize(), 0)) {
                NETWORK_LOG("[Board]\tprocessAssignmentFromMaster::send failed: ", std::strerror(errno));
                return;
            }
            clientBoard.id = clientNewID;
            NETWORK_LOG("[Board]\tDirect client get final ID of ", static_cast<int>(clientBoard.id), " assigned by studio");
            return;
        }
    }
}

void NetworkModule::processHardwareSpecsFromMaster(Protocol::ReadablePacket &packet)
{
    char buffer[256];
    std::memset(&buffer, 0, sizeof(buffer));
    WritablePacket response(&buffer, &buffer + sizeof(WritablePacket::Header) + packet.payload());

    response.prepare(Protocol::ProtocolType::Connection, Protocol::ConnectionCommand::HardwareSpecs);
    response << BoardSize { Pin::Count, 1 };

}

void NetworkModule::processMaster(Scheduler &scheduler)
{
    NETWORK_LOG("[Board]\tNetworkModule::processMaster");

    using namespace Protocol;

    std::uint8_t buffer[1024];
    std::memset(&buffer, 0, sizeof(buffer));

    // Proccess master input, read must be non-blocking
    const auto ret = ::read(_masterSocket, &buffer, sizeof(buffer));

    if (ret == 0 || (ret < 0 && (errno == ECONNRESET || errno == ETIMEDOUT))) {
        NETWORK_LOG("[Board]\tDisconnected from master");
        ::close(_masterSocket);
        _masterSocket = -1;
        _boardID = 0u;
        _connectionType = Protocol::ConnectionType::None;
        _nodeDistance = 0u;
        scheduler.setState(Scheduler::State::Disconnected);
        notifyDisconnectionToClients();
	    return;
    }
    else if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        NETWORK_LOG("[Board]\tNo data received from master, return");
        return;
    }

    NETWORK_LOG("[Board]\tReceived ", ret, " bytes from master");

    std::uint8_t *bufferPtr = &buffer[0];
    std::uint8_t *bufferEnd = bufferPtr + ret;

    while (bufferPtr != bufferEnd) {
        ReadablePacket packet(bufferPtr, bufferEnd);

        if (packet.magicKey() != SpecialLabMagicKey) {
            NETWORK_LOG("[Board]\tNo new packet from master to process...");
            ++bufferPtr;
            continue;
        }

        switch (packet.protocolType()) {
        case ProtocolType::Connection:
            switch (packet.commandAs<ConnectionCommand>()) {
            case ConnectionCommand::IDAssignment:
                processAssignmentFromMaster(packet);
                break;
            case ConnectionCommand::HardwareSpecs:
                processHardwareSpecsFromMaster(packet);
                break;
            }
            break;
        default:
            break;
        }

        /* others cases ... */

        bufferPtr += packet.totalSize();
    }
}

void NetworkModule::processHardwareEvents(Scheduler &scheduler)
{
    NETWORK_LOG("[Board]\tNetworkModule::processHardwareEvents");

    using namespace Protocol;

    auto &events = scheduler.hardwareModule().inputEvents();

    if (events.empty())
        return;

    WritablePacket packet(_NetworkBuffer.transferEnd(), _NetworkBuffer.transferRealEnd());
    packet.prepare(ProtocolType::Event, EventCommand::ControlsChanged);

    packet << _boardID;
    packet << events;

    _NetworkBuffer.incrementTransferHead(packet.totalSize());
}

void NetworkModule::tick(Scheduler &scheduler) noexcept
{
    NETWORK_LOG("[Board]\tNetworkModule::tick");

    if (scheduler.state() != Scheduler::State::Connected) // If connected, process master and clients
        return;

    processMaster(scheduler);

    if (scheduler.state() != Scheduler::State::Connected) // If master has been disconnected, return
        return;

    proccessNewClientConnections(scheduler);

    readClients(scheduler); // 1
    processClientsData(scheduler); // 2

    processHardwareEvents(scheduler);

    if (_NetworkBuffer.transferSize() == 0u) {
        NETWORK_LOG("[Board]\tNo data to transfer to master");
        return;
    }

    transferToMaster(scheduler); // 3
}

void NetworkModule::discover(Scheduler &scheduler) noexcept
{
    NETWORK_LOG("[Board]\tNetworkModule::discover");

    // Check if the broadcast socket is binded
    if (!_isBinded) {
        if (!tryToBindUdp())
            return;
        _isBinded = true;
    }

    // Emit broadcast packet only if board is connected
    // if (scheduler.state() == Scheduler::State::Connected)
    discoveryEmit(scheduler);
    discoveryScan(scheduler);
}

void NetworkModule::sendHardwareSpecsToMaster(void)
{
    NETWORK_LOG("[Board]\tNetworkModule::sendHardwareSpecsToMaster");

    using namespace Protocol;

    WritablePacket packet(_NetworkBuffer.transferEnd(), _NetworkBuffer.transferRealEnd());
    packet.prepare(ProtocolType::Connection, ConnectionCommand::HardwareSpecs);

    packet << _boardID;

    BoardSize boardSpecs;
    boardSpecs.heigth = 1;
    boardSpecs.width = Pin::Count;

    packet << boardSpecs;

    _NetworkBuffer.incrementTransferHead(packet.totalSize());
}

void NetworkModule::startIDRequestToMaster(const Endpoint &masterEndpoint, Scheduler &scheduler)
{
    using namespace Protocol;

    char requestBuffer[sizeof(WritablePacket::Header) + sizeof(BoardID)];
    WritablePacket requestPacket(std::begin(requestBuffer), std::end(requestBuffer));
    requestPacket.prepare(ProtocolType::Connection, ConnectionCommand::IDAssignment);
    requestPacket << BoardID(0u);

    // Send ID assignment to master
    NETWORK_LOG("[Board]\tSending ID assignment packet...");
    if (!::send(_masterSocket, requestPacket.rawDataBegin(), requestPacket.totalSize(), 0)) {
        NETWORK_LOG("[Board]\tinitNewMasterConnection::send failed: ", std::strerror(errno));
        return;
    }

    std::memset(&requestBuffer, 0, sizeof(requestBuffer));

    // Wait for ID assignation from master
    NETWORK_LOG("[Board]\tWaiting for ID assignment packet from master...");
    auto ret = ::read(_masterSocket, &requestBuffer, sizeof(requestBuffer));
    if (ret < 0) {
        NETWORK_LOG("[Board]\tinitNewMasterConnection::read failed: ", std::strerror(errno));
        return;
    }

    ReadablePacket responsePacket(std::begin(requestBuffer), std::end(requestBuffer));
    if (responsePacket.protocolType() != ProtocolType::Connection ||
            responsePacket.commandAs<ConnectionCommand>() != ConnectionCommand::IDAssignment) {
                NETWORK_LOG("[Board]\tInvalid ID assignment packet...");
                return;
    }

    _boardID = responsePacket.extract<BoardID>();
    NETWORK_LOG("[Board]\tAssigned BoardID from master: ", static_cast<int>(_boardID));

    // Only if ID assignment is done correctly
    _connectionType = masterEndpoint.connectionType;
    _nodeDistance = masterEndpoint.distance;
    scheduler.setState(Scheduler::State::Connected);

    fcntl(_masterSocket, F_SETFL, O_NONBLOCK);

    sendHardwareSpecsToMaster();
}

void NetworkModule::setSocketKeepAlive(const int socket) const noexcept
{
    int enable = 1;
    int idle = 3;
    int interval = 3;
    int maxpkt = 1;

    setsockopt(socket, SOL_SOCKET, SO_KEEPALIVE, &enable, sizeof(int));
    setsockopt(socket, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(int));
    setsockopt(socket, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(int));
    setsockopt(socket, IPPROTO_TCP, TCP_KEEPCNT, &maxpkt, sizeof(int));
}

void NetworkModule::initNewMasterConnection(const Endpoint &masterEndpoint, Scheduler &scheduler) noexcept
{
    NETWORK_LOG("[Board]\tNetworkModule::initNewMasterConnection");

    if (_masterSocket != -1) {
        ::close(_masterSocket);
        _masterSocket = -1;
    }
    _masterSocket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (_masterSocket < 0) {
        NETWORK_LOG("[Board]\tinitNewMasterConnection::socket failed: ", strerror(errno));
        return;
    }

    sockaddr_in masterAddress;
    masterAddress.sin_family = AF_INET;
    masterAddress.sin_port = ::htons(LexoPort + 1);
    masterAddress.sin_addr.s_addr = masterEndpoint.address;

    auto ret = ::connect(
        _masterSocket,
        reinterpret_cast<const sockaddr *>(&masterAddress),
        sizeof(masterAddress)
    );
    if (ret < 0) {
        NETWORK_LOG("[Board]\tinitNewMasterConnection::connect failed: ", strerror(errno));
        return;
    }
    setSocketKeepAlive(_masterSocket);
    NETWORK_LOG("[Board]\tConnected to studio master socket");
    startIDRequestToMaster(masterEndpoint, scheduler);
    return;
}

void NetworkModule::analyzeUdpEndpoints(const std::vector<Endpoint> &udpEndpoints, Scheduler &scheduler) noexcept
{
    NETWORK_LOG("[Board]\tNetworkModule::analyzeUdpEndpoints");

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
        NETWORK_LOG("[Board]\tNew endpoint found for studio connection");
        initNewMasterConnection(udpEndpoints.at(index), scheduler);
    }
}

void NetworkModule::discoveryScan(Scheduler &scheduler)
{
    NETWORK_LOG("[Board]\tNetworkModule::discoveryScan");

    sockaddr_in udpSenderAddress;
    int udpSenderAddressLength = sizeof(udpSenderAddress);

    Protocol::DiscoveryPacket packet;
    std::vector<Endpoint> udpEndpoints;

    while (1) {
        const auto size = ::recvfrom(
            _udpLocalSocket,
            &packet,
            sizeof(Protocol::DiscoveryPacket),
            MSG_WAITALL | MSG_DONTWAIT,
            reinterpret_cast<sockaddr *>(&udpSenderAddress),
            reinterpret_cast<socklen_t *>(&udpSenderAddressLength)
        );
        NETWORK_LOG("[Board]\tNetworkModule::discoveryScan::recvfrom: ", size);

        // Loop end condition, until read buffer is empty
        if (size < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            NETWORK_LOG("[Board]\tNetworkModule::discoveryScan: nothing remaining on the socket");
            if (scheduler.state() != Scheduler::State::Connected && !udpEndpoints.empty())
                analyzeUdpEndpoints(udpEndpoints, scheduler);
            return;
        } else if (size < 0)
            throw std::runtime_error(std::strerror(errno));

        // Ignore unknown and self broadcasted packets
        if (packet.magicKey != Protocol::SpecialLabMagicKey || packet.boardID == _boardID) {
            NETWORK_LOG("[Board]\tNetworkModule::discoveryScan: ignoring packet");
            continue;
        }

        // Debug
        char senderAddressString[100];
        std::memset(senderAddressString, 0, 100);
        ::inet_ntop(AF_INET, &(udpSenderAddress.sin_addr), senderAddressString, 100);
        NETWORK_LOG("[Board]\tNetworkModule::discoveryScan: UDP DiscoveryPacket received from ", senderAddressString);

        Endpoint endpoint;
        endpoint.address = udpSenderAddress.sin_addr.s_addr;
        endpoint.connectionType = packet.connectionType;
        endpoint.distance = packet.distance;

        udpEndpoints.push_back(endpoint);
    }
}

void NetworkModule::discoveryEmit(Scheduler &scheduler) noexcept
{
    (void)scheduler; // cast to remove error
    NETWORK_LOG("[Board]\tNetworkModule::discoveryEmit");

    Protocol::DiscoveryPacket packet;
    packet.magicKey = Protocol::SpecialLabMagicKey;
    packet.boardID = _boardID;
    packet.connectionType = _connectionType;
    packet.distance = _nodeDistance;

    const std::string &broadcastAddress = confTable.get("BroadcastAddress", "NotFound").c_str();

    sockaddr_in udpBroadcastAddress;
    udpBroadcastAddress.sin_family = AF_INET;
    udpBroadcastAddress.sin_port = ::htons(LexoPort);
    udpBroadcastAddress.sin_addr.s_addr = ::inet_addr(broadcastAddress == "NotFound" ? "127.0.0.1" : broadcastAddress.c_str());

    const auto ret = ::sendto(
        _udpBroadcastSocket,
        &packet,
        sizeof(Protocol::DiscoveryPacket),
        0,
        reinterpret_cast<const sockaddr *>(&udpBroadcastAddress),
        sizeof(udpBroadcastAddress)
    );
    if (ret < 0) {
        NETWORK_LOG("[Board]\tNetworkModule::discoveryEmit::sendto failed: ", std::strerror(errno));
    }
}

void NetworkModule::proccessNewClientConnections(Scheduler &scheduler)
{
    (void)scheduler; // cast to remove error
    NETWORK_LOG("[Board]\tNetworkModule::proccessNewClientConnections");

    sockaddr_in clientAddress;
    std::memset(&clientAddress, 0, sizeof(clientAddress));
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
            NETWORK_LOG("[Board]\tNo new client board connection to proccess...");
            return;
        } else if (clientSocket < 0)
            throw std::runtime_error(std::strerror(errno));

        NETWORK_LOG("[Board]\tNew board connection from ", inet_ntoa(clientAddress.sin_addr), ":", ntohs(clientAddress.sin_port));

        // Filling new client struct
        Client clientBoard;

        clientBoard.socket = clientSocket;
        clientBoard.address = clientAddress.sin_addr.s_addr;
        clientBoard.port = clientAddress.sin_port;
        clientBoard.id = 0u;

        _clients.push(clientBoard);
    }
}

void NetworkModule::readClients(Scheduler &scheduler)
{
    (void)scheduler; // cast to remove error
    NETWORK_LOG("[Board]\tNetworkModule::readClients");

    // Return if there is no client board(s)
    if (_clients.empty()) {
        NETWORK_LOG("[Board]\tNo connected board to proccess...");
        return;
    }

    std::size_t assignOffset { AssignOffset };
    std::size_t inputsOffset { InputsOffset };

    for (auto client = _clients.begin(); client != _clients.end(); ) {

        NETWORK_LOG(
            "[Board]\tProccessing client: ",
            inet_ntoa(in_addr { client->address }),
            ":",
            ntohs(client->port),
            " with boardID = ",
            static_cast<int>(client->id)
        );

        if (client->id == 0) {
            processClientAssignmentRequest(client, assignOffset);    // Client in "assign mode"
        } else
            readDataFromClient(client, inputsOffset);                // Client in "inputs mode"

        if (client != _clients.end())
            client++;
    }
    _NetworkBuffer.incrementAssignHead(assignOffset - AssignOffset);
    _NetworkBuffer.incrementSlaveDataHead(inputsOffset - InputsOffset);
}

void NetworkModule::processClientsData(Scheduler &scheduler)
{
    (void)scheduler;
    NETWORK_LOG("[Board]\tNetworkModule::processClientsData");

    /* Processing all data in 4 steps : */
    /* PRIORITY ORDER: slaves assigns, self assigns, slave events, self events */
    /* Note: All "Assigns" packets have to be process one by one to add footprint when "events" can be transfered directly */

    // 1 - Extract the "slaves assigns" from the "slaves data" area in the reception buffer & add footprint & store in transfer buffer.
    // 2 - Extract "self assigns" from "self assigns" area in the reception buffer & add footprint & store in transfer buffer.
    // 3 - Copy all the "slaves events" from the "slaves data" area (all remaining data) into the transfer buffer.
    // 4 - Copy all "self events" from the HardwareModule into the transfer buffer

    using namespace Protocol;

    std::uint8_t *transferPtr = _NetworkBuffer.transferEnd();
    std::uint8_t *transferRealEnd = _NetworkBuffer.transferEnd();
    std::size_t transferSize = 0u;

    // STEP 1 : "slaves assigns"
    {
        const std::uint8_t *slavesDataPtr = _NetworkBuffer.slaveDataBegin();
        const std::uint8_t *slavesDataEnd = _NetworkBuffer.slaveDataEnd();
        while (slavesDataPtr != slavesDataEnd) {
            NETWORK_LOG("[Board]\tProcessing a new SLAVE assign request...");
            ReadablePacket clientPacket(slavesDataPtr, slavesDataEnd);

            if (clientPacket.magicKey() != SpecialLabMagicKey ||
                    !(clientPacket.protocolType() == ProtocolType::Connection &&
                    clientPacket.commandAs<ConnectionCommand>() == ConnectionCommand::IDAssignment)) {
                ++slavesDataPtr;
                continue;
            }

            WritablePacket forwardPacket(transferPtr, transferRealEnd);

            forwardPacket = clientPacket;
            forwardPacket.pushFootprint(_boardID);

            transferSize += forwardPacket.totalSize();
            transferPtr += forwardPacket.totalSize();
            slavesDataPtr += clientPacket.totalSize();;
        }
    }

    // STEP 2 : "self assigns"
    {
        const std::uint8_t *assignPtr = _NetworkBuffer.assignBegin();
        const std::uint8_t *assignEnd = _NetworkBuffer.assignEnd();

        while (assignPtr != assignEnd) {
            NETWORK_LOG("[Board]\tProcessing a new SELF assign request...");
            ReadablePacket clientPacket(assignPtr, assignEnd);

            if (clientPacket.magicKey() != SpecialLabMagicKey ||
                    !(clientPacket.protocolType() == ProtocolType::Connection &&
                    clientPacket.commandAs<ConnectionCommand>() == ConnectionCommand::IDAssignment)) {
                ++assignPtr;
                continue;
            }

            WritablePacket forwardPacket(transferPtr, transferRealEnd);

            forwardPacket = clientPacket;
            transferSize += forwardPacket.totalSize();
            transferPtr += forwardPacket.totalSize();
            assignPtr += clientPacket.totalSize();
        }
    }

    // STEP 4: "self events"

    _NetworkBuffer.incrementTransferHead(transferSize);
}

void NetworkModule::transferToMaster(Scheduler &scheduler)
{
    (void)scheduler; // cast to remove error
    NETWORK_LOG("[Board]\tNetworkModule::transferToMaster");

    // Transfer all data of the current tick to master endpoint
    auto ret = ::send(_masterSocket, _NetworkBuffer.transferBegin(), _NetworkBuffer.transferSize(), 0);
    if (!ret && !(errno == EAGAIN || errno == EWOULDBLOCK)) {
        NETWORK_LOG("[Board]\ttransferToMaster::send failed: ", std::strerror(errno));
        return;
    }
    NETWORK_LOG("[Board]\tTransfered ", ret, " bytes to master endpoint");
    _NetworkBuffer.reset();
}

bool NetworkModule::readDataFromClient(Client *client, std::size_t &offset)
{
    std::uint8_t *networkBuffer = _NetworkBuffer.transferBegin() + offset;

    const auto ret = ::recv(client->socket, networkBuffer, 1024, MSG_DONTWAIT);

    if (ret < 0) {
        if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { // No data to proccess for this client
            NETWORK_LOG("[Board]\tNo data to process from this client...");
            return false;
        } else if (ret < 0) { // Error will reading from client socket
            NETWORK_LOG("[Board]\tError reading data from client");
            return false;
        }
    } else if (ret == 0) { // Client board disconnection detected
        NETWORK_LOG("[Board]\tClient board disconnection detected");
        _clients.erase(client);
        return false;
    }

    NETWORK_LOG("[Board]\tReceived " << ret << " bytes from client");
    offset += ret; // Increment specific index for next read operation

    return true;
}

void NetworkModule::processClientAssignmentRequest(Client *client, std::size_t &assignOffset)
{
    using namespace Protocol;

    std::uint8_t *requestPacketPtr = _NetworkBuffer.transferBegin() + assignOffset;

    if (!readDataFromClient(client, assignOffset))
        return;

    WritablePacket requestPacket(requestPacketPtr, _NetworkBuffer.assignRealEnd());

    if (requestPacket.magicKey() != SpecialLabMagicKey ||
            !(requestPacket.protocolType() == ProtocolType::Connection &&
            requestPacket.commandAs<ConnectionCommand>() == ConnectionCommand::IDAssignment)) {
                NETWORK_LOG("[Board]\tInvalid packet from a client in assignment mode !");
                return;
    }

    // Adding self board ID to the stack
    requestPacket.pushFootprint(_boardID);

    // Add temporary board ID to the packet
    ++_selfAssignIndex;
    requestPacket.pushFootprint(_selfAssignIndex);
    client->id = _selfAssignIndex;
    if (_selfAssignIndex == 255)
        _selfAssignIndex = 0;

    // Increasing the assign index by the size of 2 footprint
    assignOffset += 2;
}
