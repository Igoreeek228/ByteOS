@echo off
set "QEMU_BIN=C:\Program Files\qemu\qemu-system-x86_64.exe"
if not exist "%QEMU_BIN%" set "QEMU_BIN=qemu-system-x86_64.exe"

cd /d "%~dp0"
echo Starting ByteOS in QEMU...
"%QEMU_BIN%" -cdrom "build\byteOS.iso" -m 2048 -vga std -audiodev dsound,id=snd0 -device ac97,audiodev=snd0 -serial stdio
