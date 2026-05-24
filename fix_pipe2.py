import os

obj_path = r'c:\Users\k024g\source\repos\TD_Engine\Resources\Models\3Dmodel\pipe\pipe2.obj'

if not os.path.exists(obj_path):
    print("Error: pipe2.obj not found at", obj_path)
    exit(1)

with open(obj_path, 'r') as f:
    lines = f.readlines()

new_lines = []
for line in lines:
    if line.startswith('vn '):
        parts = line.strip().split()
        nx, ny, nz = float(parts[1]), float(parts[2]), float(parts[3])
        new_lines.append(f'vn {-nx} {-ny} {-nz}\n')
    elif line.startswith('f '):
        parts = line.strip().split()
        # Reverse the vertices to fix backface culling
        reversed_verts = parts[1:][::-1]
        new_lines.append('f ' + ' '.join(reversed_verts) + '\n')
    else:
        new_lines.append(line)

with open(obj_path, 'w') as f:
    f.writelines(new_lines)

print("Successfully fixed pipe2.obj inverted normals and winding order!")
