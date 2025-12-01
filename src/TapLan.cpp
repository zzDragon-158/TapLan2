#include "TapLan.hpp"

static const char* TAG = "[TapLan]";
ConfigDataT TapLan::config_;

uint64_t getMacNum(const uint8_t* mac);

TapLan::TapLan(): runFlag_(false),
                  udpSockPtr_(nullptr), tcpSockPtr_(nullptr),
                  nodeMgrPtr_(nullptr),
                  recvThreadName_("recvWorker"), sendThreadName_("sendWorker"), syncThreadName_("syncWorker")
{
    memset(&serverAddr_, 0, sizeof(serverAddr_));
    mac_.num = 0;
    memset(&udpSockPtrArr_, 0, sizeof(udpSockPtrArr_));

    if (config_.runMode == RunMode_Server) {
        LOGI(TAG, "We are running in server mode.");

        nodeMgrPtr_ = std::make_shared<NodeMgr>(config_.netNum, config_.netNumLen);

        serverAddr_.sin6_family = AF_INET6;
        serverAddr_.sin6_port = htons(config_.localPort);

        initUdpSockPtr();

        runFlag_ = udpSockPtr_->isFdValid() && TapDevPtr->open();

        if (runFlag_) {
            TapDevPtr->getMacAddr(mac_.addr, sizeof(Mac));
            std::shared_ptr<Node> n = nodeMgrPtr_->addNode(&serverAddr_, mac_.num);
            TapDevPtr->setIpv4Addr(&n->ipv4Addr, config_.netNumLen);
        }
    } else if (config_.runMode == RunMode_Client) {
        LOGI(TAG, "We are running in client mode.");

        serverAddr_.sin6_family = AF_INET6;
        memcpy(&serverAddr_.sin6_addr, &config_.remoteAddr, sizeof(in6_addr));
        serverAddr_.sin6_port = htons(config_.remotePort);

        initUdpSockPtr();

        runFlag_ = udpSockPtr_->isFdValid() && TapDevPtr->open();

        if (runFlag_) {
            TapDevPtr->getMacAddr(mac_.addr, sizeof(Mac));
        }
    } else {
        // RunMode_None
    }
}

TapLan::~TapLan()
{
    stop();
}

void TapLan::initUdpSockPtr()
{
    if (config_.isMultiPortEnable) {
        uint16_t startPort = config_.localPort - config_.localPort % 4;
        for (int i = 0; i < 4; ++i) {
            uint16_t port = startPort + i;
            udpSockPtrArr_[i] = new UdpSocket(port);
            if (!udpSockPtrArr_[i]->isFdValid()) {
                config_.isMultiPortEnable = false;
            }

            if (port == config_.localPort) {
                udpSockPtr_ = udpSockPtrArr_[i];
            }
        }
    } else {
        udpSockPtr_ = new UdpSocket(config_.localPort);
    }

    if (!udpSockPtr_) {
        udpSockPtr_ = new UdpSocket(config_.localPort);
    }
}

void TapLan::handleTapData(void* buf, size_t bufLen)
{
    sockaddr_in6 dstAddr;
    memset(&dstAddr, 0, sizeof(dstAddr));
    dstAddr.sin6_family = AF_INET6;

    std::time_t now = std::time(nullptr);
    uint16_t portOffset = (now / 60 % 60) % 4;

    if (config_.runMode == RunMode_Server) {
        EtherHeader* eh = reinterpret_cast<EtherHeader*>(buf);
        bool needBroadcast = eh->dst[0] & 0x01;
        if (!needBroadcast) {
            std::shared_ptr<Node> n = nodeMgrPtr_->findNode(getMacNum(eh->dst));
            memcpy(&dstAddr.sin6_addr, &n->ipv6Addr, sizeof(dstAddr.sin6_addr));
            dstAddr.sin6_port = n->ipv6Port;
            (config_.isMultiPortEnable? udpSockPtrArr_[portOffset]: udpSockPtr_)->sendTo(buf, bufLen, (const sockaddr*)&dstAddr, sizeof(dstAddr));

            return ;
        }

        uint64_t srcMacNum = getMacNum(eh->src);
        auto broadcast = [&](uint64_t m, std::shared_ptr<Node> n) {
            if (n->status == NODE_OFFLINE || n->mac.num == srcMacNum)
                return ;

            memcpy(&dstAddr.sin6_addr, &n->ipv6Addr, sizeof(in6_addr));
            dstAddr.sin6_port = n->ipv6Port;
            (config_.isMultiPortEnable? udpSockPtrArr_[portOffset]: udpSockPtr_)->sendTo(buf, bufLen, (sockaddr*)&dstAddr, sizeof(dstAddr));
        };
        nodeMgrPtr_->forEach(broadcast);
    }
    else if (config_.runMode == RunMode_Client) {
        (config_.isMultiPortEnable? udpSockPtrArr_[portOffset]: udpSockPtr_)->sendTo(buf, bufLen, (const sockaddr*)&serverAddr_, sizeof(serverAddr_));
    } else {
        // RunMode_None
    }
}

void TapLan::readTapData()
{
    uint8_t tapRxBuf[65536];

    while (runFlag_) {
        ssize_t readBytes = TapDevPtr->read(tapRxBuf, sizeof(tapRxBuf), 3000);
        if (readBytes <= ETHERNET_HEADER_LEN) {
            continue;
        }

        handleTapData(tapRxBuf, readBytes);
    }

    std::cout << "Thread " << sendThreadName_ << " has exited." << std::endl;
}

void TapLan::handleSockData(void* buf, size_t bufLen, sockaddr_in6& srcAddr)
{
    std::time_t now = std::time(nullptr);
    uint16_t portOffset = (now / 60 % 60) % 4;

    if (config_.runMode == RunMode_Server) {
        sockaddr_in6 dstAddr;
        memset(&dstAddr, 0, sizeof(dstAddr));
        dstAddr.sin6_family = AF_INET6;
        EtherHeader* eh = reinterpret_cast<EtherHeader*>(buf);
        uint64_t srcMacNum = getMacNum(eh->src);
        uint64_t dstMacNum = getMacNum(eh->dst);
        bool needBroadcast = eh->dst[0] & 0x01;
        bool isSendToMe = needBroadcast || (dstMacNum == mac_.num);

        if (needBroadcast) {        // broadcast
            auto broadcast = [&](uint64_t m, std::shared_ptr<Node> n) {
                if (n->status == NODE_OFFLINE || n->mac.num == srcMacNum || n->mac.num == mac_.num)
                    return ;

                memcpy(&dstAddr.sin6_addr, &n->ipv6Addr, sizeof(in6_addr));
                dstAddr.sin6_port = n->ipv6Port;
                (config_.isMultiPortEnable? udpSockPtrArr_[portOffset]: udpSockPtr_)->sendTo(buf, bufLen, (sockaddr*)&dstAddr, sizeof(dstAddr));
            };

            nodeMgrPtr_->forEach(broadcast);
            TapDevPtr->write(buf, bufLen);
        } else if (!isSendToMe) {   // not broadcast && not send to me
            std::shared_ptr<Node> n = nodeMgrPtr_->findNode(dstMacNum);
            if (!n) {
                return ;
            }
            memcpy(&dstAddr.sin6_addr, &n->ipv6Addr, sizeof(dstAddr.sin6_addr));
            dstAddr.sin6_port = n->ipv6Port;
            (config_.isMultiPortEnable? udpSockPtrArr_[portOffset]: udpSockPtr_)->sendTo(buf, bufLen, (sockaddr*)&dstAddr, sizeof(sockaddr_in6));
        } else {                    // not broadcast && send to me
            TapDevPtr->write(buf, bufLen);
        }
    } else if (config_.runMode == RunMode_Client) {
        TapDevPtr->write(buf, bufLen);
    } else {
        // RunMode_None
    }
}

void TapLan::recvSockData()
{
    uint8_t udpRxBuf[65536];

    sockaddr_in6 srcAddr;
    socklen_t srcAddrLen = sizeof(srcAddr);

    sockaddr_in6 dstAddr;
    memset(&dstAddr, 0, sizeof(dstAddr));
    dstAddr.sin6_family = AF_INET6;

    TapLanPollFd pfds[4];
    if (config_.isMultiPortEnable) {
        for (int i = 0; i < 4; ++i) {
            pfds[i] = { static_cast<TapLanSocket>(*udpSockPtrArr_[i]), POLLIN, 0 };
        }
    }

    while (runFlag_) {
        if (!config_.isMultiPortEnable) {
            ssize_t recvBytes = udpSockPtr_->recvFrom(udpRxBuf, sizeof(udpRxBuf), (sockaddr*)&srcAddr, &srcAddrLen);
            if (recvBytes <= ETHERNET_HEADER_LEN) {
                continue;
            }

            handleSockData(udpRxBuf, recvBytes, srcAddr);
            continue;
        } else {
            int pollCnt = TapLanPoll(pfds, 4, 3000);
            if (pollCnt < 0) {
                LOGE(TAG, "TapLanPoll failed.");
                continue;
            } else if (pollCnt == 0) {
                continue;
            }
            for (int i = 0; i < 4; ++i) {
                if (pfds[i].revents != 0) {
                    ssize_t recvBytes = udpSockPtrArr_[i]->recvFrom(udpRxBuf, sizeof(udpRxBuf), (sockaddr*)&srcAddr, &srcAddrLen);
                    if (recvBytes <= ETHERNET_HEADER_LEN) {
                        continue;
                    }

                    handleSockData(udpRxBuf, recvBytes, srcAddr);
                }
            }
        }
    }

    std::cout << "Thread " << recvThreadName_ << " has exited." << std::endl;
}

void TapLan::syncNodeStatusToClients()
{
    tcpSockPtr_ = new TcpSocket(config_.localPort);
    if (!tcpSockPtr_->isFdValid() || !tcpSockPtr_->listen(5)) {
        LOGF(TAG, "run sync server failed.");
        runFlag_ = false;
        return ;
    }

    std::vector<TapLanPollFd> pfds;
    std::vector<TcpSocket> clients;
    std::map<TapLanSocket, uint64_t> sockToMacMap;
    pfds.push_back({ static_cast<TapLanSocket>(*tcpSockPtr_), POLLIN, 0 });
    while (runFlag_) {
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
                if (recvBytes == 0) {
                    auto it = sockToMacMap.find(static_cast<TapLanSocket>(client));
                    if (it != sockToMacMap.end()) {
                        if (!nodeMgrPtr_->setNodeStatus(it->second, NODE_OFFLINE))
                            LOGW(TAG, "set node status failed.");
                        sockToMacMap.erase(it->first);
                    }
                    pfds.erase(pfds.begin() + i);
                    clients.erase(clients.begin() + i - 1);
                    continue;
                } else if (recvBytes != sizeof(SyncMessage)) {
                    continue;
                }

                const SyncMessage* req = reinterpret_cast<const SyncMessage*>(recvBuf);
                if (req->op != 1 || req->ipv4Addr.s_addr != 0 || req->netIDLen != 0 || req->numsOfNode != 0) {
                    continue;
                }

                std::shared_ptr<Node> n = nodeMgrPtr_->findNode(req->mac.num);
                if (!n) {
                    sockaddr_in6 addr;
                    client.getRemoteAddr(&addr);
                    n = nodeMgrPtr_->addNode(&addr, req->mac.num);
                    if (!n) {
                        LOGE(TAG, "add node failed.");
                        continue;
                    }
                }

                sockToMacMap[static_cast<TapLanSocket>(client)] = req->mac.num;
                nodeMgrPtr_->setNodeStatus(req->mac.num, NODE_ONLINE);

                memcpy(sendBuf, recvBuf, recvBytes);
                SyncMessage* resp = reinterpret_cast<SyncMessage*>(sendBuf);
                resp->op = 2;
                resp->netIDLen = config_.netNumLen;
                resp->ipv4Addr.s_addr = n->ipv4Addr.s_addr;

                size_t offset = recvBytes;
                nodeMgrPtr_->forEach([&](uint64_t k, std::shared_ptr<Node> v) {
                    memcpy(sendBuf + offset, v.get(), sizeof(Node));
                    offset += sizeof(Node);
                    ++resp->numsOfNode;
                });

                size_t sendBytes = offset;
                client.send(sendBuf, sendBytes);
            }
        }
    }
}

void TapLan::syncNodeStatusFromServer()
{
    bool isConnected = false;
    bool hasIPv4Addr = false;
    uint8_t buf[65536];

    auto retryConnect = [&]() {
        if (isConnected) {
            return isConnected;
        }

        delete tcpSockPtr_;
        tcpSockPtr_ = new TcpSocket(config_.localPort, serverAddr_);
        if (!tcpSockPtr_->isFdValid()) {
            LOGE(TAG, "create tcp socket failed.");
            std::this_thread::sleep_for(std::chrono::seconds(3));
            return isConnected;
        }

        while (!isConnected && runFlag_) {
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

        SyncMessage* req = reinterpret_cast<SyncMessage*>(buf);
        memset(req, 0, sizeof(SyncMessage));
        req->op = 1;
        req->mac.num = mac_.num;
        while (!hasIPv4Addr && runFlag_) {
            tcpSockPtr_->send(req, sizeof(SyncMessage));

            ssize_t recvBytes = tcpSockPtr_->recv(buf, sizeof(buf));
            if (recvBytes == 0) {
                LOGW(TAG, "server has close, retrying to connect...");
                isConnected = false;
                while (!retryConnect() && runFlag_);
                hasIPv4Addr = false;
                continue;
            } else if (recvBytes < sizeof(SyncMessage)) {
                LOGE(TAG, "recvBytes[%ld] is not correct.", recvBytes);
                std::this_thread::sleep_for(std::chrono::seconds(3));
                continue;
            }

            SyncMessage* resp = reinterpret_cast<SyncMessage*>(buf);
            if (resp->op != 2) {
                LOGE(TAG, "resp is not correct.");
                continue;
            }
            TapDevPtr->setIpv4Addr(&resp->ipv4Addr, resp->netIDLen);
            hasIPv4Addr = true;

            nodeMgrPtr_ = std::make_shared<NodeMgr>(ntohs(resp->ipv4Addr.s_addr), resp->netIDLen);
            for (int i = 0; i < resp->numsOfNode; ++i) {
                Node* n = reinterpret_cast<Node*>(buf + sizeof(SyncMessage) + i * sizeof(Node));
                nodeMgrPtr_->addNode(n->mac.num, *n);
            }
        }

        return hasIPv4Addr;
    };

    do {
        while (!retryConnect() && runFlag_);
        while (!getAndSetIPv4Addr() && runFlag_);
        ssize_t recvBytes = tcpSockPtr_->recv(buf, sizeof(buf));
        if (recvBytes == 0) {
            isConnected = false;
            hasIPv4Addr = false;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    } while (runFlag_);
}

void TapLan::syncNodeStatus()
{
    if (config_.runMode == RunMode_Server) {
        syncNodeStatusToClients();
    } else if (config_.runMode == RunMode_Client) {
        syncNodeStatusFromServer();
    } else {
        // RunMode_None
    }

    std::cout << "Thread " << syncThreadName_ << " has exited." << std::endl;
}

void TapLan::showNodeStatus()
{
    if (!nodeMgrPtr_) {
        LOGI(TAG, "No other nodes have been obtained from the server.");
        return ;
    }

    LOGR("Status     TapLan MAC address    TapLan IP address    Public IP address\n");
//  LOGR("offline    00:00:00:00:00:00     255.255.255.255      aaaa:bbbb:cccc:dddd:eeee:ffff:aaaa:bbbb");

    auto printNodeStatus = [&](uint64_t m, std::shared_ptr<Node> n) {
        char tapmacbuf[32];
        sprintf(tapmacbuf, "%.2X:%.2X:%.2X:%.2X:%.2X:%.2X",
            n->mac.addr[0], n->mac.addr[1], n->mac.addr[2], 
            n->mac.addr[3], n->mac.addr[4], n->mac.addr[5]);

        std::string ipv6str = IPv6_NTOP(n->ipv6Addr);

        char tapipbuf[INET_ADDRSTRLEN];
        uint8_t* ipv4addr = reinterpret_cast<uint8_t*>(&(n->ipv4Addr));
        sprintf(tapipbuf, "%u.%u.%u.%u", ipv4addr[0], ipv4addr[1], ipv4addr[2], ipv4addr[3]);

        char buf[128];
        sprintf(buf, "%-11s%-22s%-21s[%s]:%u\n",
            (n->status == NODE_ONLINE? "ONLINE": "OFFLINE"),
            tapmacbuf, tapipbuf, ipv6str.c_str(), ntohs(n->ipv6Port));
        LOGR("%s", buf);
    };
    nodeMgrPtr_->forEach(printNodeStatus);
}

void TapLan::showStats()
{
    uint64_t totalSendBytes = 0, totalSendErrors = 0, totalRecvBytes = 0, totalRecvErrors = 0;
    LOGR("multi-port mode is %s\n", (config_.isMultiPortEnable? "enable": "disable"));
    if (config_.isMultiPortEnable) {
        for (int i = 0; i < 4; ++i) {
            uint64_t sendBytes = udpSockPtrArr_[i]->getSendBytes(),
                     sendErrors = udpSockPtrArr_[i]->getSendErrors(),
                     recvBytes = udpSockPtrArr_[i]->getRecvBytes(),
                     recvErrors = udpSockPtrArr_[i]->getRecvErrors();
            LOGR("UDP port %u:\n", udpSockPtrArr_[i]->getBindPort());
            LOGR("    TX bytes:   %lu\n", sendBytes);
            LOGR("    TX errors:  %lu\n", sendErrors);
            LOGR("    RX bytes:   %lu\n", recvBytes);
            LOGR("    RX errors:  %lu\n", recvErrors);
            totalSendBytes += sendBytes;
            totalSendErrors += sendErrors;
            totalRecvBytes += recvBytes;
            totalRecvErrors += recvErrors;
        }
    } else {
        totalSendBytes += udpSockPtr_->getSendBytes();
        totalSendErrors += udpSockPtr_->getSendErrors();
        totalRecvBytes += udpSockPtr_->getRecvBytes();
        totalRecvErrors += udpSockPtr_->getRecvErrors();
    }
    LOGR("total:\n");
    LOGR("    TX bytes:   %lu\n", totalSendBytes);
    LOGR("    TX errors:  %lu\n", totalSendErrors);
    LOGR("    RX bytes:   %lu\n", totalRecvBytes);
    LOGR("    RX errors:  %lu\n", totalRecvErrors);
}

bool TapLan::run()
{
    if (!runFlag_)
        return false;

    sendThread_ = std::thread(&TapLan::readTapData, this);
    pthread_setname_np(sendThread_.native_handle(), sendThreadName_);

    recvThread_ = std::thread(&TapLan::recvSockData, this);
    pthread_setname_np(recvThread_.native_handle(), recvThreadName_);

    syncThread_ = std::thread(&TapLan::syncNodeStatus, this);
    pthread_setname_np(syncThread_.native_handle(), syncThreadName_);

    return true;
}

bool TapLan::stop()
{
    if (!runFlag_)
        return false;

    runFlag_ = false;
    if (sendThread_.joinable())
        sendThread_.join();
    if (recvThread_.joinable())
        recvThread_.join();
    if (syncThread_.joinable())
        syncThread_.join();

    return true;
}

uint64_t getMacNum(const uint8_t* mac)
{
    Mac m;
    m.num = 0;
    memcpy(m.addr, mac, 6);

    return m.num;
}
