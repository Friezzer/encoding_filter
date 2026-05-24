#include "AppPipeline.hpp"
#include <iostream>
#include <fstream>

AppPipeline::AppPipeline(const std::string& in, const std::string& out, const std::string& log)
    : input_path(in), output_path(out) {
    logger = std::make_unique<Logger>(log);
    detected_encoding = "UNKNOWN";
}

bool AppPipeline::run() {
    std::cout << "=== Запуск конвейера обработки ===\n";

    // ЭТАП 1: Определение кодировки
    std::cout << "[1/3] Анализ кодировки файла...\n";
    if (detector.is_valid_utf8_file(input_path)) {
        detected_encoding = "UTF-8";
    } else {
        //todo
        detected_encoding = "CP1251 (Предположительно)"; 
    }
    std::cout << "      Определена кодировка: " << detected_encoding << "\n";

    // ЭТАП 2: Настройка Логгера
    // Передаем логгеру кодировку, чтобы он знал, как декодировать кракозябры
    logger->set_source_encoding(detected_encoding);

    // ЭТАП 3: Основной проход 
    std::cout << "[2/3] Фильтрация ASCII данных...\n";
    
    std::ifstream in_file(input_path, std::ios::binary);
    std::ofstream out_file(output_path, std::ios::binary);
    
    if (!in_file.is_open() || !out_file.is_open()) {
        std::cerr << "Ошибка доступа к файлам.\n";
        return false;
    }

    std::string line;
    size_t line_counter = 0, discarded = 0, written = 0;

    while (std::getline(in_file, line)) {
        line_counter++;
        std::string reason;

        if (filter.is_ascii_line(line, reason)) {
            out_file << line << "\n";
            written++;
        } else {
            logger->log_discarded_line(line_counter, line, reason);
            discarded++;
        }
    }

    std::cout << "[3/3] Обработка завершена.\n";
    std::cout << "      Отброшено строк: " << discarded << " из " << line_counter << "\n";

    return true;
}