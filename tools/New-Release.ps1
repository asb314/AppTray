<#
.SYNOPSIS
	Builds AppTray, runs the tests and packages dist\AppTray-<version>-win-x64.zip (+ .sha256).
	With -Publish it also tags v<version> and creates the GitHub release with both files.
.DESCRIPTION
	The version comes from src\AppTray.Core\Version.h (bump it first). -Publish needs a clean
	working tree that is pushed, and GitHub credentials in Git Credential Manager for the owner
	of the repository named in Version.h (APPTRAY_REPOSITORY).
.EXAMPLE
	pwsh -File tools\New-Release.ps1             # package only, to check the zip
	pwsh -File tools\New-Release.ps1 -Publish    # tag + GitHub release
#>
param([switch]$Publish)

$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$versionHeader = Get-Content (Join-Path $repo 'src\AppTray.Core\Version.h') -Raw
$version = [regex]::Match($versionHeader, 'APPTRAY_VERSION_TEXT\s+"([^"]+)"').Groups[1].Value
$repository = [regex]::Match($versionHeader, 'APPTRAY_REPOSITORY\s+L"([^"]+)"').Groups[1].Value
if (-not $version -or -not $repository) { throw 'Cannot read the version or repository from Version.h' }
$tag = "v$version"
Write-Host "AppTray $version ($repository)"

if ($Publish) {
	if (git -C $repo status --porcelain) { throw 'The working tree has changes: commit them first' }
	git -C $repo fetch --quiet origin
	$upstream = git -C $repo rev-parse '@{u}' 2>$null
	if ($LASTEXITCODE -ne 0) { throw 'The branch has no upstream: push it with -u first' }
	if ((git -C $repo rev-parse HEAD) -ne $upstream) { throw 'HEAD is not pushed: push first' }
	if (git -C $repo tag --list $tag) { throw "Tag $tag already exists: bump Version.h" }
}

# Build and test
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$msbuild = & $vswhere -latest -prerelease -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' |
	Select-Object -First 1
& $msbuild (Join-Path $repo 'AppTray.slnx') /p:Configuration=Release /p:Platform=x64 /m /nologo /v:minimal
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)" }
& (Join-Path $repo 'x64\Release\AppTray.Core.Tests.exe') --gtest_brief=1
if ($LASTEXITCODE -ne 0) { throw "Tests failed ($LASTEXITCODE)" }
$exe = Join-Path $repo 'x64\Release\AppTray.exe'
if ((Get-Item $exe).VersionInfo.ProductVersion -ne $version) { throw 'AppTray.exe does not carry the Version.h version' }

# Package: the exe, the license and the readme
$dist = Join-Path $repo 'dist'
New-Item -ItemType Directory -Force $dist | Out-Null
$zip = Join-Path $dist "AppTray-$version-win-x64.zip"
$staging = Join-Path $dist "AppTray-$version"
Remove-Item $staging -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory $staging | Out-Null
Copy-Item $exe, (Join-Path $repo 'LICENSE'), (Join-Path $repo 'README.md') $staging
Compress-Archive -Path (Join-Path $staging '*') -DestinationPath $zip -Force
Remove-Item $staging -Recurse -Force
$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
$hashFile = "$zip.sha256"
"$hash  $(Split-Path $zip -Leaf)" | Set-Content $hashFile -Encoding ascii
Write-Host "Packaged $zip"
Write-Host "SHA-256  $hash"
if (-not $Publish) { return }

# Tag, then the GitHub release with both files
$owner = $repository.Split('/')[0]
$env:GCM_INTERACTIVE = 'never'; $env:GIT_TERMINAL_PROMPT = '0'
$credential = "protocol=https`nhost=github.com`nusername=$owner`n`n" | git credential fill 2>$null
$token = ($credential | Where-Object { $_ -like 'password=*' }) -replace '^password=', ''
if (-not $token) { throw "No GitHub credential for $owner in Git Credential Manager" }
$headers = @{ Authorization = "Bearer $token"; 'User-Agent' = 'AppTray-release'; Accept = 'application/vnd.github+json' }

$previous = git -C $repo describe --tags --abbrev=0 2>$null
$range = if ($previous) { "$previous..HEAD" } else { 'HEAD' }
# Through a file written and read as UTF-8: PowerShell decodes a native program's output with the
# console's code page (OEM in a console, ANSI without one), never UTF-8, and mangles accents
$changesFile = Join-Path ([IO.Path]::GetTempPath()) "AppTray-changes-$PID.txt"
try {
	git -C $repo -c i18n.logOutputEncoding=UTF-8 log $range --format='- %s' --no-merges --output=$changesFile
	if ($LASTEXITCODE -ne 0) { throw "git log $range failed" }
	$changes = (Get-Content -LiteralPath $changesFile -Encoding utf8) -join "`n"
}
finally {
	Remove-Item -LiteralPath $changesFile -ErrorAction SilentlyContinue
}
$body = @"
Download **AppTray-$version-win-x64.zip**, unzip ``AppTray.exe`` into its own folder inside the tree of apps it should list, and start it. No installer, no runtime.

SHA-256 of the zip: ``$hash``

Changes:
$changes
"@

git -C $repo tag -a $tag -m "AppTray $version"
if ($LASTEXITCODE -ne 0) { throw "Cannot create tag $tag" }
git -C $repo push origin $tag
if ($LASTEXITCODE -ne 0) { git -C $repo tag -d $tag | Out-Null; throw "Cannot push tag $tag" }
$release = $null
try {
	$release = Invoke-RestMethod "https://api.github.com/repos/$repository/releases" -Method Post -Headers $headers `
		-ContentType 'application/json; charset=utf-8' `
		-Body ([Text.Encoding]::UTF8.GetBytes((@{ tag_name = $tag; name = "AppTray $version"; body = $body } | ConvertTo-Json)))
	$uploadBase = $release.upload_url -replace '\{.*\}$', ''
	foreach ($file in $zip, $hashFile) {
		$name = Split-Path $file -Leaf
		$type = if ($file.EndsWith('.zip')) { 'application/zip' } else { 'text/plain' }
		Invoke-RestMethod "$uploadBase`?name=$([uri]::EscapeDataString($name))" -Method Post -Headers $headers `
			-ContentType $type -InFile $file | Out-Null
	}
}
catch {
	# Leave nothing half-published, so the same command can simply run again. If the release
	# cannot be deleted, the tag stays too: a release without its tag would become a draft.
	$releaseGone = $true
	if ($release) {
		try { Invoke-RestMethod "https://api.github.com/repos/$repository/releases/$($release.id)" -Method Delete -Headers $headers }
		catch {
			$releaseGone = $false
			Write-Warning "Delete the half-published release and the tag $tag by hand: $($release.html_url)"
		}
	}
	if ($releaseGone) {
		git -C $repo push --delete origin $tag
		git -C $repo tag -d $tag | Out-Null
	}
	throw
}
Write-Host "Published $($release.html_url)"
