/**
 * @ Author: Paul Creze
 * @ Description: Network module
 */

#pragma once

// C++ standard library
#include <cstring>

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
#include "Types.hpp"
#include "Module.hpp"

/** @brief Board module responsible of network communication */
class alignas_cacheline NetworkModule : public Module
{
public:
    /** @brief Size of the network buffers & areas */
    static constexpr std::size_t TransferBufferSize = 8192;
    static constexpr std::size_t ReceptionBufferSize = 4096;
    static constexpr std::size_t NetworkBufferSize = TransferBufferSize + ReceptionBufferSize;
    static constexpr std::size_t AssignAreaSize = 256;
    static constexpr std::size_t InputsAreaSize = 3840;

    /** @brief Reception buffer offsets */
    static constexpr std::size_t AssignOffset = TransferBufferSize;
    static constexpr std::size_t InputsOffset = TransferBufferSize + AssignAreaSize;

    using Vector = Core::Vector<std::uint8_t, std::uint16_t>;

    /** @brief Network buffer used for all packet emission and reception */
    class NetworkBuffer : public Vector
    {

        public:

            NetworkBuffer(short unsigned int networkBufferSize) : Vector(static_cast<short unsigned int>(networkBufferSize)) {  }

            void setTransferSize(short unsigned int size) noexcept { this->setSize(size); }

            void reset(void) noexcept
            {
                std::memset(data(), 0, capacity());
                setSize(0);
            }

        private:

    };

    /*
        Network buffer representation:

        |     TRANSFER [8192]    |              RECEPTION [4096]               |
        |                        |                                             |
        |                        |  Self assigns [256]     Slaves data [3840]  |
        |________________________|______________________|______________________|

                                   TOTAL [12288]
    */

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

    /** @brief Network buffer used for all packet emission and reception */
    NetworkBuffer _networkBuffer;

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
    void processAssignmentFromMaster(Protocol::ReadablePacket &&assignmentPacket);


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

    /** @brief Start ID request & assignment (blocking) procedure with the master */
    void startIDRequestToMaster(const Endpoint &masterEndpoint, Scheduler &scheduler);

    /** @brief Notify boards that connection with the studio has been lost & close all clients */
    void notifyDisconnectionToClients(void);
};

// static_assert_fit_cacheline(NetworkModule);
