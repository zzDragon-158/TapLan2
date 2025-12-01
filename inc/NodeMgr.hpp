#pragma     once
#include    <cstdint>
#include    <cstring>
#include    <ctime>
#include    <memory>
#include    <map>
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

typedef enum {
    NODE_ONLINE = 0,
    NODE_OFFLINE,
} NodeStatus;

// TODO: maybe need this in future
// typedef enum {
//     NODE_CONNECTED = 0,
//     NODE_DISCONNECTED,
//     NODE_INFO_UPDATED,
// } NodeEvent;

#pragma pack(push, 1)
union Mac {
    uint64_t    num;
    uint8_t     addr[8];
};

struct SyncMessage {
    uint8_t     op;         // 1: reqIP; 2: respIP; 3: reqNodeStatus; 4: respNodeStatus;
    uint8_t     netIDLen;
    in_addr     ipv4Addr;
    Mac         mac;
    uint16_t    numsOfNode;
};

struct Node {
    in6_addr    ipv6Addr;
    uint16_t    ipv6Port;
    in_addr     ipv4Addr;
    Mac         mac;
    uint16_t    status;
    time_t      lastSeen;
};
#pragma pack(pop)

class NodeMgr {
public:
    NodeMgr(uint32_t netNum = ((192 << 24) + (168 << 16) + (208 << 8)), uint8_t netNumLen = 24);
    ~NodeMgr();
    std::shared_ptr<Node>   addNode(const sockaddr_in6* addr, uint64_t macNum);
    std::shared_ptr<Node>   addNode(uint64_t macNum, Node& node);
    std::shared_ptr<Node>   delNode(uint64_t macNum);
    std::shared_ptr<Node>   findNode(uint64_t macNum, bool isLocked = false);
    bool        setNodeStatus(uint64_t macNum, NodeStatus status);
    size_t      getNodeNums() { return macToNodeMap_.size(); };
    template<typename Func>
    void forEach(Func&& f)
    {
        std::unique_lock<std::shared_mutex> wLock(rwMutex_);
        for (auto& it : macToNodeMap_) {
            auto& key = it.first;
            auto& value = it.second;
            f(key, value);
        }
    }

private:
    uint32_t netNum_;
    uint8_t netNumLen_;
    std::bitset<256> addrPool_;
    std::map<uint64_t, std::shared_ptr<Node>> macToNodeMap_;
    std::shared_mutex rwMutex_;
};
