# calbum — Agent Guide

## Project Identity

**calbum** — a high-performance native Windows image gallery built in C17 with
Direct3D 11 / Direct2D.  (Not TaskSlinger — that was a leftover name from an
earlier iteration.  The cache directory is `%LOCALAPPDATA%\calbum\Cache`.)

## Version Management

- Single source of truth: **`VERSION`** file in the repo root
- Current: `0.1.0`
- SemVer 2.0.0 — major version zero (0.y.z) means initial development
- The Makefile reads `VERSION` and passes `-DAPP_VERSION`/`-DAPP_VERSION_W`
- The release workflow reads `VERSION` as fallback when no git tag is provided

## Release Process

```bash
# 1. Update VERSION file (if bumping)
echo "0.2.0" > VERSION

# 2. Commit and tag
git add VERSION
git commit -m "Bump version to 0.2.0"
git tag v0.2.0

# 3. Push — triggers .github/workflows/release.yml
git push origin main
git push origin v0.2.0

# 4. CI builds x64 + arm64, creates a draft release
#    Edit the draft to add real release notes from RELEASE_NOTES.md
```

## Build

```bash
make release      # optimized build (-O2, GUI subsystem)
make run          # build + launch
make debug        # debug build with symbols (-O0 -g)
make test         # 29+ unit tests
make clean        # remove artifacts
```

Cross-compilation (used by CI):
```bash
make release CC=x86_64-w64-mingw32-gcc TARGET=calbum-x64.exe
make release CC=aarch64-w64-mingw32-gcc TARGET=calbum-arm64.exe
```

## Code Style

| Rule | Standard |
|------|----------|
| Language | C17 (`-std=c17`) |
| Indentation | 4 spaces, no tabs |
| Braces | Allman style |
| Column limit | 120 |
| Naming | `snake_case` |
| Module prefixes | `fs_`, `il_`, `gal_`, `r_`, `aw_`, `fm_`, `ui_`, `app_` |

## Architecture

- **Unity build** — `build.c` includes all `.c` files; single translation unit
- **Immediate-mode UI** — everything redrawn every frame
- **Direct3D 11** instanced rendering, **Direct2D** for text
- **WIC** image decoding (background threads)
- **BC1 texture compression** for GPU thumbnails
- **Arena allocator** — zero-fragmentation, two arenas (main + nav)

## Commit Conventions

- Imperative mood, 50-char subject, 72-char wrap body
- Explain the *why* not just the *what*
- Reference modules by prefix (e.g., "renderer", "asset_worker", "release workflow")
