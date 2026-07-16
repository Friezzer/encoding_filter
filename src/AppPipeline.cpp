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

ChunkResult AppPipeline::process_chunk(const char* data, size_t start_idx, size_t end_idx, 
                                       const std::string& encoding, size_t start_line) {
    ChunkResult result;
    result.valid_data.reserve(end_idx - start_idx);
    result.log_data.reserve(end_idx - start_idx);
    size_t line_start = start_idx;
    size_t valid_block_start = start_idx; // Указатель начала непрерывного ASCII-блока
    size_t error_pos = 0;
    size_t current_line_num = start_line; // Потокобезопасный локальный счетчик строк

    // Быстрые не-конкатенирующие лямбды для записи в буфер лога
    auto append_to_buf = [](std::vector<char>& buf, const char* str, size_t len) {
        buf.insert(buf.end(), str, str + len);
    };

    auto append_num_to_buf = [](std::vector<char>& buf, size_t num) {
        char temp[32];
        int len = std::snprintf(temp, sizeof(temp), "%zu", num);
        buf.insert(buf.end(), temp, temp + len);
    };

    for (size_t i = start_idx; i < end_idx; ++i) {
        if (data[i] == '\n' || i == end_idx - 1) {
            size_t line_len = i - line_start;
            bool has_newline = (data[i] == '\n');
            if (!has_newline && i == end_idx - 1) line_len++;

            if (filter.check_block_swar(data + line_start, line_len, error_pos)) {
                // Строка чистая: ничего не делаем
            } else {
                // Строка содержит не-ASCII:
                // Сбрасываем весь накопленный ASCII-блок одним быстрым вызовом insert 
                if (line_start > valid_block_start) {
                    result.valid_data.insert(result.valid_data.end(), 
                                             data + valid_block_start, 
                                             data + line_start);
                }


                // Форматируем и пишем лог напрямую в байтовый вектор log_data
                append_to_buf(result.log_data, "Строка ", 14); // Длина UTF-8 префикса в байтах
                append_num_to_buf(result.log_data, current_line_num);
                append_to_buf(result.log_data, " ОТБРОШЕНА. Причина: не-ASCII байт на позиции ", 38);
                append_num_to_buf(result.log_data, error_pos + 1);
                append_to_buf(result.log_data, "\nСодержимое: ", 25);
                
                // Записываем саму декодированную строку
                detector.transcode_to_utf8_buf(data + line_start, line_len, encoding, result.log_data);
                
                append_to_buf(result.log_data, "\n----------------------------------------\n", 42);

                result.lines_discarded++;

                // Сдвигаем начало следующего ASCII-блока на строку, идущую за ошибкой
                valid_block_start = i + 1;
            }
            result.lines_processed++;
            current_line_num++;
            line_start = i + 1;
        }
    }

    // В конце чанка сбрасываем остаток чистых строк
    if (valid_block_start < end_idx) {
        result.valid_data.insert(result.valid_data.end(), 
                                 data + valid_block_start, 
                                 data + end_idx);
    }

    return result;
}

void AppPipeline::worker_thread(const char* file_data) {
    while (true) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(task_mtx);
            task_cv.wait(lock, [this] { return !task_queue.empty() || producer_done; });
            
            if (task_queue.empty() && producer_done) return; 
            
            task = task_queue.front();
            task_queue.pop();
        }

        // Фильтруем данные, передавая параметр task.start_line из очереди
        ChunkResult res = process_chunk(file_data, task.start_idx, task.end_idx, detected_encoding, task.start_line);
        res.id = task.id;

        {
            std::unique_lock<std::mutex> lock(result_mtx);
            results_map[res.id] = std::move(res);
        }
        result_cv.notify_one(); 
    }
}

void AppPipeline::writer_thread(size_t& total_processed, size_t& total_discarded) {
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
                break;
            }

            res = std::move(results_map[next_write_id]);
            results_map.erase(next_write_id);
        }

        if (!res.valid_data.empty()) {
            size_t total_written = 0;
            size_t to_write = res.valid_data.size();
            const char* data_ptr = res.valid_data.data();

            while (total_written < to_write) {
                ssize_t bytes = write(out_fd, data_ptr + total_written, to_write - total_written);
                if (bytes <= 0) break; 
                total_written += bytes;
            }
        }

        if (!res.log_data.empty()) {
            logger->write_raw_buffer(res.log_data.data(), res.log_data.size());
        }

        total_processed += res.lines_processed;
        total_discarded += res.lines_discarded;

        next_write_id++;
        active_tasks--;
        bp_cv.notify_one(); 
    }
    
    close(out_fd);
}

bool AppPipeline::run() {
    std::cout << "=== Запуск конвейера обработки ===\n";
    detector.init_statistical_models();

    size_t file_size = 0;
    int fd = -1;
    const char* file_data = map_file_to_memory(input_path, file_size, fd);
    if (!file_data) {
        std::cerr << "[Ошибка] Не удалось отобразить файл в память: " << input_path << "\n";
        return false;
    }

    std::cout << "[1/3] Анализ кодировки файла...\n";
    detected_encoding = detector.detect_encoding(file_data, file_size);
    std::cout << "      Определена кодировка: " << detected_encoding << "\n";

    std::cout << "[2/3] Фильтрация ASCII данных...\n";
    unsigned int num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;

    size_t total_processed = 0;
    size_t total_discarded = 0;

    std::thread writer(&AppPipeline::writer_thread, this, std::ref(total_processed), std::ref(total_discarded));

    // Создаем пул потоков
    std::vector<std::thread> workers;
    for (unsigned int i = 0; i < num_threads; ++i) {
        workers.emplace_back(&AppPipeline::worker_thread, this, file_data);
    }

    const size_t MAX_IN_FLIGHT = 16; 
    const size_t CHUNK_SIZE = 4 * 1024 * 1024; // 4 Мегабайта

    size_t current_start = 0;
    size_t chunk_id = 0;
    size_t global_line_counter = 1; 
    auto start = std::chrono::high_resolution_clock::now();
    while (current_start < file_size) {
        {
            std::unique_lock<std::mutex> lock(bp_mtx);
            bp_cv.wait(lock, [this] { return active_tasks < MAX_IN_FLIGHT; });
        }

        size_t current_end = current_start + CHUNK_SIZE;
        size_t lines_in_this_chunk = 0; // Сюда посчитаем строки текущего куска

        // 1. Считаем количество новых строк (\n) в базовом куске
        for (size_t k = current_start; k < current_end && k < file_size; ++k) {
            if (file_data[k] == '\n') {
                lines_in_this_chunk++;
            }
        }

        if (current_end >= file_size) {
            current_end = file_size;
            // Если самый конец файла не заканчивается на '\n', добавляем последнюю строку
            if (file_size > 0 && file_data[file_size - 1] != '\n') {
                lines_in_this_chunk++;
            }
        } else {
            // Сдвигаем конец чанка вправо до ближайшего '\n'
            while (current_end < file_size && file_data[current_end] != '\n') {
                current_end++;
            }
            if (current_end < file_size) {
                current_end++; // Включаем сам '\n'
                lines_in_this_chunk++; // Считаем этот пограничный '\n'
            }
        }

        // Кидаем задачу в очередь, передавая в качестве start_line текущий глобальный счетчик строк
        {
            std::unique_lock<std::mutex> lock(task_mtx);
            task_queue.push({chunk_id, current_start, current_end, global_line_counter});
            active_tasks++;
        }
        task_cv.notify_one();

        // Рассчитываем стартовый номер строки для чанка
        global_line_counter += lines_in_this_chunk;

        current_start = current_end;
        chunk_id++;
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end - start;
    std::cout << "Время выполнения: " << elapsed.count() << " мс" << "\n";
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

// Поблочный анализ кодировки
bool AppPipeline::run_chunk_analysis(size_t chunk_size) {
    if (chunk_size == 0) chunk_size = 100;

    std::cout << "=== Поблочный анализ кодировки ===\n";
    std::cout << "    Размер блока: " << chunk_size << " байт\n";

    detector.init_statistical_models();

    size_t file_size = 0;
    int fd = -1;
    const char* file_data = map_file_to_memory(input_path, file_size, fd);
    if (!file_data) {
        std::cerr << "[Ошибка] Не удалось отобразить файл в память: " << input_path << "\n";
        return false;
    }

    std::unordered_map<std::string, size_t> tally;
    size_t total_chunks = 0;

    for (size_t offset = 0; offset < file_size; offset += chunk_size) {
        size_t this_len = std::min(chunk_size, file_size - offset); // последнее окно короче
        // verbose=false
        std::string verdict = detector.detect_encoding(file_data + offset, this_len, false);
        tally[verdict]++;
        total_chunks++;
    }

    unmap_file(file_data, file_size, fd); // освобождаем память ОС

    if (total_chunks == 0) {
        std::cout << "    Файл пуст — блоков нет.\n";
        return true;
    }

    std::vector<std::pair<std::string, size_t>> ranked(tally.begin(), tally.end());
    std::sort(ranked.begin(), ranked.end(),
              [](const std::pair<std::string, size_t>& a,
                 const std::pair<std::string, size_t>& b) {
                  return a.second > b.second;
              });

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

    // Итоговое решение по файлу
    const std::pair<std::string, size_t>& winner = ranked.front();
    double win_pct = 100.0 * static_cast<double>(winner.second)
                            / static_cast<double>(total_chunks);
    std::cout << "    Доминирующая кодировка: " << winner.first
              << " (" << win_pct << " % блоков)\n";

    return true;
}