#pragma once

#include <string>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <fstream>
#include <iostream>
#include <sstream>
#include <math.h>
#define UTF8_ACCEPT 0
#define UTF8_REJECT 12

// 1. Структура для хранения таблицы кодировки
struct EncodingMap {
    std::string name;
    uint32_t byte_to_unicode[256] = {0}; 
};

// 2. Структура для хранения эталонных частот языка
struct LanguageFreq {
    std::string name;
    std::unordered_map<uint32_t, double> unicode_frequencies; 
};

class EncodingDetector {
public:
    EncodingDetector() = default;
    // Метод для загрузки конфигурационных файлов (вызывается 1 раз при старте)
    bool init_statistical_models();

    // Главный метод: определяет любую кодировку (UTF-8, CP1251, KOI8-R)
    std::string detect_encoding(const char* data, size_t size);

    std::string transcode_to_utf8(const std::string& raw_line, const std::string& encoding) const;
private:
    // Хранилища конфигураций (загружаются в память один раз)
    EncodingMap map_cp1251;
    EncodingMap map_koi8r;
    LanguageFreq freq_russian;
    bool is_initialized = false;

    // Вспомогательные парсеры файлов
    bool load_encoding_map(const std::string& filepath, EncodingMap& out_map, const std::string& name);
    bool load_language_freq(const std::string& filepath, LanguageFreq& out_lang, const std::string& name);

    // Математический движок косинусного сходства
    double calculate_similarity(const std::vector<size_t>& byte_counts, size_t cyrillic_total, 
                                const EncodingMap& enc, const LanguageFreq& lang);

    // Оригинальные элементы для автомата UTF-8
    static const uint8_t utf8d[];
    bool is_valid_utf8(const char* data, size_t size);
    
    static inline uint32_t decode(uint32_t* state, uint32_t* codep, uint32_t byte) {
        uint32_t type = utf8d[byte];

        *codep = (*state != UTF8_ACCEPT) ?
            (byte & 0x3fu) | (*codep << 6) :
            (0xff >> type) & (byte);

        *state = utf8d[256 + *state + type];
        return *state;
    }

    static void unicode_to_utf8(uint32_t codepoint, std::string& out);
};

