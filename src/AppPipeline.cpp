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

ChunkResult AppPipeline::process_chunk(const char* data, size_t start_idx, size_t end_idx, const std::string& encoding) {
    ChunkResult result;
    result.valid_data.reserve((end_idx - start_idx) * 0.9); // Резерв памяти
    size_t line_start = start_idx;

    for (size_t i = start_idx; i < end_idx; ++i) {
        if (data[i] == '\n' || i == end_idx - 1) {
            size_t line_len = i - line_start;
            if (i == end_idx - 1 && data[i] != '\n') line_len++;

            size_t error_pos = 0;
            if (filter.check_block_swar(data + line_start, line_len, error_pos)) {
                // Валидно: складываем сырые байты в буфер
                result.valid_data.insert(result.valid_data.end(), data + line_start, data + line_start + line_len);
                result.valid_data.push_back('\n');
            } else {
                std::string raw_line(data + line_start, line_len);
                std::string safe_line = detector.transcode_to_utf8(raw_line, encoding);
                
                // Формируем сообщение (можно через обычное сложение, раз мы перекодируем)
                std::string log_msg = "Строка "
                                      " ОТБРОШЕНА. Позиция " + std::to_string(error_pos + 1) + 
                                      "\nСодержимое: " + safe_line + "\n----------------------------------------\n";
                
                // ВАЖНО: Вставляем строку напрямую в конец единого вектора байт!
                result.log_data.insert(result.log_data.end(), log_msg.begin(), log_msg.end());
                
                result.lines_discarded++;
            }
            result.lines_processed++;
            line_start = i + 1;
        }
    }
    return result;
}

// ---------------------------------------------------------
// 2. РАБОЧИЙ ПОТОК (Хватает задачи из очереди и выполняет)
// ---------------------------------------------------------
void AppPipeline::worker_thread(const char* file_data) {
    while (true) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(task_mtx);
            // Ждем, пока появится задача ИЛИ пока производитель не скажет "Всё!"
            task_cv.wait(lock, [this] { return !task_queue.empty() || producer_done; });
            
            if (task_queue.empty() && producer_done) return; // Конец работы
            
            task = task_queue.front();
            task_queue.pop();
        }

        // Фильтруем данные
        ChunkResult res = process_chunk(file_data, task.start_idx, task.end_idx, detected_encoding);
        res.id = task.id;

        // Отправляем результат в карту готовых кусков
        {
            std::unique_lock<std::mutex> lock(result_mtx);
            results_map[res.id] = std::move(res);
        }
        result_cv.notify_one(); // Будим поток записи!
    }
}

// ---------------------------------------------------------
// 3. ПОТОК ЗАПИСИ (Строго последовательно пишет на диск)
// ---------------------------------------------------------
void AppPipeline::writer_thread(size_t& total_processed, size_t& total_discarded) {
    // Открываем выходной файл системным вызовом
    int out_fd = open(output_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out_fd == -1) {
        std::cerr << "[Ошибка] Не удалось создать выходной файл: " << output_path << "\n";
        return;
    }

    while (true) {
        ChunkResult res;
        {
            std::unique_lock<std::mutex> lock(result_mtx);
            result_cv.wait(lock, [this] { 
                return results_map.count(next_write_id) > 0 || (producer_done && active_tasks == 0); 
            });

            if (results_map.count(next_write_id) == 0 && producer_done && active_tasks == 0) {
                break; // Конец работы
            }

            res = std::move(results_map[next_write_id]);
            results_map.erase(next_write_id);
        }

        // --- БЫСТРАЯ СИСТЕМНАЯ ЗАПИСЬ РЕЗУЛЬТАТОВ НА ДИСК ---
        if (!res.valid_data.empty()) {
            size_t total_written = 0;
            size_t to_write = res.valid_data.size();
            const char* data_ptr = res.valid_data.data();

            // Пишем мегабайтный кусок напрямую в кэш ядра Linux
            while (total_written < to_write) {
                ssize_t bytes = write(out_fd, data_ptr + total_written, to_write - total_written);
                if (bytes <= 0) break; 
                total_written += bytes;
            }
        }

        // Запись логов через обновленный системный логгер
        if (!res.log_data.empty()) {
            logger->write_raw_buffer(res.log_data.data(), res.log_data.size());
        }

        total_processed += res.lines_processed;
        total_discarded += res.lines_discarded;

        next_write_id++;
        active_tasks--;
        bp_cv.notify_one(); 
    }
    
    // Закрываем дескриптор выходного файла
    close(out_fd);
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
    unsigned int num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;

    size_t total_processed = 0;
    size_t total_discarded = 0;

    // Запускаем выделенный поток записи на диск
    std::thread writer(&AppPipeline::writer_thread, this, std::ref(total_processed), std::ref(total_discarded));

    // Создаем ПУЛ ПОТОКОВ-ВЫЧИСЛИТЕЛЕЙ (Thread Pool)
    std::vector<std::thread> workers;
    for (unsigned int i = 0; i < num_threads; ++i) {
        workers.emplace_back(&AppPipeline::worker_thread, this, file_data);
    }

    // Лимит памяти (Backpressure). Не более 16 кусков в ОЗУ одновременно!
    const size_t MAX_IN_FLIGHT = 16; 
    const size_t CHUNK_SIZE = 4 * 1024 * 1024; // 4 Мегабайта

    size_t current_start = 0;
    size_t chunk_id = 0;

    // Главный цикл: режет файл и кидает задачи в очередь
    while (current_start < file_size) {
        // Контроль памяти: если активных задач >= 16, останавливаем чтение!
        {
            std::unique_lock<std::mutex> lock(bp_mtx);
            bp_cv.wait(lock, [this] { return active_tasks < MAX_IN_FLIGHT; });
        }

        size_t current_end = current_start + CHUNK_SIZE;
        if (current_end >= file_size) {
            current_end = file_size;
        } else {
            while (current_end < file_size && file_data[current_end] != '\n') current_end++;
            if (current_end < file_size) current_end++;
        }

        // Кидаем задачу в очередь рабочим потокам
        {
            std::unique_lock<std::mutex> lock(task_mtx);
            task_queue.push({chunk_id, current_start, current_end});
            active_tasks++;
        }
        task_cv.notify_one(); // Будим одного свободного рабочего

        current_start = current_end;
        chunk_id++;
    }

    // Сообщаем всем потокам, что файл закончился
    producer_done = true;
    task_cv.notify_all();
    result_cv.notify_all();

    // Ждем корректного завершения всех потоков
    for (auto& w : workers) w.join();
    writer.join();
    unmap_file(file_data, file_size, fd); // Освобождаем память ОС
    std::cout << "      Всего проверено строк: " << total_processed << "\n";
    std::cout << "      Отброшено строк:       " << total_discarded << "\n";
    std::cout << "      Потоков задействовано: " << num_threads << "\n";
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