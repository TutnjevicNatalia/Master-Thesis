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

# WaveformViewer is a plain Win32 GUI app (File > Open a WAV file, see
# its waveform drawn with GDI). It only builds on Windows, since it uses
# the Win32 API directly, so it is skipped entirely on Mac/Linux.
if(WIN32)
    add_executable(WaveformViewer WIN32
        apps/WaveformViewer/main.cpp
        src/WavReader.cpp
    )
    target_include_directories(WaveformViewer PRIVATE include)
    target_link_libraries(WaveformViewer PRIVATE user32 gdi32 comdlg32)
endif()
