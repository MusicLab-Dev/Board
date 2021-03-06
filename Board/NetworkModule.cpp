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
    std::cout << "NetworkModule constructor" << std::endl;

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

bool NetworkModule::tryToBindUsb(void) noexcept
{
    // Define broadcast address
    sockaddr_in usbBroadcastAddress {
        .sin_family = AF_INET,
        .sin_port = ::htons(420),
        .sin_addr = {
            .s_addr = ::inet_addr("169.254.255.255")
        }
    };

    const auto ret = ::bind(
        _usbBroadcastSocket,
        reinterpret_cast<const sockaddr *>(&usbBroadcastAddress),
        sizeof(usbBroadcastAddress)
    );
    return ret < 0;
}

NetworkModule::~NetworkModule(void)
{
    std::cout << "NetworkModule destructor" << std::endl;
}

void NetworkModule::tick(Scheduler &scheduler) noexcept
{
    if (scheduler.state() != Scheduler::State::Connected)
        return;
    std::cout << "NetworkModule::tick function" << std::endl;
    processClients(scheduler);
    // Send hardware module data
}

void NetworkModule::discover(Scheduler &scheduler) noexcept
{
    // Check if the broadcast socket is binded
    if (!_isBinded) {
        if (!tryToBindUsb()) {
            std::cout << "NetworkModule::discover: Couldn't bind to usb broadcast" << std::endl;
            return;
        }
        _isBinded = true;
    }

    // Emit broadcast packet only if board is connected
    if (scheduler.state() == Scheduler::State::Connected)
        discoveryEmit(scheduler);
    discoveryScan(scheduler);
}

void NetworkModule::initNewMasterConnection(const Endpoint &masterEndpoint, Scheduler &scheduler) noexcept
{
    std::cout << "NetworkModule::initNewMasterConnection function" << std::endl;

    if (_masterSocket) {
        ::close(_masterSocket);
        _masterSocket = -1;
    }
    _masterSocket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (_masterSocket < 0) {
        std::cout << "Error: initNewMasterConnection: socket: " << strerror(errno) << std::endl;
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
        std::cout << "Error: initNewMasterConnection: connect: " << strerror(errno) << std::endl;
        return;
    }
    std::cout << "Connected to master" << std::endl;

    std::cout << "Starting ID request procedure..." << std::endl;

    Protocol::Packet request(Protocol::PacketID::IDResquest);
    ::send(_masterSocket, &request, request.size(), 0);

    Protocol::Packet response;
    const auto size = ::read(_masterSocket, &response, sizeof(response));
    std::cout << "response is " << size << "bytes" << std::endl;
    // to continue...

    // only if ID assignment is done correctly
    _connectionType = masterEndpoint.connectionType;
    _nodeDistance = masterEndpoint.distance;
    scheduler.setState(Scheduler::State::Connected);
}

void NetworkModule::analyzeUsbEndpoints(const std::vector<Endpoint> &usbEndpoints, Scheduler &scheduler) noexcept
{
    std::cout << "NetworkModule::analyzeUsbEndpoints function" << std::endl;

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
    if (_connectionType != Protocol::ConnectionType::USB &&
        usbEndpoints.at(index).connectionType == Protocol::ConnectionType::USB) {
        std::cout << "New endpoint found for studio connection" << std::endl;
        initNewMasterConnection(usbEndpoints.at(index), scheduler);
    } else if (usbEndpoints.at(index).distance + 1 < _nodeDistance) {
        std::cout << "New endpoint found for studio connection" << std::endl;
        initNewMasterConnection(usbEndpoints.at(index), scheduler);
    }
}

void NetworkModule::discoveryScan(Scheduler &scheduler)
{
    std::cout << "NetworkModule::discoveryScan function" << std::endl;

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
        std::cout << "recvfrom size: " << size << std::endl;

        // loop end condition, until read buffer is empty
        if (size < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            std::cout << "nothing remaining on the socket" << std::endl;
            if (scheduler.state() != Scheduler::State::Connected && !usbEndpoints.empty())
                analyzeUsbEndpoints(usbEndpoints, scheduler);
            return;
        } else if (size < 0) {
            throw std::runtime_error(std::strerror(errno));
            return;
        }

        // ignore unknow and self broadcasted packets
        if (packet.magicKey != Protocol::MusicLabMagicKey || packet.boardID == _boardID) {
            std::cout << "ignoring packet" << std::endl;
            continue;
        }

        // debug
        char senderAddressString[100];
        std::memset(senderAddressString, 0, 100);
        ::inet_ntop(AF_INET, &(usbSenderAddress.sin_addr), senderAddressString, 100);
        std::cout << "usb discovery packet received from " << senderAddressString << std::endl;

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
    std::cout << "NetworkModule::discoveryEmit function" << std::endl;

    Protocol::DiscoveryPacket packet;
    packet.connectionType = _connectionType;
    packet.distance = _nodeDistance;

    sockaddr_in usbBroadcastAddress {
        .sin_family = AF_INET,
        .sin_port = ::htons(420),
        .sin_addr = {
            .s_addr = ::inet_addr("169.254.255.255")
        }
    };

    ::sendto(
        _usbBroadcastSocket,
        &packet,
        sizeof(Protocol::DiscoveryPacket),
        0,
        reinterpret_cast<const sockaddr *>(&usbBroadcastAddress), sizeof(usbBroadcastAddress)
    );
}

void NetworkModule::processClients(Scheduler &scheduler) noexcept
{
    std::cout << "Network module processClients function" << std::endl;
}