import os, json, glob

def process_file(filepath):
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            data = json.load(f)
    except Exception as e:
        return
    
    modified = False
    
    # Handle Prefab format
    if 'prefab' in data:
        obj = data['prefab']
        if obj.get('name') == 'Player':
            if obj.get('modelPath') != 'Resources/Models/3Dmodel/sword/ken.obj':
                obj['modelPath'] = 'Resources/Models/3Dmodel/sword/ken.obj'
                modified = True
            for comp in obj.get('components', []):
                if 'modelPath' in comp and comp.get('type') == 'MeshRenderer':
                    if comp['modelPath'] != 'Resources/Models/3Dmodel/sword/ken.obj':
                        comp['modelPath'] = 'Resources/Models/3Dmodel/sword/ken.obj'
                        modified = True
                if 'meshPath' in comp and comp.get('type') == 'GpuMeshCollider':
                    if comp['meshPath'] != 'Resources/Models/3Dmodel/sword/ken.obj':
                        comp['meshPath'] = 'Resources/Models/3Dmodel/sword/ken.obj'
                        modified = True
                        
    # Handle Scene format
    if 'objects' in data:
        for obj in data['objects']:
            if obj.get('name') == 'Player':
                for comp in obj.get('components', []):
                    if 'modelPath' in comp and comp.get('type') == 'MeshRenderer':
                        if comp['modelPath'] != 'Resources/Models/3Dmodel/sword/ken.obj':
                            comp['modelPath'] = 'Resources/Models/3Dmodel/sword/ken.obj'
                            modified = True
                    if 'meshPath' in comp and comp.get('type') == 'GpuMeshCollider':
                        if comp['meshPath'] != 'Resources/Models/3Dmodel/sword/ken.obj':
                            comp['meshPath'] = 'Resources/Models/3Dmodel/sword/ken.obj'
                            modified = True

    if modified:
        with open(filepath, 'w', encoding='utf-8') as f:
            json.dump(data, f, indent=2, ensure_ascii=False)
        print(f'Modified {filepath}')

base_dir = r'c:\Users\k024g\source\repos\TD_Engine\Resources'
for d in ['Scenes', 'Prefabs']:
    for f in glob.glob(os.path.join(base_dir, d, '*.*')):
        if f.endswith('.json') or f.endswith('.prefab'):
            process_file(f)
print('Done!')
