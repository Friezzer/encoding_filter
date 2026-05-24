#pragma once // Защищает от ошибок повторного импорта этого файла

#include <string>
#include <fstream>

class Logger {
private:
    std::ofstream log_file;
    std::string source_encoding; // Здесь Логгер будет хранить текущую кодировку
    // Вспомогательный метод для перекодировки строки в UTF-8
    std::string transcode_to_utf8(const std::string& raw_line);

public:
    // Конструктор
    Logger(const std::string& log_path);

    // Деструктор
    ~Logger();
    
    // Метод для передачи кодировки от Детектора к Логгеру
    void set_source_encoding(const std::string& encoding);

    // Метод логирования отброшенной строки
    void log_discarded_line(size_t line_num, const std::string& raw_line, const std::string& reason);
};