#include "NodeMgr.hpp"

NodeMgr::NodeMgr(uint32_t netNum, uint8_t netNumLen): netNum_(netNum), netNumLen_(netNumLen)
{
    addrPool_.set(0);
    addrPool_.set(addrPool_.size() - 1);
}

NodeMgr::~NodeMgr()
{
    auto freeNode = [](uint64_t k, Node* n) {
        delete n;
    };
    forEach(freeNode);
}

Node* NodeMgr::newNode(const sockaddr_in6* addr, const uint8_t* mac)
{
    Node* nodePtr = new Node;
    if (!nodePtr)
        return nullptr;

    memset(nodePtr, 0, sizeof(Node));
    memcpy(&nodePtr->ipv6Addr, &addr->sin6_addr, sizeof(in6_addr));
    nodePtr->ipv6Port = addr->sin6_port;
    memcpy(nodePtr->mac.addr, mac, 6);
    nodePtr->status = NodeStatus_ONLINE;

    uint32_t hostNum = 0;
    for (size_t i = 1; i < addrPool_.size() - 1; ++i) {
        if (!addrPool_.test(i)) {
            hostNum = i;
            break;
        }
    }
    if (hostNum == 0) {
        delete nodePtr;
        return nullptr;
    }
    nodePtr->ipv4Addr.s_addr = htonl(netNum_ + hostNum);

    return nodePtr;
}

bool NodeMgr::addNode(Node* n)
{
    uint32_t hostNum = n->ipv4Addr.s_addr >> netNumLen_;// & (1 << (32 - netNumLen_) - 1);
    if (hostNum == 0 || hostNum == addrPool_.size() - 1) {
        delete n;
        return false;
    }

    addrPool_.set(hostNum);
    macToNodeMap_[n->mac.num] = n;

    return true;
}

bool NodeMgr::delNode(uint64_t mac)
{ 
    Node* n = findNode(mac);
    if (!n)
        return false;

    uint32_t hostNum = n->ipv4Addr.s_addr & (1 << (32 - netNumLen_) - 1);
    delete n;
    addrPool_.reset(hostNum);
    macToNodeMap_.erase(mac);

    return true;
}

Node* NodeMgr::findNode(uint64_t mac)
{
    auto it = macToNodeMap_.find(mac);
    if (it == macToNodeMap_.end())
        return nullptr;

    return it->second;
}

uint64_t NodeMgr::getMacNum(const uint8_t* mac)
{
    Mac m;
    m.num = 0;
    memcpy(m.addr, mac, 6);

    return m.num;
}

bool NodeMgr::setNodeStatus(uint64_t mac, NodeStatus status)
{
    Node* n = findNode(mac);
    if (!n)
        return false;

    n->status = status;

    return true;
}

// void NodeMgr::forEach(const std::function<void(uint64_t, Node*)>& f)
// {
//     for (auto& [key, value]: macToNodeMap_) {
//         f(key, value);
//     }
// }

// TODO: support sync node status
