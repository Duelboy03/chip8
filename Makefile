# CHIP-8 in C++17. `make` builds everything; `make test` runs the checks.
#
# The SDL2 front end needs sdl2 (brew install sdl2). The headless tools do not
# link against SDL at all, so they build even without it.

CXX      ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
SDL_LIBS   := $(shell sdl2-config --libs 2>/dev/null)

.PHONY: all headless test clean run

all: chip8 run_rom tests

# Everything except the windowed emulator; useful on a machine without SDL2.
headless: run_rom tests

chip8: main.cpp chip8.cpp chip8.hpp
ifeq ($(strip $(SDL_LIBS)),)
	@echo "SDL2 not found. Install it with:  brew install sdl2"
	@echo "(or build just the headless tools with: make headless)"
	@exit 1
endif
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) main.cpp chip8.cpp -o $@ $(SDL_LIBS)

run_rom: run_rom.cpp chip8.cpp chip8.hpp
	$(CXX) $(CXXFLAGS) run_rom.cpp chip8.cpp -o $@

tests: tests.cpp chip8.cpp chip8.hpp
	$(CXX) $(CXXFLAGS) tests.cpp chip8.cpp -o $@

test: tests
	./tests

clean:
	rm -f chip8 run_rom tests
