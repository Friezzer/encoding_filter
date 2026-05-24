#include "FilterEngine.hpp"
#include <iostream>

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

std::string FilterEngine::to_hex(unsigned char c) const {
    char buf[3];
    snprintf(buf, sizeof(buf), "%02X", c);
    return std::string(buf);
}

