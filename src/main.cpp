#include <iostream>
#include "AppPipeline.hpp"
#include <chrono>
#include <cstdlib>
int main(int argc, char* argv[]) {
    //std::ios_base::sync_with_stdio(false);
    //std::cin.tie(nullptr); 
    if (argc < 4) {
        std::cout << "Использование: " << argv[0] << " <вход> <выход> <лог> [размер_блока]\n";
        std::cout << "  Без [размер_блока] — обычная обработка файла.\n";
        std::cout << "  С [размер_блока]   — поблочный анализ кодировки (например, 100).\n";
        return 1;
    }
    
    AppPipeline app(argv[1], argv[2], argv[3]);
    auto start = std::chrono::high_resolution_clock::now();

    // Если передан 4-й аргумент - размер блока, запускаем поблочный анализ кодировки.
    bool ok;
    if (argc >= 5) {
        size_t chunk_size = static_cast<size_t>(std::strtoul(argv[4], nullptr, 10));
        ok = app.run_chunk_analysis(chunk_size);
    } else {
        ok = app.run();
    }

    if (ok) {
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed = end - start;
        std::cout << "Время выполнения: " << elapsed.count() << " мс" << "\n";
        return 0; 
    }
    
    return 1;
}