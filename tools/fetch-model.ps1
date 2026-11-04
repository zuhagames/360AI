param([string]$Model='smollm2-135m',[switch]$Bundled,[switch]$List)
$ErrorActionPreference='Stop'
$portable=Test-Path -LiteralPath (Join-Path $PSScriptRoot 'models.json')
$projectRoot=if($portable){$PSScriptRoot}else{(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path}
$catalogPath=if($portable){Join-Path $projectRoot 'models.json'}else{Join-Path $projectRoot 'release/models.json'}
$catalog=Get-Content -LiteralPath $catalogPath -Raw | ConvertFrom-Json
if($List){
  $catalog.models | Select-Object id,name,mode,recommended_free_gb,description
  exit 0
}
$selected=@($catalog.models | Where-Object {if($Bundled){$_.bundled}else{$_.id -eq $Model}})
if(-not $selected.Count){throw 'Unknown model. Use -List to see model IDs and disk requirements.'}
$directory=if($portable){$projectRoot}else{Join-Path $projectRoot 'build/models'}
New-Item -ItemType Directory -Path $directory -Force | Out-Null
foreach($entry in $selected){
  Write-Output "$($entry.name): reserve $($entry.recommended_free_gb) GB on HDD/USB. $($entry.description)"
  foreach($file in $entry.files){
    if($file.filename -notmatch '^[A-Za-z0-9_.-]+\.gguf$' -or $file.filename.Length -gt 42 -or $file.sha256 -notmatch '^[a-f0-9]{64}$' -or -not $file.url.StartsWith('https://huggingface.co/')){throw 'Invalid model manifest.'}
    $destination=Join-Path $directory $file.filename
    if(Test-Path -LiteralPath $destination){
      if((Get-Item -LiteralPath $destination).Length -eq $file.size -and (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant() -eq $file.sha256){Write-Output "Already verified: $($file.filename)";continue}
      throw "Cached model checksum mismatch: $($file.filename)"
    }
    $partial=$destination+'.part'
    if((Test-Path -LiteralPath $partial) -and (Get-Item -LiteralPath $partial).Length -gt $file.size){throw 'Partial download is larger than the pinned model.'}
    if(-not (Test-Path -LiteralPath $partial) -or (Get-Item -LiteralPath $partial).Length -lt $file.size){
      & curl.exe --fail --location --continue-at - --retry 3 --retry-all-errors --connect-timeout 20 --max-time 3600 --output $partial $file.url
      if($LASTEXITCODE -ne 0){throw 'Model download interrupted. Run the same command to resume.'}
    }
    if((Get-Item -LiteralPath $partial).Length -ne $file.size -or (Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash.ToLowerInvariant() -ne $file.sha256){throw 'Downloaded model size/checksum mismatch.'}
    Move-Item -LiteralPath $partial -Destination $destination
    Write-Output "Verified: $($file.filename)"
  }
}
