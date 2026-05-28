#include "AppPipeline.hpp"
#include <iostream>
#include <fstream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <algorithm>
#include <iomanip>
#include <unordered_map>
#include <vector>

AppPipeline::AppPipeline(const std::string& in, const std::string& out, const std::string& log)
    : input_path(in), output_path(out) {
    logger = std::make_unique<Logger>(log);
    detected_encoding = "UNKNOWN";
}

// Отображение файла в виртуальную память
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
    size_t error_pos = 0;
    size_t line_len;
    std::string safe_line;
    // Сканируем mmap-буфер построчно по символу '\n'
    for (size_t i = 0; i < file_size; ++i) {
        if (file_data[i] == '\n' || i == file_size - 1) {
            line_len = i - line_start;
            if (i == file_size - 1 && file_data[i] != '\n') {
                line_len++; 
            }
            error_pos = 0;
            if (filter.check_block_swar(file_data + line_start, line_len, error_pos)) {
                // Записываем чистые ASCII байты без выделений на куче (Zero-Copy)
                out_file.write(file_data + line_start, line_len);
                out_file.put('\n');
            } else {
               // Если файл уже UTF-8, ASCII или UNKNOWN - пишем напрямую из mmap-памяти без аллокаций!
                if (detected_encoding == "UTF-8" || detected_encoding == "ASCII" || detected_encoding == "UNKNOWN") {
                    logger->log_discarded_line(line_counter, file_data + line_start, line_len, error_pos);
                } else {
                    std::string raw_line(file_data + line_start, line_len);
                    // Только для CP1251 и KOI8-R делаем аллокацию и транскодирование
                    safe_line = detector.transcode_to_utf8(raw_line, detected_encoding);
                    // Передаем буфер safe_line.data() и его длину safe_line.length() в тот же метод!
                    logger->log_discarded_line(line_counter, safe_line.data(), safe_line.length(), error_pos);
                }
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

// Поблочный анализ кодировки.
// Файл режется на окна по chunk_size байт; для каждого окна вызывается тот же
// detector.detect_encoding (но без диагностической таблицы), а вердикты копятся
// в счётчиках. В конце печатается, сколько процентов окон отнесено к каждой кодировке.
//
// Замечание о единицах: для однобайтовых кодировок (CP1251, KOI8-R) 1 байт = 1 символ,
// поэтому окно в chunk_size байт — это примерно chunk_size кириллических символов.
// Меняя chunk_size, можно эмпирически увидеть, при какой длине решение стабилизируется.
bool AppPipeline::run_chunk_analysis(size_t chunk_size) {
    if (chunk_size == 0) chunk_size = 100; // защита от нулевого размера окна

    std::cout << "=== Поблочный анализ кодировки ===\n";
    std::cout << "    Размер блока: " << chunk_size << " байт\n";

    detector.init_statistical_models();

    // 1. Отображаем файл в виртуальную память (как и в обычном режиме)
    size_t file_size = 0;
    int fd = -1;
    const char* file_data = map_file_to_memory(input_path, file_size, fd);
    if (!file_data) {
        std::cerr << "[Ошибка] Не удалось отобразить файл в память: " << input_path << "\n";
        return false;
    }

    // 2. Счётчики принятых решений по каждой кодировке
    std::unordered_map<std::string, size_t> tally;
    size_t total_chunks = 0;

    // 3. Идём по mmap-буферу окнами фиксированного размера (без копирования)
    for (size_t offset = 0; offset < file_size; offset += chunk_size) {
        size_t this_len = std::min(chunk_size, file_size - offset); // последнее окно короче
        // verbose=false — глушим диагностическую таблицу детектора, чтобы не засорять консоль
        std::string verdict = detector.detect_encoding(file_data + offset, this_len, false);
        tally[verdict]++;
        total_chunks++;
    }

    unmap_file(file_data, file_size, fd); // освобождаем память ОС

    if (total_chunks == 0) {
        std::cout << "    Файл пуст — блоков нет.\n";
        return true;
    }

    // 4. Сортируем кодировки по убыванию числа решений для наглядного вывода
    std::vector<std::pair<std::string, size_t>> ranked(tally.begin(), tally.end());
    std::sort(ranked.begin(), ranked.end(),
              [](const std::pair<std::string, size_t>& a,
                 const std::pair<std::string, size_t>& b) {
                  return a.second > b.second;
              });

    // 5. Печатаем таблицу: кодировка, число блоков, доля в процентах
    std::cout << "\n    Всего блоков: " << total_chunks << "\n";
    std::cout << "    ------------------------------------\n";
    std::cout << std::fixed << std::setprecision(2);
    for (const auto& entry : ranked) {
        double percent = 100.0 * static_cast<double>(entry.second)
                                / static_cast<double>(total_chunks);
        std::cout << "    "
                  << std::left  << std::setw(9) << entry.first
                  << std::right << std::setw(8) << entry.second
                  << "   " << std::setw(6) << percent << " %\n";
    }
    std::cout << "    ------------------------------------\n";

    // 6. Итоговое решение по файлу — мажоритарное голосование (самая частая кодировка)
    const std::pair<std::string, size_t>& winner = ranked.front();
    double win_pct = 100.0 * static_cast<double>(winner.second)
                            / static_cast<double>(total_chunks);
    std::cout << "    Доминирующая кодировка: " << winner.first
              << " (" << win_pct << " % блоков)\n";

    return true;
}