param([string]$VsInstallPath, [string]$WorkDirectory)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$PSNativeCommandUseErrorActionPreference = $false
if (!$WorkDirectory) {
    $WorkDirectory = Join-Path $env:TEMP ('IPRegionMon-concurrency-' + [Guid]::NewGuid().ToString('N'))
}
$WorkDirectory = [IO.Path]::GetFullPath($WorkDirectory)
if (Test-Path -LiteralPath $WorkDirectory) { throw 'Choose a new output directory.' }
New-Item -ItemType Directory -Path $WorkDirectory | Out-Null
if (!$VsInstallPath) {
    $installer = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer'
    $VsInstallPath = & (Join-Path $installer 'vswhere.exe') -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
$devCmd = Join-Path $VsInstallPath 'Common7\Tools\VsDevCmd.bat'
$installer = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer'
$build = @"
@echo off
setlocal
set "PATH=$installer;%PATH%"
call "$devCmd" -no_logo -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%
pushd "%~dp0"
cl /nologo /W4 /WX /EHsc /std:c++17 /MD /utf-8 /LD "$PSScriptRoot\module_fixture.cpp" /Femodule_fixture.dll
if errorlevel 1 exit /b %errorlevel%
cl /nologo /W4 /WX /EHsc /std:c++17 /MD /utf-8 "$PSScriptRoot\module_host.cpp" /Femodule_host.exe
exit /b %errorlevel%
"@
$buildPath = Join-Path $WorkDirectory 'build.cmd'
Set-Content -LiteralPath $buildPath -Value $build -Encoding ASCII
& $env:ComSpec /d /v:off /c $buildPath
if ($LASTEXITCODE -ne 0) { throw 'Native test compilation failed.' }
& (Join-Path $WorkDirectory 'module_host.exe') (Join-Path $WorkDirectory 'module_fixture.dll')
if ($LASTEXITCODE -ne 0) { throw "Module lifetime test failed: $LASTEXITCODE" }
Write-Host "Artifacts: $WorkDirectory"
exit 0
