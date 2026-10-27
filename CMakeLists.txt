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

# --- core: чистый C++, без платформенных зависимостей. Пока только заголовки-контракты (T020);
# реализацию добавит T022.
add_library(pcclub_core INTERFACE)
target_include_directories(pcclub_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)

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
            tests/render/test_formats.cpp
            tests/render/test_resources.cpp
            src/render/render_common.cpp
            src/render/mesh_format.cpp
            src/render/texture_format.cpp
            src/render/mesh_resources.cpp
            src/render/texture_resources.cpp)
        target_compile_options(pcclub_tests PRIVATE -Wall -Wextra)
        target_link_libraries(pcclub_tests PRIVATE pcclub_core doctest::doctest)
        add_test(NAME pcclub_tests COMMAND pcclub_tests)
    endif()
endif()
