<#
    .SYNOPSIS
    Runs the Python checks, builds the embedding shim as a shared library, and
    runs the C probe against a live interpreter.

    .DESCRIPTION
    The gate xgit runs (Test-XmipSelfVerifyingModule). Needs the `python` and
    `c` prerequisites: CPython with its headers and import library, zig cc for
    the shim. The Microsoft Store alias for python is not an interpreter and is
    skipped. The probe and the shim's build are the capability's, shared by
    every language technology (ADR-0044): probe/verify.ps1 beside this
    repository's mount in the estate, or where XMIP_CONTRACT_PROBE points.
#>
[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

function Find-Python {
    [CmdletBinding()]
    [OutputType([string])]
    param()

    if ($env:XMIP_PYTHON -and (Test-Path -LiteralPath $env:XMIP_PYTHON)) {
        return $env:XMIP_PYTHON
    }
    foreach ($name in 'python3', 'python') {
        $found = Get-Command $name -ErrorAction SilentlyContinue
        if ($found -and $found.Source -notmatch 'WindowsApps') {
            return $found.Source
        }
    }
    [hashtable] $installed = @{
        Path        = Join-Path $env:LOCALAPPDATA 'Programs' 'Python'
        Directory   = $true
        Filter      = 'Python3*'
        ErrorAction = 'SilentlyContinue'
    }
    $found = Get-ChildItem @installed |
        Sort-Object Name -Descending |
        Select-Object -First 1
    if ($found) {
        return (Join-Path $found.FullName 'python.exe')
    }
    return $null
}

[string] $python = Find-Python
if ([string]::IsNullOrWhiteSpace($python)) {
    Write-Host 'FAILED. No CPython; prerequisite.toml declares python.'
    exit 2
}
[string] $probe = $env:XMIP_CONTRACT_PROBE
if ([string]::IsNullOrWhiteSpace($probe)) {
    $probe = Join-Path $PSScriptRoot '..' 'probe'
}
[string] $verify = Join-Path $probe 'verify.ps1'
if (-not (Test-Path -LiteralPath $verify)) {
    Write-Host "FAILED. The capability's probe is not at $probe; set XMIP_CONTRACT_PROBE."
    exit 2
}

Write-Host "   python -m unittest  ($python)"
& $python -m unittest discover -s tests -q
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

# Where the headers and the library are, asked of the interpreter itself.
[string] $prefix = & $python -c 'import sys; print(sys.base_prefix)'
[string] $pyInclude = & $python -c 'import sysconfig; print(sysconfig.get_path("include"))'
[string] $version = & $python -c 'import sys; print(f"{sys.version_info[0]}{sys.version_info[1]}")'
[string[]] $link = @()
if ($IsWindows) {
    $link = @('-L', (Join-Path $prefix 'libs'), "-lpython$version")
}
else {
    [string] $libDir = & $python -c 'import sysconfig; print(sysconfig.get_config_var("LIBDIR"))'
    $link = @('-L', $libDir, "-lpython$($version[0]).$($version.Substring(1))")
}

# The probe embeds the interpreter through the shim, from these.
$env:XMIP_PYTHON_PATH = (Resolve-Path python).Path
$env:PYTHONHOME = $prefix
if ($IsWindows) {
    $env:Path = "$prefix;$env:Path"
}

[hashtable] $build = @{
    Directory = $PSScriptRoot
    Compiler  = 'cc'
    Source    = @('shim/xmip_python_shim.c')
    Standard  = 'python'
    Include   = @($pyInclude)
    Link      = $link
}
& $verify @build
exit $LASTEXITCODE
