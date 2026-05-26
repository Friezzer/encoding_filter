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

    // 0. Загружаем модели один раз (обычно это делается в main или конструкторе)
    detector.init_statistical_models();

    // 1. Определение кодировки (одна строчка!)
    std::cout << "[1/3] Анализ кодировки файла...\n";
    detected_encoding = detector.detect_encoding(input_path);
    std::cout << "      Определена кодировка: " << detected_encoding << "\n";

    // ЭТАП 3: Основной проход 
    std::cout << "[2/3] Фильтрация ASCII данных...\n";
    
    std::ifstream in_file(input_path, std::ios::binary);
    std::ofstream out_file(output_path, std::ios::binary);
    
    // Выделяем буферы в памяти
    std::vector<char> in_buf(65536);
    std::vector<char> out_buf(65536);

    // Связываем потоки с нашими большими буферами
    in_file.rdbuf()->pubsetbuf(in_buf.data(), in_buf.size());
    out_file.rdbuf()->pubsetbuf(out_buf.data(), out_buf.size());

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
            // Детектор сам переводит строку в UTF-8!
            std::string safe_line = detector.transcode_to_utf8(line, detected_encoding);
            
            // Логгер просто записывает готовую безопасную строку
            logger->log_discarded_line(line_counter, safe_line, reason);
            discarded++;
        }
    }

    std::cout << "[3/3] Обработка завершена.\n";
    std::cout << "      Отброшено строк: " << discarded << " из " << line_counter << "\n";

    return true;
}