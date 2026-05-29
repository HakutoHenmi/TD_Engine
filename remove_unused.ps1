$projectDir = "C:\Users\k024g\source\repos\TD_Engine"
$resourcesDir = Join-Path $projectDir "Resources"
$srcExts = "*.cpp","*.h","*.json","*.prefab","*.csv","*.txt"
$srcFiles = Get-ChildItem -Path $projectDir -Include $srcExts -File -Recurse | Where-Object { 
    $_.FullName -notmatch "\\\.git\\" -and $_.FullName -notmatch "\\x64\\" -and $_.FullName -notmatch "\\\.vs\\" -and $_.FullName -notmatch "\\externals\\" 
}
$fileData = @()
foreach ($sf in $srcFiles) {
    try { $fileData += [System.IO.File]::ReadAllText($sf.FullName) } catch { }
}
$allResources = Get-ChildItem -Path $resourcesDir -File -Recurse
$unusedCount = 0
foreach ($file in $allResources) {
    $found = $false
    foreach ($content in $fileData) {
        if ($content.Contains($file.Name)) { $found = $true; break }
    }
    if (-not $found) {
        $baseName = $file.BaseName
        if ($baseName.Length -gt 3) {
            foreach ($content in $fileData) {
                if ($content.Contains($baseName)) { $found = $true; break }
            }
        }
    }
    
    if (-not $found) { 
        Remove-Item -Path $file.FullName -Force
        Write-Host "Removed: $($file.FullName)"
        $unusedCount++ 
    }
}
Write-Host "Total removed: $unusedCount"
