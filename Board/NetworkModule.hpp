/**
 * @ Author: Paul Creze
 * @ Description: Network module
 */

#pragma once

// C++ standard library
#include <cstring>
#include <array>

// Network headers
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

// Lexo headers
#include <Core/Vector.hpp>
#include <Protocol/Packet.hpp>
#include <Protocol/Protocol.hpp>
#include <Protocol/ConnectionProtocol.hpp>
#include <Protocol/EventProtocol.hpp>

#include "Types.hpp"
#include "Module.hpp"

/** @brief Board module responsible of network communication */
class alignas_cacheline NetworkModule : public Module
{
public:

    static constexpr Net::Port LexoPort = 4242;

    /** @brief Size of the network buffers & areas */
    static constexpr std::size_t TransferBufferSize = 8192;
    static constexpr std::size_t ReceptionBufferSize = 4096;
    static constexpr std::size_t NetworkBufferSize = TransferBufferSize + ReceptionBufferSize;
    static constexpr std::size_t AssignAreaSize = 256;
    static constexpr std::size_t InputsAreaSize = 3840;

    /** @brief Reception buffer offsets */
    static constexpr std::size_t AssignOffset = TransferBufferSize;
    static constexpr std::size_t InputsOffset = TransferBufferSize + AssignAreaSize;

    /*
        Network buffer representation:

        |     TRANSFER [8192]    |              RECEPTION [4096]               |
        |                        |                                             |
        |                        |  Self assigns [256]     Slaves data [3840]  |
        |________________________|______________________|______________________|

                                   TOTAL [12288]
    */
    class NetworkBuffer
    {
    public:
        /** @brief Add data to the transfer */
        void writeTransfer(const Protocol::Internal::PacketBase &packet) noexcept;


        /** @brief Get transfer range */
        [[nodiscard]] std::size_t transferSize(void) const noexcept { return _transferHead;  }
        [[nodiscard]] const std::uint8_t *transferBegin(void) const noexcept { return _data.data(); }
        [[nodiscard]] const std::uint8_t *transferEnd(void) const noexcept { return _data.data() + _transferHead; }
        [[nodiscard]] std::uint8_t *transferBegin(void) noexcept { return _data.data(); }
        [[nodiscard]] std::uint8_t *transferEnd(void) noexcept { return _data.data() + _transferHead; }
        [[nodiscard]] std::uint8_t *transferRealEnd(void) noexcept { return _data.data() + TransferBufferSize; }
        [[nodiscard]] const std::uint8_t *transferRealEnd(void) const noexcept { return _data.data() + TransferBufferSize; }

        /** @brief Get assign range */
        [[nodiscard]] std::size_t assignSize(void) const noexcept { return _assignHead;  }
        [[nodiscard]] std::uint8_t *assignBegin(void) noexcept { return _data.data() + AssignOffset; }
        [[nodiscard]] std::uint8_t *assignEnd(void) noexcept { return assignBegin() + _assignHead; }
        [[nodiscard]] const std::uint8_t *assignBegin(void) const noexcept { return _data.data() + AssignOffset; }
        [[nodiscard]] const std::uint8_t *assignEnd(void) const noexcept { return assignBegin() + _assignHead; }
        [[nodiscard]] std::uint8_t *assignRealEnd(void) noexcept { return _data.data() + AssignAreaSize; }
        [[nodiscard]] const std::uint8_t *assignRealEnd(void) const noexcept { return _data.data() + AssignAreaSize; }

        /** @brief Get slave data range */
        [[nodiscard]] std::size_t slaveDataSize(void) const noexcept { return _slaveDataHead;  }
        [[nodiscard]] std::uint8_t *slaveDataBegin(void) noexcept { return _data.data() + InputsOffset; }
        [[nodiscard]] std::uint8_t *slaveDataEnd(void) noexcept { return slaveDataBegin() + _slaveDataHead; }
        [[nodiscard]] const std::uint8_t *slaveDataBegin(void) const noexcept { return _data.data() + InputsOffset; }
        [[nodiscard]] const std::uint8_t *slaveDataEnd(void) const noexcept { return slaveDataBegin() + _slaveDataHead; }
        [[nodiscard]] std::uint8_t *slaveDataRealEnd(void) noexcept { return _data.data() + InputsAreaSize; }
        [[nodiscard]] const std::uint8_t *slaveDataRealEnd(void) const noexcept { return _data.data() + InputsAreaSize; }


        /** @brief Increment the transfer head */
        void incrementTransferHead(const std::size_t offset) noexcept { _transferHead += offset; }

        /** @brief Increment the assign head */
        void incrementAssignHead(const std::size_t offset) noexcept { _assignHead += offset; }

        /** @brief Increment the assign head */
        void incrementSlaveDataHead(const std::size_t offset) noexcept { _slaveDataHead += offset; }


        /** @brief Reset the network buffer */
        void reset(void) noexcept
        {
            _transferHead = 0u;
            _assignHead = 0u;
            _slaveDataHead = 0u;
        }

    private:
        std::array<std::uint8_t, NetworkBufferSize> _data {};
        std::size_t _transferHead { 0u };
        std::size_t _assignHead { 0u };
        std::size_t _slaveDataHead { 0u };
    };

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

        // Client network informations
        Net::Socket socket { 0 };
        Net::IP address { 0u };
        Net::Port port { 0u };

        Protocol::BoardID id { 0u };
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

private:
    Protocol::BoardID _boardID { 0u };
    Protocol::ConnectionType _connectionType { Protocol::ConnectionType::None };
    Protocol::NodeDistance _nodeDistance { 0u };
    bool _isBinded { false };

    static NetworkBuffer _NetworkBuffer;

    Net::Socket _udpBroadcastSocket { -1 }; // Socket used to send and receive msg over UDP
    Net::Socket _udpLocalSocket { -1 };
    Net::Socket _masterSocket { -1 }; // Socket used to exchange with master (act as client)
    Net::Socket _slavesSocket { -1 }; // Socket used to exchange with multiple slaves (act as server)

    Core::Vector<Client, std::uint16_t> _clients;

    /* Direct client board(s) temporary index assignment */
    std::uint8_t _selfAssignIndex = 0;


    /** @brief Read data from connected boards that are in client mode (STEP 1) */
    void readClients(Scheduler &scheduler);

    /** @brief Process data from the reception buffer & place the result into the transfer buffer (STEP 2) */
    void processClientsData(Scheduler &scheduler);

    /** @brief Transfer all processed data from the transfer buffer to the master endpoint (STEP 3) */
    void transferToMaster(Scheduler &scheduler);


    /** @brief Read data from a specific connection and place it into the reception buffer at receptionIndex */
    bool readDataFromClient(Client *client, std::size_t &offset);

    /** @brief Accept and proccess new board connections */
    void proccessNewClientConnections(Scheduler &scheduler);

    /** @brief Process & client packet in assigment mode */
    void processClientAssignmentRequest(Client *client, std::size_t &offset);


    /** @brief Process master connection */
    void processMaster(Scheduler &scheduler);

    /** @brief Process ID assignment packet from the master endpoint */
    void processAssignmentFromMaster(Protocol::ReadablePacket &packet);

    /** @brief Process hardware specs packet from the master endpoint */
    void processHardwareSpecsFromMaster(Protocol::ReadablePacket &packet);


    /** @brief Try to bind previouly opened UDP broadcast socket */
    [[nodiscard]] bool tryToBindUdp(void);

    /** @brief Discovery function that read and process near board message */
    void discoveryScan(Scheduler &scheduler);

    /** @brief Discovery function that emit on UDP broadcast address */
    void discoveryEmit(Scheduler &scheduler) noexcept;

    /** @brief Analyse every available UDP endpoints */
    void analyzeUdpEndpoints(const std::vector<Endpoint> &udpEndpoints, Scheduler &scheduler) noexcept;

    /** @brief Set keepalive option on socket */
    void setSocketKeepAlive(const int socket) const noexcept;

    /** @brief Init a new connection to a new master (server) endpoint */
    void initNewMasterConnection(const Endpoint &masterEndpoint, Scheduler &scheduler) noexcept;

    void sendHardwareSpecsToMaster();

    /** @brief Start ID request & assignment (blocking) procedure with the master */
    void startIDRequestToMaster(const Endpoint &masterEndpoint, Scheduler &scheduler);

    /** @brief Notify boards that connection with the studio has been lost & close all clients */
    void notifyDisconnectionToClients(void);

    void processHardwareEvents(Scheduler &scheduler);

    void setSocketReusable(const Net::Socket socket)
    {
        const int enable = 1;
        auto ret = ::setsockopt(
            socket,
            SOL_SOCKET,
            SO_REUSEADDR,
            &enable,
            sizeof(enable)
        );
        if (ret < 0)
            throw std::runtime_error(std::strerror(errno));
    }

};

// static_assert_fit_cacheline(NetworkModule);

#include "NetworkModule.ipp"