#include <iostream>
#include "AppPipeline.hpp"
#include <chrono>
int main(int argc, char* argv[]) {
    //std::ios_base::sync_with_stdio(false);
    //std::cin.tie(nullptr); 
    if (argc < 4) {
        std::cout << "Использование: " << argv[0] << " <вход> <выход> <лог>\n";
        return 1;
    }
    
    AppPipeline app(argv[1], argv[2], argv[3]);
     auto start = std::chrono::high_resolution_clock::now();
    if (app.run()) {
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed = end - start;
        std::cout << "Время выполнения: " << elapsed.count() << " мс" << "\n";
        return 0; // Успех
    }
    
    return 1; // Системная ошибка
}