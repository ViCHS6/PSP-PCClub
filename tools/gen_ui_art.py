#!/usr/bin/env python3
"""A012: процедурная графика «Окна 2000 Клубная», stdlib + Pillow.

Запуск: py -3.12 tools/gen_ui_art.py [-o DIR]
Без -o: PNG и JSON в assets/textures/, лист в assets/previews/ui_sheet.png.
С -o: все результаты, включая ui_sheet.png, только в DIR. Шрифт всегда
читается относительно скрипта: assets/fonts/font_ui12.{fnt,png} (FNT1).

ui_atlas.json — UTF-8 JSON, объект {имя_спрайта: запись}, без служебных ключей.
Имя состоит из snake_case-компонентов через точку. Обязательные поля записи:
tex — имя PNG без расширения; x,y,w,h — целые тексели, начало сверху слева,
прямоугольник [x,x+w) × [y,y+h). Все выделенные прямоугольники независимы.
slice: [left,top,right,bottom] — фиксированные края 9-slice; центр и стороны
растягиваются nearest-neighbour, минимальный размер — сумма краёв + 1.
tile_edges: true — стороны 9-slice повторяются с обрезкой последнего тайла,
углы неизменны, центр растягивается. Так button.focus сохраняет шаг пунктира.
repeat: "x"/"y"/"xy" — рекомендуемые оси тайлинга обычного спрайта.
hot: [x,y] — локальная горячая точка курсора (экранная позиция минус hot).
frames: [имена_спрайтов] — полный цикл, включая текущую запись первым кадром;
каждый кадр имеет собственный прямоугольник и hot. frame_ms — длительность
каждого кадра в миллисекундах. Это анимация UV, не вертикальная лента TEX1:
конвертировать каждый PNG с --format t4, --frames 1, без фильтрации.

В основных сетках по 16 иконок; семнадцатая, start, обоих размеров находится
в ui_frame. Потребитель всегда использует tex из карты, не выводит его из ID.
Все четыре системных атласа имеют одну 15-цветную палитру, индекс 0 прозрачен.
Обои имеют отдельную непрозрачную палитру. PNG сохраняются indexed 4-bit без
метаданных времени, случайности, сглаживания и зависимости от системных шрифтов.
Обои масштабируются ×2 до 512×256 и обрезаются справа до 480×256.
Тексты в макете и на обоях — только демонстрационная графика, не строки ОС.
"""

import argparse
import json
from pathlib import Path
import struct

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[1]
PALETTE = [
    "000000", "222222", "555555", "888888", "C0C0C0", "E0E0E0", "FFFFFF",
    "15356B", "2050C0", "368C92", "F0C020", "E04030", "5E7A5A", "D8D0B8",
    "7A5638", "70A0C0",
]
T, BLACK, DARK, GRAY, SILVER, LIGHT, WHITE, NAVY, BLUE, TEAL, YELLOW, RED, GREEN, BEIGE, BROWN, SKY = range(16)
ICONS = ("cashier", "network", "shop", "mail", "ledger", "records", "games",
         "settings", "computer", "trash", "folder", "document", "info",
         "warning", "error", "question")
NEAREST = Image.Resampling.NEAREST


def palette_image(size, palette=PALETTE, transparent=True, fill=0):
    image = Image.new("P", size, fill)
    image.putpalette([int(c[i:i + 2], 16) for c in palette for i in (0, 2, 4)])
    if transparent:
        image.info["transparency"] = 0
    return image


def save_png(image, path):
    image.save(path, format="PNG", bits=4, optimize=False, compress_level=9)


class BitmapFont:
    def __init__(self):
        base = ROOT / "assets/fonts/font_ui12"
        raw = base.with_suffix(".fnt").read_bytes()
        header = struct.unpack_from("<4sHHHHBBBBHH12s", raw)
        if header[:2] != (b"FNT1", 1) or len(raw) != 32 + header[3] * 16:
            raise ValueError("font_ui12: неверный FNT1")
        with Image.open(base.with_suffix(".png")) as image:
            atlas = image.convert("RGBA").getchannel("A")
        self.glyphs = {}
        for i in range(header[3]):
            cp, x, y, w, h, dx, dy, advance = struct.unpack_from("<IHHBBbbB3x", raw, 32 + i * 16)
            self.glyphs[chr(cp)] = (atlas.crop((x, y, x + w, y + h)), dx, dy, advance)

    def width(self, text):
        return sum(self.glyphs[c][3] for c in text)

    def text(self, target, xy, text, color, scale=1):
        x, y = xy
        for c in text:
            mask, dx, dy, advance = self.glyphs[c]
            if mask.width and mask.height:
                mask = mask.resize((mask.width * scale, mask.height * scale), NEAREST)
                target.paste(color, (x + dx * scale, y + dy * scale), mask)
            x += advance * scale


def bevel(size, pressed=False, field=False, focus=False):
    image = palette_image(size, fill=WHITE if field else SILVER)
    d = ImageDraw.Draw(image)
    w, h = size
    upper, lower = (DARK, WHITE) if pressed or field else (WHITE, BLACK)
    inner_upper, inner_lower = (GRAY, LIGHT) if pressed or field else (LIGHT, GRAY)
    for n, a, b in ((0, upper, lower), (1, inner_upper, inner_lower)):
        d.line((n, h - n - 2, n, n), fill=a)
        d.line((n, n, w - n - 2, n), fill=a)
        d.line((n, h - n - 1, w - n - 1, h - n - 1), fill=b)
        d.line((w - n - 1, n, w - n - 1, h - n - 1), fill=b)
    if focus:
        for y in range(3, h - 3):
            for x in range(3, w - 3):
                if (x in (3, w - 4) or y in (3, h - 4)) and (x + y) % 2 == 0:
                    d.point((x, y), fill=BLACK)
    return image


def icon(name, size):
    # Обе версии рисуются в своей сетке: крупная получает тонкие блики и тень.
    image = palette_image((size, size))
    d = ImageDraw.Draw(image)
    s = size // 16

    def rect(box, fill, outline=None):
        x0, y0, x1, y1 = box
        d.rectangle((x0 * s, y0 * s, (x1 + 1) * s - 1, (y1 + 1) * s - 1),
                    fill=fill, outline=outline, width=s)

    def line(points, color, width=1):
        d.line([(x * s, y * s) for x, y in points], fill=color, width=width * s)

    def poly(points, fill, outline=BLACK):
        d.polygon([(x * s, y * s) for x, y in points], fill=fill, outline=outline, width=s)

    def monitor(x, y, w=10, h=8):
        rect((x, y, x + w, y + h), BEIGE, BLACK)
        line([(x + 1, y + h - 1), (x + 1, y + 1), (x + w - 1, y + 1)], WHITE)
        rect((x + 2, y + 2, x + w - 2, y + h - 2), NAVY)
        line([(x + 3, y + h - 3), (x + 3, y + 3), (x + w - 3, y + 3)], TEAL)
        rect((x + w // 2 - 1, y + h + 1, x + w // 2 + 1, y + h + 1), GRAY)
        rect((x + 2, y + h + 2, x + w - 2, y + h + 2), BEIGE, DARK)

    if name == "cashier":
        rect((3, 1, 11, 5), BLACK)
        rect((4, 2, 10, 4), GREEN)
        line([(5, 3), (6, 3)], YELLOW)
        line([(8, 3), (9, 3)], YELLOW)
        poly([(3, 6), (11, 6), (13, 11), (1, 11)], BEIGE)
        for x in (4, 7, 10):
            rect((x, 7, x + 1, 7), WHITE)
            rect((x, 9, x + 1, 9), DARK)
        rect((1, 11, 13, 13), BEIGE, BLACK)
        rect((6, 12, 8, 12), BROWN)
        rect((12, 9, 14, 11), YELLOW, BROWN)
    elif name == "network":
        line([(7, 7), (7, 10), (2, 10), (13, 10)], DARK)
        line([(2, 10), (2, 12)], YELLOW)
        line([(13, 10), (13, 12)], YELLOW)
        monitor(3, 0, 8, 5)
        rect((0, 11, 5, 14), BEIGE, BLACK)
        rect((1, 12, 4, 13), TEAL)
        rect((10, 11, 15, 14), BEIGE, BLACK)
        rect((11, 12, 14, 13), BLUE)
    elif name == "shop":
        rect((2, 7, 13, 13), BEIGE, BLACK)
        rect((3, 9, 6, 12), NAVY)
        rect((8, 9, 11, 11), SKY)
        poly([(3, 2), (12, 2), (14, 6), (1, 6)], RED)
        for x in (3, 7, 11):
            rect((x, 3, x + 1, 6), WHITE)
        line([(1, 7), (14, 7)], DARK)
    elif name == "mail":
        rect((1, 4, 14, 12), BEIGE, BROWN)
        line([(2, 5), (7, 9), (13, 5)], WHITE)
        line([(2, 11), (6, 8)], WHITE)
        line([(13, 11), (9, 8)], GRAY)
        rect((11, 2, 14, 5), BLUE, NAVY)
        rect((12, 3, 13, 4), SKY)
    elif name == "ledger":
        rect((2, 2, 11, 14), BROWN, BLACK)
        rect((4, 1, 13, 12), BEIGE, BLACK)
        rect((5, 2, 12, 11), WHITE)
        line([(7, 3), (7, 10)], RED)
        for y in (4, 7, 10):
            line([(5, y), (11, y)], GRAY)
        line([(10, 13), (14, 7)], YELLOW, 2)
        d.point((10 * s, 14 * s), fill=BLACK)
    elif name == "records":
        poly([(4, 2), (11, 2), (10, 8), (8, 10), (6, 8)], YELLOW, BROWN)
        line([(4, 3), (1, 3), (2, 6), (5, 7)], BROWN)
        line([(11, 3), (14, 3), (13, 6), (10, 7)], BROWN)
        line([(5, 3), (5, 6)], WHITE)
        rect((7, 9, 8, 11), YELLOW)
        rect((4, 12, 11, 13), BROWN, BLACK)
        line([(5, 12), (10, 12)], YELLOW)
    elif name in ("games", "start"):
        if name == "start":
            monitor(1, 1, 11, 8)
        poly([(3, 8), (11, 8), (14, 11), (14, 14), (11, 14), (9, 12),
              (5, 12), (3, 14), (0, 14), (0, 11)], BEIGE)
        line([(2, 10), (4, 10)], DARK)
        line([(3, 9), (3, 11)], DARK)
        rect((10, 10, 10, 10), RED)
        rect((12, 11, 12, 11), BLUE)
        if name == "games":
            line([(7, 7), (7, 4), (10, 4), (10, 1)], DARK)
            rect((4, 2, 6, 4), YELLOW, BROWN)
    elif name == "settings":
        for x, y in ((6, 1), (6, 12), (1, 6), (12, 6), (2, 2), (11, 2), (2, 11), (11, 11)):
            rect((x, y, x + 2, y + 2), DARK)
        d.ellipse((2 * s, 2 * s, 13 * s, 13 * s), fill=GRAY, outline=BLACK, width=s)
        d.ellipse((5 * s, 5 * s, 10 * s, 10 * s), fill=NAVY, outline=WHITE, width=s)
        line([(10, 12), (13, 5)], YELLOW, 2)
    elif name == "computer":
        monitor(1, 1, 12, 9)
        poly([(3, 12), (12, 12), (14, 14), (1, 14)], BEIGE)
        line([(4, 13), (11, 13)], GRAY)
    elif name == "trash":
        poly([(3, 4), (12, 4), (11, 14), (4, 14)], SILVER)
        rect((2, 2, 13, 4), LIGHT, DARK)
        rect((6, 1, 9, 1), DARK)
        for x in (5, 8, 10):
            line([(x, 6), (x, 12)], GRAY)
        line([(4, 5), (4, 11)], WHITE)
    elif name == "folder":
        poly([(1, 3), (6, 3), (8, 5), (14, 5), (14, 13), (1, 13)], YELLOW, BROWN)
        poly([(3, 7), (15, 7), (12, 13), (1, 13)], BEIGE, BROWN)
        line([(4, 8), (13, 8)], WHITE)
    elif name == "document":
        poly([(3, 1), (10, 1), (13, 4), (13, 14), (3, 14)], WHITE, DARK)
        line([(10, 1), (10, 4), (13, 4)], GRAY)
        for y in (6, 8, 10):
            line([(5, y), (10, y)], BLUE if y == 6 else GRAY)
    elif name in ("info", "question", "warning", "error"):
        if name == "warning":
            poly([(7, 1), (14, 13), (1, 13)], YELLOW)
            rect((7, 5, 8, 9), BLACK)
            rect((7, 11, 8, 11), BLACK)
        else:
            fill = RED if name == "error" else BLUE
            d.ellipse((s, s, 14 * s, 14 * s), fill=fill, outline=BLACK, width=s)
            if name == "error":
                line([(5, 5), (10, 10)], WHITE, 2)
                line([(10, 5), (5, 10)], WHITE, 2)
            elif name == "info":
                rect((7, 4, 8, 5), WHITE)
                rect((7, 7, 8, 11), WHITE)
                rect((6, 11, 9, 11), WHITE)
            else:
                line([(5, 5), (6, 4), (9, 4), (10, 5), (10, 6), (7, 8), (7, 9)], WHITE)
                rect((7, 11, 8, 11), WHITE)
    else:
        raise ValueError(name)
    if size == 32:
        alpha = image.convert("RGBA").getchannel("A")
        shadow = palette_image(image.size)
        shadow.paste(DARK, (1, 1), alpha.crop((0, 0, size - 1, size - 1)))
        shadow.paste(image, (0, 0), alpha)
        image = shadow
        # Однопиксельные верхние блики сохраняют объём без новых цветов.
        px = image.load()
        for y in range(1, size - 1):
            for x in range(1, size - 1):
                if px[x, y] in (BEIGE, SILVER, YELLOW) and px[x, y - 1] in (BLACK, BROWN, DARK):
                    px[x, y] = WHITE
    return image


class Atlas:
    def __init__(self, name, size, mapping, images):
        self.name, self.mapping = name, mapping
        self.image = palette_image(size)
        images[name] = self.image
        self.occupied = []

    def add(self, key, sprite, xy=None, **metadata):
        w, h = sprite.size
        if xy is None:
            # Первый подходящий свободный прямоугольник; порядок явно задан.
            for y in range(self.image.height - h + 1):
                for x in range(self.image.width - w + 1):
                    if all(x + w <= a or a + c <= x or y + h <= b or b + e <= y
                           for a, b, c, e in self.occupied):
                        xy = x, y
                        break
                if xy is not None:
                    break
        if xy is None:
            raise ValueError(f"{self.name}: не помещается {key}")
        x, y = xy
        if x < 0 or y < 0 or x + w > self.image.width or y + h > self.image.height:
            raise ValueError(f"{key}: за границами атласа")
        if any(x < a + c and a < x + w and y < b + e and b < y + h
               for a, b, c, e in self.occupied):
            raise ValueError(f"{key}: пересечение спрайтов")
        self.occupied.append((x, y, w, h))
        self.image.paste(sprite, xy)
        self.mapping[key] = dict(tex=self.name, x=x, y=y, w=w, h=h, **metadata)


def make_frame(mapping, images):
    atlas = Atlas("ui_frame", (128, 64), mapping, images)
    atlas.add("icon32.start", icon("start", 32), (0, 0))
    atlas.add("icon16.start", icon("start", 16), (32, 0))
    for key, pressed, field, focus in (
            ("frame.window", False, False, False), ("frame.field", False, True, False),
            ("button.normal", False, False, False), ("button.pressed", True, False, False),
            ("button.focus", False, False, True)):
        metadata = dict(slice=[4, 4, 4, 4])
        if focus:
            metadata["tile_edges"] = True
        atlas.add(key, bevel((12, 12), pressed, field, focus), **metadata)
    for active in (True, False):
        tile = palette_image((4, 14))
        d = ImageDraw.Draw(tile)
        for y in range(14):
            color = (NAVY if y < 4 else BLUE if y < 12 else SKY) if active else (GRAY if y < 7 else DARK)
            d.line((0, y, 3, y), fill=color)
        atlas.add("title.active" if active else "title.inactive", tile, repeat="x")
    for action in ("minimize", "close"):
        for pressed in (False, True):
            tile = bevel((12, 12), pressed)
            d = ImageDraw.Draw(tile)
            n = int(pressed)
            if action == "close":
                d.line((3 + n, 3 + n, 8 + n, 8 + n), fill=BLACK, width=2)
                d.line((8 + n, 3 + n, 3 + n, 8 + n), fill=BLACK, width=2)
            else:
                d.rectangle((3 + n, 8 + n, 8 + n, 9 + n), fill=BLACK)
            atlas.add(f"caption.{action}_{'pressed' if pressed else 'normal'}", tile)
    for control in ("checkbox", "radio"):
        for checked in (False, True):
            tile = bevel((12, 12), field=True) if control == "checkbox" else palette_image((12, 12))
            d = ImageDraw.Draw(tile)
            if control == "checkbox" and checked:
                d.line([(3, 5), (5, 8), (9, 3)], fill=BLACK, width=2)
            elif control == "radio":
                d.ellipse((0, 0, 11, 11), fill=GRAY, outline=DARK)
                d.ellipse((2, 2, 10, 10), fill=WHITE)
                if checked:
                    d.ellipse((4, 4, 7, 7), fill=BLACK)
            atlas.add(f"{control}.{'on' if checked else 'off'}", tile)
    for direction, points in {
            "up": [(3, 7), (6, 4), (9, 7)], "down": [(3, 4), (6, 7), (9, 4)],
            "left": [(7, 3), (4, 6), (7, 9)], "right": [(4, 3), (7, 6), (4, 9)]}.items():
        for pressed in (False, True):
            tile = bevel((12, 12), pressed)
            ImageDraw.Draw(tile).polygon([(x + int(pressed), y + int(pressed)) for x, y in points], fill=BLACK)
            atlas.add(f"scroll.{direction}_{'pressed' if pressed else 'normal'}", tile)
    atlas.add("scroll.thumb", bevel((12, 12)), slice=[2, 2, 2, 2])
    tile = palette_image((2, 2), fill=LIGHT)
    d = ImageDraw.Draw(tile)
    d.point((0, 0), fill=SILVER)
    d.point((1, 1), fill=SILVER)
    atlas.add("scroll.track", tile, repeat="xy")
    atlas.add("progress.segment", palette_image((6, 8), fill=BLUE))
    tile = palette_image((4, 2), fill=GRAY)
    ImageDraw.Draw(tile).line((0, 1, 3, 1), fill=WHITE)
    atlas.add("menu.separator", tile, repeat="x")
    tile = palette_image((4, 16), fill=SILVER)
    d = ImageDraw.Draw(tile)
    d.line((0, 0, 3, 0), fill=WHITE)
    d.line((0, 1, 3, 1), fill=LIGHT)
    atlas.add("taskbar.tile", tile, repeat="x")
    # Текст и эмблема накладываются потребителем; кнопка остаётся 9-slice.
    atlas.add("taskbar.start_normal", bevel((12, 14)), slice=[3, 3, 3, 3])
    atlas.add("taskbar.start_pressed", bevel((12, 14), pressed=True), slice=[3, 3, 3, 3])


def make_cursors(mapping, images):
    atlas = Atlas("ui_cursor", (64, 32), mapping, images)
    for i, name in enumerate(("arrow", "hand", "wait", "wait_1", "ibeam")):
        tile = palette_image((16, 16))
        d = ImageDraw.Draw(tile)
        if name == "arrow":
            d.polygon([(1, 0), (1, 13), (4, 10), (7, 15), (9, 14), (6, 9), (11, 9)],
                      fill=WHITE, outline=BLACK)
            hot = [1, 0]
        elif name == "hand":
            d.polygon([(4, 7), (4, 1), (5, 0), (6, 1), (6, 6), (8, 4), (10, 5),
                       (12, 5), (14, 7), (14, 11), (12, 15), (6, 15), (2, 10),
                       (2, 8), (3, 7)], fill=WHITE, outline=BLACK)
            d.line((8, 6, 8, 9), fill=BLACK)
            d.line((10, 6, 10, 9), fill=BLACK)
            d.line((12, 7, 12, 10), fill=BLACK)
            hot = [5, 0]
        elif name.startswith("wait"):
            d.polygon([(4, 2), (11, 2), (11, 5), (8, 8), (11, 11), (11, 13),
                       (4, 13), (4, 11), (7, 8), (4, 5)], fill=WHITE, outline=BLACK)
            d.rectangle((3, 0, 12, 2), fill=BLACK)
            d.line((4, 1, 11, 1), fill=WHITE)
            d.rectangle((3, 13, 12, 15), fill=BLACK)
            d.line((4, 14, 11, 14), fill=WHITE)
            if name == "wait":
                d.polygon([(5, 4), (10, 4), (7, 7)], fill=BLACK)
                d.line((7, 9, 7, 11), fill=BLACK)
                d.line((6, 12, 9, 12), fill=BLACK)
            else:
                d.line((7, 4, 8, 4), fill=BLACK)
                d.polygon([(7, 9), (5, 12), (10, 12)], fill=BLACK)
            hot = [7, 7]
        else:
            d.line((5, 1, 10, 1), fill=BLACK, width=3)
            d.line((7, 2, 7, 13), fill=BLACK, width=3)
            d.line((5, 14, 10, 14), fill=BLACK, width=3)
            d.line((5, 1, 10, 1), fill=WHITE)
            d.line((7, 2, 7, 13), fill=WHITE)
            d.line((5, 14, 10, 14), fill=WHITE)
            hot = [7, 7]
        meta = dict(hot=hot)
        if name == "wait":
            meta.update(frames=["cursor.wait", "cursor.wait_1"], frame_ms=350)
        atlas.add("cursor." + name, tile, (i % 4 * 16, i // 4 * 16), **meta)


def make_wallpaper(mapping, images, font):
    colors = ["368C92", "70A0A0", "90B8B0", "D9D2BC", "E8F0F0", "FFFFFF",
              "5E7A5A", "4B6449", "BFB79F", "8F8670", "D8D0B8", "7A5638",
              "2F3A55", "F0C020", "2050C0", "222222"]
    image = palette_image((256, 128), colors, transparent=False)
    d = ImageDraw.Draw(image)
    d.rectangle((0, 48, 255, 82), fill=1)
    d.rectangle((0, 83, 255, 127), fill=2)
    for x, y in ((19, 26), (85, 51), (203, 34)):
        d.rectangle((x, y + 4, x + 31, y + 9), fill=4)
        d.rectangle((x + 5, y, x + 17, y + 8), fill=4)
        d.rectangle((x + 15, y + 2, x + 26, y + 8), fill=5)
        d.line((x + 2, y + 10, x + 29, y + 10), fill=3)
    d.rectangle((0, 114, 255, 127), fill=6)
    d.line((0, 115, 255, 115), fill=7)
    # Девять рядов окон, стыки панелей и вход в клуб в цоколе.
    d.rectangle((128, 50, 228, 117), fill=9)
    d.rectangle((130, 47, 221, 115), fill=3)
    d.rectangle((129, 45, 224, 48), fill=8)
    d.line((132, 49, 132, 115), fill=4)
    for y in range(51, 106, 6):
        d.line((132, y + 4, 220, y + 4), fill=8)
        for x in range(136, 215, 12):
            d.rectangle((x, y, x + 5, y + 3), fill=12)
            d.line((x, y, x + 4, y), fill=9)
            d.point((x + 1, y + 2), fill=13 if (x + y) % 5 == 0 else 1)
    for x in (155, 179, 203):
        d.line((x, 49, x, 114), fill=8)
    d.rectangle((169, 107, 187, 116), fill=12)
    d.rectangle((166, 104, 190, 109), fill=14)
    font.text(image, (168, 102), "КЛУБ", 5)
    d.polygon([(173, 117), (184, 117), (197, 127), (158, 127)], fill=8)
    # Колонка иконок занимает первые 35 текселей; подпись целиком в небе.
    for text, y, scale, color in (("ОКНА 2000", 0, 2, 5), ("Клубная", 22, 1, 12),
                                  ("Весь мир. Через подвал.", 33, 1, 12)):
        x = 110 - font.width(text) * scale // 2
        if scale == 2:
            font.text(image, (x + 1, y + 1), text, 12, scale=scale)
        font.text(image, (x, y), text, color, scale=scale)
    mapping["wallpaper.desktop"] = dict(tex="ui_wallpaper", x=0, y=0, w=256, h=128)
    images["ui_wallpaper"] = image


def sprite(images, mapping, key):
    r = mapping[key]
    return images[r["tex"]].crop((r["x"], r["y"], r["x"] + r["w"], r["y"] + r["h"])).convert("RGBA")


def put(target, images, mapping, key, xy, size=None):
    image = sprite(images, mapping, key)
    if size:
        image = image.resize(size, NEAREST)
    target.paste(image, xy, image)


def nine(target, images, mapping, key, box):
    x, y, w, h = box
    r = mapping[key]
    a, b, c, e = r["slice"]
    if w < a + c + 1 or h < b + e + 1:
        raise ValueError(f"{key}: слишком малый размер 9-slice")
    image = sprite(images, mapping, key)
    sx, sy = (0, a, r["w"] - c, r["w"]), (0, b, r["h"] - e, r["h"])
    dx, dy = (0, a, w - c, w), (0, b, h - e, h)
    for j in range(3):
        for i in range(3):
            part = image.crop((sx[i], sy[j], sx[i + 1], sy[j + 1]))
            size = dx[i + 1] - dx[i], dy[j + 1] - dy[j]
            if r.get("tile_edges") and ((i == 1) != (j == 1)):
                tiled = Image.new("RGBA", size)
                for py in range(0, size[1], part.height):
                    for px in range(0, size[0], part.width):
                        tiled.paste(part, (px, py))
                part = tiled
            else:
                part = part.resize(size, NEAREST)
            target.paste(part, (x + dx[i], y + dy[j]), part)


def mock_screen(images, mapping, font):
    screen = Image.new("RGB", (480, 272))
    screen.paste(images["ui_wallpaper"].convert("RGB").resize((512, 256), NEAREST), (0, 0))
    colors = ["#" + c for c in PALETTE]

    def text(xy, content, color=BLACK):
        font.text(screen, xy, content, colors[color])

    def button(key, box, label):
        nine(screen, images, mapping, key, box)
        x, y, w, h = box
        text((x + (w - font.width(label)) // 2, y + (h - 12) // 2), label)

    for key, label, y in (("computer", "Компьютер", 8), ("cashier", "Касса", 62),
                          ("mail", "Почта", 116), ("trash", "Корзина", 170)):
        put(screen, images, mapping, "icon32." + key, (16, y))
        text((max(2, 32 - font.width(label) // 2) + 1, y + 32), label, NAVY)
        text((max(2, 32 - font.width(label) // 2), y + 31), label, WHITE)

    def window(box, title, active, icon_key):
        x, y, w, h = box
        nine(screen, images, mapping, "frame.window", box)
        put(screen, images, mapping, "title.active" if active else "title.inactive",
            (x + 3, y + 3), (w - 6, 14))
        put(screen, images, mapping, "icon16." + icon_key, (x + 4, y + 2))
        text((x + 23, y + 4), title, WHITE)
        put(screen, images, mapping, "caption.minimize_normal", (x + w - 29, y + 4))
        put(screen, images, mapping, "caption.close_normal", (x + w - 15, y + 4))

    window((245, 28, 221, 155), "Почта", False, "mail")
    text((254, 50), "Входящие: 2 письма")
    nine(screen, images, mapping, "frame.field", (254, 67, 201, 82))
    put(screen, images, mapping, "icon16.mail", (260, 72))
    text((280, 73), "Новинки сентября")
    put(screen, images, mapping, "icon16.document", (260, 91))
    text((280, 92), "Счёт за Интернет")
    button("button.normal", (382, 156, 72, 18), "Закрыть")
    window((86, 91, 276, 151), "Касса", True, "cashier")
    text((96, 113), "Место    Посетитель    Осталось")
    nine(screen, images, mapping, "frame.field", (96, 128, 254, 56))
    d = ImageDraw.Draw(screen)
    d.rectangle((99, 131, 334, 144), fill=colors[BLUE])
    text((102, 132), "ПК 01    Саша         00:45", WHITE)
    text((102, 147), "ПК 02    Игорь        00:12")
    text((102, 162), "ПК 03    Свободно")
    put(screen, images, mapping, "scroll.track", (336, 142), (12, 30))
    put(screen, images, mapping, "scroll.up_normal", (336, 130))
    put(screen, images, mapping, "scroll.thumb", (336, 143))
    put(screen, images, mapping, "scroll.down_normal", (336, 170))
    put(screen, images, mapping, "checkbox.on", (98, 191))
    text((115, 191), "Продлить на час")
    put(screen, images, mapping, "radio.on", (259, 191))
    text((276, 191), "Общий")
    button("button.focus", (97, 214, 90, 19), "Продлить")
    button("button.pressed", (199, 214, 67, 19), "Чек")
    button("button.normal", (276, 214, 74, 19), "Закрыть")
    put(screen, images, mapping, "taskbar.tile", (0, 256), (480, 16))
    nine(screen, images, mapping, "taskbar.start_normal", (2, 257, 62, 14))
    put(screen, images, mapping, "icon16.start", (4, 256))
    text((23, 258), "Пуск")
    nine(screen, images, mapping, "button.pressed", (70, 257, 99, 14))
    put(screen, images, mapping, "icon16.cashier", (72, 256))
    text((91, 258), "Касса")
    nine(screen, images, mapping, "button.normal", (173, 257, 99, 14))
    put(screen, images, mapping, "icon16.mail", (175, 256))
    text((194, 258), "Почта")
    nine(screen, images, mapping, "frame.field", (384, 257, 94, 14))
    text((390, 258), "01.09  09:00")
    put(screen, images, mapping, "cursor.arrow", (184, 199))
    return screen


def make_sheet(images, mapping, font):
    sheet = Image.new("RGB", (1240, 1490), "#15356B")
    font.text(sheet, (20, 14), "ОКНА 2000 КЛУБНАЯ / АТЛАСЫ И ЭКРАН", "#FFFFFF", scale=2)
    slots = (("ui_icons32", 20, 70), ("ui_icons16", 424, 70), ("ui_frame", 640, 70),
             ("ui_cursor", 640, 304), ("ui_wallpaper", 20, 502))
    for name, x, y in slots:
        image = images[name].convert("RGBA")
        font.text(sheet, (x, y - 18), f"{name}  {image.width}×{image.height}  ×3", "#E0E0E0")
        bg = Image.new("RGB", image.size, "#8F8670")
        d = ImageDraw.Draw(bg)
        for cy in range(0, image.height, 4):
            for cx in range(0, image.width, 4):
                if (cx // 4 + cy // 4) % 2 == 0:
                    d.rectangle((cx, cy, cx + 3, cy + 3), fill="#BFB79F")
        bg.paste(image, (0, 0), image)
        sheet.paste(bg.resize((image.width * 3, image.height * 3), NEAREST), (x, y))
    font.text(sheet, (820, 495), "ПАЛИТРА / 15 + ПРОЗРАЧНОСТЬ", "#FFFFFF")
    d = ImageDraw.Draw(sheet)
    for i, c in enumerate(PALETTE[1:]):
        x, y = 820 + (i % 5) * 28, 518 + (i // 5) * 28
        d.rectangle((x, y, x + 23, y + 23), fill="#" + c)
    font.text(sheet, (820, 622), "Свет: сверху слева", "#FFFFFF")
    font.text(sheet, (820, 642), "Пуск: монитор + геймпад", "#FFFFFF")
    font.text(sheet, (20, 918), "МАКЕТ 480×272  ×2 / 01 СЕНТЯБРЯ 2001", "#FFFFFF")
    sheet.paste(mock_screen(images, mapping, font).resize((960, 544), NEAREST), (20, 940))
    return sheet


def generate(output, preview):
    font = BitmapFont()
    mapping, images = {}, {}
    for size, side in ((32, 128), (16, 64)):
        atlas = Atlas(f"ui_icons{size}", (side, side), mapping, images)
        for i, name in enumerate(ICONS):
            atlas.add(f"icon{size}.{name}", icon(name, size), (i % 4 * size, i // 4 * size))
    make_frame(mapping, images)
    make_cursors(mapping, images)
    make_wallpaper(mapping, images, font)
    output.mkdir(parents=True, exist_ok=True)
    preview.parent.mkdir(parents=True, exist_ok=True)
    for name, image in images.items():
        save_png(image, output / (name + ".png"))
    (output / "ui_atlas.json").write_text(
        json.dumps(mapping, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n")
    make_sheet(images, mapping, font).save(preview, format="PNG", optimize=False, compress_level=9)
    return mapping


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-o", "--output", type=Path, help="все результаты в DIR")
    args = parser.parse_args(argv)
    output = args.output if args.output is not None else ROOT / "assets/textures"
    preview = output / "ui_sheet.png" if args.output is not None else ROOT / "assets/previews/ui_sheet.png"
    mapping = generate(output, preview)
    print(f"A012: {len(mapping)} sprites, 5 textures -> {output}; preview -> {preview}")


if __name__ == "__main__":
    main()
