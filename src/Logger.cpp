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

void Logger::set_source_encoding(const std::string& encoding) {
    source_encoding = encoding;
}

// Метод перекодировки (пока базовый каркас)
std::string Logger::transcode_to_utf8(const std::string& raw_line) {
    // Если файл и так в UTF-8 или чистом ASCII - ничего делать не нужно
    if (source_encoding == "UTF-8" || source_encoding == "ASCII") {
        return raw_line;
    }
    return raw_line + " [Внимание: исходная кодировка " + source_encoding + "]";
}

void Logger::log_discarded_line(size_t line_num, const std::string& raw_line, const std::string& reason) {
    if (log_file.is_open()) {
        std::string safe_line = transcode_to_utf8(raw_line);

        log_file << "Строка " << line_num << " ОТБРОШЕНА. Причина: " << reason << "\n";
        log_file << "Содержимое: " << safe_line << "\n";
        log_file << "----------------------------------------\n";
    }
}