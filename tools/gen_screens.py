#!/usr/bin/env python3
"""A011: процедурные экраны 64×64; только stdlib и Pillow.

py -3.12 tools/gen_screens.py
py -3.12 tools/gen_screens.py -o build/screens

Без -o: ленты в assets/textures/, таблица в assets/previews/scr_sheet.png.
С -o: все PNG, включая scr_sheet.png, только в указанной папке.
Анимированные ленты: 8 кадров, проигрывание 4 кадра/с в T042.
"""

import argparse
from pathlib import Path
import random

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
SIZE = 64
SEED = 11092001
FRAME_COUNTS = {
    "scr_shooter": 8, "scr_rts": 8, "scr_tbs": 8, "scr_rpg": 8,
    "scr_sim": 8, "scr_race": 8, "scr_action": 8, "scr_casual": 8,
    "scr_desktop": 1, "scr_loading": 8, "scr_bsod": 1, "scr_off": 1,
}

# ART.md §2; первый цвет — дополнительный тёмный оттенок кинескопа.
PALETTE = (
    "101416", "222222", "2F3A55", "4B6449", "5E7A5A", "5E412A",
    "8A6440", "8F8670", "B8AE94", "D9D2BC", "FFD9A0", "E8F0F0",
    "2050C0", "30C050", "E04030", "F0C020",
)
BLACK, DARK, NAVY, FOREST, GRASS, EARTH, WOOD, GRAY = range(8)
BEIGE, CREAM, WARM, WHITE, BLUE, GREEN, RED, GOLD = range(8, 16)
BOB = (0, 1, 2, 1, 0, -1, -2, -1)
ORBIT = ((0, -3), (2, -2), (3, 0), (2, 2),
         (0, 3), (-2, 2), (-3, 0), (-2, -2))


def canvas(color):
    image = Image.new("P", (SIZE, SIZE), color)
    image.putpalette([channel for color in PALETTE for channel in bytes.fromhex(color)])
    return image, ImageDraw.Draw(image)


def noise_text(draw, x, y, width, rows, color, seed=SEED):
    rng = random.Random(seed)
    for row in range(rows):
        cursor = x
        while cursor < x + width - 2:
            length = min(rng.randrange(1, 5), x + width - cursor)
            draw.line((cursor, y + row * 3, cursor + length - 1, y + row * 3), fill=color)
            cursor += length + rng.randrange(2, 4)


def bar(draw, box, color, amount):
    x, y, width = box
    draw.rectangle((x, y, x + width, y + 4), fill=BLACK, outline=GRAY)
    if amount:
        draw.rectangle((x + 1, y + 1, x + amount, y + 3), fill=color)


def person(draw, x, y, color, step=0):
    draw.rectangle((x - 1, y - 8, x + 1, y - 6), fill=WARM)
    draw.rectangle((x - 2, y - 5, x + 2, y), fill=color)
    draw.line((x - 1, y, x - 2 - step, y + 4), fill=DARK, width=2)
    draw.line((x + 1, y, x + 2 + step, y + 4), fill=DARK, width=2)


def shooter(frame):
    image, d = canvas(EARTH)
    d.polygon(((0, 0), (64, 0), (43, 18), (21, 18)), fill=GRAY)
    d.polygon(((0, 0), (21, 18), (21, 39), (0, 59)), fill=WOOD)
    d.polygon(((64, 0), (43, 18), (43, 39), (64, 59)), fill=BEIGE)
    d.rectangle((22, 18, 42, 39), fill=GRAY)
    d.rectangle((27, 21, 36, 39), fill=NAVY)
    for y in (9, 19, 29, 39):
        d.line((0, y, 20, 19 + y // 2), fill=EARTH)
    d.polygon(((0, 59), (21, 39), (43, 39), (64, 59), (64, 64), (0, 64)), fill=GRAY)
    for x in (6, 27, 49):
        d.line((32, 37, x, 64), fill=BEIGE)
    dx, dy = ORBIT[frame]
    person(d, 31 + dx, 34 + dy // 2, RED, frame % 2)
    d.line((25, 28, 29, 28), fill=GREEN)
    d.line((35, 28, 39, 28), fill=GREEN)
    d.line((32, 22, 32, 25), fill=GREEN)
    d.line((32, 31, 32, 34), fill=GREEN)
    recoil = (0, 2, 3, 1, 0, 1, 2, 1)[frame]
    d.polygon(((33, 64), (36, 45 + recoil), (42, 35 + recoil),
               (47, 35 + recoil), (51, 48), (62, 59), (62, 64)), fill=DARK)
    d.polygon(((39, 49 + recoil), (43, 37 + recoil), (46, 37 + recoil),
               (47, 50 + recoil)), fill=GRAY)
    d.polygon(((47, 55), (53, 49), (60, 58), (60, 64), (50, 64)), fill=WOOD)
    if frame in (1, 2, 5):
        d.polygon(((44, 24), (47, 32), (54, 30), (49, 37),
                   (51, 42), (44, 39), (38, 42), (40, 35), (35, 30), (42, 31)), fill=GOLD)
        d.rectangle((42, 32, 46, 37), fill=WHITE)
    d.rectangle((0, 57, 26, 63), fill=BLACK)
    bar(d, (3, 59, 19), RED, 15)
    return image


def rts(frame):
    image, d = canvas(GRASS)
    rng = random.Random(SEED)
    for _ in range(50):
        x, y = rng.randrange(64), rng.randrange(52)
        d.rectangle((x, y, x + 1, y + 1), fill=FOREST)
    d.polygon(((0, 4), (17, 8), (27, 26), (48, 35), (64, 34),
               (64, 43), (43, 42), (19, 31), (11, 16), (0, 13)), fill=NAVY)
    d.line((0, 6, 14, 11, 24, 28, 45, 38, 63, 38), fill=BLUE, width=3)
    d.polygon(((0, 44), (17, 25), (43, 11), (47, 15), (21, 30), (6, 48)), fill=BEIGE)
    d.rectangle((17, 27, 27, 33), fill=WOOD, outline=EARTH)
    d.rectangle((41, 8, 56, 21), fill=EARTH)
    d.polygon(((39, 9), (48, 3), (58, 9)), fill=RED)
    d.rectangle((45, 11, 50, 19), fill=BEIGE)
    for x, y in ((5, 20), (9, 43), (53, 44), (58, 26)):
        d.rectangle((x, y, x + 4, y + 4), fill=EARTH)
        d.polygon(((x - 2, y + 1), (x + 2, y - 7), (x + 6, y + 1)), fill=FOREST)
    dx, dy = ORBIT[frame]
    for x, y in ((28, 19), (32, 26), (37, 22)):
        d.ellipse((x + dx - 2, y + dy - 1, x + dx + 3, y + dy + 3), fill=BLACK)
        d.rectangle((x + dx - 1, y + dy - 2, x + dx + 2, y + dy + 1), fill=BLUE)
        d.point((x + dx, y + dy - 2), fill=WHITE)
    d.rectangle((22 + dx, 13 + dy, 42 + dx, 32 + dy), outline=GREEN)
    d.line((44, 24, 50 + BOB[frame], 29), fill=RED, width=2)
    d.rectangle((0, 51, 63, 63), fill=DARK, outline=GRAY)
    d.rectangle((3, 54, 15, 61), fill=FOREST, outline=BEIGE)
    d.point((7, 57), fill=BLUE)
    d.point((12, 55), fill=RED)
    for x in (20, 30, 40):
        d.rectangle((x, 54, x + 7, 61), fill=WOOD, outline=BEIGE)
    bar(d, (50, 55, 10), GREEN, 7)
    return image


def tbs(frame):
    image, d = canvas(GRASS)
    for row in range(7):
        for col in range(5):
            x, y = col * 12 + (row % 2) * 6 - 6, row * 9 - 1
            d.polygon(((x, y + 4), (x + 3, y), (x + 9, y),
                       (x + 12, y + 4), (x + 9, y + 9), (x + 3, y + 9)), outline=FOREST)
    for x, y in ((5, 6), (30, 5), (7, 44), (34, 46)):
        d.polygon(((x, y + 7), (x + 4, y - 1), (x + 9, y + 7)), fill=FOREST)
    d.rectangle((28, 12, 41, 23), fill=GRAY)
    for x in (28, 33, 38):
        d.rectangle((x, 8, x + 3, 16), fill=BEIGE)
    d.rectangle((32, 17, 36, 23), fill=BLACK)
    dx, dy = ORBIT[frame]
    x, y = 19 + dx, 31 + dy
    d.ellipse((x - 9, y + 5, x + 9, y + 10), outline=GOLD, width=2)
    d.rectangle((x - 7, y, x + 5, y + 5), fill=WOOD)
    d.polygon(((x + 3, y + 2), (x + 4, y - 6), (x + 8, y - 9),
               (x + 10, y - 4), (x + 7, y - 3), (x + 7, y + 3)), fill=BEIGE)
    d.line((x - 5, y + 4, x - 6 - frame % 2, y + 9), fill=DARK, width=2)
    d.line((x + 4, y + 4, x + 6 + frame % 2, y + 9), fill=DARK, width=2)
    d.line((x - 7, y + 1, x - 11, y + 5), fill=EARTH, width=2)
    d.polygon(((x - 3, y - 9), (x + 2, y - 8), (x + 2, y), (x - 6, y)), fill=BLUE)
    d.rectangle((x - 2, y - 13, x + 1, y - 10), fill=WARM)
    d.line((x + 2, y - 16, x + 2, y - 2), fill=CREAM)
    d.polygon(((x + 3, y - 16), (x + 10, y - 14 + frame % 3), (x + 3, y - 11)), fill=RED)
    d.rectangle((46, 0, 63, 63), fill=EARTH, outline=BEIGE)
    d.rectangle((49, 4, 60, 18), fill=NAVY, outline=GOLD)
    d.ellipse((53, 6, 57, 10), fill=WARM)
    d.polygon(((54, 10), (50, 16), (59, 16)), fill=BLUE)
    for y in (24, 34, 44):
        d.rectangle((50, y, 59, y + 6), fill=GRAY, outline=BEIGE)
    d.polygon(((53, 54), (59, 57), (53, 60)), fill=GOLD)
    return image


def rpg(frame):
    image, d = canvas(FOREST)
    for row in range(-2, 7):
        for col in range(-3, 7):
            x, y = 30 + (col - row) * 10, 8 + (col + row) * 5
            d.polygon(((x, y), (x + 10, y + 5), (x, y + 10), (x - 10, y + 5)),
                      fill=WOOD if (row + col) % 2 else EARTH, outline=GRAY)
    for x, y in ((7, 13), (55, 15), (5, 40), (59, 46)):
        d.rectangle((x - 1, y, x + 2, y + 7), fill=EARTH)
        d.polygon(((x - 7, y + 1), (x, y - 12), (x + 7, y + 1)), fill=FOREST)
        d.line((x - 4, y - 2, x, y - 9), fill=GRASS, width=2)
    dx, dy = ORBIT[frame]
    x, y = 23 + dx, 34 + dy
    d.ellipse((x - 6, y, x + 6, y + 5), fill=DARK)
    d.polygon(((x - 1, y - 9), (x + 3, y - 6), (x + 4, y + 2), (x - 5, y + 2)), fill=BLUE)
    d.rectangle((x - 1, y - 13, x + 2, y - 10), fill=WARM)
    d.line((x + 3, y - 5, x + 11, y - 12), fill=WHITE, width=2)
    mx, my = 42 - dx, 30 - dy
    d.ellipse((mx - 6, my - 8, mx + 6, my + 4), fill=RED, outline=EARTH)
    d.polygon(((mx - 6, my - 5), (mx - 7, my - 13), (mx - 1, my - 8)), fill=GOLD)
    d.polygon(((mx + 6, my - 5), (mx + 7, my - 13), (mx + 1, my - 8)), fill=GOLD)
    d.line((mx - 2, my - 5, mx + 2, my - 5), fill=WHITE)
    sx, sy = ORBIT[(frame * 3) % 8]
    d.line((33 + sx, 25 + sy, 35 + sx, 31 + sy), fill=GOLD, width=2)
    d.line((31 + sx, 28 + sy, 37 + sx, 28 + sy), fill=WARM)
    d.rectangle((0, 54, 63, 63), fill=DARK)
    d.ellipse((3, 55, 11, 63), fill=RED, outline=CREAM)
    bar(d, (14, 57, 17), RED, 13)
    bar(d, (35, 57, 17), BLUE, 12)
    d.ellipse((55, 55, 62, 63), fill=BLUE, outline=CREAM)
    return image


def sim(frame):
    image, d = canvas(NAVY)
    bank = BOB[frame]
    d.polygon(((0, 28 - bank), (13, 23 - bank), (27, 29), (40, 23 + bank),
               (63, 29 + bank), (63, 49), (0, 49)), fill=GRASS)
    d.line((0, 28 - bank, 13, 23 - bank, 27, 29, 40, 23 + bank, 63, 29 + bank), fill=BEIGE)
    d.polygon(((29, 30), (35, 30), (53, 50), (11, 50)), fill=GRAY)
    d.line((29, 30, 11, 50), fill=CREAM)
    d.line((35, 30, 53, 50), fill=CREAM)
    for y in range(32, 45):
        if (y - 32 - frame) % 8 < 3:
            d.line((31, y, 32, y), fill=WHITE)
    d.line((26, 23, 30, 23, 32, 25, 34, 23, 38, 23), fill=GREEN)
    d.line((4, 0, 15, 18, 7, 46), fill=DARK, width=4)
    d.line((59, 0, 48, 18, 57, 46), fill=DARK, width=4)
    d.polygon(((0, 44), (16, 41), (48, 41), (63, 44), (63, 63), (0, 63)), fill=DARK)
    for x in (12, 32, 52):
        d.ellipse((x - 7, 46, x + 7, 60), fill=BLACK, outline=GRAY)
        d.line((x - 4, 56, x + 4, 56), fill=FOREST)
        d.line((x, 53, x + 2 + bank, 48), fill=GOLD)
        d.point((x, 53), fill=WHITE)
    return image


def race(frame):
    image, d = canvas(NAVY)
    d.polygon(((0, 20), (12, 12), (23, 21), (35, 14), (47, 20), (58, 10), (63, 16),
               (63, 29), (0, 29)), fill=FOREST)
    d.rectangle((0, 25, 63, 63), fill=GRASS)
    d.polygon(((29, 25), (35, 25), (62, 63), (2, 63)), fill=GRAY)
    d.line((29, 25, 2, 63), fill=CREAM, width=2)
    d.line((35, 25, 62, 63), fill=CREAM, width=2)
    # Фаза дороги периодична: шаг последнего кадра совпадает с первым.
    for y in range(26, 64):
        depth = (y - 25) / 38
        phase = (depth * 4 - frame / 8) % 1
        if phase < 0.45:
            width = max(1, int(depth * 2))
            d.line((32 - width, y, 32 + width, y), fill=CREAM)
        if phase < 0.5:
            left = round(29 - depth * 27)
            right = round(35 + depth * 27)
            d.point((left, y), fill=RED)
            d.point((right, y), fill=RED)
    x = 32 + BOB[frame]
    d.rectangle((x - 11, 49, x + 11, 59), fill=BLACK)
    d.polygon(((x - 7, 43), (x + 7, 43), (x + 10, 54), (x + 9, 60),
               (x - 9, 60), (x - 10, 54)), fill=RED)
    d.polygon(((x - 5, 45), (x + 5, 45), (x + 7, 50), (x - 7, 50)), fill=NAVY)
    d.line((x - 8, 54, x + 8, 54), fill=WOOD)
    d.rectangle((x - 8, 56, x - 5, 57), fill=GOLD)
    d.rectangle((x + 5, 56, x + 8, 57), fill=GOLD)
    bar(d, (3, 4, 22), GREEN, 15)
    return image


def action(frame):
    image, d = canvas(GRAY)
    d.rectangle((0, 0, 63, 12), fill=NAVY)
    for x, height, color in ((0, 22, WOOD), (17, 18, BEIGE), (44, 25, WOOD)):
        d.rectangle((x, 5, x + 16, height), fill=color, outline=EARTH)
        for wx in (x + 3, x + 10):
            d.rectangle((wx, 9, wx + 3, 13), fill=NAVY)
        d.rectangle((x + 6, height - 6, x + 10, height), fill=DARK)
    d.polygon(((0, 26), (63, 20), (63, 25), (0, 32)), fill=BEIGE)
    d.polygon(((0, 57), (63, 51), (63, 63), (0, 63)), fill=BEIGE)
    for x in range(-10, 74, 17):
        d.line((x, 43 - x // 10, x + 8, 42 - x // 10), fill=CREAM, width=2)
    dx, dy = ORBIT[frame]
    x, y = 30 + dx * 2, 39 + dy
    d.ellipse((x - 13, y - 1, x + 13, y + 8), fill=DARK)
    d.rectangle((x - 12, y, x + 12, y + 4), fill=BLUE)
    d.polygon(((x - 9, y), (x - 5, y - 6), (x + 5, y - 6), (x + 10, y)), fill=BLUE)
    d.polygon(((x - 4, y - 5), (x + 4, y - 5), (x + 7, y - 1), (x - 7, y - 1)), fill=NAVY)
    for wx in (x - 7, x + 7):
        d.ellipse((wx - 3, y + 2, wx + 2, y + 7), fill=BLACK)
        d.point((wx, y + 4), fill=BEIGE)
    d.line((x + 11, y, x + 11, y + 2), fill=GOLD, width=2)
    person(d, 45 - BOB[frame], 53, RED, frame % 2)
    d.rectangle((2, 3, 16, 7), fill=BLACK)
    d.rectangle((3, 4, 13, 6), fill=RED)
    return image


def casual(frame):
    image, d = canvas(FOREST)
    d.rectangle((3, 3, 60, 60), fill=DARK, outline=BEIGE, width=2)
    d.rectangle((6, 6, 57, 13), fill=EARTH)
    noise_text(d, 9, 9, 18, 1, GOLD)
    for x in (44, 49, 54):
        d.rectangle((x, 9, x + 1, 10), fill=GREEN)
    d.rectangle((7, 17, 56, 56), fill=BLACK)
    for x in range(8, 56, 6):
        for y in range(18, 56, 6):
            d.point((x, y), fill=FOREST)
    path = ([(x, 21) for x in range(13, 46, 8)]
            + [(45, y) for y in range(29, 54, 8)]
            + [(x, 53) for x in range(37, 12, -8)]
            + [(13, y) for y in range(45, 21, -8)])
    head = frame * 2
    for segment in range(9, -1, -1):
        x, y = path[(head - segment) % len(path)]
        color = GREEN if segment < 5 else GRASS
        if segment < 9:
            tail_x, tail_y = path[(head - segment - 1) % len(path)]
            d.line((tail_x, tail_y, x, y), fill=color, width=5)
        d.rectangle((x - 2, y - 2, x + 2, y + 2), fill=color)
        if segment == 0:
            d.point((x, y - 1), fill=BLACK)
    d.rectangle((29, 33, 33, 37), fill=RED)
    d.point((32, 32), fill=GOLD)
    return image


def desktop(_frame):
    image, d = canvas(FOREST)
    d.rectangle((1, 1, 62, 56), fill=GRASS)
    for x, y, color in ((5, 5, BLUE), (5, 20, GOLD), (5, 35, CREAM)):
        d.rectangle((x, y, x + 7, y + 6), fill=BEIGE, outline=WHITE)
        d.rectangle((x + 1, y + 1, x + 6, y + 4), fill=color)
        d.line((x + 2, y + 7, x + 5, y + 7), fill=DARK)
        noise_text(d, x - 1, y + 10, 11, 1, WHITE, seed=y)
    d.rectangle((25, 21, 58, 45), fill=BEIGE, outline=CREAM)
    d.rectangle((27, 23, 56, 27), fill=BLUE)
    noise_text(d, 29, 25, 18, 1, WHITE)
    d.rectangle((51, 24, 54, 26), fill=BEIGE)
    d.rectangle((28, 30, 36, 40), fill=WHITE)
    d.rectangle((39, 31, 53, 32), fill=GRAY)
    noise_text(d, 39, 35, 14, 2, GRAY)
    d.rectangle((0, 57, 63, 63), fill=BEIGE)
    d.line((0, 57, 63, 57), fill=WHITE)
    d.rectangle((2, 59, 15, 62), fill=GRAY, outline=CREAM)
    d.rectangle((19, 59, 39, 62), fill=GRAY)
    noise_text(d, 48, 60, 12, 1, DARK)
    d.polygon(((43, 43), (43, 50), (45, 48), (47, 51), (48, 50), (46, 47), (49, 47)), fill=BLACK)
    d.line((44, 45, 44, 48), fill=WHITE)
    return image


def loading(frame):
    image, d = canvas(NAVY)
    d.polygon(((19, 10), (43, 10), (47, 14), (47, 29),
               (44, 31), (19, 31), (17, 28), (17, 13)), fill=GRAY)
    d.rectangle((19, 11, 43, 28), fill=BEIGE, outline=CREAM)
    d.rectangle((21, 13, 41, 25), fill=DARK, outline=GRAY)
    d.rectangle((23, 15, 39, 23), fill=NAVY)
    d.line((24, 17, 24, 16, 30, 16), fill=GRAY)
    d.line((27, 21, 35, 21), fill=GREEN)
    d.rectangle((29, 29, 35, 32), fill=GRAY)
    d.rectangle((26, 32, 38, 33), fill=BEIGE)
    d.polygon(((23, 27), (27, 27), (29, 29), (35, 29), (37, 27),
               (41, 27), (46, 34), (43, 36), (38, 33), (26, 33),
               (21, 36), (18, 34)), fill=CREAM, outline=EARTH)
    d.rectangle((23, 28, 25, 32), fill=DARK)
    d.rectangle((22, 29, 26, 31), fill=DARK)
    d.rectangle((38, 28, 39, 29), fill=RED)
    d.rectangle((41, 30, 42, 31), fill=RED)
    d.line((30, 31, 33, 31), fill=GRAY)
    noise_text(d, 19, 38, 26, 1, CREAM)
    d.rectangle((8, 44, 55, 52), fill=BLACK, outline=BEIGE)
    # Восемь ячеек замкнуты в кольцо, сегменты мягко уходят за края.
    for offset, color in enumerate((FOREST, GRASS, GREEN)):
        slot = (frame + offset) % 8
        x = 10 + slot * 5
        d.rectangle((x, 46, x + 3, 50), fill=color)
    return image


def bsod(_frame):
    image, d = canvas(BLUE)
    d.rectangle((17, 10, 46, 15), fill=WHITE)
    noise_text(d, 21, 12, 22, 1, BLUE)
    noise_text(d, 6, 22, 51, 6, WHITE)
    noise_text(d, 6, 44, 39, 2, WHITE, seed=SEED + 1)
    noise_text(d, 17, 55, 29, 1, WHITE, seed=SEED + 2)
    return image


def off(_frame):
    image, d = canvas(BLACK)
    d.line((7, 14, 7, 10, 10, 7, 25, 7), fill=DARK)
    d.line((9, 13, 9, 10, 12, 9, 18, 9), fill=DARK)
    return image


RENDERERS = {
    "scr_shooter": shooter, "scr_rts": rts, "scr_tbs": tbs, "scr_rpg": rpg,
    "scr_sim": sim, "scr_race": race, "scr_action": action, "scr_casual": casual,
    "scr_desktop": desktop, "scr_loading": loading, "scr_bsod": bsod, "scr_off": off,
}


def crt(image):
    # Затемняем только выбранные оттенки: новых цветов сканлинии не создают.
    shade = {GRASS: FOREST, WOOD: EARTH, CREAM: BEIGE, WHITE: CREAM, WARM: BEIGE}
    pixels = image.load()
    for y in range(1, SIZE, 2):
        for x in range(SIZE):
            pixels[x, y] = shade.get(pixels[x, y], pixels[x, y])
    d = ImageDraw.Draw(image)
    d.rectangle((0, 0, 63, 63), outline=BLACK)
    for x, y in ((1, 1), (61, 1), (1, 61), (61, 61)):
        d.rectangle((x, y, x + 1, y + 1), fill=BLACK)
    return image


def make_strip(screen_id):
    count = FRAME_COUNTS[screen_id]
    strip = Image.new("RGB", (SIZE, SIZE * count))
    for frame in range(count):
        image = RENDERERS[screen_id](frame)
        strip.paste(crt(image).convert("RGB"), (0, SIZE * frame))
    return strip


def make_sheet(strips):
    scale, gap, label_height, margin = 3, 8, 21, 16
    cell = SIZE * scale
    width = margin * 2 + 8 * (cell + gap) - gap
    row_height = cell + label_height + gap
    sheet = Image.new("RGB", (width, margin * 2 + len(strips) * row_height), "#101416")
    d = ImageDraw.Draw(sheet)
    # Встроенный шрифт исключает зависимость от системных шрифтов.
    font = ImageFont.load_default()
    d.fontmode = "1"
    for row, (screen_id, strip) in enumerate(strips.items()):
        y = margin + row * row_height
        count = FRAME_COUNTS[screen_id]
        d.text((margin, y), f"{screen_id}  |  {count} frame(s)  |  64x64", font=font, fill="#D9D2BC")
        for frame in range(count):
            x = margin + frame * (cell + gap)
            image = strip.crop((0, frame * SIZE, SIZE, (frame + 1) * SIZE))
            sheet.paste(image.resize((cell, cell), Image.Resampling.NEAREST), (x, y + label_height))
    return sheet


def generate(output=None):
    preview = Path(output) / "scr_sheet.png" if output is not None else (
        ROOT / "assets" / "previews" / "scr_sheet.png")
    output = Path(output) if output is not None else ROOT / "assets" / "textures"
    output.mkdir(parents=True, exist_ok=True)
    preview.parent.mkdir(parents=True, exist_ok=True)
    strips = {screen_id: make_strip(screen_id) for screen_id in FRAME_COUNTS}
    for screen_id, strip in strips.items():
        strip.save(output / f"{screen_id}.png", compress_level=9)
    make_sheet(strips).save(preview, compress_level=9)
    return preview


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-o", "--output", type=Path, help="папка для лент и scr_sheet.png")
    args = parser.parse_args(argv)
    preview = generate(args.output)
    print(f"Generated {len(FRAME_COUNTS)} screens, preview: {preview}")


if __name__ == "__main__":
    main()
