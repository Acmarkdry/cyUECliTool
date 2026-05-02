@echo off
setlocal

set "PLUGIN_ROOT=%~dp0"
set "PY=%PLUGIN_ROOT%Python\.venv\Scripts\python.exe"
set "UE=%PLUGIN_ROOT%Python\ue.py"

if exist "%PY%" (
	"%PY%" "%UE%" %*
) else (
	python "%UE%" %*
)

exit /b %ERRORLEVEL%
