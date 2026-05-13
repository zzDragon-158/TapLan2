#include    "NodeMgr.hpp"
#include "BsdSock.hpp"
#include    "Common.hpp"
#include    "Config.hpp"
#include    "DataSec.hpp"
#include    "TapDev.hpp"
#include    "LogMgr.hpp"
#include    <cstddef>
#include    <cstdint>
#include    <cstring>
#include    <memory>

NodeMgr::NodeMgr()
    : netNum_(g_cfgData.netNum())
    , netNumLen_(g_cfgData.netNumLen())
    , verNum_(0)
    , nodeStatus_(NodeStatus::outOfSync)
    , tcpSockSPtr_(nullptr)
{
    sndBuf_ = new uint8_t[UINT16_MAX + 1];
    rcvBuf_ = new uint8_t[UINT16_MAX + 1];
    if (g_cfgData.runMode() == RunMode::server) {
        LOGI("We are running in server mode.");
        if (g_cfgData.running()) {
            const Mac& mac = g_tapDev.getMacAddr();
            NodeInfoSPtr nodeInfo = assignIpHostNumForNode(g_cfgData.serverAddr(), mac);
            nodeInfo->status = NodeStatus::synced;
            g_tapDev.setIPv4Addr(nodeInfo->ipv4Addr, g_cfgData.netNumLen());
            addNode(mac, *nodeInfo);
        }
    } else {
        LOGI("We are running in client mode.");
    }
}

NodeMgr::~NodeMgr()
{
    delete[] sndBuf_;
    delete[] rcvBuf_;
}

void NodeMgr::reset()
{
    WLock lock;
    verNum_ = 0;
    macToNodeSess_.clear();
    activeDeltaBuffer_.clear();
    processingBuffer_.clear();
    addrPool_.reset();
}

uint32_t NodeMgr::getHostNum(const in_addr& ipv4Addr)
{
    uint32_t hostNumMask = netNumLen_ == 32? 0: ((1u << (32 - netNumLen_)) - 1);
    uint32_t hostNum = ntohl(ipv4Addr.s_addr) & hostNumMask;

    return hostNum;
}

NodeInfoSPtr NodeMgr::constructNodeInfo(const sockaddr_in6& addr, const Mac& mac, uint32_t hostNum)
{
    NodeInfoSPtr nodeInfo = std::make_shared<NodeInfo>();
    nodeInfo->ipv6Addr = addr.sin6_addr;
    nodeInfo->ipv6Port = addr.sin6_port;
    nodeInfo->ipv4Addr.s_addr = g_cfgData.noSync()? UINT32_MAX: htonl(netNum_ + hostNum);
    nodeInfo->mac = mac;
    nodeInfo->status = NodeStatus::outOfSync;
    nodeInfo->lastSeen = time(nullptr);

    return nodeInfo;
}

NodeInfoSPtr NodeMgr::assignIpHostNumForNode(const sockaddr_in6& addr, const Mac& mac)
{
    uint32_t hostNum = 0;
    if (!g_cfgData.noSync()) {
        for (size_t i = 1; i < addrPool_.size() - 1; ++i) {
            if (!addrPool_.test(i)) {
                addrPool_.set(i);
                hostNum = i;
                break;
            }
        }
        if (hostNum == 0) {
            LOGE("Unable to assign IP host number.");
            return nullptr;
        }
    }

    NodeInfoSPtr nodeInfo = constructNodeInfo(addr, mac, hostNum);

    return nodeInfo;
}

NodeSessSPtr NodeMgr::addNode(const sockaddr_in6& addr, const SyncMsgHdr& syncMsgHdr)
{
    const Mac& mac = syncMsgHdr.mac;

    NodeInfoSPtr nodeInfo;
    NodeSessSPtr node = findNode(mac);
    if (node) {
        uint32_t hostNum = getHostNum(node->nodeInfo->ipv4Addr);
        nodeInfo = constructNodeInfo(addr, mac, hostNum);
    } else {
        nodeInfo = assignIpHostNumForNode(addr, mac);
    }
    if (!nodeInfo) {
        return nullptr;
    }

    node = std::make_shared<NodeSession>();
    node->nodeInfo = nodeInfo;
    node->aeadSess = std::make_shared<AeadSession>(g_tapDev.getMacAddr(), syncMsgHdr.nonce);

    WLock wLock(rwMutex_);
    macToNodeSess_.insert_or_assign(mac, node);
    activeDeltaBuffer_[mac] = nodeInfo;

    return node;
}

NodeSessSPtr NodeMgr::addNode(const Mac& mac, const NodeInfo& nodeInfo)
{
    uint32_t hostNum = getHostNum(nodeInfo.ipv4Addr);
    addrPool_.set(hostNum);

    WLock wLock(rwMutex_);
    auto [it, inserted] = macToNodeSess_.insert_or_assign(mac, std::make_shared<NodeSession>());
    it->second->nodeInfo = std::make_shared<NodeInfo>(nodeInfo);

    return it->second;
}

NodeSessSPtr NodeMgr::delNode(const Mac& mac)
{
    NodeSessSPtr node = findNode(mac);
    if (!node)
        return nullptr;

    NodeInfoSPtr nodeInfo = node->nodeInfo;
    uint32_t hostNum = getHostNum(nodeInfo->ipv4Addr);
    addrPool_.reset(hostNum);

    {
        WLock wLock(rwMutex_);
        macToNodeSess_.erase(mac);
        // FIXME: not support notify client.
        activeDeltaBuffer_[mac] = nodeInfo;
    }

    return node;
}

NodeSessSPtr NodeMgr::findNode(const Mac& mac)
{
    RLock rLock(rwMutex_);
    auto it = macToNodeSess_.find(mac);
    if (it == macToNodeSess_.end()) {
        return nullptr;
    }

    return it->second;
}

bool NodeMgr::setNodeStatus(const Mac& mac, NodeStatus status)
{
    NodeSessSPtr node = findNode(mac);
    if (!node)
        return false;

    NodeInfoSPtr nodeInfo = node->nodeInfo;

    nodeInfo->status = status;
    {
        WLock wLock(rwMutex_);
        activeDeltaBuffer_[mac] = nodeInfo;
    }

    return true;
}

void NodeMgr::setSockaddr(sockaddr_in6& addr, NodeInfoSPtr n)
{
    addr = {};
    addr.sin6_family = AF_INET6;
    addr.sin6_addr = n->ipv6Addr;
    addr.sin6_port = n->ipv6Port;
}

void NodeMgr::server()
{
    tcpSockSPtr_ = std::make_shared<TcpSock>(g_cfgData.localPort());
    if (!tcpSockSPtr_->isFdValid() || !tcpSockSPtr_->listen(5)) {
        g_cfgData.running() = false;
        return ;
    }

    pfds_.push_back({ tcpSockSPtr_->getFd(), POLLIN, 0 });
    while (g_cfgData.running()) {
        pollAndProcess();
    }
}

void NodeMgr::client()
{
    while (g_cfgData.running()) {
        bool ok = false;

        switch (nodeStatus_) {
        case NodeStatus::outOfSync:
            ok = connectToServer();
            break;

        case NodeStatus::connected:
            ok = reqIPFromServer();
            break;

        case NodeStatus::ipGot:
        case NodeStatus::synced:
            ok = syncNodeFromServer();
            break;

        default:
            LOGE("Unknown status[{}].", nodeStatus_);
        }

        if (!ok)
            std::this_thread::sleep_for(std::chrono::seconds(IO_WAIT_TIME));
    }
}

bool NodeMgr::connectToServer()
{
    if (!tcpSockSPtr_ || !tcpSockSPtr_->isFdValid()) {
        tcpSockSPtr_ = std::make_shared<TcpSock>(0, g_cfgData.serverAddr());
        if (!tcpSockSPtr_->isFdValid()) {
            return false;
        }
    }

    if (!tcpSockSPtr_->connect()) {
        return false;
    }

    nodeStatus_ = NodeStatus::connected;
    reset();
    LOGD("Succeed in connecting to server.");
    return true;
}

bool NodeMgr::reqIPFromServer()
{
    aeadSession_ = std::make_shared<AeadSession>(g_tapDev.getMacAddr());

    SyncMsgHdr& reqMsgHdr = reinterpret_cast<SyncMsgHdr&>(*sndBuf_);
    reqMsgHdr.mac = g_tapDev.getMacAddr();
    reqMsgHdr.op = OP::getIP;
    reqMsgHdr.nonce = aeadSession_->getSendNonce();
    reqMsgHdr.port = htons(g_cfgData.localPort());
    reqMsgHdr.msgLen = sizeof(reqMsgHdr);
    sendMsg(*tcpSockSPtr_, sndBuf_, sizeof(reqMsgHdr));
    LOGD("send msg to server for requesting IP.");

    ssize_t recvBytes = recvMsg(*tcpSockSPtr_, rcvBuf_, UINT16_MAX);
    if (recvBytes == -2) {
        LOGE("Wait IPMsg timeout.");
        return false;
    } else if (recvBytes == -1 || recvBytes == 0) {
        LOGF("Failed to recv IPMsg. Please check whether the password is correct.");
        g_cfgData.running() = false;
        nodeStatus_ = NodeStatus::outOfSync;
        tcpSockSPtr_->close();
        return false;
    }

    if (!handleSyncMsg(rcvBuf_, recvBytes, tcpSockSPtr_)) {
        LOGE("Failed to parse OP_RESQ_IP.");
        return false;
    }

    nodeStatus_ = NodeStatus::ipGot;
    return true;
}

bool NodeMgr::syncNodeFromServer()
{
    if (nodeStatus_ != NodeStatus::synced) {
        SyncMsgHdr& reqMsgHdr = reinterpret_cast<SyncMsgHdr&>(*sndBuf_);
        reqMsgHdr.mac = g_tapDev.getMacAddr();
        reqMsgHdr.op = OP::reqSync;
        reqMsgHdr.msgLen = sizeof(reqMsgHdr);
        sendMsg(*tcpSockSPtr_, sndBuf_, sizeof(reqMsgHdr));
        LOGD("send msg to server for syncing node.");
    }

    ssize_t recvBytes = recvMsg(*tcpSockSPtr_, rcvBuf_, UINT16_MAX);
    if (recvBytes == -2) {
        // LOGT("Wait sync timeout.");
        return false;
    } else if (recvBytes == -1 || recvBytes == 0) {
        LOGE("Failed to sync node.");
        nodeStatus_ = NodeStatus::outOfSync;
        tcpSockSPtr_->close();
        return false;
    }

    if(!handleSyncMsg(rcvBuf_, recvBytes, tcpSockSPtr_)) {
        LOGE("Failed to parse OP::respSync.");
        return false;
    }

    nodeStatus_ = NodeStatus::synced;
    return true;
}

ssize_t NodeMgr::sendMsg(TcpSock& tcpSock, void* msg, size_t msgLen)
{
    if (g_cfgData.enableSec()) {
        AeadPacket* packet = reinterpret_cast<AeadPacket*>(msg);
        std::memmove(
            packet->payload,
            packet,
            msgLen
        );
        size_t payloadLen = msgLen;

        if (g_cfgData.runMode() == RunMode::server) {
            const Mac& mac = tcpSock.getMac();
            NodeSessSPtr node = findNode(mac);
            if (!node || !node->aeadSess) {
                LOGE("Failed to sendmsg to {}.", mac);
                return -1;
            }
            node->aeadSess->encrypt(packet, payloadLen);
        } else {
            aeadSession_->encrypt(packet, payloadLen);
        }
        msgLen = NONCE_SIZE + payloadLen;
    }

    size_t sendBytes = tcpSock.send(msg, msgLen);

    return sendBytes;
}

ssize_t NodeMgr::recvMsg(TcpSock& tcpSock, void* buf, size_t bufLen)
{
    ssize_t recvBytes = tcpSock.recv(buf, bufLen);
    if (recvBytes <= 0)
        return recvBytes;

    if (g_cfgData.enableSec()) {
        AeadPacket* packet = reinterpret_cast<AeadPacket*>(buf);
        size_t payloadLen = recvBytes - NONCE_SIZE;

        if (g_cfgData.runMode() == RunMode::server) {
            const Mac& mac = AeadSession::fetchMacFromNonce(packet->nonce);
            bool ok = false;

            NodeSessSPtr node = findNode(mac);
            if (!node) {
                ok = AeadSession::decryptWithoutCheck(packet, payloadLen);
            } else {
                bool isSameSess = node->aeadSess->isSameSession(packet->nonce);
                bool isOnline = node->nodeInfo->status != NodeStatus::outOfSync;
                if (isSameSess && isOnline) {
                    ok = node->aeadSess->decrypt(packet, payloadLen);
                } else if (!isSameSess && !isOnline) {
                    LOGI("client[{}] reconnect.", mac);
                    ok = AeadSession::decryptWithoutCheck(packet, payloadLen);
                } else {
                    LOGW("mac[{}], isSameSess[{}], isOnline[{}].", mac, isSameSess, isOnline);
                    return -2;
                }
            }
            if (!ok) {
                // Maybe the password is incorrect
                LOGE("Failed to decrypt sync msg from client[{}].", tcpSock.getRemoteAddr().sin6_addr);
                return -1;
            }
        } else {
            if (!aeadSession_->decrypt(packet, payloadLen)) {
                LOGE("Failed to decrypt sync msg from server.");
                return -1;
            }
        }

        std::memmove(
            packet,
            packet->payload,
            payloadLen
        );
        recvBytes = payloadLen;
    }

    return recvBytes;
}

void NodeMgr::pollAndProcess()
{
    int pollCnt = UnivPoll(pfds_.data(), pfds_.size(), IO_WAIT_TIME * 1000);
    if (pollCnt < 0) {
        LOGE("Failed to poll.");
        return ;
    } else if (pollCnt == 0) {
        return ;
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
        TcpSockSPtr client = clients_[i - 1];
        ssize_t recvBytes = recvMsg(*client, rcvBuf_, UINT16_MAX);
        if (recvBytes == -2) {
            LOGW("poll but recv timeout.");
            continue;
        } else if (recvBytes == -1 || recvBytes == 0) {
            setNodeStatus(client->getMac(), NodeStatus::outOfSync);
            LOGI("client{} is offline.", client->getMac());

            std::swap(pfds_[i], pfds_.back());
            pfds_.pop_back();
            std::swap(clients_[i - 1], clients_.back());
            clients_.pop_back();
            continue;
        }

        handleSyncMsg(rcvBuf_, recvBytes, client);
    }

    syncNodeToClients();
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
    size_t sendBytes = 0;
    SyncMsgHdr& syncMsgHdr = reinterpret_cast<SyncMsgHdr&>(*sndBuf_);
    sendBytes += sizeof(SyncMsgHdr);
    syncMsgHdr.mac = g_tapDev.getMacAddr();
    syncMsgHdr.op = OP::modNode;
    // syncMsgHdr.key = ;
    SyncNodeMsg& syncMsg = reinterpret_cast<SyncNodeMsg&>(*(sndBuf_ + sendBytes));
    sendBytes += sizeof(SyncNodeMsg);
    syncMsg.numsOfNode = 0;
    syncMsg.verNum = ++verNum_;
    for (auto& [k, v]: processingBuffer_) {
        syncMsg.nodes[syncMsg.numsOfNode++] = *v.get();
        sendBytes += sizeof(NodeInfo);
    }
    processingBuffer_.clear();

    for (auto& client: clients_) {
        NodeSessSPtr node = findNode(client->getMac());
        if (!node || !node->nodeInfo) {
            continue;
        }

        if (node->nodeInfo->status == NodeStatus::synced) {
            syncMsgHdr.msgLen = sendBytes;
            sendMsg(*client, sndBuf_, sendBytes);
        }
    }

    return true;
}

bool NodeMgr::handleSyncMsg(uint8_t* msg, size_t msgLen, TcpSockSPtr srcSock)
{
    bool ok;
    SyncMsgHdr& msgHdr = reinterpret_cast<SyncMsgHdr&>(*msg);
    LOGD("recv {} from {}", msgHdr.op, srcSock->getRemoteAddr().sin6_addr);

    switch (msgHdr.op) {
    case OP::getIP:
        ok = handleIPReq(msg, srcSock);
        break;

    case OP::assignIP:
        ok = handleIPMsg(msg);
        break;

    case OP::reqSync:
        ok = handleSyncNodeReq(msg, srcSock);
        break;

    case OP::respSync:
    case OP::modNode:
        ok = handleSyncNodeMsg(msg);
        break;

    default:
        LOGE("Unknown OP[{}].", msgHdr.op);
        break;
    }

    if (!ok) {
        LOGW("Failed to handle OP[{}].", msgHdr.op);
    }

    return ok;
}

bool NodeMgr::handleIPReq(uint8_t* reqMsg, TcpSockSPtr client)
{
    if (g_cfgData.runMode() == RunMode::client) {
        LOGW("Why are we here.handle IPReq?");
        return false;
    }
    size_t sendBytes = 0;

    SyncMsgHdr& reqMsgHdr = reinterpret_cast<SyncMsgHdr&>(*reqMsg);
    SyncMsgHdr& respMsgHdr = reinterpret_cast<SyncMsgHdr&>(*sndBuf_);
    respMsgHdr = reqMsgHdr;
    respMsgHdr.op = OP::assignIP;
    sendBytes += sizeof(SyncMsgHdr);

    client->setMac(respMsgHdr.mac);
    sockaddr_in6 addr = client->getRemoteAddr();
    addr.sin6_port = respMsgHdr.port;
    NodeSessSPtr node = addNode(addr, reqMsgHdr);
    if (!node) {
        LOGE("Failed to add node.");
        return false;
    }

    NodeInfoSPtr nodeInfo = node->nodeInfo;
    respMsgHdr.nonce = node->aeadSess->getSendNonce();
    IPMsg& ipMsg = reinterpret_cast<IPMsg&>(*(sndBuf_ + sendBytes));
    ipMsg.netIDLen = g_cfgData.netNumLen();
    ipMsg.ipv4Addr = nodeInfo->ipv4Addr;
    sendBytes += sizeof(IPMsg);

    respMsgHdr.msgLen = sendBytes;
    if (sendMsg(*client, sndBuf_, sendBytes) <= 0) {
        LOGE("Failed to send IPMsg.");
        return false;
    } else {
        setNodeStatus(reqMsgHdr.mac, NodeStatus::ipGot);
    }

    return true;
}

bool NodeMgr::handleIPMsg(uint8_t* respMsg)
{
    SyncMsgHdr& syncMsgHdr = reinterpret_cast<SyncMsgHdr&>(*respMsg);
    aeadSession_->setRecvNonce(syncMsgHdr.nonce);

    IPMsg& ipMsg = reinterpret_cast<IPMsg&>(*(respMsg + sizeof(SyncMsgHdr)));

    netNumLen_ = ipMsg.netIDLen;

    uint32_t subnetMask = ~(static_cast<uint32_t>(1 << (32 - ipMsg.netIDLen)) - 1);
    netNum_ = ntohl(ipMsg.ipv4Addr.s_addr) & subnetMask;

    return g_tapDev.setIPv4Addr(ipMsg.ipv4Addr, ipMsg.netIDLen);
}

bool NodeMgr::handleSyncNodeReq(uint8_t* reqMsg, TcpSockSPtr client)
{
    size_t sendBytes = 0;

    SyncMsgHdr& reqMsgHdr = reinterpret_cast<SyncMsgHdr&>(*reqMsg);
    SyncMsgHdr& respMsgHdr = reinterpret_cast<SyncMsgHdr&>(*sndBuf_);
    respMsgHdr = reqMsgHdr;
    sendBytes += sizeof(SyncMsgHdr);

    respMsgHdr.op = OP::respSync;

    SyncNodeMsg& respMsg = reinterpret_cast<SyncNodeMsg&>(*(sndBuf_ + sendBytes));
    respMsg.verNum = verNum_;
    respMsg.numsOfNode = 0;
    sendBytes += sizeof(SyncNodeMsg);

    forEach([&](uint64_t k, NodeSessSPtr v) {
        respMsg.nodes[respMsg.numsOfNode++] = *v->nodeInfo.get();
        sendBytes += sizeof(NodeInfo);
    });

    respMsgHdr.msgLen = sendBytes;
    if (sendMsg(*client, sndBuf_, sendBytes) <= 0) {
        LOGE("Failed to send SyncNodeMsg.");
        return false;
    } else {
        setNodeStatus(reqMsgHdr.mac, NodeStatus::synced);
    }

    return true;
}

bool NodeMgr::handleSyncNodeMsg(uint8_t* respMsg)
{
    SyncMsgHdr& msgHdr = reinterpret_cast<SyncMsgHdr&>(*respMsg);
    SyncNodeMsg& msgBody = reinterpret_cast<SyncNodeMsg&>(*msgHdr.msgBody);

    size_t numsOfNode = msgBody.numsOfNode;
    size_t expectedSize = numsOfNode * sizeof(NodeInfo);
    size_t actualSize = msgHdr.msgLen - sizeof(SyncMsgHdr) - sizeof(SyncNodeMsg);
    if (expectedSize != actualSize) {
        LOGE("NodeSize is incorrect, e[{}]:a[{}].", expectedSize, actualSize);
        return false;
    }

    if (verNum_ != 0 && (verNum_ + 1 != msgBody.verNum)) {
        LOGW("vernum expect [{}] but [{}]", verNum_ + 1, msgBody.verNum);
    }
    verNum_ = msgBody.verNum;

    for (int i = 0; i < numsOfNode; ++i) {
        NodeInfo& nodeInfo = msgBody.nodes[i];
        if (IN6_IS_ADDR_LOOPBACK(&nodeInfo.ipv6Addr)) {
            nodeInfo.ipv6Addr = g_cfgData.serverAddr().sin6_addr;
            nodeInfo.ipv6Port = g_cfgData.serverAddr().sin6_port;
        }
        NodeSessSPtr node = addNode(nodeInfo.mac, nodeInfo);
        if (node) {
            node->aeadSess = aeadSession_;
        } else {
            LOGE("Unable to add node.");
        }
    }

    return true;
}
