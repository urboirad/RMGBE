@echo off
setlocal

set CC=C:\MinGW\bin\gcc.exe
set RC=C:\MinGW\bin\windres.exe
set FLAGS=-Wall -Wextra -Iinclude -Ivendor
set SRC=main.c src\gap_buffer.c src\text_renderer.c src\editor.c src\file_panel.c src\terminal.c src\syntax.c src\theme.c vendor\tinyfiledialogs.c
set LIBS=-Llib -lglfw3 -lopengl32 -lgdi32 -lole32 -lcomdlg32 -lshell32 -lm
set INC=-isystem "C:\MinGW\x86_64-w64-mingw32\include" -isystem "C:\MinGW\lib\gcc\x86_64-w64-mingw32\15.2.0\include"
set OUT=output\RMGBE.exe
set RES=output\resources.o

if not exist output mkdir output

"%RC%" "src\resources.rc" -o "%RES%"

if "%1"=="debug" (
    echo Building Debug...
    "%CC%" -g3 -O0 %FLAGS% %INC% %SRC% "%RES%" -o %OUT% %LIBS%
) else (
    echo Building Release...
    "%CC%" -Os -DNDEBUG -ffunction-sections -fdata-sections -fno-unwind-tables -fno-asynchronous-unwind-tables -nostdinc %FLAGS% %INC% %SRC% "%RES%" -o %OUT% -s "-Wl,--gc-sections" %LIBS%
)

if %errorlevel% equ 0 (echo Done: %OUT%) else (echo Build failed)
