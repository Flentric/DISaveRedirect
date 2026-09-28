@echo off
call "D:\Visual Studio Community\VC\Auxiliary\Build\vcvars32.bat" >nul
cd /d "%~dp0"
cl /nologo /O2 /W3 /LD /MT saveredirect.c /Fe:DISaveRedirect.asi
