cmake_minimum_required(VERSION 3.14)
cmake_policy(SET CMP0091 NEW)

project(BeiDouItemEff LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(KAENTAKE_ROOT "" CACHE PATH "Path to the existing kaentake source checkout (see README)")
if(NOT EXISTS "${KAENTAKE_ROOT}/src/pch.h")
    message(FATAL_ERROR "Set KAENTAKE_ROOT to the existing kaentake source checkout")
endif()
set(DETOURS_ROOT "${KAENTAKE_ROOT}/external/Detours")

add_library(ItemEffDetours STATIC
    "${DETOURS_ROOT}/src/creatwth.cpp"
    "${DETOURS_ROOT}/src/modules.cpp"
    "${DETOURS_ROOT}/src/detours.cpp"
    "${DETOURS_ROOT}/src/disolx86.cpp"
    "${DETOURS_ROOT}/src/disasm.cpp"
    "${DETOURS_ROOT}/src/image.cpp"
)
target_include_directories(ItemEffDetours PUBLIC "${DETOURS_ROOT}/src")

add_library(BeiDouItemEff SHARED
    dllmain.cpp
    avatar_storage.cpp
    cape_itemeff.cpp
    "${KAENTAKE_ROOT}/src/debug.cpp"
    "${KAENTAKE_ROOT}/src/hook.cpp"
)

target_include_directories(BeiDouItemEff PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${KAENTAKE_ROOT}/src"
    "${KAENTAKE_ROOT}/external/WzLib/include"
)

target_compile_definitions(BeiDouItemEff PRIVATE
    NOMINMAX
)

target_link_libraries(BeiDouItemEff PRIVATE
    ItemEffDetours
    ole32
    oleaut32
    psapi
    uuid
)

set_target_properties(BeiDouItemEff PROPERTIES
    OUTPUT_NAME "BeiDouItemEff"
)

option(ITEMEFF_BUILD_TESTS "Build the isolated COM lifetime regression fixture" ON)
if(ITEMEFF_BUILD_TESTS)
    add_library(ItemEffLifetimeFixture SHARED dllmain.cpp avatar_storage.cpp cape_itemeff.cpp
        "${KAENTAKE_ROOT}/src/debug.cpp" "${KAENTAKE_ROOT}/src/hook.cpp")
    target_include_directories(ItemEffLifetimeFixture PRIVATE "${CMAKE_CURRENT_LIST_DIR}"
        "${KAENTAKE_ROOT}/src" "${KAENTAKE_ROOT}/external/WzLib/include")
    target_compile_definitions(ItemEffLifetimeFixture PRIVATE NOMINMAX ITEMEFF_LIFECYCLE_TESTING)
    target_link_libraries(ItemEffLifetimeFixture PRIVATE ItemEffDetours ole32 oleaut32 psapi uuid)
    add_executable(ItemEffLifetimeTests "${CMAKE_CURRENT_LIST_DIR}/../tests/ItemEffLifetimeTests.cpp")
    target_compile_options(ItemEffLifetimeTests PRIVATE /UNDEBUG)
    target_link_libraries(ItemEffLifetimeTests PRIVATE ole32 uuid)
endif()
