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

public:
    AppPipeline(const std::string& in, const std::string& out, const std::string& log);
    bool run();
};