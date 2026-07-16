#pragma once

#include <cstring>
#include <fstream>
#include <iostream>
#include <cstdint>
#include <memory>
class FilterEngine {
public:
    // Возвращает true, если строка содержит только ASCII 
    bool is_ascii_line(const std::string& line, std::string& out_reason) const;
    // оптимизированная проверка блоками по 8 байт
    bool check_block_swar(const char* data, size_t size, size_t& out_error_pos) const;

private:

    std::string to_hex(unsigned char c) const;
};
