#include "TapLan.hpp"

const char* TAG = "TapLan";

TapLan::TapLan(uint16_t port): runFlag_(false), runMode_(RunMode_Server), udpSockPtr_(nullptr)
{
    memset(&serverAddr_, 0, sizeof(serverAddr_));
    serverAddr_.sin6_family = AF_INET6;
    serverAddr_.sin6_port = htons(port);

    udpSockPtr_ = new UdpSocket(port);
    tcpSockPtr_ = new TcpSocket(port);
    runFlag_ = udpSockPtr_->open() && TapDevPtr->open() && tcpSockPtr_->open();

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
    runFlag_ = udpSockPtr_->open() && TapDevPtr->open();

    if (runFlag_) {
        mac_.num = 0;
        TapDevPtr->getMacAddr(mac_.addr, sizeof(Mac));
        in_addr ipv4Addr;
        ipv4Addr.S_un.S_un_b.s_b1 = 192;
        ipv4Addr.S_un.S_un_b.s_b2 = 168;
        ipv4Addr.S_un.S_un_b.s_b3 = 208;
        ipv4Addr.S_un.S_un_b.s_b4 = 3;
        TapDevPtr->setIpv4Addr(&ipv4Addr, 24);   // needmod
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
            if (n->status == NodeSTATUS_OFFLINE || n->mac.num == srcMacNum)
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
            node = NodeMgrPtr->newNode(&srcAddr, eh->src);
            if (node)
                NodeMgrPtr->addNode(node);
        }

        if (needBroadcast) {        // broadcast
            auto broadcast = [&](uint64_t m, Node* n) {
                if (n->status == NodeSTATUS_OFFLINE || n->mac.num == srcMacNum || n->mac.num == mac_.num)
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

void TapLan::syncNodeStatusToClient()
{
    bool isConnected = false;
    uint8_t msgBuf[65536];
    while (runFlag_) {
        
    }
}

void TapLan::syncNodeStatusFromServer()
{
    while (runFlag_) {
        
    }
}

void TapLan::syncNodeStatus()
{
    if (runMode_ == RunMode_Server) {
        syncNodeStatusToClient();
    } else if (runMode_ == RunMode_Client) {
        syncNodeStatusFromServer();
    } else {
        // RunMode_None
    }
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
