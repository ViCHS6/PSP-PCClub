"""11 жёстких частей (1.75 м, 156 треугольников) и walk/sit_down/type для T041."""

import argparse
import json
import math
from pathlib import Path
import sys

from convert_anim import convert as convert_anim
from convert_model import convert as convert_model


# Имя, родитель, сустав в осях движка, размеры коробки, центр коробки от сустава.
PARTS = [
    ("pelvis", None, (0, 1, 0), (.30, .18, .20), (0, 0, 0)),
    ("torso", "pelvis", (0, .10, 0), (.40, .45, .22), (0, .225, 0)),
    ("head", "torso", (0, .45, 0), (.22, .20, .22), (0, .10, 0)),
    ("upper_arm_l", "torso", (-.24, .35, 0), (.12, .28, .13), (0, -.14, 0)),
    ("forearm_l", "upper_arm_l", (0, -.28, 0), (.10, .29, .11), (0, -.145, 0)),
    ("upper_arm_r", "torso", (.24, .35, 0), (.12, .28, .13), (0, -.14, 0)),
    ("forearm_r", "upper_arm_r", (0, -.28, 0), (.10, .29, .11), (0, -.145, 0)),
    ("thigh_l", "pelvis", (-.10, -.10, 0), (.14, .43, .16), (0, -.215, 0)),
    ("shin_l", "thigh_l", (0, -.43, 0), (.12, .43, .13), (0, -.215, 0)),
    ("thigh_r", "pelvis", (.10, -.10, 0), (.14, .43, .16), (0, -.215, 0)),
    ("shin_r", "thigh_r", (0, -.43, 0), (.12, .43, .13), (0, -.215, 0)),
]


def blender_xyz(v):
    x, y, z = v
    return [x, -z, y]


def quat_x(degrees):
    # X одинаков в обеих системах; JSON хранит xyzw в Blender.
    angle = math.radians(degrees) / 2
    return [math.sin(angle), 0, 0, math.cos(angle)]


def box_lines(size, center, color, base=0):
    lines = []
    for x, y, z in [(-1,-1,-1), (1,-1,-1), (1,1,-1), (-1,1,-1),
                    (-1,-1,1), (1,-1,1), (1,1,1), (-1,1,1)]:
        p = [center[i] + s * size[i] / 2 for i, s in enumerate((x, y, z))]
        lines.append("v " + " ".join(f"{v:.8g}" for v in (*p, *color)))
    for face in [(1,4,3,2), (5,6,7,8), (1,5,8,4), (2,3,7,6), (4,8,7,3), (1,2,6,5)]:
        lines.append("f " + " ".join(str(i + base) for i in face))
    return lines


def generate(output):
    output.mkdir(parents=True, exist_ok=True)
    parts = [{"name": n, "parent": p, "offset": blender_xyz(o)} for n, p, o, _, _ in PARTS]
    for name, _, _, size, center in PARTS:
        skin = name == "head" or name.startswith("forearm")
        color = (.68, .53, .40) if skin else ((.29, .36, .29) if name.startswith(("torso", "upper"))
                                             else (.18, .22, .32))
        lines = [f"o chr_placeholder_{name}"] + box_lines(size, center, color)
        if name.startswith("shin"):
            lines += box_lines((.14, .04, .25), (0, -.45, .06), (.12, .12, .12), 8)
        obj = output / f"chr_placeholder_{name}.obj"
        obj.write_text("\n".join(lines) + "\n", encoding="utf-8")
        convert_model(obj, obj.with_suffix(".msh"))
    clips = []
    for name, count, loop in [("walk", 30, True), ("sit_down", 16, False), ("type", 30, True)]:
        frames = []
        for i in range(count):
            phase = 2 * math.pi * i / count
            rotation = {p[0]: quat_x(0) for p in PARTS}
            root = (0, 0, 0)
            if name == "walk":
                swing = math.sin(phase)
                rotation.update(thigh_l=quat_x(28*swing), thigh_r=quat_x(-28*swing),
                                shin_l=quat_x(max(0, -swing)*30), shin_r=quat_x(max(0, swing)*30),
                                upper_arm_l=quat_x(-24*swing), upper_arm_r=quat_x(24*swing),
                                forearm_l=quat_x(-12), forearm_r=quat_x(-12))
                root = (0, .02 * (1 - math.cos(2*phase)), .08 * math.sin(phase))
            else:
                t = (i / (count - 1)) if name == "sit_down" else 1
                t = t*t*(3-2*t)
                tap = 5 * math.sin(phase*3) if name == "type" else 0
                rotation.update(thigh_l=quat_x(-90*t), thigh_r=quat_x(-90*t),
                                shin_l=quat_x(90*t), shin_r=quat_x(90*t),
                                upper_arm_l=quat_x(-35*t), upper_arm_r=quat_x(-35*t),
                                forearm_l=quat_x(-55*t+tap), forearm_r=quat_x(-55*t-tap))
                root = (0, -.44*t, -.20*t)
            frames.append({"root": blender_xyz(root), "rotations": rotation})
        clips.append({"name": name, "fps": 15, "loop": loop, "frames": frames})
    # Один файл набора для демо; отдельные JSON удобны A009/A010 и ручной проверке.
    for filename, subset in [("chr_placeholder", clips)] + [("chr_placeholder_" + c["name"], [c]) for c in clips]:
        source = output / f"{filename}.anim.json"
        source.write_text(json.dumps({"version": 1, "coordinate_system": "blender_z_up",
                                      "parts": parts, "clips": subset}, indent=2) + "\n", encoding="utf-8")
        convert_anim(source, output / f"{filename}.anm")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-o", "--output", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        generate(args.output)
    except (OSError, ValueError) as error:
        print(f"error: {args.output}: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
