@echo off
set "QEMU_BIN=C:\Program Files\qemu\qemu-system-x86_64.exe"
if not exist "%QEMU_BIN%" set "QEMU_BIN=qemu-system-x86_64.exe"

cd /d "%~dp0"
echo Starting OLD IgorOS (Beta 2) in QEMU...
"%QEMU_BIN%" -cdrom "build\igorOS_old_beta2.iso" -m 2048 -vga std -serial stdio
