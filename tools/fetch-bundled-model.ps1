$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$model = Get-Content -LiteralPath (Join-Path $projectRoot 'release\model.json') -Raw | ConvertFrom-Json
if ([IO.Path]::GetFileName($model.filename) -ne $model.filename -or $model.sha256 -notmatch '^[a-f0-9]{64}$') {
  throw 'Invalid bundled-model manifest.'
}
$directory = Join-Path $projectRoot 'build\models'
New-Item -ItemType Directory -Path $directory -Force | Out-Null
$destination = Join-Path $directory $model.filename
if (Test-Path -LiteralPath $destination) {
  if ((Get-Item -LiteralPath $destination).Length -eq $model.size -and
      (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant() -eq $model.sha256) {
    Write-Output "Model already verified: $destination"
    exit 0
  }
  throw 'Existing cached model does not match the pinned model checksum.'
}
$partial = $destination + '.part'
& curl.exe --fail --location --retry 2 --connect-timeout 20 --max-time 900 --output $partial $model.url
if ($LASTEXITCODE -ne 0) { throw 'Model download failed.' }
if ((Get-Item -LiteralPath $partial).Length -ne $model.size -or
    (Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash.ToLowerInvariant() -ne $model.sha256) {
  throw 'Downloaded model checksum or size mismatch.'
}
Move-Item -LiteralPath $partial -Destination $destination -Force
Write-Output "Downloaded and verified: $destination"
