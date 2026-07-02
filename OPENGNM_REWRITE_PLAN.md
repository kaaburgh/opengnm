# opengnm — Clean Rewrite Plan

> **Goal:** A clean-rewrite PS4 GNM library providing `sceGnm*` / `sceGpa*` drop-in API
> compatibility with the Sony official PS4 SDK. Any PS4 app or game written against the
> official SDK headers should compile and link against opengnm unmodified. Usable via
> OpenOrbis on PS4, with a generic host backend for testing.
>
> **Source:** Forked from `freegnm` (clean git history — no prior commits).
> **Sony ABI reference:** `shadPS4/src/core/libraries/gnmdriver/` (207 `sceGnm*` decls).

---

## OpenOrbis SDK Compatibility (binding constraints)

opengnm must build and link against the OpenOrbis PS4 toolchain. Key findings from the
freegnm audit:

1. **OpenOrbis headers are incomplete.** `orbis/_types/gnm.h` declares `sceGnm*` as
   `void func()` with no params and uses incomplete types (`u_short`, `u_int`).
   freegnm's `driver_orbis.c` and `platform_orbis.c` deliberately avoid OpenOrbis
   headers and declare proper signatures inline. **opengnm does the same:** we provide
   our own complete `sceGnm*` declarations and do NOT include
   `orbis/_types/gnm.h`. Consumers include `<gnm.h>` from opengnm instead.

2. **Target triple:** `x86_64-ps4-elf` (clang `--target=x86_64-ps4-elf -fPIC`).

3. **Sysroot:** `-isysroot $(OO_PS4_TOOLCHAIN) -isystem $(TOOLCHAIN)/include`.

4. **Link libraries (orbis):** `-lc -lkernel -lSceGnmDriver` for core GNM;
   `-lSceVideoOut` for present/flip paths (separate, not GNM).

5. **Calling convention:** `PS4_SYSV_ABI = __attribute__((sysv_abi))`. Technically
   redundant on x86_64 (sysv is default) but matches Sony SDK convention and documents
   intent. opengnm defines this in `gnm_types.h`.

6. **Error codes:** Sony `ORBIS_GNM_ERROR_*` pattern (`0x80D11000+` for submission,
   `0x80D12000+` validation warnings, `0x80D13000+` validation errors,
   `0x80D13FFF` not-enabled). Sourced from shadPS4 `gnm_error.h`.

7. **Firmware symbols:** At link time, `sceGnm*` resolve to `libSceGnmDriver.sprx`
   (via `-lSceGnmDriver`). opengnm declares externs with proper signatures; the
   firmware provides the implementations. For functions OpenOrbis doesn't stub,
   we declare them ourselves.

8. **Build config:** `config.orbis.mak` sets `PLATFORM=orbis`,
   `TOOLCHAIN=$(OO_PS4_TOOLCHAIN)`. CMake sets `OPENGNM_PLATFORM=orbis` with
   `PLATFORM_PS4=ON`.

9. **Struct binary compatibility:** All `Gnm*` structs use `_Static_assert` size
   checks matching freegnm's verified sizes (shader registers, RT, DRT, buffer,
   sampler, command buffer). This ensures binary compat with real PS4 data.

10. **Docker build:** `build.sh` wraps the OpenOrbis Docker image
    (`openorbisofficial/toolchain:latest`) for reproducible cross-compiles.

---

## Current State (freegnm → opengnm gap)

| Metric | freegnm (source) | Sony SDK (target) | Gap |
|--------|-----------------|-------------------|-----|
| `sceGnm*` functions | 16 wrapped | 207 total | 191 missing |
| API naming | `gnm*` / `gnmDriver*` / `gpa*` | `sceGnm*` / `sceGpa*` | Prefix mismatch |
| Header layout | Flat `gnm/*.h` | Sony SDK convention | Reorganize |
| Calling convention | None | `PS4_SYSV_ABI` | Add |
| Struct sizes | `_Static_assert` verified | Must match exactly | Preserve |
| Implementation LOC | ~18K (C) | — | Port proven algos |

**Critical proven code to port (not rewrite from zero):**
- gpuaddr / AddrLib surface computation: 4,164 LOC (AMD PAL-derived math)
- GCN assembler: 9,039 LOC (needed for `sceGnmCreateFetchShader`)
- PM4 encoding: 551 LOC
- drawcommandbuffer.c: 1,939 LOC (PM4 command buffer building)

---

## Architecture

```
opengnm/
├── include/                        # Public headers (Sony SDK layout)
│   ├── gnm.h                       # Master include
│   ├── gnm_types.h                 # All Gnm* enums + PS4_SYSV_ABI + forward decls
│   ├── gnmdriver.h                 # sceGnm* runtime (207 functions)
│   ├── gnm_drawcommandbuffer.h     # sceGnmDrawCmd* command buffer building
│   ├── gnm_rendertarget.h
│   ├── gnm_depthrendertarget.h
│   ├── gnm_texture.h
│   ├── gnm_sampler.h
│   ├── gnm_shader.h                # Fetch shader, input semantics, stage registers
│   ├── gnm_shaderbinary.h
│   ├── gnm_dataformat.h
│   ├── gnm_error.h                 # ORBIS_GNM_ERROR_* codes
│   ├── gnm_controls.h              # Control register bitfield structs
│   ├── gpuaddr.h                   # sceGpa* surface computation
│   ├── platform.h                  # Platform detection (ORBIS/GENERIC)
│   └── pm4/                        # PM4 register definitions
│       ├── amdgfxregs.h
│       └── sid.h
├── src/
│   ├── drawcommandbuffer.c         # sceGnmDrawCmd* (PM4 emission — core value)
│   ├── rendertarget.c
│   ├── depthrendertarget.c
│   ├── texture.c
│   ├── shader.c                    # Fetch shader generation via GCN assembler
│   ├── shaderbinary.c
│   ├── dataformat.c
│   ├── commandbuffer.c
│   ├── error.c
│   ├── driver_orbis.c              # Orbis: firmware delegation
│   ├── driver_generic.c            # Generic: pure software PM4 (host testing)
│   ├── platform_orbis.c
│   ├── platform_generic.c
│   ├── validate.c                  # sceGnmValidate* (NEW)
│   ├── resource.c                  # sceGnmRegisterOwner/Resource (NEW)
│   ├── workload.c                  # sceGnmBeginWorkload/etc (NEW)
│   ├── debugprof.c                 # sceGnmSqtt*/Spm*/Debugger* (STUBS)
│   ├── gpuaddr/                    # sceGpa* surface computation (ported)
│   │   ├── surface.c
│   │   ├── tilemodes.c
│   │   ├── tiler.c
│   │   ├── decompress.c
│   │   └── surfgen.c
│   ├── gcn/                        # GCN assembler (ported, lib-internal)
│   │   ├── assembler.c
│   │   ├── decoder.c
│   │   └── ...
│   └── pm4/                        # PM4 encoding (ported)
├── tests/
│   ├── test_surface.c
│   ├── test_drawcmd.c
│   ├── test_validate.c
│   └── test_api.c
├── CMakeLists.txt
├── Makefile
├── config.orbis.mak
├── config.generic.mak
├── build.sh                        # Docker wrapper (OpenOrbis)
├── COPYING
└── README.md
```

**Tools split out:** `gcn-dis`, `gnf-conv`, `gnf-info`, `gnf-mk`, `pm4-dis`, `psb-dis`
move to a separate `opengnm-tools/` repo. The library keeps only what it needs: GCN
assembler (fetch shaders), gpuaddr, PM4 encoding. GNF/FNF/PSSL decoder code goes with
the tools.

---

## Phases

### Phase 1: Header Layer + Build System [CRITICAL]

The foundation — all public headers with correct Sony SDK signatures.

**Deliverables:**
- `include/gnm_types.h` — All `Gnm*` enums (`GnmZFormat`, `GnmStencilFormat`,
  `GnmBlendOp`, `GnmArrayMode`, etc.) with exact values matching freegnm's `types.h`.
  Defines `PS4_SYSV_ABI`. Does NOT include `orbis/_types/gnm.h` (incomplete).
- `include/gnm_controls.h` — Control bitfield structs (`GnmBlendControl`,
  `GnmDepthStencilControl`, `GnmPrimitiveSetup`, `GnmViewportTransformControl`,
  `GnmDbRenderControl`) with `_Static_assert` size checks.
- `include/gnmdriver.h` — All 207 `sceGnm*` function declarations with
  `PS4_SYSV_ABI` and correct signatures. Organized by category (see Appendix A).
- `include/gnm_drawcommandbuffer.h` — All `sceGnmDrawCmd*` declarations (renamed
  from `gnmDrawCmd*`).
- `include/gnm_rendertarget.h` / `gnm_depthrendertarget.h` / `gnm_texture.h` /
  `gnm_sampler.h` / `gnm_shader.h` / `gnm_shaderbinary.h` / `gnm_dataformat.h` /
  `gnm_error.h` — Type definitions + creation functions, `sceGnm*` / `sceGpa*`.
- `include/gpuaddr.h` — `sceGpaComputeSurfaceInfo`, `sceGpaFindOptimalSurface`,
  etc. (renamed from `gpa*`).
- `include/pm4/amdgfxregs.h` + `include/pm4/sid.h` — GCN register defs (ported).
- `include/platform.h` — `GnmGpuMode`, platform detection.
- `CMakeLists.txt` — `OPENGNM_PLATFORM` option (orbis/generic/auto),
  `OPENGNM_TOOLS` option (default OFF).
- `Makefile` + `config.orbis.mak` + `config.generic.mak` — dual-config Make build.
- `build.sh` — Docker wrapper for OpenOrbis cross-compile.

**Gate P1:** Headers compile (no impl yet). `_Static_assert` sizes match freegnm.
`#include <gnm.h>` compiles on host + orbis targets.

### Phase 2: Core Implementation [CRITICAL]

Command buffer building, resource setup, shader helpers.

**Deliverables:**
- `src/drawcommandbuffer.c` — All `sceGnmDrawCmd*` functions. Emit PM4 packets.
  **Port algorithm from freegnm's 1,939 LOC, rewrite with `sceGnm*` naming.**
- `src/rendertarget.c` / `depthrendertarget.c` — RT creation/sizing/address. Port.
- `src/texture.c` — Texture creation/sizing/address. Port.
- `src/shader.c` — Fetch shader generation (uses GCN assembler). Port.
- `src/dataformat.c` — Format utils. Port.
- `src/commandbuffer.c` — Cmd buffer init/reset. Port.
- `src/error.c` — `sceGnmStrError`. Port.
- `src/gpuaddr/` — Port 4,164 LOC AddrLib verbatim (AMD PAL math). Rename to `sceGpa*`.
- `src/gcn/` — Port GCN assembler (internal, `opengnm_gcn_*` prefix).
- `src/pm4/` — Port PM4 encoding helpers.

**Gate P2:** `sceGnmDrawCmd*` produce byte-identical PM4 to freegnm's `gnmDrawCmd*`.
`sceGpaComputeSurfaceInfo` matches `gpaComputeSurfaceInfo`. Ported tests pass.

### Phase 3: Runtime Delegation (orbis backend) [CRITICAL]

Thin wrappers delegating to PS4 firmware `libSceGnmDriver.sprx`.

**Deliverables:**
- `src/driver_orbis.c` — All runtime `sceGnm*`:
  - Draw/Dispatch/Shader-set/Shader-update/Submit/Init/SDMA/Compute-queue/
    VGT/Wave/Misc — forward to firmware via proper-signature externs (NOT
    OpenOrbis's `void func()` stubs).
  - Firmware extern declarations inline (matching shadPS4 RE signatures).
- `src/platform_orbis.c` — `sceGnmGpuMode` (BASE/NEO via `sceKernelIsNeoMode`),
  `sceGnmPlatGetBufferLabelAddress` (via `sceVideoOutGetBufferLabelAddress`).
  Declare externs inline (avoid OpenOrbis incomplete headers).

**Gate P3:** opengnm orbis build links `-lkernel -lSceGnmDriver`. Trivial draw+submit
compiles and links. No dependency on `orbis/_types/gnm.h`.

### Phase 4: Validation + Resource Registration + Workload [IMPORTANT]

New functionality.

**Deliverables:**
- `src/validate.c` — `sceGnmValidate*` (11 functions). Orbis: forward to firmware.
  Generic: PM4 packet validation (walk packets, check headers/sizes).
- `src/resource.c` — `sceGnmRegisterOwner`/`Resource`/`FindResources`/`GetResource*`
  (19 functions). Orbis: forward. Generic: in-memory registry.
- `src/workload.c` — `sceGnmBeginWorkload`/`EndWorkload`/`CreateWorkloadStream`/
  `DingDong`/`AreSubmitsAllowed` (8 functions). Orbis: forward. Generic: minimal
  tracking.

**Gate P4:** Validation detects malformed PM4 on generic. Resource registration
tracks allocations. Workload stream create/destroy works.

### Phase 5: Generic Backend (host testing) [IMPORTANT]

Full software implementation for testing without a PS4.

**Deliverables:**
- `src/driver_generic.c` — All runtime `sceGnm*` in software:
  - Draw/dispatch: emit PM4 into command buffer
  - Submit: validate PM4 via decoder, log malformed, return OK
  - Shader set: emit `SET_SH_REG` PM4
  - SDMA: emit DMA PM4
  - Init: default hardware state PM4 blob (port freegnm's 200-line blob)
  - Compute queue/workload/resource/validate: in-memory stubs
- `src/platform_generic.c` — `sceGnmGpuMode` returns `GNM_GPU_BASE`,
  `sceGnmPlatGetBufferLabelAddress` returns malloc'd label.

**Gate P5:** Generic build compiles without PS4 SDK. All ported tests pass. PM4
output validated by decoder.

### Phase 6: Debugger/Profiler Stubs [LOW]

Headers match Sony SDK; implementations are no-ops.

**Deliverables:**
- `src/debugprof.c` — All `sceGnmSqtt*` (~25), `sceGnmSpm*` (~12),
  `sceGnmDebugger*` (~10), `sceGnmGpuPaDebug*`, `sceGnmDebugHardwareStatus`, marker
  functions, coredump functions, `DriverInternalRetrieveGnmInterface*` (7),
  `LogicalCu*` (3), misc getters (~70 functions). Return
  `ORBIS_GNM_ERROR_VALIDATION_NOT_ENABLED` or appropriate error. Orbis: forward
  lightweight ones to firmware.

**Gate P6:** All 207 `sceGnm*` have implementations (real or stub). Full header
surface compiles. A test calling every function links and runs without crashing.

### Phase 7: Test Porting + Integration [IMPORTANT]

**Deliverables:**
- Port freegnm's test suite to opengnm — rename `gnm*` → `sceGnm*`, update includes.
- `tests/test_surface.c` — gpuaddr (54 tests).
- `tests/test_drawcmd.c` — PM4 command buffer building.
- `tests/test_validate.c` — PM4 validation.
- `tests/test_api.c` — Call every `sceGnm*` once (linkage + signature check).
- Verify Eden builds against opengnm (update `video_core/CMakeLists.txt`).
- Verify freegnm-examples compile against opengnm.

**Gate P7:** All tests pass on generic backend. Eden links against opengnm.
freegnm-examples compile.

---

## Design Decisions

1. **Port proven algorithms, rewrite structure.** gpuaddr (4K LOC AddrLib), GCN
   assembler (9K LOC), PM4 encoding are battle-tested. The "clean rewrite" applies to
   API surface, header organization, naming, and code structure — implementation
   logic is ported from freegnm with renaming and refactoring.

2. **`sceGnm*` naming throughout.** All public functions use `sceGnm*` / `sceGpa*`.
   Internal helpers use `opengnm_*` prefix.

3. **`PS4_SYSV_ABI` on all declarations.** `__attribute__((sysv_abi))`.

4. **Binary-compatible struct layouts.** All `Gnm*` struct sizes preserved via
   `_Static_assert`.

5. **No `gnm*` backward compat.** Clean break. Eden and freegnm-examples migrate to
   `sceGnm*`.

6. **No `orbis/_types/gnm.h` dependency.** opengnm provides complete declarations
   (OpenOrbis headers are incomplete — `void func()` stubs, missing types).

7. **Library vs tools split.** Library keeps GCN assembler (fetch shaders), gpuaddr,
   PM4, core GNM. GNF/FNF/PSSL decoder code → `opengnm-tools/`.

8. **Two backends, same headers.** `#include <gnm.h>` works on orbis + generic.
   Backend via build config.

---

## Appendix A: sceGnm* Function Categories (207 total)

| Category | Count | Phase | Notes |
|----------|-------|-------|-------|
| Draw (DrawIndex/Auto/Indirect/Multi/Offset) | 12 | 2-3 | Core |
| Dispatch (Direct/Indirect/OnMec/Init) | 4 | 2-3 | Core |
| Shader set (Vs/Ps/Ps350/Cs/CsMod/Gs/Es/Hs/Ls/Embedded) | 11 | 2-3 | Core |
| Shader update (Vs/Ps/Ps350/Gs/Hs) | 5 | 3 | Forward to firmware |
| Submit (CommandBuffers/AndFlip/ForWorkload/Done/RequestFlip) | 7 | 3 | Forward to firmware |
| Init (DefaultHardwareState 175/200/350/ContextState 400) | 6 | 2-3 | Core + forward |
| SDMA (Open/Close/CopyLinear/CopyTiled/CopyWindow/ConstFill/Flush/GetMinCmd) | 8 | 3 | Forward to firmware |
| Compute queue (Map/MapPriority/Unmap/TessRing/GsRing) | 5 | 3 | Forward to firmware |
| VGT/Wave (SetVgtControl/Reset/WaveLimit*) | 4 | 3 | Forward to firmware |
| Validate (Validate*/GetDiagnostics/Disable/Reset/Register) | 11 | 4 | New |
| Resource (RegisterOwner/Resource/Find/Get/Set/Unregister) | 19 | 4 | New |
| Workload (Begin/End/Create/Destroy/DingDong/AreSubmits) | 8 | 4 | New |
| EQ (AddEqEvent/Delete/GetEventType/GetTimeStamp) | 4 | 4 | Forward to firmware |
| Sqtt (trace buffer profiling) | 25 | 6 | Stubs |
| Spm (performance counters) | 12 | 6 | Stubs |
| Debugger (GetAddressWatch/Halt/Read/Write/Resume) | 10 | 6 | Stubs |
| Markers (Push/Pop/Color/Set/ThreadTrace) | 6 | 6 | Stubs |
| Coredump/Misc (GetCoredump*/GetDebugTimestamp/GetLastWaited) | 8 | 6 | Stubs |
| DriverInternal (RetrieveGnmInterface* 7 variants/VirtualQuery/TriggerCapture) | 10 | 6 | Stubs |
| LogicalCu/GpuPa (CuIndex/Mask/Tca/Physical) | 5 | 6 | Stubs |
| MipStats (Setup/Disable/RequestAndReset) | 3 | 6 | Stubs |
| Misc (FlushGarlic/GetGpuCoreClock/GetNumTca/GetOffChipTess/IsUserPa/PaHeartbeat) | 14 | 6 | Stubs |

---

## Risk Register

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| gpuaddr port introduces surface bugs | Low | High | Port verbatim, diff test output vs freegnm |
| sceGnm* signature mismatch with firmware | Med | High | Cross-check shadPS4 RE + OpenOrbis + NIDs |
| Missing firmware symbol at link | Med | Med | Declare externs; link test on orbis |
| Struct layout drift | Low | High | `_Static_assert` every struct size |
| OpenOrbis header conflict | Med | Med | Don't include `orbis/_types/gnm.h` |
| Eden migration breaks builds | Med | Med | Gate P7: verify Eden links |
| Debugger stubs wrong error codes | Low | Low | Match `ORBIS_GNM_ERROR_*` |

---

## Execution Order

1. **Phase 1** — Headers + build system. Foundation.
2. **Phase 2** — Core implementation (command buffer, RT, texture, shader, gpuaddr).
3. **Phase 3** — Runtime delegation (orbis backend). First PS4 link.
4. **Phase 5** — Generic backend (parallel with 3). Host testing.
5. **Phase 4** — Validate + resource + workload.
6. **Phase 7** — Test porting + Eden integration.
7. **Phase 6** — Debugger/profiler stubs. Polish.

After Phase 3, opengnm builds and links on PS4. After Phase 7, it's validated.
