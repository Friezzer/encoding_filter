#pragma once

#include <string>
#include <fstream>
#include <iostream>
class FilterEngine {
public:
    // Возвращает true, если строка содержит только ASCII (0..127)
    bool is_ascii_line(const std::string& line, std::string& out_reason) const;

private:
    // Вспомогательная функция для красивого вывода байта в шестнадцатеричном виде
    std::string to_hex(unsigned char c) const;
};
