"""Проверки входных CSV и автономного отчёта balance-runner."""

import csv
from html.parser import HTMLParser
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import plot_balance as plot


class HtmlCheck(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.external = []
        self.stack = []

    def handle_starttag(self, tag, attrs):
        if tag in ("script", "link", "iframe", "img"):
            self.external.append(tag)
        if tag not in ("meta", "line", "circle", "polyline"):
            self.stack.append(tag)

    def handle_endtag(self, tag):
        if tag in ("line", "circle", "polyline"):
            return
        if not self.stack or self.stack.pop() != tag:
            raise ValueError(f"неверная вложенность HTML: {tag}")


class PlotBalanceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.path = self.root / "normal.csv"
        self.fields = ["day", "date", *plot.METRICS]
        self.rows = [
            [1, "2001-09-01", 20000, -20, 10, 8, 2, 35.5, 30],
            [2, "2001-09-02", 19990, -10, 12, 9, 3, 40, 31],
        ]
        self.write()

    def write(self):
        with self.path.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.writer(stream)
            writer.writerow(self.fields)
            writer.writerows(self.rows)

    def test_parse_csv(self):
        run = plot.read_csv(self.path)
        self.assertEqual(run.name, "normal.csv")
        self.assertEqual(len(run.rows), 2)
        self.assertEqual(run.rows[0]["load_pct"], 35.5)
        self.assertEqual(run.rows[1]["profit"], -10)

    def test_bom_and_extra_runner_columns(self):
        self.fields += ["expenses", "satisfaction_avg", "debt"]
        for row in self.rows:
            row += [20, 70.5, 0]
        self.write()
        self.path.write_bytes(b"\xef\xbb\xbf" + self.path.read_bytes())
        self.assertEqual(len(plot.read_csv(self.path).rows), 2)

    def test_html_and_svg_multiple_runs(self):
        run = plot.read_csv(self.path)
        other = plot.Run('passive<&".csv', [dict(row) for row in run.rows])
        html = plot.render_html([run, other])
        parser = HtmlCheck()
        parser.feed(html)
        parser.close()
        self.assertEqual(parser.stack, [])
        self.assertEqual(parser.external, [])
        self.assertIn("passive&lt;&amp;&quot;.csv", html)
        charts = []
        for raw in re.findall(r"<svg\b.*?</svg>", html, flags=re.S):
            svg = ET.fromstring(raw)
            self.assertEqual(svg.tag, f"{{{plot.SVG_NS}}}svg")
            if svg.get("role") == "img":
                charts.append(svg)
                for point in svg.iter(f"{{{plot.SVG_NS}}}polyline"):
                    self.assertNotIn("nan", point.get("points"))
                    self.assertNotIn("inf", point.get("points"))
        self.assertEqual(len(charts), 5)
        self.assertEqual(len(charts[2].findall(f".//{{{plot.SVG_NS}}}polyline")), 6)
        self.assertEqual(len(charts[0].findall(f".//{{{plot.SVG_NS}}}polyline")), 2)

    def test_single_day_and_constant_zero_values(self):
        run = plot.Run("zero.csv", [{"day": 1, **dict.fromkeys(plot.METRICS, 0)}])
        html = plot.render_html([run])
        for raw in re.findall(r"<svg\b.*?</svg>", html, flags=re.S):
            ET.fromstring(raw)
        self.assertNotIn("nan", html)
        self.assertNotIn("inf", html)

    def test_nine_runs_have_distinct_colors(self):
        run = plot.read_csv(self.path)
        runs = [plot.Run(f"run_{index}.csv", run.rows) for index in range(9)]
        svg = ET.fromstring(plot.chart_svg(runs, "Деньги", "руб.", (("money", "Касса"),)))
        lines = svg.findall(f".//{{{plot.SVG_NS}}}polyline")
        self.assertEqual(len({line.get("stroke") for line in lines}), 9)

    def test_missing_file_and_invalid_encoding(self):
        with self.assertRaises(ValueError):
            plot.read_csv(self.root / "missing.csv")
        self.path.write_bytes(b"\xff\xfe")
        with self.assertRaises(ValueError):
            plot.read_csv(self.path)

    def test_bad_csv(self):
        valid = self.path.read_text(encoding="utf-8")
        bad = [
            "", "day,money\n", ",".join(self.fields) + "\n",
            valid.replace("20000", "oops"), valid.replace("20000", "NaN"),
            valid.replace("20000", "inf"), valid.replace("20000", "1e309"),
            valid.replace("35.5", "-1"), valid.replace("35.5", "101"),
            valid.replace("2001-09-01", "2001-02-30"),
            valid.replace("2,2001-09-02", "1,2001-09-02"),
            valid.replace("1,2001-09-01", "0,2001-09-01"),
            valid.replace("1,2001-09-01", "1.5,2001-09-01"),
            valid.replace("10,8,2", "10,-8,2"),
            valid.replace("10,8,2", "10,8.5,2"),
            valid.replace("day,date", "day,day"),
            valid + "3,2001-09-03\n",
            valid.replace("35.5,30", "35.5,30,99"),
            valid + '"unterminated\n',
        ]
        for text in bad:
            with self.subTest(csv=text):
                self.path.write_text(text, encoding="utf-8")
                with self.assertRaises(ValueError):
                    plot.read_csv(self.path)

    def test_no_runs(self):
        with self.assertRaises(ValueError):
            plot.render_html([])

    def test_cli(self):
        output = self.root / "balance.html"
        result = subprocess.run(
            [sys.executable, "-X", "utf8", str(Path(plot.__file__)), str(self.path), "-o", str(output)],
            capture_output=True, text=True, encoding="utf-8",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("<!doctype html>", output.read_text(encoding="utf-8"))

    def test_cli_error_preserves_report(self):
        output = self.root / "balance.html"
        output.write_text("previous report", encoding="utf-8")
        self.path.write_text("", encoding="utf-8")
        result = subprocess.run(
            [sys.executable, "-X", "utf8", str(Path(plot.__file__)), str(self.path), "-o", str(output)],
            capture_output=True, text=True, encoding="utf-8",
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(str(self.path), result.stderr)
        self.assertEqual(output.read_text(encoding="utf-8"), "previous report")

    def test_cli_does_not_overwrite_csv(self):
        original = self.path.read_bytes()
        result = subprocess.run(
            [sys.executable, "-X", "utf8", str(Path(plot.__file__)), str(self.path), "-o", str(self.path)],
            capture_output=True, text=True, encoding="utf-8",
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.path.read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
