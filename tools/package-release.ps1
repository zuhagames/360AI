param(
  [ValidatePattern('^\d+\.\d+(?:\.\d+)?$')]
  [string]$Version = '1.4'
)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$catalog=Get-Content -LiteralPath (Join-Path $projectRoot 'release/models.json') -Raw | ConvertFrom-Json
$models=@($catalog.models | Where-Object {$_.bundled})
$modelFiles=@($models | ForEach-Object {$_.files})
foreach($model in $modelFiles){
  $modelPath=Join-Path $projectRoot ('build/models/'+$model.filename)
  if(-not (Test-Path -LiteralPath $modelPath)){throw 'Fetch the bundled models first with tools/fetch-bundled-model.ps1.'}
  if((Get-Item -LiteralPath $modelPath).Length -ne $model.size -or (Get-FileHash -LiteralPath $modelPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $model.sha256){throw 'Bundled model checksum/size mismatch.'}
}
$outputDirectory = Join-Path $projectRoot "build\releases\v$Version"
$stageDirectory = Join-Path $outputDirectory ([Guid]::NewGuid().ToString('N'))
$usbDirectory = Join-Path $stageDirectory '360AI'
New-Item -ItemType Directory -Path $usbDirectory -Force | Out-Null

$executable = Join-Path $projectRoot '360AI.xex'
if (-not (Test-Path -LiteralPath $executable)) { throw 'Build 360AI.xex first with build.bat.' }
Copy-Item -LiteralPath $executable -Destination (Join-Path $usbDirectory '360AI.xex')
foreach($model in $modelFiles){Copy-Item -LiteralPath (Join-Path $projectRoot ('build/models/'+$model.filename)) -Destination (Join-Path $usbDirectory $model.filename)}
foreach ($name in @('NOTICE', 'GEMMA_TERMS.txt', 'GEMMA_POLICY.txt', 'MODEL_INFO.txt','APACHE-2.0.txt','SMOLLM2_INFO.txt')) {
  Copy-Item -LiteralPath (Join-Path $projectRoot "release\model-notices\$name") -Destination (Join-Path $usbDirectory $name)
}
foreach ($name in @('START_HERE.txt', 'CHANGELOG.txt', 'CREDITS.txt','MODELS.txt')) {
  Copy-Item -LiteralPath (Join-Path $projectRoot "release\$name") -Destination (Join-Path $usbDirectory $name)
}
Copy-Item -LiteralPath (Join-Path $projectRoot 'release/models.json') -Destination (Join-Path $usbDirectory 'models.json')
Copy-Item -LiteralPath (Join-Path $projectRoot 'tools/fetch-model.ps1') -Destination (Join-Path $usbDirectory 'Download-Models.ps1')

$usbZip = Join-Path $outputDirectory "360AI-$Version-USB.zip"
Compress-Archive -LiteralPath $usbDirectory -DestinationPath $usbZip -Force

# Verify the USB archive contents and binary identity before publishing it.
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead($usbZip)
try {
  $expected = @('360AI/360AI.xex', '360AI/START_HERE.txt', '360AI/CHANGELOG.txt', '360AI/CREDITS.txt')
  $expected += @($modelFiles | ForEach-Object {'360AI/'+$_.filename})
  $expected += @('360AI/NOTICE', '360AI/GEMMA_TERMS.txt', '360AI/GEMMA_POLICY.txt', '360AI/MODEL_INFO.txt','360AI/APACHE-2.0.txt','360AI/SMOLLM2_INFO.txt','360AI/MODELS.txt','360AI/models.json','360AI/Download-Models.ps1')
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
  foreach($model in $modelFiles){
  $modelEntry = $archive.Entries | Where-Object { $_.FullName.Replace('\', '/') -eq ('360AI/' + $model.filename) }
  if ($modelEntry.Length -ne $model.size) { throw 'Archived model size mismatch.' }
  $stream = $modelEntry.Open()
  $sha = [Security.Cryptography.SHA256]::Create()
  try { $modelHash = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
  finally { $stream.Dispose(); $sha.Dispose() }
  if ($modelHash -ne $model.sha256) { throw 'Archived model checksum mismatch.' }
  }
} finally { $archive.Dispose() }

$zipInfo = Get-Item -LiteralPath $usbZip
[PSCustomObject]@{
  Name = $zipInfo.Name
  Length = $zipInfo.Length
  SHA256 = (Get-FileHash -LiteralPath $usbZip -Algorithm SHA256).Hash.ToLowerInvariant()
}
