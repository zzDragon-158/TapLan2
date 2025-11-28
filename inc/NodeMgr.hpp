#pragma     once
#include    <cstdint>
#include    <cstring>
#include    <map>
#include    <bitset>
#include    <functional>

#ifdef      _WIN32
// #include    <WS2tcpip.h>
#include    "WinHeaders.hpp"

#elif       __linux__
#include    <arpa/inet.h>

#else
#error      "unsupported platform!"

#endif

typedef enum {
    NodeStatus_ONLINE = 0,
    NodeStatus_OFFLINE,
    NUMS_OF_NodeSTATUS,
} NodeStatus;

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
};
#pragma pack(pop)

class NodeMgr {
public:
                NodeMgr(uint32_t netNum = ((192 << 24) + (168 << 16) + (208 << 8)), uint8_t netNumLen = 24);
                ~NodeMgr();
    Node*       newNode(const sockaddr_in6* addr, const uint8_t* mac);
    bool        addNode(Node* n);
    bool        delNode(uint64_t mac);
    Node*       findNode(uint64_t mac);
    uint64_t    getMacNum(const uint8_t* mac);
    bool        setNodeStatus(uint64_t mac, NodeStatus status);
    template<typename Func>
    void forEach(Func&& f)
    {
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
    std::map<uint64_t, Node*> macToNodeMap_;
};
