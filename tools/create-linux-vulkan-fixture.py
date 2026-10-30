#!/usr/bin/env python3
"""Create a disposable renderer fixture. This does not replace missing original project assets."""
import argparse
import copy
import hashlib
import json
import pathlib
import shutil
import struct
import zlib

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument(
    "output", type=pathlib.Path, help="new fixture project directory (must not exist)"
)
parser.add_argument(
    "--engine-root",
    type=pathlib.Path,
    default=pathlib.Path(__file__).resolve().parents[1],
)
parser.add_argument(
    "--render-path",
    choices=("forward", "deferred"),
    default="forward",
    help="deferred uses native static materials without forward-only custom shader references",
)
args = parser.parse_args()
src = args.engine_root.resolve()
root = args.output.resolve()
if root.exists():
    parser.error(
        "output already exists; choose a fresh directory so existing project edits are preserved"
    )
root.mkdir(parents=True)
(root / "Content").mkdir()
# Copy only shared input/UI support; original game scenes stay in the original project.
for folder in ["Content/Config", "Content/UI", "Config"]:
    if (src / folder).exists():
        shutil.copytree(src / folder, root / folder, dirs_exist_ok=True)
for folder in ["Materials", "Textures", "Scenes", "UI", "Mesh"]:
    (root / "Content" / folder).mkdir(exist_ok=True)
config = json.loads((src / "MyEngine.project.json").read_text())
config["name"] = "Linux Vulkan Diagnostic"
config["projectId"] = hashlib.sha256(b"myengine-linux-vulkan-diagnostic-v1").hexdigest()
config["graphics"].update(
    backend="vulkan", hardwareRayTracing=False, renderPath="forward"
)
config["startupScene"] = "Content/Scenes/VulkanDiagnostic.scene.json"
(root / "MyEngine.project.json").write_text(json.dumps(config, indent=2))


def png(path):
    w = h = 64
    raw = b"".join(
        (
            b"\x00"
            + b"".join(
                (
                    bytes(
                        (245, 210, 35, 255)
                        if (x // 8 + y // 8) % 2
                        else (25, 80, 210, 255)
                    )
                    for x in range(w)
                )
            )
            for y in range(h)
        )
    )

    def chunk(t, d):
        return (
            struct.pack(">I", len(d))
            + t
            + d
            + struct.pack(">I", zlib.crc32(t + d) & 4294967295)
        )

    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw))
        + chunk(b"IEND", b"")
    )


png(root / "Content/Textures/AcceptanceChecker.png")
for name, color, texture in [
    ("Red", [0.95, 0.07, 0.05, 1], False),
    ("Green", [0.07, 0.85, 0.2, 1], False),
    ("Checker", [1, 1, 1, 1], True),
    ("Ground", [0.4, 0.45, 0.52, 1], False),
]:
    m = {
        "type": "Material",
        "version": 2,
        "name": name,
        "shader": "Content/Engine/Shaders/Mesh.shader",
        "surface": {
            "blendMode": "Opaque",
            "twoSided": False,
            "wireframe": False,
            "alphaThreshold": 0.5,
        },
        "overrides": {
            "properties": {
                "BaseColor": {"type": "Vec4", "data": color},
                "Metallic": {"type": "Float", "data": [0]},
                "Roughness": {"type": "Float", "data": [0.5]},
            },
            "textures": (
                {"BaseColorMap": "../Textures/AcceptanceChecker.png"} if texture else {}
            ),
        },
    }
    (root / f"Content/Materials/Acceptance{name}.mat").write_text(
        json.dumps(m, indent=2)
    )
actors = []


def actor(name, pos, scale, components, rot=[0, 0, 0]):
    actors.append(
        {
            "id": len(actors) + 1,
            "name": name,
            "active": True,
            "parentID": 0,
            "transform": {"position": pos, "rotation": rot, "scale": scale},
            "components": components,
        }
    )


def component(t, data):
    return {"type": t, "version": 1, "enabled": True, "data": data}


def mesh(mat, path="__builtin__/Cube"):
    return component(
        "MeshRenderer",
        {
            "mesh": path,
            "material": f"Content/Materials/Acceptance{mat}.mat",
            "materials": [f"Content/Materials/Acceptance{mat}.mat"],
        },
    )


actor("Red Cube", [-1.8, 0.6, 1], [1, 1.2, 1], [mesh("Red")], [0, 20, 0])
actor("Checker Cube", [0, 0.6, 1], [1, 1.2, 1], [mesh("Checker")], [0, 20, 0])
actor("Green Cube", [1.8, 0.6, 1], [1, 1.2, 1], [mesh("Green")], [0, 20, 0])
actor("Ground", [0, -0.12, 1], [10, 0.2, 10], [mesh("Ground")])
actor(
    "Sun",
    [0, 3, -1],
    [1, 1, 1],
    [
        component(
            "Light",
            {
                "properties": {
                    "type": 0,
                    "color": [1, 0.95, 0.85],
                    "intensity": 3,
                    "castsShadows": True,
                    "shadowIntensity": 1,
                    "range": 20,
                }
            },
        )
    ],
    [45, -25, 0],
)
actor(
    "Acceptance Camera",
    [0, 2.6, -7],
    [1, 1, 1],
    [
        component(
            "Camera",
            {
                "properties": {
                    "mainCamera": True,
                    "fovYDegrees": 55,
                    "nearClip": 0.1,
                    "farClip": 100,
                    "clearColor": [0.06, 0.08, 0.13],
                }
            },
        )
    ],
    [12, 0, 0],
)
actor(
    "Acceptance HUD",
    [0, 0, 0],
    [1, 1, 1],
    [
        component(
            "UICanvas",
            {
                "properties": {
                    "canvasSpace": "Screen",
                    "defaultFontPaths": ["Content/UI/Fonts/LatoLatin-Regular.ttf"],
                    "documentPath": "Content/UI/Acceptance.rml",
                    "stylePaths": [],
                    "generatedStylePaths": [],
                    "inputMode": "GameAndUI",
                    "interactive": True,
                    "sortOrder": 0,
                    "sourceMode": "AssetDocument",
                    "visible": True,
                }
            },
        )
    ],
)
scene = {
    "name": "VulkanDiagnostic",
    "actors": actors,
    "nextID": len(actors) + 1,
    "mainCameraHintActorID": 6,
}
(root / "Content/Scenes/VulkanDiagnostic.scene.json").write_text(
    json.dumps(scene, indent=2)
)
(root / "Content/UI/Acceptance.rml").write_text(
    '<rml><head><title>Vulkan Acceptance</title><style>body { font-family: LatoLatin; font-size: 20px; color: #ffffff; } #badge { position: absolute; left: 20px; top: 20px; width: 460px; padding: 12px; background-color: #16324f; border: 2px #6dd3ce; } </style></head><body><div id="badge">LINUX VULKAN: geometry + texture + UI</div></body></rml>'
)
(root / "Content/Mesh/AcceptanceTriangle.obj").write_text(
    "o AcceptanceTriangle\nv -0.5 0 0\nv 0.5 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0.5 1\nvn 0 0 -1\nf 1/1/1 3/3/1 2/2/1\n"
)
print(root)
pbr_scene = copy.deepcopy(scene)
pbr_scene["name"] = "VulkanPBRDiagnostic"
for name in ("Red", "Green", "Checker", "Ground"):
    material = json.loads(
        (root / f"Content/Materials/Acceptance{name}.mat").read_text()
    )
    material["name"] += " PBR"
    material["shader"] = "Content/Engine/Shaders/ShadowedMainPass.shader"
    material["overrides"]["properties"]["Metallic"]["data"] = [
        0.65 if name == "Red" else 0.0
    ]
    material["overrides"]["properties"]["Roughness"]["data"] = [
        0.3 if name == "Red" else 0.65
    ]
    (root / f"Content/Materials/Acceptance{name}PBR.mat").write_text(
        json.dumps(material, indent=2)
    )
for item in pbr_scene["actors"]:
    for comp in item["components"]:
        if comp["type"] == "MeshRenderer":
            data = comp["data"]
            data["material"] = data["material"].replace(".mat", "PBR.mat")
            data["materials"] = [
                path.replace(".mat", "PBR.mat") for path in data["materials"]
            ]
(root / "Content/Scenes/VulkanPBRDiagnostic.scene.json").write_text(
    json.dumps(pbr_scene, indent=2)
)

if args.render_path == "deferred":
    # The diagnostic custom code shaders provide Forward, not GBuffer passes.
    # Native static materials exercise the GPU-driven indirect deferred path.
    for path in (root / "Content/Materials").glob("Acceptance*.mat"):
        material = json.loads(path.read_text())
        material.pop("shader", None)
        material.pop("parent", None)
        path.write_text(json.dumps(material, indent=2))
    config["name"] += " Deferred"
    config["projectId"] = hashlib.sha256(
        b"myengine-linux-vulkan-deferred-v1"
    ).hexdigest()
    config["graphics"]["renderPath"] = "deferred"
    config["startupScene"] = "Content/Scenes/VulkanPBRDiagnostic.scene.json"
    (root / "MyEngine.project.json").write_text(json.dumps(config, indent=2))
