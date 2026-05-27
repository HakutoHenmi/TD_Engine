import json
import math

with open('Resources/Scenes/select.json', 'r', encoding='utf-8') as f:
    data = json.load(f)

# Find background and make it transparent
for obj in data['objects']:
    if obj['name'] == 'Background':
        for comp in obj['components']:
            if comp['type'] == 'UIImage':
                comp['color'][3] = 0.0

max_id = max([obj['id'] for obj in data['objects']])

# Check if Camera already exists
if not any(obj['name'] == 'Camera' for obj in data['objects']):
    data['objects'].append({
        'id': max_id + 1, 'parentId': 4294967295, 'name': 'Camera', 'locked': False,
        'translate': [0, 0, -15], 'rotate': [0, 0, 0], 'scale': [1, 1, 1],
        'components': [{'type': 'Camera', 'enabled': True}]
    })

if not any(obj['name'] == 'DirLight' for obj in data['objects']):
    data['objects'].append({
        'id': max_id + 2, 'parentId': 4294967295, 'name': 'DirLight', 'locked': False,
        'translate': [0, 10, -10], 'rotate': [0.5, 0.5, 0], 'scale': [1, 1, 1],
        'components': [{'type': 'DirectionalLight', 'enabled': True, 'color': [1.0, 0.8, 0.7], 'intensity': 1.5}]
    })

if not any(obj['name'] == 'MainGear' for obj in data['objects']):
    data['objects'].append({
        'id': max_id + 3, 'parentId': 4294967295, 'name': 'MainGear', 'locked': False,
        'translate': [6, -4, 5], 'rotate': [0, 0, 0], 'scale': [12, 12, 12], # Placed on the right
        'components': [{'type': 'MeshRenderer', 'enabled': True, 'modelPath': 'Resources/Models/3Dmodel/gear/gear.obj', 'texturePath': 'Resources/Textures/white1x1.png', 'shaderName': 'Steampunk', 'color': [0.6, 0.5, 0.3, 1.0]}]
    })

# Arrange buttons on the left side to correspond to the right-side gear
# Let's say gear center is around right-bottom
buttons = ['StageButton_0', 'StageButton_1', 'StageButton_2']
# Let's put texts in an arc. If the gear is on the right, texts should be on the left of the gear.
angles = [-15, 0, 15] # degrees
radius = 350
cx = 400
cy = 100

for i, bname in enumerate(buttons):
    for obj in data['objects']:
        if obj['name'] == bname:
            for comp in obj['components']:
                if comp['type'] == 'RectTransform':
                    rad = math.radians(angles[i] + 180) # Left side of the circle
                    x = int(cx + radius * math.cos(rad))
                    y = int(cy + radius * math.sin(rad))
                    comp['pos'] = [x, y]
                    comp['rotation'] = 0 # keep text horizontal for readability

with open('Resources/Scenes/select.json', 'w', encoding='utf-8') as f:
    json.dump(data, f, indent=2, ensure_ascii=False)
