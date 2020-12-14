/**
 * @ Author: Paul Creze
 * @ Description: Network module
 */

#include <iostream>
#include <cstring>
#include <cerrno>

#include "Scheduler.hpp"

NetworkModule::NetworkModule(void)
{
    std::cout << "[Board]\tNetworkModule constructor" << std::endl;

    // Open UDP broadcast socket
    int broadcast = 1;
    _usbBroadcastSocket = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (_usbBroadcastSocket < 0)
        throw std::runtime_error(std::strerror(errno));
    const auto ret = ::setsockopt(
        _usbBroadcastSocket,
        SOL_SOCKET,
        SO_BROADCAST,
        &broadcast,
        sizeof(broadcast)
    );
    if (ret < 0)
        throw std::runtime_error(std::strerror(errno));
    if (tryToBindUsb())
        _isBinded = true;
}

NetworkModule::~NetworkModule(void)
{
    std::cout << "[Board]\tNetworkModule destructor" << std::endl;

    close(_usbBroadcastSocket);
    shutdown(_masterSocket, SHUT_RDWR);
    close(_masterSocket);
}

bool NetworkModule::tryToBindUsb(void)
{
    // Define broadcast address
    const std::string &broadcastAddress = confTable.get("BroadcastAddress", "NotFound").c_str();
    sockaddr_in usbBroadcastAddress {
        .sin_family = AF_INET,
        .sin_port = ::htons(420),
        .sin_addr = {
            .s_addr = ::inet_addr(broadcastAddress == "NotFound" ? "127.0.0.1" : broadcastAddress.c_str())
        }
    };

    const auto ret = ::bind(
        _usbBroadcastSocket,
        reinterpret_cast<const sockaddr *>(&usbBroadcastAddress),
        sizeof(usbBroadcastAddress)
    );

    if (ret < 0 && (errno == 13 || errno == 98))
        throw std::runtime_error(std::strerror(errno));
    if (ret < 0) {
        std::cout << "[Board]\ttryToBindUsb: USB broadcast address does not exist..." << std::endl;
    }
    return ret == 0;
}

void NetworkModule::processMaster(Scheduler &scheduler)
{
    std::cout << "[Board] NetworkModule::processMaster" << std::endl;

    char buffer[1024];
    const auto ret = ::read(_masterSocket, &buffer, sizeof(buffer));
    if (ret == 0) {
        std::cout << "[Board] Disconnected from master" << std::endl;
        close(_masterSocket);
        _masterSocket = -1;
        _boardID = 0u;
        _connectionType = Protocol::ConnectionType::None;
        _nodeDistance = 0u;
        scheduler.setState(Scheduler::State::Disconnected);
    }
}

void NetworkModule::tick(Scheduler &scheduler) noexcept
{
    std::cout << "[Board]\tNetworkModule::tick" << std::endl;
    processMaster(scheduler);
    if (scheduler.state() != Scheduler::State::Connected)
        return;
    processClients(scheduler);
    // Send hardware module data
}

void NetworkModule::discover(Scheduler &scheduler) noexcept
{
    std::cout << "[Board]\tNetworkModule::discover" << std::endl;

    // Check if the broadcast socket is binded
    if (!_isBinded) {
        if (!tryToBindUsb())
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
    _buffer.clear();
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

    if (_masterSocket) {
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

void NetworkModule::analyzeUsbEndpoints(const std::vector<Endpoint> &usbEndpoints, Scheduler &scheduler) noexcept
{
    std::cout << "[Board]\tNetworkModule::analyzeUsbEndpoints" << std::endl;

    std::size_t index = 0;
    std::size_t i = 0;

    for (const auto &endpoint : usbEndpoints) {
        if (usbEndpoints.at(index).connectionType != Protocol::ConnectionType::USB &&
            endpoint.connectionType == Protocol::ConnectionType::USB) {
            index = i;
        } else if (endpoint.distance < usbEndpoints.at(index).distance) {
            index = i;
        }
        i++;
    }
    if ((_connectionType != Protocol::ConnectionType::USB &&
        usbEndpoints.at(index).connectionType == Protocol::ConnectionType::USB) ||
        usbEndpoints.at(index).distance + 1 < _nodeDistance) {
        std::cout << "[Board]\tNew endpoint found for studio connection" << std::endl;
        initNewMasterConnection(usbEndpoints.at(index), scheduler);
    }
}

void NetworkModule::discoveryScan(Scheduler &scheduler)
{
    std::cout << "[Board]\tNetworkModule::discoveryScan" << std::endl;

    sockaddr_in usbSenderAddress;
    int usbSenderAddressLength = sizeof(usbSenderAddress);

    Protocol::DiscoveryPacket packet;
    std::vector<Endpoint> usbEndpoints;

    while (1) {
        const auto size = ::recvfrom(
            _usbBroadcastSocket,
            &packet,
            sizeof(Protocol::DiscoveryPacket),
            MSG_WAITALL | MSG_DONTWAIT,
            reinterpret_cast<sockaddr *>(&usbSenderAddress),
            reinterpret_cast<socklen_t *>(&usbSenderAddressLength)
        );
        std::cout << "[Board]\tNetworkModule::discoveryScan::recvfrom: " << size << std::endl;

        // Loop end condition, until read buffer is empty
        if (size < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            std::cout << "[Board]\tNetworkModule::discoveryScan: nothing remaining on the socket" << std::endl;
            if (scheduler.state() != Scheduler::State::Connected && !usbEndpoints.empty())
                analyzeUsbEndpoints(usbEndpoints, scheduler);
            return;
        } else if (size < 0)
            throw std::runtime_error(std::strerror(errno));

        // Ignore unknow and self broadcasted packets
        if (packet.magicKey != Protocol::SpecialLabMagicKey || packet.boardID == _boardID) {
            std::cout << "[Board]\tNetworkModule::discoveryScan: ignoring packet" << std::endl;
            continue;
        }

        // Debug
        char senderAddressString[100];
        std::memset(senderAddressString, 0, 100);
        ::inet_ntop(AF_INET, &(usbSenderAddress.sin_addr), senderAddressString, 100);
        std::cout << "[Board]\tNetworkModule::discoveryScan: USB DiscoveryPacket received from " << senderAddressString << std::endl;

        Endpoint endpoint {
            .address = usbSenderAddress.sin_addr.s_addr,
            .connectionType = packet.connectionType,
            .distance = packet.distance
        };
        usbEndpoints.push_back(endpoint);
    }
}

void NetworkModule::discoveryEmit(Scheduler &scheduler) noexcept
{
    std::cout << "[Board]\tNetworkModule::discoveryEmit" << std::endl;

    Protocol::DiscoveryPacket packet;
    packet.boardID = _boardID;
    packet.connectionType = _connectionType;
    packet.distance = _nodeDistance;

    const std::string &broadcastAddress = confTable.get("BroadcastAddress", "NotFound").c_str();
    sockaddr_in usbBroadcastAddress {
        .sin_family = AF_INET,
        .sin_port = ::htons(420),
        .sin_addr = {
            .s_addr = ::inet_addr(broadcastAddress == "NotFound" ? "127.0.0.1" : broadcastAddress.c_str())
        }
    };
    const auto ret = ::sendto(
        _usbBroadcastSocket,
        &packet,
        sizeof(Protocol::DiscoveryPacket),
        0,
        reinterpret_cast<const sockaddr *>(&usbBroadcastAddress),
        sizeof(usbBroadcastAddress)
    );
    if (ret < 0) {
        std::cout << "[Board]\t NetworkModule::discoveryEmit::sendto failed: " << std::strerror(errno) << std::endl;
    }
}

void NetworkModule::processClients(Scheduler &scheduler) noexcept
{
    std::cout << "[Board]\tNetworkModule::processClients" << std::endl;
}
