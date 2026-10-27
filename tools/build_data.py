#!/usr/bin/env python3
"""Проверка исходников и сборка data.pak v1 (docs/formats/data_pak.md)."""

from __future__ import annotations

import argparse
import csv
from datetime import date
import json
import math
import os
from pathlib import Path
import re
import struct
import sys
import tempfile
import zlib


NONE = 0xFFFF
ID_RE = re.compile(r"[a-z][a-z0-9_]*\Z")
EXPR_RE = re.compile(r"\s*([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)\s*\*\s*([a-z][a-z0-9_]*(?::[a-z][a-z0-9_]*)?)\s*\Z")
GENRES = "shooter_team shooter_arena shooter_3p rts tbs arpg mmo sim action race sport casual minigame".split()
COMPONENTS = "cpu gpu ram hdd psu monitor mouse keyboard headphones chair".split()
ENUMS = {
    "genre": GENRES,
    "net": "none lan online_low online_high".split(),
    "tags": "tournament team evergreen kids adult".split(),
    "component": COMPONENTS,
    "flags": "noisy minigame_lover likes_graphics smoker night".split(),
    "traits": "competitive loud shy generous troublemaker".split(),
    "kind": "hall internet network aircon soundproof renovation sign ad night_tariff staff_admin staff_guard staff_cleaner antivirus".split(),
    "where": "admin visitors".split(),
    "service": "reboot clean replace crimp install virus network".split(),
    "tag": "holidays session".split(),
    "category": "crisis opportunity calendar regular info story".split(),
    "trigger": "daily_roll date on_action on_state scheduled".split(),
    "action": "record_beaten pc_bought tournament_end game_installed pirate_installed upgrade_bought pc_broken pc_hung visitor_left_angry regular_unlocked day_closed month_started goods_out".split(),
    "present": "mail visitor phone world".split(),
    "var": "money rep day hour weekday date pcs pirate_share pirate_installs flag relation game_installed visits_archetype competitor_active".split(),
    "op": ["==", "!=", "<", "<=", ">", ">="],
    "effect": "money rep flag relation modifier spawn_visitor schedule_event uninstall damage_pc item unlock chance start_minigame".split(),
    "target": "arrivals hw_price pop dust".split(),
}

# Явный little-endian: выравнивание C++ задаётся только полями x.
RECORD_FORMATS = {
    "PakHeader": "<4sHHII",
    "PakSection": "<4sIIIHH",
    "GameRec": "<HHBBHiiiHHHHIffHHH2xf",
    "ArchetypeRec": "<HHf24f7fffiiifHHffBBBBff13ffffI4H",
    "RegularRec": "<7HBBHHB3xIf",
    "HardwareRec": "<HHBbHiiHHIff",
    "GoodsRec": "<HHiif",
    "UpgradeRec": "<HHBBHHHiiiiff",
    "MinigameRec": "<HHBB2xf",
    "EventRec": "<H6Biff4H4BHH",
    "ChoiceRec": "<HBBHH",
    "EffectRec": "<4BHHH2xif",
    "CondRec": "<BBHf",
    "ChanceTermRec": "<BxHf",
    "PeriodRec": "<HBxHH",
    "EconomyRec": "<iifii3fiHHBB10H2x",
    "StringKeyRec": "<IH2x",
}
RECORD_SIZES = {name: struct.calcsize(fmt) for name, fmt in RECORD_FORMATS.items()}
SECTIONS = [
    ("GAME", "GameRec"), ("ARCH", "ArchetypeRec"), ("REGU", "RegularRec"),
    ("HARD", "HardwareRec"), ("GOOD", "GoodsRec"), ("UPGR", "UpgradeRec"),
    ("MINI", "MinigameRec"), ("EVNT", "EventRec"), ("CHOI", "ChoiceRec"),
    ("EFFE", "EffectRec"), ("COND", "CondRec"), ("CHNC", "ChanceTermRec"),
    ("LIST", None), ("PERI", "PeriodRec"), ("HOLI", None), ("FLAG", None),
    ("ECON", "EconomyRec"), ("STRS", None), ("STRK", "StringKeyRec"), ("NAME", None),
]
TABLES = {
    "GAME": ("games.json", 256), "ARCH": ("archetypes.json", 16),
    "REGU": ("regulars.json", 64), "HARD": ("hardware.csv", 256),
    "GOOD": ("goods.csv", 32), "UPGR": ("upgrades.csv", 64),
    "MINI": ("minigames.json", 16), "EVNT": ("events", 512),
}
CSV_FIELDS = {
    "HARD": "id type name release base_price score ram_mb hdd_mb power_w wear_rate fail_base comfort".split(),
    "GOOD": "id name buy_price sell_price weight".split(),
    "UPGR": "id kind name level archetype requires days price cost_month cost_day cost_hour cost_minute value".split(),
}


def fnv1a32(key: str) -> int:
    h = 0x811C9DC5
    for b in key.encode("utf-8"):
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


class JsonObject(dict):
    """Сохраняет повторные ключи, которые обычный json.load теряет."""

    def __init__(self, pairs):
        super().__init__()
        self.duplicates = []
        for key, value in pairs:
            if key in self:
                self.duplicates.append(key)
            self[key] = value


class Builder:
    def __init__(self, root: Path):
        self.root = root
        self.errors = []
        self.warnings = []
        self.rows = {}
        self.index = {}
        self.records = {tag: [] for tag, _ in SECTIONS}
        self.names = {}
        self.strings = []
        self.string_keys = {}
        self.flags = {}
        self.flags_set = set()
        self.flags_used = {}

    def error(self, ctx, field, message):
        filename, ident = ctx
        self.errors.append(f"{filename}: {ident}.{field}: {message}")

    def obj(self, value, ctx, field, allowed, required=()):
        if not isinstance(value, dict):
            self.error(ctx, field, "ожидался объект")
            value = {}
        for key in getattr(value, "duplicates", ()):
            self.error(ctx, f"{field}.{key}" if field else key, "дубликат ключа")
        for key in value:
            if key not in allowed:
                self.error(ctx, f"{field}.{key}" if field else key, "неизвестное поле")
        for key in required:
            if key not in value:
                self.error(ctx, f"{field}.{key}" if field else key, "обязательное поле отсутствует")
        return value

    def seq(self, value, ctx, field, maximum=255, minimum=0, exact=None):
        if not isinstance(value, list):
            self.error(ctx, field, "ожидался список")
            return []
        if len(value) > maximum or len(value) < minimum or (exact is not None and len(value) != exact):
            bounds = f"ровно {exact}" if exact is not None else f"{minimum}..{maximum}"
            self.error(ctx, field, f"длина {len(value)}, допустимо {bounds} (лимит)")
        return value

    def number(self, value, ctx, field, lo=-3.4028234663852886e38,
               hi=3.4028234663852886e38, integer=False, positive=False):
        valid = type(value) in (int, float)
        try:
            valid = valid and math.isfinite(value) and lo <= value <= hi
        except (OverflowError, ValueError):
            valid = False
        valid = valid and (not integer or type(value) is int) and (not positive or value > 0)
        if not valid:
            kind = "целое число" if integer else "конечное число float32"
            self.error(ctx, field, f"ожидалось {kind} в диапазоне {lo}..{hi}" + (", > 0" if positive else ""))
            return 0
        if not integer:
            value = struct.unpack("<f", struct.pack("<f", value))[0]
            if positive and value == 0:
                self.error(ctx, field, "положительное значение обращается в 0 при переводе в float32")
        return value

    def num(self, row, ctx, field, lo=0, hi=3.4028234663852886e38, default=None, positive=False):
        value = row.get(field, default)
        if ctx[0].endswith(".csv") and value is not None:
            try:
                value = float(value) if value != "" else (default if default is not None else 0)
            except (ValueError, TypeError):
                pass
        return self.number(value, ctx, field, lo, hi, positive=positive)

    def integer(self, row, ctx, field, lo=0, hi=65535, default=None, positive=False):
        value = row.get(field, default)
        if ctx[0].endswith(".csv") and value is not None:
            try:
                value = int(value) if value != "" else (default if default is not None else 0)
            except (ValueError, TypeError):
                pass
        return self.number(value, ctx, field, lo, hi, integer=True, positive=positive)

    def money(self, row, ctx, field, lo=0, default=None, positive=False):
        return self.integer(row, ctx, field, lo, 2147483647, default, positive)

    def boolean(self, value, ctx, field):
        if type(value) is not bool:
            self.error(ctx, field, "ожидался bool")
            return 0
        return int(value)

    def text(self, value, ctx, field, ascii_only=False):
        if not isinstance(value, str) or "\0" in value:
            self.error(ctx, field, "ожидалась строка без NUL")
            return ""
        try:
            value.encode("ascii" if ascii_only else "utf-8")
        except UnicodeError:
            self.error(ctx, field, "ожидалась строка ASCII" if ascii_only else "некорректный UTF-8")
            return ""
        return value

    def ident(self, value, ctx, field="id"):
        if not isinstance(value, str) or not ID_RE.fullmatch(value):
            self.error(ctx, field, "неверный id: ожидался [a-z][a-z0-9_]*")
            return ""
        return value

    def name(self, value, ctx, field, nullable=False):
        if nullable and value is None:
            return NONE
        value = self.text(value, ctx, field, ascii_only=True)
        if value not in self.names:
            if len(self.names) >= NONE:
                self.error(ctx, field, "NAME: превышен лимит 65535 строк")
            self.names[value] = len(self.names)
        return self.names[value]

    def string(self, value, ctx, field):
        if not isinstance(value, str) or value not in self.string_keys:
            self.error(ctx, field, f"нет строки в strings_ru.json: {value!r}")
            return 0
        return self.string_keys[value]

    def enum(self, value, kind, ctx, field):
        if value not in ENUMS[kind]:
            self.error(ctx, field, f"неизвестное значение enum {kind}: {value!r}")
            return 0
        return ENUMS[kind].index(value)

    def mask(self, value, kind, ctx, field):
        result = 0
        for i, item in enumerate(self.seq(value, ctx, field)):
            result |= 1 << self.enum(item, kind, ctx, f"{field}[{i}]")
        return result

    def ref(self, value, table, ctx, field, nullable=False):
        if nullable and value in (None, ""):
            return NONE
        if not isinstance(value, str) or value not in self.index[table]:
            self.error(ctx, field, f"битая ссылка {value!r} в {table}")
            return NONE
        return self.index[table][value]

    def day(self, value, ctx, field, month_day=False):
        pattern = r"\d{2}-\d{2}" if month_day else r"\d{4}-\d{2}-\d{2}"
        try:
            if not isinstance(value, str) or not re.fullmatch(pattern, value):
                raise ValueError
            parsed = date.fromisoformat("2000-" + value if month_day else value)
            return parsed.month * 100 + parsed.day if month_day else (parsed - date(2000, 1, 1)).days
        except ValueError:
            self.error(ctx, field, f"несуществующая дата {value!r}")
            return 0

    def read_json(self, path, required=False):
        if not path.exists() and not required:
            return []
        try:
            return json.loads(path.read_text(encoding="utf-8-sig"), object_pairs_hook=JsonObject)
        except (OSError, UnicodeError, ValueError, RecursionError) as exc:
            self.error((str(path), "<file>"), "json", f"не удалось прочитать JSON: {exc}")
            return {}

    def read_csv(self, path, tag):
        if not path.exists():
            return []
        ctx = (str(path), "<header>")
        try:
            with path.open(encoding="utf-8-sig", newline="") as stream:
                reader = csv.DictReader(stream, strict=True)
                fields = reader.fieldnames or []
                for field in fields:
                    if field not in CSV_FIELDS[tag]:
                        self.error(ctx, field, "неизвестное поле CSV")
                    if fields.count(field) > 1:
                        self.error(ctx, field, "дубликат столбца CSV")
                for field in CSV_FIELDS[tag]:
                    if field not in fields:
                        self.error(ctx, field, "обязательный столбец отсутствует")
                rows = list(reader)
                for i, row in enumerate(rows):
                    rctx = (str(path), str(row.get("id", f"<row:{i}>")))
                    if None in row:
                        self.error(rctx, "<columns>", "лишние ячейки CSV")
                        row.pop(None)
                    for field, value in row.items():
                        if value is None:
                            self.error(rctx, field, "не хватает ячейки CSV")
                return rows
        except (OSError, UnicodeError, csv.Error) as exc:
            self.error(ctx, "csv", f"не удалось прочитать CSV: {exc}")
            return []

    def add_table(self, tag, values, path, limit):
        values = self.seq(values, (str(path), "<table>"), "records", maximum=limit)
        for i, value in enumerate(values):
            if not isinstance(value, dict):
                self.error((str(path), f"<row:{i}>"), "record", "ожидался объект")
                continue
            ctx = (str(path), str(value.get("id", f"<row:{i}>")))
            ident = self.ident(value.get("id"), ctx)
            if ident in self.index[tag]:
                self.error(ctx, "id", "дубликат id в таблице " + tag)
            else:
                self.index[tag][ident] = len(self.rows[tag])
            self.rows[tag].append((value, ctx))

    def load(self):
        for tag, (filename, limit) in TABLES.items():
            self.rows[tag], self.index[tag] = [], {}
            path = self.root / filename
            if tag == "EVNT":
                for source in sorted(path.glob("*.json"), key=lambda p: p.name.encode("utf-8")):
                    values = self.read_json(source)
                    self.add_table(tag, [values] if isinstance(values, dict) else values, source, limit)
                if len(self.rows[tag]) > limit:
                    self.error((str(path), "<table>"), "records", f"EVNT: превышен лимит {limit}")
            else:
                values = self.read_csv(path, tag) if tag in CSV_FIELDS else self.read_json(path)
                self.add_table(tag, values, path, limit)
        self.calendar_ctx = (str(self.root / "calendar.json"), "calendar")
        self.calendar = self.obj(self.read_json(self.root / "calendar.json", True), self.calendar_ctx,
                                 "", ["start", "periods", "holidays"], ["start", "periods", "holidays"])
        self.rows["PERI"], self.index["PERI"] = [], {}
        self.add_table("PERI", self.calendar.get("periods", []), self.root / "calendar.json", NONE)
        self.economy_ctx = (str(self.root / "economy.json"), "economy")
        self.economy = self.read_json(self.root / "economy.json", True)
        path = self.root / "strings_ru.json"
        raw = self.read_json(path, True)
        if not isinstance(raw, dict):
            self.error((str(path), "strings"), "strings", "ожидался объект ключ → строка")
            raw = {}
        for key in getattr(raw, "duplicates", ()):
            self.error((str(path), key), "key", "дубликат ключа строки")
        hashes = {}
        for key, value in raw.items():
            ctx = (str(path), key)
            checked_key = self.text(key, ctx, "key")
            self.string_keys[key] = len(self.strings)
            self.strings.append(self.text(value, ctx, "value"))
            h = fnv1a32(key) if checked_key == key else None
            if h is not None and h in hashes and hashes[h] != key:
                self.error(ctx, "key", f"коллизия FNV-1a: {hashes[h]!r} и {key!r}")
            if h is not None:
                hashes[h] = key
        if len(self.strings) > NONE:
            self.error((str(path), "strings"), "STRS", "превышен лимит 65535 строк")
        flag_names = set()

        def collect(value):
            pending = [value]
            while pending:
                current = pending.pop()
                if isinstance(current, dict):
                    candidates = [current.get("var")]
                    if isinstance(current.get("per"), dict):
                        candidates.extend(current["per"])
                    expr = current.get("expr")
                    if isinstance(expr, str) and (match := EXPR_RE.fullmatch(expr)):
                        candidates.append(match[2])
                    for var in candidates:
                        if isinstance(var, str) and var.startswith("flag:") and ID_RE.fullmatch(var[5:]):
                            flag_names.add(var[5:])
                    if current.get("type") == "flag" and isinstance(current.get("id"), str) and ID_RE.fullmatch(current["id"]):
                        flag_names.add(current["id"])
                        self.flags_set.add(current["id"])
                    pending.extend(current.values())
                elif isinstance(current, list):
                    pending.extend(current)

        for row, _ in self.rows["REGU"]:
            collect(row.get("unlock", []))
        for row, _ in self.rows["EVNT"]:
            collect(row)
        self.flags = {name: i for i, name in enumerate(sorted(flag_names, key=lambda s: s.encode("utf-8")))}
        if len(self.flags) > 512:
            self.error((str(self.root / "events"), "<table>"), "flags", "FLAG: превышен лимит 512")

    def pack(self, record, *values):
        # Даже после ошибки продолжаем обход всех записей и сбор диагностики.
        return b"" if self.errors else struct.pack(RECORD_FORMATS[record], *values)

    def append(self, tag, record, *values):
        self.records[tag].append(self.pack(record, *values))

    def span(self, tag, records, ctx, field):
        first = len(self.records[tag]) if records else 0
        self.records[tag].extend(records)
        if len(self.records[tag]) > NONE:
            self.error(ctx, field, f"{tag}: превышен общий лимит 65535 записей")
        return first, len(records)

    def variable(self, value, ctx, field):
        if not isinstance(value, str):
            self.error(ctx, field, "ожидалось имя переменной")
            return 0, NONE
        name, separator, arg = value.partition(":")
        var = self.enum(name, "var", ctx, field)
        targets = {"relation": "REGU", "game_installed": "GAME", "visits_archetype": "ARCH"}
        if name == "flag":
            ident = self.ident(arg, ctx, field)
            if ident:
                self.flags_used.setdefault(ident, (ctx, field))
            return var, self.flags.get(ident, NONE)
        if name in targets:
            return var, self.ref(arg, targets[name], ctx, field)
        if separator:
            self.error(ctx, field, f"переменная {name!r} не принимает параметр")
        return var, NONE

    def conditions(self, value, ctx, field):
        records = []
        for i, raw in enumerate(self.seq(value, ctx, field)):
            p = f"{field}[{i}]"
            row = self.obj(raw, ctx, p, ["var", "op", "val"], ["var", "op", "val"])
            var, arg = self.variable(row.get("var"), ctx, p + ".var")
            op = self.enum(row.get("op"), "op", ctx, p + ".op")
            val = row.get("val")
            if var == ENUMS["var"].index("date"):
                val = self.day(val, ctx, p + ".val")
            else:
                val = self.number(val, ctx, p + ".val", 0 if var == 9 else -3.4028234663852886e38,
                                  1 if var == 9 else 3.4028234663852886e38)
                if var == 9 and val not in (0, 1):
                    self.error(ctx, p + ".val", "flag: ожидалось 0 или 1")
            records.append(self.pack("CondRec", var, op, arg, val))
        return self.span("COND", records, ctx, field)

    def pair(self, row, ctx, field, lo, hi, integer=False):
        values = self.seq(row.get(field), ctx, field, exact=2)
        result = [self.number(v, ctx, f"{field}[{i}]", lo, hi, integer) for i, v in enumerate(values)]
        if len(result) != 2:
            return 0, 0
        if result[0] > result[1]:
            self.error(ctx, field, "минимум больше максимума")
        return tuple(result)

    def games(self):
        fields = "id title ref genre release price_license price_pirate req net pop minigame score_scale tags box_tex".split()
        for row, ctx in self.rows["GAME"]:
            self.obj(row, ctx, "", fields, [f for f in fields if f not in ("ref", "score_scale")])
            req = self.obj(row.get("req"), ctx, "req", ["cpu", "gpu", "ram", "hdd_mb"], ["cpu", "gpu", "ram", "hdd_mb"])
            pop = self.obj(row.get("pop"), ctx, "pop", ["peak", "floor", "rise_days", "half_life_days"], ["peak", "floor", "rise_days", "half_life_days"])
            peak = self.number(pop.get("peak"), ctx, "pop.peak", 0, positive=True)
            floor = self.number(pop.get("floor"), ctx, "pop.floor", 0)
            if floor > peak:
                self.error(ctx, "pop.floor", "floor должен быть ≤ peak")
            requirements = [self.number(req.get(f), ctx, "req." + f, 0, 4294967295 if f == "hdd_mb" else 65535, True) for f in ("cpu", "gpu", "ram", "hdd_mb")]
            rise = self.number(pop.get("rise_days"), ctx, "pop.rise_days", 0, 65535, True)
            half = self.number(pop.get("half_life_days"), ctx, "pop.half_life_days", 0, 65535, True)
            self.append("GAME", "GameRec", self.name(row.get("id"), ctx, "id"), self.string(row.get("title"), ctx, "title"),
                        self.enum(row.get("genre"), "genre", ctx, "genre"), self.enum(row.get("net"), "net", ctx, "net"),
                        self.mask(row.get("tags"), "tags", ctx, "tags"), self.day(row.get("release"), ctx, "release"),
                        self.money(row, ctx, "price_license"), self.money(row, ctx, "price_pirate"), *requirements[:3],
                        self.ref(row.get("minigame"), "MINI", ctx, "minigame", True), requirements[3], peak, floor, rise, half,
                        self.name(row.get("box_tex"), ctx, "box_tex", True), self.num(row, ctx, "score_scale", lo=-3.4028234663852886e38, default=0))

    def archetypes(self):
        fields = "id name base_rate hourly weekday season money price_expect elasticity patience skill session_hours extend alt_accept affinity virus hunger rep_weight flags models".split()
        for row, ctx in self.rows["ARCH"]:
            self.obj(row, ctx, "", fields, [f for f in fields if f not in ("season", "affinity", "alt_accept", "rep_weight")])
            hourly = self.seq(row.get("hourly"), ctx, "hourly", exact=24)
            weekday = self.seq(row.get("weekday"), ctx, "weekday", exact=7)
            hourly = [self.number(v, ctx, f"hourly[{i}]", 0) for i, v in enumerate(hourly)]
            weekday = [self.number(v, ctx, f"weekday[{i}]", 0) for i, v in enumerate(weekday)]
            season = self.obj(row.get("season", {}), ctx, "season", ["holidays", "session"])
            seasons = [self.number(season.get(f, 1), ctx, "season." + f, 0) for f in ("holidays", "session")]
            affinity = self.obj(row.get("affinity", {}), ctx, "affinity", GENRES)
            affinities = [self.number(affinity.get(g, 1), ctx, "affinity." + g, 0) for g in GENRES]
            models = self.seq(row.get("models"), ctx, "models", maximum=4, minimum=1)
            model_ids = [self.name(v, ctx, f"models[{i}]") for i, v in enumerate(models)]
            self.append("ARCH", "ArchetypeRec", self.name(row.get("id"), ctx, "id"), self.string(row.get("name"), ctx, "name"),
                        self.num(row, ctx, "base_rate"), *hourly, *weekday, *seasons,
                        *self.pair(row, ctx, "money", 0, 2147483647, True), self.money(row, ctx, "price_expect", positive=True),
                        self.num(row, ctx, "elasticity"), *self.pair(row, ctx, "patience", 1, 65535, True),
                        *self.pair(row, ctx, "skill", 0, 1), *self.pair(row, ctx, "session_hours", 1, 24, True), len(models), 0,
                        self.num(row, ctx, "extend", hi=1), self.num(row, ctx, "alt_accept", hi=1, default=0.6), *affinities,
                        self.num(row, ctx, "virus", hi=1), self.num(row, ctx, "hunger", hi=1), self.num(row, ctx, "rep_weight", default=0.3),
                        self.mask(row.get("flags"), "flags", ctx, "flags"), *(model_ids + [NONE] * (4 - len(models))))

    def regulars(self):
        fields = "id name nick archetype model palette fav_game fav_minigame skill unlock traits event_pool".split()
        for row, ctx in self.rows["REGU"]:
            self.obj(row, ctx, "", fields, fields)
            nick = len(self.strings)
            self.strings.append(self.text(row.get("nick"), ctx, "nick"))
            if len(self.strings) > NONE:
                self.error(ctx, "nick", "STRS: превышен лимит 65535 строк")
            first, count = self.conditions(row.get("unlock"), ctx, "unlock")
            pool = self.seq(row.get("event_pool"), ctx, "event_pool")
            indices = [self.ref(v, "EVNT", ctx, f"event_pool[{i}]") for i, v in enumerate(pool)]
            pool_first, pool_count = self.span("LIST", indices, ctx, "event_pool")
            self.append("REGU", "RegularRec", self.name(row.get("id"), ctx, "id"), self.string(row.get("name"), ctx, "name"), nick,
                        self.ref(row.get("archetype"), "ARCH", ctx, "archetype"), self.name(row.get("model"), ctx, "model"),
                        self.ref(row.get("fav_game"), "GAME", ctx, "fav_game", True), self.ref(row.get("fav_minigame"), "MINI", ctx, "fav_minigame", True),
                        self.integer(row, ctx, "palette", hi=255), count, first, pool_first, pool_count,
                        self.mask(row.get("traits"), "traits", ctx, "traits"), self.num(row, ctx, "skill", hi=1))

    def hardware(self):
        for row, ctx in self.rows["HARD"]:
            self.obj(row, ctx, "", CSV_FIELDS["HARD"])
            kind = self.enum(row.get("type"), "component", ctx, "type")
            score = self.integer(row, ctx, "score", default=0, positive=kind in (0, 1))
            ram = self.integer(row, ctx, "ram_mb", default=0, positive=kind == 2)
            hdd = self.integer(row, ctx, "hdd_mb", hi=4294967295, default=0, positive=kind == 3)
            self.append("HARD", "HardwareRec", self.name(row.get("id"), ctx, "id"), self.string(row.get("name"), ctx, "name"), kind,
                        self.integer(row, ctx, "comfort", -128, 127, 0), score, self.day(row.get("release"), ctx, "release"),
                        self.money(row, ctx, "base_price", positive=True), ram, self.integer(row, ctx, "power_w", default=0), hdd,
                        self.num(row, ctx, "wear_rate", default=0), self.num(row, ctx, "fail_base", default=0))

    def goods(self):
        for row, ctx in self.rows["GOOD"]:
            self.obj(row, ctx, "", CSV_FIELDS["GOOD"])
            self.append("GOOD", "GoodsRec", self.name(row.get("id"), ctx, "id"), self.string(row.get("name"), ctx, "name"),
                        self.money(row, ctx, "buy_price", positive=True), self.money(row, ctx, "sell_price", positive=True),
                        self.num(row, ctx, "weight", default=1))

    def upgrades(self):
        parents = []
        for row, ctx in self.rows["UPGR"]:
            self.obj(row, ctx, "", CSV_FIELDS["UPGR"])
            kind = self.enum(row.get("kind"), "kind", ctx, "kind")
            parent = self.ref(row.get("requires"), "UPGR", ctx, "requires", True)
            parents.append(parent)
            arg = self.ref(row.get("archetype"), "ARCH", ctx, "archetype", True)
            if arg != NONE and kind not in (6, 7):
                self.error(ctx, "archetype", "параметр допустим только для sign/ad")
            value = self.num(row, ctx, "value", default=0, hi=16 if kind == 0 else 3.4028234663852886e38)
            if kind == 0 and value != int(value):
                self.error(ctx, "value", "число мест зала должно быть целым 0..16")
            level = self.integer(row, ctx, "level", hi=255, default=0)
            if kind == 2 and level not in (1, 2, 3):
                self.error(ctx, "level", "network: допустимо 1..3")
            self.append("UPGR", "UpgradeRec", self.name(row.get("id"), ctx, "id"), self.string(row.get("name"), ctx, "name"), kind,
                        level, arg, parent, self.integer(row, ctx, "days", default=0, positive=kind == 7),
                        *[self.money(row, ctx, f, default=0) for f in ("price", "cost_month", "cost_day", "cost_hour")],
                        self.num(row, ctx, "cost_minute", default=0), value)
        for i, (_, ctx) in enumerate(self.rows["UPGR"]):
            seen, current = set(), i
            while current != NONE and current < len(parents):
                if current in seen:
                    self.error(ctx, "requires", "цикл зависимостей апгрейдов (включая ссылку на себя)")
                    break
                seen.add(current)
                current = parents[current]

    def minigames(self):
        fields = ["id", "name", "score_scale", "invert", "where"]
        for row, ctx in self.rows["MINI"]:
            self.obj(row, ctx, "", fields, [f for f in fields if f != "invert"])
            self.append("MINI", "MinigameRec", self.name(row.get("id"), ctx, "id"), self.string(row.get("name"), ctx, "name"),
                        self.boolean(row.get("invert", False), ctx, "invert"), self.mask(row.get("where"), "where", ctx, "where"),
                        self.num(row, ctx, "score_scale", positive=True))

    def either_ref(self, value, tables, ctx, field):
        matches = [(i, self.index[tag][value]) for i, tag in enumerate(tables) if isinstance(value, str) and value in self.index[tag]]
        if len(matches) != 1:
            self.error(ctx, field, f"{'неоднозначная' if matches else 'битая'} ссылка {value!r} в {'/'.join(tables)}")
            return 0, NONE
        return matches[0]

    def effects(self, value, ctx, field, depth=0):
        rows = self.seq(value, ctx, field)
        # Сначала резервируем соседей: then не должен разрывать список родителя.
        first, count = self.span("EFFE", [b""] * len(rows), ctx, field)
        for i, raw in enumerate(rows):
            p = f"{field}[{i}]"
            row = self.obj(raw, ctx, p, ["type", "val", "expr", "id", "regular", "target", "mult", "days", "archetype", "in_days", "hour", "filter", "pc", "component", "count", "p", "then"], ["type"])
            kind = row.get("type")
            eff_type = self.enum(kind, "effect", ctx, p + ".type")
            required = {
                "money": [], "rep": ["val"], "flag": ["id", "val"], "relation": ["regular", "val"],
                "modifier": ["target", "mult", "days"], "spawn_visitor": [], "schedule_event": ["id", "in_days"],
                "uninstall": ["filter"], "damage_pc": ["pc", "component"], "item": ["id", "count"],
                "unlock": ["id"], "chance": ["p", "then"], "start_minigame": ["id"],
            }
            # Недопустимые параметры известного типа тоже считаются опечатками.
            extra = {"money": ["val", "expr"], "spawn_visitor": ["archetype", "regular"], "schedule_event": ["hour"]}
            if isinstance(kind, str) and kind in required:
                allowed = ["type"] + required[kind] + extra.get(kind, [])
                for key in row:
                    if key not in allowed:
                        self.error(ctx, p + "." + key, f"неизвестное поле для эффекта {kind}")
                for key in required[kind]:
                    if key not in row:
                        self.error(ctx, p + "." + key, "обязательное поле отсутствует")
            sub, hour, child_count, ref, arg, child_first, ival, fval = 0, 255, 0, NONE, NONE, 0, 0, 0
            number = lambda key, lo=-3.4028234663852886e38, hi=3.4028234663852886e38, integer=False, positive=False: self.number(row.get(key), ctx, p + "." + key, lo, hi, integer, positive)
            if kind == "money":
                if ("val" in row) == ("expr" in row):
                    self.error(ctx, p + ".val", "нужно ровно одно из val/expr")
                if "expr" in row:
                    expr = row["expr"]
                    match = EXPR_RE.fullmatch(expr) if isinstance(expr, str) else None
                    if not match:
                        self.error(ctx, p + ".expr", "ожидалось выражение '<число> * <переменная>'")
                    else:
                        sub = 1
                        fval = self.number(float(match[1]), ctx, p + ".expr")
                        ref, arg = self.variable(match[2], ctx, p + ".expr")
                else:
                    ival = number("val", -2147483648, 2147483647, True)
            elif kind == "rep":
                fval = number("val")
            elif kind == "flag":
                flag = self.ident(row.get("id"), ctx, p + ".id")
                ref = self.flags.get(flag, NONE)
                ival = number("val", 0, 1, True)
            elif kind == "relation":
                ref = self.ref(row.get("regular"), "REGU", ctx, p + ".regular")
                ival = number("val", -2147483648, 2147483647, True)
            elif kind == "modifier":
                target = row.get("target")
                name, sep, param = target.partition(":") if isinstance(target, str) else (None, "", "")
                sub = self.enum(name, "target", ctx, p + ".target")
                if name == "pop" or (name == "arrivals" and sep):
                    arg = self.ref(param, "GAME" if name == "pop" else "ARCH", ctx, p + ".target")
                elif sep:
                    self.error(ctx, p + ".target", "цель не принимает параметр")
                fval = number("mult", 0, positive=True)
                ival = number("days", 1, 2147483647, True)
            elif kind == "spawn_visitor":
                if ("regular" in row) == ("archetype" in row):
                    self.error(ctx, p + ".archetype", "нужно ровно одно из archetype/regular")
                sub = int("regular" in row)
                key = "regular" if sub else "archetype"
                ref = self.ref(row.get(key), "REGU" if sub else "ARCH", ctx, p + "." + key)
            elif kind == "schedule_event":
                ref = self.ref(row.get("id"), "EVNT", ctx, p + ".id")
                ival = number("in_days", 0, 2147483647, True)
                if "hour" in row:
                    hour = number("hour", 0, 23, True)
            elif kind == "uninstall":
                value = row.get("filter")
                if isinstance(value, str) and value.startswith("game:"):
                    sub, ref = 2, self.ref(value[5:], "GAME", ctx, p + ".filter")
                elif value in ("pirate", "all"):
                    sub = int(value == "all")
                else:
                    self.error(ctx, p + ".filter", "неизвестное значение enum filter")
            elif kind == "damage_pc":
                ref = NONE if row.get("pc") == "random" else number("pc", 0, 15, True)
                arg = NONE if row.get("component") == "random" else self.enum(row.get("component"), "component", ctx, p + ".component")
            elif kind in ("item", "unlock"):
                sub, ref = self.either_ref(row.get("id"), ("HARD", "GOOD") if kind == "item" else ("UPGR", "GAME"), ctx, p + ".id")
                if kind == "item":
                    ival = number("count", -2147483648, 2147483647, True)
            elif kind == "chance":
                fval = number("p", 0, 1)
                if depth >= 4:
                    self.error(ctx, p + ".then", "превышен лимит глубины chance: 4")
                else:
                    child_first, child_count = self.effects(row.get("then"), ctx, p + ".then", depth + 1)
            elif kind == "start_minigame":
                value = row.get("id")
                if value in ENUMS["service"]:
                    if value in self.index["MINI"]:
                        self.error(ctx, p + ".id", f"неоднозначная ссылка {value!r}: MINI и ServiceGame")
                    sub, ref = 1, ENUMS["service"].index(value)
                else:
                    ref = self.ref(value, "MINI", ctx, p + ".id")
            self.records["EFFE"][first + i] = self.pack("EffectRec", eff_type, sub, hour, child_count, ref, arg, child_first, ival, fval)
        return first, count

    def events(self):
        fields = "id category trigger weight chance cooldown_days once conditions present choices".split()
        for row, ctx in self.rows["EVNT"]:
            self.obj(row, ctx, "", fields, ["id", "category", "trigger", "present", "choices"])
            trigger = self.obj(row.get("trigger"), ctx, "trigger", ["type", "hour", "date", "action"], ["type"])
            trig = self.enum(trigger.get("type"), "trigger", ctx, "trigger.type")
            hour, trig_date, action = 255, 0, 0
            if "hour" in trigger or trig == 0:
                hour = self.number(trigger.get("hour"), ctx, "trigger.hour", 0, 23, True)
            if trig == 1 or "date" in trigger:
                converted = self.day(trigger.get("date"), ctx, "trigger.date")
                if trig == 1:
                    trig_date = converted
            if trig == 2 or "action" in trigger:
                converted = self.enum(trigger.get("action"), "action", ctx, "trigger.action")
                if trig == 2:
                    action = converted
            chance = self.obj(row.get("chance", {}), ctx, "chance", ["base", "per"], ["base"] if "chance" in row else [])
            per = chance.get("per", {})
            if not isinstance(per, dict):
                self.error(ctx, "chance.per", "ожидался объект переменная → коэффициент")
                per = {}
            if len(per) > 255:
                self.error(ctx, "chance.per", "превышен лимит 255")
            for key in getattr(per, "duplicates", ()):
                self.error(ctx, "chance.per." + key, "дубликат переменной")
            terms = []
            for key, coef in per.items():
                var, arg = self.variable(key, ctx, "chance.per." + key)
                terms.append(self.pack("ChanceTermRec", var, arg, self.number(coef, ctx, "chance.per." + key)))
            chance_first, chance_count = self.span("CHNC", terms, ctx, "chance.per")
            cond_first, cond_count = self.conditions(row.get("conditions", []), ctx, "conditions")
            present = self.obj(row.get("present"), ctx, "present", ["kind", "model", "text"], ["kind", "text"])
            choices = []
            for i, raw in enumerate(self.seq(row.get("choices"), ctx, "choices", minimum=1)):
                p = f"choices[{i}]"
                choice = self.obj(raw, ctx, p, ["text", "requires", "effects"], ["text"])
                req_first, req_count = self.conditions(choice.get("requires", []), ctx, p + ".requires")
                effect_first, effect_count = self.effects(choice.get("effects", []), ctx, p + ".effects")
                choices.append(self.pack("ChoiceRec", self.string(choice.get("text"), ctx, p + ".text"), req_count, effect_count, req_first, effect_first))
            choice_first, choice_count = self.span("CHOI", choices, ctx, "choices")
            self.append("EVNT", "EventRec", self.name(row.get("id"), ctx, "id"), self.enum(row.get("category"), "category", ctx, "category"), trig,
                        hour, action, self.boolean(row.get("once", False), ctx, "once"), self.enum(present.get("kind"), "present", ctx, "present.kind"),
                        trig_date, self.num(row, ctx, "weight", default=1, positive=True), self.number(chance.get("base", 1), ctx, "chance.base", 0, 1),
                        self.integer(row, ctx, "cooldown_days", default=0), cond_first, chance_first, choice_first, cond_count, chance_count, choice_count, 0,
                        self.name(present.get("model"), ctx, "present.model", True), self.string(present.get("text"), ctx, "present.text"))

    def calendar_and_economy(self):
        for row, ctx in self.rows["PERI"]:
            self.obj(row, ctx, "periods", ["id", "tag", "from", "to"], ["id", "tag", "from", "to"])
            self.append("PERI", "PeriodRec", self.name(row.get("id"), ctx, "id"), self.enum(row.get("tag"), "tag", ctx, "tag"),
                        self.day(row.get("from"), ctx, "from", True), self.day(row.get("to"), ctx, "to", True))
        holidays = self.seq(self.calendar.get("holidays", []), self.calendar_ctx, "holidays", maximum=4294967295)
        self.records["HOLI"] = [self.day(v, self.calendar_ctx, f"holidays[{i}]", True) for i, v in enumerate(holidays)]
        ctx = self.economy_ctx
        fields = "start_money start_rep start_price_hour start_price_night tax_rate electricity_price loan_rate_week debt_limit debt_days_to_lose start_upgrades start_pcs start_pc".split()
        row = self.obj(self.economy, ctx, "", fields, fields)
        upgrades = self.seq(row.get("start_upgrades"), ctx, "start_upgrades")
        indices = [self.ref(v, "UPGR", ctx, f"start_upgrades[{i}]") for i, v in enumerate(upgrades)]
        halls = [self.rows["UPGR"][i][0] for i in indices if i != NONE and self.rows["UPGR"][i][0].get("kind") == "hall"]
        if len(halls) != 1:
            self.error(ctx, "start_upgrades", "должен быть ровно один hall")
        first, count = self.span("LIST", indices, ctx, "start_upgrades")
        pcs = self.integer(row, ctx, "start_pcs", hi=16)
        if len(halls) == 1:
            try:
                if pcs > float(halls[0].get("value", 0)):
                    self.error(ctx, "start_pcs", "больше мест стартового зала")
            except (ValueError, TypeError):
                pass
        pc = self.obj(row.get("start_pc"), ctx, "start_pc", COMPONENTS, COMPONENTS[:8])
        components = []
        for component in COMPONENTS:
            index = self.ref(pc.get(component), "HARD", ctx, "start_pc." + component, component not in pc)
            if index != NONE and self.rows["HARD"][index][0].get("type") != component:
                self.error(ctx, "start_pc." + component, "тип железа не совпадает с ключом")
            components.append(index)
        self.append("ECON", "EconomyRec", self.day(self.calendar.get("start"), self.calendar_ctx, "start"),
                    self.money(row, ctx, "start_money", -2147483648), self.num(row, ctx, "start_rep", hi=100),
                    self.money(row, ctx, "start_price_hour", positive=True), self.money(row, ctx, "start_price_night", positive=True),
                    self.num(row, ctx, "tax_rate", hi=1), self.num(row, ctx, "electricity_price"), self.num(row, ctx, "loan_rate_week", hi=1),
                    self.money(row, ctx, "debt_limit", positive=True), self.integer(row, ctx, "debt_days_to_lose", positive=True), first, count, pcs, *components)

    def validate(self):
        self.load()
        self.games()
        self.archetypes()
        self.regulars()
        self.hardware()
        self.goods()
        self.upgrades()
        self.minigames()
        self.events()
        self.calendar_and_economy()
        for flag in self.flags:
            self.records["FLAG"].append(self.name(flag, (str(self.root / "events"), flag), "flag"))
        for flag, (ctx, field) in self.flags_used.items():
            if flag not in self.flags_set:
                self.warnings.append(f"{ctx[0]}: {ctx[1]}.{field}: флаг {flag!r} проверяется, но нигде не ставится")
        if len(self.strings) > NONE:
            self.error((str(self.root / "strings_ru.json"), "strings"), "STRS", "превышен лимит 65535 строк (включая nick)")

    @staticmethod
    def string_blob(values):
        encoded = [s.encode("utf-8") + b"\0" for s in values]
        offsets, offset = [], 4 * len(values)
        for value in encoded:
            offsets.append(offset)
            offset += len(value)
        return struct.pack(f"<{len(offsets)}I", *offsets) + b"".join(encoded)

    def build(self):
        self.validate()
        if self.errors:
            return None
        self.records["STRK"] = [struct.pack(RECORD_FORMATS["StringKeyRec"], h, index)
                                for h, index in sorted((fnv1a32(key), index) for key, index in self.string_keys.items())]
        directory, payload = bytearray(), bytearray()
        cursor = 16 + 20 * len(SECTIONS)
        cursor += (-cursor) % 16
        payload.extend(b"\0" * (cursor - 16 - 20 * len(SECTIONS)))
        for tag, record in SECTIONS:
            if tag in ("STRS", "NAME"):
                values = self.strings if tag == "STRS" else list(self.names)
                data, count, size = self.string_blob(values), len(values), 1
            else:
                values = self.records[tag]
                size = RECORD_SIZES[record] if record else 2
                data = b"".join(values) if record else struct.pack(f"<{len(values)}H", *values)
                count = len(values)
            directory.extend(struct.pack(RECORD_FORMATS["PakSection"], tag.encode("ascii"), cursor, len(data), count, size, 0))
            payload.extend(data)
            cursor += len(data)
            padding = (-cursor) % 16
            payload.extend(b"\0" * padding)
            cursor += padding
        body = directory + payload
        blob = struct.pack(RECORD_FORMATS["PakHeader"], b"PCCD", 1, len(SECTIONS), 16 + len(body), zlib.crc32(body)) + body
        if len(blob) > 1024 * 1024:
            self.warnings.append(f"data.pak: размер {len(blob)} байт превышает бюджет 1 МиБ")
        return blob


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="каталог JSON/CSV")
    parser.add_argument("-o", "--output", type=Path, required=True, help="выходной data.pak")
    parser.add_argument("--report", action="store_true", help="показать количество записей и размеры")
    args = parser.parse_args(argv)
    builder = Builder(args.source)
    blob = builder.build()
    for warning in builder.warnings:
        print("Предупреждение: " + warning, file=sys.stderr)
    if builder.errors:
        for error in builder.errors:
            print(error, file=sys.stderr)
        return 1
    temporary = None
    try:
        # replace только после полной записи: существующий pak переживёт и ошибку ввода-вывода.
        with tempfile.NamedTemporaryFile(dir=args.output.parent, prefix=args.output.name + ".", suffix=".tmp", delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(blob)
        os.replace(temporary, args.output)
    except OSError as exc:
        print(f"{args.output}: <output>.file: не удалось записать pak: {exc}", file=sys.stderr)
        return 1
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()
    if args.report:
        for tag, _ in SECTIONS:
            count = len(builder.strings) if tag == "STRS" else len(builder.names) if tag == "NAME" else len(builder.records[tag])
            print(f"{tag}: {count}")
        print(f"file_size={len(blob)} bytes")
        print(f"STRS_size={len(builder.string_blob(builder.strings))} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
