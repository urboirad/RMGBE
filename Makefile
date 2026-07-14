CC      = gcc
CFLAGS  = -O2 -Wall -IC:/MinGW/x86_64-w64-mingw32/include -Iinclude -Ivendor
LDFLAGS = -Llib -lglfw3 -lopengl32 -lgdi32 -lole32 -lcomdlg32 -lshell32 -lm
SRCS    = main.c src/gap_buffer.c src/text_renderer.c src/editor.c src/file_panel.c src/terminal.c
OUT     = output/RMGBE.exe

all:
	$(CC) $(CFLAGS) $(SRCS) -o $(OUT) $(LDFLAGS)
