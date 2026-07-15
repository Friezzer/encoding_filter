#pragma once

#include <string>
#include <memory>
#include "Logger.hpp"
#include "FilterEngine.hpp"
#include "EncodingDetector.hpp"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <queue>
// Структура задачи для рабочего потока
struct Task {
    size_t id;
    size_t start_idx;
    size_t end_idx;
};

// Контейнер для результатов работы потока
struct ChunkResult {
    size_t id;
    std::vector<char> valid_data;
    std::vector<char> log_data;
    size_t lines_processed = 0;
    size_t lines_discarded = 0;
};

class AppPipeline {
private:
    std::string input_path;
    std::string output_path;
    
    std::unique_ptr<Logger> logger;
    FilterEngine filter;
    EncodingDetector detector;
    std::string detected_encoding; // Сюда сохраним результат
    
     // --- Многопоточные примитивы (Thread Pool & Queues) ---
    std::mutex task_mtx;
    std::condition_variable task_cv;
    std::queue<Task> task_queue;

    std::mutex result_mtx;
    std::condition_variable result_cv;
    std::unordered_map<size_t, ChunkResult> results_map;

    std::mutex bp_mtx; // Мьютекс для обратного давления (Backpressure)
    std::condition_variable bp_cv;
    
    std::atomic<size_t> active_tasks{0};     // Сколько задач сейчас в ОЗУ
    std::atomic<bool> producer_done{false};  // Флаг завершения чтения файла
    size_t next_write_id = 0;                // ID куска, который диск ждет для записи

    const char* map_file_to_memory(const std::string& path, size_t& out_size, int& out_fd);
    void unmap_file(const char* addr, size_t size, int fd);
    // Методы многопоточной обработки
    ChunkResult process_chunk(const char* data, size_t start_idx, size_t end_idx, const std::string& encoding);
    void worker_thread(const char* file_data);
    void writer_thread(size_t& total_processed, size_t& total_discarded);

public:
    AppPipeline(const std::string& in, const std::string& out, const std::string& log);
    bool run();

    // Поблочный анализ: режет файл на окна фиксированного размера, по каждому
    // окну определяет кодировку и печатает распределение решений в процентах.
    bool run_chunk_analysis(size_t chunk_size = 100);
};