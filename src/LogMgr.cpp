#include "LogMgr.hpp"
#include <algorithm>
#include <cstdarg>
#include <iostream>
// TODO: MinGw GCC not support
//#include <print>

namespace {
class BoundedCharOutput {
public:
    using difference_type = std::ptrdiff_t;

    BoundedCharOutput(char* pos, char* end) : pos_(pos), end_(end) {}
    BoundedCharOutput& operator*() { return *this; }
    BoundedCharOutput& operator++() { return *this; }
    BoundedCharOutput operator++(int) { return *this; }
    BoundedCharOutput& operator=(char value) {
        if (pos_ < end_)
            *pos_++ = value;
        return *this;
    }
    char* position() const { return pos_; }

private:
    char* pos_;
    char* end_;
};
}

LogMgr::LogMgr()
{
    // logFile_.open("/tmp/TapLan.log", std::ios::out | std::ios::app);
}

LogMgr::~LogMgr()
{
    terminate();
    // logFile_.close();
    delete[] logEntryRing_;
}

int LogMgr::initLogMgr()
{
    delete[] logEntryRing_;
    logEntryRing_ = new (std::nothrow) LogEntry[LOG_RING_SIZE];
    if (!logEntryRing_) {
        //std::println(stderr, "Cant allocate {}bytes memory.", LOG_ENTRY_RING_SIZE);
        return -1;
    }

    for (size_t i = 0; i < LOG_RING_SIZE; ++i) {
        logEntryRing_[i].state.store(ENTRY_EMPTY);
    }

    rIdx_.store(0);
    wIdx_.store(0);
    droppedLogs_.store(0);
    notificationPending_.store(false);

    return 0;
}

bool LogMgr::run()
{
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return false;

    if (initLogMgr() != 0) {
        running_.store(false, std::memory_order_release);
        return false;
    }

    logThread_ = std::thread(&LogMgr::logWrk, this);
    pthread_setname_np(logThread_.native_handle(), logThreadName_);

    return true;
}

bool LogMgr::terminate()
{
    bool wasRunning = running_.exchange(false, std::memory_order_acq_rel);
    if (wasRunning && !notificationPending_.exchange(true, std::memory_order_acq_rel))
        logSem_.release();
    if (logThread_.joinable())
        logThread_.join();

    return wasRunning;
}

LogMgr::LogEntry* LogMgr::acquireLogEntry(size_t& entryIdx)
{
    size_t write = wIdx_.load(std::memory_order_relaxed);
    for (;;) {
        const size_t read = rIdx_.load(std::memory_order_acquire);
        if (write - read >= LOG_RING_SIZE) {
            droppedLogs_.fetch_add(1, std::memory_order_relaxed);
            return nullptr;
        }
        if (wIdx_.compare_exchange_weak(write, write + 1,
                                        std::memory_order_acq_rel,
                                        std::memory_order_relaxed)) {
            entryIdx = write;
            break;
        }
    }

    LogEntry& entry = logEntryRing_[entryIdx & LOG_RING_MASK];
    EntryState expected = ENTRY_EMPTY;
    if (!entry.state.compare_exchange_strong(expected, ENTRY_WRITTING,
                                             std::memory_order_acquire,
                                             std::memory_order_relaxed)) {
        // Capacity accounting guarantees this cannot occur unless state is corrupted.
        droppedLogs_.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }
    return &entry;
}

void LogMgr::publishLogEntry(LogEntry& entry)
{
    entry.state.store(ENTRY_READY, std::memory_order_release);
    if (!notificationPending_.exchange(true, std::memory_order_acq_rel))
        logSem_.release();
}

void LogMgr::logOutput(LogLevel level, const char* tag, const char* format, ...)
{
    if (!running_.load(std::memory_order_acquire)
        || level > logLevel_.load(std::memory_order_relaxed))
        return ;

    size_t entryIdx;
    LogEntry* logEntry = acquireLogEntry(entryIdx);
    if (!logEntry)
        return ;

    size_t offset = 0;
    if (level != LogLevel::raw) {
        int written = snprintf(logEntry->data,
                          LOG_DATA_SIZE,
                          "%s\t%s\t",
                          logLevelStr[static_cast<size_t>(level)],
                          tag);
        offset = std::min(static_cast<size_t>(std::max(written, 0)), LOG_DATA_SIZE - 1);
    }

    va_list args;
    va_start(args, format);
    int written = vsnprintf(logEntry->data + offset,
                        LOG_DATA_SIZE - offset,
                        format,
                        args);
    va_end(args);
    if (written > 0)
        offset = std::min(offset + static_cast<size_t>(written), LOG_DATA_SIZE - 1);

    if (level != LogLevel::raw && offset < LOG_DATA_SIZE - 1) {
        logEntry->data[offset++] = '\n';
    }
    logEntry->data[offset] = '\0';

    publishLogEntry(*logEntry);
}

void LogMgr::doLogToRing(LogLevel level, const char* tag, std::string_view fmt, std::format_args args)
{
    if (!running_.load(std::memory_order_acquire)
        || level > logLevel_.load(std::memory_order_relaxed))
        return ;

    size_t entryIdx;
    LogEntry* logEntry = acquireLogEntry(entryIdx);
    if (!logEntry)
        return ;

    char* pos = logEntry->data;
    char* end = logEntry->data + LOG_DATA_SIZE - 1;
    if (level != LogLevel::raw) {
        int written = snprintf(pos, LOG_DATA_SIZE, "%s\t%s\t",
                               logLevelStr[static_cast<size_t>(level)], tag);
        pos += std::min(static_cast<size_t>(std::max(written, 0)), LOG_DATA_SIZE - 1);
    }

    try {
        pos = std::vformat_to(BoundedCharOutput(pos, end), fmt, args).position();
    } catch (const std::format_error& error) {
        int written = snprintf(pos, static_cast<size_t>(end - pos) + 1,
                               "Invalid log format: %s", error.what());
        pos += std::min(static_cast<size_t>(std::max(written, 0)),
                        static_cast<size_t>(end - pos));
    }
    if (level != LogLevel::raw && pos < end)
        *pos++ = '\n';
    *pos = '\0';

    publishLogEntry(*logEntry);
}

void LogMgr::clearLog()
{
    size_t logCnt = 0;

    while (rIdx_.load(std::memory_order_relaxed) != wIdx_.load(std::memory_order_relaxed)) {
        size_t entryIdx = rIdx_.load(std::memory_order_relaxed);
        LogEntry& logEntry = logEntryRing_[entryIdx & LOG_RING_MASK];
        if (logEntry.state.load(std::memory_order_acquire) != ENTRY_READY)
            break;

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
    while (running_.load(std::memory_order_acquire)) {
        logSem_.try_acquire_for(std::chrono::milliseconds(10));
        notificationPending_.store(false, std::memory_order_release);
        clearLog();
    }

    clearLog();
    uint64_t dropped = droppedLogs_.load(std::memory_order_relaxed);
    if (dropped)
        std::cerr << "[WARN]\t[LogMgr]\tDropped " << dropped << " log messages.\n";
}
