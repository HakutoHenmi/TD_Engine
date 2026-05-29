$projectDir = "C:\Users\k024g\source\repos\TD_Engine"
$resourcesDir = Join-Path $projectDir "Resources"

$srcExts = "*.cpp","*.h","*.json","*.prefab","*.csv","*.txt"
$srcFiles = Get-ChildItem -Path $projectDir -Include $srcExts -File -Recurse | Where-Object { 
    $_.FullName -notmatch "\\\.git\\" -and 
    $_.FullName -notmatch "\\x64\\" -and 
    $_.FullName -notmatch "\\\.vs\\" -and 
    $_.FullName -notmatch "\\externals\\" 
}

$fileData = @()
foreach ($sf in $srcFiles) {
    try {
        $content = [System.IO.File]::ReadAllText($sf.FullName, [System.Text.Encoding]::UTF8)
        $fileData += @{ Path = $sf.FullName; Content = $content; Modified = $false; Encoding = [System.Text.Encoding]::UTF8 }
    } catch {
        try {
            $sjis = [System.Text.Encoding]::GetEncoding(932)
            $content = [System.IO.File]::ReadAllText($sf.FullName, $sjis)
            $fileData += @{ Path = $sf.FullName; Content = $content; Modified = $false; Encoding = $sjis }
        } catch { }
    }
}

$standardDirs = @("Models", "Textures", "Audio", "Sound", "Prefabs", "Scenes", "Stages", "UI", "Fonts", "Scripts", "Data", "shaders")
$allResources = Get-ChildItem -Path $resourcesDir -File -Recurse

$fileInfoList = @()

foreach ($file in $allResources) {
    $relPath = $file.FullName.Substring($resourcesDir.Length + 1).Replace('\','/')
    $parts = $relPath.Split('/')
    $topDir = if ($parts.Length -gt 1) { $parts[0] } else { "" }
    $ext = $file.Extension.ToLower()
    
    $newRelPath = $relPath
    
    if ($standardDirs -notcontains $topDir) {
        if ($ext -match "\.(obj|mtl|gltf|glb|bin|fbx|blend)$") { $newRelPath = "Models/" + $relPath }
        elseif ($ext -match "\.(png|jpg|jpeg|tga|hdr|ico)$") { $newRelPath = "Textures/" + $relPath }
        elseif ($ext -match "\.(wav|mp3|ogg)$") { $newRelPath = "Audio/" + $relPath }
        elseif ($ext -match "\.(prefab)$") { $newRelPath = "Prefabs/" + $relPath }
        elseif ($ext -match "\.(json)$") {
            if ($file.Name.ToLower() -match "(scene|stage|pahase)") { $newRelPath = "Scenes/" + $relPath }
            else { $newRelPath = "Data/" + $relPath }
        }
        elseif ($ext -match "\.(csv)$") { $newRelPath = "Data/" + $relPath }
    } else {
        if ($topDir -eq "Sound") {
            $newRelPath = "Audio/" + ($parts[1..($parts.Length-1)] -join "/")
        }
    }
    
    $fileInfoList += @{
        AbsPath = $file.FullName
        OldRelPath = $relPath
        NewRelPath = $newRelPath
        Used = $false
        FileName = $file.Name
    }
}

$sortedInfoList = $fileInfoList | Sort-Object { $_.OldRelPath.Length } -Descending

foreach ($info in $sortedInfoList) {
    $oldPathStr = "Resources/" + $info.OldRelPath
    $oldPathStr2 = "Resources\\" + $info.OldRelPath.Replace('/', '\')
    $newPathStr = "Resources/" + $info.NewRelPath
    
    foreach ($fd in $fileData) {
        if ($fd.Content.Contains($info.FileName)) {
            $info.Used = $true
        }
        
        if ($info.OldRelPath -ne $info.NewRelPath) {
            if ($fd.Content.Contains($oldPathStr)) {
                $fd.Content = $fd.Content.Replace($oldPathStr, $newPathStr)
                $fd.Modified = $true
                $info.Used = $true
            }
            if ($fd.Content.Contains($oldPathStr2)) {
                $fd.Content = $fd.Content.Replace($oldPathStr2, $newPathStr)
                $fd.Modified = $true
                $info.Used = $true
            }
        }
    }
}

foreach ($fd in $fileData) {
    if ($fd.Modified) {
        [System.IO.File]::WriteAllText($fd.Path, $fd.Content, $fd.Encoding)
    }
}

$usedCount = 0
$unusedCount = 0

foreach ($info in $fileInfoList) {
    $oldAbs = $info.AbsPath
    $newAbs = Join-Path $resourcesDir $info.NewRelPath.Replace('/', '\')
    
    if ($info.Used) {
        $usedCount++
        if ($oldAbs -ne $newAbs) {
            $newDir = [System.IO.Path]::GetDirectoryName($newAbs)
            if (-not (Test-Path $newDir)) {
                New-Item -ItemType Directory -Force -Path $newDir | Out-Null
            }
            Move-Item -Path $oldAbs -Destination $newAbs -Force
            Write-Host "Moved: $($info.OldRelPath) -> $($info.NewRelPath)"
        }
    } else {
        $unusedCount++
        try {
            Remove-Item -Path $oldAbs -Force
            Write-Host "Deleted unused: $($info.OldRelPath)"
        } catch { }
    }
}

$dirs = Get-ChildItem -Path $resourcesDir -Directory -Recurse | Sort-Object -Property @{Expression={$_.FullName.Length}; Descending=$true}
foreach ($d in $dirs) {
    if ((Get-ChildItem -Path $d.FullName -Force).Count -eq 0) {
        Remove-Item -Path $d.FullName -Force
    }
}

Write-Host "Done. Used: $usedCount, Deleted: $unusedCount"
