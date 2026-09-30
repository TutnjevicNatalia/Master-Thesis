cmake_minimum_required(VERSION 3.15)
project(VoiceQC CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra)
endif()

add_executable(VoiceQC
    src/main.cpp
    src/WavReader.cpp
    src/ReverberationEstimator.cpp
)

target_include_directories(VoiceQC PRIVATE include)
