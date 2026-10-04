# 8-Bit Zombies - works on macOS (Homebrew) and Linux
#
# macOS:  brew install pkg-config sdl2 sdl2_image sdl2_mixer
# Ubuntu: sudo apt install build-essential pkg-config libsdl2-dev \
#             libsdl2-image-dev libsdl2-mixer-dev freeglut3-dev

CC      ?= cc
TARGET  = zombies
SRCS    = $(wildcard src/*.c)
OBJS    = $(SRCS:.c=.o)
PKGS    = sdl2 SDL2_image SDL2_mixer

CFLAGS  += -std=gnu99 -Wall -Wextra -O2 -g $(shell pkg-config --cflags $(PKGS))
LDLIBS  += $(shell pkg-config --libs $(PKGS)) -lm

ifeq ($(shell uname -s),Darwin)
  CFLAGS += -DGL_SILENCE_DEPRECATION
  LDLIBS += -framework OpenGL -framework GLUT
else
  LDLIBS += -lglut -lGLU -lGL
endif

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS) $(LDLIBS)

src/%.o: src/%.c src/*.h
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

host: $(TARGET)
	./$(TARGET) host

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all run host clean
