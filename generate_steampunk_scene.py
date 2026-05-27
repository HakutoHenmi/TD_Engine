import json
import os

filepath = 'Resources/Scenes/title.json'
with open(filepath, 'r', encoding='utf-8') as f:
    data = json.load(f)

# Filter out existing generated objects
new_entities = []
for ent in data['entities']:
    name = ent.get('name', '')
    if not (name.startswith('Gear_') or name.startswith('Pipe_') or name in ['TitleCamera', 'DirLight']):
        new_entities.append(ent)

start_id = 1048600

# Camera
new_entities.append({
    "id": start_id,
    "parentId": 4294967295,
    "name": "TitleCamera",
    "locked": False,
    "translate": [0, 3, -15],
    "rotate": [0.1, 0, 0],
    "scale": [1, 1, 1],
    "components": [
        {"type": "Camera", "enabled": True}
    ]
})

# Light
new_entities.append({
    "id": start_id + 1,
    "parentId": 4294967295,
    "name": "DirLight",
    "locked": False,
    "translate": [0, 10, -10],
    "rotate": [0.5, 0.5, 0],
    "scale": [1, 1, 1],
    "components": [
        {"type": "DirectionalLight", "enabled": True, "color": [1.0, 0.8, 0.7], "intensity": 1.5}
    ]
})

# Materials:
# Copper: color.r > 0.7 -> [0.8, 0.4, 0.1, 1.0]
# Brass:  color.g > 0.5 -> [0.6, 0.8, 0.2, 1.0]
# Iron:   else          -> [0.3, 0.3, 0.3, 1.0]
materials = [
    [0.6, 0.8, 0.2, 1.0], # Brass
    [0.8, 0.4, 0.1, 1.0], # Copper
    [0.3, 0.3, 0.3, 1.0], # Iron
]

# Layout gears to look like they interlock in the background
gears = [
    {"name": "Gear_0", "pos": [-6, 6, 5], "scale": 4.0, "mat": 0},
    {"name": "Gear_1", "pos": [-2, 4, 4], "scale": 3.0, "mat": 1},
    {"name": "Gear_2", "pos": [3, 5, 5], "scale": 5.0, "mat": 0},
    {"name": "Gear_3", "pos": [8, 7, 6], "scale": 3.5, "mat": 2},
    {"name": "Gear_4", "pos": [-8, 0, 4.5], "scale": 3.5, "mat": 1},
    {"name": "Gear_5", "pos": [-4, -2, 5.5], "scale": 4.5, "mat": 2},
    {"name": "Gear_6", "pos": [1, -4, 4], "scale": 3.0, "mat": 0},
    {"name": "Gear_7", "pos": [5, -2, 6], "scale": 4.0, "mat": 1},
    {"name": "Gear_8", "pos": [9, 1, 5], "scale": 3.0, "mat": 0},
    {"name": "Gear_9", "pos": [-10, -6, 5], "scale": 5.0, "mat": 0},
]

for i, g in enumerate(gears):
    new_entities.append({
        "id": start_id + 2 + i,
        "parentId": 4294967295,
        "name": g["name"],
        "locked": False,
        "translate": g["pos"],
        "rotate": [0, 0, 0],
        "scale": [g["scale"], g["scale"], g["scale"]],
        "components": [
            {"type": "MeshRenderer", "enabled": True, "modelPath": "Resources/Models/3Dmodel/gear/gear.obj", "texturePath": "Resources/Textures/white1x1.png", "shaderName": "Steampunk", "color": materials[g["mat"]]}
        ]
    })

# Pipes
pipes = [
    {"name": "Pipe_0", "pos": [-8, 3, 2], "rot": [0, 0, 1.57], "scale": 2.0, "mat": 2}, # Vertical
    {"name": "Pipe_1", "pos": [0, 8, 3], "rot": [1.57, 0, 0], "scale": 3.0, "mat": 1},   # Horizontal
    {"name": "Pipe_2", "pos": [7, -5, 2], "rot": [0, 0, 1.57], "scale": 2.5, "mat": 0}, # Vertical
    {"name": "Pipe_3", "pos": [4, 2, 7], "rot": [1.57, 0, 0], "scale": 1.5, "mat": 2},   # Horizontal
    {"name": "Pipe_4", "pos": [-5, -7, 3], "rot": [1.57, 0, 0], "scale": 2.0, "mat": 1}, # Horizontal
]

for i, p in enumerate(pipes):
    new_entities.append({
        "id": start_id + 12 + i,
        "parentId": 4294967295,
        "name": p["name"],
        "locked": False,
        "translate": p["pos"],
        "rotate": p["rot"],
        "scale": [p["scale"], p["scale"], p["scale"]],
        "components": [
            {"type": "MeshRenderer", "enabled": True, "modelPath": "Resources/Models/3Dmodel/pipe/pipe1.obj", "texturePath": "Resources/Textures/white1x1.png", "shaderName": "Steampunk", "color": materials[p["mat"]]}
        ]
    })

data['entities'] = new_entities

with open(filepath, 'w', encoding='utf-8') as f:
    json.dump(data, f, indent=2, ensure_ascii=False)

print("title.json updated successfully!")
