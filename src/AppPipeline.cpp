#include "AppPipeline.hpp"
#include <iostream>
#include <fstream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

AppPipeline::AppPipeline(const std::string& in, const std::string& out, const std::string& log)
    : input_path(in), output_path(out) {
    logger = std::make_unique<Logger>(log);
    detected_encoding = "UNKNOWN";
}

// Отображение файла в виртуальную память (mmap)
const char* AppPipeline::map_file_to_memory(const std::string& path, size_t& out_size, int& out_fd) {
    out_fd = open(path.c_str(), O_RDONLY);
    if (out_fd == -1) return nullptr;

    struct stat sb;
    if (fstat(out_fd, &sb) == -1) {
        close(out_fd);
        return nullptr;
    }
    out_size = sb.st_size;

    if (out_size == 0) {
        close(out_fd);
        return nullptr;
    }

    void* mapped = mmap(nullptr, out_size, PROT_READ, MAP_PRIVATE, out_fd, 0);
    if (mapped == MAP_FAILED) {
        close(out_fd);
        return nullptr;
    }

    return static_cast<const char*>(mapped);
}

// Освобождение mmap-памяти
void AppPipeline::unmap_file(const char* addr, size_t size, int fd) {
    if (addr && addr != MAP_FAILED) {
        munmap(const_cast<char*>(addr), size);
    }
    if (fd != -1) {
        close(fd);
    }
}

bool AppPipeline::run() {
    std::cout << "=== Запуск конвейера обработки ===\n";

    detector.init_statistical_models();

    // 1. Отображаем файл в виртуальную память Linux
    size_t file_size = 0;
    int fd = -1;
    const char* file_data = map_file_to_memory(input_path, file_size, fd);
    if (!file_data) {
        std::cerr << "[Ошибка] Не удалось отобразить файл в память: " << input_path << "\n";
        return false;
    }

    // 2. Детектируем кодировку прямо в ОЗУ за один вызов!
    std::cout << "[1/3] Анализ кодировки файла...\n";
    detected_encoding = detector.detect_encoding(file_data, file_size);
    std::cout << "      Определена кодировка: " << detected_encoding << "\n";

    // 3. Открываем выходной файл для фильтрации
    std::cout << "[2/3] Фильтрация ASCII данных...\n";
    std::ofstream out_file(output_path, std::ios::binary);
    if (!out_file.is_open()) {
        std::cerr << "Ошибка: не удалось создать выходной файл: " << output_path << "\n";
        unmap_file(file_data, file_size, fd);
        return false;
    }

    // Настройка быстрого буфера вывода
    std::vector<char> out_buf(65536);
    out_file.rdbuf()->pubsetbuf(out_buf.data(), out_buf.size());

    size_t line_start = 0;
    size_t line_counter = 1;
    size_t discarded = 0;

    // Сканируем mmap-буфер построчно по символу '\n'
    for (size_t i = 0; i < file_size; ++i) {
        if (file_data[i] == '\n' || i == file_size - 1) {
            size_t line_len = i - line_start;
            if (i == file_size - 1 && file_data[i] != '\n') {
                line_len++; 
            }

            size_t error_pos = 0;
            // Быстрая проверка SWAR!
            if (filter.check_block_swar(file_data + line_start, line_len, error_pos)) {
                // Записываем чистые ASCII байты без выделений на куче (Zero-Copy)
                out_file.write(file_data + line_start, line_len);
                out_file.put('\n');
            } else {
                // Нашли не-ASCII. Выделяем временную строку для перекодирования
                std::string raw_line(file_data + line_start, line_len);
                
                // Детектор сам перекодирует её в безопасный UTF-8!
                std::string safe_line = detector.transcode_to_utf8(raw_line, detected_encoding);
                
                std::string reason = "не-ASCII байт на позиции " + std::to_string(error_pos + 1);
                
                // Передаем готовую UTF-8 строку в логгер
                logger->log_discarded_line(line_counter, safe_line, reason);
                discarded++;
            }

            line_start = i + 1;
            line_counter++;
        }
    }

    out_file.close();
    unmap_file(file_data, file_size, fd); // Освобождаем память ОС

    std::cout << "[3/3] Обработка завершена.\n";
    std::cout << "      Всего проверено строк: " << line_counter - 1 << "\n";
    std::cout << "      Отброшено строк:        " << discarded << "\n";

    return true;
}