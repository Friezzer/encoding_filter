#include "FilterEngine.hpp"


bool FilterEngine::is_ascii_line(const std::string& line, std::string& out_reason) const {
    for (size_t i = 0; i < line.length(); ++i) {
        unsigned char c = line[i];
        if (c > 127) {
            out_reason = "найден не-ASCII байт (0x" + to_hex(c) + ") на позиции " + std::to_string(i + 1);
            return false;
        }
    }
    return true;
}

bool FilterEngine::check_block_swar(const char* data, size_t size, size_t& out_error_pos) const {
    size_t i = 0;
    const uint64_t mask = 0x8080808080808080ULL; // Маска старшего бита для 8 байт

    // Сканируем быстрыми блоками по 8 байт (64 бита) за одну инструкцию!
    for (; i + 8 <= size; i += 8) {
        uint64_t chunk;
        std::memcpy(&chunk, data + i, 8); // Безопасное копирование в регистр

        if ((chunk & mask) != 0) {
            // Если нашли плохой байт, локализуем его внутри этого блока
            for (size_t j = 0; j < 8; ++j) {
                unsigned char c = static_cast<unsigned char>(data[i + j]);
                if (c > 127) {
                    out_error_pos = i + j;
                    return false;
                }
            }
        }
    }

    // Остаток строки (меньше 8 байт) проверяем побайтово
    for (; i < size; ++i) {
        unsigned char c = static_cast<unsigned char>(data[i]);
        if (c > 127) {
            out_error_pos = i;
            return false;
        }
    }

    return true;
}

std::string FilterEngine::to_hex(unsigned char c) const {
    char buf[3];
    snprintf(buf, sizeof(buf), "%02X", c);
    return std::string(buf);
}

