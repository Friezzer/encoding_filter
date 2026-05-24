cmake_minimum_required(VERSION 3.12)
project(FilterProject LANGUAGES CXX)

# Настройка стандарта компиляции (C++17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Список исходных файлов проекта
set(PROJECT_SOURCES
    src/main.cpp
    src/Logger.cpp
    src/FilterEngine.cpp
    src/AppPipeline.cpp
    src/EncodingDetector.cpp
)

# Создание исполняемого файла
add_executable(filter_app ${PROJECT_SOURCES})

# Пути поиска заголовочных файлов (.hpp)
target_include_directories(filter_app PRIVATE include)

# Включение предупреждений компилятора для контроля качества кода
if(MSVC)
    target_compile_options(filter_app PRIVATE /W4)
else()
    target_compile_options(filter_app PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Копирование папки конфигураций config/ в директорию сборки
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/config DESTINATION ${CMAKE_CURRENT_BINARY_DIR})