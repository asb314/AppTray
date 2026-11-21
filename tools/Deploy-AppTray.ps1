<#
.SYNOPSIS
	Builds AppTray (Release x64), runs the unit tests and installs AppTray.exe into a folder.
.DESCRIPTION
	A running copy in the destination is closed first (AppTray.exe --exit) so the exe can be
	replaced. The data\ folder next to it (catalog.json, user.json, log) is never touched.
.PARAMETER Destination
	Install folder, inside the tree AppTray should list (its parent becomes the root).
.PARAMETER SkipTests
	Do not run AppTray.Core.Tests.exe.
.PARAMETER Start
	Start the installed AppTray afterwards.
.EXAMPLE
	pwsh -File tools\Deploy-AppTray.ps1 -Destination C:\Applications\AppTray -Start
#>
param(
	[Parameter(Mandatory)][string]$Destination,
	[switch]$SkipTests,
	[switch]$Start
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$msbuild = & $vswhere -latest -prerelease -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' |
	Select-Object -First 1
if (-not $msbuild) { throw 'MSBuild not found (Visual Studio 2026 with C++ is required)' }

& $msbuild (Join-Path $repo 'AppTray.slnx') /p:Configuration=Release /p:Platform=x64 /m /nologo /v:minimal
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)" }

if (-not $SkipTests) {
	& (Join-Path $repo 'x64\Release\AppTray.Core.Tests.exe') --gtest_brief=1
	if ($LASTEXITCODE -ne 0) { throw "Tests failed ($LASTEXITCODE)" }
}

# Full path, so a relative -Destination still matches the running process
$target = [IO.Path]::GetFullPath((Join-Path $Destination 'AppTray.exe'))
if (Test-Path $target) {
	$running = @(Get-Process AppTray -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $target })
	if ($running.Count -gt 0) {
		Write-Host 'Closing the running AppTray...'
		Start-Process $target -ArgumentList '--exit' -Wait
		$running | Wait-Process -Timeout 10 -ErrorAction SilentlyContinue
		if (@($running | Where-Object { Get-Process -Id $_.Id -ErrorAction SilentlyContinue }).Count -gt 0) {
			throw 'AppTray did not exit; close it and retry'
		}
	}
}

New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
Copy-Item (Join-Path $repo 'x64\Release\AppTray.exe') $target -Force
$version = (Get-Item $target).VersionInfo.ProductVersion
Write-Host "Installed AppTray $version -> $target"

if ($Start) {
	Start-Process $target
	Write-Host 'Started.'
}
