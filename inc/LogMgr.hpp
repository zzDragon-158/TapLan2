#pragma     once
#include    <cstdio>
#include    <cstdarg>
#include    <cstring>
#include    <iostream>
#include    <fstream>
#include    <queue>
#include    <mutex>
#include    <condition_variable>
#include    <thread>
#include    <sstream>
#include    <atomic>
#include    <pthread.h>

#define     LogMgrPtr                   LogMgr::ptr()
#define     LOGR(fmt, ...)              LogMgrPtr->logOutput(fmt, ##__VA_ARGS__)
#define     LOGF(TAG, fmt, ...)         LogMgrPtr->logOutput(LOG_FATAL, TAG, fmt, ##__VA_ARGS__)
#define     LOGE(TAG, fmt, ...)         LogMgrPtr->logOutput(LOG_ERROR, TAG, fmt, ##__VA_ARGS__)
#define     LOGW(TAG, fmt, ...)         LogMgrPtr->logOutput(LOG_WARN, TAG, fmt, ##__VA_ARGS__)
#define     LOGI(TAG, fmt, ...)         LogMgrPtr->logOutput(LOG_INFO, TAG, fmt, ##__VA_ARGS__)
#define     LOGD(TAG, fmt, ...)         LogMgrPtr->logOutput(LOG_DEBUG, TAG, fmt, ##__VA_ARGS__)
#define     LOGT(TAG, fmt, ...)         LogMgrPtr->logOutput(LOG_TRACE, TAG, fmt, ##__VA_ARGS__)

enum LogLevel {
    LOG_FATAL = 0,
    LOG_ERROR,
    LOG_WARN,
    LOG_INFO,
    LOG_DEBUG,
    LOG_TRACE,
    NUMS_OF_LEVEL,
};

class LogMgr {
public:
    struct LogEntry {
        std::atomic<int> state;
        char data[2044];
    };

    enum EntryState {
        ENTRY_EMPTY = 0,
        ENTRY_WRITTING,
        ENTRY_READY,
    };

    bool run();
    bool terminate();
    void setLogLevel(char level) { logLevel_ = level; };
    bool isRunning() { return running_; };
    void logOutput(const char* format, ...);
    void logOutput(char level, const char* tag, const char* format, ...);
    static LogMgr* ptr();

private:
    static constexpr size_t LOG_BUF_SIZE = 2048;
    static constexpr size_t LOG_BUF_NUM = 1024;
    static constexpr size_t LOG_DATA_SIZE = 2044;
    static constexpr size_t LOG_BUF_RING_MASK = LOG_BUF_NUM - 1;
    static constexpr size_t TOTAL_LOG_BUF_SIZE = LOG_BUF_SIZE * LOG_BUF_NUM;
    static constexpr const char* logLevelStr[NUMS_OF_LEVEL] = { "[FATAL]", "[ERROR]", "[WARN]", "[INFO]", "[DEBUG]", "[TRACE]" };
    static constexpr char logThreadName_[] = "logWrk";
    std::ofstream logFile_;
    char logLevel_;
    std::queue<std::string> logQueue_;
    std::mutex logMutex_;
    std::condition_variable logCv_;
    std::thread logThread_;
    bool running_;

    LogEntry* logEntryRing_;
    std::atomic<size_t> rIdx_;
    std::atomic<size_t> wIdx_;

    LogMgr();
    ~LogMgr();
    int initLogMgr();
    void logWrk();
};

inline LogMgr* LogMgr::ptr() {
    static LogMgr ins;

    return &ins;
}
