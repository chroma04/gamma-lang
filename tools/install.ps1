# Installs a prebuilt `gamma` release on Windows (x86_64):
#
#     irm https://github.com/chroma04/gamma-lang/releases/latest/download/install.ps1 | iex
#
# Environment:
#   GAMMA_VERSION   the release to install, such as `v0.1.0` (default: the latest)
#   GAMMA_INSTALL   the installation prefix (default: $HOME\.gamma); the compiler goes to
#                   $GAMMA_INSTALL\bin\gamma.exe, the standard library to $GAMMA_INSTALL\lib\gamma
#   GAMMA_DOWNLOAD  the URL of the directory holding the release archives (default: the
#                   GitHub release of GAMMA_VERSION); an https:// or file: URL
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

if ($env:GAMMA_DOWNLOAD) {
    $base = $env:GAMMA_DOWNLOAD
    # Over plain HTTP the archive and its checksum could both be replaced in transit.
    if ($base -notmatch '^(https://|file:)') { Fail 'GAMMA_DOWNLOAD must be an https:// or file: URL' }
}
elseif ($env:GAMMA_VERSION) { $base = "$repo/releases/download/$($env:GAMMA_VERSION)" }
else { $base = "$repo/releases/latest/download" }

[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

# Downloads $url to $out; only https (and, for local archives, file:) is accepted, also after redirects.
function Fetch($url, $out) {
    if ($url -like 'file:*') {
        Copy-Item -LiteralPath ([Uri]$url).LocalPath -Destination $out
        return
    }
    $resp = Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $out -PassThru
    $final = if ($resp.BaseResponse.ResponseUri) { $resp.BaseResponse.ResponseUri }
             elseif ($resp.BaseResponse.RequestMessage) { $resp.BaseResponse.RequestMessage.RequestUri }
    if ($final -and $final.Scheme -ne 'https') { throw "redirected to a non-https URL: $final" }
}

$work = Join-Path ([IO.Path]::GetTempPath()) ("gamma-install-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
try {
    Write-Host "Downloading $base/$asset"
    try {
        Fetch "$base/$asset" (Join-Path $work $asset)
        Fetch "$base/$asset.sha256" (Join-Path $work "$asset.sha256")
    } catch {
        Fail "cannot download $base/$asset ($($_.Exception.Message))"
    }

    $expected = ((Get-Content (Join-Path $work "$asset.sha256") -Raw).Trim() -split '\s+')[0].ToLower()
    if ($expected -notmatch '^[0-9a-f]{64}$') { Fail "$asset.sha256 does not hold a SHA-256" }
    $actual = (Get-FileHash -Algorithm SHA256 (Join-Path $work $asset)).Hash.ToLower()
    if ($expected -ne $actual) { Fail "the checksum of $asset does not match (expected $expected, got $actual)" }

    $unpacked = Join-Path $work 'unpacked'
    Expand-Archive -Path (Join-Path $work $asset) -DestinationPath $unpacked
    $dir = Get-ChildItem -Path $unpacked -Directory | Where-Object { $_.Name -like 'gamma-*' } | Select-Object -First 1
    if (-not $dir) { Fail "$asset does not hold a gamma directory" }

    if (-not (Test-Path (Join-Path $dir.FullName 'bin\gamma.exe'))) { Fail "$asset does not hold bin\gamma.exe" }

    # Replace the previous installation's files only, leaving anything else in the prefix.
    # The library is copied in full before the old one is swapped out, so a failed copy
    # leaves the previous installation working.
    $docDest = Join-Path $prefix 'share\doc\gamma'
    New-Item -ItemType Directory -Force -Path (Join-Path $prefix 'bin'), (Join-Path $prefix 'lib'), $docDest | Out-Null
    $stdDest = Join-Path $prefix 'lib\gamma'
    $stdNew = Join-Path $prefix 'lib\gamma.new'
    $stdOld = Join-Path $prefix 'lib\gamma.old'
    foreach ($d in $stdNew, $stdOld) { if (Test-Path $d) { Remove-Item -Recurse -Force $d } }
    Copy-Item -Recurse -Force (Join-Path $dir.FullName 'lib\gamma') $stdNew
    if (Test-Path $stdDest) { Rename-Item $stdDest 'gamma.old' }
    try {
        Rename-Item $stdNew 'gamma'
    } catch {
        if (Test-Path $stdOld) { Rename-Item $stdOld 'gamma' }
        throw
    }
    Remove-Item -Recurse -Force $stdOld -ErrorAction SilentlyContinue
    Copy-Item -Force (Join-Path $dir.FullName 'LICENSE'), (Join-Path $dir.FullName 'README.md') $docDest
    $exeNew = Join-Path $prefix 'bin\gamma.exe.new'
    Copy-Item -Force (Join-Path $dir.FullName 'bin\gamma.exe') $exeNew
    Move-Item -Force $exeNew (Join-Path $prefix 'bin\gamma.exe')
} finally {
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}

$bin = Join-Path $prefix 'bin'
Write-Host "Installed gamma to $bin\gamma.exe"
# The user PATH is read and written as stored, so entries such as %USERPROFILE%\bin stay
# unexpanded and the value stays a REG_EXPAND_SZ.
$envKey = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey('Environment')
try {
    $userPath = [string]$envKey.GetValue('Path', '', [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
    $known = $userPath -split ';' | Where-Object { $_ } | ForEach-Object {
        $_.TrimEnd('\'); [Environment]::ExpandEnvironmentVariables($_).TrimEnd('\')
    }
    if ($known -notcontains $bin.TrimEnd('\')) {
        $envKey.SetValue('Path', ($(if ($userPath) { $userPath.TrimEnd(';') + ';' } else { '' }) + $bin),
                         [Microsoft.Win32.RegistryValueKind]::ExpandString)
        # Deleting a variable that does not exist changes nothing, but makes Windows tell
        # running programs (Explorer) that the environment changed.
        [Environment]::SetEnvironmentVariable('GAMMA_ENV_REFRESH', $null, 'User')
        Write-Host "Added $bin to your PATH; open a new terminal to use `gamma`."
    }
} finally {
    $envKey.Close()
}
if (-not (Get-Command gcc -ErrorAction SilentlyContinue) -and -not (Get-Command clang -ErrorAction SilentlyContinue)) {
    Write-Host 'Note: gamma build and --backend=c need a MinGW-w64 C compiler (gcc or clang) on PATH.'
    Write-Host '      For example: winget install MSYS2.MSYS2, then pacman -S mingw-w64-ucrt-x86_64-gcc'
    Write-Host '      and add C:\msys64\ucrt64\bin to PATH.'
}
