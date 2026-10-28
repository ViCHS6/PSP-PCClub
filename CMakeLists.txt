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
# T042: общий модуль экранов и временные ленты до слияния A011.
target_sources(pcclub_render PRIVATE src/render/screen_tex.cpp)
if(NOT PSP)
    target_compile_definitions(pcclub_game PRIVATE PCCLUB_SCREEN_BENCH)
endif()
file(GENERATE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/screen_texture.py CONTENT [=[
import subprocess
import sys
from pathlib import Path
from PIL import Image, ImageDraw

source, output, converter, screen_id = sys.argv[1:]
source, output = Path(source), Path(output)
static = screen_id in ("desktop", "bsod", "off")
if source.is_file():
    with Image.open(source) as image:
        width, height = image.size
    frames = height // 64
    if width != 64 or height % 64 or (frames != 1 if static else not 4 <= frames <= 8):
        raise SystemExit(f"{source}: expected 64x64" if static else
                         f"{source}: expected 64x(64*N), N=4..8")
else:
    frames = 1 if static else 4
    colors = {
        "shooter": "#E04030", "rts": "#5E7A5A", "tbs": "#F0C020",
        "rpg": "#30C050", "sim": "#2F3A55", "race": "#7A5638",
        "action": "#6B4C30", "casual": "#D8D0B8", "desktop": "#4B6449",
        "loading": "#222222", "bsod": "#2050C0", "off": "#080808"
    }
    image = Image.new("RGB", (64, 64 * frames), colors[screen_id])
    draw = ImageDraw.Draw(image)
    for frame in range(frames):
        y = frame * 64
        if frames > 1:
            x = 4 + frame * 13
            draw.rectangle((4, y + 48, 59, y + 55), fill="#222222")
            draw.rectangle((x, y + 48, x + 12, y + 55), fill="#E8F0F0")
        elif screen_id != "off":
            for row in range(5):
                draw.rectangle((5, y + 6 + row * 7, 25 + row * 5, y + 8 + row * 7), fill="#E8F0F0")
    source = output.with_suffix(".png")
    image.save(source)
subprocess.run([sys.executable, converter, str(source), "-o", str(output),
                "--format", "t4", "--frames", str(frames)], check=True)
]=])
# CONFIGURE_DEPENDS замечает появление PNG после слияния A011 без ручной настройки.
file(GLOB SCREEN_SOURCE_PNGS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/textures/scr_*.png")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/screen_sources.txt" CONTENT "${SCREEN_SOURCE_PNGS}\n")
set(SCREEN_TEXTURE_OUTPUTS)
foreach(screen_id shooter rts tbs rpg sim race action casual desktop loading bsod off)
    set(screen_png "${CMAKE_CURRENT_SOURCE_DIR}/assets/textures/scr_${screen_id}.png")
    set(screen_dependencies)
    if(EXISTS "${screen_png}")
        list(APPEND screen_dependencies "${screen_png}")
    endif()
    set(screen_tex "${DEMO_ASSETS}/scr_${screen_id}.tex")
    add_custom_command(
        OUTPUT "${screen_tex}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${DEMO_ASSETS}"
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_BINARY_DIR}/screen_texture.py"
                "${screen_png}" "${screen_tex}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/convert_texture.py" "${screen_id}"
        DEPENDS tools/convert_texture.py "${CMAKE_CURRENT_BINARY_DIR}/screen_texture.py"
                "${CMAKE_CURRENT_BINARY_DIR}/screen_sources.txt" ${screen_dependencies}
        VERBATIM)
    list(APPEND SCREEN_TEXTURE_OUTPUTS "${screen_tex}")
endforeach()
add_custom_target(pcclub_screen_assets ALL DEPENDS ${SCREEN_TEXTURE_OUTPUTS})
add_dependencies(pcclub pcclub_screen_assets)

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
        set_tests_properties(pcclub_audio_tests PROPERTIES ENVIRONMENT "SDL_AUDIODRIVER=pcclub_no_audio_device")
    endif()
endif()

if(NOT PSP AND PCCLUB_TESTS)
    add_executable(pcclub_screen_tests tests/test_main.cpp
        tests/render/test_screen_tex.cpp src/render/screen_tex.cpp)
    target_compile_options(pcclub_screen_tests PRIVATE -Wall -Wextra)
    target_link_libraries(pcclub_screen_tests PRIVATE pcclub_core doctest::doctest)
    add_test(NAME pcclub_screen_tests COMMAND pcclub_screen_tests)
endif()
