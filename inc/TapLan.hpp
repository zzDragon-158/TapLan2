#pragma     once
#include    <cstdint>
#include    <ctime>
#include    <pthread.h>
#include    "LogMgr.hpp"
#include    "NodeMgr.hpp"
#include    "Socket.hpp"
#include    "TapDev.hpp"

class IOPool {
private:
    const size_t IOURING_SIZE = 512;
    uint8_t* dataBufs_;
    std::vector<IOContext*> ioCtxs_;
    std::stack<IOContext*> freeStack_;

public:
    io_uring iouring_;
    IOPool() {
        int ret;

        io_uring_params params{};
        params.flags = IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_DEFER_TASKRUN;
        ret = io_uring_queue_init_params(IOURING_SIZE, &iouring_, &params);
        if (ret < 0) {
            // LOGF(TAG, "Failed to init queue params.[%s]", strerror(-ret));
            g_cfgData.isRunning = false;
            return ;
        }

        dataBufs_ = (uint8_t*)mmap(NULL, DATA_BUF_NUM * DATA_BUF_SIZE,
                                   PROT_READ | PROT_WRITE,
                                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
        if (dataBufs_ == MAP_FAILED) {
            // LOGD(TAG, "Cant allocate hugepage memory.");
            posix_memalign((void **)&dataBufs_, 4096, DATA_BUF_NUM * DATA_BUF_SIZE);
        }

        // iovec iovs[DATA_BUF_NUM];
        for (size_t i = 0; i < DATA_BUF_NUM; ++i) {
            IOContext* ctx = new IOContext();

            ctx->owner = this;
            ctx->ring = &iouring_;
            // ctx->bufId = i;
            ctx->buf = dataBufs_ + i * DATA_BUF_SIZE;
            ctx->msgHdr.msg_name = &ctx->addr;
            ctx->msgHdr.msg_namelen = sizeof(ctx->addr);
            ctx->msgHdr.msg_iov = &ctx->iov;
            ctx->msgHdr.msg_iovlen = 1;
            ctx->iov.iov_base = ctx->buf;
            ctx->iov.iov_len = DATA_BUF_SIZE;

            // iovs[i].iov_base = ctx->buf;
            // iovs[i].iov_len = DATA_BUF_SIZE;

            ioCtxs_.push_back(ctx);
            freeStack_.push(ctx);
        }
        // ret = io_uring_register_buffers(&iouring_, iovs, DATA_BUF_NUM);
        // if (ret < 0) {
        //     LOGF(TAG, "Failed to register buffers.[%s]", strerror(-ret));
        //     g_cfgData.isRunning = false;
        // }
        io_uring_submit(&iouring_);
    }

    IOContext* acquire() {
        if (freeStack_.empty())
            return nullptr;

        IOContext* ctx = freeStack_.top();
        freeStack_.pop();
        return ctx;
    }

    void release(IOContext* ctx) {
        freeStack_.push(ctx);
    }

    ~IOPool() {
        for (auto ctx: ioCtxs_) delete ctx;
    }
};

class TapLan {
public:
    TapLan();
    ~TapLan();
    bool run();
    bool stop();
    void showNodeStatus();
    void showStats();
    UdpSocket* getUdpSockPtr();

private:
    sockaddr_in6    serverAddr_;
    UdpSocket*      udpSockPtr_;
    UdpSocket*      udpSockPtrArr_[4];
    std::shared_ptr<NodeMgr>    nodeMgrPtr_;
    const char      *recvThreadName_, *sendThreadName_, *syncThreadName_;
    std::thread     recvThread_, sendThread_, syncThread_;

    void initUdpSockPtr();
    void handleSockData(uint8_t* buf, size_t bufLen, sockaddr_in6& srcAddr);
    void recvSockData();
    void handleTapData(uint8_t* buf, size_t bufLen);
    void readTapData();
    void syncNodeStatus();

#ifdef      _WIN32
    HANDLE hIOCP_;
    IOPool* readBufs_;
    IOPool* recvBufs_;
    IOPool* ioBufs_;
    std::thread iocpWrkThread;

    int reqTapRead(IOContext* ctx);
    int reqTapWrite(IOContext* ctx);
    int reqUdpRecv(IOContext* ctx);
    int reqUdpSend(IOContext* ctx);
    void handleTapRead(IOContext* ctx);
    void handleTapWrite(IOContext* ctx);
    void handleUdpRecv(IOContext* ctx);
    void handleUdpSend(IOContext* ctx);
    void iocpWrk();

#elif       __linux__
#if 0
    // for io_uring
    union uring_userdata {
        // TODO: use this union to reinterprete [cqe->userdate]
        uint64_t userdata;
        struct {
            uint8_t     op;
            uint8_t     port_offset;
            uint16_t    buf_id;
            uint32_t    reserved;
        };
    };
    struct uring_send_msg_hdr {
        msghdr hdr;
        iovec iov;
        sockaddr_in6 addr;
    };
    struct uring_send_msg {
        uint64_t nums_of_addr;
        sockaddr_in6 addrs[254];
        alignas(16) uint8_t data[];
    };
    const uint32_t QD = 256;
    const uint32_t UDP_BUF_GRP_ID = 1;
    const uint32_t UDP_BUF_NUM = 128;
    const uint32_t UDP_BUF_SIZE = 16384;
    const uint32_t TAP_BUF_NUM = 128;
    const uint32_t TAP_BUF_SIZE = 16384;
    const size_t MSG_HDR_SIZE = sizeof(uring_send_msg);

    io_uring tap_uring;
    uint8_t *read_bufs;
    void prep_tap_read(uint32_t buf_id);
    void handle_tap_read(io_uring_cqe *cqe);
    void uring_read_tap_wrk();

    io_uring udp_uring;
    uint8_t *recv_bufs;
    void prep_udp_recv();
    int handle_udp_recv(io_uring_cqe *cqe);
    void uring_recv_udp_wrk();
#endif
    // refactor
    IOPool* ioPool_;

    int reqTapRead();
    int reqTapWrite(IOContext* ctx);
    int reqUdpRecv();
    int reqUdpSend(IOContext* ctx);
    void handleTapRead(IOContext* ctx);
    void handleTapWrite(IOContext* ctx);
    void handleUdpRecv(IOContext* ctx);
    void handleUdpSend(IOContext* ctx);
    void iouringWrk();
#endif
};
