$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'fetch-model.ps1') -Bundled
