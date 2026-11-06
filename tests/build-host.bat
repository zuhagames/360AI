@echo off
setlocal
set "VC=C:\Program Files (x86)\Microsoft Visual Studio 10.0\VC"
set "WINSDK=C:\Program Files (x86)\Microsoft SDKs\Windows\v7.0A"
set "PATH=%VC%\bin;C:\Program Files (x86)\Microsoft Visual Studio 10.0\Common7\IDE;%PATH%"
set "INCLUDE=%VC%\include;%WINSDK%\Include"
set "LIB=%VC%\lib;%WINSDK%\Lib"
if not exist build\tests mkdir build\tests
set SRC=src\common.cpp src\platform.cpp src\gguf.cpp src\model.cpp src\quant.cpp src\tokenizer.cpp src\weights.cpp src\gpu.cpp src\ops.cpp src\engine.cpp src\xboxui.cpp src\json.cpp src\settings.cpp src\textstream.cpp src\mcp.cpp src\server.cpp src\app.cpp tests\native.cpp
"%VC%\bin\cl.exe" /nologo /EHsc /MT /O2 /D GX_NO_VMX /I src %SRC% /Fo"build\tests\\" /Fe"build\tests\native.exe" /link ws2_32.lib
if errorlevel 1 exit /b 1
build\tests\native.exe
