#include "EncodingDetector.hpp"

const uint8_t EncodingDetector::utf8d[] = {
  // Первая часть: 256 байт. Перевод байта в класс (0..11)
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,  9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
   7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
   8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2,  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  10,3,3,3,3,3,3,3,3,3,3,3,3,4,3,3, 11,6,6,6,5,8,8,8,8,8,8,8,8,8,8,8,

  // Вторая часть: 108 байт (9 строк по 12 элементов). Матрица переходов.
   0,12,24,36,60,96,84,12,12,12,48,72, 12,12,12,12,12,12,12,12,12,12,12,12,
  12, 0,12,12,12,12,12, 0,12, 0,12,12, 12,24,12,12,12,12,12,24,12,24,12,12,
  12,12,12,12,12,12,12,24,12,12,12,12, 12,24,12,12,12,12,12,12,12,24,12,12,
  12,12,12,12,12,12,12,36,12,36,12,12, 12,36,12,12,12,12,12,36,12,36,12,12,
  12,36,12,12,12,12,12,12,12,12,12,12, 
};

// Парсер файлов маппинга *.map
bool EncodingDetector::load_encoding_map(const std::string& filepath, EncodingMap& out_map, const std::string& name) {
    std::ifstream file(filepath);
    if (!file.is_open()) return false;

    out_map.name = name;
    int byte_val;
    std::string hex_unicode;
    
    // Читаем строки вида: "224 0430"
    while (file >> byte_val >> hex_unicode) {
        if (byte_val >= 0 && byte_val < 256) {
            uint32_t unicode;
            std::stringstream ss;
            ss << std::hex << hex_unicode;
            ss >> unicode;
            out_map.byte_to_unicode[byte_val] = unicode;
        }
    }
    file.close();
    return true;
}

// Парсер частотных файлов *.freq
bool EncodingDetector::load_language_freq(const std::string& filepath, LanguageFreq& out_lang, const std::string& name) {
    std::ifstream file(filepath);
    if (!file.is_open()) return false;

    out_lang.name = name;
    std::string hex_unicode;
    double frequency;

    // Читаем строки вида: "0430 0.0801"
    while (file >> hex_unicode >> frequency) {
        uint32_t unicode;
        std::stringstream ss;
        ss << std::hex << hex_unicode;
        ss >> unicode;
        out_lang.unicode_frequencies[unicode] = frequency;
    }
    file.close();
    return true;
}

// Инициализация (Вызывается из AppPipeline при старте)
bool EncodingDetector::init_statistical_models() {
    bool ok = true;
    ok &= load_encoding_map("../config/cp1251.txt", map_cp1251, "CP1251");
    ok &= load_encoding_map("../config/koi8r.txt", map_koi8r, "KOI8-R");
    ok &= load_language_freq("../config/russian.txt", freq_russian, "Russian");
    
    if (!ok) {
        std::cerr << "[Ошибка] Не удалось загрузить файлы конфигурации из папки config/\n";
    }
    is_initialized = ok;
    return ok;
}

bool EncodingDetector::is_valid_utf8_file(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[Detector] Ошибка: не удалось открыть " << filepath << " для анализа.\n";
        return false;
    }

    uint32_t state = UTF8_ACCEPT; // 0
    uint32_t codepoint = 0;
    char byte;

    while (file.get(byte)) {
        // Передаем байт в функцию (обязательно приводим к uint8_t, чтобы избежать отрицательных значений)
        decode(&state, &codepoint, static_cast<uint8_t>(byte));
        
        // Если попали в состояние ошибки (12), мгновенно прерываем чтение
        if (state == UTF8_REJECT) {
            file.close();
            return false;
        }
    }

    file.close();
    return state == UTF8_ACCEPT;
}

double EncodingDetector::calculate_similarity(const std::vector<size_t>& byte_counts, size_t cyrillic_total, 
                                              const EncodingMap& enc, const LanguageFreq& lang) {
    if (cyrillic_total == 0) return 0.0;

    std::unordered_map<uint32_t, double> observed_freqs;

    // Проекция: переводим байты кодировки в коды Юникода
    for (int b = 128; b < 256; ++b) {
        if (byte_counts[b] == 0) continue;
        
        uint32_t unicode_char = enc.byte_to_unicode[b];
        if (unicode_char != 0) { 
            // Сразу считаем относительную частоту (нормализация)
            observed_freqs[unicode_char] += (double)byte_counts[b] / cyrillic_total;
        }
    }

    // Расчет скалярного произведения и длин векторов
    double dot_product = 0.0;
    double norm_observed = 0.0;
    double norm_expected = 0.0;

    for (const auto& [unicode, freq] : observed_freqs) {
        norm_observed += freq * freq;
        
        // Если такая буква есть в эталоне русского языка
        auto it = lang.unicode_frequencies.find(unicode);
        if (it != lang.unicode_frequencies.end()) {
            dot_product += freq * it->second;
        }
    }

    // Длина эталонного вектора
    for (const auto& [unicode, freq] : lang.unicode_frequencies) {
        norm_expected += freq * freq;
    }

    if (norm_observed == 0.0 || norm_expected == 0.0) return 0.0;

    // Косинусное сходство = (a * b) / (|a| * |b|)
    return dot_product / (std::sqrt(norm_observed) * std::sqrt(norm_expected));
}

std::string EncodingDetector::detect_encoding(const std::string& filepath) {
    // 1. Сначала проверяем на UTF-8 с помощью автомата
    if (is_valid_utf8_file(filepath)) {
        return "UTF-8";
    }

    // 2. Если статистические модели не загружены, сдаемся
    if (!is_initialized) return "UNKNOWN";

    // 3. Открываем файл для сбора статистики (Сэмплирование)
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) return "UNKNOWN";

    std::vector<size_t> histogram(256, 0);
    char byte;
    size_t total_read = 0;
    size_t cyrillic_total = 0;
    size_t unique_cyrillic = 0;

    // Читаем не более 50 000 байт (Константное время O(1))
    while (file.get(byte) && total_read < 50000) {
        unsigned char ubyte = static_cast<unsigned char>(byte); // Каст обязателен!
        histogram[ubyte]++;
        
        if (ubyte >= 128) {
            cyrillic_total++;
            if (histogram[ubyte] == 1) {
                unique_cyrillic++; // Считаем уникальные кириллические байты
            }
        }
        total_read++;
    }
    file.close();

    // 4. Эвристические предохранители
    if (cyrillic_total < 100) {
        return "ASCII"; // Слишком мало кириллицы, считаем файл латинским
    }
    if (unique_cyrillic < 7) {
        return "UNKNOWN"; // Мусорные/синтетические данные (атака нулевой энтропии)
    }

    // 5. Вычисляем сходство
    double score_1251 = calculate_similarity(histogram, cyrillic_total, map_cp1251, freq_russian);
    double score_koi8 = calculate_similarity(histogram, cyrillic_total, map_koi8r, freq_russian);

    // Для наглядности при отладке (можно закомментировать позже)
    // std::cout << "  [Отладка] Сходство CP1251: " << score_1251 << ", KOI8-R: " << score_koi8 << "\n";

    // 6. Принятие решения (Порог доверия)
    const double THRESHOLD = 0.60;

    if (score_1251 > THRESHOLD && score_1251 > score_koi8) {
        return "CP1251";
    } else if (score_koi8 > THRESHOLD && score_koi8 > score_1251) {
        return "KOI8-R";
    }
    return "UNKNOWN";
}

// Вспомогательный метод перевода Юникода в байты UTF-8 (из старого логгера)
void EncodingDetector::unicode_to_utf8(uint32_t codepoint, std::string& out) {
    if (codepoint <= 0x7F) {
        out += static_cast<char>(codepoint);
    } else if (codepoint <= 0x7FF) {
        out += static_cast<char>(0xC0 | (codepoint >> 6));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else if (codepoint <= 0xFFFF) {
        out += static_cast<char>(0xE0 | (codepoint >> 12));
        out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else if (codepoint <= 0x10FFFF) {
        out += static_cast<char>(0xF0 | (codepoint >> 18));
        out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else {
        out += "\xEF\xBF\xBD"; 
    }
}

// Метод транскодирования строки целиком
std::string EncodingDetector::transcode_to_utf8(const std::string& raw_line, const std::string& encoding) const {
    if (encoding == "UTF-8" || encoding == "ASCII" || encoding == "UNKNOWN" || !is_initialized) {
        return raw_line;
    }

    const EncodingMap& map = (encoding == "CP1251") ? map_cp1251 : map_koi8r;
    
    std::string utf8_line = "";
    // ОПТИМИЗАЦИЯ: Выделяем память ОДИН раз на всю строку!
    utf8_line.reserve(raw_line.length() * 2); 

    for (size_t i = 0; i < raw_line.length(); ++i) {
        unsigned char c = static_cast<unsigned char>(raw_line[i]);
        if (c < 128) {
            utf8_line += raw_line[i];
        } else {
            uint32_t unicode = map.byte_to_unicode[c];
            if (unicode != 0) {
                unicode_to_utf8(unicode, utf8_line); 
            } else {
                utf8_line += "\xEF\xBF\xBD";
            }
        }
    }
    return utf8_line;
}