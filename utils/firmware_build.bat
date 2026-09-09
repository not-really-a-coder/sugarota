@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0firmware_build.ps1" %*
