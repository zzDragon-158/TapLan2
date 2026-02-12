#include    "TapLan.hpp"

static const char* TAG = "[TapLan]";
ConfigDataT TapLan::config_;

TapLan::TapLan(): serverAddr_{}, udpSockPtr_(nullptr), udpSockPtrArr_{},
                nodeMgrPtr_(nullptr), recvThreadName_("recvWorker"),
                sendThreadName_("sendWorker"), syncThreadName_("syncWorker")
{
    if (config_.runMode == RunMode_Server) {
        LOGI(TAG, "We are running in server mode.");

        nodeMgrPtr_ = std::make_shared<NodeMgr>();

        serverAddr_.sin6_family = AF_INET6;
        serverAddr_.sin6_port = htons(config_.localPort);

        initUdpSockPtr();

        config_.isRunning = udpSockPtr_->isFdValid() && TapDevPtr->isFdVaild();

        if (config_.isRunning) {
            TapDevPtr->getMacAddr(config_.mac);
            std::shared_ptr<Node> n = nodeMgrPtr_->addNode(&serverAddr_, config_.mac);
            TapDevPtr->setIPv4Addr(&n->ipv4Addr, config_.netNumLen);
        }
    } else if (config_.runMode == RunMode_Client) {
        LOGI(TAG, "We are running in client mode.");

        nodeMgrPtr_ = std::make_shared<NodeMgr>();

        serverAddr_.sin6_family = AF_INET6;
        memcpy(&serverAddr_.sin6_addr, &config_.remoteAddr, sizeof(in6_addr));
        serverAddr_.sin6_port = config_.remotePort;

        initUdpSockPtr();

        config_.isRunning = udpSockPtr_->isFdValid() && TapDevPtr->isFdVaild();

        if (config_.isRunning) {
            TapDevPtr->getMacAddr(config_.mac);
        }
    } else {
        // RunMode_None
    }
}

TapLan::~TapLan()
{
    stop();
}

UdpSocket* TapLan::getUdpSockPtr()
{
    UdpSocket* curUdpSockPtr = udpSockPtr_;
    if (config_.switchPortInterval) {
        std::time_t now = std::time(nullptr);
        uint16_t portOffset = (now / 60 / config_.switchPortInterval) % 4;
        curUdpSockPtr = udpSockPtrArr_[portOffset];
    }

    return curUdpSockPtr;
}

void TapLan::initUdpSockPtr()
{
    if (config_.switchPortInterval) {
        uint16_t startPort = config_.localPort - config_.localPort % 4;
        for (int i = 0; i < 4; ++i) {
            uint16_t port = startPort + i;
            udpSockPtrArr_[i] = new UdpSocket(port);
            if (!udpSockPtrArr_[i]->isFdValid()) {
                config_.switchPortInterval = 0;
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

void TapLan::handleTapData(uint8_t* buf, size_t bufLen)
{
    sockaddr_in6 dstAddr{};
    dstAddr.sin6_family = AF_INET6;

    UdpSocket* udpSockPtr = getUdpSockPtr();

    EtherHeader& eh = reinterpret_cast<EtherHeader&>(*buf);
    Mac& dstMac = reinterpret_cast<Mac&>(eh.dst);
    Mac& srcMac = reinterpret_cast<Mac&>(eh.src);
    bool needBroadcast = eh.dst[0] & 0x01;
    if (config_.runMode == RunMode_Server) {
        if (!needBroadcast) {
            std::shared_ptr<Node> n = nodeMgrPtr_->findNode(dstMac);
            if (n) {
                memcpy(&dstAddr.sin6_addr, &n->ipv6Addr, sizeof(dstAddr.sin6_addr));
                dstAddr.sin6_port = n->ipv6Port;
                udpSockPtr->sendTo(buf, bufLen, (const sockaddr*)&dstAddr, sizeof(dstAddr));
            } else {
                udpSockPtr->incDropped(1);
            }
        } else {
            size_t sendCnt = 0;
            nodeMgrPtr_->forEach([&](uint64_t m, std::shared_ptr<Node> n) {
                if (n->status == NODE_OFFLINE || n->mac == srcMac)
                    return ;

                memcpy(&dstAddr.sin6_addr, &n->ipv6Addr, sizeof(in6_addr));
                dstAddr.sin6_port = n->ipv6Port;
                udpSockPtr->sendTo(buf, bufLen, (sockaddr*)&dstAddr, sizeof(dstAddr));
                ++sendCnt;
            }, false);
            if (sendCnt == 0) {
                udpSockPtr->incDropped(1);
            }
        }
    }
    else if (config_.runMode == RunMode_Client) {
        if (!config_.noServerMode || nodeMgrPtr_->findNode(dstMac) || needBroadcast)
            udpSockPtr->sendTo(buf, bufLen, (const sockaddr*)&serverAddr_, sizeof(serverAddr_));
    } else {
        // RunMode_None
    }
}

void TapLan::readTapData()
{
    uint8_t tapRxBuf[65536];

    while (config_.isRunning) {
        ssize_t readBytes = TapDevPtr->read(tapRxBuf, sizeof(tapRxBuf), 3000);
        if (readBytes < ETHERNET_HEADER_LEN) {
            continue;
        }

        handleTapData(tapRxBuf, readBytes);
    }

    std::cout << "Thread " << sendThreadName_ << " has exited." << std::endl;
}

void TapLan::handleSockData(uint8_t* buf, size_t bufLen, sockaddr_in6& srcAddr)
{
    UdpSocket* udpSockPtr = getUdpSockPtr();

    EtherHeader eh = reinterpret_cast<EtherHeader&>(*buf);
    Mac& srcMac = reinterpret_cast<Mac&>(eh.src);
    Mac& dstMac = reinterpret_cast<Mac&>(eh.dst);
    bool needBroadcast = eh.dst[0] & 0x01;
    bool isSendToMe = needBroadcast || (dstMac == config_.mac);
    if (config_.runMode == RunMode_Server) {
        sockaddr_in6 dstAddr{};
        dstAddr.sin6_family = AF_INET6;

        if (needBroadcast) {        // broadcast
            auto broadcast = [&](uint64_t m, std::shared_ptr<Node> n) {
                if (n->status == NODE_OFFLINE || n->mac == srcMac || n->mac == config_.mac)
                    return ;

                memcpy(&dstAddr.sin6_addr, &n->ipv6Addr, sizeof(in6_addr));
                dstAddr.sin6_port = n->ipv6Port;
                udpSockPtr->sendTo(buf, bufLen, (sockaddr*)&dstAddr, sizeof(dstAddr));
            };

            nodeMgrPtr_->forEach(broadcast, false);
            TapDevPtr->write(buf, bufLen);
        } else if (!isSendToMe) {   // not broadcast && not send to me
            std::shared_ptr<Node> n = nodeMgrPtr_->findNode(dstMac);
            if (!n) {
                return ;
            }
            memcpy(&dstAddr.sin6_addr, &n->ipv6Addr, sizeof(dstAddr.sin6_addr));
            dstAddr.sin6_port = n->ipv6Port;
            udpSockPtr->sendTo(buf, bufLen, (sockaddr*)&dstAddr, sizeof(sockaddr_in6));
        } else {                    // not broadcast && send to me
            TapDevPtr->write(buf, bufLen);
        }
    } else if (config_.runMode == RunMode_Client) {
        if (config_.noServerMode && !nodeMgrPtr_->findNode(srcMac)) {
            nodeMgrPtr_->addNode(&serverAddr_, srcMac);
        }
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
    if (config_.switchPortInterval) {
        for (int i = 0; i < 4; ++i) {
            pfds[i] = { static_cast<TapLanSocket>(*udpSockPtrArr_[i]), POLLIN, 0 };
        }
    }

    while (config_.isRunning) {
        if (!config_.switchPortInterval) {
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

void TapLan::syncNodeStatus()
{
    if (config_.runMode == RunMode_Server) {
        nodeMgrPtr_->server();
    } else if (config_.runMode == RunMode_Client) {
        nodeMgrPtr_->client();
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
    nodeMgrPtr_->forEach(printNodeStatus, false);
}

void TapLan::showStats()
{
    uint64_t totalSendBytes = 0, totalSendErrs = 0, totalRecvBytes = 0, totalRecvErrs = 0, totalDropped = 0;
    if (config_.switchPortInterval) {
        LOGR("Each UDP:\n");
        for (int i = 0; i < 4; ++i) {
            uint64_t sendBytes = udpSockPtrArr_[i]->getSendBytes(),
                     sendErrs = udpSockPtrArr_[i]->getSendErrors(),
                     recvBytes = udpSockPtrArr_[i]->getRecvBytes(),
                     recvErrs = udpSockPtrArr_[i]->getRecvErrors(),
                     dropped = udpSockPtrArr_[i]->getDropped();

            if (i == 3) {
                LOGR("╙── UDP port %u:\n", udpSockPtrArr_[i]->getBindPort());
                LOGR("    ╟── TX bytes:   %lu\n", sendBytes);
                LOGR("    ╟── TX errors:  %lu\n", sendErrs);
                LOGR("    ╟── RX bytes:   %lu\n", recvBytes);
                LOGR("    ╟── RX errors:  %lu\n", recvErrs);
                LOGR("    ╙── dropped:    %lu\n", dropped);
            } else {
                LOGR("╟── UDP port %u:\n", udpSockPtrArr_[i]->getBindPort());
                LOGR("║   ╟── TX bytes:   %lu\n", sendBytes);
                LOGR("║   ╟── TX errors:  %lu\n", sendErrs);
                LOGR("║   ╟── RX bytes:   %lu\n", recvBytes);
                LOGR("║   ╟── RX errors:  %lu\n", recvErrs);
                LOGR("║   ╙── dropped:    %lu\n", dropped);
            }
            totalSendBytes += sendBytes;
            totalSendErrs += sendErrs;
            totalRecvBytes += recvBytes;
            totalRecvErrs += recvErrs;
            totalDropped += dropped;
        }
    } else {
        totalSendBytes += udpSockPtr_->getSendBytes();
        totalSendErrs += udpSockPtr_->getSendErrors();
        totalRecvBytes += udpSockPtr_->getRecvBytes();
        totalRecvErrs += udpSockPtr_->getRecvErrors();
        totalDropped += udpSockPtr_->getDropped();
    }

    LOGR("\n");

    LOGR("Total UDP\n");
    LOGR("╟── TX bytes:       %lu\n", totalSendBytes);
    LOGR("╟── TX errors:      %lu\n", totalSendErrs);
    LOGR("╟── RX bytes:       %lu\n", totalRecvBytes);
    LOGR("╟── RX errors:      %lu\n", totalRecvErrs);
    LOGR("╙── dropped:        %lu\n", totalDropped);

    LOGR("\n");

    LOGR("Total TAP:\n");
    LOGR("╟── write bytes:    %lu\n", TapDevPtr->getWriteBytes());
    LOGR("╟── write errors:   %lu\n", TapDevPtr->getWriteErrs());
    LOGR("╟── read  bytes:    %lu\n", TapDevPtr->getReadBytes());
    LOGR("╙── read  errors:   %lu\n", TapDevPtr->getReadErrs());
}

bool TapLan::run()
{
    if (!config_.isRunning)
        return false;

    if (config_.isIoUringEnable) {
#ifdef      _WIN32
        sendThread_ = std::thread(&TapLan::readTapData, this);
        recvThread_ = std::thread(&TapLan::recvSockData, this);
#else
        sendThread_ = std::thread(&TapLan::uring_read_tap_wrk, this);
        recvThread_ = std::thread(&TapLan::uring_recv_udp_wrk, this);
#endif
    } else {
        sendThread_ = std::thread(&TapLan::readTapData, this);
        recvThread_ = std::thread(&TapLan::recvSockData, this);
    }

    pthread_setname_np(sendThread_.native_handle(), sendThreadName_);
    pthread_setname_np(recvThread_.native_handle(), recvThreadName_);

    if (!config_.noServerMode) {
        syncThread_ = std::thread(&TapLan::syncNodeStatus, this);
        pthread_setname_np(syncThread_.native_handle(), syncThreadName_);
    }

    return true;
}

bool TapLan::stop()
{
    if (!config_.isRunning)
        return false;

    config_.isRunning = false;
    if (sendThread_.joinable())
        sendThread_.join();
    if (recvThread_.joinable())
        recvThread_.join();
    if (syncThread_.joinable())
        syncThread_.join();

    return true;
}

#ifdef      __linux__
void TapLan::prep_tap_read(uint32_t buf_id) {
    uring_send_msg *msg = (uring_send_msg *)(read_bufs + (buf_id * TAP_BUF_SIZE));
    io_uring_sqe* sqe = io_uring_get_sqe(&tap_uring);
    io_uring_prep_read_fixed(sqe, tap_fd, msg->data, TAP_BUF_SIZE - MSG_HDR_SIZE, 0, buf_id);
    sqe->user_data = (TOKEN_TAP_READ << 16) | buf_id;
};

void TapLan::handle_tap_read(io_uring_cqe *cqe)
{
    uint16_t buf_id = (uint16_t)cqe->user_data;
    if (cqe->res <= 0) {
        if (cqe->res != -EAGAIN && cqe->res != 0) {
            TapDevPtr->incReadErrs(1);
            LOGE(TAG, "handle tap read failed.[%s]", strerror(-cqe->res));
        }
        prep_tap_read(buf_id);
        return ;
    }

    TapDevPtr->incReadBytes(cqe->res);
    UdpSocket* udpSockPtr = getUdpSockPtr();
    TapLanSocket udp_fd = static_cast<TapLanSocket>(*udpSockPtr);
    uring_send_msg *msg = (uring_send_msg *)(read_bufs + (buf_id * TAP_BUF_SIZE));
    if (config_.runMode == RunMode_Server) {
        EtherHeader& eh = reinterpret_cast<EtherHeader&>(*msg->data);
        Mac& dstMac = reinterpret_cast<Mac&>(eh.dst);
        Mac& srcMac = reinterpret_cast<Mac&>(eh.src);

        bool needBroadcast = eh.dst[0] & 0x01;
        if (!needBroadcast) {
            std::shared_ptr<Node> n = nodeMgrPtr_->findNode(dstMac);
            if (n) {
                msg->nums_of_addr = 1;
                sockaddr_in6& addr = msg->addrs[0];
                memset(&addr, 0, sizeof(sockaddr_in6));
                addr.sin6_family = AF_INET6;
                memcpy(&addr.sin6_addr, &n->ipv6Addr, sizeof(in6_addr));
                addr.sin6_port = n->ipv6Port;

                io_uring_sqe *sqe = io_uring_get_sqe(&tap_uring);
                io_uring_prep_sendto(sqe, udp_fd, &msg->data, cqe->res, 0, (sockaddr*)&addr, sizeof(sockaddr_in6));
                sqe->user_data = (TOKEN_UDP_SEND << 16) | buf_id;
            } else {
                prep_tap_read(buf_id);
            }
        } else {
            msg->nums_of_addr = 0;
            nodeMgrPtr_->forEach([&](uint64_t m, std::shared_ptr<Node> n) {
                if (n->status == NODE_OFFLINE || n->mac == srcMac)
                    return ;

                sockaddr_in6& addr = msg->addrs[msg->nums_of_addr++];
                memset(&addr, 0, sizeof(sockaddr_in6));
                addr.sin6_family = AF_INET6;
                memcpy(&addr.sin6_addr, &n->ipv6Addr, sizeof(in6_addr));
                addr.sin6_port = n->ipv6Port;

                io_uring_sqe *sqe = io_uring_get_sqe(&tap_uring);
                io_uring_prep_sendto(sqe, udp_fd, &msg->data, cqe->res, 0, (sockaddr*)&addr, sizeof(sockaddr_in6));
                sqe->user_data = (TOKEN_UDP_SEND << 16) | buf_id;
            }, false);

            if (msg->nums_of_addr == 0) {
                udpSockPtr->incDropped(1);
                prep_tap_read(buf_id);
            }
        }
    } else if (config_.runMode == RunMode_Client) {
        msg->nums_of_addr = 1;
        sockaddr_in6& addr = msg->addrs[0];
        memcpy(&addr, &serverAddr_, sizeof(sockaddr_in6));

        io_uring_sqe *sqe = io_uring_get_sqe(&tap_uring);
        io_uring_prep_sendto(sqe, udp_fd, &msg->data, cqe->res, 0, (sockaddr*)&serverAddr_, sizeof(sockaddr_in6));
        sqe->user_data = (TOKEN_UDP_SEND << 16) | buf_id;
    } else {
        // RunMode_None
    }

    return ;
}

void TapLan::uring_read_tap_wrk()
{
    int ret;
    TapLanSocket udp_fd = static_cast<TapLanSocket>(*udpSockPtr_);
    io_uring_params params{};
    params.flags = IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_DEFER_TASKRUN;
    ret = io_uring_queue_init_params(QD, &tap_uring, &params);
    if (ret < 0) {
        LOGF(TAG, "TAP queue init params failed.[%s]", strerror(-ret));
        config_.isRunning = false;
        return ;
    }

    read_bufs = (uint8_t *)mmap(NULL, TAP_BUF_NUM * TAP_BUF_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
    if (read_bufs == MAP_FAILED) {
        posix_memalign((void **)&read_bufs, 4096, TAP_BUF_NUM * TAP_BUF_SIZE);
    }

    iovec iovs[TAP_BUF_NUM];
    for (size_t i = 0; i < TAP_BUF_NUM; ++i) {
        iovs[i].iov_base = read_bufs + i * TAP_BUF_SIZE;
        iovs[i].iov_len = TAP_BUF_SIZE;
    }
    ret = io_uring_register_buffers(&tap_uring, iovs, TAP_BUF_NUM);
    if (ret < 0) {
        LOGF(TAG, "TAP register buffers failed.[%s]", strerror(-ret));
        config_.isRunning = false;
        return ;
    }

    for (size_t idx = 0; idx < TAP_BUF_NUM; ++idx) {
        prep_tap_read(idx);
    }
    io_uring_submit(&tap_uring);

    auto handle_udp_send = [&](io_uring_cqe *cqe) {
        UdpSocket* udpSockPtr = getUdpSockPtr();
        uint32_t buf_id = (uint16_t)cqe->user_data;
        if (cqe->res < 0) {
            udpSockPtr->incSendErrs(1);
            LOGE(TAG, "TAP handle udp send failed.[%s]", strerror(-cqe->res));
        }

        udpSockPtr->incSendBytes(cqe->res);
        uring_send_msg *msg = (uring_send_msg *)(read_bufs + (buf_id * TAP_BUF_SIZE));
        if (!--msg->nums_of_addr)
            prep_tap_read(buf_id);
    };

    io_uring_cqe *cqe;
    __kernel_timespec timeout{3, 0};
    while (config_.isRunning) {
        ret = io_uring_wait_cqe_timeout(&tap_uring, &cqe, &timeout);
        if (ret < 0 && ret != -ETIME) {
            LOGF(TAG, "TAP wait cqe failed.[%s]", strerror(-ret));
            break;
        }

        unsigned head;
        unsigned cqe_count = 0;
        io_uring_for_each_cqe(&tap_uring, head, cqe) {
            ++cqe_count;

            switch (cqe->user_data >> 16)
            {
            case TOKEN_UDP_RECV:
                break;

            case TOKEN_TAP_READ:
                handle_tap_read(cqe);
                break;

            case TOKEN_TAP_WRITE:
                break;

            case TOKEN_UDP_SEND:
                handle_udp_send(cqe);
                break;
            
            default:
                break;
            }
        }

        io_uring_cq_advance(&tap_uring, cqe_count);
        io_uring_submit(&tap_uring);
    }

    io_uring_queue_exit(&tap_uring);
    config_.isRunning = false;
    std::cout << "Thread " << sendThreadName_ << " has exited." << std::endl;
    return ;
}

void TapLan::prep_udp_recv() {
    TapLanSocket udp_fd = static_cast<TapLanSocket>(*udpSockPtr_);
    io_uring_sqe* sqe = io_uring_get_sqe(&udp_uring);
    io_uring_prep_recv_multishot(sqe, udp_fd, nullptr, 0, 0);
    sqe->flags |= IOSQE_BUFFER_SELECT;
    sqe->buf_group = UDP_BUF_GRP_ID;
    sqe->user_data = (TOKEN_UDP_RECV << 16);
}

int TapLan::handle_udp_recv(io_uring_cqe *cqe) {
    UdpSocket* udpSockPtr = getUdpSockPtr();
    TapLanSocket udp_fd = static_cast<TapLanSocket>(*udpSockPtr);

    if (!(cqe->flags & IORING_CQE_F_MORE)) {
        LOGE(TAG, "UDP recvmsg multishot stop.[%d]", strerror(-cqe->res));
        prep_udp_recv();
    }

    if (cqe->res < 0) {
        udpSockPtr->incRecvErrs(1);
        LOGE(TAG, "UDP handle udp recv failed.[%s]", strerror(-cqe->res));
        return 1;
    } else if (cqe->res == 0) {
        udpSockPtr->incRecvErrs(1);
        return 1;
    } else {
        udpSockPtr->incRecvBytes(cqe->res);
    }

    uint32_t buf_id = cqe->flags >> IORING_CQE_BUFFER_SHIFT;
    uring_send_msg *msg = (uring_send_msg *)(recv_bufs + (buf_id * UDP_BUF_SIZE));
    EtherHeader eh = reinterpret_cast<EtherHeader&>(*msg->data);
    Mac& dstMac = reinterpret_cast<Mac&>(eh.dst);
    Mac& srcMac = reinterpret_cast<Mac&>(eh.src);
    bool needBroadcast = eh.dst[0] & 0x01;
    bool isSendToMe = needBroadcast || (dstMac == config_.mac);
    if (config_.runMode == RunMode_Server) {
        if (needBroadcast) {        // broadcast
            msg->nums_of_addr = 0;
            nodeMgrPtr_->forEach([&](uint64_t m, std::shared_ptr<Node> n) {
                if (n->status == NODE_OFFLINE || n->mac == srcMac || n->mac == config_.mac)
                    return ;

                sockaddr_in6& addr = msg->addrs[msg->nums_of_addr++];
                memset(&addr, 0, sizeof(sockaddr_in6));
                addr.sin6_family = AF_INET6;
                memcpy(&addr.sin6_addr, &n->ipv6Addr, sizeof(in6_addr));
                addr.sin6_port = n->ipv6Port;

                io_uring_sqe *sqe = io_uring_get_sqe(&udp_uring);
                io_uring_prep_sendto(sqe, udp_fd, &msg->data, cqe->res, 0, (sockaddr*)&addr, sizeof(sockaddr_in6));
                sqe->user_data = (TOKEN_UDP_SEND << 16) | buf_id;
            }, false);

            ++msg->nums_of_addr;
            io_uring_sqe *sqe = io_uring_get_sqe(&udp_uring);
            io_uring_prep_write_fixed(sqe, tap_fd, msg->data, cqe->res, 0, buf_id);
            sqe->user_data = (TOKEN_TAP_WRITE << 16) | buf_id;
        } else if (!isSendToMe) {   // not broadcast && not send to me
            std::shared_ptr<Node> n = nodeMgrPtr_->findNode(dstMac);
            if (!n) {
                udpSockPtr->incDropped(1);
                return 1;
            }
            msg->nums_of_addr = 1;
            sockaddr_in6& addr = msg->addrs[0];
            memset(&addr, 0, sizeof(sockaddr_in6));
            addr.sin6_family = AF_INET6;
            memcpy(&addr.sin6_addr, &n->ipv6Addr, sizeof(in6_addr));
            addr.sin6_port = n->ipv6Port;
            
            io_uring_sqe *sqe = io_uring_get_sqe(&udp_uring);
            io_uring_prep_sendto(sqe, udp_fd, &msg->data, cqe->res, 0, (sockaddr*)&addr, sizeof(sockaddr_in6));
            sqe->user_data = (TOKEN_UDP_SEND << 16) | buf_id;
        } else {                    // not broadcast && send to me
            msg->nums_of_addr = 1;
            io_uring_sqe *sqe = io_uring_get_sqe(&udp_uring);
            io_uring_prep_write_fixed(sqe, tap_fd, msg->data, cqe->res, 0, buf_id);
            sqe->user_data = (TOKEN_TAP_WRITE << 16) | buf_id;
        }
    } else if (config_.runMode == RunMode_Client) {
        if (isSendToMe) {
            msg->nums_of_addr = 1;
            io_uring_sqe *sqe = io_uring_get_sqe(&udp_uring);
            io_uring_prep_write_fixed(sqe, tap_fd, msg->data, cqe->res, 0, buf_id);
            sqe->user_data = (TOKEN_TAP_WRITE << 16) | buf_id;
        } else {
            udpSockPtr->incDropped(1);
            return 1;
        }
    } else {
        // RunMode_None
    }

    return 0;
}

void TapLan::uring_recv_udp_wrk()
{
    int ret;
    TapLanSocket udp_fd = static_cast<TapLanSocket>(*udpSockPtr_);
    io_uring_params params{};
    params.flags = IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_DEFER_TASKRUN;
    ret = io_uring_queue_init_params(QD, &udp_uring, &params);
    if (ret < 0) {
        LOGF(TAG, "UDP queue init params failed.[%s]", strerror(-ret));
        config_.isRunning = false;
        return ;
    }

    io_uring_buf_ring *udp_buf_ring;
    size_t buf_ring_size = UDP_BUF_NUM * sizeof(io_uring_buf);
    posix_memalign((void**)(&udp_buf_ring), 4096, buf_ring_size);

    io_uring_buf_reg bufReg{};
    bufReg.ring_addr = reinterpret_cast<uint64_t>(udp_buf_ring);
    bufReg.ring_entries = UDP_BUF_NUM;
    bufReg.bgid = UDP_BUF_GRP_ID;
    ret = io_uring_register_buf_ring(&udp_uring, &bufReg, 0);
    if (ret < 0) {
        LOGF(TAG, "UDP register buf ring failed.[%s]", strerror(-ret));
        config_.isRunning = false;
        return ;
    }

    recv_bufs = (uint8_t *)mmap(NULL, UDP_BUF_NUM * UDP_BUF_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
    if (recv_bufs == MAP_FAILED) {
        posix_memalign((void **)&recv_bufs, 4096, UDP_BUF_NUM * UDP_BUF_SIZE);
    }

    iovec iovs[UDP_BUF_NUM];
    for (size_t i = 0; i < UDP_BUF_NUM; ++i) {
        iovs[i].iov_base = recv_bufs + i * UDP_BUF_SIZE;
        iovs[i].iov_len = UDP_BUF_SIZE;
    }
    ret = io_uring_register_buffers(&udp_uring, iovs, UDP_BUF_NUM);
    if (ret < 0) {
        LOGF(TAG, "UDP register udp buffers failed.[%s]", strerror(-ret));
        config_.isRunning = false;
        return ;
    }

    io_uring_buf_ring_init(udp_buf_ring);
    int buf_ring_mask = io_uring_buf_ring_mask(UDP_BUF_NUM);
    for (size_t i = 0; i < UDP_BUF_NUM; ++i) {
        io_uring_buf_ring_add(udp_buf_ring, recv_bufs + (i * UDP_BUF_SIZE) + MSG_HDR_SIZE,
                                UDP_BUF_SIZE - MSG_HDR_SIZE, i, buf_ring_mask, i);
    }
    io_uring_buf_ring_advance(udp_buf_ring, UDP_BUF_NUM);

    prep_udp_recv();
    io_uring_submit(&udp_uring);

    auto handle_tap_write = [&](io_uring_cqe *cqe) {
        if (cqe->res < 0) {
            TapDevPtr->incWriteErrs(1);
            LOGE(TAG, "UDP handle tap write failed.[%s]", strerror(-cqe->res));
        }

        TapDevPtr->incWriteBytes(cqe->res);
        uint16_t buf_id = cqe->user_data;
        uring_send_msg *msg = (uring_send_msg *)(recv_bufs + (buf_id * UDP_BUF_SIZE));
        if (!--msg->nums_of_addr)
            return 1;
        return 0;
    };

    auto handle_udp_send = [&](io_uring_cqe *cqe) {
        UdpSocket* udpSockPtr = getUdpSockPtr();
        if (cqe->res < 0) {
            udpSockPtr->incSendErrs(1);
            LOGE(TAG, "UDP handle udp send failed.[%s]", strerror(-cqe->res));
        }

        udpSockPtr->incSendBytes(cqe->res);
        uint16_t buf_id = cqe->user_data;
        uring_send_msg *msg = (uring_send_msg *)(recv_bufs + (buf_id * UDP_BUF_SIZE));
        if (!--msg->nums_of_addr)
            return 1;
        return 0;
    };

    io_uring_cqe *cqe;
    __kernel_timespec timeout{3, 0};
    while (config_.isRunning) {
        ret = io_uring_wait_cqe_timeout(&udp_uring, &cqe, &timeout);
        if (ret < 0 && ret != -ETIME) {
            LOGF(TAG, "UDP wait cqe failed.[%s]", strerror(-ret));
            break;
        }

        unsigned head;
        unsigned cqe_count = 0;
        unsigned ring_count = 0;
        io_uring_for_each_cqe(&udp_uring, head, cqe) {
            cqe_count++;

            unsigned prev_ring_count = ring_count;
            uint16_t buf_id;
            switch (cqe->user_data >> 16)
            {
            case TOKEN_UDP_RECV:
                ring_count += handle_udp_recv(cqe);
                buf_id = cqe->flags >> IORING_CQE_BUFFER_SHIFT;
                break;

            case TOKEN_TAP_READ:
                break;

            case TOKEN_TAP_WRITE:
                ring_count += handle_tap_write(cqe);
                buf_id = cqe->user_data;
                break;

            case TOKEN_UDP_SEND:
                ring_count += handle_udp_send(cqe);
                buf_id = cqe->user_data;
                break;

            default:
                break;
            }

            if (prev_ring_count != ring_count) {
                io_uring_buf_ring_add(udp_buf_ring, recv_bufs + (buf_id * UDP_BUF_SIZE) + MSG_HDR_SIZE,
                                        UDP_BUF_SIZE - MSG_HDR_SIZE, buf_id, buf_ring_mask, prev_ring_count);
            }
        }

        io_uring_cq_advance(&udp_uring, cqe_count);
        if (ring_count)
            io_uring_buf_ring_advance(udp_buf_ring, ring_count);
        io_uring_submit(&udp_uring);
    }

    io_uring_queue_exit(&udp_uring);
    config_.isRunning = false;
    std::cout << "Thread " << recvThreadName_ << " has exited." << std::endl;
    return ;
}

#endif
