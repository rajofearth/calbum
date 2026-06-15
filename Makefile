CC      ?= gcc
LDFLAGS = -lgdi32 -lshell32 -luser32 -lkernel32 -lole32 -luuid -ld3d11 -ldxguid -lwindowscodecs -ld3dcompiler -ldwmapi -ld2d1 -ldwrite

# Read version from VERSION file
APP_VERSION := $(shell cat VERSION)

CFLAGS_RELEASE = -std=c17 -O2 -mwindows -Wall -Wextra \
	-DAPP_VERSION='"$(APP_VERSION)"' -DAPP_VERSION_W='L"$(APP_VERSION)"'
CFLAGS_DEBUG   = -std=c17 -O0 -mwindows -Wall -Wextra -Wpedantic -g \
	-DAPP_VERSION='"$(APP_VERSION)"' -DAPP_VERSION_W='L"$(APP_VERSION)"'

BUILD    = build.c
TARGET   = calbum.exe
TARGET_DEBUG = calbum_debug.exe

.PHONY: all release debug dev run clean test format lint lint-full lint-fix size

all: test release

release:
	$(CC) $(BUILD) -o $(TARGET) $(CFLAGS_RELEASE) $(LDFLAGS)

debug:
	$(CC) $(BUILD) -o $(TARGET_DEBUG) $(CFLAGS_DEBUG) $(LDFLAGS)

run: release
	./$(TARGET)

clean:
	rm -f $(TARGET) $(TARGET_DEBUG)
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
	@echo "=== clang-tidy: warning summary ==="
	@clang-tidy src/*.c -- -I. 2>&1 \
	  | grep -oE '\[[^]]+\]$$' \
	  | sort \
	  | uniq -c \
	  | sort -rn \
	  | awk '{printf "  %4d  %s\n", $$1, $$2}'
	@total=$$(clang-tidy src/*.c -- -I. 2>&1 | grep -cE '\[[^]]+\]$$' 2>/dev/null || true); \
	 echo "-----------------------"; \
	 echo "  $$total total warnings"; \
	 if [ "$$total" -eq 0 ]; then echo "  ✨ Clean!"; fi

lint-full:
	clang-tidy src/*.c -- -I.

lint-fix:
	@echo "=== Auto-fixing mechanical warnings ==="
	@clang-tidy --fix-errors \
	  --checks="-*,readability-uppercase-literal-suffix,readability-math-missing-parentheses,readability-isolate-declaration,readability-braces-around-statements,readability-redundant-declaration,readability-else-after-return,readability-avoid-nested-conditional-operator,bugprone-implicit-widening-of-multiplication-result" \
	  src/*.c -- -I. 2>&1 \
	  | grep -E "applied [0-9]+ of|^clang-tidy applied" || true
	@echo "---"
	@echo "Run 'make lint' to check remaining warnings."

size: release
	@echo "Binary:"; ls -lh $(TARGET)
	@echo "Source:"; wc -l src/*.c src/*.h build.c
