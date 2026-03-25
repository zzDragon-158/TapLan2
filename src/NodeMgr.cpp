#include "NodeMgr.hpp"
#include "LogMgr.hpp"
#include "TapLan.hpp"

const char* TAG = "[NodeMgr]";

NodeMgr::NodeMgr()
    : netNum_(g_cfgData.netNum)
    , netNumLen_(g_cfgData.netNumLen)
    , verNum_(0)
    , serverAddr_{}
    , connStatus_(NOT_CONNECTED)
    , tcpSockSPtr_(nullptr)
{
    serverAddr_.sin6_family = AF_INET6;
    serverAddr_.sin6_addr = g_cfgData.remoteAddr;
    serverAddr_.sin6_port = g_cfgData.remotePort;

    addrPool_.set(0);
    addrPool_.set(addrPool_.size() - 1);
}

NodeMgr::~NodeMgr()
{
    ;
}

void NodeMgr::reset()
{
    WLock lock;
    macToNode_.clear();
    activeDeltaBuffer_.clear();
    processingBuffer_.clear();
    addrPool_.reset();
}

NodeSPtr NodeMgr::addNode(const sockaddr_in6* addr, uint64_t macNum)
{
    NodeSPtr n = findNode(macNum);
    if (n) {
        n->ipv6Addr = addr->sin6_addr;
        n->ipv6Port = addr->sin6_port;

        if (!g_cfgData.noSync) {
            WLock wLock(rwMutex_);
            activeDeltaBuffer_[macNum] = n;
        }

        return n;
    }

    uint32_t hostNum = 0;
    if (!g_cfgData.noSync) {
        for (size_t i = 1; i < addrPool_.size() - 1; ++i) {
            if (!addrPool_.test(i)) {
                addrPool_.set(i);
                hostNum = i;
                break;
            }
        }
        if (hostNum == 0) {
            LOGE(TAG, "No enough addr for allocating.");
            return nullptr;
        }
    }

    n = std::make_shared<Node>();
    n->ipv6Addr = addr->sin6_addr;
    n->ipv6Port = addr->sin6_port;
    n->ipv4Addr.s_addr = g_cfgData.noSync? UINT32_MAX: htonl(netNum_ + hostNum);
    n->mac = macNum;
    n->status = NODE_ONLINE;
    n->lastSeen = time(nullptr);

    {
        WLock wLock(rwMutex_);
        macToNode_[macNum] = n;
        activeDeltaBuffer_[macNum] = n;
    }

    return n;
}

NodeSPtr NodeMgr::addNode(uint64_t macNum, Node& node)
{
    std::shared_ptr n = std::make_shared<Node>();
    *n.get() = node;

    uint32_t hostNumMask = static_cast<uint32_t>(1 << (32 - netNumLen_)) - 1;
    uint32_t hostNum = ntohl(n->ipv4Addr.s_addr) & hostNumMask;
    addrPool_.set(hostNum);

    {
        WLock wLock(rwMutex_);
        macToNode_[macNum] = n;
    }

    return n;
}

NodeSPtr NodeMgr::delNode(uint64_t macNum)
{
    NodeSPtr n = findNode(macNum);
    if (!n)
        return nullptr;

    uint32_t hostNumMask = static_cast<uint32_t>(1 << (32 - netNumLen_)) - 1;
    uint32_t hostNum = ntohl(n->ipv4Addr.s_addr) & hostNumMask;
    addrPool_.reset(hostNum);

    {
        WLock wLock(rwMutex_);
        macToNode_.erase(macNum);
        // FIXME: not support notify client.
        activeDeltaBuffer_[macNum] = n;
    }

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

    n->status = status;
    {
        WLock wLock(rwMutex_);
        activeDeltaBuffer_[macNum] = n;
    }

    return true;
}

void NodeMgr::setSockaddr(sockaddr_in6& addr, NodeSPtr n)
{
    addr = {};
    addr.sin6_family = AF_INET6;
    addr.sin6_addr = n->ipv6Addr;
    addr.sin6_port = n->ipv6Port;
}

void NodeMgr::server()
{
    tcpSockSPtr_ = std::make_shared<TcpSock>(g_cfgData.localPort);
    if (!tcpSockSPtr_->isFdValid() || !tcpSockSPtr_->listen(5)) {
        g_cfgData.isRunning = false;
        return ;
    }

    pfds_.push_back({ tcpSockSPtr_->getFd(), POLLIN, 0 });
    while (g_cfgData.isRunning) {
        int pollCnt = TapLanPoll(pfds_.data(), pfds_.size(), IO_WAIT_TIME * 1000);
        if (pollCnt < 0) {
            LOGE(TAG, "Failed to poll.");
            break;
        } else if (pollCnt == 0) {
            continue;
        }

        size_t pfdsLen = pfds_.size();
        if (pfds_.begin()->revents) {
            --pollCnt;
            TcpSockSPtr client = tcpSockSPtr_->accept();
            if (client) {
                clients_.push_back(client);
                pfds_.push_back({ client->getFd(), POLLIN, 0 });
            }
        }
        for (int i = pfdsLen - 1; pollCnt && i > 0; --i) {
            if (!pfds_[i].revents) {
                continue;
            }

            --pollCnt;
            uint8_t recvBuf[65536];
            TcpSockSPtr client = clients_[i - 1];
            ssize_t recvBytes = client->recv(recvBuf, sizeof(recvBuf));
            if (recvBytes == -2) {
                LOGW(TAG, "poll but recv timeout.");
                continue;
            } else if (recvBytes == -1 || recvBytes == 0) {
                uint64_t macNum = client->getMac();
                setNodeStatus(macNum, NODE_OFFLINE);

                std::swap(pfds_[i], pfds_.back());
                pfds_.pop_back();
                std::swap(clients_[i - 1], clients_.back());
                clients_.pop_back();
                continue;
            }

            handleSyncMsg(recvBuf, recvBytes, client);
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
    if (!tcpSockSPtr_ || !tcpSockSPtr_->isFdValid()) {
        tcpSockSPtr_ = std::make_shared<TcpSock>(0, serverAddr_);
        if (!tcpSockSPtr_->isFdValid()) {
            return false;
        }
    }

    if (!tcpSockSPtr_->connect()) {
        return false;
    }

    connStatus_ = CONNECTED;
    return true;
}

bool NodeMgr::reqIPFromServer()
{
    uint8_t rcvBuf[65536];

    SyncMsgHdr reqMsgHdr{};
    TapDevPtr->getMacAddr(reqMsgHdr.mac);
    reqMsgHdr.op = OP_REQ_IP;
    reqMsgHdr.port = htons(g_cfgData.localPort);
    reqMsgHdr.msgLen = sizeof(reqMsgHdr);
    tcpSockSPtr_->send(&reqMsgHdr, sizeof(reqMsgHdr));

    ssize_t recvBytes = tcpSockSPtr_->recv(rcvBuf, sizeof(rcvBuf));
    if (recvBytes == -2) {
        LOGE(TAG, "Wait IPMsg timeout.");
        return false;
    } else if (recvBytes == -1 || recvBytes == 0) {
        LOGE(TAG, "Failed to recv IPMsg.");
        connStatus_ = NOT_CONNECTED;
        tcpSockSPtr_->close();
        return false;
    }

    if (!handleSyncMsg(rcvBuf, recvBytes, tcpSockSPtr_)) {
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
        reqMsgHdr.mac = g_cfgData.mac;
        reqMsgHdr.op = OP_REQ_SYNC_NODE;
        reqMsgHdr.msgLen = sizeof(reqMsgHdr);
        tcpSockSPtr_->send(&reqMsgHdr, sizeof(reqMsgHdr));
    }

    ssize_t recvBytes = tcpSockSPtr_->recv(rcvBuf, sizeof(rcvBuf));
    if (recvBytes == -2) {
        // LOGT(TAG, "Wait sync timeout.");
        return false;
    } else if (recvBytes == -1 || recvBytes == 0) {
        LOGE(TAG, "Failed to sync node.");
        connStatus_ = NOT_CONNECTED;
        tcpSockSPtr_->close();
        return false;
    }

    if(!handleSyncMsg(rcvBuf, recvBytes, tcpSockSPtr_)) {
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
        syncMsg.nodes[syncMsg.numsOfNode++] = *v.get();
        sendBytes += sizeof(Node);
    }
    processingBuffer_.clear();

    for (auto& client: clients_) {
        if (client->getMac() != 0) {
            syncMsgHdr.msgLen = sendBytes;
            client->send(sndBuf, sendBytes);
        }
    }

    return true;
}

bool NodeMgr::handleSyncMsg(uint8_t* msg, size_t msgLen, TcpSockSPtr srcSock)
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

bool NodeMgr::handleIPReq(uint8_t* reqMsg, TcpSockSPtr client)
{
    uint8_t sndBuf[65536];
    size_t sendBytes = 0;

    SyncMsgHdr& respMsgHdr = reinterpret_cast<SyncMsgHdr&>(*sndBuf);
    std::memcpy(&respMsgHdr, reqMsg, sizeof(respMsgHdr));
    respMsgHdr.op = OP_RESP_IP;
    sendBytes += sizeof(SyncMsgHdr);

    sockaddr_in6 addr = client->getRemoteAddr();
    addr.sin6_port = respMsgHdr.port;
    NodeSPtr n = addNode(&addr, static_cast<uint64_t>(respMsgHdr.mac));
    if (!n) {
        LOGE(TAG, "Failed to add node.");
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
    if (client->send(sndBuf, sendBytes) <= 0) {
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

bool NodeMgr::handleSyncNodeReq(uint8_t* reqMsg, TcpSockSPtr client)
{
    uint8_t sndBuf[65536];
    size_t sendBytes = 0;

    SyncMsgHdr& respMsgHdr = reinterpret_cast<SyncMsgHdr&>(*sndBuf);
    std::memcpy(&respMsgHdr, reqMsg, sizeof(respMsgHdr));
    sendBytes += sizeof(SyncMsgHdr);

    respMsgHdr.op = OP_RESP_SYNC_NODE;
    client->setMac(respMsgHdr.mac);

    SyncNodeMsg& respMsg = reinterpret_cast<SyncNodeMsg&>(*(sndBuf + sendBytes));
    respMsg.verNum = verNum_;
    respMsg.numsOfNode = 0;
    sendBytes += sizeof(SyncNodeMsg);

    forEach([&](uint64_t k, NodeSPtr v) {
        respMsg.nodes[respMsg.numsOfNode++] = *v.get();
        sendBytes += sizeof(Node);
    });

    respMsgHdr.msgLen = sendBytes;
    if (client->send(sndBuf, sendBytes) <= 0) {
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
            n.ipv6Addr = g_cfgData.remoteAddr;
            n.ipv6Port = g_cfgData.remotePort;
        }
        addNode(n.mac, n);
    }

    return true;
}
