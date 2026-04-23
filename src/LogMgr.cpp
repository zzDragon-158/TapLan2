#include "LogMgr.hpp"

LogMgr::LogMgr(): logLevel_(LOG_INFO), running_(false)
{
    // logFile_.open("TapLan.log", std::ios::out | std::ios::app);
}

LogMgr::~LogMgr()
{
    // logFile_.close();
}

int LogMgr::initLogMgr()
{
#ifdef  _WIN32
    logEntryRing_ = _aligned_malloc(TOTAL_LOG_BUF_SIZE, 4096);
#elif   __linux__
    posix_memalign((void**)&logEntryRing_, 4096, TOTAL_LOG_BUF_SIZE);
#endif
    if (!logEntryRing_) {
        printf("Cant allocate [%u] memory.", TOTAL_LOG_BUF_SIZE);
        return -1;
    }

    for (size_t i = 0; i < LOG_BUF_NUM; ++i) {
        logEntryRing_[i].state.store(ENTRY_EMPTY);
    }

    rIdx_.store(0);
    wIdx_.store(0);

    return 0;
}

bool LogMgr::run()
{
    if (running_)
        return false;

    logThread_ = std::thread(&LogMgr::logWrk, this);
    pthread_setname_np(logThread_.native_handle(), logThreadName_);

    return true;
}

bool LogMgr::terminate()
{
    if (!running_)
        return false;

    running_ = false;
    logCv_.notify_all();
    if (logThread_.joinable())
        logThread_.join();

    return true;
}

void LogMgr::logOutput(const char* format, ...)
{
    if (!running_)
        return ;

    size_t entryIdx = wIdx_.fetch_add(1, std::memory_order_relaxed);
    LogEntry& logEntry = logEntryRing_[entryIdx & LOG_BUF_RING_MASK];

    int expectedState = ENTRY_EMPTY;
    if (!logEntry.state.compare_exchange_strong(expectedState,
                                                ENTRY_WRITTING,
                                                std::memory_order_acquire,
                                                std::memory_order_relaxed)) {
        // FIXME: drop because buf full
        return ;
    }

    va_list args;
    va_start(args, format);
    vsnprintf(logEntry.data, LOG_DATA_SIZE, format, args);
    va_end(args);
    logEntry.state.store(ENTRY_READY, std::memory_order_release);

    logCv_.notify_one();
}

void LogMgr::logOutput(char level, const char* tag, const char* format, ...)
{
    if (!running_ || level > logLevel_)
        return ;

    size_t entryIdx = wIdx_.fetch_add(1, std::memory_order_relaxed);
    LogEntry& logEntry = logEntryRing_[entryIdx & LOG_BUF_RING_MASK];

    int expectedState = ENTRY_EMPTY;
    if (!logEntry.state.compare_exchange_strong(expectedState,
                                                ENTRY_WRITTING,
                                                std::memory_order_acquire,
                                                std::memory_order_relaxed)) {
        // FIXME: drop because buf full
        return ;
    }

    int offset = snprintf(logEntry.data, LOG_DATA_SIZE, "%s\t%s\t", logLevelStr[level], tag);

    va_list args;
    va_start(args, format);
    offset += vsnprintf(logEntry.data + offset, LOG_DATA_SIZE - offset, format, args);
    va_end(args);

    logEntry.data[offset] = '\n';
    logEntry.data[offset + 1] = '\0';
    logEntry.state.store(ENTRY_READY, std::memory_order_release);

    logCv_.notify_one();
}

void LogMgr::logWrk()
{
    running_ = (initLogMgr() == 0);

    while (running_) {
        std::unique_lock<std::mutex> lock(logMutex_);
        logCv_.wait(lock, [&]{ return !running_ || rIdx_.load(std::memory_order_relaxed) != wIdx_.load(std::memory_order_relaxed); });

        while (rIdx_.load(std::memory_order_relaxed) != wIdx_.load(std::memory_order_relaxed)) {
            size_t entryIdx = rIdx_.load(std::memory_order_relaxed);
            LogEntry& logEntry = logEntryRing_[entryIdx & LOG_BUF_RING_MASK];
            while (logEntry.state.load(std::memory_order_acquire) != ENTRY_READY) {
                // FIXME: only support x86;
                __builtin_ia32_pause();
            }

            std::cout << logEntry.data;
            logEntry.state.store(ENTRY_EMPTY, std::memory_order_release);
            rIdx_.fetch_add(1, std::memory_order_relaxed);
        }

        std::cout << std::flush;
    }
}
