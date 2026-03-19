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

void NodeMgr::reset()
{
    WLock lock;
    macToNode_.clear();
    addrPool_.reset();
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
    tcpSockPtr_ = new TcpSock(g_cfgData.localPort);
    if (!tcpSockPtr_->isFdValid() || !tcpSockPtr_->listen(5)) {
        LOGF(TAG, "Trying to run in server mode failed.");
        g_cfgData.isRunning = false;
        return ;
    }

    pfds_.push_back({ static_cast<SockFd>(*tcpSockPtr_), POLLIN, 0 });
    while (g_cfgData.isRunning) {
        int pollCnt = TapLanPoll(pfds_.data(), pfds_.size(), IO_WAIT_TIME * 1000);
        if (pollCnt < 0) {
            LOGE(TAG, "TapLanPoll failed.");
            continue;
        }

        size_t pfdsLen = pfds_.size();
        if (pfds_.begin()->revents != 0) {
            --pollCnt;
            SockFd tcpFd = INVALID_SOCKET;
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
                TcpSock& client = clients_[i - 1];
                ssize_t recvBytes = client.recv(recvBuf, sizeof(recvBuf));
                if (recvBytes == 0) {   // 对方关闭连接
                    auto it = sockToMac_.find(static_cast<SockFd>(client));
                    if (it != sockToMac_.end()) {
                        if (!setNodeStatus(it->second, NODE_OFFLINE))
                            LOGW(TAG, "set node status failed.");
                        sockToMac_.erase(it->first);
                    }
                    pfds_.erase(pfds_.begin() + i);
                    clients_.erase(clients_.begin() + i - 1);
                    continue;
                } else if (recvBytes < 0) {  // 接收消息格式不对
                    continue;
                }

                handleSyncMsg(recvBuf, recvBytes, client);
            }
        }

        syncNodeToClients();
    }
}

void NodeMgr::client()
{
    while (g_cfgData.isRunning) {
        bool ok = false;

        switch (connStatus_) {
        case NOT_CONNECTED:
            ok = connectToServer();
            break;

        case CONNECTED:
            ok = reqIPFromServer();
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

bool NodeMgr::connectToServer()
{
    if (!tcpSockPtr_ || !tcpSockPtr_->isFdValid()) {
        delete tcpSockPtr_;
        tcpSockPtr_ = new TcpSock(g_cfgData.localPort, serverAddr_);
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

bool NodeMgr::reqIPFromServer()
{
    uint8_t rcvBuf[65536];

    SyncMsgHdr reqMsgHdr{};
    reqMsgHdr.op = OP_REQ_IP;
    TapDevPtr->getMacAddr(reqMsgHdr.mac);

    reqMsgHdr.msgLen = sizeof(reqMsgHdr);
    tcpSockPtr_->send(&reqMsgHdr, sizeof(reqMsgHdr));
    ssize_t recvBytes = tcpSockPtr_->recv(rcvBuf, sizeof(rcvBuf));
    if (recvBytes == 0) {
        LOGW(TAG, "server has closed the connection.");
        connStatus_ = NOT_CONNECTED;
        return false;
    } else if (recvBytes < 0) {
        return false;
    }

    if (!handleSyncMsg(rcvBuf, recvBytes, *tcpSockPtr_)) {
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
        SyncMsgHdr reqMsgHdr{};
        reqMsgHdr.op = OP_REQ_SYNC_NODE;
        reqMsgHdr.mac = g_cfgData.mac;

        reqMsgHdr.msgLen = sizeof(reqMsgHdr);
        tcpSockPtr_->send(&reqMsgHdr, sizeof(reqMsgHdr));
    }

    ssize_t recvBytes = tcpSockPtr_->recv(rcvBuf, sizeof(rcvBuf));
    if (recvBytes == 0) {
        LOGW(TAG, "server has closed the connection.");
        connStatus_ = NOT_CONNECTED;
        return false;
    } else if (recvBytes < 0) {
        return false;
    }

    if(!handleSyncMsg(rcvBuf, recvBytes, *tcpSockPtr_)) {
        LOGE(TAG, "Failed to parse OP_RESP_SYNC_NODE.");
        return false;
    }

    connStatus_ = SYNCED;
    return true;
}

bool NodeMgr::syncNodeToClients()
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
    SyncMsgHdr& syncMsgHdr = reinterpret_cast<SyncMsgHdr&>(*sndBuf);
    sendBytes += sizeof(SyncMsgHdr);
    syncMsgHdr.mac = g_cfgData.mac;
    syncMsgHdr.op = OP_MOD;
    // syncMsgHdr.key = ;
    SyncNodeMsg& syncMsg = reinterpret_cast<SyncNodeMsg&>(*(sndBuf + sendBytes));
    sendBytes += sizeof(SyncNodeMsg);
    syncMsg.numsOfNode = 0;
    syncMsg.verNum = ++verNum_;
    for (auto& [k, v]: processingBuffer_) {
        std::memcpy(sndBuf + sendBytes, v.get(), sizeof(Node));
        sendBytes += sizeof(Node);
        ++syncMsg.numsOfNode;
    }
    processingBuffer_.clear();

    for (auto& client: clients_) {
        if (sockToMac_.find(static_cast<SockFd>(client)) != sockToMac_.end()) {
            syncMsgHdr.msgLen = sendBytes;
            client.send(sndBuf, sendBytes);
        }
    }

    return true;
}

bool NodeMgr::handleSyncMsg(uint8_t* msg, size_t msgLen, TcpSock& srcSock)
{
    bool ok;
    SyncMsgHdr& msgHdr = reinterpret_cast<SyncMsgHdr&>(*msg);

    switch (msgHdr.op) {
    case OP_REQ_IP:
        ok = handleIPReq(msg, srcSock);
        break;

    case OP_RESP_IP:
        ok = handleIPMsg(msg);
        break;

    case OP_REQ_SYNC_NODE:
        ok = handleSyncNodeReq(msg, srcSock);
        break;

    case OP_RESP_SYNC_NODE:
        reset();

    case OP_MOD:
        ok = handleSyncNodeMsg(msg);
        break;

    default:
        LOGE(TAG, "Unknown OP[%u].", msgHdr.op);
        break;
    }

    if (!ok) {
        LOGW(TAG, "Failed to handle OP[%u].", msgHdr.op);
    }

    return ok;
}

bool NodeMgr::handleIPReq(uint8_t* reqMsg, TcpSock& client)
{
    uint8_t sndBuf[65536];
    size_t sendBytes = 0;

    SyncMsgHdr& respMsgHdr = reinterpret_cast<SyncMsgHdr&>(*sndBuf);
    memcpy(&respMsgHdr, reqMsg, sizeof(respMsgHdr));
    respMsgHdr.op = OP_RESP_IP;
    sendBytes += sizeof(SyncMsgHdr);

    sockaddr_in6 addr;
    client.getRemoteAddr(&addr);
    NodeSPtr n = addNode(&addr, static_cast<uint64_t>(respMsgHdr.mac));
    if (!n) {
        LOGE(TAG, "add node failed.");
        return false;
    }

    if (n->status != NODE_ONLINE) {
        n->status = NODE_ONLINE;
        activeDeltaBuffer_[respMsgHdr.mac] = n;
    }

    IPMsg& ipMsg = reinterpret_cast<IPMsg&>(*(sndBuf + sendBytes));
    ipMsg.netIDLen = g_cfgData.netNumLen;
    ipMsg.ipv4Addr = n->ipv4Addr;
    sendBytes += sizeof(IPMsg);

    respMsgHdr.msgLen = sendBytes;
    if (client.send(sndBuf, sendBytes) <= 0) {
        LOGE(TAG, "Failed to send IPMsg.");
        return false;
    }

    return true;
}

bool NodeMgr::handleIPMsg(uint8_t* respMsg)
{
    IPMsg& ipMsg = reinterpret_cast<IPMsg&>(*(respMsg + sizeof(SyncMsgHdr)));

    netNumLen_ = ipMsg.netIDLen;

    uint32_t subnetMask = ~(static_cast<uint32_t>(1 << (32 - ipMsg.netIDLen)) - 1);
    netNum_ = ntohl(ipMsg.ipv4Addr.s_addr) & subnetMask;

    return TapDevPtr->setIPv4Addr(&ipMsg.ipv4Addr, ipMsg.netIDLen);
}

bool NodeMgr::handleSyncNodeReq(uint8_t* reqMsg, TcpSock& client)
{
    uint8_t sndBuf[65536];
    size_t sendBytes = 0;

    SyncMsgHdr& respMsgHdr = reinterpret_cast<SyncMsgHdr&>(*sndBuf);
    memcpy(&respMsgHdr, reqMsg, sizeof(respMsgHdr));
    sendBytes += sizeof(SyncMsgHdr);

    respMsgHdr.op = OP_RESP_SYNC_NODE;
    sockToMac_[static_cast<SockFd>(client)] = respMsgHdr.mac;

    SyncNodeMsg& respMsg = reinterpret_cast<SyncNodeMsg&>(*(sndBuf + sendBytes));
    respMsg.verNum = verNum_;
    respMsg.numsOfNode = 0;
    sendBytes += sizeof(SyncNodeMsg);

    forEach([&](uint64_t k, NodeSPtr v) {
        std::memcpy(sndBuf + sendBytes, v.get(), sizeof(Node));
        sendBytes += sizeof(Node);
        ++respMsg.numsOfNode;
    });

    respMsgHdr.msgLen = sendBytes;
    if (client.send(sndBuf, sendBytes) <= 0) {
        LOGE(TAG, "Failed to send SyncNodeMsg.");
        return false;
    }

    return true;
}

bool NodeMgr::handleSyncNodeMsg(uint8_t* respMsg)
{
    SyncMsgHdr& msgHdr = reinterpret_cast<SyncMsgHdr&>(*respMsg);
    SyncNodeMsg& msgBody = reinterpret_cast<SyncNodeMsg&>(*msgHdr.msgBody);

    size_t numsOfNode = msgBody.numsOfNode;
    size_t expectedSize = numsOfNode * sizeof(Node);
    size_t actualSize = msgHdr.msgLen - sizeof(SyncMsgHdr) - sizeof(SyncNodeMsg);
    if (expectedSize != actualSize) {
        LOGE(TAG, "NodeSize is incorrect, e[%u]:a[%u].", expectedSize, actualSize);
        return false;
    }

    if (verNum_ != 0 && (verNum_ + 1 != msgBody.verNum)) {
        LOGW(TAG, "vernum expect [%u] but [%u]", verNum_ + 1, msgBody.verNum);
    }
    verNum_ = msgBody.verNum;

    for (int i = 0; i < numsOfNode; ++i) {
        Node& n = msgBody.nodes[i];
        if (IN6_IS_ADDR_UNSPECIFIED(&n.ipv6Addr)) {
            std::memcpy(&n.ipv6Addr, &g_cfgData.remoteAddr, sizeof(in6_addr));
        }
        addNode(n.mac, n);
    }

    return true;
}
