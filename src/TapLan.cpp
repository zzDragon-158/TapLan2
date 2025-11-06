#include "TapLan.hpp"

static const char* TAG = "TapLan";

TapLan::TapLan(uint16_t port): runFlag_(false), runMode_(RunMode_Server), udpSockPtr_(nullptr)
{
    memset(&serverAddr_, 0, sizeof(serverAddr_));
    serverAddr_.sin6_family = AF_INET6;
    serverAddr_.sin6_port = htons(port);

    udpSockPtr_ = new UdpSocket(port);
    tcpSockPtr_ = new TcpSocket(port);
    runFlag_ = udpSockPtr_->isFdValid() && tcpSockPtr_->isFdValid() && TapDevPtr->open();

    if (runFlag_) {
        mac_.num = 0;
        TapDevPtr->getMacAddr(mac_.addr, sizeof(Mac));
        Node* n = NodeMgrPtr->newNode(&serverAddr_, mac_.addr);
        NodeMgrPtr->addNode(n);
        TapDevPtr->setIpv4Addr(&n->ipv4Addr, 24);   // needmod
    }
}

TapLan::TapLan(const char* ipv6Addr, uint16_t port): runFlag_(false), runMode_(RunMode_Client), udpSockPtr_(nullptr)
{
    memset(&serverAddr_, 0, sizeof(serverAddr_));
    serverAddr_.sin6_family = AF_INET6;
    inet_pton(AF_INET6, ipv6Addr, &serverAddr_.sin6_addr);
    serverAddr_.sin6_port = htons(port);

    udpSockPtr_ = new UdpSocket(port);
    tcpSockPtr_ = new TcpSocket(serverAddr_);
    runFlag_ = udpSockPtr_->isFdValid() && tcpSockPtr_->isFdValid() && TapDevPtr->open();

    if (runFlag_) {
        mac_.num = 0;
        TapDevPtr->getMacAddr(mac_.addr, sizeof(Mac));
        // in_addr ipv4Addr;
        // ipv4Addr.S_un.S_un_b.s_b1 = 192;
        // ipv4Addr.S_un.S_un_b.s_b2 = 168;
        // ipv4Addr.S_un.S_un_b.s_b3 = 208;
        // ipv4Addr.S_un.S_un_b.s_b4 = 3;
        // TapDevPtr->setIpv4Addr(&ipv4Addr, 24);   // needmod
    }
}

TapLan::~TapLan()
{
    stop();
}

void TapLan::handleTapData(void* buf, size_t bufLen)
{
    sockaddr_in6 dstAddr;
    memset(&dstAddr, 0, sizeof(dstAddr));
    dstAddr.sin6_family = AF_INET6;

    if (runMode_ == RunMode_Server) {
        EtherHeader* eh = reinterpret_cast<EtherHeader*>(buf);
        bool needBroadcast = eh->dst[0] & 0x01;
        if (!needBroadcast) {
            Node* n = NodeMgrPtr->findNode(NodeMgrPtr->getMacNum(eh->dst));
            memcpy(&dstAddr.sin6_addr, &n->ipv6Addr, sizeof(dstAddr.sin6_addr));
            dstAddr.sin6_port = n->ipv6Port;
            udpSockPtr_->sendTo(buf, bufLen, (const sockaddr*)&dstAddr, sizeof(dstAddr));

            return ;
        }

        uint64_t srcMacNum = NodeMgrPtr->getMacNum(eh->src);
        auto broadcast = [&](uint64_t m, Node* n) {
            if (n->status == NodeStatus_OFFLINE || n->mac.num == srcMacNum)
                return ;

            memcpy(&dstAddr.sin6_addr, &n->ipv6Addr, sizeof(in6_addr));
            dstAddr.sin6_port = n->ipv6Port;
            udpSockPtr_->sendTo(buf, bufLen, (sockaddr*)&dstAddr, sizeof(dstAddr));
        };
        NodeMgrPtr->forEach(broadcast);
    }
    else if (runMode_ == RunMode_Client) {
        udpSockPtr_->sendTo(buf, bufLen, (const sockaddr*)&serverAddr_, sizeof(serverAddr_));
    } else {
        // RunMode_None
    }
}

void TapLan::readTapData()
{
    uint8_t tapRxBuf[65536];

    while (runFlag_) {
        ssize_t readBytes = TapDevPtr->read(tapRxBuf, sizeof(tapRxBuf), 5000);
        if (readBytes <= ETHERNET_HEADER_LEN) {
            continue;
        }

        handleTapData(tapRxBuf, readBytes);
    }
}

void TapLan::handleSockData(void* buf, size_t bufLen, sockaddr_in6& srcAddr)
{
    if (runMode_ == RunMode_Server) {
        sockaddr_in6 dstAddr;
        memset(&dstAddr, 0, sizeof(dstAddr));
        dstAddr.sin6_family = AF_INET6;
        EtherHeader* eh = reinterpret_cast<EtherHeader*>(buf);
        uint64_t srcMacNum = NodeMgrPtr->getMacNum(eh->src);
        uint64_t dstMacNum = NodeMgrPtr->getMacNum(eh->dst);
        bool needBroadcast = eh->dst[0] & 0x01;
        bool isSendToMe = needBroadcast || (dstMacNum == mac_.num);

        Node* node = NodeMgrPtr->findNode(dstMacNum);
        if (!node) {
            return ;
        }

        if (needBroadcast) {        // broadcast
            auto broadcast = [&](uint64_t m, Node* n) {
                if (n->status == NodeStatus_OFFLINE || n->mac.num == srcMacNum || n->mac.num == mac_.num)
                    return ;

                memcpy(&dstAddr.sin6_addr, &n->ipv6Addr, sizeof(in6_addr));
                dstAddr.sin6_port = n->ipv6Port;
                udpSockPtr_->sendTo(buf, bufLen, (sockaddr*)&dstAddr, sizeof(dstAddr));
            };

            NodeMgrPtr->forEach(broadcast);
            TapDevPtr->write(buf, bufLen);
        } else if (!isSendToMe) {   // not broadcast && not send to me
            memcpy(&dstAddr.sin6_addr, &node->ipv6Addr, sizeof(dstAddr.sin6_addr));
            dstAddr.sin6_port = node->ipv6Port;
            udpSockPtr_->sendTo(buf, bufLen, (sockaddr*)&dstAddr, sizeof(sockaddr_in6));
        } else {                    // not broadcast && send to me
            TapDevPtr->write(buf, bufLen);
        }
    } else if (runMode_ == RunMode_Client) {
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

    while (runMode_) {
        ssize_t recvBytes = udpSockPtr_->recvFrom(udpRxBuf, sizeof(udpRxBuf), (sockaddr*)&srcAddr, &srcAddrLen);
        if (recvBytes <= ETHERNET_HEADER_LEN) {
            continue;
        }

        handleSockData(udpRxBuf, recvBytes, srcAddr);
    }
}

void TapLan::syncNodeStatusToClients()
{
    // callback of handle-recv-data
    // TcpSocket::CbRecvFunc cbRecv = [&](const void* reqBuf, const size_t& reqLen,
    //     const sockaddr_in6* addr, TapLanSocket sock,
    //     void* respBuf, size_t& respLen)
    // {
    //     if (reqLen != sizeof(SyncMessage))
    //         return false;

    //     // req value check
    //     const SyncMessage* req = reinterpret_cast<const SyncMessage*>(reqBuf);
    //     if (req->op != 1 || req->ipv4Addr.s_addr != 0 || req->netIDLen != 0 || req->numsOfNode != 0) {
    //         return false;
    //     }

    //     // new node if not exist
    //     Node* node = NodeMgrPtr->findNode(req->mac.num);
    //     if (!node) {
    //         node = NodeMgrPtr->newNode(addr, req->mac.addr);
    //         if (!NodeMgrPtr->addNode(node)) {
    //             LOGE(TAG, "allocate node failed.");
    //             return false;
    //         }
    //     }

    //     // fill resp
    //     memcpy(respBuf, reqBuf, reqLen);
    //     SyncMessage* resp = reinterpret_cast<SyncMessage*>(respBuf);
    //     resp->op = 2;
    //     resp->netIDLen = 24;
    //     resp->ipv4Addr.s_addr = node->ipv4Addr.s_addr;

    //     // fill all node status
    //     size_t bufOffset = reqLen;
    //     auto fillNodeToSM = [&](uint64_t m, Node* n) {
    //         memcpy(respBuf + bufOffset, n, sizeof(Node));
    //         bufOffset += sizeof(Node);
    //         ++(resp->numsOfNode);
    //     };
    //     NodeMgrPtr->forEach(fillNodeToSM);
    //     if (respLen < bufOffset) {
    //         LOGE(TAG, "not enough buffer to resp.");
    //         return false;
    //     }
    //     respLen = bufOffset;

    //     return true;
    // };

    tcpSockPtr_->listen(5);
    std::vector<TapLanPollFd> pfds;
    std::vector<TcpSocket> clients;
    pfds.push_back({ *tcpSockPtr_, POLLIN, 0 });
    while (runFlag_) {
        // tcpSockPtr_->recv(cbRecv);
        int pollCnt = TapLanPoll(pfds.data(), pfds.size(), 5000);
        if (pollCnt < 0) {
            LOGE(TAG, "TapLanPoll failed. %d");
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
        auto it = pfds.end() - 1;
        while (pollCnt && it-- >= pfds.begin()) {
            if (it->revents != 0) {
                --pollCnt;
                uint8_t recvBuf[65536];
                uint8_t sendBuf[65536];
                auto idx = std::distance(pfds.begin(), it) - 1;
                TcpSocket& client = clients[idx];
                ssize_t recvBytes = client.recv(recvBuf, sizeof(recvBuf));
                if (recvBytes == 0) {
                    pfds.erase(it);
                    clients.erase(clients.begin() + idx);
                } else if (recvBytes != sizeof(SyncMessage)) {
                    continue;
                }

                const SyncMessage* req = reinterpret_cast<const SyncMessage*>(recvBuf);
                if (req->op != 1 || req->ipv4Addr.s_addr != 0 || req->netIDLen != 0 || req->numsOfNode != 0) {
                    continue;
                }

                Node* node = NodeMgrPtr->findNode(req->mac.num);
                if (!node) {
                    sockaddr_in6 addr;
                    client.getRemoteAddr(&addr);
                    node = NodeMgrPtr->newNode(&addr, req->mac.addr);
                    if (!node) {
                        LOGE(TAG, "new node failed.");
                        continue;
                    }
                    if (!NodeMgrPtr->addNode(node)) {
                        LOGE(TAG, "add node failed.");
                        continue;
                    }
                }

                memcpy(sendBuf, recvBuf, recvBytes);
                SyncMessage* resp = reinterpret_cast<SyncMessage*>(sendBuf);
                resp->op = 2;
                resp->netIDLen = 24;
                resp->ipv4Addr.s_addr = node->ipv4Addr.s_addr;

                size_t sendBytes = recvBytes;
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
        while (!isConnected) {
            if (!tcpSockPtr_->connect()) {
                LOGE(TAG, "Can not connect to server, retrying ...");
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }
            isConnected = true;
        }
    };

    auto getAndSetIPv4Addr = [&] () {
        if (hasIPv4Addr)
            return ;
        SyncMessage* req = reinterpret_cast<SyncMessage*>(buf);
        memset(req, 0, sizeof(SyncMessage));
        req->op = 1;
        req->mac.num = mac_.num;
        while (!hasIPv4Addr) {
            tcpSockPtr_->send(req, sizeof(SyncMessage));

            ssize_t recvBytes = tcpSockPtr_->recv(buf, sizeof(buf));
            if (recvBytes == 0) {
                isConnected = false;
                retryConnect();
                continue;
            } else if (recvBytes < sizeof(SyncMessage)) {
                LOGE(TAG, "recvBytes[%ld] is not correct.", recvBytes);
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }

            SyncMessage* resp = reinterpret_cast<SyncMessage*>(buf);
            if (resp->op != 2) {
                LOGE(TAG, "resp is not correct.");
                continue;
            }
            TapDevPtr->setIpv4Addr(&resp->ipv4Addr, resp->netIDLen);
            hasIPv4Addr = true;
        }
    };

    do {
        retryConnect();
        getAndSetIPv4Addr();
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
    if (runMode_ == RunMode_Server) {
        syncNodeStatusToClients();
    } else if (runMode_ == RunMode_Client) {
        syncNodeStatusFromServer();
    } else {
        // RunMode_None
    }
}

void TapLan::showNodeStatus()
{
    fprintf(stdout, "status     TapLan MAC address    TapLan IP address    Public IP address\n");
//  fprintf(stdout, "offline    00:00:00:00:00:00     255.255.255.255      aaaa:bbbb:cccc:dddd:eeee:ffff:aaaa:bbbb");

    auto printNodeStatus = [&](uint64_t m, Node* n) {
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
            (n->status == NodeStatus_ONLINE? "ONLINE": "OFFLINE"),
            tapmacbuf, tapipbuf, ipv6str.c_str(), ntohs(n->ipv6Port));
        fprintf(stdout, "%s", buf);
    };
    NodeMgrPtr->forEach(printNodeStatus);

    fflush(stdout);
}

bool TapLan::run()
{
    if (!runFlag_)
        return false;

    threadReadTapData_ = std::thread(&TapLan::readTapData, this);
    threadRecvSockData_ = std::thread(&TapLan::recvSockData, this);
    threadSyncNodeStatus_ = std::thread(&TapLan::syncNodeStatus, this);

    return true;
}

bool TapLan::stop()
{
    if (!runFlag_)
        return false;

    runFlag_ = false;
    if (threadReadTapData_.joinable())
        threadReadTapData_.join();
    if (threadRecvSockData_.joinable())
        threadRecvSockData_.join();
    if (threadSyncNodeStatus_.joinable())
        threadSyncNodeStatus_.join();

    return true;
}
