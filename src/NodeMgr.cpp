#include "NodeMgr.hpp"
#include "LogMgr.hpp"
#include "TapLan.hpp"

#define cfgData TapLan::config_

const char* TAG = "[NodeMgr]";

NodeMgr::NodeMgr(): netNum_(cfgData.netNum), netNumLen_(cfgData.netNumLen),
                    verNum_(0), tcpSockPtr_(nullptr)
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
    n->mac = macNum;
    n->status = NODE_ONLINE;
    n->lastSeen = time(nullptr);

    std::unique_lock<std::shared_mutex> wLock(rwMutex_);
    addrPool_.set(hostNum);
    macToNodeMap_[macNum] = n;
    ++verNum_;

    return n;
}

std::shared_ptr<Node> NodeMgr::addNode(uint64_t macNum, Node& node)
{
    std::shared_ptr n = std::make_shared<Node>();
    memcpy(n.get(), &node, sizeof(Node));

    std::unique_lock<std::shared_mutex> wLock(rwMutex_);
    uint32_t hostNumMask = static_cast<uint32_t>(1 << (32 - netNumLen_)) - 1;
    uint32_t hostNum = ntohl(n->ipv4Addr.s_addr) & hostNumMask;
    addrPool_.set(hostNum);
    macToNodeMap_[macNum] = n;

    return n;
}

std::shared_ptr<Node> NodeMgr::delNode(uint64_t macNum)
{
    std::shared_ptr<Node> n = findNode(macNum);
    if (!n)
        return nullptr;

    std::unique_lock<std::shared_mutex> wLock(rwMutex_);
    uint32_t hostNumMask = static_cast<uint32_t>(1 << (32 - netNumLen_)) - 1;
    uint32_t hostNum = ntohl(n->ipv4Addr.s_addr) & hostNumMask;
    addrPool_.reset(hostNum);
    macToNodeMap_.erase(macNum);
    ++verNum_;

    return n;
}

std::shared_ptr<Node> NodeMgr::findNode(uint64_t macNum)
{
    std::shared_lock<std::shared_mutex> rLock(rwMutex_);
    auto it = macToNodeMap_.find(macNum);
    if (it == macToNodeMap_.end())
        return nullptr;

    return it->second;
}

bool NodeMgr::setNodeStatus(uint64_t macNum, uint8_t status)
{
    std::shared_ptr<Node> n = findNode(macNum);
    if (!n)
        return false;

    std::unique_lock<std::shared_mutex> wLock(rwMutex_);
    n->status = status;
    ++verNum_;

    return true;
}

// TODO: support sync node status
void NodeMgr::server()
{
    tcpSockPtr_ = new TcpSocket(cfgData.localPort);
    if (!tcpSockPtr_->isFdValid() || !tcpSockPtr_->listen(5)) {
        LOGF(TAG, "Trying to run in server mode failed.");
        cfgData.isRunning = false;
        return ;
    }

    std::vector<TapLanPollFd> pfds;
    std::vector<TcpSocket> clients;
    std::map<TapLanSocket, uint64_t> sockToMacMap;
    pfds.push_back({ static_cast<TapLanSocket>(*tcpSockPtr_), POLLIN, 0 });
    while (cfgData.isRunning) {
        int pollCnt = TapLanPoll(pfds.data(), pfds.size(), 3000);
        if (pollCnt < 0) {
            LOGE(TAG, "TapLanPoll failed.");
            continue;
        }

        size_t pfdsLen = pfds.size();
        if (pfds.begin()->revents != 0) {
            --pollCnt;
            TapLanSocket tcpFd = INVALID_SOCKET;
            sockaddr_in6 addr;
            if (tcpSockPtr_->accept(tcpFd, addr)) {
                pfds.push_back({ tcpFd, POLLIN, 0 });
                clients.push_back({ tcpFd, addr });
            } else {
                LOGE(TAG, "accept failed.");
            }
        }
        for (int i = pfdsLen - 1; pollCnt && i > 0; --i) {
            if (pfds[i].revents != 0) {
                --pollCnt;
                uint8_t recvBuf[65536];
                uint8_t sendBuf[65536];
                TcpSocket& client = clients[i - 1];
                ssize_t recvBytes = client.recv(recvBuf, sizeof(recvBuf));
                if (recvBytes == 0) {   // 对方关闭连接
                    auto it = sockToMacMap.find(static_cast<TapLanSocket>(client));
                    if (it != sockToMacMap.end()) {
                        if (!setNodeStatus(it->second, NODE_OFFLINE))
                            LOGW(TAG, "set node status failed.");
                        sockToMacMap.erase(it->first);
                    }
                    pfds.erase(pfds.begin() + i);
                    clients.erase(clients.begin() + i - 1);
                    continue;
                } else if (recvBytes != sizeof(SyncMessage)) {  // 接收消息格式不对
                    continue;
                }

                // 处理消息
                const SyncMessage& reqMsgHdr = reinterpret_cast<const SyncMessage&>(recvBuf);
                if (reqMsgHdr.key) {
                    // TODO: check if the key is valid.
                }
                handleRequest(client, reqMsgHdr);
            }
        }
    }
}

bool NodeMgr::handleRequest(TcpSocket& client, const SyncMessage& reqMsgHdr)
{
    uint8_t sndBuf[65536];
    size_t sendBytes = 0;

    SyncMessage& rspMsgHdr = reinterpret_cast<SyncMessage&>(*sndBuf);
    std::memcpy(&rspMsgHdr, &reqMsgHdr, sizeof(SyncMessage));
    sendBytes += sizeof(SyncMessage);

    switch(reqMsgHdr.op) {
        case OP_REQ_IP: {
            rspMsgHdr.op = OP_RESP_IP;

            sockaddr_in6 addr;
            client.getRemoteAddr(&addr);
            std::shared_ptr<Node> n = addNode(&addr, static_cast<uint64_t>(rspMsgHdr.mac));
            if (!n) {
                LOGE(TAG, "add node failed.");
                return false;
            }

            RespIpMessage& rspMsg = reinterpret_cast<RespIpMessage&>(*(sndBuf + sendBytes));
            rspMsg.netIDLen = cfgData.netNumLen;
            rspMsg.ipv4Addr = n->ipv4Addr;
            sendBytes += sizeof(RespIpMessage);

            break;
        }
        case OP_REQ_SYNC_NODE: {
            rspMsgHdr.op = OP_RESP_SYNC_NODE;

            RespNodeStatusMessage& rspMsg = reinterpret_cast<RespNodeStatusMessage&>(*(sndBuf + sendBytes));
            rspMsg.verNum = verNum_;
            rspMsg.numsOfNode = 0;
            sendBytes += sizeof(RespNodeStatusMessage);

            forEach([&](uint64_t k, std::shared_ptr<Node> v) {
                std::memcpy(sndBuf + sendBytes, v.get(), sizeof(Node));
                sendBytes += sizeof(Node);
                ++rspMsg.numsOfNode;
            }, false);

            break;
        }
        default:
            LOGW(TAG, "unknown req: %u.", reqMsgHdr.op);
            return false;
    }

    client.send(sndBuf, sendBytes);
    return true;
}

void NodeMgr::client()
{
    sockaddr_in6 serverAddr{};
    serverAddr.sin6_family = AF_INET6;
    std::memcpy(&serverAddr.sin6_addr, &cfgData.remoteAddr, sizeof(in6_addr));
    serverAddr.sin6_port = cfgData.remotePort;

    bool isConnected = false;
    bool hasIPv4Addr = false;
    bool isSync = false;
    uint8_t sndBuf[65536];
    uint8_t rcvBuf[65536];

    auto retryConnect = [&]() {
        if (isConnected) {
            return isConnected;
        }

        delete tcpSockPtr_;
        tcpSockPtr_ = new TcpSocket(cfgData.localPort, serverAddr);
        if (!tcpSockPtr_->isFdValid()) {
            LOGE(TAG, "create tcp socket failed.");
            std::this_thread::sleep_for(std::chrono::seconds(3));
            return isConnected;
        }

        while (!isConnected && cfgData.isRunning) {
            if (!tcpSockPtr_->connect()) {
                LOGE(TAG, "Can not connect to server, retrying ...");
                std::this_thread::sleep_for(std::chrono::seconds(3));
                continue;
            }
            isConnected = true;
        }

        return isConnected;
    };

    auto getAndSetIPv4Addr = [&]() {
        if (hasIPv4Addr)
            return hasIPv4Addr;

        SyncMessage reqMsgHdr{};
        reqMsgHdr.op = OP_REQ_IP;
        reqMsgHdr.mac = cfgData.mac;
        while (!hasIPv4Addr && cfgData.isRunning) {
            tcpSockPtr_->send(&reqMsgHdr, sizeof(SyncMessage));

            ssize_t recvBytes = tcpSockPtr_->recv(rcvBuf, sizeof(rcvBuf));
            if (recvBytes == 0) {
                LOGW(TAG, "server has close, retrying to connect...");
                isConnected = false;
                while (!retryConnect() && cfgData.isRunning);
                continue;
            } else if (recvBytes < sizeof(SyncMessage)) {
                LOGE(TAG, "recvBytes[%ld] is not correct.", recvBytes);
                std::this_thread::sleep_for(std::chrono::seconds(3));
                continue;
            }

            hasIPv4Addr = handleResponse(rcvBuf, recvBytes);
        }

        return hasIPv4Addr;
    };

    auto syncNodeStatus = [&]() {
        SyncMessage reqMsgHdr{};
        reqMsgHdr.op = OP_REQ_SYNC_NODE;
        reqMsgHdr.mac = cfgData.mac;

        while (!isSync && cfgData.isRunning) {
            tcpSockPtr_->send(&reqMsgHdr, sizeof(SyncMessage));

            ssize_t recvBytes = tcpSockPtr_->recv(rcvBuf, sizeof(rcvBuf));
            if (recvBytes == 0) {
                LOGW(TAG, "server has close, retrying to connect...");
                isConnected = false;
                while (!retryConnect() && cfgData.isRunning);
                hasIPv4Addr = false;
                while (!getAndSetIPv4Addr() && cfgData.isRunning);
                continue;
            } else if (recvBytes < sizeof(SyncMessage)) {
                LOGE(TAG, "recvBytes[%ld] is not correct.", recvBytes);
                std::this_thread::sleep_for(std::chrono::seconds(3));
                continue;
            }

            isSync = handleResponse(rcvBuf, recvBytes);
        }

        return isSync;
    };

    do {
        while (!retryConnect() && cfgData.isRunning);
        while (!getAndSetIPv4Addr() && cfgData.isRunning);
        while (!syncNodeStatus() && cfgData.isRunning);
        ssize_t recvBytes = tcpSockPtr_->recv(rcvBuf, sizeof(rcvBuf));
        if (recvBytes == 0) {
            isConnected = false;
            hasIPv4Addr = false;
            isSync = false;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    } while (cfgData.isRunning);
}

bool NodeMgr::handleResponse(uint8_t* rcvBuf, size_t recvbytes)
{
    size_t offset = 0;
    SyncMessage& rspMsgHdr = reinterpret_cast<SyncMessage&>(*rcvBuf);
    offset += sizeof(SyncMessage);
    switch (rspMsgHdr.op) {
    case OP_RESP_IP: {
        RespIpMessage& rspMsg = reinterpret_cast<RespIpMessage&>(*(rcvBuf + offset));
        offset += sizeof(RespIpMessage);
        netNumLen_ = rspMsg.netIDLen;
        uint32_t subnetMask = ~(static_cast<uint32_t>(1 << (32 - rspMsg.netIDLen)) - 1);
        netNum_ = ntohl(rspMsg.ipv4Addr.s_addr) & subnetMask;
        TapDevPtr->setIpv4Addr(&rspMsg.ipv4Addr, rspMsg.netIDLen);
        } break;
    case OP_RESP_SYNC_NODE: {
        RespNodeStatusMessage& rspMsg = reinterpret_cast<RespNodeStatusMessage&>(*(rcvBuf + offset));
        offset += sizeof(RespNodeStatusMessage);
        verNum_ = rspMsg.verNum;

        if (rspMsg.numsOfNode * sizeof(Node) != recvbytes - offset) {
            LOGE(TAG, "handle OP_RESP_SYNC_NODE failed.");
            std::this_thread::sleep_for(std::chrono::seconds(3));
            return false;
        }

        size_t numsOfNode = rspMsg.numsOfNode;
        while (numsOfNode-- && (recvbytes - offset) >= sizeof(Node)) {
            Node& n = reinterpret_cast<Node&>(*(rcvBuf + offset));
            if (IN6_IS_ADDR_UNSPECIFIED(&n.ipv6Addr)) {
                std::memcpy(&n.ipv6Addr, &cfgData.remoteAddr, sizeof(in6_addr));
            }
            offset += sizeof(Node);
            addNode(n.mac, n);
        }
    } break;
    default:
        LOGE(TAG, "unknown resp: %u", rspMsgHdr.op);
        break;
    }

    return true;
}
