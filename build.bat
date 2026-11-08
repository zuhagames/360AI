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

set SRC=common platform gguf model quant tokenizer weights gpu ops engine xboxui json settings textstream chat assistant assistant_store dashboard mcp server app main
set FLAGS=/c /nologo /O2 /Oi /MT /GR- /EHs-c- /GS- /D_XBOX /D XBOX /D NDEBUG /I src

for %%f in (%SRC%) do (
  echo compiling %%f.cpp
  "%CL_EXE%" %FLAGS% src\%%f.cpp /Fo"%OUT%\%%f.obj" || goto fail
)

echo linking default.xex
"%LINK_EXE%" /nologo /OUT:%OUT%\default.xex /MACHINE:PPCBE /XEX:NO /IGNORE:4089 %OUT%\common.obj %OUT%\platform.obj %OUT%\gguf.obj %OUT%\model.obj %OUT%\quant.obj %OUT%\tokenizer.obj %OUT%\weights.obj %OUT%\gpu.obj %OUT%\ops.obj %OUT%\engine.obj %OUT%\xboxui.obj %OUT%\json.obj %OUT%\settings.obj %OUT%\textstream.obj %OUT%\chat.obj %OUT%\assistant.obj %OUT%\assistant_store.obj %OUT%\dashboard.obj %OUT%\mcp.obj %OUT%\server.obj %OUT%\app.obj %OUT%\main.obj /LIBPATH:"%LIB%" xnet.lib d3d9.lib d3dx9.lib xgraphics.lib xapilib.lib xboxkrnl.lib || goto fail

echo packaging 360AI.xex
"%IXE%\imagexex.exe" /IN:%OUT%\default.xex /OUT:360AI.xex /CONFIG:xex.xml || goto fail

echo build ok
exit /b 0
:fail
echo build failed
exit /b 1
