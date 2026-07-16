#pragma once // Защищает от ошибок повторного импорта этого файла

#include <string>
#include <fstream>

class Logger {
private:
    int log_fd; // Системный файловый дескриптор

public:
    Logger(const std::string& log_path);
    ~Logger();

    // метод записи готового буфера 
    void write_raw_buffer(const char* data, size_t len);
};