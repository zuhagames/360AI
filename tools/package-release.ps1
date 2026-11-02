param(
  [ValidatePattern('^\d+\.\d+(?:\.\d+)?$')]
  [string]$Version = '1.0'
)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$outputDirectory = Join-Path $projectRoot "build\releases\v$Version"
$stageDirectory = Join-Path $outputDirectory ([Guid]::NewGuid().ToString('N'))
$usbDirectory = Join-Path $stageDirectory '360AI'
$bridgeDirectory = Join-Path $stageDirectory '360AI-PC-Bridge'
New-Item -ItemType Directory -Path $usbDirectory -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $bridgeDirectory 'bridge') -Force | Out-Null

$executable = Join-Path $projectRoot '360AI.xex'
if (-not (Test-Path -LiteralPath $executable)) { throw 'Build 360AI.xex first with build.bat.' }
Copy-Item -LiteralPath $executable -Destination (Join-Path $usbDirectory '360AI.xex')
foreach ($name in @('START_HERE.txt', 'CHANGELOG.txt', 'CREDITS.txt')) {
  Copy-Item -LiteralPath (Join-Path $projectRoot "release\$name") -Destination (Join-Path $usbDirectory $name)
}
foreach ($name in @('cli.mjs', 'mcp-client.mjs', 'mcp.example.json')) {
  Copy-Item -LiteralPath (Join-Path $projectRoot "bridge\$name") -Destination (Join-Path $bridgeDirectory "bridge\$name")
}
Copy-Item -LiteralPath (Join-Path $projectRoot 'release\PC_BRIDGE.txt') -Destination (Join-Path $bridgeDirectory 'START_HERE.txt')
Copy-Item -LiteralPath (Join-Path $projectRoot 'release\CREDITS.txt') -Destination (Join-Path $bridgeDirectory 'CREDITS.txt')

$usbZip = Join-Path $outputDirectory "360AI-$Version-USB.zip"
$bridgeZip = Join-Path $outputDirectory "360AI-$Version-PC-Bridge.zip"
Compress-Archive -LiteralPath $usbDirectory -DestinationPath $usbZip -Force
Compress-Archive -LiteralPath $bridgeDirectory -DestinationPath $bridgeZip -Force
Copy-Item -LiteralPath $executable -Destination (Join-Path $outputDirectory '360AI.xex') -Force

# Verify the USB archive contents and binary identity before publishing it.
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead($usbZip)
try {
  $expected = @('360AI/360AI.xex', '360AI/START_HERE.txt', '360AI/CHANGELOG.txt', '360AI/CREDITS.txt')
  $actual = @($archive.Entries | ForEach-Object { $_.FullName.Replace('\', '/') })
  if (Compare-Object $expected $actual) { throw 'USB archive contents do not match the release manifest.' }
  $entry = $archive.Entries | Where-Object { $_.FullName.Replace('\', '/') -eq '360AI/360AI.xex' }
  $stream = $entry.Open()
  $sha = [Security.Cryptography.SHA256]::Create()
  try { $archiveHash = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '') }
  finally { $stream.Dispose(); $sha.Dispose() }
  if ($archiveHash -ne (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash) {
    throw 'Archived executable does not match the built executable.'
  }
} finally { $archive.Dispose() }

$checksums = foreach ($name in @('360AI.xex', "360AI-$Version-USB.zip", "360AI-$Version-PC-Bridge.zip")) {
  $hash = (Get-FileHash -LiteralPath (Join-Path $outputDirectory $name) -Algorithm SHA256).Hash.ToLowerInvariant()
  "$hash  $name"
}
[IO.File]::WriteAllLines((Join-Path $outputDirectory 'SHA256SUMS.txt'), $checksums, [Text.Encoding]::ASCII)
Get-ChildItem -LiteralPath $outputDirectory -File | Select-Object Name, Length
