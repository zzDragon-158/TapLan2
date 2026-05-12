#include "LogMgr.hpp"
#include <cstdarg>
#include <iostream>
#include <print>

LogMgr::LogMgr()
: logLevel_(LogLevel::info)
, running_(false)
, logEntryRing_(nullptr)
, logSem_{0}
{
    // logFile_.open("/tmp/TapLan.log", std::ios::out | std::ios::app);
}

LogMgr::~LogMgr()
{
    // logFile_.close();
#ifdef  _WIN32
    _aligned_free(logEntryRing_);
#elif   __linux__
    free(logEntryRing_);
#endif
}

int LogMgr::initLogMgr()
{
#ifdef  _WIN32
    logEntryRing_ = (LogEntry*)_aligned_malloc(LOG_ENTRY_RING_SIZE, 4096);
#elif   __linux__
    posix_memalign((void**)&logEntryRing_, 4096, LOG_ENTRY_RING_SIZE);
#endif
    if (!logEntryRing_) {
        std::println(stderr, "Cant allocate {}bytes memory.", LOG_ENTRY_RING_SIZE);
        return -1;
    }

    for (size_t i = 0; i < LOG_RING_SIZE; ++i) {
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

    running_ = (initLogMgr() == 0);
    if (!running_)
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
    if (logThread_.joinable())
        logThread_.join();

    return true;
}

void LogMgr::logOutput(LogLevel level, const char* tag, const char* format, ...)
{
    if (!running_ || level > logLevel_)
        return ;

    size_t entryIdx = wIdx_.fetch_add(1, std::memory_order_relaxed);
    LogEntry& logEntry = logEntryRing_[entryIdx & LOG_RING_MASK];

    EntryState expectedState = ENTRY_EMPTY;
    if (!logEntry.state.compare_exchange_strong(expectedState,
                                                ENTRY_WRITTING,
                                                std::memory_order_acquire,
                                                std::memory_order_relaxed)) {
        std::println(stderr, "Log Ring Full.");
        running_ = false;
        return ;
    }

    int offset = 0;
    if (level != LogLevel::raw) {
        offset = snprintf(logEntry.data,
                          LOG_DATA_SIZE,
                          "%s\t%s\t",
                          logLevelStr[static_cast<size_t>(level)],
                          tag);
    }

    va_list args;
    va_start(args, format);
    offset += vsnprintf(logEntry.data + offset,
                        LOG_DATA_SIZE - offset,
                        format,
                        args);
    va_end(args);

    if (level != LogLevel::raw) {
        logEntry.data[offset] = '\n';
        logEntry.data[offset + 1] = '\0';
    }
    logEntry.state.store(ENTRY_READY, std::memory_order_release);

    // wIdx_.notify_one();
    if ((entryIdx & LOG_NOTIFY_THRESHOLD) == 0)
        logSem_.release();
}

void LogMgr::doLogToRing(LogLevel level, const char* tag, std::string_view fmt, std::format_args args)
{
    if (!running_ || level > logLevel_)
        return ;

    size_t entryIdx = wIdx_.fetch_add(1, std::memory_order_relaxed);
    LogEntry& logEntry = logEntryRing_[entryIdx & LOG_RING_MASK];

    EntryState expectedState = ENTRY_EMPTY;
    if (!logEntry.state.compare_exchange_strong(expectedState,
                                                ENTRY_WRITTING,
                                                std::memory_order_acquire,
                                                std::memory_order_relaxed)) {
        std::println(stderr, "Log Ring Full.");
        running_ = false;
        return ;
    }

    char* pos = logEntry.data;
    if (level != LogLevel::raw) {
        pos = std::format_to(
            pos,
            "{}\t{}\t",
            tag,
            logLevelStr[static_cast<size_t>(level)]
        );
    }
    pos = std::vformat_to(
        pos,
        fmt,
        args
    );
    if (level != LogLevel::raw) {
        *pos++ = '\n';
    }
    *pos = '\0';
    logEntry.state.store(ENTRY_READY, std::memory_order_release);

    if ((entryIdx & LOG_NOTIFY_THRESHOLD) == 0)
        logSem_.release();
}

void LogMgr::clearLog()
{
    size_t logCnt = 0;

    while (rIdx_.load(std::memory_order_relaxed) != wIdx_.load(std::memory_order_relaxed)) {
        size_t entryIdx = rIdx_.load(std::memory_order_relaxed);
        LogEntry& logEntry = logEntryRing_[entryIdx & LOG_RING_MASK];
        while (logEntry.state.load(std::memory_order_acquire) != ENTRY_READY) {
            cpuRelax();
        }

        std::cout << logEntry.data;
        logEntry.state.store(ENTRY_EMPTY, std::memory_order_release);
        rIdx_.fetch_add(1, std::memory_order_relaxed);
        ++logCnt;
    }

    if (logCnt)
        std::cout << std::flush;
}

void LogMgr::logWrk()
{
    while (running_) {
        logSem_.try_acquire_for(std::chrono::milliseconds(10));

        clearLog();
    }

    clearLog();
}
