@echo off
setlocal
set SDK=C:\Program Files (x86)\Microsoft Xbox 360 SDK
set CL_EXE=%SDK%\TechPreview\Jul12Compiler\bin\win32\cl.exe
set LINK_EXE=%SDK%\TechPreview\Jul12Compiler\bin\win32\link.exe
set IXE=%SDK%\bin\win32
set INCLUDE=%SDK%\include\xbox;%SDK%\include\xbox\mmc
set LIB=%SDK%\lib\xbox
set OUT=build
if not exist %OUT% mkdir %OUT%
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build-bearssl.ps1 -Compiler "%CL_EXE%" -Librarian "%SDK%\TechPreview\Jul12Compiler\bin\win32\lib.exe" -Output build\tls-xbox -Xbox || goto fail

set SRC=common platform network gguf model quant tokenizer weights gpu ops engine xboxui json settings textstream chat assistant assistant_store https updater dashboard mcp server app main
set FLAGS=/c /nologo /O2 /Oi /MT /GR- /EHs-c- /GS- /D_XBOX /D XBOX /D NDEBUG /I src /I third_party\bearssl\inc

for %%f in (%SRC%) do (
  echo compiling %%f.cpp
  "%CL_EXE%" %FLAGS% src\%%f.cpp /Fo"%OUT%\%%f.obj" || goto fail
)

echo linking default.xex
set OBJECTS=%OUT%\common.obj %OUT%\platform.obj %OUT%\network.obj %OUT%\gguf.obj %OUT%\model.obj %OUT%\quant.obj %OUT%\tokenizer.obj %OUT%\weights.obj %OUT%\gpu.obj %OUT%\ops.obj %OUT%\engine.obj %OUT%\xboxui.obj %OUT%\json.obj %OUT%\settings.obj %OUT%\textstream.obj %OUT%\chat.obj %OUT%\assistant.obj %OUT%\assistant_store.obj %OUT%\https.obj %OUT%\updater.obj %OUT%\dashboard.obj %OUT%\mcp.obj %OUT%\server.obj %OUT%\app.obj
set LIBRARIES=build\tls-xbox\bearssl.lib xnet.lib d3d9.lib d3dx9.lib xgraphics.lib xapilib.lib xboxkrnl.lib
"%LINK_EXE%" /nologo /OUT:%OUT%\default.xex /MACHINE:PPCBE /XEX:NO /IGNORE:4089 %OBJECTS% %OUT%\main.obj /LIBPATH:"%LIB%" %LIBRARIES% || goto fail

echo packaging 360AI.xex
"%IXE%\imagexex.exe" /IN:%OUT%\default.xex /OUT:360AI.xex /CONFIG:xex.xml || goto fail
echo building update installer
"%CL_EXE%" %FLAGS% /D GX_UPDATE_HELPER src\main.cpp /Fo"%OUT%\update_main.obj" || goto fail
"%LINK_EXE%" /nologo /OUT:%OUT%\updater.exe /MACHINE:PPCBE /XEX:NO /IGNORE:4089 %OBJECTS% %OUT%\update_main.obj /LIBPATH:"%LIB%" %LIBRARIES% || goto fail
"%IXE%\imagexex.exe" /IN:%OUT%\updater.exe /OUT:360AI-updater.xex /CONFIG:xex.xml || goto fail

echo build ok
exit /b 0
:fail
echo build failed
exit /b 1
