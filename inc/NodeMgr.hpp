#pragma     once
#include    <cstdint>
#include    <cstring>
#include    <ctime>
#include    <memory>
#include    <map>
#include    <unordered_map>
#include    <bitset>
#include    <functional>
#include    <mutex>
#include    <shared_mutex>
#include    "BsdSock.hpp"
#include    "Common.hpp"

enum NodeStatus {
    NODE_ONLINE = 0,
    NODE_OFFLINE,
};

enum ConnStatus {
    NOT_CONNECTED = 0,
    CONNECTED,
    GOT_IP,
    SYNCED,
};

enum OP_TYPE {
    OP_REQ_IP = 0,
    OP_RESP_IP,
    OP_REQ_SYNC_NODE,
    OP_RESP_SYNC_NODE,
    OP_MOD,
};

#pragma pack(push, 1)
struct Node {
    time_t      lastSeen;
    Mac         mac;
    uint8_t     resv[2];
    // 16 bytes    
    in6_addr    ipv6Addr;
    in_addr     ipv4Addr;
    uint16_t    ipv6Port;
    uint8_t     status;
    uint8_t     resv1[1];
    // 24 bytes
};

struct SyncMsgHdr {
    Mac         mac;
    uint16_t    op;         // 1: reqIP; 2: respIP; 3: reqNodeStatus; 4: respNodeStatus;
    // 8 bytes
    uint8_t     key[16];
    // 16 bytes
    uint16_t    msgLen;
    uint8_t     resv[6];
    // 24 bytes
    uint8_t     msgBody[];
};

struct IPMsg {
    in_addr     ipv4Addr;
    uint8_t     netIDLen;
    uint8_t     resv[3];
};

struct SyncNodeMsg {
    uint32_t    verNum;
    uint32_t    numsOfNode;
    Node        nodes[];
};
#pragma pack(pop)

using NodeSPtr = std::shared_ptr<Node>;
using WLock = std::unique_lock<std::shared_mutex>;
using RLock = std::shared_lock<std::shared_mutex>;

class NodeMgr {
public:
    NodeMgr();
    ~NodeMgr();
    NodeSPtr    addNode(const sockaddr_in6* addr, uint64_t macNum);
    NodeSPtr    addNode(uint64_t macNum, Node& node);
    NodeSPtr    delNode(uint64_t macNum);
    NodeSPtr    findNode(uint64_t macNum);
    bool        setNodeStatus(uint64_t macNum, uint8_t status);
    size_t      getNodeNums() { return macToNode_.size(); };
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
    sockaddr_in6 serverAddr_;
    uint8_t connStatus_;
    uint32_t netNum_;
    uint8_t netNumLen_;
    std::bitset<256> addrPool_;
    std::map<uint64_t, NodeSPtr> macToNode_;
    uint32_t verNum_;
    std::shared_mutex rwMutex_;
    TcpSock* tcpSockPtr_;
    std::vector<TapLanPollFd> pfds_;
    std::vector<TcpSockSPtr> clients_;
    std::unordered_map<uint64_t, NodeSPtr> activeDeltaBuffer_;
    std::unordered_map<uint64_t, NodeSPtr> processingBuffer_;

    void reset();

    bool connectToServer();
    bool reqIPFromServer();
    bool syncNodeFromServer();
    bool syncNodeToClients();

    bool handleSyncMsg(uint8_t* msg, size_t msgLen, TcpSock* srcSock);
    bool handleIPReq(uint8_t* reqMsg, TcpSock* client);
    bool handleIPMsg(uint8_t* respMsg);
    bool handleSyncNodeReq(uint8_t* reqMsg, TcpSock* client);
    bool handleSyncNodeMsg(uint8_t* respMsg);
};
