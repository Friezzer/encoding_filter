#pragma once // Защищает от ошибок повторного импорта этого файла

#include <string>
#include <fstream>

class Logger {
private:
    int log_fd; // Системный файловый дескриптор (вместо std::ofstream)

public:
    Logger(const std::string& log_path);
    ~Logger();

    // Универсальный метод записи готового буфера через write()
    void write_raw_buffer(const char* data, size_t len);
};