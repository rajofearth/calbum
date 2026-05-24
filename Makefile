CC      ?= gcc
LDFLAGS = -lgdi32 -lshell32 -luser32 -lkernel32 -lole32 -luuid -ld3d11 -ldxguid -lwindowscodecs -ld3dcompiler -ldwmapi
CFLAGS_RELEASE = -O2 -mwindows -Wall -Wextra
CFLAGS_DEBUG   = -O0 -mwindows -Wall -Wextra -Wpedantic -g

BUILD    = build.c
TARGET   = calbum.exe

.PHONY: all release debug dev run clean test format lint size

all: test release

release:
	$(CC) $(BUILD) -o $(TARGET) $(CFLAGS_RELEASE) $(LDFLAGS)

debug:
	$(CC) $(BUILD) -o $(TARGET) $(CFLAGS_DEBUG) $(LDFLAGS)

run: release
	./$(TARGET)

clean:
	rm -f $(TARGET)
	rm -rf tests/build/

test:
	mkdir -p tests/build
	$(CC) tests/test_main.c -o tests/build/test_runner \
		-g -O0 -Wall -Wextra \
		$(LDFLAGS) -lshlwapi \
		-I.
	./tests/build/test_runner

format:
	clang-format -i src/*.c src/*.h build.c tests/test_main.c

lint:
	clang-tidy src/*.c -- -I.

size: release
	@echo "Binary:"; ls -lh $(TARGET)
	@echo "Source:"; wc -l src/*.c src/*.h build.c
