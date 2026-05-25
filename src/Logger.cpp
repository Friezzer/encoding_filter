#include "Logger.hpp"
#include <iostream>

Logger::Logger(const std::string& log_path) {
    log_file.open(log_path);
    if (!log_file.is_open()) {
        std::cerr << "[Warning] Не удалось открыть файл лога: " << log_path << std::endl;
    }
}

// Деструктор
Logger::~Logger() {
    if (log_file.is_open()) {
        log_file.close();
    }
}

void Logger::log_discarded_line(size_t line_num, const std::string& safe_line, const std::string& reason) {
    if (log_file.is_open()) {
        log_file << "Строка " << line_num << " ОТБРОШЕНА. Причина: " << reason << "\n";
        log_file << "Содержимое: " << safe_line << "\n";
        log_file << "----------------------------------------\n";
    }
}