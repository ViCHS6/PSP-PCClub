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
    src/core/events.cpp
    src/core/hooks_stub.cpp)
target_include_directories(pcclub_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(pcclub_core PRIVATE pcclub_options)

# --- Платформа, рендер, игра. OBJECT-библиотеки: на PSP объекты должны попасть в исполняемый
# файл целиком (PSP_MODULE_INFO не вызывается из кода, статическая библиотека его бы выкинула).
add_library(pcclub_platform OBJECT src/platform/${PCCLUB_TARGET}/platform_${PCCLUB_TARGET}.cpp)
target_sources(pcclub_platform PRIVATE src/platform/audio_mixer.cpp
    src/platform/${PCCLUB_TARGET}/audio_${PCCLUB_TARGET}.cpp)
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

# --- ui: ОС администратора (T030, контракт — src/ui/ui_api.h).
add_library(pcclub_ui OBJECT
    src/ui/ui_common.cpp
    src/ui/ui.cpp
    src/ui/demo_app.cpp)

foreach(lib pcclub_platform pcclub_render pcclub_game pcclub_ui)
    target_link_libraries(${lib} PRIVATE pcclub_options pcclub_core)
endforeach()

add_executable(pcclub src/main.cpp)
target_link_libraries(pcclub PRIVATE
    pcclub_options pcclub_core pcclub_platform pcclub_render pcclub_game pcclub_ui)

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

# T041: исходники заглушки и бинарные ресурсы создаются только в build/.
target_sources(pcclub_render PRIVATE src/render/anim.cpp src/render/anim_format.cpp)
set(ANIM_PARTS pelvis torso head upper_arm_l forearm_l upper_arm_r forearm_r thigh_l shin_l thigh_r shin_r)
set(ANIM_OUTPUTS ${DEMO_ASSETS}/chr_placeholder.anm)
set(ANIM_BYPRODUCTS ${DEMO_ASSETS}/chr_placeholder.anim.json)
foreach(part IN LISTS ANIM_PARTS)
    list(APPEND ANIM_OUTPUTS ${DEMO_ASSETS}/chr_placeholder_${part}.msh)
    list(APPEND ANIM_BYPRODUCTS ${DEMO_ASSETS}/chr_placeholder_${part}.obj)
endforeach()
foreach(clip walk sit_down type)
    list(APPEND ANIM_BYPRODUCTS ${DEMO_ASSETS}/chr_placeholder_${clip}.anim.json
        ${DEMO_ASSETS}/chr_placeholder_${clip}.anm)
endforeach()
add_custom_command(
    OUTPUT ${ANIM_OUTPUTS}
    BYPRODUCTS ${ANIM_BYPRODUCTS}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_anim_placeholder.py -o ${DEMO_ASSETS}
    DEPENDS tools/gen_anim_placeholder.py tools/convert_anim.py tools/convert_model.py
    VERBATIM)
add_custom_target(pcclub_anim_assets ALL DEPENDS ${ANIM_OUTPUTS})
add_dependencies(pcclub pcclub_anim_assets)
file(GENERATE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/demo_audio.py CONTENT [=[
import math
import os
import struct
import sys
import wave

rate = 22050
def write(name, samples):
    with wave.open(os.path.join(sys.argv[1], name), "wb") as out:
        out.setparams((1, 2, rate, 0, "NONE", "not compressed"))
        out.writeframes(b"".join(struct.pack("<h", round(x)) for x in samples))

write("sfx_click.wav", (
    11000 * math.sin(2 * math.pi * 1400 * i / rate)
    * min(1, i / (rate * 0.005)) * max(0, 1 - i / (rate * 0.08)) ** 3
    for i in range(round(rate * 0.08))))

notes = (220, 261.6256, 329.6276, 440, 329.6276, 261.6256, 196, 261.6256)
def music():
    for i in range(rate * 8):
        beat = i // (rate // 2)
        t = (i % (rate // 2)) / rate
        envelope = min(1, t / 0.01) * min(1, (0.5 - t) / 0.02) * math.exp(-3 * t)
        arp = math.sin(2 * math.pi * notes[beat % len(notes)] * t)
        chord = sum(math.sin(2 * math.pi * hz * i / rate) for hz in (110, 165, 220)) / 3
        edge = min(1, i / (rate * 0.02), (rate * 8 - 1 - i) / (rate * 0.02))
        yield (arp * envelope * 4400 + chord * 1200) * edge
write("music_demo.wav", music())
]=])
add_custom_command(
    OUTPUT ${DEMO_ASSETS}/sfx_click.wav ${DEMO_ASSETS}/music_demo.wav
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DEMO_ASSETS}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/demo_audio.py ${DEMO_ASSETS}
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/demo_audio.py
    VERBATIM)
add_custom_target(pcclub_demo_audio ALL DEPENDS
    ${DEMO_ASSETS}/sfx_click.wav ${DEMO_ASSETS}/music_demo.wav)
add_dependencies(pcclub pcclub_demo_audio)

# T030: шрифты UI (FNT1 + атлас TEX1 t4, docs/formats/ui.md §4) и data.pak со строками.
set(UI_FONT_OUTPUTS)
foreach(font font_ui12 font_mono12 font_ui14 font_ui14b)
    add_custom_command(
        OUTPUT ${DEMO_ASSETS}/${font}.tex ${DEMO_ASSETS}/${font}.fnt
        COMMAND ${CMAKE_COMMAND} -E make_directory ${DEMO_ASSETS}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/convert_texture.py
                ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/${font}.png -o ${DEMO_ASSETS}/${font}.tex --format t4
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/${font}.fnt ${DEMO_ASSETS}/${font}.fnt
        DEPENDS tools/convert_texture.py assets/fonts/${font}.png assets/fonts/${font}.fnt
        VERBATIM)
    list(APPEND UI_FONT_OUTPUTS ${DEMO_ASSETS}/${font}.tex ${DEMO_ASSETS}/${font}.fnt)
endforeach()
file(GLOB PCCLUB_DATA_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/data/*.json ${CMAKE_CURRENT_SOURCE_DIR}/data/*.csv
    ${CMAKE_CURRENT_SOURCE_DIR}/data/events/*.json)
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/data.pak
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_data.py
            ${CMAKE_CURRENT_SOURCE_DIR}/data -o ${CMAKE_CURRENT_BINARY_DIR}/data.pak
    DEPENDS tools/build_data.py ${PCCLUB_DATA_SOURCES}
    VERBATIM)
add_custom_target(pcclub_ui_assets ALL DEPENDS ${UI_FONT_OUTPUTS} ${CMAKE_CURRENT_BINARY_DIR}/data.pak)
add_dependencies(pcclub pcclub_ui_assets)

if(PSP)
    target_link_libraries(pcclub PRIVATE pspaudio)
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
            tests/core/test_events.cpp
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
        target_sources(pcclub_tests PRIVATE tests/render/test_anim.cpp
            src/render/anim.cpp src/render/anim_format.cpp)
        add_test(NAME convert_anim_tests COMMAND ${Python3_EXECUTABLE} -B -m unittest
            discover -s ${CMAKE_CURRENT_SOURCE_DIR}/tools/tests -p test_convert_anim.py)
        add_executable(pcclub_audio_tests tests/test_main.cpp tests/platform/test_audio_mixer.cpp
            src/platform/audio_mixer.cpp src/platform/pc/platform_pc.cpp src/platform/pc/audio_pc.cpp)
        target_compile_options(pcclub_audio_tests PRIVATE -Wall -Wextra)
        target_link_libraries(pcclub_audio_tests PRIVATE pcclub_core doctest::doctest SDL2::SDL2)
        add_dependencies(pcclub_audio_tests pcclub_demo_audio)
        add_test(NAME pcclub_audio_tests COMMAND pcclub_audio_tests)
        target_sources(pcclub_tests PRIVATE tests/ui/test_ui_common.cpp src/ui/ui_common.cpp)
        target_compile_definitions(pcclub_tests PRIVATE
            PCCLUB_FONTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts")
        # Поведение ОС: ui.cpp с заглушками render/platform/audio внутри теста.
        add_executable(pcclub_ui_tests tests/test_main.cpp tests/ui/test_ui_flow.cpp
            src/ui/ui.cpp src/ui/ui_common.cpp)
        target_compile_options(pcclub_ui_tests PRIVATE -Wall -Wextra)
        target_compile_definitions(pcclub_ui_tests PRIVATE
            PCCLUB_FONTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts")
        target_link_libraries(pcclub_ui_tests PRIVATE pcclub_core doctest::doctest)
        add_test(NAME pcclub_ui_tests COMMAND pcclub_ui_tests)
        set_tests_properties(pcclub_audio_tests PROPERTIES ENVIRONMENT "SDL_AUDIODRIVER=pcclub_no_audio_device")
    endif()
endif()
