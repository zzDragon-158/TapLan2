#pragma     once
#include    <fstream>
#include    <thread>
#include    <atomic>
#include    <semaphore>

#define     LOGR(fmt, ...)         g_logMgr.logToRing(LogLevel::raw, "", fmt __VA_OPT__(,) __VA_ARGS__)
#define     LOGF(fmt, ...)         g_logMgr.logToRing(LogLevel::fatal, TAG, fmt __VA_OPT__(,) __VA_ARGS__)
#define     LOGE(fmt, ...)         g_logMgr.logToRing(LogLevel::error, TAG, fmt __VA_OPT__(,) __VA_ARGS__)
#define     LOGW(fmt, ...)         g_logMgr.logToRing(LogLevel::warn, TAG, fmt __VA_OPT__(,) __VA_ARGS__)
#define     LOGI(fmt, ...)         g_logMgr.logToRing(LogLevel::info, TAG, fmt __VA_OPT__(,) __VA_ARGS__)
#define     LOGD(fmt, ...)         g_logMgr.logToRing(LogLevel::debug, TAG, fmt __VA_OPT__(,) __VA_ARGS__)
#define     LOGT(fmt, ...)         g_logMgr.logToRing(LogLevel::trace, TAG, fmt __VA_OPT__(,) __VA_ARGS__)

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
    void setLogLevel(LogLevel level) { logLevel_.store(level, std::memory_order_relaxed); };
    bool isRunning() { return running_.load(std::memory_order_acquire); };
    void logOutput(LogLevel level, const char* tag, const char* format, ...);
    template<typename... Args>
    void logToRing(LogLevel level, const char* tag, std::string_view fmt, Args&&... args) {
        doLogToRing(level, tag, fmt, std::make_format_args(args...));
    }
    static LogMgr& instance() {
        static LogMgr ins;
        return ins;
    }

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
    static constexpr const char* logLevelStr[static_cast<size_t>(LogLevel::numsOfLogLevel)]  = {
        "[FATAL]", "[ERROR]", "[WARN]", "[INFO]", "[DEBUG]", "[TRACE]"
    };
    static constexpr char logThreadName_[] = "logWrk";

    struct alignas(LOG_ENTRY_SIZE) LogEntry {
        std::atomic<EntryState> state;
        char data[LOG_DATA_SIZE];
    };
    static_assert(sizeof(LogEntry) == LOG_ENTRY_SIZE);

    std::ofstream logFile_;
    std::atomic<LogLevel> logLevel_{LogLevel::info};
    std::thread logThread_;
    std::atomic<bool> running_{false};

    LogEntry* logEntryRing_ = nullptr;
    std::counting_semaphore<1> logSem_{0};
    std::atomic<bool> notificationPending_{false};
    std::atomic<size_t> rIdx_;
    std::atomic<size_t> wIdx_;
    std::atomic<uint64_t> droppedLogs_{0};

    LogMgr();
    ~LogMgr();
    int initLogMgr();
    LogEntry* acquireLogEntry(size_t& entryIdx);
    void publishLogEntry(LogEntry& entry);
    void doLogToRing(LogLevel level, const char* tag, std::string_view fmt, std::format_args args);
    void clearLog();
    void logWrk();
};

inline LogMgr& g_logMgr = LogMgr::instance();

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
