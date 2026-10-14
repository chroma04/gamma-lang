# Installs a prebuilt `gamma` release on Windows (x86_64):
#
#     irm https://github.com/chroma04/gamma-lang/releases/latest/download/install.ps1 | iex
#
# Environment:
#   GAMMA_VERSION   the release to install, such as `v0.1.0` (default: the latest)
#   GAMMA_INSTALL   the installation prefix (default: $HOME\.gamma); the compiler goes to
#                   $GAMMA_INSTALL\bin\gamma.exe, the standard library to $GAMMA_INSTALL\lib\gamma
#   GAMMA_DOWNLOAD  the URL of the directory holding the release archives (default: the
#                   GitHub release of GAMMA_VERSION)
#
# The compiler and the interpreter need nothing else. `gamma build`, `gamma run --backend=c`
# and `gamma test --backend=c` need a MinGW-w64 C compiler (gcc or clang) on PATH; MSVC's
# cl.exe is not supported.
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$repo = 'https://github.com/chroma04/gamma-lang'
$prefix = if ($env:GAMMA_INSTALL) { $env:GAMMA_INSTALL } else { Join-Path $HOME '.gamma' }

function Fail($msg) { throw "install.ps1: $msg" }

if (-not [Environment]::Is64BitOperatingSystem) { Fail "no prebuilt gamma for 32-bit Windows; build it from source: $repo" }
$arch = if ($env:PROCESSOR_ARCHITEW6432) { $env:PROCESSOR_ARCHITEW6432 } else { $env:PROCESSOR_ARCHITECTURE }
if ($arch -ne 'AMD64') { Fail "no prebuilt gamma for $arch; build it from source: $repo" }
$asset = 'gamma-windows-x86_64.zip'

if ($env:GAMMA_DOWNLOAD) { $base = $env:GAMMA_DOWNLOAD }
elseif ($env:GAMMA_VERSION) { $base = "$repo/releases/download/$($env:GAMMA_VERSION)" }
else { $base = "$repo/releases/latest/download" }

[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

$work = Join-Path ([IO.Path]::GetTempPath()) ("gamma-install-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
try {
    Write-Host "Downloading $base/$asset"
    try {
        Invoke-WebRequest -UseBasicParsing -Uri "$base/$asset" -OutFile (Join-Path $work $asset)
        Invoke-WebRequest -UseBasicParsing -Uri "$base/$asset.sha256" -OutFile (Join-Path $work "$asset.sha256")
    } catch {
        Fail "cannot download $base/$asset ($($_.Exception.Message))"
    }

    $expected = ((Get-Content (Join-Path $work "$asset.sha256") -Raw).Trim() -split '\s+')[0].ToLower()
    $actual = (Get-FileHash -Algorithm SHA256 (Join-Path $work $asset)).Hash.ToLower()
    if ($expected -ne $actual) { Fail "the checksum of $asset does not match (expected $expected, got $actual)" }

    $unpacked = Join-Path $work 'unpacked'
    Expand-Archive -Path (Join-Path $work $asset) -DestinationPath $unpacked
    $dir = Get-ChildItem -Path $unpacked -Directory | Where-Object { $_.Name -like 'gamma-*' } | Select-Object -First 1
    if (-not $dir) { Fail "$asset does not hold a gamma directory" }

    New-Item -ItemType Directory -Force -Path (Join-Path $prefix 'bin'), (Join-Path $prefix 'lib') | Out-Null
    $stdDest = Join-Path $prefix 'lib\gamma'
    if (Test-Path $stdDest) { Remove-Item -Recurse -Force $stdDest }
    Copy-Item -Recurse -Force (Join-Path $dir.FullName 'lib\gamma') $stdDest
    Copy-Item -Force (Join-Path $dir.FullName 'bin\gamma.exe') (Join-Path $prefix 'bin\gamma.exe')
} finally {
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}

$bin = Join-Path $prefix 'bin'
Write-Host "Installed gamma to $bin\gamma.exe"
$userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
if (($userPath -split ';') -notcontains $bin) {
    [Environment]::SetEnvironmentVariable('Path', ($(if ($userPath) { "$userPath;" } else { '' }) + $bin), 'User')
    Write-Host "Added $bin to your PATH; open a new terminal to use `gamma`."
}
if (-not (Get-Command gcc -ErrorAction SilentlyContinue) -and -not (Get-Command clang -ErrorAction SilentlyContinue)) {
    Write-Host 'Note: gamma build and --backend=c need a MinGW-w64 C compiler (gcc or clang) on PATH.'
    Write-Host '      For example: winget install MSYS2.MSYS2, then pacman -S mingw-w64-ucrt-x86_64-gcc'
    Write-Host '      and add C:\msys64\ucrt64\bin to PATH.'
}
