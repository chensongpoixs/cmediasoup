@echo off

setlocal


set client=app

if "%1" NEQ "" set client=%1

set Dir=%~dp0%client%\

@ping -n 1 127.0.0.1>nul
start "mediasoupclient" /D %Dir% npm start

REM pause