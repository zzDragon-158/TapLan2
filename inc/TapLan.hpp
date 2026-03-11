#pragma     once
#include    <cstdint>
#include    <ctime>
#include    <pthread.h>
#include    "LogMgr.hpp"
#include    "NodeMgr.hpp"
#include    "Socket.hpp"
#include    "TapDev.hpp"
#include    "AioIntf.hpp"

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
    std::thread     recvThread_, sendThread_, syncThread_, aioWrkThread_;

    void initUdpSockPtr();
    void handleSockData(uint8_t* buf, size_t bufLen, sockaddr_in6& srcAddr);
    void recvSockData();
    void handleTapData(uint8_t* buf, size_t bufLen);
    void readTapData();
    void syncNodeStatus();

    void handleTapRead(AioIntf::Ctx* ctx);
    void handleTapWrite(AioIntf::Ctx* ctx);
    void handleUdpRecv(AioIntf::Ctx* ctx);
    void handleUdpSend(AioIntf::Ctx* ctx);
    void aioWrk();

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
};
