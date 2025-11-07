#include "LogMgr.hpp"

const size_t MAX_BUF_SIZE = 512;
const char* logLevelStr[NUMS_OF_LEVEL] = { "[FATAL]", "[ERROR]", "[WARN]", "[INFO]", "[DEBUG]", "[TRACE]" };

LogMgr::LogMgr(): logLevel_(LOG_TRACE)
{
    // logFile_.open("TapLan.log", std::ios::out | std::ios::app);
    run();
}

LogMgr::~LogMgr()
{
    terminate();
    // logFile_.close();
}

bool LogMgr::run()
{
    running_ = true;
    logThread_ = std::thread(&LogMgr::logWorker, this);

    return true;
}

bool LogMgr::terminate()
{
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

    std::stringstream msg;
    char buf[MAX_BUF_SIZE] = { 0 };
    va_list args;
    va_start(args, format);
    vsnprintf(buf, MAX_BUF_SIZE, format, args);
    va_end(args);
    msg << buf;

    {
        std::lock_guard<std::mutex> lock(logMutex_);
        logQueue_.push(msg.str());
    }
    logCv_.notify_one();
}

void LogMgr::logOutput(int level, const char* tag, const char* format, ...)
{
    if (!running_ || level > logLevel_)
        return ;

    std::stringstream msg;
    msg << logLevelStr[level] << '\t' << tag << '\t';

    char buf[MAX_BUF_SIZE] = { 0 };
    va_list args;
    va_start(args, format);
    vsnprintf(buf, MAX_BUF_SIZE, format, args);
    va_end(args);
    msg << buf << std::endl;

    {
        std::lock_guard<std::mutex> lock(logMutex_);
        logQueue_.push(msg.str());
    }
    logCv_.notify_one();
}

void LogMgr::logWorker()
{
    while (running_ || !logQueue_.empty()) {
        std::unique_lock<std::mutex> lock(logMutex_);
        logCv_.wait(lock, [&]{ return !running_ || !logQueue_.empty(); });

        while (!logQueue_.empty()) {
            // logFile_ << logQueue_.front() << std::endl;
            std::cout << logQueue_.front();
            logQueue_.pop();
        }
    }
}
