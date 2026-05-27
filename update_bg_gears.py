import json

path = 'c:/Users/k024g/source/repos/TD_Engine/Resources/Scenes/select.json'
with open(path, 'r', encoding='utf-8') as f:
    data = json.load(f)

# Add new background gears
new_gears = [
    {'id': 1048620, 'parentId': 4294967295, 'name': 'BgGear1Pivot', 'locked': False, 'translate': [-10, 8, 18], 'rotate': [1.5708, 0, 0], 'scale': [8, 8, 8], 'components': []},
    {'id': 1048621, 'parentId': 1048620, 'name': 'BgGear1', 'locked': False, 'translate': [0, 0, 0], 'rotate': [0, 0, 0], 'scale': [1, 1, 1], 'components': [{'type': 'MeshRenderer', 'enabled': True, 'modelPath': 'Resources/Models/3Dmodel/gear/gear.obj', 'texturePath': 'Resources/Textures/white1x1.png', 'shaderName': 'Steampunk', 'color': [0.4, 0.35, 0.3, 1.0]}]},
    
    {'id': 1048622, 'parentId': 4294967295, 'name': 'BgGear2Pivot', 'locked': False, 'translate': [8, 10, 22], 'rotate': [1.5708, 0, 0], 'scale': [10, 10, 10], 'components': []},
    {'id': 1048623, 'parentId': 1048622, 'name': 'BgGear2', 'locked': False, 'translate': [0, 0, 0], 'rotate': [0, 0, 0], 'scale': [1, 1, 1], 'components': [{'type': 'MeshRenderer', 'enabled': True, 'modelPath': 'Resources/Models/3Dmodel/gear/gear.obj', 'texturePath': 'Resources/Textures/white1x1.png', 'shaderName': 'Steampunk', 'color': [0.3, 0.3, 0.35, 1.0]}]},
    
    {'id': 1048624, 'parentId': 4294967295, 'name': 'BgGear3Pivot', 'locked': False, 'translate': [2, -10, 20], 'rotate': [1.5708, 0, 0], 'scale': [7, 7, 7], 'components': []},
    {'id': 1048625, 'parentId': 1048624, 'name': 'BgGear3', 'locked': False, 'translate': [0, 0, 0], 'rotate': [0, 0, 0], 'scale': [1, 1, 1], 'components': [{'type': 'MeshRenderer', 'enabled': True, 'modelPath': 'Resources/Models/3Dmodel/gear/gear.obj', 'texturePath': 'Resources/Textures/white1x1.png', 'shaderName': 'Steampunk', 'color': [0.45, 0.4, 0.35, 1.0]}]},

    {'id': 1048626, 'parentId': 4294967295, 'name': 'BgGear4Pivot', 'locked': False, 'translate': [-8, -8, 25], 'rotate': [1.5708, 0, 0], 'scale': [12, 12, 12], 'components': []},
    {'id': 1048627, 'parentId': 1048626, 'name': 'BgGear4', 'locked': False, 'translate': [0, 0, 0], 'rotate': [0, 0, 0], 'scale': [1, 1, 1], 'components': [{'type': 'MeshRenderer', 'enabled': True, 'modelPath': 'Resources/Models/3Dmodel/gear/gear.obj', 'texturePath': 'Resources/Textures/white1x1.png', 'shaderName': 'Steampunk', 'color': [0.35, 0.3, 0.25, 1.0]}]},
    
    {'id': 1048628, 'parentId': 4294967295, 'name': 'Pipe_Bg', 'locked': False, 'translate': [0, 5, 28], 'rotate': [0, 0, 1.5708], 'scale': [80, 5, 5], 'components': [{'type': 'MeshRenderer', 'enabled': True, 'modelPath': 'Resources/Models/3Dmodel/pipe/pipe1.obj', 'texturePath': 'Resources/Textures/white1x1.png', 'shaderName': 'Steampunk', 'color': [0.25, 0.2, 0.15, 1.0]}]}
]

for ng in new_gears:
    if not any(item.get('id') == ng['id'] for item in data['entities']):
        data['entities'].append(ng)

with open(path, 'w', encoding='utf-8') as f:
    json.dump(data, f, indent=2, ensure_ascii=False)
print('Done!')
