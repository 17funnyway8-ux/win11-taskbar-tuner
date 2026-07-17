cmake_minimum_required(VERSION 3.20)
project(Win11TaskbarTuner VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows-specific settings
if(WIN32)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
    set(CMAKE_SYSTEM_VERSION 10.0)

    # 静态链接 C/C++ 运行时，避免分发时依赖 VCRUNTIME140_1.dll 等
    # MultiThreaded = /MT（Release），MultiThreadedDebug = /MTd（Debug）
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

    # 源文件以 UTF-8 保存，告诉 MSVC 按 UTF-8 解析 L"中文" 宽字符串
    # 避免 L"..." 字面量被错误地按系统 ANSI（中文=GBK）解析
    add_compile_options(/utf-8)
endif()

# Source files (explicit listing, not GLOB — CMake best practice)
set(SOURCES
    src/main.cpp
    src/taskbar_util.cpp
    src/config.cpp
    src/logger.cpp
)

set(HEADERS
    include/app.h
    include/config.h
    include/logger.h
)

# Main executable
add_executable(Win11TaskbarTuner WIN32
    ${SOURCES}
    ${HEADERS}
    resources/app.rc
)

target_include_directories(Win11TaskbarTuner PRIVATE
    include
)

# Unicode build (required for wWinMain and wide-string APIs)
target_compile_definitions(Win11TaskbarTuner PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_link_libraries(Win11TaskbarTuner PRIVATE
    user32
    shell32
    gdi32
    comctl32
    dwmapi
    advapi32
    shlwapi
    kernel32
)

# Windows subsystem for GUI app (no console window)
set_target_properties(Win11TaskbarTuner PROPERTIES
    WIN32_EXECUTABLE TRUE
)

# Install rules
install(TARGETS Win11TaskbarTuner
    RUNTIME DESTINATION bin
)
