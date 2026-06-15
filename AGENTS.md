## Project Identity

**calbum** — a high-performance native Windows image gallery built in C17 with
Direct3D 11 / Direct2D.  The cache directory is `%LOCALAPPDATA%\calbum\Cache`.

## Version Management

- Single source of truth: **`APP_VERSION` / `APP_VERSION_W`** in [`src/types.h`](src/types.h)
- Current: `"0.1.0"` / `L"0.1.0"`
- SemVer 2.0.0 — major version zero (0.y.z) means initial development
- Override for CI builds: `-DAPP_VERSION='"1.0.0"' -DAPP_VERSION_W='L"1.0.0"'`
- Bump the version in `types.h` before tagging a release

## Commit Conventions

- Imperative mood, 50-char subject, 72-char wrap body
- Explain the *why* not just the *what*
- Reference modules by prefix (e.g., "renderer", "asset_worker", "release workflow")
