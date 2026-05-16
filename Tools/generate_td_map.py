"""
TD Engine - 大規模タワーディフェンスマップ生成スクリプト
Blenderで実行: テキストエディタに貼り付け → Alt+P で実行

マップ構成:
- 1枚の大きな地面
- 6方向から敵が攻めてくるルート（隙間なし）
- 山（パスとの重なり防止）
- 全オブジェクト分離出力
"""

import bpy
import bmesh
import math
import json
import os
import random
from mathutils import Vector, Matrix

# ============================================================
# 設定
# ============================================================
OUTPUT_DIR = "C:/Users/k024g/source/repos/TD_Engine/Resources/Scenes/Map/"
MAP_SIZE = 350.0          # マップの半辺の長さ（全体 700x700）
BASE_RADIUS = 18.0        # ベース平地の半径（ルートの始点用）
PATH_WIDTH = 15.0         # 道幅
MOUNTAIN_HEIGHT = 12.0    # 山の最大高さ

random.seed(42)  # 再現性のためシード固定

# ============================================================
# ユーティリティ
# ============================================================
def clear_scene():
    """シーンを全クリア"""
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    for m in list(bpy.data.meshes):
        bpy.data.meshes.remove(m)

def create_object(name, verts, faces):
    """頂点とフェイスからオブジェクト作成（ローカル原点を中心に調整）"""
    if not verts:
        return None
        
    # X, Zの中心を計算 (Yは足元基準にするため0とする)
    min_x = min(v[0] for v in verts)
    max_x = max(v[0] for v in verts)
    min_z = min(v[2] for v in verts)
    max_z = max(v[2] for v in verts)
    cx = (min_x + max_x) / 2.0
    cy = 0.0
    cz = (min_z + max_z) / 2.0
    
    # 頂点をローカル座標に変換
    local_verts = [(v[0] - cx, v[1] - cy, v[2] - cz) for v in verts]
    
    # エンジン座標系(Y-Up) を Blender座標系(Z-Up) に変換（X, Y, Z -> X, -Z, Y）
    blender_verts = [(v[0], -v[2], v[1]) for v in local_verts]
    # 座標変換でハンドネスが反転するため、フェイスの巻き順も反転
    reversed_faces = [f[::-1] for f in faces]

    mesh = bpy.data.meshes.new(name + "_mesh")
    obj = bpy.data.objects.new(name, mesh)
    
    # Blender上での位置を正しく設定 (X, -Z, Y)
    obj.location = (cx, -cz, cy)

    bpy.context.collection.objects.link(obj)
    mesh.from_pydata(blender_verts, [], reversed_faces)
    mesh.update()
    return obj

def create_plane(name, cx, cz, sx, sz, y=0.0, subdivx=1, subdivz=1):
    """四角形の平面を作成 (中心cx,cz / サイズsx,sz)"""
    verts = []
    faces = []
    for iz in range(subdivz + 1):
        for ix in range(subdivx + 1):
            x = cx - sx/2 + sx * ix / subdivx
            z = cz - sz/2 + sz * iz / subdivz
            verts.append((x, y, z))
    for iz in range(subdivz):
        for ix in range(subdivx):
            i = iz * (subdivx + 1) + ix
            faces.append((i, i+1, i+subdivx+2, i+subdivx+1))
    return create_object(name, verts, faces)

def create_path_segment(name, p0, p1, width, y=0.05):
    """2点間の道セグメント（幅付き平面）- 隣接セグメントと隙間なく接続"""
    d = Vector((p1[0]-p0[0], 0, p1[2]-p0[2]))
    length = d.length
    if length < 0.01:
        return None
    d.normalize()
    n = Vector((-d.z, 0, d.x)) * (width / 2)
    
    verts = [
        (p0[0]+n.x, y, p0[2]+n.z),
        (p0[0]-n.x, y, p0[2]-n.z),
        (p1[0]-n.x, y, p1[2]-n.z),
        (p1[0]+n.x, y, p1[2]+n.z),
    ]
    faces = [(0,1,2,3)]
    return create_object(name, verts, faces)

def lerp(a, b, t):
    return a + (b - a) * t

def catmull_rom(p0, p1, p2, p3, t):
    """Catmull-Romスプライン補間"""
    t2 = t * t
    t3 = t2 * t
    x = 0.5 * ((2*p1[0]) + (-p0[0]+p2[0])*t + (2*p0[0]-5*p1[0]+4*p2[0]-p3[0])*t2 + (-p0[0]+3*p1[0]-3*p2[0]+p3[0])*t3)
    z = 0.5 * ((2*p1[2]) + (-p0[2]+p2[2])*t + (2*p0[2]-5*p1[2]+4*p2[2]-p3[2])*t2 + (-p0[2]+3*p1[2]-3*p2[2]+p3[2])*t3)
    return (x, 0, z)

def smooth_path(waypoints, subdivisions=6):
    """ウェイポイントをスムーズカーブに変換"""
    if len(waypoints) < 2:
        return waypoints
    
    # 端点を複製してCatmull-Romに対応
    pts = [waypoints[0]] + waypoints + [waypoints[-1]]
    result = []
    for i in range(1, len(pts) - 2):
        for j in range(subdivisions):
            t = j / subdivisions
            p = catmull_rom(pts[i-1], pts[i], pts[i+1], pts[i+2], t)
            result.append(p)
    result.append(waypoints[-1])
    return result

def create_mountain(name, cx, cz, radius, height, segments=8, rings=4):
    """プロシージャル山を生成"""
    verts = [(cx, 0, cz)]  # ベース中心
    
    # リングごとに頂点を追加（上から下）
    for r in range(rings + 1):
        ring_ratio = r / rings  # 0(頂上) → 1(底)
        ring_radius = radius * ring_ratio
        ring_height = height * (1.0 - ring_ratio ** 1.5)
        
        for s in range(segments):
            a = 2 * math.pi * s / segments
            # ランダムな凹凸を追加
            noise_r = 1.0 + random.uniform(-0.2, 0.2) * ring_ratio
            noise_h = random.uniform(-0.1, 0.1) * height * ring_ratio
            
            x = cx + ring_radius * math.cos(a) * noise_r
            z = cz + ring_radius * math.sin(a) * noise_r
            y = ring_height + noise_h
            verts.append((x, y, z))
    
    faces = []
    # 頂上のファン
    for s in range(segments):
        next_s = (s + 1) % segments
        faces.append((0, 1 + s, 1 + next_s))
    
    # リング間のフェイス
    for r in range(rings):
        base = 1 + r * segments
        next_base = 1 + (r + 1) * segments
        for s in range(segments):
            next_s = (s + 1) % segments
            faces.append((base + s, next_base + s, next_base + next_s, base + next_s))
    
    return create_object(name, verts, faces)

# ============================================================
# マップ生成
# ============================================================
clear_scene()
all_objects = []

print("=== TD Map Generation Start ===")

# --------------------------------------------------
# 1. 地面（1枚の大きな平面）
# --------------------------------------------------
print("Generating ground plane...")
ground = create_plane("Ground", 0, 0, MAP_SIZE * 2, MAP_SIZE * 2, y=0.0, subdivx=1, subdivz=1)
all_objects.append(ground)
print("  Created 1 ground plane")

# --------------------------------------------------
# 2. 敵ルート定義（6方向）
# --------------------------------------------------
print("Generating enemy routes...")

# ルートのウェイポイント定義 (160.0 スケールで定義後、動的に拡大)
base_routes = [
    {
        "name": "Route_North",
        "type": "straight",
        "waypoints": [
            (0, 0, -BASE_RADIUS), (0, 0, -45), (0, 0, -80),
            (0, 0, -120), (0, 0, -145), (0, 0, -160)
        ]
    },
    {
        "name": "Route_NorthEast",
        "type": "winding",
        "waypoints": [
            (12, 0, -12),
            (35, 0, -30), (50, 0, -40), (45, 0, -65),
            (60, 0, -85), (80, 0, -80), (95, 0, -110),
            (115, 0, -125), (130, 0, -140), (160, 0, -160)
        ]
    },
    {
        "name": "Route_East",
        "type": "winding",
        "waypoints": [
            (BASE_RADIUS, 0, 0),
            (40, 0, 10), (60, 0, -5), (85, 0, 15),
            (105, 0, -10), (130, 0, 5), (145, 0, 0),
            (160, 0, 0)
        ]
    },
    {
        "name": "Route_SouthEast",
        "type": "straight",
        "waypoints": [
            (12, 0, 12), (40, 0, 40), (70, 0, 70),
            (100, 0, 100), (130, 0, 130), (160, 0, 160)
        ]
    },
    {
        "name": "Route_South",
        "type": "winding",
        "waypoints": [
            (0, 0, BASE_RADIUS),
            (-10, 0, 40), (15, 0, 65), (-15, 0, 90),
            (10, 0, 120), (0, 0, 140), (0, 0, 160)
        ]
    },
    {
        "name": "Route_West",
        "type": "winding",
        "waypoints": [
            (-BASE_RADIUS, 0, 0),
            (-45, 0, -10), (-65, 0, 15), (-85, 0, -15),
            (-110, 0, 0), (-130, 0, 15), (-145, 0, -5),
            (-160, 0, 0)
        ]
    },
]

route_data_for_json = []  # エンジン用のルートデータ
routes = []

for ri, route in enumerate(base_routes):
    rname = route["name"]
    # MAP_SIZE倍率に合わせてルートウェイポイントも拡大
    scale_factor = MAP_SIZE / 160.0
    wps = [(x * scale_factor, y, z * scale_factor) for (x, y, z) in route["waypoints"]]
    
    routes.append({"name": rname, "type": route["type"], "waypoints": wps})
    
    # スムーズ化（subdivisions増やして隙間対策）
    smooth_wps = smooth_path(wps, subdivisions=16)
    
    # パスセグメント作成（隙間なく接続）→ ルートごとに1メッシュへ結合
    route_segments = []
    for si in range(len(smooth_wps) - 1):
        p0 = smooth_wps[si]
        p1 = smooth_wps[si + 1]
        seg_name = f"{rname}_Path_{si:03d}"
        seg = create_path_segment(seg_name, p0, p1, PATH_WIDTH, y=0.1)
        if seg:
            route_segments.append(seg)

    if route_segments:
        bpy.ops.object.select_all(action='DESELECT')
        for seg in route_segments:
            seg.select_set(True)
        bpy.context.view_layer.objects.active = route_segments[0]
        bpy.ops.object.join()
        merged = bpy.context.view_layer.objects.active
        merged.name = f"{rname}_Merged"
        # ★地形出力をオフにするため追加しない
        # all_objects.append(merged)
        print(f"  Merged {len(route_segments)} segments -> {merged.name} (Export Skipped)")
    
    # ルートデータ保存（エンジンのフローフィールド用）
    spawn_pt = wps[-1]
    route_data_for_json.append({
        "name": rname,
        "type": route["type"],
        "waypoints": [{"x": p[0], "y": p[1], "z": p[2]} for p in wps],
        "spawn": {"x": spawn_pt[0], "y": 0, "z": spawn_pt[2]},
        "target": {"x": 0, "y": 0, "z": 0}
    })

print(f"  Created {len(routes)} routes")

# --------------------------------------------------
# 3. 山岳（自動配置：パスとの重なり防止）
# --------------------------------------------------
print("Generating mountains & hills...")
mountain_idx = 0

# 全てのパスの細かい座標リストを生成（コリジョン判定用）
all_path_points = []
for route in routes:
    pts = smooth_path(route["waypoints"], subdivisions=12)
    all_path_points.extend(pts)

def is_far_from_path(x, z, required_dist):
    for p in all_path_points:
        dist = math.sqrt((x - p[0])**2 + (z - p[2])**2)
        if dist < required_dist:
            return False
    return True

# 大きめの山
for _ in range(120):
    mx = random.uniform(-MAP_SIZE + 10, MAP_SIZE - 10)
    mz = random.uniform(-MAP_SIZE + 10, MAP_SIZE - 10)
    # センターベース付近には置かない
    if math.sqrt(mx*mx + mz*mz) < BASE_RADIUS + 10:
        continue
    
    mr = random.uniform(15, 30)
    mh = random.uniform(10, MOUNTAIN_HEIGHT * 2.0)
    
    # 道からの距離が (山の半径 + 道幅の半分 + マージン) 以上離れているか確認
    if is_far_from_path(mx, mz, mr + PATH_WIDTH/2 + 5.0):
        name = f"Mountain_{mountain_idx:03d}"
        mt = create_mountain(name, mx, mz, mr, mh, segments=12, rings=4)
        all_objects.append(mt)
        mountain_idx += 1

# 小さめの丘
for _ in range(250):
    hx = random.uniform(-MAP_SIZE + 10, MAP_SIZE - 10)
    hz = random.uniform(-MAP_SIZE + 10, MAP_SIZE - 10)
    hr = random.uniform(6, 12)
    hh = random.uniform(5, 10)
    
    if is_far_from_path(hx, hz, hr + PATH_WIDTH/2 + 3.0):
        name = f"Hill_{mountain_idx:03d}"
        hill = create_mountain(name, hx, hz, hr, hh, segments=8, rings=3)
        # ★地形出力をオフにするため追加しない
        # all_objects.append(hill)
        mountain_idx += 1

print(f"  Created {mountain_idx} mountains/hills")

# ============================================================
# エクスポート
# ============================================================
print(f"\n=== Exporting {len(all_objects)} objects ===")
os.makedirs(OUTPUT_DIR, exist_ok=True)

scene_data = {
    "map_name": "TD_Large_Map_Square",
    "map_size": MAP_SIZE,
    "base_radius": BASE_RADIUS,
    "routes": route_data_for_json,
    "objects": []
}

exported = 0
for obj in all_objects:
    if obj is None:
        continue
    
    # 全選択解除 → このオブジェクトのみ選択
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    
    # JSONに位置情報を記録 (エクスポートで(0,0,0)にする前に保存)
    loc = obj.location.copy()
    rot = obj.rotation_euler.copy()
    scl = obj.scale.copy()
    
    # 確実に出力される頂点がローカル座標になるように、いったん(0,0,0)にリセット
    obj.location = (0, 0, 0)
    bpy.context.view_layer.update()
    
    # OBJエクスポート
    filepath = os.path.join(OUTPUT_DIR, f"{obj.name}.obj")
    bpy.ops.wm.obj_export(
        filepath=filepath,
        export_selected_objects=True,
        apply_modifiers=True,
        forward_axis='NEGATIVE_Z',
        up_axis='Y',
        export_materials=False,
        export_uv=True,
        export_normals=True,
    )
    
    # エクスポート後にBlender上の位置を元に戻す(Blenderの見た目を保つため)
    obj.location = loc
    
    # JSONには元のエンジン座標系(Y-Up)で出力し直す
    obj_entry = {
        "name": obj.name,
        "mesh": f"{obj.name}.obj",
        "position": [round(loc.x, 4), round(loc.z, 4), round(-loc.y, 4)],
        "rotation": [round(rot.x, 4), round(rot.z, 4), round(-rot.y, 4)],
        "scale": [round(scl.x, 4), round(scl.z, 4), round(scl.y, 4)],
        "tag": "ground" if "Ground" in obj.name else
               "path" if "Path" in obj.name else
               "mountain" if "Mountain" in obj.name or "Hill" in obj.name else
               "structure"
    }
    scene_data["objects"].append(obj_entry)
    exported += 1
    
    if exported % 50 == 0:
        print(f"  Exported {exported}/{len(all_objects)}...")

# JSONレイアウトファイル出力
json_path = os.path.join(OUTPUT_DIR, "map_layout.json")
with open(json_path, 'w', encoding='utf-8') as f:
    json.dump(scene_data, f, indent=2, ensure_ascii=False)

print(f"\n=== Export Complete ===")
print(f"  Objects: {exported}")
print(f"  Output: {OUTPUT_DIR}")
print(f"  Layout: {json_path}")
print(f"  Routes: {len(route_data_for_json)}")
