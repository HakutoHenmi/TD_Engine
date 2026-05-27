import os

def fix_gear():
    path = "Resources/Models/3Dmodel/gear/gear.obj"
    with open(path, "r") as f:
        lines = f.readlines()
    
    with open(path, "w") as f:
        for line in lines:
            if line.startswith("v "):
                parts = line.split()
                x, y, z = float(parts[1]), float(parts[2]), float(parts[3])
                # Swap Y and Z to make face Z. Map (x, y, z) to (x, z, -y)
                f.write(f"v {x:.6f} {z:.6f} {-y:.6f}\n")
            elif line.startswith("vn "):
                parts = line.split()
                nx, ny, nz = float(parts[1]), float(parts[2]), float(parts[3])
                f.write(f"vn {nx:.6f} {nz:.6f} {-ny:.6f}\n")
            else:
                f.write(line)
    print("Fixed gear.obj")

def fix_pipe():
    path = "Resources/Models/3Dmodel/pipe/pipe1.obj"
    with open(path, "r") as f:
        lines = f.readlines()
    
    with open(path, "w") as f:
        for line in lines:
            if line.startswith("v "):
                parts = line.split()
                x, y, z = float(parts[1]), float(parts[2]), float(parts[3])
                # Center Y (it was around 2.0)
                y -= 2.0
                f.write(f"v {x:.6f} {y:.6f} {z:.6f}\n")
            else:
                f.write(line)
    print("Fixed pipe1.obj")

fix_gear()
fix_pipe()
