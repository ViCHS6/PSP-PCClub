cmake_minimum_required(VERSION 3.16)
project(pcclub LANGUAGES C CXX)

# Цель сборки определяет тулчейн: psp-cmake выставляет PSP=TRUE, обычный cmake — PC.
#   PC:  cmake -S . -B build/pc -G Ninja
#   PSP: psp-cmake -S . -B build/psp -G Ninja -DCMAKE_BUILD_TYPE=Release
if(PSP)
    set(PCCLUB_TARGET psp)
else()
    set(PCCLUB_TARGET pc)
endif()
message(STATUS "pcclub: цель сборки — ${PCCLUB_TARGET}")

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Общие флаги игрового кода (AGENTS.md, «Правила кода C++»).
add_library(pcclub_options INTERFACE)
target_compile_options(pcclub_options INTERFACE
    -Wall -Wextra
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>)
target_include_directories(pcclub_options INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# --- core: чистый C++, без платформенных зависимостей (контракт — T020, реализация — T022).
add_library(pcclub_core STATIC
    src/core/formulas.cpp
    src/core/data.cpp
    src/core/world.cpp
    src/core/visitors.cpp
    src/core/pcs.cpp
    src/core/commands.cpp
    src/core/conditions.cpp
    src/core/hooks_stub.cpp)
target_include_directories(pcclub_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(pcclub_core PRIVATE pcclub_options)

# --- Платформа, рендер, игра. OBJECT-библиотеки: на PSP объекты должны попасть в исполняемый
# файл целиком (PSP_MODULE_INFO не вызывается из кода, статическая библиотека его бы выкинула).
add_library(pcclub_platform OBJECT src/platform/${PCCLUB_TARGET}/platform_${PCCLUB_TARGET}.cpp)
add_library(pcclub_render OBJECT
    src/render/render_common.cpp
    src/render/mesh_format.cpp
    src/render/mesh_resources.cpp
    src/render/texture_format.cpp
    src/render/texture_resources.cpp
    src/render/render_${PCCLUB_TARGET}.cpp)
add_library(pcclub_game OBJECT
    src/game/debug_overlay.cpp
    src/game/demo_scene.cpp)

foreach(lib pcclub_platform pcclub_render pcclub_game)
    target_link_libraries(${lib} PRIVATE pcclub_options pcclub_core)
endforeach()

add_executable(pcclub src/main.cpp)
target_link_libraries(pcclub PRIVATE
    pcclub_options pcclub_core pcclub_platform pcclub_render pcclub_game)

# Демо-ресурсы лежат относительно приложения одинаково на PC и PSP.
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
set(DEMO_ASSETS ${CMAKE_CURRENT_BINARY_DIR}/assets)
file(GENERATE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/demo_texture.py CONTENT [=[
import sys
from PIL import Image
image = Image.new("RGBA", (16, 16))
image.putdata([(94, 65, 42, 255) if (x // 8 + y // 8) % 2 == 0
               else (122, 86, 56, 255) for y in range(16) for x in range(16)])
image.save(sys.argv[1])
]=])
add_custom_command(
    OUTPUT ${DEMO_ASSETS}/prop_desk_chipboard.msh
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DEMO_ASSETS}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/convert_model.py
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/placeholders/prop_desk_chipboard.obj
            -o ${DEMO_ASSETS}/prop_desk_chipboard.msh
    DEPENDS tools/convert_model.py assets/placeholders/prop_desk_chipboard.obj
            assets/placeholders/prop_desk_chipboard.mtl
    VERBATIM)
add_custom_command(
    OUTPUT ${DEMO_ASSETS}/demo_checker.tex
    BYPRODUCTS ${DEMO_ASSETS}/demo_checker.png
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DEMO_ASSETS}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/demo_texture.py
            ${DEMO_ASSETS}/demo_checker.png
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/convert_texture.py
            ${DEMO_ASSETS}/demo_checker.png -o ${DEMO_ASSETS}/demo_checker.tex --format t4
    DEPENDS tools/convert_texture.py ${CMAKE_CURRENT_BINARY_DIR}/demo_texture.py
    VERBATIM)
add_custom_target(pcclub_demo_assets ALL DEPENDS
    ${DEMO_ASSETS}/prop_desk_chipboard.msh ${DEMO_ASSETS}/demo_checker.tex)
add_dependencies(pcclub pcclub_demo_assets)

if(PSP)
    target_link_libraries(pcclub PRIVATE pspgu pspge pspdisplay pspctrl psppower)
    create_pbp_file(
        TARGET pcclub
        TITLE "Компьютерный клуб"
        VERSION 00.01
        MEMSIZE 0           # = PSP_LARGE_MEMORY=0: совместимость с PSP-1000
        BUILD_PRX           # PRX нужен для отладки через PSPLINK
    )
else()
    find_package(SDL2 REQUIRED)
    find_package(OpenGL REQUIRED)
    foreach(lib pcclub_platform pcclub_render)
        target_link_libraries(${lib} PRIVATE SDL2::SDL2)
    endforeach()
    target_link_libraries(pcclub PRIVATE SDL2::SDL2 OpenGL::GL)

    # --- Тесты (только PC)
    option(PCCLUB_TESTS "Собирать юнит-тесты" ON)
    if(PCCLUB_TESTS)
        include(FetchContent)
        FetchContent_Declare(doctest
            URL https://github.com/doctest/doctest/archive/refs/tags/v2.4.11.tar.gz
            URL_HASH SHA256=632ed2c05a7f53fa961381497bf8069093f0d6628c5f26286161fbd32a560186
            DOWNLOAD_EXTRACT_TIMESTAMP ON)
        FetchContent_MakeAvailable(doctest)

        enable_testing()
        add_executable(pcclub_tests
            tests/test_main.cpp
            tests/core/test_math.cpp
            tests/core/test_contract.cpp
            tests/core/test_sim_run.cpp
            tests/render/test_formats.cpp
            tests/render/test_resources.cpp
            src/render/render_common.cpp
            src/render/mesh_format.cpp
            src/render/texture_format.cpp
            src/render/mesh_resources.cpp
            src/render/texture_resources.cpp)
        target_compile_options(pcclub_tests PRIVATE -Wall -Wextra)
        target_link_libraries(pcclub_tests PRIVATE pcclub_core doctest::doctest)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/core/formulas.cpp")
            target_sources(pcclub_tests PRIVATE tests/core/test_formulas.cpp)
        else()
            add_library(pcclub_formula_tests_obj OBJECT tests/core/test_formulas.cpp)
            target_compile_options(pcclub_formula_tests_obj PRIVATE -Wall -Wextra)
            target_link_libraries(pcclub_formula_tests_obj PRIVATE pcclub_core doctest::doctest)
        endif()
        add_test(NAME pcclub_tests COMMAND pcclub_tests)
    endif()
endif()

# --- T025: автономный прогон ядра; тестовый пак явно отделён от игрового контента T026.
if(NOT PSP)
    add_executable(balance-runner src/tools/balance_runner.cpp src/tools/balance_bots.cpp)
    target_link_libraries(balance-runner PRIVATE pcclub_options pcclub_core)
    file(GLOB_RECURSE BALANCE_TEST_INPUTS CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/data_build/valid/*")
    set(BALANCE_TEST_PAK "${CMAKE_CURRENT_BINARY_DIR}/balance_test.pak")
    add_custom_command(
        OUTPUT "${BALANCE_TEST_PAK}"
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/build_data.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/data_build/valid" -o "${BALANCE_TEST_PAK}"
        DEPENDS ${BALANCE_TEST_INPUTS} tools/build_data.py src/core/data_types.h docs/formats/data_pak.md
        VERBATIM)
    add_custom_target(balance_test_data DEPENDS "${BALANCE_TEST_PAK}")
    add_dependencies(balance-runner balance_test_data)

    if(PCCLUB_TESTS)
        file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/balance_smoke.cmake" CONTENT [=[
file(MAKE_DIRECTORY "${OUT}")
foreach(strategy passive normal aggressive)
    foreach(repeat 1 2)
        execute_process(
            COMMAND "${RUNNER}" --days 10 --seed 1 --strategy "${strategy}"
                    --data "${DATA}" --out "${OUT}/${strategy}_${repeat}.csv"
            RESULT_VARIABLE result OUTPUT_VARIABLE log ERROR_VARIABLE error)
        if(NOT result STREQUAL "0")
            message(FATAL_ERROR "${strategy}: ${result}: ${log} ${error}")
        endif()
        file(STRINGS "${OUT}/${strategy}_${repeat}.csv" rows)
        list(LENGTH rows count)
        list(GET rows 0 header)
        if(NOT count EQUAL 11 OR NOT header STREQUAL
            "day,date,money,revenue,expenses,profit,visitors_arrived,visitors_served,visitors_lost,load_pct,satisfaction_avg,reputation,pcs_working,hung_events,broken_events,debt")
            message(FATAL_ERROR "Invalid CSV: ${strategy}, rows=${count}, header=${header}")
        endif()
        foreach(day RANGE 1 10)
            list(GET rows ${day} row)
            if(NOT row MATCHES "^${day},[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9],")
                message(FATAL_ERROR "Invalid CSV day: ${row}")
            endif()
        endforeach()
    endforeach()
    file(SHA256 "${OUT}/${strategy}_1.csv" first)
    file(SHA256 "${OUT}/${strategy}_2.csv" second)
    if(NOT first STREQUAL second)
        message(FATAL_ERROR "${strategy}: nondeterministic CSV")
    endif()
endforeach()
function(expect_error)
    execute_process(COMMAND "${RUNNER}" ${ARGN}
        RESULT_VARIABLE result OUTPUT_VARIABLE log ERROR_VARIABLE error)
    if(result STREQUAL "0" OR error STREQUAL "")
        message(FATAL_ERROR "Expected an error for ${ARGN}: ${log}")
    endif()
endfunction()
expect_error(--days 0)
expect_error(--days -1)
expect_error(--days 1.5)
expect_error(--days 2147483648)
expect_error(--days)
expect_error(--days 1 --days 2)
expect_error(--seed -1)
expect_error(--seed 4294967296)
expect_error(--seed 12x)
expect_error(--strategy unknown)
expect_error(--unknown 1)
expect_error(--data "${OUT}/missing.pak")
expect_error(--data "${DATA}" --out "${OUT}/missing/dir/output.csv")
file(SHA256 "${DATA}" original_data)
expect_error(--data "${DATA}" --out "${DATA}")
file(SHA256 "${DATA}" current_data)
if(NOT original_data STREQUAL current_data)
    message(FATAL_ERROR "Runner overwrote the input data")
endif()
execute_process(
    COMMAND "${PYTHON}" "${PLOT}" "${OUT}/passive_1.csv" "${OUT}/normal_1.csv"
            "${OUT}/aggressive_1.csv" -o "${OUT}/balance.html"
    RESULT_VARIABLE result OUTPUT_VARIABLE log ERROR_VARIABLE error)
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Plot: ${log} ${error}")
endif()
]=])
        add_test(NAME balance_runner_smoke COMMAND ${CMAKE_COMMAND}
            "-DRUNNER=$<TARGET_FILE:balance-runner>" "-DDATA=${BALANCE_TEST_PAK}"
            "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/balance_smoke" "-DPYTHON=${Python3_EXECUTABLE}"
            "-DPLOT=${CMAKE_CURRENT_SOURCE_DIR}/tools/plot_balance.py"
            -P "${CMAKE_CURRENT_BINARY_DIR}/balance_smoke.cmake")
        set_tests_properties(balance_runner_smoke PROPERTIES TIMEOUT 30)
        add_test(NAME balance_runner_90days COMMAND balance-runner --days 90 --seed 1
            --strategy normal --out "${CMAKE_CURRENT_BINARY_DIR}/balance_90.csv")
        set_tests_properties(balance_runner_90days PROPERTIES TIMEOUT 10)
    endif()
endif()
