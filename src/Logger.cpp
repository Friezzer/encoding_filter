#include "Logger.hpp"
#include <iostream>

Logger::Logger(const std::string& log_path) {
    log_file.open(log_path);
    if (!log_file.is_open()) {
        std::cerr << "[Warning] Не удалось открыть файл лога: " << log_path << "\n";
    }
}

// Деструктор
Logger::~Logger() {
    if (log_file.is_open()) {
        log_file.close();
    }
}

void Logger::log_discarded_line(size_t line_num, const char* data, size_t len, size_t error_pos) {
    if (log_file.is_open()) {
        log_file << "Строка " << line_num << " ОТБРОШЕНА. Причина: не-ASCII байт на позиции " << error_pos + 1 << "\n";
        log_file << "Содержимое: ";
        log_file.write(data, len); // Пишем напрямую из переданного буфера памяти
        log_file << "\n----------------------------------------\n";
    }
}