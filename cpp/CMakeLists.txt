cmake_minimum_required(VERSION 3.20)

project(poker_engine LANGUAGES CXX)

enable_testing()

function(copy_mingw_runtime target_name)
    if(WIN32 AND MINGW)
        get_filename_component(MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)

        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libgcc_s_seh-1.dll"
                "$<TARGET_FILE_DIR:${target_name}>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libstdc++-6.dll"
                "$<TARGET_FILE_DIR:${target_name}>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libwinpthread-1.dll"
                "$<TARGET_FILE_DIR:${target_name}>"
        )
    endif()
endfunction()

add_library(poker_core
    src/card.cpp
    src/deck.cpp
    src/exact_equity.cpp
    src/equity.cpp
    src/evaluator.cpp
    src/ev.cpp
    src/hand.cpp
    src/range.cpp
)

target_compile_features(poker_core PRIVATE cxx_std_17)
target_include_directories(poker_core PUBLIC include)

add_executable(poker
    src/main.cpp
)

target_compile_features(poker PRIVATE cxx_std_17)
target_link_libraries(poker PRIVATE poker_core)

add_executable(poker_tests
    tests/main.cpp
)

target_compile_features(poker_tests PRIVATE cxx_std_17)
target_link_libraries(poker_tests PRIVATE poker_core)

add_test(NAME poker_tests COMMAND poker_tests)

add_executable(poker_equity_benchmark
    src/equity_benchmark.cpp
)

target_compile_features(poker_equity_benchmark PRIVATE cxx_std_17)
target_link_libraries(poker_equity_benchmark PRIVATE poker_core)

add_executable(poker_exact_benchmark
    src/exact_benchmark.cpp
)

target_compile_features(poker_exact_benchmark PRIVATE cxx_std_17)
target_link_libraries(poker_exact_benchmark PRIVATE poker_core)

if(MSVC)
    target_compile_options(poker_core PRIVATE /W4)
    target_compile_options(poker PRIVATE /W4)
    target_compile_options(poker_tests PRIVATE /W4)
    target_compile_options(poker_equity_benchmark PRIVATE /W4)
    target_compile_options(poker_exact_benchmark PRIVATE /W4)
else()
    target_compile_options(poker_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(poker PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(poker_tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(poker_equity_benchmark PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(poker_exact_benchmark PRIVATE -Wall -Wextra -Wpedantic)
endif()

copy_mingw_runtime(poker)
copy_mingw_runtime(poker_tests)
copy_mingw_runtime(poker_equity_benchmark)
copy_mingw_runtime(poker_exact_benchmark)
