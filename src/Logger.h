#pragma once

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <mutex>
#include <ctime>
#include <iomanip>
#include <memory>

enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARNING = 2,
    ERROR = 3
};

class Logger {
public:
    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }

    void setLogLevel(LogLevel level) {
        minLevel_ = level;
    }

    void setLogFile(const std::string& filename) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (fileStream_.is_open()) {
            fileStream_.close();
        }
        fileStream_.open(filename, std::ios::app);
        logToFile_ = fileStream_.is_open();
    }

    void setLogToConsole(bool enabled) {
        logToConsole_ = enabled;
    }

    template<typename... Args>
    void debug(Args&&... args) {
        log(LogLevel::DEBUG, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void info(Args&&... args) {
        log(LogLevel::INFO, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void warning(Args&&... args) {
        log(LogLevel::WARNING, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void error(Args&&... args) {
        log(LogLevel::ERROR, std::forward<Args>(args)...);
    }

private:
    Logger() : minLevel_(LogLevel::INFO), logToConsole_(true), logToFile_(false) {}
    ~Logger() {
        if (fileStream_.is_open()) {
            fileStream_.close();
        }
    }

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::string getCurrentTime() {
        auto now = std::time(nullptr);
        auto tm = *std::localtime(&now);
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }

    std::string levelToString(LogLevel level) {
        switch (level) {
            case LogLevel::DEBUG:   return "DEBUG";
            case LogLevel::INFO:    return "INFO";
            case LogLevel::WARNING: return "WARNING";
            case LogLevel::ERROR:   return "ERROR";
            default:                return "UNKNOWN";
        }
    }

    template<typename... Args>
    void log(LogLevel level, Args&&... args) {
        if (level < minLevel_) {
            return;
        }

        std::ostringstream oss;
        (oss << ... << std::forward<Args>(args));
        
        std::string message = oss.str();
        std::string formattedMsg = "[" + getCurrentTime() + "] [" + levelToString(level) + "] " + message;

        std::lock_guard<std::mutex> lock(mutex_);
        
        if (logToConsole_) {
            std::ostream& out = (level >= LogLevel::ERROR) ? std::cerr : std::cout;
            out << formattedMsg << std::endl;
        }

        if (logToFile_ && fileStream_.is_open()) {
            fileStream_ << formattedMsg << std::endl;
            fileStream_.flush();
        }
    }

    LogLevel minLevel_;
    bool logToConsole_;
    bool logToFile_;
    std::ofstream fileStream_;
    std::mutex mutex_;
};

// Convenience macros
#define LOG_DEBUG(...)   Logger::getInstance().debug(__VA_ARGS__)
#define LOG_INFO(...)    Logger::getInstance().info(__VA_ARGS__)
#define LOG_WARNING(...) Logger::getInstance().warning(__VA_ARGS__)
#define LOG_ERROR(...)   Logger::getInstance().error(__VA_ARGS__)

// Helper macro for checking return values
#define LOG_CHECK(condition, errorMsg) \
    do { \
        if (!(condition)) { \
            LOG_ERROR(errorMsg); \
        } \
    } while(0)

// Helper macro for checking return values and returning on error
#define LOG_CHECK_RETURN(condition, errorMsg, returnValue) \
    do { \
        if (!(condition)) { \
            LOG_ERROR(errorMsg); \
            return returnValue; \
        } \
    } while(0)

// Helper macro for checking return values and throwing on error
#define LOG_CHECK_THROW(condition, errorMsg) \
    do { \
        if (!(condition)) { \
            LOG_ERROR(errorMsg); \
            throw std::runtime_error(errorMsg); \
        } \
    } while(0)
