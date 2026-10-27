#!/usr/bin/env python3
"""Внутри Blender: независимое превью 256×256, камера спереди справа и сверху."""

import argparse
from pathlib import Path
import sys

import bpy
from mathutils import Vector

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from blender_export import asset_object, material_data


def preview_material(source, has_color):
    kd, texture = material_data(source) if source else ((1.0, 1.0, 1.0), None)
    material = bpy.data.materials.new("preview_material")
    material.use_nodes = True
    nodes = material.node_tree.nodes
    nodes.clear()
    output = nodes.new("ShaderNodeOutputMaterial")
    shader = nodes.new("ShaderNodeBsdfPrincipled")
    shader.inputs["Roughness"].default_value = 0.85
    # Kd в MTL — sRGB; Blender-шейдер принимает линейный цвет.
    linear = tuple(c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4 for c in kd)
    shader.inputs["Base Color"].default_value = (*linear, 1.0)
    links = material.node_tree.links
    links.new(shader.outputs["BSDF"], output.inputs["Surface"])
    color_output = None
    if texture:
        image_node = nodes.new("ShaderNodeTexImage")
        image_node.image = next(n.image for n in source.node_tree.nodes
                                if n.type == "TEX_IMAGE" and n.image
                                and Path(bpy.path.abspath(n.image.filepath, library=n.image.library)).name == texture)
        image_node.interpolation = "Closest"
        color_output = image_node.outputs["Color"]
        links.new(image_node.outputs["Alpha"], shader.inputs["Alpha"])
    if has_color:
        attribute = nodes.new("ShaderNodeVertexColor")
        attribute.layer_name = "Col"
        multiply = nodes.new("ShaderNodeMixRGB")
        multiply.blend_type = "MULTIPLY"
        multiply.inputs[0].default_value = 1.0
        multiply.inputs[1].default_value = (*linear, 1.0)
        if color_output:
            links.new(color_output, multiply.inputs[1])
        links.new(attribute.outputs["Color"], multiply.inputs[2])
        color_output = multiply.outputs["Color"]
    if color_output:
        links.new(color_output, shader.inputs["Base Color"])
    return material


def render_preview(output):
    source = asset_object()
    evaluated = source.evaluated_get(bpy.context.evaluated_depsgraph_get())
    mesh = bpy.data.meshes.new_from_object(evaluated, preserve_all_data_layers=True,
                                          depsgraph=bpy.context.evaluated_depsgraph_get())
    scene = bpy.data.scenes.new("asset_preview")
    obj = bpy.data.objects.new("preview_asset", mesh)
    scene.collection.objects.link(obj)
    mesh.materials.clear()
    for slot in evaluated.material_slots:
        mesh.materials.append(preview_material(slot.material, bool(mesh.color_attributes.get("Col"))))
    if not mesh.materials:
        mesh.materials.append(preview_material(None, bool(mesh.color_attributes.get("Col"))))

    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = 16
    scene.cycles.seed = 0
    scene.cycles.use_denoising = False
    scene.render.threads_mode = "FIXED"
    scene.render.threads = 4
    scene.render.resolution_x = scene.render.resolution_y = 256
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGB"
    scene.render.image_settings.color_depth = "8"
    scene.render.film_transparent = False
    scene.render.use_file_extension = True
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"
    scene.view_settings.exposure = 0.0
    scene.view_settings.gamma = 1.0
    scene.world = bpy.data.worlds.new("preview_world")
    scene.world.use_nodes = True
    background = scene.world.node_tree.nodes.get("Background")
    background.inputs["Color"].default_value = (0.18, 0.18, 0.18, 1.0)
    background.inputs["Strength"].default_value = 0.7

    corners = [Vector(corner) for corner in obj.bound_box]
    center = sum(corners, Vector()) / 8.0
    radius = max((p - center).length for p in corners)
    if radius <= 1e-6:
        raise ValueError(f"{source.name}: пустые габариты")
    direction = Vector((1.0, -1.0, 0.8)).normalized()
    camera_data = bpy.data.cameras.new("preview_camera")
    camera = bpy.data.objects.new("preview_camera", camera_data)
    scene.collection.objects.link(camera)
    camera.location = center + direction * radius * 4.0
    camera.rotation_euler = (-direction).to_track_quat("-Z", "Y").to_euler()
    camera_data.type = "ORTHO"
    rotation = camera.rotation_euler.to_matrix().transposed()
    projected = [rotation @ (p - center) for p in corners]
    camera_data.ortho_scale = max(max(p[a] for p in projected) - min(p[a] for p in projected)
                                  for a in (0, 1)) * 1.18
    camera_data.clip_start = max(radius * 0.001, 0.0001)
    camera_data.clip_end = radius * 20.0
    scene.camera = camera
    for name, offset, energy in (("key", (1, -2, 3), 350.0), ("fill", (-2, -1, 1), 150.0)):
        light_data = bpy.data.lights.new(name, "AREA")
        light_data.energy = energy * radius * radius
        light_data.shape = "DISK"
        light_data.size = radius * 3.0
        light = bpy.data.objects.new(name, light_data)
        scene.collection.objects.link(light)
        light.location = center + Vector(offset) * radius * 2.0
        light.rotation_euler = (center - light.location).to_track_quat("-Z", "Y").to_euler()
    output.parent.mkdir(parents=True, exist_ok=True)
    scene.render.filepath = str(output)
    bpy.ops.render.render(write_still=True, scene=scene.name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-o", "--output", type=Path)
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])
    output = args.output or (Path(__file__).resolve().parents[1] / "assets" / "previews"
                             / (Path(bpy.data.filepath).stem + ".png"))
    try:
        render_preview(output.resolve())
    except (OSError, ValueError, RuntimeError) as error:
        print(f"ОШИБКА: {error}", file=sys.stderr)
        raise SystemExit(1)


if __name__ == "__main__":
    main()
