#include <iostream>
#include "AppPipeline.hpp"
#include <chrono>
int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cout << "Использование: " << argv[0] << " <вход> <выход> <лог>\n";
        return 1;
    }
    
    AppPipeline app(argv[1], argv[2], argv[3]);
    int start_time = std::chrono::steady_clock::now().time_since_epoch().count();
    if (app.run()) {
        int end_time = std::chrono::steady_clock::now().time_since_epoch().count();
        std::cout << "Время выполнения: " << (end_time - start_time) << " нс" << std::endl;
        return 0; // Успех
    }
    
    return 1; // Системная ошибка
}