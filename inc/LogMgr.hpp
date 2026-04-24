#pragma     once
#include    <cstdio>
#include    <cstdarg>
#include    <cstring>
#include    <iostream>
#include    <fstream>
#include    <thread>
#include    <atomic>
#include    <semaphore>
#include    <pthread.h>

#define     LogMgrPtr                   LogMgr::ptr()
#define     LOGR(fmt, ...)              LogMgrPtr->logOutput(LogLevel::raw, "", fmt, ##__VA_ARGS__)
#define     LOGF(TAG, fmt, ...)         LogMgrPtr->logOutput(LogLevel::fatal, TAG, fmt, ##__VA_ARGS__)
#define     LOGE(TAG, fmt, ...)         LogMgrPtr->logOutput(LogLevel::error, TAG, fmt, ##__VA_ARGS__)
#define     LOGW(TAG, fmt, ...)         LogMgrPtr->logOutput(LogLevel::warn, TAG, fmt, ##__VA_ARGS__)
#define     LOGI(TAG, fmt, ...)         LogMgrPtr->logOutput(LogLevel::info, TAG, fmt, ##__VA_ARGS__)
#define     LOGD(TAG, fmt, ...)         LogMgrPtr->logOutput(LogLevel::debug, TAG, fmt, ##__VA_ARGS__)
#define     LOGT(TAG, fmt, ...)         LogMgrPtr->logOutput(LogLevel::trace, TAG, fmt, ##__VA_ARGS__)

enum class LogLevel: char {
    raw = -1,
    fatal = 0,
    error,
    warn,
    info,
    debug,
    trace,
    numsOfLogLevel,
};

class LogMgr {
public:
    bool run();
    bool terminate();
    void setLogLevel(LogLevel level) { logLevel_ = level; };
    bool isRunning() { return running_; };
    void logOutput(LogLevel level, const char* tag, const char* format, ...);
    static LogMgr* ptr();

private:
    enum EntryState: int {
        ENTRY_EMPTY = 0,
        ENTRY_WRITTING,
        ENTRY_READY,
    };

    static constexpr size_t LOG_ENTRY_SIZE = 256;
    static constexpr size_t LOG_RING_SIZE = 8192;
    static constexpr size_t LOG_DATA_SIZE = LOG_ENTRY_SIZE - sizeof(EntryState);
    static constexpr size_t LOG_RING_MASK = LOG_RING_SIZE - 1;
    static constexpr size_t LOG_NOTIFY_THRESHOLD = LOG_RING_SIZE / 4 - 1;
    static constexpr size_t LOG_ENTRY_RING_SIZE = LOG_ENTRY_SIZE * LOG_RING_SIZE;
    static constexpr const char* logLevelStr[static_cast<size_t>(LogLevel::numsOfLogLevel)]  = {
        "[FATAL]", "[ERROR]", "[WARN]", "[INFO]", "[DEBUG]", "[TRACE]"
    };
    static constexpr char logThreadName_[] = "logWrk";

    struct LogEntry {
        std::atomic<EntryState> state;
        char data[LOG_DATA_SIZE];
    };

    std::ofstream logFile_;
    LogLevel logLevel_;
    std::thread logThread_;
    bool running_;

    LogEntry* logEntryRing_;
    std::counting_semaphore<1> logSem_{0};
    std::atomic<size_t> rIdx_;
    std::atomic<size_t> wIdx_;

    LogMgr();
    ~LogMgr();
    int initLogMgr();
    void clearLog();
    void logWrk();
};

inline LogMgr* LogMgr::ptr() {
    static LogMgr ins;

    return &ins;
}

inline void cpuRelax()
{
#if defined(_MSC_VER)
    _mm_pause();
#elif defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
    __asm__ volatile("yield");
#else
    std::this_thread::yield();  // fallback
#endif
}
