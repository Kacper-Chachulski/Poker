cmake_minimum_required(VERSION 3.20)

project(poker_engine LANGUAGES CXX)

enable_testing()

add_library(poker_core
    src/card.cpp
    src/deck.cpp
    src/evaluator.cpp
    src/hand.cpp
)

target_compile_features(poker_core PRIVATE cxx_std_17)
target_include_directories(poker_core PUBLIC include)

add_executable(poker_benchmark
    src/main.cpp
)

target_compile_features(poker_benchmark PRIVATE cxx_std_17)
target_link_libraries(poker_benchmark PRIVATE poker_core)

add_executable(poker_tests
    tests/main.cpp
)

target_compile_features(poker_tests PRIVATE cxx_std_17)
target_link_libraries(poker_tests PRIVATE poker_core)

add_test(NAME poker_tests COMMAND poker_tests)

if(MSVC)
    target_compile_options(poker_core PRIVATE /W4)
    target_compile_options(poker_benchmark PRIVATE /W4)
    target_compile_options(poker_tests PRIVATE /W4)
else()
    target_compile_options(poker_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(poker_benchmark PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(poker_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32 AND MINGW)
    get_filename_component(MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)

    add_custom_command(TARGET poker_benchmark POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MINGW_BIN_DIR}/libgcc_s_seh-1.dll"
            "$<TARGET_FILE_DIR:poker_benchmark>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MINGW_BIN_DIR}/libstdc++-6.dll"
            "$<TARGET_FILE_DIR:poker_benchmark>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MINGW_BIN_DIR}/libwinpthread-1.dll"
            "$<TARGET_FILE_DIR:poker_benchmark>"
    )

    add_custom_command(TARGET poker_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MINGW_BIN_DIR}/libgcc_s_seh-1.dll"
            "$<TARGET_FILE_DIR:poker_tests>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MINGW_BIN_DIR}/libstdc++-6.dll"
            "$<TARGET_FILE_DIR:poker_tests>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MINGW_BIN_DIR}/libwinpthread-1.dll"
            "$<TARGET_FILE_DIR:poker_tests>"
    )
endif()
