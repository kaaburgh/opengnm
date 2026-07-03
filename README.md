# opengnm

opengnm is a clean-rewrite PS4 GNM library providing `sceGnm*` / `sceGpa*` drop-in
API compatibility with the Sony official PS4 SDK.

Any PS4 app or game written against the official SDK headers should compile and link
against opengnm unmodified.

## Features

- **Full `sceGnm*` API surface** — 207 functions matching the Sony SDK ABI
- **OpenOrbis SDK compatible** — builds with the OpenOrbis PS4 toolchain
- **Two backends:**
  - `orbis` — delegates to the official `libSceGnmDriver` firmware (74 real
    externs + 14 `sceGnmDriver*` forwarding wrappers + 172 retail stubs +
    11 validate stubs)
  - `generic` — pure software PM4 emission for host testing (no PS4 needed,
    14 PM4 packet builders + real `sceGnm*` + 172 stubs + 11 validate stubs)
- **Binary-compatible struct layouts** — `_Static_assert` verified sizes
- **Surface computation** — `sceGpa*` (gpuaddr / AddrLib) for RT/texture sizing
- **GCN assembler** — fetch shader generation
- **PM4 encoding** — command buffer packet building

## Status

Phases 1-4 and Phase 5A are complete. All 207+ `sceGnm*` functions are
implemented across both backends, and the generic host backend passes 50 tests
via CMake/CTest and Makefile. Remaining gates are OpenOrbis/orbis link testing,
PS4 hardware smoke testing, Eden linkage, and example compilation.

See `OPENGNM_REWRITE_PLAN.md` for the full plan and progress.

## Building

### Host (generic, for testing)

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DOPENGNM_PLATFORM=generic
cmake --build build
```

Or with Make:

```sh
cp config.generic.mak config.mak
make
```

### PS4 (OpenOrbis)

```sh
export OO_PS4_TOOLCHAIN=/path/to/openorbis
cp config.orbis.mak config.mak
make
make install DESTDIR=$OO_PS4_TOOLCHAIN
```

### Docker

```sh
./build.sh all      # build lib + tests in OpenOrbis Docker
./build.sh tests    # build + run host tests
```

## License

MIT, see [COPYING](COPYING).

## Sources

- Clean rewrite, based on the PS4 SDK ABI and AMD public documentation
- Sony ABI reference: `shadPS4/src/core/libraries/gnmdriver/`
- Surface computation: AMD PAL / Mesa AddrLib
