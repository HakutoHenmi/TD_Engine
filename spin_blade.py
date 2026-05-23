import sys

obj_path = r"c:\Users\k024g\source\repos\TD_Engine\Resources\Models\3Dmodel\sword\ken.obj"

with open(obj_path, 'r', encoding='utf-8') as f:
    lines = f.readlines()

out_lines = []
for line in lines:
    if line.startswith('v '):
        parts = line.strip().split()
        if len(parts) >= 4:
            x = float(parts[1])
            y = float(parts[2])
            z = float(parts[3])
            # Rotate 90 degrees around Y axis to spin the blade
            new_x = z
            new_y = y
            new_z = -x
            out_lines.append(f"v {new_x:.6f} {new_y:.6f} {new_z:.6f}\n")
        else:
            out_lines.append(line)
    elif line.startswith('vn '):
        parts = line.strip().split()
        if len(parts) >= 4:
            nx = float(parts[1])
            ny = float(parts[2])
            nz = float(parts[3])
            new_nx = nz
            new_ny = ny
            new_nz = -nx
            out_lines.append(f"vn {new_nx:.6f} {new_ny:.6f} {new_nz:.6f}\n")
        else:
            out_lines.append(line)
    else:
        out_lines.append(line)

with open(obj_path, 'w', encoding='utf-8') as f:
    f.writelines(out_lines)

print("Successfully spun the blade 90 degrees around Y axis.")
