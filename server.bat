@echo off

setlocal

set server=server


if "%1" NEQ "" set server=%1

set Dir=%~dp0%server%\

@ping -n 1 127.0.0.1>nul
start "mediasoupserver" /D %Dir% node  %Dir%server.js 

REM pause