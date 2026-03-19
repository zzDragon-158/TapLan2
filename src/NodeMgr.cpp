#include "NodeMgr.hpp"
#include "LogMgr.hpp"
#include "TapLan.hpp"

const char* TAG = "[NodeMgr]";

NodeMgr::NodeMgr(): serverAddr_{}, connStatus_(NOT_CONNECTED), 
                    netNum_(g_cfgData.netNum),
                    netNumLen_(g_cfgData.netNumLen),
                    verNum_(0), tcpSockPtr_(nullptr)
{
    serverAddr_.sin6_family = AF_INET6;
    memcpy(&serverAddr_.sin6_addr, &g_cfgData.remoteAddr, sizeof(in6_addr));
    serverAddr_.sin6_port = g_cfgData.remotePort;

    addrPool_.set(0);
    addrPool_.set(addrPool_.size() - 1);
}

NodeMgr::~NodeMgr()
{
    // pass
}

NodeSPtr NodeMgr::addNode(const sockaddr_in6* addr, uint64_t macNum)
{
    NodeSPtr n = findNode(macNum);
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

    WLock wLock(rwMutex_);
    addrPool_.set(hostNum);
    macToNode_[macNum] = n;
    activeDeltaBuffer_[macNum] = n;

    return n;
}

NodeSPtr NodeMgr::addNode(uint64_t macNum, Node& node)
{
    std::shared_ptr n = std::make_shared<Node>();
    std::memcpy(n.get(), &node, sizeof(Node));

    WLock wLock(rwMutex_);
    uint32_t hostNumMask = static_cast<uint32_t>(1 << (32 - netNumLen_)) - 1;
    uint32_t hostNum = ntohl(n->ipv4Addr.s_addr) & hostNumMask;
    addrPool_.set(hostNum);
    macToNode_[macNum] = n;

    return n;
}

NodeSPtr NodeMgr::delNode(uint64_t macNum)
{
    NodeSPtr n = findNode(macNum);
    if (!n)
        return nullptr;

    WLock wLock(rwMutex_);
    uint32_t hostNumMask = static_cast<uint32_t>(1 << (32 - netNumLen_)) - 1;
    uint32_t hostNum = ntohl(n->ipv4Addr.s_addr) & hostNumMask;
    addrPool_.reset(hostNum);
    macToNode_.erase(macNum);

    return n;
}

NodeSPtr NodeMgr::findNode(uint64_t macNum)
{
    RLock rLock(rwMutex_);
    auto it = macToNode_.find(macNum);
    if (it == macToNode_.end())
        return nullptr;

    return it->second;
}

bool NodeMgr::setNodeStatus(uint64_t macNum, uint8_t status)
{
    NodeSPtr n = findNode(macNum);
    if (!n)
        return false;

    WLock wLock(rwMutex_);
    n->status = status;
    activeDeltaBuffer_[macNum] = n;

    return true;
}

void NodeMgr::setSockaddr(sockaddr_in6& addr, NodeSPtr n)
{
    addr.sin6_family = AF_INET6;
    memcpy(&addr.sin6_addr, &n->ipv6Addr, sizeof(n->ipv6Addr));
    addr.sin6_port = n->ipv6Port;
}

void NodeMgr::server()
{
    tcpSockPtr_ = new TcpSocket(g_cfgData.localPort);
    if (!tcpSockPtr_->isFdValid() || !tcpSockPtr_->listen(5)) {
        LOGF(TAG, "Trying to run in server mode failed.");
        g_cfgData.isRunning = false;
        return ;
    }

    pfds_.push_back({ static_cast<SocketFd>(*tcpSockPtr_), POLLIN, 0 });
    while (g_cfgData.isRunning) {
        int pollCnt = TapLanPoll(pfds_.data(), pfds_.size(), 3000);
        if (pollCnt < 0) {
            LOGE(TAG, "TapLanPoll failed.");
            continue;
        }

        size_t pfdsLen = pfds_.size();
        if (pfds_.begin()->revents != 0) {
            --pollCnt;
            SocketFd tcpFd = INVALID_SOCKET;
            sockaddr_in6 addr;
            if (tcpSockPtr_->accept(tcpFd, addr)) {
                pfds_.push_back({ tcpFd, POLLIN, 0 });
                clients_.push_back({ tcpFd, addr });
            } else {
                LOGE(TAG, "accept failed.");
            }
        }
        for (int i = pfdsLen - 1; pollCnt && i > 0; --i) {
            if (pfds_[i].revents != 0) {
                --pollCnt;
                uint8_t recvBuf[65536];
                uint8_t sendBuf[65536];
                TcpSocket& client = clients_[i - 1];
                ssize_t recvBytes = client.recv(recvBuf, sizeof(recvBuf));
                if (recvBytes == 0) {   // 对方关闭连接
                    auto it = sockToMac_.find(static_cast<SocketFd>(client));
                    if (it != sockToMac_.end()) {
                        if (!setNodeStatus(it->second, NODE_OFFLINE))
                            LOGW(TAG, "set node status failed.");
                        sockToMac_.erase(it->first);
                    }
                    pfds_.erase(pfds_.begin() + i);
                    clients_.erase(clients_.begin() + i - 1);
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
        syncNodeStatus();
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
            NodeSPtr n = addNode(&addr, static_cast<uint64_t>(rspMsgHdr.mac));
            if (!n) {
                LOGE(TAG, "add node failed.");
                return false;
            }
            if (n->status != NODE_ONLINE) {
                n->status = NODE_ONLINE;
                activeDeltaBuffer_[rspMsgHdr.mac] = n;
            }

            RespIpMessage& rspMsg = reinterpret_cast<RespIpMessage&>(*(sndBuf + sendBytes));
            rspMsg.netIDLen = g_cfgData.netNumLen;
            rspMsg.ipv4Addr = n->ipv4Addr;
            sendBytes += sizeof(RespIpMessage);

            break;
        }
        case OP_REQ_SYNC_NODE: {
            sockToMac_[static_cast<SocketFd>(client)] = rspMsgHdr.mac;
            rspMsgHdr.op = OP_RESP_SYNC_NODE;

            RespNodeStatusMessage& rspMsg = reinterpret_cast<RespNodeStatusMessage&>(*(sndBuf + sendBytes));
            rspMsg.verNum = verNum_;
            rspMsg.numsOfNode = 0;
            sendBytes += sizeof(RespNodeStatusMessage);

            forEach([&](uint64_t k, NodeSPtr v) {
                std::memcpy(sndBuf + sendBytes, v.get(), sizeof(Node));
                sendBytes += sizeof(Node);
                ++rspMsg.numsOfNode;
            });

            break;
        }
        default:
            LOGW(TAG, "unknown req: %u.", reqMsgHdr.op);
            return false;
    }

    client.send(sndBuf, sendBytes);
    return true;
}

bool NodeMgr::syncNodeStatus()
{
    /* swap buffer */ {
        std::unique_lock<std::shared_mutex> wLock(rwMutex_);
        if (activeDeltaBuffer_.empty())
            return true;

        activeDeltaBuffer_.swap(processingBuffer_);
    }

    // construct sync message
    uint8_t sndBuf[65536];
    size_t sendBytes = 0;
    SyncMessage& syncMsgHdr = reinterpret_cast<SyncMessage&>(*sndBuf);
    sendBytes += sizeof(SyncMessage);
    syncMsgHdr.mac = g_cfgData.mac;
    syncMsgHdr.op = OP_MOD;
    // syncMsgHdr.key = ;
    RespNodeStatusMessage& syncMsg = reinterpret_cast<RespNodeStatusMessage&>(*(sndBuf + sendBytes));
    sendBytes += sizeof(RespNodeStatusMessage);
    syncMsg.numsOfNode = 0;
    syncMsg.verNum = ++verNum_;
    for (auto& [k, v]: processingBuffer_) {
        std::memcpy(sndBuf + sendBytes, v.get(), sizeof(Node));
        sendBytes += sizeof(Node);
        ++syncMsg.numsOfNode;
    }
    processingBuffer_.clear();

    for (auto& client: clients_) {
        if (sockToMac_.find(static_cast<SocketFd>(client)) != sockToMac_.end())
            client.send(sndBuf, sendBytes);
    }

    return true;
}

void NodeMgr::client()
{
    sockaddr_in6 serverAddr{};
    serverAddr.sin6_family = AF_INET6;
    std::memcpy(&serverAddr.sin6_addr, &g_cfgData.remoteAddr, sizeof(in6_addr));
    serverAddr.sin6_port = g_cfgData.remotePort;

    bool hasIPv4Addr = false;
    bool isSync = false;
    uint8_t sndBuf[65536];

    while (g_cfgData.isRunning) {
        bool ok;

        switch (connStatus_) {
        case NOT_CONNECTED:
            ok = connectToServer();
            break;

        case CONNECTED:
            ok = reqIPv4FromServer();
            break;

        case GOT_IP:
        case SYNCED:
            ok = syncNodeFromServer();
            break;

        default:
            LOGE(TAG, "Unknown status[%u].", connStatus_);
        }

        if (!ok)
            std::this_thread::sleep_for(std::chrono::seconds(IO_WAIT_TIME));
    }
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
        TapDevPtr->setIPv4Addr(&rspMsg.ipv4Addr, rspMsg.netIDLen);
        } break;
    case OP_RESP_SYNC_NODE:
    case OP_MOD: {
        RespNodeStatusMessage& rspMsg = reinterpret_cast<RespNodeStatusMessage&>(*(rcvBuf + offset));
        offset += sizeof(RespNodeStatusMessage);
        if ((rspMsgHdr.op == OP_MOD) && (verNum_ + 1 != rspMsg.verNum)) {
            LOGW(TAG, "vernum expect [%u] but [%u]", verNum_ + 1, rspMsg.verNum);
        }
        verNum_ = rspMsg.verNum;

        if (rspMsg.numsOfNode * sizeof(Node) != recvbytes - offset) {
            LOGE(TAG, "handle %u failed.", rspMsgHdr.op);
            return false;
        }

        size_t numsOfNode = rspMsg.numsOfNode;
        while (numsOfNode-- && (recvbytes - offset) >= sizeof(Node)) {
            Node& n = reinterpret_cast<Node&>(*(rcvBuf + offset));
            if (IN6_IS_ADDR_UNSPECIFIED(&n.ipv6Addr)) {
                std::memcpy(&n.ipv6Addr, &g_cfgData.remoteAddr, sizeof(in6_addr));
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

bool NodeMgr::connectToServer()
{
    if (!tcpSockPtr_ || !tcpSockPtr_->isFdValid()) {
        delete tcpSockPtr_;
        tcpSockPtr_ = new TcpSocket(g_cfgData.localPort, serverAddr_);
        if (!tcpSockPtr_->isFdValid()) {
            LOGE(TAG, "Failed to create tcp socket.");
            return false;
        }
    }

    if (!tcpSockPtr_->connect()) {
        LOGE(TAG, "Failed to connect.");
        return false;
    }

    connStatus_ = CONNECTED;
    return true;
}

bool NodeMgr::reqIPv4FromServer()
{
    uint8_t rcvBuf[65536];

    SyncMessage reqMsgHdr{};
    reqMsgHdr.op = OP_REQ_IP;
    TapDevPtr->getMacAddr(reqMsgHdr.mac);

    tcpSockPtr_->send(&reqMsgHdr, sizeof(SyncMessage));
    ssize_t recvBytes = tcpSockPtr_->recv(rcvBuf, sizeof(rcvBuf));
    if (recvBytes == 0) {
        LOGW(TAG, "server has closed the connection.");
        connStatus_ = NOT_CONNECTED;
        return false;
    } else if (recvBytes < sizeof(SyncMessage)) {
        LOGE(TAG, "recvBytes[%ld] is unexpected.", recvBytes);
        return false;
    }

    if (!handleResponse(rcvBuf, recvBytes)) {
        LOGE(TAG, "Failed to parse OP_RESQ_IP.");
        return false;
    }

    connStatus_ = GOT_IP;
    return true;
}

bool NodeMgr::syncNodeFromServer()
{
    uint8_t rcvBuf[65536];

    if (connStatus_ != SYNCED) {
        SyncMessage reqMsgHdr{};
        reqMsgHdr.op = OP_REQ_SYNC_NODE;
        reqMsgHdr.mac = g_cfgData.mac;

        tcpSockPtr_->send(&reqMsgHdr, sizeof(SyncMessage));
    }

    ssize_t recvBytes = tcpSockPtr_->recv(rcvBuf, sizeof(rcvBuf));
    if (recvBytes == 0) {
        LOGW(TAG, "server has closed the connection.");
        connStatus_ = NOT_CONNECTED;
        return false;
    } else if (recvBytes < sizeof(SyncMessage)) {
        LOGE(TAG, "recvBytes[%ld] is unexpected.", recvBytes);
        return false;
    }

    macToNode_.clear();
    addrPool_.reset();
    if(!handleResponse(rcvBuf, recvBytes)) {
        LOGE(TAG, "Failed to parse OP_RESP_SYNC_NODE.");
        return false;
    }

    connStatus_ = SYNCED;
    return true;
}
