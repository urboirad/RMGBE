@echo off
setlocal

set CC=C:\MinGW\bin\gcc.exe
set FLAGS=-Wall -Wextra -Iinclude -Ivendor
set SRC=main.c src\gap_buffer.c src\text_renderer.c src\editor.c src\file_panel.c src\terminal.c src\syntax.c src\update.c
set LIBS=-Llib -lglfw3 -lopengl32 -lgdi32 -lole32 -lcomdlg32 -lshell32 -lm
set INC=-isystem "C:\MinGW\x86_64-w64-mingw32\include" -isystem "C:\MinGW\lib\gcc\x86_64-w64-mingw32\15.2.0\include"
set OUT=output\RMGBE.exe

if not exist output mkdir output

if "%1"=="debug" (
    echo Building Debug...
    "%CC%" -g3 -O0 %FLAGS% %INC% %SRC% -o %OUT% %LIBS%
) else (
    echo Building Release...
    "%CC%" -Os -DNDEBUG -ffunction-sections -fdata-sections -fno-unwind-tables -fno-asynchronous-unwind-tables -nostdinc %FLAGS% %INC% %SRC% -o %OUT% -s "-Wl,--gc-sections" %LIBS%
)

if %errorlevel% equ 0 (echo Done: %OUT%) else (echo Build failed)
