#include "EncodingDetector.hpp"
#include <algorithm>
#include <cmath>

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

// Вспомогательный метод перевода 2-байтового UTF-8 символа кириллицы в код Юникода
uint32_t utf8_char_to_unicode(unsigned char b1, unsigned char b2) {
    return ((static_cast<uint32_t>(b1) & 0x1F) << 6) | (static_cast<uint32_t>(b2) & 0x3F);
}

// Парсер для биграмм
bool EncodingDetector::load_language_markov(const std::string& filepath, LanguageMarkov& out_markov, const std::string& name) {
    std::ifstream file(filepath);
    if (!file.is_open()) return false;

    out_markov.name = name;
    std::string line_str;

    // Временная структура для хранения сырых данных из файла
    struct RawTransition {
        uint32_t prev_uni;
        uint32_t curr_uni;
        double abs_freq;
    };
    std::vector<RawTransition> raw_list;

    while (std::getline(file, line_str)) {
        if (line_str.empty() || line_str[0] == '#' || line_str[0] == ';') continue;
        
        if (!std::isdigit(static_cast<unsigned char>(line_str[0]))) continue;

        std::stringstream ss(line_str);
        int rank_num;
        std::string bigram_str;
        double abs_freq;
        int ling_rank;


        if (ss >> rank_num >> bigram_str >> abs_freq >> ling_rank) {
            if (bigram_str.length() == 4) {
                uint32_t prev_uni = utf8_char_to_unicode(bigram_str[0], bigram_str[1]);
                uint32_t curr_uni = utf8_char_to_unicode(bigram_str[2], bigram_str[3]);

                if (out_markov.unicode_to_idx.find(prev_uni) == out_markov.unicode_to_idx.end()) {
                    out_markov.unicode_to_idx[prev_uni] = out_markov.alphabet_size++;
                }
                if (out_markov.unicode_to_idx.find(curr_uni) == out_markov.unicode_to_idx.end()) {
                    out_markov.unicode_to_idx[curr_uni] = out_markov.alphabet_size++;
                }

                raw_list.push_back({prev_uni, curr_uni, abs_freq});
            }
        }
    }
    file.close();

    size_t A = out_markov.alphabet_size;
    if (A == 0) return false;

    // Считаем сумму выходящих переходов для каждого состояния
    std::vector<double> row_sums(A, 0.0);
    for (const auto& r : raw_list) {
        size_t idx_prev = out_markov.unicode_to_idx[r.prev_uni];
        row_sums[idx_prev] += r.abs_freq;
    }

    // Выделяем память под двумерный вектор и заполняем штрафами -6.0 по умолчанию
    const double LAPLACE_PENALTY = -12.0; 
    out_markov.transition_matrix.assign(A, std::vector<double>(A, LAPLACE_PENALTY));

    // Вычисляем логарифмы переходных вероятностей 
    for (const auto& r : raw_list) {
        size_t idx_prev = out_markov.unicode_to_idx[r.prev_uni];
        size_t idx_curr = out_markov.unicode_to_idx[r.curr_uni];

        if (row_sums[idx_prev] > 0.0) {
            double prob = r.abs_freq / row_sums[idx_prev];
            out_markov.transition_matrix[idx_prev][idx_curr] = std::log(prob);
        }
    }

    return true;
}

bool EncodingDetector::init_statistical_models() {
    bool ok = true;
    ok &= load_encoding_map("../config/cp1251.txt", map_cp1251, "CP1251");
    ok &= load_encoding_map("../config/koi8r.txt", map_koi8r, "KOI8-R");
    ok &= load_language_freq("../config/russian.txt", freq_russian, "Russian");
    // Загружаем марковскую матрицу биграмм для цепи Маркова
    ok &= load_language_markov("../config/markov.txt", markov_russian, "Russian_Markov");
    if (!ok) {
        std::cerr << "[Ошибка] Не удалось загрузить файлы конфигурации из папки config/\n";
    }
    is_initialized = ok;
    return ok;
}

bool EncodingDetector::is_valid_utf8(const char* data, size_t size) {
    uint32_t state = UTF8_ACCEPT;
    uint32_t codepoint = 0;
    for (size_t i = 0; i < size; ++i) {
        decode(&state, &codepoint, static_cast<uint8_t>(data[i]));
        if (state == UTF8_REJECT) {
            return false;
        }
    }
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
    return dot_product / (std::sqrt(norm_observed) * std::sqrt(norm_expected));
}

double EncodingDetector::calculate_markov_score(const char* data, size_t size, 
                                                const EncodingMap& enc, const LanguageMarkov& lang) {
    if (!is_initialized || lang.alphabet_size == 0) return -9999.0;

    double total_log_prob = 0.0;
    size_t valid_transitions = 0;
    int prev_idx = -1;

    const size_t TARGET_TRANSITIONS = 30000; 

    for (size_t i = 0; i < size; ++i) {
        unsigned char c = static_cast<unsigned char>(data[i]);
        if (c < 128) {
            prev_idx = -1; // Сброс окна на латинице
            continue;
        }

        uint32_t unicode = enc.byte_to_unicode[c];
        if (unicode == 0) {
            prev_idx = -1;
            continue;
        }

        // Находим локальный индекс буквы
        int curr_idx = -1;
        auto it_curr = lang.unicode_to_idx.find(unicode);
        if (it_curr != lang.unicode_to_idx.end()) {
            curr_idx = static_cast<int>(it_curr->second);
        }

        if (curr_idx == -1) {
            prev_idx = -1;
            continue;
        }

        if (prev_idx != -1) {
            // Мгновенный доступ в двумерный вектор переходов
            total_log_prob += lang.transition_matrix[prev_idx][curr_idx];
            valid_transitions++;

            // Набрали достаточный объем выборки — прерываем проход
            if (valid_transitions >= TARGET_TRANSITIONS) {
                break;
            }
        }
        prev_idx = curr_idx;
    }

    // Защита от деления на ноль: ни одной биграммы кириллицы не набралось
    if (valid_transitions == 0) return -9999.0; // нет данных для Маркова
    return total_log_prob / valid_transitions;   // нормализованное лог-правдоподобие
}

// Опционально включить/настроить адаптивный порог косинуса. По умолчанию выключено.
void EncodingDetector::set_adaptive_cosine_threshold(bool enabled, double T_inf, double T_floor, double k) {
    adaptive_cosine_threshold = enabled;
    cosine_T_inf   = T_inf;
    cosine_T_floor = T_floor;
    cosine_k       = k;
}

double EncodingDetector::get_cosine_threshold(size_t cyrillic_total) const {
    if (!adaptive_cosine_threshold) {
        return cosine_threshold_fixed; // прежнее поведение: фиксированные 0.60
    }
    if (cyrillic_total == 0) return cosine_T_floor;

    double t = cosine_T_inf - cosine_k / static_cast<double>(cyrillic_total);
    if (t < cosine_T_floor) t = cosine_T_floor;
    if (t > cosine_T_inf)   t = cosine_T_inf;
    return t;
}

std::string EncodingDetector::detect_encoding(const char* data, size_t size, bool verbose) {
    // 1. Сначала проверяем на UTF-8 конечным автоматом
    if (is_valid_utf8(data, size)) {
        return "UTF-8";
    }

    if (!is_initialized) return "UNKNOWN";

    // Инициализируем переменные для сэмплирования кириллицы
    std::vector<size_t> histogram(256, 0);
    size_t cyrillic_total = 0;
    size_t unique_cyrillic = 0;
    const size_t TARGET_CYRILLIC_BYTES = 300; 

    // Пропускаем ASCII-шум, накапливаем кириллицу для анализа
    for (size_t i = 0; i < size; ++i) {
        unsigned char ubyte = static_cast<unsigned char>(data[i]);
        if (ubyte >= 128) {
            histogram[ubyte]++;
            cyrillic_total++;
            if (histogram[ubyte] == 1) {
                unique_cyrillic++;
            }
            if (cyrillic_total >= TARGET_CYRILLIC_BYTES) {
                break;
            }
        }
    }

    // Предохранители
    //if (cyrillic_total < 10) return "ASCII";
    //if (unique_cyrillic < 7) return "UNKNOWN";

    // Метод 1: Косинусное сходство
    double score_1251_cosine = calculate_similarity(histogram, cyrillic_total, map_cp1251, freq_russian);
    double score_koi8_cosine = calculate_similarity(histogram, cyrillic_total, map_koi8r, freq_russian);

    // Метод 2: Логарифмическое правдоподобие цепи Маркова
    double score_1251_markov = calculate_markov_score(data, size, map_cp1251, markov_russian);
    double score_koi8_markov = calculate_markov_score(data, size, map_koi8r, markov_russian);
    const double MARKOV_THRESHOLD = -4.5;
    const double cosine_threshold = get_cosine_threshold(cyrillic_total);


    if (verbose) {
    std::cout << "\n-------------------------------------------------------------\n";
    std::cout << "[Diagnostic] Результаты верификации однобайтовых гипотез:\n";
    std::cout << "-------------------------------------------------------------\n";
    std::cout << " Метрика анализа        |    Гипотеза CP1251   |   Гипотеза KOI8-R   \n";
    std::cout << "-------------------------------------------------------------\n";
    
    std::cout << " Косинусное сходство    |        " 
              << score_1251_cosine << "       |       " 
              << score_koi8_cosine << "       (Порог: >" << cosine_threshold << ")\n";
              
    std::cout << " Log-Likelihood Маркова |       " 
              << score_1251_markov << "       |      " 
              << score_koi8_markov << "       (Порог: >" << MARKOV_THRESHOLD << ")\n";
    std::cout << "-------------------------------------------------------------\n";


    }

    // Какую кодировку выбирает каждый метод сам по себе
    const std::string cos_pick    = (score_1251_cosine >= score_koi8_cosine) ? "CP1251" : "KOI8-R";
    const std::string markov_pick = (score_1251_markov >= score_koi8_markov) ? "CP1251" : "KOI8-R";
    const double cos_best    = std::max(score_1251_cosine, score_koi8_cosine);
    const double markov_best = std::max(score_1251_markov, score_koi8_markov);
    const bool cos_confident    = (cos_best > cosine_threshold) &&
                                  (std::fabs(score_1251_cosine - score_koi8_cosine) > 1e-9);
    const bool markov_confident = (markov_best > MARKOV_THRESHOLD);
    const bool conflict         = (cos_pick != markov_pick);

    if (cos_confident && !conflict) {
        return cos_pick;
    }

    if (markov_confident) {
        return markov_pick;
    }

    // 3) Ни один метод не уверен
    return "UNKNOWN";
}

void EncodingDetector::unicode_to_utf8_buf(uint32_t codepoint, std::vector<char>& out_buf) {
    if (codepoint <= 0x7F) {
        out_buf.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        out_buf.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        out_buf.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        out_buf.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        out_buf.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out_buf.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0x10FFFF) {
        out_buf.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        out_buf.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        out_buf.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out_buf.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        out_buf.insert(out_buf.end(), {'\xEF', '\xBF', '\xBD'}); 
    }
}

void EncodingDetector::transcode_to_utf8_buf(const char* raw_data, size_t len, const std::string& encoding, std::vector<char>& out_buf) const {
    if (encoding == "UTF-8" || encoding == "ASCII" || encoding == "UNKNOWN" || !is_initialized) {
        out_buf.insert(out_buf.end(), raw_data, raw_data + len);
        return;
    }

    const EncodingMap& map = (encoding == "CP1251") ? map_cp1251 : map_koi8r;
    
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = static_cast<unsigned char>(raw_data[i]);
        if (c < 128) {
            out_buf.push_back(raw_data[i]);
        } else {
            uint32_t unicode = map.byte_to_unicode[c];
            if (unicode != 0) {
                unicode_to_utf8_buf(unicode, out_buf);
            } else {
                out_buf.insert(out_buf.end(), {'\xEF', '\xBF', '\xBD'});
            }
        }
    }
}