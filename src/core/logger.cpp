/**
 * @file logger.cpp
 * @brief 日志系统实现
 *
 * 提供完整的日志记录功能：
 * - 多级别日志（DEBUG/INFO/WARNING/ERROR/CRITICAL）
 * - 支持文件输出和控制台输出
 * - 异步写入（减少 I/O 阻塞）
 * - 日志文件轮转（按大小自动分割）
 * - 备份文件管理（自动清理旧日志）
 *
 * 日志格式：
 * [2024-01-23 10:30:45.123] [    INFO] This is a log message
 * [2024-01-23 10:30:45.456] [   ERROR] An error occurred
 */

#include "logger.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <unistd.h>
#include <sys/stat.h>
#include <cstring>
#include <algorithm>
#include <vector>
#include <future>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

static Logger *g_logger_instance = nullptr;

Logger &Logger::getInstance()
{
    static Logger instance;
    g_logger_instance = &instance;
    return instance;
}

Logger::Logger()
    : m_log_level(LogLevel::INFO), m_max_file_size(10 * 1024 * 1024), m_max_backup_files(5), m_console_output(true), m_async_write(true), m_running(false), m_current_file_size(0)
{
}

Logger::~Logger()
{
    shutdown();
}

bool Logger::initialize(const LogConfig &config)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_log_stream.is_open())
    {
        m_log_stream.close();
    }

    m_log_file = config.log_file;
    m_max_file_size = config.max_file_size;
    m_max_backup_files = config.max_backup_files;
    m_console_output = config.console_output;
    m_async_write = config.async_write;

    setLogLevel(config.log_level);

    cleanupOldLogs(0);

    m_log_stream.open(m_log_file, std::ios::out | std::ios::trunc);
    if (!m_log_stream.is_open())
    {
        std::cerr << "[ERROR] Failed to open log file: " << m_log_file << std::endl;
        return false;
    }

    struct stat st;
    if (stat(m_log_file.c_str(), &st) == 0)
    {
        m_current_file_size = st.st_size;
    }

    if (m_async_write)
    {
        m_running = true;
        m_writer_thread = std::thread(&Logger::asyncWriter, this);
    }

    std::stringstream ss;
    ss << "=== Logger initialized ===";
    ss << " [file=" << m_log_file;
    ss << ", level=" << getLogLevelString(m_log_level);
    ss << ", max_size=" << (m_max_file_size / 1024 / 1024) << "MB";
    ss << ", max_backups=" << m_max_backup_files;
    ss << ", async=" << (m_async_write ? "true" : "false") << "]";

    std::string first_line = ">>> [INFO] Log file will be CLEANED on next startup <<<";

    if (m_log_stream.is_open())
    {
        m_log_stream << ss.str() << std::endl;
        m_log_stream << first_line << std::endl;
        m_log_stream.flush();
        m_current_file_size += ss.str().size() + first_line.size() + 2;
    }

    if (m_console_output)
    {
        std::cout << ss.str() << std::endl;
    }

    return true;
}

bool Logger::initialize(const std::string &config_file)
{
    LogConfig config;

    std::ifstream file(config_file);
    if (!file.is_open())
    {
        std::cerr << "[WARN] Config file not found, using default log config" << std::endl;
        return initialize(config);
    }

    try
    {
        json root;
        file >> root;
        file.close();

        if (root.contains("server") && root["server"].is_object())
        {
            const auto &server_cfg = root["server"];

            config.log_file = server_cfg.value("log_file", config.log_file);
            config.log_level = server_cfg.value("log_level", config.log_level);
            config.max_file_size = server_cfg.value("log_max_size", 10) * 1024 * 1024;
            config.max_backup_files = server_cfg.value("log_max_backups", config.max_backup_files);
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "[WARN] Failed to parse config JSON: " << e.what() << ", using defaults" << std::endl;
        file.close();
        return initialize(config);
    }

    return initialize(config);
}

void Logger::shutdown()
{
    if (m_async_write)
    {
        m_running = false;
        m_queue_cv.notify_one();

        if (m_writer_thread.joinable())
        {
            try
            {
                auto start = std::chrono::steady_clock::now();
                auto future = std::async(std::launch::async, [this]()
                                         { m_writer_thread.join(); });

                if (future.wait_for(std::chrono::seconds(2)) == std::future_status::timeout)
                {
                    std::cerr << "[WARN] Logger shutdown timeout, forcing exit" << std::endl;
                }
                else
                {
                    auto end = std::chrono::steady_clock::now();
                    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
                    if (duration.count() > 1000)
                    {
                        std::cerr << "[WARN] Logger shutdown took " << duration.count() << "ms" << std::endl;
                    }
                }
            }
            catch (const std::exception &e)
            {
                std::cerr << "[WARN] Logger shutdown exception: " << e.what() << std::endl;
            }
        }
    }

    if (m_log_stream.is_open())
    {
        m_log_stream.flush();
        m_log_stream.close();
    }
}

void Logger::setLogLevel(LogLevel level)
{
    m_log_level = level;
}

void Logger::setLogLevel(const std::string &level)
{
    std::string lower_level = level;
    std::transform(lower_level.begin(), lower_level.end(), lower_level.begin(), ::tolower);

    if (lower_level == "debug")
        m_log_level = LogLevel::DEBUG;
    else if (lower_level == "info")
        m_log_level = LogLevel::INFO;
    else if (lower_level == "warning" || lower_level == "warn")
        m_log_level = LogLevel::WARNING;
    else if (lower_level == "error")
        m_log_level = LogLevel::ERROR;
    else if (lower_level == "critical")
        m_log_level = LogLevel::CRITICAL;
    else
        m_log_level = LogLevel::INFO;
}

std::string Logger::getLogLevelString(LogLevel level) const
{
    switch (level)
    {
    case LogLevel::DEBUG:
        return "DEBUG";
    case LogLevel::INFO:
        return "INFO";
    case LogLevel::WARNING:
        return "WARNING";
    case LogLevel::ERROR:
        return "ERROR";
    case LogLevel::CRITICAL:
        return "CRITICAL";
    default:
        return "UNKNOWN";
    }
}

std::string Logger::getCurrentTimestamp() const
{
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) %
              1000;

    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

std::string Logger::formatMessage(LogLevel level, const std::string &message)
{
    std::stringstream ss;
    ss << "[" << getCurrentTimestamp() << "]";
    ss << " [" << std::setw(8) << getLogLevelString(level) << "]";
    ss << " " << message;
    return ss.str();
}

void Logger::debug(const std::string &message) { log(LogLevel::DEBUG, message); }
void Logger::info(const std::string &message) { log(LogLevel::INFO, message); }
void Logger::warning(const std::string &message) { log(LogLevel::WARNING, message); }
void Logger::error(const std::string &message) { log(LogLevel::ERROR, message); }
void Logger::critical(const std::string &message) { log(LogLevel::CRITICAL, message); }

void Logger::log(LogLevel level, const std::string &message)
{
    if (level < m_log_level)
        return;

    std::string formatted = formatMessage(level, message);

    if (m_async_write && m_running)
    {
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        m_log_queue.push(formatted);
        m_queue_cv.notify_one();
    }
    else
    {
        writeToFile(formatted);
    }
}

void Logger::writeToFile(const std::string &message)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    checkAndRotate();

    if (m_log_stream.is_open())
    {
        m_log_stream << message << std::endl;
        m_log_stream.flush();
        m_current_file_size += message.size() + 1;
    }

    if (m_console_output)
    {
        std::cout << message << std::endl;
    }
}

void Logger::asyncWriter()
{
    while (m_running)
    {
        std::unique_lock<std::mutex> lock(m_queue_mutex);

        m_queue_cv.wait_for(lock, std::chrono::milliseconds(100), [this]
                            { return !m_log_queue.empty() || !m_running; });

        while (!m_log_queue.empty())
        {
            std::string message = m_log_queue.front();
            m_log_queue.pop();

            lock.unlock();
            writeToFile(message);
            lock.lock();
        }
    }

    while (!m_log_queue.empty())
    {
        std::string message = m_log_queue.front();
        m_log_queue.pop();
        writeToFile(message);
    }
}

void Logger::checkAndRotate()
{
    if (m_current_file_size >= m_max_file_size)
    {
        rotate();
    }
}

void Logger::rotate()
{
    m_log_stream.close();

    std::string oldest_backup = getBackupFileName(m_max_backup_files);
    if (access(oldest_backup.c_str(), F_OK) == 0)
    {
        remove(oldest_backup.c_str());
    }

    for (int i = m_max_backup_files - 1; i >= 1; --i)
    {
        std::string old_name = getBackupFileName(i);
        std::string new_name = getBackupFileName(i + 1);
        rename(old_name.c_str(), new_name.c_str());
    }

    std::string backup_name = getBackupFileName(1);
    rename(m_log_file.c_str(), backup_name.c_str());

    m_log_stream.open(m_log_file, std::ios::app);
    if (!m_log_stream.is_open())
    {
        std::cerr << "[ERROR] Failed to reopen log file after rotation" << std::endl;
        return;
    }

    struct stat st;
    if (stat(m_log_file.c_str(), &st) == 0)
    {
        m_current_file_size = st.st_size;
    }
    else
    {
        m_current_file_size = 0;
    }

    std::string msg = "Log rotated: " + backup_name;
    writeToFile(msg);
}

std::string Logger::getBackupFileName(int index) const
{
    size_t dot_pos = m_log_file.rfind('.');
    if (dot_pos != std::string::npos)
    {
        return m_log_file.substr(0, dot_pos) + "." + std::to_string(index) +
               m_log_file.substr(dot_pos);
    }
    return m_log_file + "." + std::to_string(index);
}

std::string Logger::getTimestampForFilename() const
{
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);

    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y%m%d_%H%M%S");
    return ss.str();
}

size_t Logger::getCurrentLogFileSize() const
{
    struct stat st;
    if (stat(m_log_file.c_str(), &st) == 0)
    {
        return st.st_size;
    }
    return 0;
}

size_t Logger::getTotalLogSize() const
{
    size_t total = getCurrentLogFileSize();

    for (int i = 1; i <= m_max_backup_files; ++i)
    {
        std::string backup = getBackupFileName(i);
        struct stat st;
        if (stat(backup.c_str(), &st) == 0)
        {
            total += st.st_size;
        }
    }

    return total;
}

int Logger::getBackupFileCount() const
{
    int count = 0;
    for (int i = 1; i <= m_max_backup_files; ++i)
    {
        std::string backup = getBackupFileName(i);
        if (access(backup.c_str(), F_OK) == 0)
        {
            count++;
        }
    }
    return count;
}

int Logger::cleanupOldLogs(int keep_count)
{
    int removed = 0;

    for (int i = keep_count + 1; i <= m_max_backup_files + 10; ++i)
    {
        std::string backup = getBackupFileName(i);
        if (access(backup.c_str(), F_OK) == 0)
        {
            remove(backup.c_str());
            removed++;
        }
        else
        {
            break;
        }
    }

    return removed;
}
