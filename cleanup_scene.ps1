$scenePath = "Resources/Scenes/tesuto.json"
if (-not (Test-Path $scenePath)) {
    Write-Host "Scene file not found: $scenePath"
    exit
}

Write-Host "Loading scene file: $scenePath"
$jsonContent = Get-Content -Raw -Path $scenePath -Encoding UTF8
$data = ConvertFrom-Json $jsonContent

if (-not $data.objects) {
    Write-Host "No objects found in scene."
    exit
}

$originalCount = $data.objects.Count
$cleanedObjects = [System.Collections.Generic.List[Object]]::new()

# 除外対象の名前
$excludeNames = @("Pipe", "Cannon", "Canon", "BulletTank", "Missile", "Poison", "IceCanon", "SpawnedEnemy", "Bullet", "Contact", "VFX", "HitDistortion", "MirrorShatter", "Explosion")

foreach ($obj in $data.objects) {
    $shouldExclude = $false
    $name = $obj.name

    # Tag コンポーネントをチェック
    if ($obj.components) {
        foreach ($comp in $obj.components) {
            if ($comp.type -eq "Tag" -and ($comp.tag -in @("Pipe", "Canon", "Cannon", "BulletTank", "Missile", "Poison"))) {
                $shouldExclude = $true
                break
            }
        }
    }

    if (-not $shouldExclude) {
        foreach ($exName in $excludeNames) {
            if ($name -like "*$exName*") {
                $shouldExclude = $true
                break
            }
        }
    }

    if (-not $shouldExclude) {
        $cleanedObjects.Add($obj)
    }
}

$data.objects = $cleanedObjects
$newCount = $data.objects.Count

# JSONを整形して保存
$newJson = ConvertTo-Json $data -Depth 100
[System.IO.File]::WriteAllText($scenePath, $newJson, [System.Text.Encoding]::UTF8)

Write-Host "Cleaned up scene successfully!"
Write-Host "Removed $($originalCount - $newCount) zombie entities."
Write-Host "Entity count: $originalCount -> $newCount"
