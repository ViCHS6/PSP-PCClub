#requires -Version 5.1
[CmdletBinding(SupportsShouldProcess)]
param(
    [string]$Drive,
    [string]$Build = 'build\psp',
    [string]$Data = 'build\data.pak',
    [string]$Name = 'PCCLUB'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))

function Resolve-RepoPath {
    param([string]$Path)
    if ([string]::IsNullOrWhiteSpace($Path)) {
        throw 'Путь не должен быть пустым.'
    }
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $repoRoot $Path))
}

# Имя остаётся одним каталогом и не выходит из PSP\GAME.
if ([string]::IsNullOrWhiteSpace($Name) -or $Name -match '[<>:"/\\|?*\x00-\x1f]' -or
    $Name.EndsWith('.') -or $Name.EndsWith(' ') -or
    $Name -match '^(?i:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)') {
    throw 'Name должен быть допустимым именем одного каталога Windows, например PCCLUB.'
}

$eboot = Join-Path (Resolve-RepoPath $Build) 'EBOOT.PBP'
if (-not (Test-Path -LiteralPath $eboot -PathType Leaf)) {
    throw "Не найден обязательный EBOOT.PBP: $eboot. Сначала соберите PSP-версию."
}
$dataPath = Resolve-RepoPath $Data
$hasData = Test-Path -LiteralPath $dataPath -PathType Leaf
if (-not $hasData) {
    Write-Warning "Не найден необязательный data.pak: $dataPath. Копируется только EBOOT.PBP."
}

if ([string]::IsNullOrWhiteSpace($Drive)) {
    $scanned = @()
    $candidates = @()
    foreach ($volume in [System.IO.DriveInfo]::GetDrives()) {
        $scanned += $volume.Name
        try {
            if ($volume.IsReady -and (Test-Path -LiteralPath (Join-Path $volume.Name 'PSP\GAME') -PathType Container)) {
                $candidates += $volume.Name
            }
        }
        catch {
            Write-Verbose "Том $($volume.Name) недоступен: $($_.Exception.Message)"
        }
    }
    $candidates = @($candidates | Sort-Object -Unique)
    if ($candidates.Count -ne 1) {
        $found = '(нет)'
        if ($candidates.Count -gt 0) { $found = $candidates -join ', ' }
        throw "Невозможно выбрать PSP: найдено $($candidates.Count) томов с PSP\GAME. Кандидаты: $found. Проверены: $($scanned -join ', '). Включите USB-соединение или явно задайте -Drive."
    }
    $Drive = $candidates[0]
    Write-Host "Найден накопитель PSP: $Drive"
}

if ($Drive -match '^[A-Za-z]:$') { $Drive += '\' }
if (-not (Test-Path -LiteralPath $Drive -PathType Container)) {
    throw "Накопитель или тестовый каталог не найден: $Drive"
}
$resolvedDrive = Resolve-Path -LiteralPath $Drive
if ($resolvedDrive.Provider.Name -ne 'FileSystem') {
    throw 'Drive должен указывать на диск или каталог файловой системы.'
}
$destination = Join-Path $resolvedDrive.ProviderPath "PSP\GAME\$Name"
if ((Test-Path -LiteralPath $destination) -and
    -not (Test-Path -LiteralPath $destination -PathType Container)) {
    throw "Вместо каталога назначения существует файл: $destination"
}

$files = @([pscustomobject]@{ Source = $eboot; Name = 'EBOOT.PBP' })
if ($hasData) {
    $files += [pscustomobject]@{ Source = $dataPath; Name = 'data.pak' }
}
$copied = 0
$unchanged = 0
$skipped = 0
foreach ($file in $files) {
    $target = Join-Path $destination $file.Name
    $same = $false
    if (Test-Path -LiteralPath $target -PathType Leaf) {
        $sourceInfo = Get-Item -LiteralPath $file.Source
        $targetInfo = Get-Item -LiteralPath $target
        if ($sourceInfo.Length -eq $targetInfo.Length) {
            $same = (Get-FileHash -LiteralPath $file.Source -Algorithm SHA256).Hash -eq
                (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
        }
    }
    elseif (Test-Path -LiteralPath $target) {
        throw "Вместо файла назначения существует каталог: $target"
    }
    if ($same) {
        Write-Host "$($file.Name): без изменений"
        $unchanged++
        continue
    }
    if ($PSCmdlet.ShouldProcess($target, "Копировать $($file.Source); создать каталог при необходимости")) {
        if (-not (Test-Path -LiteralPath $destination -PathType Container)) {
            # CreateDirectory воспринимает квадратные скобки в пути буквально.
            [System.IO.Directory]::CreateDirectory($destination) | Out-Null
        }
        Copy-Item -LiteralPath $file.Source -Destination $target -Force
        Write-Host "$($file.Name): скопирован"
        $copied++
    }
    else {
        $skipped++
    }
}

Write-Host "Итог: скопировано $copied; без изменений $unchanged; пропущено/запланировано $skipped."
Write-Host "Назначение: $destination"
Write-Host 'PSP Go: внутренняя память — ef0:; для подключения по USB включите «USB-соединение».'
