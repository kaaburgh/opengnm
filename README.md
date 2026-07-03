# opengnm

opengnm is a clean-rewrite PS4 GNM library providing `sceGnm*` / `sceGpa*` drop-in
API compatibility with the Sony official PS4 SDK.

Any PS4 app or game written against the official SDK headers should compile and link
against opengnm unmodified.

## Features

- **Full `sceGnm*` API surface** — 207 functions matching the Sony SDK ABI
- **OpenOrbis SDK compatible** — builds with the OpenOrbis PS4 toolchain
- **Two backends:**
  - `orbis` — delegates to the official `libSceGnmDriver` firmware
  - `generic` — pure software PM4 emission for host testing (no PS4 needed)
- **Binary-compatible struct layouts** — `_Static_assert` verified sizes
- **Surface computation** — `sceGpa*` (gpuaddr / AddrLib) for RT/texture sizing

## Status

Work in progress. See `OPENGNM_REWRITE_PLAN.md` for the full plan.

## Building

### PS4 (OpenOrbis)

```sh
export OO_PS4_TOOLCHAIN=/path/to/openorbis
cp config.orbis.mak config.mak
make
make install DESTDIR=$OO_PS4_TOOLCHAIN
```

### Host (generic, for testing)

```sh
cp config.generic.mak config.mak
make tests
./testgnm
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
