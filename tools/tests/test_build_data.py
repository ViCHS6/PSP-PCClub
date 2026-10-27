"""Независимая проверка data.pak v1 и диагностики исходников."""

import ast
import copy
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools" / "build_data.py"
FIXTURES = Path(__file__).with_name("data_build")
TAGS = "GAME ARCH REGU HARD GOOD UPGR MINI EVNT CHOI EFFE COND CHNC LIST PERI HOLI FLAG ECON STRS STRK NAME".split()
SIZES = [52, 256, 32, 32, 16, 36, 12, 36, 8, 20, 8, 8, 2, 8, 2, 2, 64, 1, 8, 1]
CASES = {case["name"]: case for case in json.loads((FIXTURES / "broken_cases.json").read_text(encoding="utf-8"))}


def key_hash(key):
    value = 2166136261
    for byte in key.encode("utf-8"):
        value ^= byte
        value = value * 16777619 % (2 ** 32)
    return value


class Pak:
    """Разбирает байты через struct; упаковщик здесь не импортируется."""

    def __init__(self, blob):
        self.blob = blob
        self.header = struct.unpack_from("<4sHHII", blob)
        self.directory = [struct.unpack_from("<4sIIIHH", blob, 16 + 20 * i)
                          for i in range(self.header[2])]
        self.sections = {entry[0].decode("ascii"): entry[1:] for entry in self.directory}

    def data(self, tag):
        offset, size, _, _, _ = self.sections[tag]
        return self.blob[offset:offset + size]

    def unpack(self, tag, fmt, offset=0):
        return struct.unpack_from("<" + fmt, self.data(tag), offset)

    def strings(self, tag):
        count = self.sections[tag][2]
        data = self.data(tag)
        offsets = struct.unpack_from(f"<{count}I", data)
        return [data[offset:data.index(b"\0", offset)].decode("utf-8") for offset in offsets]


class BuildDataTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "source"
        shutil.copytree(FIXTURES / "valid", self.source)
        self.output = self.root / "data.pak"

    def read(self, filename):
        return json.loads((self.source / filename).read_text(encoding="utf-8"))

    def write(self, filename, value):
        (self.source / filename).write_text(json.dumps(value, ensure_ascii=False), encoding="utf-8")

    def run_builder(self, success=True, report=False):
        command = [sys.executable, "-B", str(SCRIPT), str(self.source), "-o", str(self.output)]
        if report:
            command.append("--report")
        process = subprocess.run(command, capture_output=True, encoding="utf-8",
                                 env={**os.environ, "PYTHONUTF8": "1", "PYTHONDONTWRITEBYTECODE": "1"})
        self.assertNotIn("Traceback", process.stderr)
        if success:
            self.assertEqual(process.returncode, 0, process.stderr)
            self.assertTrue(self.output.exists())
        else:
            self.assertNotEqual(process.returncode, 0, process.stdout)
        return process

    def reject(self, filename, ident, field, message=None):
        result = self.run_builder(False)
        self.assertIn(f"{self.source / filename}: {ident}.{field}:", result.stderr)
        if message:
            self.assertIn(message, result.stderr)
        self.assertFalse(self.output.exists())
        return result

    def broken_case(self, name):
        case = CASES[name]
        data = self.read(case["file"])
        if "append_copy" in case:
            data.append(copy.deepcopy(data[case["append_copy"]]))
        else:
            target = data
            for key in case["path"][:-1]:
                target = target[key]
            target[case["path"][-1]] = case["value"]
        self.write(case["file"], data)
        self.reject(case["file"], case["id"], case["field"], case["error"])

    def test_minimal_all_tables_binary_layout(self):
        process = self.run_builder(report=True)
        self.assertEqual(process.stderr, "")
        pak = Pak(self.output.read_bytes())
        magic, version, count, size, crc = pak.header
        self.assertEqual((magic, version, count, size), (b"PCCD", 1, 20, len(pak.blob)))
        self.assertEqual(crc, zlib.crc32(pak.blob[16:]))
        self.assertEqual([entry[0].decode("ascii") for entry in pak.directory], TAGS)
        previous_end = 16 + 20 * count
        for entry, expected_tag, expected_size in zip(pak.directory, TAGS, SIZES):
            tag, offset, size, rows, rec_size, pad = entry
            with self.subTest(tag=tag):
                self.assertEqual((offset % 16, rec_size, pad), (0, expected_size, 0))
                self.assertGreaterEqual(offset, previous_end)
                self.assertEqual(pak.blob[previous_end:offset], b"\0" * (offset - previous_end))
                self.assertLessEqual(offset + size, len(pak.blob))
                self.assertGreater(rows, 0)
                if expected_tag not in ("STRS", "NAME"):
                    self.assertEqual(size, rows * rec_size)
                else:
                    offsets = struct.unpack_from(f"<{rows}I", pak.data(expected_tag))
                    cursor = 4 * rows
                    for string_offset in offsets:
                        self.assertEqual(string_offset, cursor)
                        cursor = pak.data(expected_tag).index(b"\0", cursor) + 1
                    self.assertEqual(cursor, size)
                previous_end = offset + size
        self.assertEqual(pak.blob[previous_end:], b"\0" * (len(pak.blob) - previous_end))

        strings, names = pak.strings("STRS"), pak.strings("NAME")
        source_strings = self.read("strings_ru.json")
        self.assertEqual(strings, list(source_strings.values()) + ["Димон_2002"])
        self.assertEqual(len(names), len(set(names)))
        self.assertEqual(names.count("npc_teen"), 1)
        self.assertIn("box_test", names)
        game_id, title = pak.unpack("GAME", "HH")
        self.assertEqual((names[game_id], strings[title]), ("test_game", "Контр-Удар"))
        self.assertEqual(pak.unpack("GAME", "BBH", 4), (0, 1, 3))
        self.assertEqual(pak.unpack("GAME", "iii", 8), (517, 900, 70))
        self.assertEqual(pak.unpack("GAME", "4HI", 20), (40, 30, 64, 0, 400))
        self.assertAlmostEqual(pak.unpack("GAME", "f", 36)[0], 0.6)
        self.assertEqual(pak.data("GAME")[46:48], b"\0\0")
        self.assertEqual(pak.unpack("GAME", "f", 48), (300.0,))
        self.assertEqual(pak.unpack("ARCH", "ff", 132), (1.0, 1.0))
        self.assertEqual(pak.unpack("ARCH", "iii", 140), (30, 120, 15))
        self.assertEqual(pak.unpack("ARCH", "4B", 168), (1, 3, 1, 0))
        self.assertAlmostEqual(pak.unpack("ARCH", "f", 176)[0], 0.6)
        self.assertEqual(pak.unpack("ARCH", "13f", 180), (1.5,) + (1.0,) * 12)
        self.assertEqual(pak.unpack("ARCH", "I", 244), (3,))
        model, *unused = pak.unpack("ARCH", "4H", 248)
        self.assertEqual((names[model], unused), ("npc_teen", [65535] * 3))
        self.assertEqual(strings[pak.unpack("REGU", "H", 4)[0]], "Димон_2002")
        self.assertEqual(pak.unpack("REGU", "4H", 6), (0, model, 0, 0))
        self.assertEqual(pak.unpack("REGU", "BBHHB", 14), (2, 1, 0, 0, 1))
        self.assertEqual(pak.data("REGU")[21:24], b"\0" * 3)
        self.assertEqual(pak.unpack("REGU", "I", 24), (3,))
        self.assertEqual(pak.unpack("HARD", "BbHiiHHI", 4), (0, 0, 40, 60, 3000, 0, 30, 0))
        self.assertEqual(pak.unpack("HARD", "i", 4 * 32 + 8), (-1,))
        self.assertEqual(pak.unpack("GOOD", "iif", 4), (8, 12, 1.0))
        self.assertEqual(pak.unpack("UPGR", "BBHHH", 4), (0, 1, 65535, 65535, 0))
        self.assertEqual(pak.unpack("UPGR", "f", 32), (6.0,))
        self.assertEqual(pak.unpack("MINI", "BBHf", 4), (0, 3, 0, 300.0))
        self.assertEqual(pak.unpack("PERI", "BBHH", 2), (0, 0, 1229, 111))
        self.assertEqual(pak.unpack("HOLI", "2H"), (101, 229))
        self.assertEqual([names[i] for i in pak.unpack("FLAG", "H")], ["checked"])
        self.assertEqual(pak.unpack("ECON", "iifii", 0), (609, 20000, 30.0, 20, 100))
        self.assertEqual(pak.unpack("ECON", "HHBB", 36), (14, 1, 1, 6))
        self.assertEqual(pak.unpack("ECON", "10H", 42), tuple(range(8)) + (65535, 65535))
        self.assertEqual(pak.data("ECON")[62:64], b"\0\0")
        self.assertEqual(pak.unpack("LIST", "2H"), (0, 0))

        ev = pak.data("EVNT")
        self.assertEqual(struct.unpack_from("<6B", ev, 2), (0, 0, 11, 0, 0, 1))
        self.assertEqual(struct.unpack_from("<i", ev, 8), (0,))
        self.assertEqual(struct.unpack_from("<f", ev, 12), (1.0,))
        self.assertAlmostEqual(struct.unpack_from("<f", ev, 16)[0], 0.1)
        self.assertEqual(struct.unpack_from("<4H4B", ev, 20), (0, 1, 0, 0, 3, 2, 1, 0))
        self.assertEqual(names[struct.unpack_from("<H", ev, 32)[0]], "npc_inspector")
        self.assertEqual(strings[struct.unpack_from("<H", ev, 34)[0]], "Проверка лицензий!")
        text, req_count, effect_count, req_first, effect_first = pak.unpack("CHOI", "HBBHH")
        self.assertEqual((strings[text], req_count, effect_count, req_first, effect_first), ("Хорошо", 1, 4, 4, 0))
        self.assertEqual(pak.unpack("COND", "BBHf"), (12, 5, 0, 20.0))
        self.assertEqual(pak.unpack("COND", "BBHf", 3 * 8), (5, 5, 65535, 609.0))
        self.assertEqual(pak.unpack("CHNC", "BBH", 0), (7, 0, 65535))
        self.assertEqual(pak.unpack("CHNC", "BBH", 8), (9, 0, 0))
        self.assertEqual(pak.sections["EFFE"][2], 5)
        self.assertEqual(pak.unpack("EFFE", "4B4Hif"), (0, 1, 255, 0, 8, 65535, 0, 0, 0, -5000.0))
        self.assertEqual(pak.unpack("EFFE", "4B4H", 20), (11, 0, 255, 1, 65535, 65535, 4, 0))
        self.assertEqual(pak.unpack("EFFE", "4B4Hif", 40), (2, 0, 255, 0, 0, 65535, 0, 0, 1, 0.0))
        self.assertEqual(pak.unpack("EFFE", "f", 3 * 20 + 16), (-2.0,))
        self.assertEqual(pak.unpack("EFFE", "4B4Hif", 80), (6, 0, 255, 0, 0, 65535, 0, 0, 7, 0.0))
        keys = list(struct.iter_unpack("<IHH", pak.data("STRK")))
        expected = sorted((key_hash(key), i, 0) for i, key in enumerate(source_strings))
        self.assertEqual(keys, expected)
        self.assertEqual(len(keys), len(strings) - 1)
        for tag in TAGS:
            self.assertIn(f"{tag}: {pak.sections[tag][2]}", process.stdout)
        self.assertIn(f"file_size={len(pak.blob)} bytes", process.stdout)
        self.assertIn(f"STRS_size={len(pak.data('STRS'))} bytes", process.stdout)

    def test_record_sizes_match_every_header_static_assert(self):
        header = (ROOT / "src" / "core" / "data_types.h").read_text(encoding="utf-8")
        expected = {name: int(size) for name, size in re.findall(r"static_assert\s*\(\s*sizeof\s*\(\s*(\w+)\s*\)\s*==\s*(\d+)", header)}
        tree = ast.parse(SCRIPT.read_text(encoding="utf-8"))
        formats = next(ast.literal_eval(node.value) for node in tree.body
                       if isinstance(node, ast.Assign) and any(isinstance(target, ast.Name) and target.id == "RECORD_FORMATS"
                                                              for target in node.targets))
        actual = {name: struct.calcsize(fmt) for name, fmt in formats.items()}
        self.assertTrue(expected)
        self.assertEqual(actual, expected)

    def test_deterministic(self):
        self.run_builder()
        first = self.output.read_bytes()
        self.run_builder()
        self.assertEqual(self.output.read_bytes(), first)

    def test_invalid_id(self):
        self.broken_case("invalid_id")

    def test_duplicate_id(self):
        self.broken_case("duplicate")

    def test_broken_reference(self):
        self.broken_case("broken_reference")

    def test_missing_string(self):
        self.broken_case("missing_string")

    def test_range(self):
        self.broken_case("range")

    def test_unknown_field(self):
        self.broken_case("unknown_field")

    def test_unknown_enum(self):
        self.broken_case("enum")

    def test_limit(self):
        self.broken_case("limit")

    def test_errors_aggregate_and_do_not_overwrite_existing_output(self):
        games = self.read("games.json")
        games[0]["req"]["cpu"] = -1
        games[0]["genre"] = "wrong"
        games[0]["title"] = "missing"
        games[0]["extra"] = True
        games[0]["req"]["gpu"] = 65536
        self.write("games.json", games)
        self.write("minigames.json", [{"id":"snake","name":"missing","score_scale":0,"where":["wrong"]}])
        self.output.write_bytes(b"existing pak must survive")
        result = self.run_builder(False)
        for field in ("req.cpu", "req.gpu", "genre", "title", "extra"):
            self.assertIn(f"{self.source / 'games.json'}: test_game.{field}:", result.stderr)
        for field in ("name", "score_scale", "where[0]"):
            self.assertIn(f"{self.source / 'minigames.json'}: snake.{field}:", result.stderr)
        self.assertEqual(self.output.read_bytes(), b"existing pak must survive")
        self.assertEqual(sorted(path.name for path in self.root.iterdir()), ["data.pak", "source"])

    def test_hash_collision_reports_both_keys(self):
        self.assertEqual(key_hash("costarring"), key_hash("liquid"))
        strings = self.read("strings_ru.json")
        strings.update(costarring="Один", liquid="Другой")
        self.write("strings_ru.json", strings)
        result = self.reject("strings_ru.json", "liquid", "key", "коллизия")
        self.assertIn("costarring", result.stderr)
        self.assertIn("liquid", result.stderr)

    def test_hash_collision_is_not_hidden_by_other_errors(self):
        games = self.read("games.json")
        games[0]["id"] = "Bad-ID"
        self.write("games.json", games)
        strings = self.read("strings_ru.json")
        strings.update(costarring="Один", liquid="Другой")
        self.write("strings_ru.json", strings)
        result = self.reject("strings_ru.json", "liquid", "key", "коллизия")
        self.assertIn("Bad-ID.id:", result.stderr)

    def test_unknown_nested_effect_field(self):
        event = self.read("events/01_check.json")
        event["choices"][0]["effects"][1]["then"][0]["houre"] = 2
        self.write("events/01_check.json", event)
        self.reject("events/01_check.json", "license_check", "choices[0].effects[1].then[0].houre", "неизвестное поле")

    def test_missing_required_fields(self):
        economy = self.read("economy.json")
        del economy["tax_rate"]
        self.write("economy.json", economy)
        self.reject("economy.json", "economy", "tax_rate", "обязательное поле")

    def test_upgrade_self_reference_and_cycle(self):
        path = self.source / "upgrades.csv"
        path.write_text(path.read_text(encoding="utf-8").replace("name,1,,,,", "name,1,,hall_small,,"), encoding="utf-8")
        self.reject("upgrades.csv", "hall_small", "requires", "цикл")
        header = path.read_text(encoding="utf-8").splitlines()[0]
        path.write_text(header + "\nhall_small,hall,upg.hall_small.name,1,,hall_other,,0,12000,,,,6\n"
                        "hall_other,hall,upg.hall_small.name,2,,hall_small,,0,12000,,,,8\n", encoding="utf-8")
        self.reject("upgrades.csv", "hall_other", "requires", "цикл")

    def test_csv_header_and_component_specific_fields(self):
        path = self.source / "hardware.csv"
        content = path.read_text(encoding="utf-8").replace("score,", "scroe,", 1)
        path.write_text(content, encoding="utf-8")
        self.reject("hardware.csv", "<header>", "scroe", "неизвестное поле")
        shutil.copyfile(FIXTURES / "valid" / "hardware.csv", path)
        path.write_text(path.read_text(encoding="utf-8").replace(",3000,40,", ",3000,,"), encoding="utf-8")
        self.reject("hardware.csv", "cpu_basic", "score", "> 0")

    def test_dates_reject_impossible_day(self):
        calendar = self.read("calendar.json")
        calendar["holidays"] = ["02-30"]
        self.write("calendar.json", calendar)
        self.reject("calendar.json", "calendar", "holidays[0]", "несуществующая дата")

    def test_all_event_files_share_id_namespace(self):
        self.write("events/02_duplicate.json", [self.read("events/01_check.json")])
        self.reject("events/02_duplicate.json", "license_check", "id", "дубликат")

    def test_event_filename_and_array_order_flags_sorted_and_warning(self):
        base = self.read("events/01_check.json")
        base["id"] = "last"
        base["conditions"] = [{"var":"flag:zflag", "op":"==", "val":0}, {"var":"flag:aflag", "op":"==", "val":0}]
        self.write("events/z.json", base)
        base = copy.deepcopy(base)
        base["id"] = "first"
        other = copy.deepcopy(base)
        other["id"] = "second"
        self.write("events/00_first.json", [base, other])
        result = self.run_builder()
        pak = Pak(self.output.read_bytes())
        names = pak.strings("NAME")
        self.assertEqual([names[pak.unpack("EVNT", "H", 36 * i)[0]] for i in range(4)], ["first", "second", "license_check", "last"])
        self.assertEqual([names[i] for (i,) in struct.iter_unpack("<H", pak.data("FLAG"))], ["aflag", "checked", "zflag"])
        self.assertIn("флаг 'aflag' проверяется, но нигде не ставится", result.stderr)

    def test_table_and_list_limits(self):
        games = self.read("games.json")
        template = games[0]
        games = [{**template, "id":f"game_{i}"} for i in range(257)]
        self.write("games.json", games)
        self.reject("games.json", "<table>", "records", "лимит")
        self.write("games.json", [template])
        event = self.read("events/01_check.json")
        event["conditions"] = [{"var":"day", "op":">", "val":0}] * 256
        self.write("events/01_check.json", event)
        self.reject("events/01_check.json", "license_check", "conditions", "лимит")

    def test_global_effect_table_limit(self):
        event = self.read("events/01_check.json")
        event["choices"] = [{"text":"ev.license_check.ok", "effects":[{"type":"rep", "val":1}] * 255}] * 130
        self.write("events/01_check.json", event)
        event["id"] = "other_event"
        self.write("events/02_other.json", event)
        result = self.run_builder(False)
        self.assertIn(str(self.source / "events/02_other.json") + ": other_event.choices[", result.stderr)
        self.assertIn("EFFE: превышен общий лимит 65535", result.stderr)
        self.assertFalse(self.output.exists())

    def test_string_limit_counts_nicks(self):
        strings = self.read("strings_ru.json")
        strings.update({f"ui.filler_{i}":"Текст" for i in range(65535 - len(strings))})
        self.write("strings_ru.json", strings)
        self.reject("regulars.json", "dimon", "nick", "STRS: превышен лимит")

    def test_string_table_overflow_is_diagnostic(self):
        strings = self.read("strings_ru.json")
        strings.update({f"ui.filler_{i}":"Текст" for i in range(65537)})
        self.write("strings_ru.json", strings)
        self.reject("strings_ru.json", "strings", "STRS", "лимит")

    def test_parameterized_variables_and_expression(self):
        event = self.read("events/01_check.json")
        event["conditions"] = [
            {"var":"relation:dimon", "op":">", "val":-2},
            {"var":"game_installed:test_game", "op":"==", "val":6},
        ]
        event["chance"]["per"] = {"visits_archetype:school":0.5}
        event["choices"][0]["effects"] = [{"type":"money", "expr":"-2.5 * relation:dimon"}]
        self.write("events/01_check.json", event)
        self.run_builder()
        pak = Pak(self.output.read_bytes())
        self.assertEqual(pak.unpack("COND", "BBHf", 8), (10, 4, 0, -2.0))
        self.assertEqual(pak.unpack("COND", "BBHf", 16), (11, 0, 0, 6.0))
        self.assertEqual(pak.unpack("CHNC", "BBHf"), (12, 0, 0, 0.5))
        self.assertEqual(pak.unpack("EFFE", "4B4Hif"), (0,1,255,0,10,0,0,0,0,-2.5))

    def test_parameterized_variable_reference_rejected(self):
        event = self.read("events/01_check.json")
        event["conditions"] = [{"var":"relation:missing", "op":">", "val":0}]
        self.write("events/01_check.json", event)
        self.reject("events/01_check.json", "license_check", "conditions[0].var", "битая ссылка")

    def test_float_and_integer_storage_bounds_and_types(self):
        for value in (True, "40", 1.5, 65536, -1):
            with self.subTest(value=value):
                games = self.read("games.json")
                games[0]["req"]["cpu"] = value
                self.write("games.json", games)
                self.reject("games.json", "test_game", "req.cpu", "диапазоне")
        games = self.read("games.json")
        games[0]["req"]["cpu"] = 40
        games[0]["pop"]["peak"] = float("inf")
        self.write("games.json", games)
        self.reject("games.json", "test_game", "pop.peak", "float32")

    def test_economy_start_pc_type_and_hall_capacity(self):
        economy = self.read("economy.json")
        economy["start_pc"]["cpu"] = "gpu_basic"
        economy["start_pcs"] = 7
        self.write("economy.json", economy)
        result = self.reject("economy.json", "economy", "start_pc.cpu", "тип железа")
        self.assertIn("economy.start_pcs:", result.stderr)

    def test_ambiguous_item_reference(self):
        path = self.source / "goods.csv"
        path.write_text(path.read_text(encoding="utf-8").replace("soda,", "cpu_basic,"), encoding="utf-8")
        event = self.read("events/01_check.json")
        event["choices"][0]["effects"] = [{"type":"item", "id":"cpu_basic", "count":1}]
        self.write("events/01_check.json", event)
        self.reject("events/01_check.json", "license_check", "choices[0].effects[0].id", "неоднозначная")

    def test_chance_nesting_four_allowed_five_rejected(self):
        event = self.read("events/01_check.json")
        effects = [{"type":"rep", "val":1}]
        for _ in range(4):
            effects = [{"type":"chance", "p":0.5, "then":effects}]
        event["choices"][0]["effects"] = effects
        self.write("events/01_check.json", event)
        self.run_builder()
        self.output.unlink()
        event["choices"][0]["effects"] = [{"type":"chance", "p":0.5, "then":effects}]
        self.write("events/01_check.json", event)
        self.reject("events/01_check.json", "license_check", "choices[0].effects[0].then[0].then[0].then[0].then[0].then", "глубины")

    def test_every_effect_layout_and_reference(self):
        event = self.read("events/01_check.json")
        event["choices"][0]["effects"] = [
            {"type":"money", "val":-123},
            {"type":"relation", "regular":"dimon", "val":-4},
            {"type":"modifier", "target":"arrivals:school", "mult":1.5, "days":2},
            {"type":"modifier", "target":"pop:test_game", "mult":2, "days":3},
            {"type":"spawn_visitor", "archetype":"school"},
            {"type":"spawn_visitor", "regular":"dimon"},
            {"type":"uninstall", "filter":"game:test_game"},
            {"type":"damage_pc", "pc":"random", "component":"random"},
            {"type":"damage_pc", "pc":15, "component":"gpu"},
            {"type":"item", "id":"soda", "count":-2},
            {"type":"item", "id":"cpu_basic", "count":1},
            {"type":"unlock", "id":"hall_small"},
            {"type":"unlock", "id":"test_game"},
            {"type":"start_minigame", "id":"snake"},
            {"type":"start_minigame", "id":"reboot"},
            {"type":"schedule_event", "id":"license_check", "in_days":1, "hour":23},
        ]
        self.write("events/01_check.json", event)
        self.run_builder()
        pak = Pak(self.output.read_bytes())
        decoded = list(struct.iter_unpack("<4B4Hif", pak.data("EFFE")))
        # type, sub, hour, count, ref, arg, first, pad, ival, fval
        self.assertEqual(decoded, [
            (0,0,255,0,65535,65535,0,0,-123,0), (3,0,255,0,0,65535,0,0,-4,0),
            (4,0,255,0,65535,0,0,0,2,1.5), (4,2,255,0,65535,0,0,0,3,2),
            (5,0,255,0,0,65535,0,0,0,0), (5,1,255,0,0,65535,0,0,0,0),
            (7,2,255,0,0,65535,0,0,0,0), (8,0,255,0,65535,65535,0,0,0,0),
            (8,0,255,0,15,1,0,0,0,0), (9,1,255,0,0,65535,0,0,-2,0),
            (9,0,255,0,0,65535,0,0,1,0), (10,0,255,0,0,65535,0,0,0,0),
            (10,1,255,0,0,65535,0,0,0,0), (12,0,255,0,0,65535,0,0,0,0),
            (12,1,255,0,0,65535,0,0,0,0), (6,0,23,0,0,65535,0,0,1,0),
        ])

    def test_other_trigger_variants(self):
        event = self.read("events/01_check.json")
        for trigger, expected in [
            ({"type":"date","date":"2001-09-01","hour":9}, (1, 9, 0, 609)),
            ({"type":"on_action","action":"pc_bought"}, (2, 255, 1, 0)),
            ({"type":"on_state"}, (3, 255, 0, 0)),
            ({"type":"scheduled"}, (4, 255, 0, 0)),
        ]:
            with self.subTest(trigger=trigger):
                event["trigger"] = trigger
                self.write("events/01_check.json", event)
                self.run_builder()
                pak = Pak(self.output.read_bytes())
                self.assertEqual((*pak.unpack("EVNT", "BBB", 3), pak.unpack("EVNT", "i", 8)[0]), expected)

    def test_invalid_money_expression(self):
        event = self.read("events/01_check.json")
        event["choices"][0]["effects"][0]["expr"] = "money + 1"
        self.write("events/01_check.json", event)
        self.reject("events/01_check.json", "license_check", "choices[0].effects[0].expr", "выражение")

    def test_duplicate_json_keys(self):
        path = self.source / "games.json"
        path.write_text(path.read_text(encoding="utf-8").replace('"cpu": 40', '"cpu": 40, "cpu": 42'), encoding="utf-8")
        self.reject("games.json", "test_game", "req.cpu", "дубликат")

    def test_malformed_files_and_wrong_shapes(self):
        (self.source / "games.json").write_text("[ broken", encoding="utf-8")
        self.write("archetypes.json", [None])
        result = self.reject("games.json", "<file>", "json", "JSON")
        self.assertIn(f"{self.source / 'archetypes.json'}: <row:0>.record:", result.stderr)

    def test_oversize_is_warning_even_without_report(self):
        strings = self.read("strings_ru.json")
        strings["ui.large"] = "я" * (512 * 1024)
        self.write("strings_ru.json", strings)
        result = self.run_builder()
        self.assertGreater(self.output.stat().st_size, 1024 * 1024)
        self.assertIn("Предупреждение:", result.stderr)
        self.assertIn("1 МиБ", result.stderr)

    def test_optional_tables_may_be_absent(self):
        for filename in ("games.json", "archetypes.json", "regulars.json", "goods.csv", "minigames.json"):
            (self.source / filename).unlink()
        shutil.rmtree(self.source / "events")
        self.run_builder()
        pak = Pak(self.output.read_bytes())
        for tag in ("GAME", "ARCH", "REGU", "GOOD", "MINI", "EVNT", "COND", "CHOI", "EFFE", "CHNC", "FLAG"):
            self.assertEqual(pak.sections[tag][2], 0)


if __name__ == "__main__":
    unittest.main()
