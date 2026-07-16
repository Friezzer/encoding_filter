#include "Logger.hpp"
#include <iostream>
#include <fcntl.h>
#include <unistd.h>

Logger::Logger(const std::string& log_path) {

    log_fd = open(log_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (log_fd == -1) {
        std::cerr << "[Warning] Не удалось открыть файл лога: " << log_path << "\n";
    }
}

Logger::~Logger() {
    if (log_fd != -1) {
        close(log_fd);
    }
}

void Logger::write_raw_buffer(const char* data, size_t len) {
    if (log_fd == -1 || len == 0) return;

    size_t total_written = 0;

    while (total_written < len) {
        ssize_t bytes = write(log_fd, data + total_written, len - total_written);
        if (bytes <= 0) break;
        total_written += bytes;
    }
}