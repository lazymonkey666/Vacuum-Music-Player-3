param(
    [string]$ProjectDir,
    [string]$ProjectName,
    [string]$Configuration
)
if ($Configuration -ne "Release") {
    Write-Host "当前配置: $Configuration - 跳过版本号递增"
    exit 0
}

if (-not $ProjectDir) { $ProjectDir = (Get-Location).Path }
if (-not $ProjectName) { $ProjectName = (Split-Path $ProjectDir -Leaf) }

$rcPath = Join-Path $ProjectDir "Resource.rc"
if (-not (Test-Path $rcPath)) {
    Write-Error "找不到 .rc 文件: $rcPath"
    exit 1
}

$configPath = Join-Path $ProjectDir "ver.config"
if (-not (Test-Path $configPath)) {
    # 初始配置
    @("MAJOR=1", "LAST_MONTH=0", "BUILD_COUNT=0") | Out-File $configPath
}

$ini = Get-Content $configPath
$major = [int]($ini | Select-String "MAJOR=").Line.Split('=')[1]
$lastMon = [int]($ini | Select-String "LAST_MONTH=").Line.Split('=')[1]
$build = [int]($ini | Select-String "BUILD_COUNT=").Line.Split('=')[1]

$now = Get-Date
$yy = $now.Year % 100
$mm = $now.Month

if ($mm -ne $lastMon) {
    $build = 0
    $lastMon = $mm
}
$build++

@("MAJOR=$major", "LAST_MONTH=$lastMon", "BUILD_COUNT=$build") | Out-File $configPath

$verNum = "$major,$yy,$mm,$build"
$verStr = "$major.$yy.$mm.$build"

$content = Get-Content $rcPath -Raw
$content = $content -replace "FILEVERSION \d+,\d+,\d+,\d+", "FILEVERSION $verNum"
$content = $content -replace "PRODUCTVERSION \d+,\d+,\d+,\d+", "PRODUCTVERSION $verNum"
$content = $content -replace 'VALUE "FileVersion", "[^"]+"', "VALUE `"FileVersion`", `"$verStr`""
$content = $content -replace 'VALUE "ProductVersion", "[^"]+"', "VALUE `"ProductVersion`", `"$verStr`""

# 移除只读属性
Set-ItemProperty $rcPath -Name IsReadOnly -Value $false -ErrorAction SilentlyContinue
$content | Out-File $rcPath -Encoding Default -NoNewline

exit 0