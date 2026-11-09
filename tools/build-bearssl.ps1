param([string]$Compiler,[string]$Librarian,[string]$Output,[switch]$Xbox)
$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Path $Output -Force | Out-Null
$sources=@(Get-ChildItem -LiteralPath third_party/bearssl/src -Recurse -Filter '*.c' | Sort-Object FullName)
$config='/D inline=__inline /D BR_USE_WIN32_RAND=0 /D BR_USE_WIN32_TIME=0 /D BR_USE_UNIX_TIME=0 /D BR_USE_URANDOM=0 /D BR_USE_GETENTROPY=0 /D BR_RDRAND=0 /D BR_AES_X86NI=0 /D BR_SSE2=0 /D BR_POWER8=0 /D BR_64=0'
# Identical portable C crypto on host and big-endian Xenon. No OS RNG/time
# stubs: the HTTPS client supplies cryptographic entropy and validation time.
function FileDigest($Path){
  $hash=[Security.Cryptography.SHA256]::Create();$stream=[IO.File]::OpenRead($Path)
  try {return [BitConverter]::ToString($hash.ComputeHash($stream))} finally {$stream.Dispose();$hash.Dispose()}
}
$hashes=(@($sources)+@(Get-ChildItem third_party/bearssl/inc/*.h)+@(Get-ChildItem third_party/bearssl/src/*.h)) | ForEach-Object {FileDigest $_.FullName}
$identity=$Compiler+$config+($Xbox.IsPresent.ToString())+($hashes -join '')
$sha=[Security.Cryptography.SHA256]::Create()
try {$fingerprint=[BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($identity)))} finally {$sha.Dispose()}
$cache=Join-Path $Output 'fingerprint.txt';$library=Join-Path $Output 'bearssl.lib'
if((Test-Path -LiteralPath $library) -and (Test-Path -LiteralPath $cache) -and (Get-Content -Raw -LiteralPath $cache).Trim() -eq $fingerprint){Write-Output 'BearSSL library unchanged';exit 0}
$flags='/nologo /c /TC /O2 /MT /GS- /I third_party/bearssl/inc /I third_party/bearssl/src '+$config
if($Xbox){$flags+=' /D _XBOX /D XBOX'}
$rsp=Join-Path $Output 'sources.rsp'
($flags+' /Fo"'+$Output+'\\" '+(($sources | ForEach-Object {'"'+$_.FullName+'"'}) -join ' ')) | Set-Content -LiteralPath $rsp -Encoding ASCII
& $Compiler "@$rsp" *> (Join-Path $Output 'compile.log')
if($LASTEXITCODE -ne 0){Get-Content (Join-Path $Output 'compile.log') -Tail 60;throw 'BearSSL compilation failed'}
$objects=@($sources | ForEach-Object {'"'+(Join-Path $Output ($_.BaseName+'.obj'))+'"'})
('/nologo /OUT:"'+$library+'" '+($objects -join ' ')) | Set-Content -LiteralPath (Join-Path $Output 'objects.rsp') -Encoding ASCII
& $Librarian ('@'+(Join-Path $Output 'objects.rsp'))
if($LASTEXITCODE -ne 0){throw 'BearSSL library build failed'}
$fingerprint | Set-Content -LiteralPath $cache -Encoding ASCII
Write-Output 'BearSSL portable TLS library built'
