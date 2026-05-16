# Merge Route_*_Path_* segment meshes into one OBJ per direction.
# Updates tesuto.json to replace ~600 route entities with 6 merged entities.
#
# Usage (from repo root):
#   powershell -ExecutionPolicy Bypass -File Tools\MergeRouteMeshes.ps1
#   powershell -ExecutionPolicy Bypass -File Tools\MergeRouteMeshes.ps1 -WhatIf

param(
    [string]$ScenePath = "Resources/Scenes/tesuto.json",
    [string]$MapDir = "Resources/Scenes/Map",
    [switch]$WhatIf
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $repoRoot

$sceneFile = Join-Path $repoRoot $ScenePath
$mapDirAbs = Join-Path $repoRoot $MapDir

if (-not (Test-Path $sceneFile)) {
    Write-Error "Scene not found: $sceneFile"
}

$routePattern = '^Route_(North|South|East|West|NorthEast|SouthEast)_Path_\d+$'

function Read-ObjMesh([string]$path) {
    $verts = [System.Collections.ArrayList]::new()
    $faces = [System.Collections.Generic.List[int[]]]::new()

    foreach ($line in [System.IO.File]::ReadLines($path)) {
        if ($line.StartsWith('v ')) {
            $parts = $line.Substring(2).Trim().Split([char[]]@(' ', "`t"), [StringSplitOptions]::RemoveEmptyEntries)
            if ($parts.Length -ge 3) {
                [void]$verts.Add([PSCustomObject]@{
                    X = [double]$parts[0]
                    Y = [double]$parts[1]
                    Z = [double]$parts[2]
                })
            }
        }
        elseif ($line.StartsWith('f ')) {
            $idx = [System.Collections.Generic.List[int]]::new()
            foreach ($tok in $line.Substring(2).Trim().Split([char[]]@(' ', "`t"), [StringSplitOptions]::RemoveEmptyEntries)) {
                $vPart = $tok.Split('/')[0]
                $idx.Add([int]$vPart)
            }
            if ($idx.Count -ge 3) {
                $faces.Add($idx.ToArray())
            }
        }
    }

    return @{ Verts = $verts; Faces = $faces }
}

function Get-RotationMatrix([double[]]$euler) {
    # Match Engine: XMMatrixRotationRollPitchYaw(rotate.x, rotate.y, rotate.z)
    $cx = [Math]::Cos($euler[0]); $sx = [Math]::Sin($euler[0])
    $cy = [Math]::Cos($euler[1]); $sy = [Math]::Sin($euler[1])
    $cz = [Math]::Cos($euler[2]); $sz = [Math]::Sin($euler[2])

    # Rx * Ry * Rz (row-major, multiply vector as row on left in DX - use column vectors v' = R*v)
    $m00 = $cy * $cz; $m01 = $cy * $sz; $m02 = -$sy
    $m10 = $sx * $sy * $cz - $cx * $sz; $m11 = $sx * $sy * $sz + $cx * $cz; $m12 = $sx * $cy
    $m20 = $cx * $sy * $cz + $sx * $sz; $m21 = $cx * $sy * $sz - $sx * $cz; $m22 = $cx * $cy

    return @(
        @($m00, $m01, $m02),
        @($m10, $m11, $m12),
        @($m20, $m21, $m22)
    )
}

function Transform-Vertex(
    [double]$vx, [double]$vy, [double]$vz,
    [double]$tx, [double]$ty, [double]$tz,
    [double]$rx, [double]$ry, [double]$rz,
    [double]$sx, [double]$sy, [double]$sz
) {
    $x = $vx * $sx
    $y = $vy * $sy
    $z = $vz * $sz

    if ($rx -ne 0 -or $ry -ne 0 -or $rz -ne 0) {
        $R = Get-RotationMatrix @($rx, $ry, $rz)
        $x = [double]($R[0][0]*$x + $R[0][1]*$y + $R[0][2]*$z)
        $y = [double]($R[1][0]*$x + $R[1][1]*$y + $R[1][2]*$z)
        $z = [double]($R[2][0]*$x + $R[2][1]*$y + $R[2][2]*$z)
    }

    $out = New-Object 'double[]' 3
    $out[0] = 0.0 + $x + $tx
    $out[1] = 0.0 + $y + $ty
    $out[2] = 0.0 + $z + $tz
    return $out
}

function Write-ObjMesh([string]$path, [string]$objectName, $verts, $faces) {
    $sb = [System.Text.StringBuilder]::new()
    [void]$sb.AppendLine("# Merged route mesh - $objectName")
    [void]$sb.AppendLine("o $objectName")
    foreach ($v in $verts) {
        [void]$sb.AppendLine(("v {0:R} {1:R} {2:R}" -f $v[0], $v[1], $v[2]))
    }
    [void]$sb.AppendLine("vn 0 1 0")
    $faceIdx = 0
    foreach ($f in $faces) {
        $faceIdx++
        $parts = @()
        foreach ($vi in $f) {
            $parts += "$vi//$faceIdx"
        }
        [void]$sb.AppendLine("f " + ($parts -join ' '))
    }
    [System.IO.File]::WriteAllText($path, $sb.ToString(), [System.Text.UTF8Encoding]::new($false))
}

function Merge-RouteGroup([string]$direction, $segments) {
    $mergedVerts = [System.Collections.ArrayList]::new()
    $mergedFaces = [System.Collections.Generic.List[int[]]]::new()
    $offset = 0

    foreach ($seg in $segments) {
        $meshPath = Join-Path $mapDirAbs ([System.IO.Path]::GetFileName($seg.ModelPath))
        if (-not (Test-Path $meshPath)) {
            Write-Warning "Missing mesh: $meshPath ($($seg.Name))"
            continue
        }

        $mesh = Read-ObjMesh $meshPath
        foreach ($v in $mesh.Verts) {
            [void]$mergedVerts.Add((Transform-Vertex `
                $v.X $v.Y $v.Z $seg.Tx $seg.Ty $seg.Tz $seg.Rx $seg.Ry $seg.Rz $seg.Sx $seg.Sy $seg.Sz))
        }
        foreach ($f in $mesh.Faces) {
            $newFace = @()
            foreach ($vi in $f) {
                $newFace += $vi + $offset
            }
            $mergedFaces.Add($newFace)
        }
        $offset += $mesh.Verts.Count
    }

    return @{ Verts = $mergedVerts; Faces = $mergedFaces }
}

Write-Host "Loading scene: $sceneFile"
$jsonText = [System.IO.File]::ReadAllText($sceneFile)
$scene = $jsonText | ConvertFrom-Json

$routeObjects = @()
$keepObjects = [System.Collections.Generic.List[object]]::new()
$maxId = 0

foreach ($obj in $scene.objects) {
    if ($obj.id -gt $maxId) { $maxId = $obj.id }
    if ($obj.name -match $routePattern) {
        $meshComp = $null
        foreach ($c in $obj.components) {
            if ($c.type -eq 'MeshRenderer') { $meshComp = $c; break }
        }
        if ($null -eq $meshComp) {
            Write-Warning "Route without MeshRenderer: $($obj.name)"
            $keepObjects.Add($obj)
            continue
        }
        $routeObjects += [PSCustomObject]@{
            Name = $obj.name
            Direction = "Route_$($Matches[1])"
            ModelPath = $meshComp.modelPath
            Tx = [double]$obj.translate[0]
            Ty = [double]$obj.translate[1]
            Tz = [double]$obj.translate[2]
            Rx = [double]$obj.rotate[0]
            Ry = [double]$obj.rotate[1]
            Rz = [double]$obj.rotate[2]
            Sx = [double]$obj.scale[0]
            Sy = [double]$obj.scale[1]
            Sz = [double]$obj.scale[2]
            Tag = ($obj.components | Where-Object { $_.type -eq 'Tag' } | Select-Object -First 1).tag
            ShaderName = $meshComp.shaderName
            Color = $meshComp.color
        }
    }
    else {
        $keepObjects.Add($obj)
    }
}

Write-Host "Found $($routeObjects.Count) route path entities to merge"

$groups = $routeObjects | Group-Object Direction | Sort-Object Name
$newObjects = [System.Collections.Generic.List[object]]::new()

foreach ($group in $groups) {
    $dirName = $group.Name
    $mergedName = "${dirName}_Merged"
    $outObjName = "$mergedName.obj"
    $outPath = Join-Path $mapDirAbs $outObjName
    $modelPath = "$MapDir/$outObjName" -replace '\\', '/'

    Write-Host "  Merging $($group.Count) segments -> $mergedName"
    $merged = Merge-RouteGroup $dirName $group.Group

    if ($merged.Verts.Count -eq 0) {
        Write-Warning "  No geometry for $mergedName - skipping"
        continue
    }

    if (-not $WhatIf) {
        Write-ObjMesh $outPath $mergedName $merged.Verts $merged.Faces
    }

    $sample = $group.Group[0]
    $tag = if ($sample.Tag) { $sample.Tag } else { 'Default' }

    $maxId++
    $newObj = [ordered]@{
        id = $maxId
        parentId = 4294967295
        name = $mergedName
        locked = $false
        translate = @(0, 0, 0)
        rotate = @(0, 0, 0)
        scale = @(1, 1, 1)
        components = @(
            [ordered]@{
                type = 'MeshRenderer'
                enabled = $true
                modelPath = $modelPath
                texturePath = ''
                shaderName = $sample.ShaderName
                color = @($sample.Color[0], $sample.Color[1], $sample.Color[2], $sample.Color[3])
                uvTiling = @(1, 1)
                uvOffset = @(0, 0)
            },
            [ordered]@{
                type = 'Tag'
                enabled = $true
                tag = $tag
            }
        )
    }
    $newObjects.Add($newObj)
}

$finalObjects = @($keepObjects) + @($newObjects)
$scene.objects = $finalObjects

Write-Host ""
Write-Host "Scene objects: $($routeObjects.Count) route segments removed, $($newObjects.Count) merged routes added"
Write-Host "  Before: $($routeObjects.Count + $keepObjects.Count) -> After: $($finalObjects.Count)"
Write-Host "  MeshRenderer reduction: ~$($routeObjects.Count - $newObjects.Count) entities"

if ($WhatIf) {
    Write-Host "[WhatIf] No files written."
    exit 0
}

$backup = "$sceneFile.bak"
if (-not (Test-Path $backup)) {
    Copy-Item $sceneFile $backup
    Write-Host "Backup created: $backup"
}

# Preserve readable formatting similar to original
$output = $scene | ConvertTo-Json -Depth 20
# ConvertFrom-Json loses some formatting; acceptable for functional merge
[System.IO.File]::WriteAllText($sceneFile, $output, [System.Text.UTF8Encoding]::new($false))
Write-Host "Updated: $sceneFile"
Write-Host "Done. Rebuild and load tesuto.json to verify draw counts."
