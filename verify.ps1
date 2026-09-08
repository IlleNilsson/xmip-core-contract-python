<#
    .SYNOPSIS
    Runs the Python checks, builds the embedding shim as a shared library, and
    runs the C probe against a live interpreter.

    .DESCRIPTION
    The gate xgit runs (Test-XmipSelfVerifyingModule). Needs the `python` and
    `c` prerequisites: CPython with its headers and import library, zig cc for
    the shim. The Microsoft Store alias for python is not an interpreter and is
    skipped.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

function Find-Python {
    if ($env:XMIP_PYTHON -and (Test-Path -LiteralPath $env:XMIP_PYTHON)) { return $env:XMIP_PYTHON }
    foreach ($name in 'python3', 'python') {
        $found = Get-Command $name -ErrorAction SilentlyContinue
        if ($found -and $found.Source -notmatch 'WindowsApps') { return $found.Source }
    }
    $local = Join-Path $env:LOCALAPPDATA 'Programs' 'Python'
    $found = Get-ChildItem -Path $local -Directory -Filter 'Python3*' -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending | Select-Object -First 1
    if ($found) { return (Join-Path $found.FullName 'python.exe') }
    return $null
}

[string] $python = Find-Python
if (-not $python) { Write-Host 'FAILED. No CPython; prerequisite.toml declares python.'; exit 2 }
if (-not (Get-Command zig -ErrorAction SilentlyContinue)) { Write-Host 'FAILED. zig is not installed (prerequisite c).'; exit 2 }
[string] $include = $env:XMIP_ABI_INCLUDE
if (-not $include) { $include = Join-Path $PSScriptRoot '..' '..' '..' 'foundation' 'abi' 'include' }
if (-not (Test-Path -LiteralPath (Join-Path $include 'xmip_module.h'))) {
    Write-Host "FAILED. xmip_module.h not found under $include; set XMIP_ABI_INCLUDE."
    exit 2
}

Write-Host "   python -m unittest  ($python)"
& $python -m unittest discover -s tests -q
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# Where the headers and the library are, asked of the interpreter itself.
[string] $prefix = & $python -c "import sys; print(sys.base_prefix)"
[string] $pyInclude = & $python -c "import sysconfig; print(sysconfig.get_path('include'))"
[string] $version = & $python -c "import sys; print(f'{sys.version_info[0]}{sys.version_info[1]}')"
[string[]] $link = if ($IsWindows) {
    @('-L', (Join-Path $prefix 'libs'), "-lpython$version")
} else {
    (& $python -c "import sysconfig; print(sysconfig.get_config_var('LIBDIR'))" | ForEach-Object { @('-L', $_) }) +
        @("-lpython$($version[0]).$($version.Substring(1))")
}

New-Item -ItemType Directory -Force -Path build | Out-Null
[string] $library = if ($IsWindows) { 'xmip_core_contract_python.dll' }
    elseif ($IsMacOS) { 'libxmip_core_contract_python.dylib' } else { 'libxmip_core_contract_python.so' }
[string] $probe = if ($IsWindows) { 'probe.exe' } else { 'probe' }

Write-Host "   zig cc -shared -> build/$library"
& zig cc -shared -O2 -fvisibility=hidden -Wall -Wextra -Werror -I $include -I $pyInclude `
    shim/xmip_python_shim.c -o (Join-Path build $library) @link
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "   zig cc -> build/$probe"
& zig cc -O1 -Wall -Wextra -Werror -I $include -I $pyInclude `
    shim/xmip_python_shim.c tests/probe.c -o (Join-Path build $probe) @link
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$env:XMIP_PYTHON_PATH = (Resolve-Path python).Path
$env:PYTHONHOME = $prefix
if ($IsWindows) { $env:Path = "$prefix;$env:Path" }
& (Join-Path $PSScriptRoot 'build' $probe)
exit $LASTEXITCODE
