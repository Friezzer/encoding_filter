#pragma once // Защищает от ошибок повторного импорта этого файла

#include <string>
#include <fstream>

class Logger {
private:
    std::ofstream log_file;
public:
    Logger(const std::string& log_path);
    ~Logger();
    // Метод логирования отброшенной строки
    void log_discarded_line(size_t line_num, const char* data, size_t len, size_t error_pos);
};