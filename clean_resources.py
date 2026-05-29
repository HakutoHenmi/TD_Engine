import os
import shutil
import re

project_dir = r"C:\Users\k024g\source\repos\TD_Engine"
resources_dir = os.path.join(project_dir, "Resources")

# スキャンするソースファイル群
src_exts = {".cpp", ".h", ".json", ".prefab", ".csv", ".txt"}
src_files = []
for root, dirs, files in os.walk(project_dir):
    if ".git" in root or "x64" in root or ".vs" in root or "externals" in root:
        continue
    for f in files:
        if os.path.splitext(f)[1].lower() in src_exts:
            src_files.append(os.path.join(root, f))

# 全Resourcesファイルを取得
all_resources = []
for root, dirs, files in os.walk(resources_dir):
    for f in files:
        all_resources.append(os.path.join(root, f))

# ファイル使用状況と新しいパスの決定
file_info = []

standard_dirs = {"Models", "Textures", "Audio", "Sound", "Prefabs", "Scenes", "Stages", "UI", "Fonts", "Scripts", "Data", "shaders"}

for filepath in all_resources:
    rel_path = os.path.relpath(filepath, resources_dir).replace('\\', '/')
    filename = os.path.basename(filepath)
    ext = os.path.splitext(filename)[1].lower()
    
    parts = rel_path.split('/')
    top_dir = parts[0] if len(parts) > 1 else ""
    
    new_rel_path = rel_path
    
    if top_dir not in standard_dirs:
        # 再分類する
        if ext in {".obj", ".mtl", ".gltf", ".glb", ".bin", ".fbx", ".blend"}:
            new_rel_path = "Models/" + rel_path
        elif ext in {".png", ".jpg", ".jpeg", ".tga", ".hdr", ".ico"}:
            new_rel_path = "Textures/" + rel_path
        elif ext in {".wav", ".mp3", ".ogg"}:
            new_rel_path = "Audio/" + rel_path
        elif ext in {".prefab"}:
            new_rel_path = "Prefabs/" + rel_path
        elif ext in {".json"}:
            if "scene" in filename.lower() or "stage" in filename.lower() or "pahase" in filename.lower():
                new_rel_path = "Scenes/" + rel_path
            else:
                new_rel_path = "Data/" + rel_path
        elif ext in {".csv"}:
            new_rel_path = "Data/" + rel_path
    else:
        # すでに標準ディレクトリに入っているが、SoundはAudioにするなどの微調整
        if top_dir == "Sound":
            new_rel_path = "Audio/" + "/".join(parts[1:])
    
    file_info.append({
        "abs_path": filepath,
        "old_rel_path": rel_path,
        "new_rel_path": new_rel_path,
        "used": False,
        "filename": filename
    })

# ファイルのキャッシュと使用状況のチェック、および置換
for src_file in src_files:
    encoding = 'utf-8'
    try:
        with open(src_file, 'r', encoding='utf-8') as f:
            content = f.read()
    except Exception:
        try:
            encoding = 'shift_jis'
            with open(src_file, 'r', encoding='shift_jis') as f:
                content = f.read()
        except Exception:
            continue
    
    original_content = content
    modified = False
    
    # 逆順ソートして長いパスから置換（部分一致を防ぐため）
    sorted_info = sorted(file_info, key=lambda x: len(x["old_rel_path"]), reverse=True)
    
    for info in sorted_info:
        # パス文字列がコード内にあるか確認
        old_path_str = "Resources/" + info["old_rel_path"]
        old_path_str2 = "Resources\\" + info["old_rel_path"].replace('/', '\\')
        
        # 単純なファイル名だけで使われているか（拡張子あり）
        if info["filename"] in content:
            info["used"] = True
            
        # パス置換
        if info["old_rel_path"] != info["new_rel_path"]:
            if old_path_str in content:
                content = content.replace(old_path_str, "Resources/" + info["new_rel_path"])
                modified = True
                info["used"] = True
            if old_path_str2 in content:
                content = content.replace(old_path_str2, "Resources/" + info["new_rel_path"])
                modified = True
                info["used"] = True

    if modified:
        try:
            with open(src_file, 'w', encoding=encoding) as f:
                f.write(content)
        except Exception as e:
            print(f"Error writing {src_file}: {e}")

# ファイルの移動と削除
used_count = 0
unused_count = 0

for info in file_info:
    old_abs = info["abs_path"]
    new_abs = os.path.join(resources_dir, os.path.normpath(info["new_rel_path"]))
    
    if info["used"]:
        used_count += 1
        if old_abs != new_abs:
            os.makedirs(os.path.dirname(new_abs), exist_ok=True)
            shutil.move(old_abs, new_abs)
            print(f"Moved: {info['old_rel_path']} -> {info['new_rel_path']}")
    else:
        unused_count += 1
        try:
            os.remove(old_abs)
            print(f"Deleted unused: {info['old_rel_path']}")
        except Exception as e:
            print(f"Failed to delete {old_abs}: {e}")

# 空ディレクトリの削除
for root, dirs, files in os.walk(resources_dir, topdown=False):
    if not os.listdir(root):
        os.rmdir(root)

print(f"\nDone. Used files: {used_count}, Deleted unused files: {unused_count}")
