#pragma     once
#include    <cstdio>
#include    <cstdarg>
#include    <iostream>
#include    <fstream>
#include    <queue>
#include    <mutex>
#include    <condition_variable>
#include    <thread>
#include    <sstream>
#include    <pthread.h>

#define     LogMgrPtr                   LogMgr::ptr()
#define     LOGR(fmt, ...)              LogMgrPtr->logOutput(fmt, ##__VA_ARGS__)
#define     LOGF(TAG, fmt, ...)         LogMgrPtr->logOutput(LOG_FATAL, TAG, fmt, ##__VA_ARGS__)
#define     LOGE(TAG, fmt, ...)         LogMgrPtr->logOutput(LOG_ERROR, TAG, fmt, ##__VA_ARGS__)
#define     LOGW(TAG, fmt, ...)         LogMgrPtr->logOutput(LOG_WARN, TAG, fmt, ##__VA_ARGS__)
#define     LOGI(TAG, fmt, ...)         LogMgrPtr->logOutput(LOG_INFO, TAG, fmt, ##__VA_ARGS__)
#define     LOGD(TAG, fmt, ...)         LogMgrPtr->logOutput(LOG_DEBUG, TAG, fmt, ##__VA_ARGS__)
#define     LOGT(TAG, fmt, ...)         LogMgrPtr->logOutput(LOG_TRACE, TAG, fmt, ##__VA_ARGS__)

typedef enum {
    LOG_FATAL = 0,
    LOG_ERROR,
    LOG_WARN,
    LOG_INFO,
    LOG_DEBUG,
    LOG_TRACE,
    NUMS_OF_LEVEL,
} LogLevel;

class LogMgr {
public:
    bool run();
    bool terminate();
    void setLogLevel(LogLevel level) { logLevel_ = level; };
    bool isRunning() { return running_; };
    void logOutput(const char* format, ...);
    void logOutput(int level, const char* tag, const char* format, ...);
    static LogMgr* ptr();

private:
    std::ofstream logFile_;
    uint8_t logLevel_;
    std::queue<std::string> logQueue_;
    std::mutex logMutex_;
    std::condition_variable logCv_;
    const char* logThreadName_;
    std::thread logThread_;
    bool running_;

    LogMgr();
    ~LogMgr();
    void logWorker();
};

inline LogMgr* LogMgr::ptr() {
    static LogMgr ins;

    return &ins;
}
