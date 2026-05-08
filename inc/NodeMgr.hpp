#pragma     once
#include    <cstdint>
#include    <cstring>
#include    <ctime>
#include    <memory>
#include    <map>
#include    <unordered_map>
#include    <bitset>
#include    <mutex>
#include    <shared_mutex>
#include    "Common.hpp"
#include    "BsdSock.hpp"

enum class NodeStatus: uint8_t {
    offline = 0,
    online,
};

enum class SyncStatus {
    outOfSync = 0,
    connected,
    ipGot,
    synced,
};

enum class OP: uint16_t {
    getIP = 0,
    assignIP,
    reqSync,
    respSync,
    modNode,
};

struct Node {
    time_t      lastSeen;
    Mac         mac;
    uint8_t     resv[2];
    // 16 bytes    
    in6_addr    ipv6Addr;
    // 32 bytes
    in_addr     ipv4Addr;
    uint16_t    ipv6Port;
    NodeStatus  status;
    uint8_t     resv1[1];
    // 40 bytes
};

struct SyncMsgHdr {
    Mac         mac;
    OP          op;
    // 8 bytes
    uint8_t     key[16];
    // 16 bytes
    uint16_t    port;
    uint16_t    msgLen;
    uint8_t     resv[4];
    // 24 bytes
    uint8_t     msgBody[0];
};

struct IPMsg {
    in_addr     ipv4Addr;
    uint8_t     netIDLen;
    uint8_t     resv[3];
};

struct SyncNodeMsg {
    uint32_t    verNum;
    uint32_t    numsOfNode;
    Node        nodes[0];
};

using NodeSPtr = std::shared_ptr<Node>;
using WLock = std::unique_lock<std::shared_mutex>;
using RLock = std::shared_lock<std::shared_mutex>;

class NodeMgr {
public:
    NodeMgr();
    ~NodeMgr();
    NodeSPtr    addNode(const sockaddr_in6* addr, uint64_t macNum);
    NodeSPtr    findNode(uint64_t macNum);
    void        setSockaddr(sockaddr_in6& addr, NodeSPtr n);
    template<typename Func>
    void forEach(Func&& f)
    {
        RLock lock(rwMutex_);
        for (auto& [key, value] : macToNode_) {
            f(key, value);
        }
    }
    void server();
    void client();

private:
    static constexpr char TAG[] = "[NodeMgr]";

    uint32_t netNum_;
    uint8_t netNumLen_;
    std::bitset<256> addrPool_;

    std::shared_mutex rwMutex_;
    std::map<uint64_t, NodeSPtr> macToNode_;
    std::unordered_map<uint64_t, NodeSPtr> activeDeltaBuffer_;
    std::unordered_map<uint64_t, NodeSPtr> processingBuffer_;
    uint32_t verNum_;

    sockaddr_in6 serverAddr_;
    SyncStatus syncStatus_;
    TcpSockSPtr tcpSockSPtr_;
    std::vector<UnivPollFd> pfds_;
    std::vector<TcpSockSPtr> clients_;

    void reset();
    NodeSPtr addNode(uint64_t macNum, Node& node);
    NodeSPtr delNode(uint64_t macNum);
    bool setNodeStatus(uint64_t macNum, NodeStatus status);

    void pollAndProcess();
    bool syncNodeToClients();

    bool connectToServer();
    bool reqIPFromServer();
    bool syncNodeFromServer();

    bool handleSyncMsg(uint8_t* msg, size_t msgLen, TcpSockSPtr srcSock);
    bool handleIPReq(uint8_t* reqMsg, TcpSockSPtr client);
    bool handleIPMsg(uint8_t* respMsg);
    bool handleSyncNodeReq(uint8_t* reqMsg, TcpSockSPtr client);
    bool handleSyncNodeMsg(uint8_t* respMsg);
};
