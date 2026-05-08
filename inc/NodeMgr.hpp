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
#include    "DataSec.hpp"

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

struct NodeInfo {
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
using NodeInfoSPtr = std::shared_ptr<NodeInfo>;

struct NodeSession {
    NodeInfoSPtr nodeInfo;
    AeadSessSPtr aeadSess;
};
using NodeSessSPtr = std::shared_ptr<NodeSession>;

struct SyncMsgHdr {
    Mac         mac;
    OP          op;
    // 8 bytes
    Nonce       nonce;
    // 20 bytes
    uint16_t    port;
    uint16_t    msgLen;
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
    NodeInfo    nodes[0];
};

using WLock = std::unique_lock<std::shared_mutex>;
using RLock = std::shared_lock<std::shared_mutex>;

class NodeMgr {
public:
    NodeMgr();
    ~NodeMgr();
    NodeSessSPtr addNode(const sockaddr_in6& addr, const SyncMsgHdr& syncMsgHdr);
    NodeSessSPtr findNode(const Mac& mac);
    AeadSessSPtr getAeadSess() { return aeadSession_; };
    static void setSockaddr(sockaddr_in6& addr, NodeInfoSPtr n);
    template<typename Func>
    void forEach(Func&& f)
    {
        RLock lock(rwMutex_);
        for (auto& [key, value] : macToNodeSess_) {
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
    std::map<uint64_t, NodeSessSPtr> macToNodeSess_;
    std::unordered_map<uint64_t, NodeInfoSPtr> activeDeltaBuffer_;
    std::unordered_map<uint64_t, NodeInfoSPtr> processingBuffer_;
    uint32_t verNum_;

    AeadSessSPtr aeadSession_;
    SyncStatus syncStatus_;
    TcpSockSPtr tcpSockSPtr_;
    std::vector<UnivPollFd> pfds_;
    std::vector<TcpSockSPtr> clients_;

    void reset();

    uint32_t getHostNum(const in_addr& ipv4Addr);
    NodeInfoSPtr constructNodeInfo(const sockaddr_in6& addr, const Mac& mac, uint32_t hostNum);
    NodeInfoSPtr assignIpHostNumForNode(const sockaddr_in6& addr, const Mac& mac);

    NodeSessSPtr addNode(const Mac& mac, const NodeInfo& node);
    NodeSessSPtr delNode(const Mac& mac);
    bool setNodeStatus(const Mac& mac, NodeStatus status);

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
using NodeMgrSPtr = std::shared_ptr<NodeMgr>;
