#pragma     once
#include    <cstdint>
#include    <ctime>
#include    <pthread.h>
#include    <liburing.h>
#include    "LogMgr.hpp"
#include    "NodeMgr.hpp"
#include    "Socket.hpp"
#include    "TapDev.hpp"

enum RunModeT{
    RunMode_None = 0,
    RunMode_Server,
    RunMode_Client,
};

enum {
    TOKEN_UDP_RECV  = 1,
    TOKEN_TAP_READ  = 2,
    TOKEN_TAP_WRITE = 3,
    TOKEN_UDP_SEND  = 4,
};

struct ConfigDataT {
    uint8_t     runMode;
    uint16_t    localPort;
    uint32_t    netNum;
    uint8_t     netNumLen;
    in6_addr    remoteAddr;
    uint16_t    remotePort;
    bool        isMultiPortEnable;
    bool        isIoUringEnable;
    bool        isRunning;
    Mac         mac;

    ConfigDataT(): runMode(RunMode_Server), localPort(3460),
                    netNum((192 << 24) + (168 << 16) + (208 << 8)),
                    netNumLen(24), remoteAddr{}, remotePort(0),
                    isMultiPortEnable(false), isIoUringEnable(false),
                    isRunning(false),
                    mac{} {
        // nothing to do
    }
};

class TapLan {
public:
    static ConfigDataT config_;

    TapLan();
    ~TapLan();
    bool run();
    bool stop();
    void showNodeStatus();
    void showStats();

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

    // for io_uring
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
    const uint32_t UDP_BUF_SIZE = 65536;
    const uint32_t TAP_BUF_NUM = 128;
    const uint32_t TAP_BUF_SIZE = 65536;
    const size_t MSG_HDR_SIZE = sizeof(uring_send_msg);

    io_uring tap_uring;
    uint8_t *read_bufs;
    void prep_tap_read(uint32_t buf_id);
    void handle_tap_read(io_uring_cqe *cqe);
    void uring_read_tap_wrk();

    io_uring udp_uring;
    uint8_t *recv_bufs;
    msghdr dummy_msg_hdr;
    void prep_udp_recv();
    int handle_udp_recv(io_uring_cqe *cqe);
    void uring_recv_udp_wrk();
};
