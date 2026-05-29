$resourcesDir = "C:\Users\k024g\source\repos\TD_Engine\Resources"
$projectDir = "C:\Users\k024g\source\repos\TD_Engine"

# 検索対象の拡張子
$searchExts = @("*.cpp", "*.h", "*.json", "*.prefab", "*.csv", "*.txt")
$searchFiles = Get-ChildItem -Path $projectDir -Include $searchExts -File -Recurse

# ファイルの内容をメモリにキャッシュ (高速化のため)
$fileContents = @()
foreach ($sf in $searchFiles) {
    try {
        $content = [System.IO.File]::ReadAllText($sf.FullName)
        $fileContents += @{ Path = $sf.FullName; Content = $content }
    } catch {
        # ignore read errors
    }
}

$allFiles = Get-ChildItem -Path $resourcesDir -File -Recurse

$unusedFiles = @()
$usedFiles = @()

foreach ($file in $allFiles) {
    $fileName = $file.Name
    $found = $false
    
    foreach ($fc in $fileContents) {
        if ($fc.Content.Contains($fileName)) {
            $found = $true
            break
        }
    }
    
    # 拡張子を除いた名前でも検索（一部のライブラリでは拡張子なしで指定することがあるため）
    if (-not $found) {
        $baseName = $file.BaseName
        # 文字列が短すぎる場合は誤検知の可能性があるので注意だが、安全側に倒す
        if ($baseName.Length -gt 3) {
            foreach ($fc in $fileContents) {
                if ($fc.Content.Contains($baseName)) {
                    $found = $true
                    break
                }
            }
        }
    }

    if ($found) {
        $usedFiles += $file.FullName
    } else {
        $unusedFiles += $file.FullName
    }
}

$unusedFiles | Out-File -FilePath "$projectDir\unused_files.txt"
$usedFiles | Out-File -FilePath "$projectDir\used_files.txt"

Write-Output "Done. Unused: $($unusedFiles.Count), Used: $($usedFiles.Count)"
