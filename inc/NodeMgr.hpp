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

#define     NodeMgrPtr      NodeMgr::ptr()

typedef enum {
    NodeSTATUS_ONLINE = 0,
    NodeSTATUS_OFFLINE,
    NUMS_OF_NodeSTATUS,
} NodeStatus;

#pragma pack(push, 1)
struct NodeMessage {
    uint8_t     op;
    uint8_t     mac[6];
    uint8_t     netIDLen;
    uint32_t    ipv4Addr;
    uint8_t     paddings[4];
};

union Mac {
    uint64_t    num;
    uint8_t     addr[8];
};

struct Node {
    in6_addr    ipv6Addr;
    uint16_t    ipv6Port;
    in_addr     ipv4Addr;
    uint16_t    status;
    Mac         mac;
};
#pragma pack(pop)

class NodeMgr {
public:
                NodeMgr();
                ~NodeMgr();
    Node*       newNode(const sockaddr_in6* addr, const uint8_t* mac);
    bool        addNode(Node* n);
    bool        delNode(uint64_t key);
    Node*       findNode(uint64_t key);
    uint64_t    getMacNum(const uint8_t* mac);
    template<typename Func>
    void forEach(Func&& f)
    {
        for (auto& it : macToNodeMap_) {
            auto& key = it.first;
            auto& value = it.second;
            f(key, value);
        }
    }
    static NodeMgr* ptr();

private:
    uint32_t netId_;
    uint8_t netIdLen_;
    std::bitset<256> addrPool_;
    std::map<uint64_t, Node*> macToNodeMap_;
};

inline NodeMgr* NodeMgr::ptr()
{
    static NodeMgr ins;
    return &ins;
}
