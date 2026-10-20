@echo off
rem Usage: build.cmd [-Config Release^|Debug] [-Clean]  (wrapper around build.ps1)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*
