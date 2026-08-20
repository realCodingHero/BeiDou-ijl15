cmake_minimum_required(VERSION 3.14)
cmake_policy(SET CMP0091 NEW)

project(BeiDouIjl15 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL$<$<CONFIG:Debug>:Debug>")

add_library(ijl15 SHARED
    ezorsia/AutoLogin.cpp
    ezorsia/BossHP.cpp
    ezorsia/HpMpAlert.cpp
    ezorsia/Client.cpp
    ezorsia/ijl15.cpp
    ezorsia/ZAllocEx.cpp
    ezorsia/dllmain.cpp
    ezorsia/Memory.cpp
    ezorsia/NMCO.cpp
    ezorsia/SelectCharMacFix.cpp
    ezorsia/stdafx.cpp
    ezorsia/ezorsia.rc
)

target_include_directories(ijl15 PRIVATE ezorsia)

target_compile_definitions(ijl15 PRIVATE
    WIN32
    NDEBUG
    EZORSIA_EXPORTS
    _WINDOWS
    _USRDLL
    _CRT_SECURE_NO_WARNINGS
)

target_compile_options(ijl15 PRIVATE
    /W3
    /EHsc
    /source-charset:gbk
    /execution-charset:gbk
)

target_link_libraries(ijl15 PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/detours/detours.lib"
    imm32
    ws2_32
    user32
    gdi32
    advapi32
    ole32
    oleaut32
)

set_target_properties(ijl15 PROPERTIES
    OUTPUT_NAME "ijl15"
)
