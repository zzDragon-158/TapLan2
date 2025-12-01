#include "NodeMgr.hpp"
#include "LogMgr.hpp"

const char* TAG = "[NodeMgr]";

NodeMgr::NodeMgr(uint32_t netNum, uint8_t netNumLen): netNum_(netNum), netNumLen_(netNumLen)
{
    addrPool_.set(0);
    addrPool_.set(addrPool_.size() - 1);
}

NodeMgr::~NodeMgr()
{
    // pass
}

std::shared_ptr<Node> NodeMgr::addNode(const sockaddr_in6* addr, uint64_t macNum)
{
    std::shared_ptr<Node> n = findNode(macNum);
    if (n)
        return n;

    uint32_t hostNum = 0;
    for (size_t i = 1; i < addrPool_.size() - 1; ++i) {
        if (!addrPool_.test(i)) {
            hostNum = i;
            break;
        }
    }
    if (hostNum == 0) {
        LOGE(TAG, "no enough addr for allocating.");
        return nullptr;
    }

    n = std::make_shared<Node>();
    memcpy(&n->ipv6Addr, &addr->sin6_addr, sizeof(in6_addr));
    n->ipv6Port = addr->sin6_port;
    n->ipv4Addr.s_addr = htonl(netNum_ + hostNum);
    n->mac.num = macNum;
    n->status = NodeStatus_ONLINE;
    n->lastSeen = time(nullptr);

    addrPool_.set(hostNum);
    macToNodeMap_[macNum] = n;

    return n;
}

std::shared_ptr<Node> NodeMgr::addNode(uint64_t macNum, Node& node)
{
    std::shared_ptr n = std::make_shared<Node>();
    memcpy(n.get(), &node, sizeof(Node));

    // TODO: addrPool_.set(hostNum)
    macToNodeMap_[macNum] = n;

    return n;
}

std::shared_ptr<Node> NodeMgr::delNode(uint64_t macNum)
{
    std::shared_ptr<Node> n = findNode(macNum, true);
    if (!n)
        return nullptr;

    uint32_t hostNum = n->ipv4Addr.s_addr & (1 << (32 - netNumLen_) - 1);
    addrPool_.reset(hostNum);
    macToNodeMap_.erase(macNum);

    return n;
}

std::shared_ptr<Node> NodeMgr::findNode(uint64_t macNum, bool isLocked)
{
    auto it = macToNodeMap_.find(macNum);
    if (it == macToNodeMap_.end())
        return nullptr;

    return it->second;
}

bool NodeMgr::setNodeStatus(uint64_t macNum, NodeStatus status)
{
    std::shared_ptr<Node> n = findNode(macNum);
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
