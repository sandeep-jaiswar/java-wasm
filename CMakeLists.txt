cmake_minimum_required(VERSION 3.20)
project(java_wasm LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

option(WB_BUILD_TESTS "Build the core tests" ON)
option(WB_WASM "Build the browser bundle with Emscripten (run via emcmake)" OFF)
option(WB_OPTIMIZE_SIZE "WASM only: optimize for size (-Oz -flto) instead of speed (-O2)" OFF)
option(WB_LLM "Native only: build the llama.cpp text-completion module (wb_llm)" OFF)

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
set(WB_CORE_SOURCES
    src/core/catalog.cpp
    src/core/verify.cpp
    src/core/engine.cpp
    src/core/symbols.cpp
    src/core/session.cpp
    src/core/scaffold.cpp
    src/graph/graph.cpp
    src/graph/trace.cpp)
# Catalog::load reads a file; only native builds need it.
if(NOT WB_WASM)
    list(APPEND WB_CORE_SOURCES src/core/catalog_file.cpp)
endif()
add_library(wb_core STATIC ${WB_CORE_SOURCES})
target_include_directories(wb_core PUBLIC src/core)
target_link_libraries(wb_core PUBLIC tree_sitter tree_sitter_java nlohmann_json::nlohmann_json)

# ---- tests (native only) ----------------------------------------------------
if(WB_BUILD_TESTS AND NOT WB_WASM)
    enable_testing()
    add_executable(test_core tests/test_core.cpp)
    target_link_libraries(test_core PRIVATE wb_core)
    add_test(NAME core COMMAND test_core ${CMAKE_SOURCE_DIR}/catalog/patterns.json)

    add_executable(test_symbols tests/test_symbols.cpp)
    target_link_libraries(test_symbols PRIVATE wb_core)
    add_test(NAME symbols COMMAND test_symbols)
    add_executable(test_graph tests/test_graph.cpp)
    target_include_directories(test_graph PRIVATE src/graph)
    target_link_libraries(test_graph PRIVATE wb_core)
    add_test(NAME graph COMMAND test_graph)
    add_executable(test_scaffold tests/test_scaffold.cpp)
    target_link_libraries(test_scaffold PRIVATE wb_core)
    add_test(NAME scaffold COMMAND test_scaffold)
endif()

# ---- optional: llama.cpp text completion (MIT), native only ---------------
# Off by default. The core and the WASM bundle do not depend on it. Model weights are not
# bundled: pass a GGUF path at run time.
if(WB_LLM)
    if(WB_WASM)
        message(FATAL_ERROR "WB_LLM is native only for now; the WASM build does not include it")
    endif()
    set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_COMMON OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    # GPU offload needs the CUDA toolkit (nvcc). Off by default so CPU-only machines build.
    option(WB_LLM_CUDA "llama.cpp with CUDA (needs nvcc)" OFF)
    set(GGML_CUDA ${WB_LLM_CUDA} CACHE BOOL "" FORCE)
    # Pinned to the commit behind tag b12547. A commit hash cannot be moved, unlike a tag.
    FetchContent_Declare(llama
        GIT_REPOSITORY https://github.com/ggml-org/llama.cpp.git
        GIT_TAG        4db8b7638f2fa2e5c092ee05d49999b56e8167c3)
    FetchContent_MakeAvailable(llama)

    add_library(wb_llm STATIC src/llm/llm.cpp)
    target_include_directories(wb_llm PUBLIC src/llm)
    target_link_libraries(wb_llm PUBLIC wb_core llama)

    if(WB_BUILD_TESTS)
        enable_testing()
        add_executable(test_llm tests/test_llm.cpp)
        target_link_libraries(test_llm PRIVATE wb_llm)
        set(WB_LLM_TEST_MODEL "" CACHE FILEPATH "GGUF model for test_llm (test is skipped when empty)")
        add_test(NAME llm COMMAND test_llm ${WB_LLM_TEST_MODEL})
    endif()
endif()

# ---- browser bundle: wb.mjs + wb.wasm, served with web/ ---------------------
if(WB_WASM)
    if(NOT EMSCRIPTEN)
        message(FATAL_ERROR "WB_WASM=ON needs the Emscripten toolchain: run through emcmake")
    endif()
    # Size mode compiles and links with LTO so the core, the API and tree-sitter optimize together.
    # Default mode keeps the previous behavior: Release compile flags, -O2 at link only.
    set(WB_LINK_OPT -O2)
    set(WB_COMPILE_OPT "")
    if(WB_OPTIMIZE_SIZE)
        set(WB_LINK_OPT -Oz -flto)
        set(WB_COMPILE_OPT -Oz -flto)
        target_compile_options(tree_sitter PRIVATE ${WB_COMPILE_OPT})
        target_compile_options(tree_sitter_java PRIVATE ${WB_COMPILE_OPT})
        target_compile_options(wb_core PRIVATE ${WB_COMPILE_OPT} -fexceptions)
    endif()

    add_executable(wb src/wasm/api.cpp)
    target_link_libraries(wb PRIVATE wb_core)
    set_target_properties(wb PROPERTIES SUFFIX ".mjs")
    target_compile_options(wb PRIVATE -fexceptions)
    if(WB_OPTIMIZE_SIZE)
        target_compile_options(wb PRIVATE ${WB_COMPILE_OPT})
    endif()
    target_link_options(wb PRIVATE
        ${WB_LINK_OPT}
        -fexceptions
        -sDISABLE_EXCEPTION_CATCHING=0
        -sMODULARIZE=1
        -sEXPORT_ES6=1
        -sENVIRONMENT=web
        -sFILESYSTEM=0
        -sALLOW_MEMORY_GROWTH=1
        -sEXPORTED_RUNTIME_METHODS=cwrap,ccall,stringToUTF8,lengthBytesUTF8,UTF8ToString
        -sEXPORTED_FUNCTIONS=_wb_init,_wb_generate,_wb_check,_wb_symbols,_malloc,_free)
    set_target_properties(wb PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/dist)

    add_custom_command(TARGET wb POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/web ${CMAKE_BINARY_DIR}/dist
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/catalog ${CMAKE_BINARY_DIR}/dist/catalog)
endif()
