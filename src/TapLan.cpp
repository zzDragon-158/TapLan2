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

        config_.isRunning = udpSockPtr_->isFdValid() && TapDevPtr->open();

        if (config_.isRunning) {
            TapDevPtr->getMacAddr(config_.mac.addr, sizeof(Mac));
            std::shared_ptr<Node> n = nodeMgrPtr_->addNode(&serverAddr_, config_.mac);
            TapDevPtr->setIpv4Addr(&n->ipv4Addr, config_.netNumLen);
        }
    } else if (config_.runMode == RunMode_Client) {
        LOGI(TAG, "We are running in client mode.");

        nodeMgrPtr_ = std::make_shared<NodeMgr>();

        serverAddr_.sin6_family = AF_INET6;
        memcpy(&serverAddr_.sin6_addr, &config_.remoteAddr, sizeof(in6_addr));
        serverAddr_.sin6_port = config_.remotePort;

        initUdpSockPtr();

        config_.isRunning = udpSockPtr_->isFdValid() && TapDevPtr->open();

        if (config_.isRunning) {
            TapDevPtr->getMacAddr(config_.mac.addr, sizeof(Mac));
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

void TapLan::handleTapData(uint8_t* buf, size_t bufLen)
{
    sockaddr_in6 dstAddr;
    memset(&dstAddr, 0, sizeof(dstAddr));
    dstAddr.sin6_family = AF_INET6;

    std::time_t now = std::time(nullptr);
    uint16_t portOffset = (now / 60 % 60) % 4;

    if (config_.runMode == RunMode_Server) {
        EtherHeader& eh = reinterpret_cast<EtherHeader&>(*buf);
        Mac& dstMac = reinterpret_cast<Mac&>(eh.dst);
        Mac& srcMac = reinterpret_cast<Mac&>(eh.src);

        bool needBroadcast = eh.dst[0] & 0x01;
        if (!needBroadcast) {
            std::shared_ptr<Node> n = nodeMgrPtr_->findNode(reinterpret_cast<Mac&>(*eh.dst));
            memcpy(&dstAddr.sin6_addr, &n->ipv6Addr, sizeof(dstAddr.sin6_addr));
            dstAddr.sin6_port = n->ipv6Port;
            (config_.isMultiPortEnable? udpSockPtrArr_[portOffset]: udpSockPtr_)->sendTo(buf, bufLen, (const sockaddr*)&dstAddr, sizeof(dstAddr));

            return ;
        }

        auto broadcast = [&](uint64_t m, std::shared_ptr<Node> n) {
            if (n->status == NODE_OFFLINE || n->mac == srcMac)
                return ;

            memcpy(&dstAddr.sin6_addr, &n->ipv6Addr, sizeof(in6_addr));
            dstAddr.sin6_port = n->ipv6Port;
            (config_.isMultiPortEnable? udpSockPtrArr_[portOffset]: udpSockPtr_)->sendTo(buf, bufLen, (sockaddr*)&dstAddr, sizeof(dstAddr));
        };
        nodeMgrPtr_->forEach(broadcast, false);
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

    while (config_.isRunning) {
        ssize_t readBytes = TapDevPtr->read(tapRxBuf, sizeof(tapRxBuf), 3000);
        if (readBytes <= ETHERNET_HEADER_LEN) {
            continue;
        }

        handleTapData(tapRxBuf, readBytes);
    }

    std::cout << "Thread " << sendThreadName_ << " has exited." << std::endl;
}

void TapLan::handleSockData(uint8_t* buf, size_t bufLen, sockaddr_in6& srcAddr)
{
    std::time_t now = std::time(nullptr);
    uint16_t portOffset = (now / 60 % 60) % 4;

    if (config_.runMode == RunMode_Server) {
        sockaddr_in6 dstAddr{};
        dstAddr.sin6_family = AF_INET6;
        EtherHeader eh = reinterpret_cast<EtherHeader&>(*buf);
        Mac& srcMac = reinterpret_cast<Mac&>(eh.src);
        Mac& dstMac = reinterpret_cast<Mac&>(eh.dst);
        bool needBroadcast = eh.dst[0] & 0x01;
        bool isSendToMe = needBroadcast || (dstMac == config_.mac);

        if (needBroadcast) {        // broadcast
            auto broadcast = [&](uint64_t m, std::shared_ptr<Node> n) {
                if (n->status == NODE_OFFLINE || n->mac == srcMac || n->mac == config_.mac)
                    return ;

                memcpy(&dstAddr.sin6_addr, &n->ipv6Addr, sizeof(in6_addr));
                dstAddr.sin6_port = n->ipv6Port;
                (config_.isMultiPortEnable? udpSockPtrArr_[portOffset]: udpSockPtr_)->sendTo(buf, bufLen, (sockaddr*)&dstAddr, sizeof(dstAddr));
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

    while (config_.isRunning) {
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
    if (!config_.isRunning)
        return false;

    // sendThread_ = std::thread(&TapLan::uring_tap_wrk, this);
    // recvThread_ = std::thread(&TapLan::uring_udp_wrk, this);
    sendThread_ = std::thread(&TapLan::readTapData, this);
    recvThread_ = std::thread(&TapLan::recvSockData, this);

    pthread_setname_np(sendThread_.native_handle(), sendThreadName_);
    pthread_setname_np(recvThread_.native_handle(), recvThreadName_);

    syncThread_ = std::thread(&TapLan::syncNodeStatus, this);
    pthread_setname_np(syncThread_.native_handle(), syncThreadName_);

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

void TapLan::uring_tap_wrk()
{
    TapLanSocket udp_fd = static_cast<TapLanSocket>(*udpSockPtr_);
    io_uring tap_uring;
    io_uring_params params{};
    params.flags = IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_DEFER_TASKRUN;
    if (io_uring_queue_init_params(QD, &tap_uring, &params) < 0) {
        perror("tap_uring init");
        config_.isRunning = false;
        return ;
    }

    uint8_t *read_bufs;
    posix_memalign((void **)&read_bufs, 4096, TAP_BUF_COUNT * TAP_BUF_SIZE);

    auto prep_tap_read = [&](uint32_t i) {
        io_uring_sqe* sqe = io_uring_get_sqe(&tap_uring);
        io_uring_prep_read(sqe, tap_fd, read_bufs + (i * TAP_BUF_SIZE), TAP_BUF_SIZE, 0);
        sqe->user_data = (TOKEN_TAP_READ << 16) | i;
    };

    for (size_t idx = 0; idx < TAP_BUF_COUNT; ++idx) {
        prep_tap_read(idx);
    }
    io_uring_submit(&tap_uring);

    auto handle_tap_read = [&](io_uring_cqe *cqe) {
        uint32_t buf_id = (uint16_t)cqe->user_data;
        if (cqe->res <= 0) {
            // fprintf(stderr, "handle_tap_read failed.[%s]", strerror(-cqe->res));
            prep_tap_read(buf_id);
            return ;
        }

        uint8_t *data_ptr = (uint8_t*)read_bufs + (buf_id * TAP_BUF_SIZE);
        uring_send_msg *msg = (uring_send_msg *)(data_ptr + cqe->res);
        std::memset(msg, 0, sizeof(uring_send_msg));
        std::memcpy(&msg->addr, &serverAddr_, sizeof(sockaddr_in6));
        msg->hdr.msg_name = &msg->addr;
        msg->hdr.msg_namelen = sizeof(sockaddr_in6);
        msg->hdr.msg_iov = &msg->io;
        msg->hdr.msg_iovlen = 1;
        msg->io.iov_base = data_ptr;
        msg->io.iov_len = cqe->res;
        io_uring_sqe *sqe = io_uring_get_sqe(&tap_uring);
        io_uring_prep_sendmsg(sqe, udp_fd, &msg->hdr, 0);
        sqe->user_data = (TOKEN_UDP_SEND << 16) | buf_id;

        return ;
    };

    auto handle_udp_send = [&](io_uring_cqe *cqe) {
        uint32_t buf_id = (uint16_t)cqe->user_data;
        if (cqe->res <= 0) {
            if (cqe->res != -EAGAIN)
                fprintf(stderr, "handle_udp_send failed.[%s]", strerror(-cqe->res));
        }
        prep_tap_read(buf_id);
    };

    io_uring_cqe *cqe;
    while (config_.isRunning) {
        int ret = io_uring_wait_cqe(&tap_uring, &cqe);
        if (ret < 0)
            break;

        unsigned head;
        unsigned cqe_count = 0;
        io_uring_for_each_cqe(&tap_uring, head, cqe) {
            ++cqe_count;

            switch (cqe->user_data >> 16)
            {
            case TOKEN_TAP_READ:
                handle_tap_read(cqe);
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
    return ;
}

void TapLan::uring_udp_wrk()
{
    TapLanSocket udp_fd = static_cast<TapLanSocket>(*udpSockPtr_);
    io_uring udp_uring;
    io_uring_params params{};
    params.flags = IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_DEFER_TASKRUN;
    if (io_uring_queue_init_params(QD, &udp_uring, &params) < 0) {
        perror("io_uring_init");
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
    if (io_uring_register_buf_ring(&udp_uring, &bufReg, 0) < 0) {
        fprintf(stderr, "Error registering buf ring\n");
        config_.isRunning = false;
        return ;
    }

    uint8_t *recv_bufs;
    posix_memalign((void **)&recv_bufs, 4096, UDP_BUF_NUM * UDP_BUF_SIZE);
    io_uring_buf_ring_init(udp_buf_ring);
    int buf_ring_mask = io_uring_buf_ring_mask(UDP_BUF_NUM);
    for (size_t i = 0; i < UDP_BUF_NUM; ++i) {
        io_uring_buf_ring_add(udp_buf_ring, (uint8_t *)recv_bufs + (i * UDP_BUF_SIZE), UDP_BUF_SIZE, i, buf_ring_mask, i);
    }
    io_uring_buf_ring_advance(udp_buf_ring, UDP_BUF_NUM);

    static msghdr dummy_msg_hdr{};
    dummy_msg_hdr.msg_namelen = sizeof(sockaddr_in6);
    auto prep_udp_recv = [&]() {
        io_uring_sqe* sqe = io_uring_get_sqe(&udp_uring);
        io_uring_prep_recvmsg_multishot(sqe, udp_fd, &dummy_msg_hdr, 0);
        sqe->flags |= IOSQE_BUFFER_SELECT;
        sqe->buf_group = UDP_BUF_GRP_ID;
        sqe->user_data = (TOKEN_UDP_RECV << 16);
    };

    prep_udp_recv();
    io_uring_submit(&udp_uring);

    auto handle_udp_recv = [&](io_uring_cqe *cqe) {
        if (!(cqe->flags & IORING_CQE_F_MORE)) {
            fprintf(stderr, "multishot stop! [%d]\n", cqe->res);
            prep_udp_recv();
        }

        uint32_t buf_id = cqe->flags >> IORING_CQE_BUFFER_SHIFT;
        if (cqe->res <= 0) {
            fprintf(stderr, "handle_udp_recv failed.[%s]", strerror(-cqe->res));
            return 1;
        }

        uint8_t *data_ptr = recv_bufs + (buf_id * UDP_BUF_SIZE);
        io_uring_recvmsg_out *out = io_uring_recvmsg_validate(data_ptr, cqe->res, &dummy_msg_hdr);
        if (out && out->payloadlen > 0) {
            sockaddr_in6 *client_addr = (struct sockaddr_in6 *)io_uring_recvmsg_name(out);
            uint8_t *payload = (uint8_t *)io_uring_recvmsg_payload(out, &dummy_msg_hdr);

            // TapDevPtr->write(payload, out->payloadlen);

            io_uring_sqe *sqe = io_uring_get_sqe(&udp_uring);
            io_uring_prep_write(sqe, tap_fd, payload, out->payloadlen, 0);
            sqe->user_data = (TOKEN_TAP_WRITE << 16) | buf_id;
        }

        return 0;
    };

    auto handle_tap_write = [&](io_uring_cqe * cqe) {
        if (cqe->res <= 0) {
            fprintf(stderr, "handle_tap_write failed.[%s]", strerror(-cqe->res));
        }

        return 1;
    };

    io_uring_cqe *cqe;
    while (config_.isRunning) {
        int ret = io_uring_wait_cqe(&udp_uring, &cqe);
        if (ret < 0)
            break;

        unsigned head;
        unsigned cqe_count = 0;
        unsigned ring_count = 0;
        io_uring_for_each_cqe(&udp_uring, head, cqe) {
            cqe_count++;

            switch (cqe->user_data >> 16)
            {
            case TOKEN_UDP_RECV:
                ring_count += handle_udp_recv(cqe);
                break;

            case TOKEN_TAP_WRITE:
                ring_count += handle_tap_write(cqe);
                break;

            default:
                break;
            }
        }

        io_uring_cq_advance(&udp_uring, cqe_count);
        if (ring_count)
            io_uring_buf_ring_advance(udp_buf_ring, ring_count);
        io_uring_submit(&udp_uring);
    }

    io_uring_queue_exit(&udp_uring);
    config_.isRunning = false;
    return ;
}
