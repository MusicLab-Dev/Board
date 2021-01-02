/**
 * @ Author: Paul Creze
 * @ Description: Network module
 */

#include <iostream>
#include <cstring>
#include <cerrno>

#include "Scheduler.hpp"

NetworkModule::NetworkModule(void) : _buffer(NetworkBufferSize)
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

void NetworkModule::processMaster(Scheduler &scheduler)
{
    std::cout << "[Board]\tNetworkModule::processMaster" << std::endl;

    char buffer[1024];
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
}

void NetworkModule::tick(Scheduler &scheduler) noexcept
{
    std::cout << "[Board]\tNetworkModule::tick" << std::endl;

    if (scheduler.state() != Scheduler::State::Connected)
        return;
    processMaster(scheduler);
    if (scheduler.state() != Scheduler::State::Connected)
        return;
    proccessNewClientConnections(scheduler);
    processClients(scheduler);

    // Send hardware module data
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

    // Send ID assignment to master
    std::cout << "[Board]\tSending ID assignment packet..." << std::endl;
    WritablePacket requestID(_buffer.begin(), _buffer.end());
    requestID.prepare(ProtocolType::Connection, ConnectionCommand::IDAssignment);
    if (!sendPacket(_masterSocket, requestID)) {
        std::cout << "[Board]\tinitNewMasterConnection::send failed: " << std::strerror(errno) << std::endl;
        return;
    }

    // ID assignment from master
    std::cout << "[Board]\tWaiting for ID assignment packet..." << std::endl;
    char IDAssignmentBuffer[sizeof(WritablePacket::Header) + sizeof(BoardID)];
    auto ret = ::read(_masterSocket, &IDAssignmentBuffer, sizeof(IDAssignmentBuffer));
    if (ret < 0) {
        std::cout << "[Board]\tinitNewMasterConnection::read failed: " << std::strerror(errno) << std::endl;
        return;
    }
    ReadablePacket IDAssignment(std::begin(IDAssignmentBuffer), std::end(IDAssignmentBuffer));
    if (IDAssignment.protocolType() != ProtocolType::Connection ||
            IDAssignment.commandAs<ConnectionCommand>() != ConnectionCommand::IDAssignment) {
                std::cout << "[Board]\tInvalid ID assignment packet..." << std::endl;
                return;
    }
    std::cout << "[Board]\tIDAssignment packet size: " << ret << std::endl;
    _boardID = IDAssignment.extract<BoardID>();
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
            .id = 0u
        };
        _clients.push(clientBoard);
    }
}

void NetworkModule::processClients(Scheduler &scheduler)
{
    std::cout << "[Board]\tNetworkModule::processClients" << std::endl;

    if (_clients.empty()) {
        std::cout << "[Board]\tNo connected board to proccess..." << std::endl;
        return;
    }

    for (auto client = _clients.begin(); client != _clients.end(); client++) {

        std::cout << "[Board]\tProccessing client: " << inet_ntoa(in_addr { client->address }) << std::endl;

        // test only
        char buffer[1024];
        std::memset(buffer, 0, 1024);

        const auto ret = recv(client->socket, &buffer, 1024, MSG_DONTWAIT);
        if (ret < 0) {
            if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { // No data to proccess for this client
                std::cout << "[Board]\tNo data to process from this client..." << std::endl;
                continue;
            }
            else if (ret < 0) // Error will reading from client socket
                throw std::runtime_error(std::strerror(errno));
        } else if (ret == 0) { // Client board disconnection detected
            std::cout << "[Board]\tClient board disconnection detected" << std::endl;
            _clients.erase(client);
            return;
        }

        std::cout << "[Board]\tReceived " << ret << " bytes from client" << std::endl;
        std::cout << buffer << std::endl;
    }
}

void NetworkModule::handleClientDisconnection(Client *disconnectedClient)
{
    std::cout << "[Board]\tNetworkModule::handleClientDisconnection" << std::endl;
}
