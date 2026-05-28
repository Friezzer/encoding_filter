#pragma once

#include <string>
#include <memory>
#include "Logger.hpp"
#include "FilterEngine.hpp"
#include "EncodingDetector.hpp"

class AppPipeline {
private:
    std::string input_path;
    std::string output_path;
    
    std::unique_ptr<Logger> logger;
    FilterEngine filter;
    EncodingDetector detector;
    std::string detected_encoding; // Сюда сохраним результат
    
    const char* map_file_to_memory(const std::string& path, size_t& out_size, int& out_fd);
    void unmap_file(const char* addr, size_t size, int fd);
public:
    AppPipeline(const std::string& in, const std::string& out, const std::string& log);
    bool run();

    // Поблочный анализ: режет файл на окна фиксированного размера, по каждому
    // окну определяет кодировку и печатает распределение решений в процентах.
    bool run_chunk_analysis(size_t chunk_size = 100);
};