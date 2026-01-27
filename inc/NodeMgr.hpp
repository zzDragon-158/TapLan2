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
#ifdef      _WIN32
// #include    <WS2tcpip.h>
#include    "WinHeaders.hpp"

#elif       __linux__
#include    <arpa/inet.h>

#else
#error      "unsupported platform!"

#endif
#include    "Socket.hpp"

typedef enum {
    NODE_ONLINE = 0,
    NODE_OFFLINE,
} NodeStatus;

enum OP_TYPE {
    OP_REQ_IP = 0,
    OP_RESP_IP,
    OP_REQ_SYNC_NODE,
    OP_RESP_SYNC_NODE,
    OP_MOD,
};

// TODO: maybe need this in future
// typedef enum {
//     NODE_CONNECTED = 0,
//     NODE_DISCONNECTED,
//     NODE_INFO_UPDATED,
// } NodeEvent;

#pragma pack(push, 1)
struct Mac {
    uint8_t addr[6];

    operator uint64_t() {
        uint64_t num = 0;
        __builtin_memcpy(&num, addr, 6);
        return num;
    };
    Mac& operator =(const Mac& m) {
        __builtin_memcpy(&this->addr, &m.addr, 6);
        return *this;
    }
    Mac& operator =(const uint64_t& m) {
        __builtin_memcpy(&this->addr, &m, 6);
        return *this;
    }
};

struct SyncMessage {
    Mac         mac;
    uint8_t     op;         // 1: reqIP; 2: respIP; 3: reqNodeStatus; 4: respNodeStatus;
    uint8_t     reserved[1];
    uint8_t     key[16];
};

struct RespIpMessage {
    in_addr     ipv4Addr;
    uint8_t     netIDLen;
    uint8_t     reserved[3];
};

struct RespNodeStatusMessage {
    uint32_t    verNum;
    uint32_t    numsOfNode;
};

struct Node {
    time_t      lastSeen;
    Mac         mac;
    uint8_t     reserved[2];
    in6_addr    ipv6Addr;
    in_addr     ipv4Addr;
    uint16_t    ipv6Port;
    uint8_t     status;
    uint8_t     reserved1[1];
};
#pragma pack(pop)

class NodeMgr {
public:
    NodeMgr();
    ~NodeMgr();
    std::shared_ptr<Node>   addNode(const sockaddr_in6* addr, uint64_t macNum);
    std::shared_ptr<Node>   addNode(uint64_t macNum, Node& node);
    std::shared_ptr<Node>   delNode(uint64_t macNum);
    std::shared_ptr<Node>   findNode(uint64_t macNum);
    bool        setNodeStatus(uint64_t macNum, uint8_t status);
    size_t      getNodeNums() { return macToNode_.size(); };
    template<typename Func>
    void forEach(Func&& f, bool isWrite)
    {
        if (isWrite) {
            std::unique_lock<std::shared_mutex> wLock(rwMutex_);
            for (auto& [key, value] : macToNode_) {
                f(key, value);
            }
        } else {
            std::shared_lock<std::shared_mutex> rLock(rwMutex_);
            for (auto& [key, value] : macToNode_) {
                f(key, value);
            }
        }
    }
    void server();
    void client();

private:
    uint32_t netNum_;
    uint8_t netNumLen_;
    std::bitset<256> addrPool_;
    std::map<uint64_t, std::shared_ptr<Node>> macToNode_;
    uint32_t verNum_;
    std::shared_mutex rwMutex_;
    TcpSocket* tcpSockPtr_;
    std::vector<TapLanPollFd> pfds_;
    std::vector<TcpSocket> clients_;
    std::map<TapLanSocket, uint64_t> sockToMac_;
    std::unordered_map<uint64_t, std::shared_ptr<Node>> activeDeltaBuffer_;
    std::unordered_map<uint64_t, std::shared_ptr<Node>> processingBuffer_;

    bool handleRequest(TcpSocket& client, const SyncMessage& reqMsgHdr);
    bool syncNodeStatus();
    bool handleResponse(uint8_t* rcvBuf, size_t bufLen);
};
