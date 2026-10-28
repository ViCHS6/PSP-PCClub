"""Сравнение CSV balance-runner: автономный HTML с SVG, только стандартная библиотека."""

import argparse
import csv
from dataclasses import dataclass
from datetime import date
from html import escape
import math
from pathlib import Path
import sys
import xml.etree.ElementTree as ET


METRICS = (
    "money", "profit", "visitors_arrived", "visitors_served",
    "visitors_lost", "load_pct", "reputation",
)
CHARTS = (
    ("Деньги", "руб.", (("money", "Касса"),)),
    ("Прибыль за день", "руб.", (("profit", "Прибыль"),)),
    ("Посетители", "чел.", (
        ("visitors_arrived", "Пришли"), ("visitors_served", "Обслужены"),
        ("visitors_lost", "Потеряны"),
    )),
    ("Загрузка", "%", (("load_pct", "Загрузка"),)),
    ("Репутация", "пункты", (("reputation", "Репутация"),)),
)
COLORS = ("#1d4ed8", "#b91c1c", "#047857", "#7e22ce", "#c2410c", "#0e7490", "#a21caf", "#4d7c0f")
DASHES = ("", "8 4", "2 4")
SVG_NS = "http://www.w3.org/2000/svg"
ET.register_namespace("", SVG_NS)


def line_color(index):
    if index < len(COLORS):
        return COLORS[index]
    return f"hsl({(index * 137.508) % 360:.1f},65%,35%)"


@dataclass
class Run:
    name: str
    rows: list[dict[str, float]]


def read_csv(path: Path) -> Run:
    """Проверяет форму CSV, конечность чисел и строго возрастающие номера дней."""
    path = Path(path)
    rows = []
    try:
        with path.open(encoding="utf-8-sig", newline="") as stream:
            reader = csv.DictReader(stream, strict=True)
            fields = reader.fieldnames
            if not fields or len(set(fields)) != len(fields):
                raise ValueError("нет заголовка или имена колонок повторяются")
            missing = set(("day",) + METRICS) - set(fields)
            if missing:
                raise ValueError("нет колонок: " + ", ".join(sorted(missing)))
            previous = 0
            for raw in reader:
                if None in raw or any(value is None or not value.strip() for value in raw.values()):
                    raise ValueError(f"строка {reader.line_num}: неполная или лишняя колонка")
                row = {}
                for key in ("day",) + METRICS:
                    try:
                        value = float(raw[key])
                    except ValueError as exc:
                        raise ValueError(f"строка {reader.line_num}: {key} не число") from exc
                    if not math.isfinite(value):
                        raise ValueError(f"строка {reader.line_num}: {key} не конечное число")
                    row[key] = value
                if not row["day"].is_integer() or row["day"] <= previous:
                    raise ValueError(f"строка {reader.line_num}: день должен быть целым и возрастать от 1")
                for key in ("visitors_arrived", "visitors_served", "visitors_lost"):
                    if row[key] < 0 or not row[key].is_integer():
                        raise ValueError(f"строка {reader.line_num}: {key} должен быть целым и неотрицательным")
                for key in ("load_pct", "reputation"):
                    if not 0 <= row[key] <= 100:
                        raise ValueError(f"строка {reader.line_num}: {key} вне 0..100")
                if "date" in raw:
                    try:
                        date.fromisoformat(raw["date"])
                    except ValueError as exc:
                        raise ValueError(f"строка {reader.line_num}: неверная дата") from exc
                previous = row["day"]
                rows.append(row)
    except (OSError, UnicodeError, csv.Error, ValueError) as exc:
        raise ValueError(f"{path}: {exc}") from exc
    if not rows:
        raise ValueError(f"{path}: CSV не содержит дней")
    return Run(path.name, rows)


def element(parent, tag, text=None, **attributes):
    node = ET.SubElement(parent, f"{{{SVG_NS}}}{tag}", {key: str(value) for key, value in attributes.items()})
    if text is not None:
        node.text = text
    return node


def chart_svg(runs: list[Run], title: str, unit: str, metrics: tuple) -> str:
    width, height = 960, 320
    left, top, right, bottom = 78, 28, 24, 48
    plot_width, plot_height = width - left - right, height - top - bottom
    x_min = min(run.rows[0]["day"] for run in runs)
    x_max = max(run.rows[-1]["day"] for run in runs)
    if x_min == x_max:
        x_min, x_max = x_min - 0.5, x_max + 0.5
    values = [row[key] for run in runs for row in run.rows for key, _ in metrics]
    y_min, y_max = min(0, min(values)), max(0, max(values))
    if metrics[0][0] in ("load_pct", "reputation"):
        y_min, y_max = 0, 100
    elif y_min == y_max:
        y_min, y_max = y_min - 1, y_max + 1
    else:
        margin = (y_max - y_min) * 0.08
        y_min, y_max = y_min - margin, y_max + margin

    def x(value):
        return left + (value - x_min) / (x_max - x_min) * plot_width

    def y(value):
        return top + (y_max - value) / (y_max - y_min) * plot_height

    svg = ET.Element(f"{{{SVG_NS}}}svg", {
        "viewBox": f"0 0 {width} {height}", "role": "img",
        "aria-label": title, "style": "width:100%;height:auto",
    })
    element(svg, "title", title)
    element(svg, "desc", f"По горизонтали — игровой день; по вертикали — {unit}. Линии указаны в легенде.")
    for tick in range(5):
        value = y_min + (y_max - y_min) * tick / 4
        position = y(value)
        element(svg, "line", x1=left, y1=f"{position:.2f}", x2=width - right,
                y2=f"{position:.2f}", stroke="#e2e8f0")
        element(svg, "text", f"{value:,.2f}".rstrip("0").rstrip(".").replace(",", " "),
                x=left - 10, y=f"{position + 4:.2f}", **{"text-anchor": "end"})
    ticks = sorted(set(round(x_min + (x_max - x_min) * tick / 4) for tick in range(5)))
    for value in ticks:
        if value < x_min or value > x_max:
            continue
        element(svg, "text", str(value), x=f"{x(value):.2f}", y=height - 25,
                **{"text-anchor": "middle"})
    element(svg, "text", "День", x=width - right, y=height - 5, **{"text-anchor": "end"})
    element(svg, "text", unit, x=left, y=16)
    if y_min < 0 < y_max:
        element(svg, "line", x1=left, y1=f"{y(0):.2f}", x2=width - right,
                y2=f"{y(0):.2f}", stroke="#94a3b8")
    for index, run in enumerate(runs):
        for metric_index, (key, label) in enumerate(metrics):
            color = line_color(index)
            dash = DASHES[metric_index]
            group = element(svg, "g", **{"data-run": run.name, "data-metric": key})
            points = " ".join(f'{x(row["day"]):.2f},{y(row[key]):.2f}' for row in run.rows)
            element(group, "polyline", points=points, fill="none", stroke=color,
                    **{"stroke-width": "2.5", "stroke-dasharray": dash})
            for row in run.rows:
                point = element(group, "circle", cx=f'{x(row["day"]):.2f}', cy=f'{y(row[key]):.2f}',
                                r=2.5, fill=color)
                element(point, "title", f'{run.name} · {label} · день {int(row["day"])}: {row[key]:g}')
    return ET.tostring(svg, encoding="unicode")


def render_html(runs: list[Run]) -> str:
    if not runs or any(not run.rows for run in runs):
        raise ValueError("нужен хотя бы один непустой прогон")
    sections = []
    for title, unit, metrics in CHARTS:
        legend = []
        for index, run in enumerate(runs):
            for metric_index, (_, label) in enumerate(metrics):
                dash = DASHES[metric_index]
                sample = (
                    f'<svg xmlns="{SVG_NS}" viewBox="0 0 32 10" width="32" height="10" aria-hidden="true">'
                    f'<line x1="0" y1="5" x2="32" y2="5" stroke="{line_color(index)}" '
                    f'stroke-width="3" stroke-dasharray="{dash}" /></svg>'
                )
                legend.append(f"<li>{sample} {escape(run.name)} · {escape(label)}</li>")
        sections.append(
            f"<section><h2>{escape(title)}</h2>{chart_svg(runs, title, unit, metrics)}"
            f'<ul class="legend">{"".join(legend)}</ul></section>'
        )
    summaries = []
    for run in runs:
        last = run.rows[-1]
        summaries.append(
            f"<tr><th>{escape(run.name)}</th><td>{len(run.rows)}</td>"
            f'<td>{last["money"]:g}</td><td>{sum(row["profit"] for row in run.rows):g}</td></tr>'
        )
    return """<!doctype html>
<html lang="ru"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Баланс компьютерного клуба</title>
<style>
* { box-sizing: border-box; }
body { margin: 0 auto; padding: 32px 20px; max-width: 1200px; background: #f1f5f9;
       color: #0f172a; font: 15px/1.5 system-ui, sans-serif; }
h1 { margin: 0 0 8px; font-size: 30px; } h2 { font-size: 21px; margin: 0 0 12px; }
p { color: #475569; max-width: 90ch; }
section { margin: 24px 0; padding: 24px; background: white; border: 1px solid #cbd5e1; border-radius: 12px; }
svg text { font: 13px system-ui, sans-serif; fill: #475569; }
circle:hover { r: 5; } .legend { display: flex; flex-wrap: wrap; gap: 8px 24px; list-style: none; padding: 0; }
.legend li { display: inline-flex; align-items: center; gap: 7px; overflow-wrap: anywhere; }
table { border-collapse: collapse; width: 100%; background: white; }
td, th { text-align: right; padding: 10px; border-bottom: 1px solid #cbd5e1; }
th:first-child { text-align: left; overflow-wrap: anywhere; }
.summary { overflow-x: auto; } @media(max-width:600px) { section { padding: 10px; } body { padding: 16px 8px; } }
</style></head><body>
<h1>Баланс компьютерного клуба</h1>
<p>Итоги по игровым дням. Легенда показывает имя CSV и показатель; наведите курсор на точку для точного значения.
Деньги — касса при закрытии, прибыль — выручка минус все расходы за день, включая покупки.
Потеряны — ушедшие без обслуживания. Загрузка — доля минут нагрузки от ёмкости зала.
Прогон может закончиться досрочно при банкротстве.</p>
<div class="summary"><table><thead><tr><th>CSV</th><th>Дней</th><th>Касса, руб.</th>
<th>Сумма прибыли, руб.</th></tr></thead><tbody>""" + "".join(summaries) + (
        "</tbody></table></div>" + "".join(sections) + "</body></html>\n"
    )


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv_files", nargs="+", type=Path)
    parser.add_argument("-o", "--output", required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        if any(path.resolve() == args.output.resolve() for path in args.csv_files):
            raise ValueError("HTML не должен перезаписывать исходный CSV")
        runs = [read_csv(path) for path in args.csv_files]
        args.output.write_text(render_html(runs), encoding="utf-8")
    except (OSError, ValueError) as exc:
        print(f"Ошибка: {exc}", file=sys.stderr)
        return 1
    print(f"HTML: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
