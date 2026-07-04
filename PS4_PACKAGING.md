# PS4 Packaging and Hardware Smoke Runbook

This document records the packaging process for turning the `opengnm`
PS4-target smoke ELF into an installable PS4 package, staging it to the test
console, and validating the result with PS4debug.

## Current Status

The current `opengnm` build can produce:

- `opengnm_link_smoke.elf`: verifies that `libopengnm.a` links against
  OpenOrbis `libkernel`, `libSceGnmDriver`, and `libSceVideoOut`.
- `opengnm_hw_smoke.elf`: a hardware smoke executable that allocates garlic
  direct memory, builds a small draw command buffer, submits it through
  `sceGnmSubmitCommandBuffers`, calls `sceGnmSubmitDone`, and waits for an EOP
  label write.
- `IV0000-OGNM00001_00-OPENGNMHWSMOKE00.pkg`: an installable package wrapping
  the smoke executable as title id `OGNM00001`.

The current hardware result, observed on 2026-07-03, is a full-screen green
status view with a scrolling white bar at the top and a large digit `0`. That
means VideoOut is presenting and the GNM submit/EOP path completed.

Native macOS packaging is verified as of 2026-07-04 with the OpenOrbis v0.5.4
LLVM 18 SDK and Homebrew `llvm@18`:

```sh
./build.sh macos-hardware-pkg
```

Verified package artifact:

```text
IV0000-OGNM00001_00-OPENGNMHWSMOKE00.pkg
SHA-256: f49f68212c21d378689c913610bf49ba8f1f4d8325f3d8c78c31da0ab330e358
```

## Target Configuration Found

The active Codex config does not contain a PS4 host. The project scripts and
prior Codex session history use these defaults:

```sh
PS4_HOST=10.0.1.157
PS4_FTP_PORT=2121
PS4_PKG_DIR=/data/pkg
PS4DEBUG_HOST=10.0.1.157
PS4DEBUG_PYTHON=/Users/bizkut/Downloads/PS5/homebrew/PyPS4debug/.venv/bin/python
```

Existing staging scripts use FTP to upload packages to `/data/pkg` and
optionally use PS4debug to send a notification.

## Prerequisites

Host-side:

- Docker image: `openorbisofficial/toolchain:latest`
- Or native macOS:
  - Homebrew `llvm@18`
  - OpenOrbis v0.5.4 LLVM 18 SDK extracted by
    `../tools/setup_openorbis_llvm18_macos.sh`
  - SDK macOS helpers:
    `bin/macos/create-fself-macos`, `bin/macos/create-gp4`, `bin/macos/PkgTool.Core`
- Repository root: `/Users/bizkut/Downloads/PS5/homebrew/ps4-freegnm`
- OpenOrbis helper source directories, if the Docker image does not provide
  compatible helper binaries:
  - `../OpenOrbis/create-fself`
  - `../OpenOrbis/create-gp4`
- Go toolchain on the host if the helper binaries must be rebuilt.

PS4-side:

- PS4 reachable at `10.0.1.157`.
- FTP server running on port `2121`.
- Package install path `/data/pkg` exists or can be created by FTP.
- PS4debug running if process capture/notification is desired.

## Existing Package Pattern

The known-good local examples use this structure:

```text
sample/
├── eboot.bin
├── pkg.gp4
├── sce_module/
│   ├── libc.prx
│   └── libSceFios2.prx
├── sce_sys/
│   ├── about/right.sprx
│   ├── icon0.png
│   └── param.sfo
└── IV0000-...pkg
```

Reference files:

- `freegnm-examples/videoout-linear/Makefile`
- `tools/build_videoout_linear_pkg.sh`
- `tools/stage_videoout_linear_pkg.sh`

The packaging flow is:

1. Compile the executable for `x86_64-ps4-elf`.
2. Link with OpenOrbis startup objects and `link.x`.
3. Convert the executable to `eboot.bin` with `create-fself`.
4. Copy runtime modules from the SDK.
5. Generate `sce_sys/param.sfo` with `PkgTool.Core`.
6. Generate `pkg.gp4` with `create-gp4`.
7. Build the final `.pkg` with `PkgTool.Core pkg_build`.
8. Validate the package with `PkgTool.Core pkg_validate`.

## opengnm Hardware Smoke Metadata

Use a new title id/content id so it does not collide with `freegnm-examples`.

```make
TITLE=opengnm Hardware Smoke
VERSION=1.00
TITLE_ID=OGNM00001
CONTENT_ID=IV0000-OGNM00001_00-OPENGNMHWSMOKE00
EXE=opengnm_hw_smoke
PKG=$(CONTENT_ID).pkg
```

The title id is intentionally different from the `FGNMxxxxx` examples because
this package validates the `opengnm` Sony-style ABI surface, not the `freegnm`
`gnm*` wrapper API.

## Build Commands Inside the OpenOrbis Container

The current ELF target already works:

```sh
cd /work/opengnm
make hardware-smoke
```

For package generation, the link step should use OpenOrbis startup files and
`link.x`, not the minimal `ld.lld -e main` smoke link:

```sh
ld.lld -o opengnm_hw_smoke \
  tests/hardware_smoke.o libopengnm.a \
  -m elf_x86_64 -pie --script "$OO_PS4_TOOLCHAIN/link.x" \
  --eh-frame-hdr -L"$OO_PS4_TOOLCHAIN/lib" \
  -lc -lkernel -lSceGnmDriver -lSceVideoOut \
  "$OO_PS4_TOOLCHAIN/lib/crt1.o" \
  "$OO_PS4_TOOLCHAIN/lib/crti.o" \
  "$OO_PS4_TOOLCHAIN/lib/crtn.o"
```

Then create `eboot.bin`:

```sh
create-fself \
  -in=opengnm_hw_smoke \
  -out=opengnm_hw_smoke.oelf \
  -eboot=eboot.bin \
  --paid 0x3800000000000011
```

Copy runtime modules:

```sh
mkdir -p sce_module
cp "$OO_PS4_TOOLCHAIN/bin/data/modules/libc.prx" sce_module/
cp "$OO_PS4_TOOLCHAIN/bin/data/modules/libSceFios2.prx" sce_module/
```

For the v0.5.4 LLVM 18 native SDK, the same PRXs live under:

```sh
$OO_PS4_TOOLCHAIN/src/modules/libc.prx
$OO_PS4_TOOLCHAIN/src/modules/libSceFios2.prx
```

`Makefile` exposes this as `RUNTIME_MODULE_DIR`; the native macOS wrapper sets
it to `$OO_PS4_TOOLCHAIN/src/modules`.

Create `sce_sys` files. `icon0.png` and `right.sprx` can be copied from the
existing local smoke example until opengnm has its own assets:

```sh
mkdir -p sce_sys/about
cp /work/freegnm-examples/videoout-linear/sce_sys/icon0.png sce_sys/icon0.png
cp /work/freegnm-examples/videoout-linear/sce_sys/about/right.sprx sce_sys/about/right.sprx
```

Generate `param.sfo`:

```sh
PkgTool.Core sfo_new sce_sys/param.sfo
PkgTool.Core sfo_setentry sce_sys/param.sfo APP_TYPE --type Integer --maxsize 4 --value 1
PkgTool.Core sfo_setentry sce_sys/param.sfo APP_VER --type Utf8 --maxsize 8 --value "1.00"
PkgTool.Core sfo_setentry sce_sys/param.sfo ATTRIBUTE --type Integer --maxsize 4 --value 0
PkgTool.Core sfo_setentry sce_sys/param.sfo CATEGORY --type Utf8 --maxsize 4 --value "gd"
PkgTool.Core sfo_setentry sce_sys/param.sfo CONTENT_ID --type Utf8 --maxsize 48 --value "IV0000-OGNM00001_00-OPENGNMHWSMOKE00"
PkgTool.Core sfo_setentry sce_sys/param.sfo DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
PkgTool.Core sfo_setentry sce_sys/param.sfo SYSTEM_VER --type Integer --maxsize 4 --value 0
PkgTool.Core sfo_setentry sce_sys/param.sfo TITLE --type Utf8 --maxsize 128 --value "opengnm Hardware Smoke"
PkgTool.Core sfo_setentry sce_sys/param.sfo TITLE_ID --type Utf8 --maxsize 12 --value "OGNM00001"
PkgTool.Core sfo_setentry sce_sys/param.sfo VERSION --type Utf8 --maxsize 8 --value "1.00"
```

Generate `pkg.gp4` and build the package:

```sh
PKG_FILES="eboot.bin sce_sys/about/right.sprx sce_sys/param.sfo sce_sys/icon0.png sce_module/libc.prx sce_module/libSceFios2.prx"

create-gp4 \
  -out=pkg.gp4 \
  -content-id=IV0000-OGNM00001_00-OPENGNMHWSMOKE00 \
  -files "$PKG_FILES"

PkgTool.Core pkg_build pkg.gp4 .
PkgTool.Core pkg_validate --verbose IV0000-OGNM00001_00-OPENGNMHWSMOKE00.pkg
```

## Docker Wrapper Shape

The wrapper should follow the existing `tools/build_videoout_linear_pkg.sh`
pattern:

1. Set `ROOT_DIR` to the parent workspace.
2. Use `OPENORBIS_DOCKER_IMAGE`, defaulting to
   `openorbisofficial/toolchain:latest`.
3. Build Linux `create-fself` and `create-gp4` helper binaries from
   `../OpenOrbis` if available.
4. Mount the repository at `/work`.
5. Set `OO_PS4_TOOLCHAIN=/lib/OpenOrbisSDK` inside the container if using the
   existing example scripts, or `/usr/lib/OpenOrbisSDK` if using the current
   `opengnm/build.sh` image path.
6. Run the package make target from `/work/opengnm`.

Use `--platform linux/amd64` for the current Docker image on Apple Silicon to
avoid platform ambiguity.

## Native macOS Wrapper

The native wrapper avoids Docker and Ubuntu package setup:

```sh
cd /Users/bizkut/Downloads/PS5/homebrew/ps4-freegnm/opengnm
./build.sh macos-hardware-pkg
```

It does the following:

1. Locates Homebrew `llvm@18`, or uses `LLVM18_PREFIX`.
2. Locates the OpenOrbis v0.5.4 LLVM 18 SDK, or runs
   `../tools/setup_openorbis_llvm18_macos.sh` if `OO_PS4_TOOLCHAIN` is unset.
3. Writes `config.mak` with Homebrew `clang`, `ld.lld`, and `llvm-ar`.
4. Uses SDK macOS helpers:
   `create-fself-macos`, `create-gp4`, and `PkgTool.Core`.
5. Uses `$OO_PS4_TOOLCHAIN/src/modules` for runtime PRXs.
6. Builds and validates `IV0000-OGNM00001_00-OPENGNMHWSMOKE00.pkg`.

Useful overrides:

```sh
LLVM18_PREFIX=/opt/homebrew/opt/llvm@18
OO_PS4_TOOLCHAIN=/path/to/OpenOrbis/PS4Toolchain
RUNTIME_MODULE_DIR=/path/to/sce_module_prx_dir
```

## Staging the Package to PS4

Upload by FTP:

```sh
ROOT_DIR=/Users/bizkut/Downloads/PS5/homebrew/ps4-freegnm
PKG="$ROOT_DIR/opengnm/IV0000-OGNM00001_00-OPENGNMHWSMOKE00.pkg"
PS4_HOST="${PS4_HOST:-10.0.1.157}"
PS4_FTP_PORT="${PS4_FTP_PORT:-2121}"
PS4_PKG_DIR="${PS4_PKG_DIR:-/data/pkg}"

curl --fail --silent --show-error --ftp-create-dirs \
  -T "$PKG" \
  "ftp://$PS4_HOST:$PS4_FTP_PORT$PS4_PKG_DIR/$(basename "$PKG")"

curl --fail --silent --show-error \
  "ftp://$PS4_HOST:$PS4_FTP_PORT$PS4_PKG_DIR/" | grep -F "$(basename "$PKG")"
```

Optionally notify through PS4debug:

```sh
PS4DEBUG_PYTHON=/Users/bizkut/Downloads/PS5/homebrew/PyPS4debug/.venv/bin/python

"$PS4DEBUG_PYTHON" - "$PS4_HOST" "$PS4_PKG_DIR/$(basename "$PKG")" <<'PY'
import asyncio
import sys
from ps4debug import PS4Debug

async def main() -> None:
    host, path = sys.argv[1], sys.argv[2]
    await PS4Debug(host).notify(f"opengnm hardware smoke PKG staged: {path}")

asyncio.run(main())
PY
```

## Installing and Running

After staging:

1. Install `IV0000-OGNM00001_00-OPENGNMHWSMOKE00.pkg` from the PS4 package
   installer.
2. Launch title id `OGNM00001`.
3. Watch for the console app output:

```text
opengnm hardware smoke passed: dcb_size=<bytes>
```

If output is not visible, use PS4debug process capture after launch:

```sh
/Users/bizkut/Downloads/PS5/homebrew/PyPS4debug/.venv/bin/python \
  /Users/bizkut/Downloads/PS5/homebrew/ps4-freegnm/tools/ps4debug_probe.py \
  --host 10.0.1.157 \
  --wait \
  --title-id OGNM00001 \
  --maps \
  --map-limit 16 \
  --output /tmp/ps4_opengnm_hw_smoke.json
```

## Expected Result

Success:

- Package uploads to `ftp://10.0.1.157:2121/data/pkg/`.
- Package installs.
- Title `OGNM00001` launches.
- The smoke executable stays on a full-screen green status view.
- The scrolling white bar at the top keeps moving.
- The large center digit is `0`, meaning the EOP label was written after
  `sceGnmSubmitCommandBuffers` and `sceGnmSubmitDone`.
- The app logs `opengnm hardware smoke passed`.

Failure status digits:

- `1`: GNM direct-memory allocation failed.
- `2`: `sceGnmSubmitCommandBuffers` failed.
- `3`: `sceGnmSubmitDone` failed.
- `4`: EOP label timeout.
- `5`: GNM direct-memory map failed.

Failure modes to record:

- FTP connection refused or timed out: PS4 FTP server is not running or target IP
  changed.
- Package install fails: inspect `param.sfo`, `CONTENT_ID`, runtime modules, and
  package validation output.
- Launch crashes before submit: inspect OpenOrbis startup/link flags and missing
  runtime modules.
- Submit fails: record `sceGnmSubmitCommandBuffers` return value.
- SubmitDone fails: record `sceGnmSubmitDone` return value.
- EOP timeout: GPU accepted submission but did not write the label; inspect DCB
  content and event packet encoding.

## Commit/Artifact Policy

Do commit:

- Build scripts.
- Makefile package targets.
- Source files.
- This runbook.

Do not commit:

- `eboot.bin`
- `*.oelf`
- `*.pkg`
- Generated `pkg.gp4`
- Generated `sce_sys/param.sfo`
- Copied runtime `.prx` files
