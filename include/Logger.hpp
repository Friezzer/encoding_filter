#pragma once // Защищает от ошибок повторного импорта этого файла

#include <string>
#include <fstream>

class Logger {
private:
    std::ofstream log_file;
public:
    // Конструктор
    Logger(const std::string& log_path);

    // Деструктор
    ~Logger();
    
    // Метод логирования отброшенной строки
    void log_discarded_line(size_t line_num, const std::string& raw_line, const std::string& reason);
};