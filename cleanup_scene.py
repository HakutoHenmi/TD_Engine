import json
import os

def cleanup_scene(filepath):
    print(f"Loading scene file: {filepath}")
    with open(filepath, 'r', encoding='utf-8') as f:
        data = json.load(f)
    
    if "objects" not in data:
        print("No objects found in scene.")
        return
        
    original_count = len(data["objects"])
    cleaned_objects = []
    
    # 削除対象となる動的/ゾンビエンティティの条件
    # 初期配置されていないはずの防衛施設や一時オブジェクトを除外
    for obj in data["objects"]:
        name = obj.get("name", "")
        components = obj.get("components", [])
        
        # タグのチェック
        has_pipe_tag = False
        for comp in components:
            if comp.get("type") == "Tag" and comp.get("tag") in ["Pipe", "Canon", "Cannon", "BulletTank", "Missile", "Poison"]:
                has_pipe_tag = True
                break
        
        # 除外する名前リスト
        exclude_names = ["Pipe", "Cannon", "Canon", "BulletTank", "Missile", "Poison", "IceCanon", 
                         "SpawnedEnemy", "Bullet", "Contact", "VFX", "HitDistortion", "MirrorShatter", "Explosion"]
        
        should_exclude = False
        if has_pipe_tag:
            should_exclude = True
        else:
            for ex_name in exclude_names:
                if ex_name in name:
                    should_exclude = True
                    break
        
        if should_exclude:
            # ゾンビオブジェクトを除外
            continue
            
        cleaned_objects.append(obj)
        
    data["objects"] = cleaned_objects
    new_count = len(cleaned_objects)
    
    # 保存
    with open(filepath, 'w', encoding='utf-8') as f:
        json.dump(data, f, indent=2, ensure_ascii=False)
        
    print(f"Cleaned up scene successfully!")
    print(f"Removed {original_count - new_count} zombie entities.")
    print(f"Entity count: {original_count} -> {new_count}")

if __name__ == "__main__":
    scene_path = "Resources/Scenes/tesuto.json"
    if os.path.exists(scene_path):
        cleanup_scene(scene_path)
    else:
        print(f"Scene file not found: {scene_path}")
