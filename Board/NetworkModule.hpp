/**
 * @ Author: Paul Creze
 * @ Description: Network module
 */

#pragma once

#include <cstring>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

#include <Core/Vector.hpp>

#include <Protocol/Packet.hpp>
#include <Protocol/Protocol.hpp>
#include <Protocol/ConnectionProtocol.hpp>

#include "Types.hpp"
#include "Module.hpp"

/** @brief Board module responsible of network communication */
class alignas_cacheline NetworkModule : public Module
{
public:
    /** @brief Size of the network buffer */
    static constexpr std::size_t NetworkBufferSize = 1024;

    /** @brief Network buffer used for all packet emission and reception */
    using NetworkBuffer = Core::Vector<std::uint8_t, std::uint8_t>;

    struct Endpoint
    {
        Net::IP address { 0u };
        Protocol::ConnectionType connectionType { Protocol::ConnectionType::None };
        Protocol::NodeDistance distance { 0u };
    };

    /** @brief Data of a connected client */
    struct alignas_half_cacheline Client
    {
        ~Client(void) noexcept = default;

        Protocol::BoardID id { 0u };
        Net::Socket socket { 0 };
    };

    static_assert_fit_half_cacheline(Client);


    /** @brief Construct the network module */
    NetworkModule(void);

    /** @brief Destruct the network module */
    ~NetworkModule(void);


    /** @brief Tick called at tick rate */
    void tick(Scheduler &scheduler) noexcept;

    /** @brief Start discovery */
    void discover(Scheduler &scheduler) noexcept;

    /** @brief Tries to add data to the ring buffer */
    [[nodiscard]] bool write(const std::uint8_t *data, std::size_t size) noexcept;

private:
    Protocol::BoardID _boardID { 0u };
    Protocol::ConnectionType _connectionType { Protocol::ConnectionType::None };
    Protocol::NodeDistance _nodeDistance { 0u };
    bool _isBinded { false };

    Net::Socket _udpBroadcastSocket { -1 }; // Socket used to send and receive msg over UDP
    Net::Socket _masterSocket { -1 }; // Socket used to exchange with master (act as client)
    Net::Socket _slavesSocket { -1 }; // Socket used to exchange with multiple slaves (act as server)

    Core::Vector<Client, std::uint16_t> _clients;
    NetworkBuffer _buffer { NetworkBuffer(NetworkBufferSize) };

    /** @brief Try to bind previouly opened UDP broadcast socket */
    [[nodiscard]] bool tryToBindUdp(void);

    /** @brief Listen to connected boards that are in client mode */
    void processClients(Scheduler &scheduler);

    /** @brief Process master connection */
    void processMaster(Scheduler &scheduler);

    /** @brief Discovery function that read and process near board message */
    void discoveryScan(Scheduler &scheduler);

    /** @brief Discovery function that emit on UDP broadcast address */
    void discoveryEmit(Scheduler &scheduler) noexcept;

    /** @brief Analyse every available UDP endpoints */
    void analyzeUdpEndpoints(const std::vector<Endpoint> &udpEndpoints, Scheduler &scheduler) noexcept;

    /** @brief Init a new connection to a new master (server) endpoint */
    void initNewMasterConnection(const Endpoint &masterEndpoint, Scheduler &scheduler) noexcept;

    /** @brief Start ID request & assignment (blocking) procedure with the master */
    void startIDRequestToMaster(const Endpoint &masterEndpoint, Scheduler &scheduler);

    /** @brief Notify boards that connection with the studio has been lost & close all clients */
    void notifyDisconnectionToClients(void);

    /** @brief Send a packet (ensure that everything is transmitted) */
    [[nodiscard]] bool sendPacket(const Net::Socket socket, Protocol::WritablePacket &packet) noexcept
    {
        const auto total = packet.totalSize();
        auto current = packet.totalSize();

        do {
            const auto size = ::send(
                socket,
                _buffer.data() + (total - current),
                current,
                0
            );
            if (size > 0) {
                current -= size;
            } else
                return false;
        } while (current);
        return true;
    }
};

static_assert_fit_cacheline(NetworkModule);
