#pragma once

#include <string>
#include <cstdint>

#define UTF8_ACCEPT 0
#define UTF8_REJECT 12

class EncodingDetector {
public:
    EncodingDetector() = default;

    // Метод проверки: является ли файл валидным UTF-8?
    bool is_valid_utf8_file(const std::string& filepath);

private:
    static const uint8_t utf8d[];
    
    static inline uint32_t decode(uint32_t* state, uint32_t* codep, uint32_t byte) {
        uint32_t type = utf8d[byte];

        *codep = (*state != UTF8_ACCEPT) ?
            (byte & 0x3fu) | (*codep << 6) :
            (0xff >> type) & (byte);

        *state = utf8d[256 + *state + type];
        return *state;
    }
};