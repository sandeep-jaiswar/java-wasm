cmake_minimum_required(VERSION 3.20)
project(java_wasm LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

option(WB_BUILD_TESTS "Build the core tests" ON)
option(WB_WASM "Build the browser bundle with Emscripten (run via emcmake)" OFF)

include(FetchContent)

# ---- third-party: tree-sitter runtime + Java grammar (vendored, MIT) -------
add_library(tree_sitter STATIC third_party/tree-sitter/lib/src/lib.c)
target_include_directories(tree_sitter
    PUBLIC third_party/tree-sitter/lib/include
    PRIVATE third_party/tree-sitter/lib/src)

add_library(tree_sitter_java STATIC third_party/tree-sitter-java/src/parser.c)
target_include_directories(tree_sitter_java PUBLIC third_party/tree-sitter-java/src)

# ---- third-party: nlohmann/json (MIT), fetched at a pinned commit ---------
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        098eebd412883ccb9d1a26523758e5ec457f58e0)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

# ---- core library: no GUI, no filesystem requirement ----------------------
add_library(wb_core STATIC
    src/core/catalog.cpp
    src/core/verify.cpp
    src/core/engine.cpp)
target_include_directories(wb_core PUBLIC src/core)
target_link_libraries(wb_core PUBLIC tree_sitter tree_sitter_java nlohmann_json::nlohmann_json)

# ---- tests (native only) ----------------------------------------------------
if(WB_BUILD_TESTS AND NOT WB_WASM)
    enable_testing()
    add_executable(test_core tests/test_core.cpp)
    target_link_libraries(test_core PRIVATE wb_core)
    add_test(NAME core COMMAND test_core ${CMAKE_SOURCE_DIR}/catalog/patterns.json)
endif()

# ---- browser bundle: wb.mjs + wb.wasm, served with web/ ---------------------
if(WB_WASM)
    if(NOT EMSCRIPTEN)
        message(FATAL_ERROR "WB_WASM=ON needs the Emscripten toolchain: run through emcmake")
    endif()
    add_executable(wb src/wasm/api.cpp)
    target_link_libraries(wb PRIVATE wb_core)
    set_target_properties(wb PROPERTIES SUFFIX ".mjs")
    target_compile_options(wb PRIVATE -fexceptions)
    target_link_options(wb PRIVATE
        -O2
        -fexceptions
        -sDISABLE_EXCEPTION_CATCHING=0
        -sMODULARIZE=1
        -sEXPORT_ES6=1
        -sENVIRONMENT=web
        -sALLOW_MEMORY_GROWTH=1
        -sEXPORTED_RUNTIME_METHODS=cwrap,ccall
        -sEXPORTED_FUNCTIONS=_wb_init,_wb_generate,_wb_check,_malloc,_free)
    set_target_properties(wb PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/dist)

    add_custom_command(TARGET wb POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/web ${CMAKE_BINARY_DIR}/dist
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/catalog ${CMAKE_BINARY_DIR}/dist/catalog)
endif()
