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

// Обновленная структура на базе динамического двумерного вектора
struct LanguageMarkov {
    std::string name;
    size_t alphabet_size = 0; // Размер алфавита конкретного языка (A)
    
    // Двумерный динамический вектор размером (A x A)
    std::vector<std::vector<double>> transition_matrix;
    
    // Карта перевода Юникода в локальный плоский индекс (0 .. A-1)
    std::unordered_map<uint32_t, size_t> unicode_to_idx;
};

class EncodingDetector {
public:
    EncodingDetector() = default;
    // Метод для загрузки конфигурационных файлов (вызывается 1 раз при старте)
    bool init_statistical_models();

    // Главный метод: определяет кодировку (UTF-8, CP1251, KOI8-R)
    // verbose=false глушит диагностическую таблицу (нужно при поблочном анализе)
    std::string detect_encoding(const char* data, size_t size, bool verbose = true);

    std::string transcode_to_utf8(const std::string& raw_line, const std::string& encoding) const;

    // Опционально: включить адаптивный порог косинуса T(N) (по умолчанию ВЫКЛЮЧЕНО).
    // Параметры стоит подобрать калибровкой; формула: T(N) = clamp(T_inf - k/N, T_floor, T_inf).
    void set_adaptive_cosine_threshold(bool enabled,
                                       double T_inf = 0.83,
                                       double T_floor = 0.60,
                                       double k = 13.0);
private:
    // Хранилища конфигураций (загружаются в память один раз)
    EncodingMap map_cp1251;
    EncodingMap map_koi8r;
    LanguageFreq freq_russian;
    LanguageMarkov markov_russian;
    bool is_initialized = false;

    // --- Порог принятия для косинусного сходства ---
    bool   adaptive_cosine_threshold = false; // по умолчанию выкл. -> используется фиксированный порог
    double cosine_threshold_fixed    = 0.60;  // прежний фиксированный порог
    // Параметры адаптивной кривой T(N) = clamp(T_inf - k/N, T_floor, T_inf).
    // Значения по умолчанию ОРИЕНТИРОВОЧНЫЕ: их следует откалибровать на реальных
    // текстах (например, поблочным прогоном run_chunk_analysis при разных длинах).
    double cosine_T_inf   = 0.83; // асимптота для длинных текстов (~ (1 + mu1)/2)
    double cosine_T_floor = 0.60; // нижняя граница для коротких текстов
    double cosine_k       = 13.0; // скорость выхода на асимптоту (мотивирована 1 - S ~ 1/N)

    // Возвращает порог косинуса для текущего объёма выборки N (число кириллических символов)
    double get_cosine_threshold(size_t cyrillic_total) const;
     // парсер формата файлов биграмм
    bool load_language_markov(const std::string& filepath, LanguageMarkov& out_markov, const std::string& name);

    double calculate_markov_score(const char* data, size_t size, 
                                  const EncodingMap& enc, const LanguageMarkov& lang);

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