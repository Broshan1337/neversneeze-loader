cmake_minimum_required(VERSION 3.21)
project(NeversneezeLoader LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_add_executable(Loader
    src/main.cpp
    src/Icons.cpp
    src/StatusCard.cpp
    src/Injector.cpp
    src/MainWindow.cpp
)

qt_add_resources(Loader "assets"
    PREFIX "/assets"
    BASE "assets"
    FILES assets/steam.png assets/cs2.png assets/chevron.png
)

target_include_directories(Loader PRIVATE src)
target_link_libraries(Loader PRIVATE Qt6::Widgets)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
