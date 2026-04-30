#ifndef LOGGER_H
#define LOGGER_H

/**
 * @file logger.h
 * @brief 日志系统头文件
 *
 * 提供以下功能：
 * - 多级别日志（DEBUG/INFO/WARNING/ERROR/CRITICAL）
 * - 异步写入（不阻塞主线程）
 * - 日志轮转（文件过大时自动备份）
 * - 日志清理（删除旧备份）
 *
 * 使用示例：
 * @code
 * Logger::getInstance().initialize("config.json");
 * LOG_INFO("服务器启动");
 * LOG_ERROR("发生错误");
 * Logger::getInstance().shutdown();
 * @endcode
 */

#include <string>
#include <fstream>
#include <mutex>
#include <queue>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <chrono>

// ============================================================================
// 日志级别枚举
// ============================================================================

enum class LogLevel
{
    DEBUG = 0,
    INFO = 1,
    WARNING = 2,
    ERROR = 3,
    CRITICAL = 4
};

// ============================================================================
// 日志配置结构
// ============================================================================

struct LogConfig
{
    std::string log_file = "server.log";
    std::string log_level = "info";
    size_t max_file_size = 10 * 1024 * 1024;
    int max_backup_files = 5;
    bool console_output = true;
    bool async_write = true;
};

// ============================================================================
// Logger 日志记录器类
// ============================================================================

class Logger
{
public:
    static Logger &getInstance();

    bool initialize(const LogConfig &config);
    bool initialize(const std::string &config_file);

    void debug(const std::string &message);
    void info(const std::string &message);
    void warning(const std::string &message);
    void error(const std::string &message);
    void critical(const std::string &message);
    void log(LogLevel level, const std::string &message);

    void setLogLevel(LogLevel level);
    void setLogLevel(const std::string &level);
    LogLevel getLogLevel() const { return m_log_level; }
    std::string getLogLevelString(LogLevel level) const;

    void rotate();
    size_t getCurrentLogFileSize() const;
    size_t getTotalLogSize() const;
    int getBackupFileCount() const;
    int cleanupOldLogs(int keep_count);
    void shutdown();

    std::string getLogFilePath() const { return m_log_file; }

    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;

private:
    Logger();
    ~Logger();

    std::string getCurrentTimestamp() const;
    std::string formatMessage(LogLevel level, const std::string &message);
    void writeToFile(const std::string &message);
    void asyncWriter();
    void checkAndRotate();
    std::string getBackupFileName(int index) const;
    std::string getTimestampForFilename() const;

    std::string m_log_file;
    std::ofstream m_log_stream;
    LogLevel m_log_level;
    size_t m_max_file_size;
    int m_max_backup_files;
    bool m_console_output;
    bool m_async_write;

    std::mutex m_mutex;
    std::atomic<bool> m_running;
    std::thread m_writer_thread;

    std::queue<std::string> m_log_queue;
    std::mutex m_queue_mutex;
    std::condition_variable m_queue_cv;

    std::atomic<size_t> m_current_file_size;
};

// ============================================================================
// 便捷的日志宏
// ============================================================================

#define LOG_DEBUG(msg) Logger::getInstance().debug(msg)
#define LOG_INFO(msg) Logger::getInstance().info(msg)
#define LOG_WARNING(msg) Logger::getInstance().warning(msg)
#define LOG_ERROR(msg) Logger::getInstance().error(msg)
#define LOG_CRITICAL(msg) Logger::getInstance().critical(msg)

#endif // LOGGER_H
