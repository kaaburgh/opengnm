# opengnm — Clean Rewrite Plan

> **Goal:** A clean-rewrite PS4 GNM library providing `sceGnm*` / `sceGpa*` drop-in API
> compatibility with the Sony official PS4 SDK. Any PS4 app or game written against the
> official SDK headers should compile and link against opengnm unmodified. Usable via
> OpenOrbis on PS4, with a generic host backend for testing.
>
> **Sony ABI reference:** `shadPS4/src/core/libraries/gnmdriver/` (207 `sceGnm*` decls).
> **Firmware modules:** `/Users/bizkut/Downloads/PS4/FIRMWARES/9.00/` (decrypted ELFs).
> **RE documentation:** `tools/gnm_driver_fw900_analysis.md`, `tools/gnm_pm4_shader_analysis.md`,
> `tools/gnm_compositor_analysis.md`.

---

## OpenOrbis SDK Compatibility (binding constraints)

opengnm must build and link against the OpenOrbis PS4 toolchain. Key findings from
auditing the OpenOrbis headers and existing PS4 GNM code:

1. **OpenOrbis headers are incomplete.** `orbis/_types/gnm.h` declares `sceGnm*` as
   `void func()` with no params and uses incomplete types (`u_short`, `u_int`).
   **opengnm does NOT include `orbis/_types/gnm.h`.** We provide our own complete
   `sceGnm*` declarations with proper signatures. Consumers include `<gnm.h>` from
   opengnm instead.

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
   checks (shader registers, RT, DRT, buffer, sampler, command buffer). This ensures
   binary compat with real PS4 data.

10. **Docker build:** `build.sh` wraps the OpenOrbis Docker image
    (`openorbisofficial/toolchain:latest`) for reproducible cross-compiles.

---

## Reverse Engineering Phase

opengnm's implementation must match the real PS4 firmware driver's behavior. The RE
phase provides the ground truth for PM4 packet formats, validation rules, shader
register layouts, and function signatures that cannot be derived from public docs
alone.

### RE Resources

**Firmware module dumps (FW 9.00, already decrypted ELFs):**
```
/Users/bizkut/Downloads/PS4/FIRMWARES/9.00/
├── kernel.bin                                    # PS4 kernel (42MB)
├── modules/system/common/lib/
│   ├── libSceGnmDriver.sprx                      # GNM driver (96KB) — PRIMARY RE TARGET
│   ├── libSceGnmDriver.sprx.exports.txt          # 259 export entries (NID + vaddr + name)
│   ├── libSceGnmDriver.sprx.r2.txt               # radare2 symbol script
│   ├── libSceGnmDriverForNeoMode.sprx            # NEO/Pro variant (96KB)
│   ├── libSceVideoOut.sprx                       # Video output (144KB)
│   ├── libSceVideoOutSecondary.sprx              # Secondary video output
│   └── libkernel.sprx                            # Kernel library
├── modules/system/sys/
│   ├── GnmCompositor.elf                         # Sony's own GNM compositor (291KB)
│   └── gpudump.elf                               # GPU dump utility (128KB)
└── modules/system/priv/lib/
    └── libSceGnmDriver_sys.sprx                  # System-priv GNM driver variant
```

**RE documentation (in workspace `tools/`):**
- `tools/gnm_driver_fw900_analysis.md` (318 lines) — Full RE of `libSceGnmDriver.sprx`:
  binary info, export table structure, NID table (289 NIDs), GPU MMIO register table,
  utility functions, draw function map (12 functions with PM4 opcodes/counts/validation),
  shader setup functions (8 stages with register offsets), validate functions
  (**RESOLVED: all stubs returning 0**), submit functions, initialization, shader
  binary parser, DrawInitDefaultHardwareState.
- `tools/gnm_pm4_shader_analysis.md` (136 lines) — PM4 shader packet layouts for all
  shader stages (PS/VS/ES/GS/HS/LS/CS), register spaces (SH/CONTEXT), exact packet
  byte layouts.
- `tools/gnm_compositor_analysis.md` (85 lines) — Real-world GNM usage from Sony's
  compositor: 308 PM4 type3 headers, opcode distribution, NID imports.

**Cross-reference sources:**
- `shadPS4/src/core/libraries/gnmdriver/gnmdriver.h` — 207 `sceGnm*` function signatures
- `shadPS4/src/core/libraries/gnmdriver/gnm_error.h` — Sony error code constants
- `../OrbisNet/aerolib.csv` — 97,623 NID→symbol mappings for IDA/Ghidra/r2
- `../OpenOrbis/ps4libdoc/known_names.txt` — 219 known `sceGnm*` NIDs
- `mesa/src/amd/common/sid.h` — AMD GCN register definitions (gfx6-gfx12)
- `mesa/src/amd/registers/gfx8.json` — gfx8 register spec

**RE tooling:**
- **radare2** (6.1.8) at `/opt/homebrew/bin/r2` — disassembly and analysis
- **IDA Pro / Ghidra** — decompilation with aerolib.csv symbol resolution
- **ps4debug** — TCP-based GDB-style debugger for runtime verification on PS4
- **GoldHEN v2.4b18** — kernel access for FW 9.00

### RE Deliverables

The RE phase produces verified documentation that Phases 2-6 implement against:

**RE-1: PM4 packet formats for all draw/dispatch functions [DONE]**
- Source: `tools/gnm_driver_fw900_analysis.md` — Draw Function Map
- All 12 draw functions + 4 dispatch functions RE'd with exact PM4 opcodes, counts,
  and validation rules
- Key findings: PM4 count = N-1, shader type bit (bit 1), stage bitmask 0x2B,
  flag validation (`flags & 0x1FFFFFFE == 0`), trailing NOP pattern (`0xC0021000`)

**RE-2: PM4 packet formats for all shader set/update functions [DONE]**
- Source: `tools/gnm_pm4_shader_analysis.md` + `tools/gnm_driver_fw900_analysis.md`
- All 8 shader stages (PS/VS/ES/GS/HS/LS/CS + embedded) RE'd with SH/CONTEXT
  register offsets and exact packet byte layouts
- Shader register mapping: SH regs base 0x2C000 (opcode 0x76), CONTEXT regs base
  0xA000 (opcode 0x69)

**RE-3: Validation function behavior [DONE]**
- Source: `tools/gnm_driver_fw900_analysis.md` — Validate Functions section
- **Finding: all validate functions are stubs returning 0 (success) on FW 9.00**
- `sceGnmValidateCommandBuffers`, `ValidateDrawCommandBuffers`,
  `ValidateDispatchCommandBuffers`, `ValidateGetVersion`, `ValidateOnSubmitEnabled`,
  `ValidateResetState` — all `xor eax, eax; ret`
- Implication: Phase 4 validate implementation can be no-ops on orbis (forward to
  firmware which also no-ops), real validation only on generic backend

**RE-4: Submit function packet format [DONE]**
- Source: `tools/gnm_driver_fw900_analysis.md` — Submit Functions section
- `sceGnmSubmitCommandBuffers` builds INDIRECT_BUFFER packets
  (`0xC0023300` opcode 0x33, `0xC0023F00` opcode 0x3F)
- Kernel masks ib_base with `0x000FFFFF00000000`, ORs `VMID<<52`
- `sceGnmSubmitAndFlipCommandBuffers` = submit + flip

**RE-5: DrawInitDefaultHardwareState [DONE]**
- Source: `tools/gnm_driver_fw900_analysis.md` — DrawInitDefaultHardwareState (RESOLVED) section
- PM4 blob at data table vaddr 0x7E20 (128 dwords), function at vaddr 0x3260
- Full PM4 packet sequence documented: PFP_SYNC_ME, CLEAR_STATE, SET_SH_REG_OFFSET,
  SET_SH_REG, SET_CONTEXT_REG, ACQUIRE_MEM, SET_UCONFIG_REG, CONTEXT_CONTROL
- Variants: 175 (0x3390), 200 (0x3380), 350 (0x34F0+)
- Key difference from shadPS4: real driver has PFP_SYNC_ME + SET_SH_REG_OFFSET

**RE-6: Shader binary parser [DONE]**
- Source: `tools/gnm_driver_fw900_analysis.md` — Shader Binary Parser section
- Metadata field offsets, CRC32 algorithm, resource table layout all RE'd

**RE-7: SDMA function signatures [DONE — stubs on retail]**
- 8 functions: `sceGnmSdmaOpen/Close/CopyLinear/CopyTiled/CopyWindow/ConstFill/Flush/GetMinCmdSize`
- **Finding: ALL 8 are stubs returning `ORBIS_GNM_ERROR_FAILURE` on retail firmware**
- No SDMA error strings in binary, no code references to SDMA NIDs
- shadPS4 confirms: "Not available in retail firmware"
- See `tools/gnm_sdma_debugprof_re_analysis.md`

**RE-8: Debugger/profiler function signatures [DONE — stubs on retail]**
- `sceGnmSqtt*` (25), `sceGnmSpm*` (12), `sceGnmDebugger*` (10) — **ALL stubs**
- All return `ORBIS_GNM_ERROR_FAILURE` on retail firmware
- Require kernel-level GPU access — not available in user-mode
- See `tools/gnm_sdma_debugprof_re_analysis.md`

**RE-9: Resource registration internals [DONE — stubs on retail]**
- `sceGnmRegisterOwner`/`RegisterResource`/`FindResources` — **ALL stubs**
- All return `ORBIS_GNM_ERROR_FAILURE` on retail firmware
- Resource registration is a devkit-only feature
- See `tools/gnm_sdma_debugprof_re_analysis.md`

**RE-10: GnmCompositor real-world patterns [DONE]**
- Source: `tools/gnm_compositor_analysis.md`
- 308 PM4 type3 headers analyzed, opcode distribution confirmed
- SET_CONTEXT_REG (0x69): 142 uses, SET_SH_REG (0x76): 40 uses, NOP (0x10): 90 uses
- Reference for correct command buffer construction patterns

### RE Methodology

```
1. shadPS4 source code FIRST — most signatures and PM4 formats already RE'd
   └─ src/core/libraries/gnmdriver/gnmdriver.h (207 sceGnm* decls)
   └─ src/video_core/amdgpu/ (PM4 regs, liverpool_to_vk, shader parsing)

2. Existing RE docs SECOND — tools/gnm_*_analysis.md (already completed RE)
   └─ Draw/dispatch/shader/validate/submit all documented

3. Firmware module disassembly THIRD — for remaining gaps
   └─ Load libSceGnmDriver.sprx into r2/IDA with aerolib.csv
   └─ Parse PS4 dynamic tags (0x61000013 = NID entries)
   └─ Find functions via NID → trace logic → document findings

4. GnmCompositor.elf FOURTH — real-world usage validation
   └─ Confirm our PM4 patterns match Sony's own code

5. Runtime verification FIFTH — on PS4 via ps4debug/GoldHEN
   └─ Read GPU memory to verify static analysis
   └─ Patch submit path to log command buffers if needed
```

### RE Workflow for Remaining Gaps (RE-5, RE-7, RE-8, RE-9)

```bash
# Load libSceGnmDriver.sprx into radare2 with symbols
r2 -q -c '. /Users/bizkut/Downloads/PS4/FIRMWARES/9.00/modules/system/common/lib/libSceGnmDriver.sprx.r2.txt' \
     /Users/bizkut/Downloads/PS4/FIRMWARES/9.00/modules/system/common/lib/libSceGnmDriver.sprx

# Find function by NID (example: sceGnmSdmaOpen)
# Look up NID in aerolib.csv, then find export vaddr in .exports.txt
grep sceGnmSdmaOpen /Users/bizkut/Downloads/PS4/FIRMWARES/9.00/modules/system/common/lib/libSceGnmDriver.sprx.exports.txt

# Disassemble at the found vaddr
# r2: pdf @ <vaddr>

# Cross-reference with shadPS4
grep -rn "sceGnmSdma" /Users/bizkut/Downloads/PS5/homebrew/shadPS4/src/
```

---

## Current State (API gap)

| Metric | Target (Sony SDK) | Status | Gap |
|--------|-------------------|--------|-----|
| `sceGnm*` functions | 207 total | 207 declared (Phase 1) | Implementations needed |
| API naming | `sceGnm*` / `sceGpa*` | Done (Phase 1) | — |
| Header layout | Sony SDK convention | Done (Phase 1) | — |
| Calling convention | `PS4_SYSV_ABI` | Done (Phase 1) | — |
| Struct sizes | Binary-compatible | Verified (Phase 1) | — |
| PM4 emission | Match firmware | RE'd (RE-1 to RE-4) | Implement |
| Validation | Match firmware | RE'd (RE-3: stubs) | Implement as stubs |
| gpuaddr | AMD PAL math | Algorithm available | Port + rename |

**Proven algorithms to port (AMD PAL-derived, not rewrite from zero):**
- gpuaddr / AddrLib surface computation: 4,164 LOC (AMD PAL-derived math)
- GCN assembler: 9,039 LOC (needed for `sceGnmCreateFetchShader`)
- PM4 encoding: 551 LOC
- drawcommandbuffer: 1,939 LOC (PM4 command buffer building)

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
│   ├── validate.c                  # sceGnmValidate* (stubs — RE-3 confirmed)
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

**Shader compiler split out:** The SPIR-V to PS4 Shader Binary compiler is in a
separate `opengnm-psbc/` repo, built on Mesa 26.2.0 (NIR + ACO). It consumes
opengnm's headers for the `GnmShaderFileHeader` / `GnmVsShader` / `GnmPsShader`
binary container format. See `opengnm-psbc/OPENGNM_PSBC_PLAN.md`.

---

## Phases

### Phase 0: Reverse Engineering [DONE]

RE ground truth for PM4 formats, validation rules, and firmware behavior.
**All 10 RE deliverables complete.**

**Key finding: 122 of 207 sceGnm* functions are stubs returning
ORBIS_GNM_ERROR_FAILURE on retail firmware.** Only ~85 functions have real
implementations (draw, dispatch, shader, submit, init, compute queue, VGT, validate).

See `tools/gnm_sdma_debugprof_re_analysis.md` for the retail-vs-devkit breakdown.

**Completed RE deliverables:**
- RE-1: Draw/dispatch PM4 packet formats (12+4 functions) — DONE
- RE-2: Shader set/update PM4 packet formats (8 stages) — DONE
- RE-3: Validation function behavior (all stubs, return 0) — DONE
- RE-4: Submit function packet format — DONE
- RE-5: DrawInitDefaultHardwareState PM4 blob (128 dwords) — DONE
- RE-6: Shader binary parser — DONE
- RE-7: SDMA functions (8, all stubs on retail) — DONE
- RE-8: Debugger/profiler (Sqtt/Spm/Debugger ~47, all stubs on retail) — DONE
- RE-9: Resource registration (19, all stubs on retail) — DONE
- RE-10: GnmCompositor real-world patterns — DONE

**Method:** Load `libSceGnmDriver.sprx` into r2/IDA with `aerolib.csv` NID resolution.
Find functions via NID export table, trace logic, document findings in `tools/`.

### Phase 1: Header Layer + Build System [DONE]

The foundation — all public headers with correct Sony SDK signatures.

**Deliverables:**
- `include/gnm_types.h` — All `Gnm*` enums, `PS4_SYSV_ABI`, constants.
- `include/gnm_controls.h` — Control bitfield structs with `_Static_assert` checks.
- `include/gnmdriver.h` — All 207 `sceGnm*` function declarations with
  `PS4_SYSV_ABI` and correct signatures. Organized by category (see Appendix A).
- `include/gnm_drawcommandbuffer.h` — All `sceGnmDrawCmd*` declarations.
- `include/gnm_rendertarget.h` / `gnm_depthrendertarget.h` / `gnm_texture.h` /
  `gnm_sampler.h` / `gnm_shader.h` / `gnm_shaderbinary.h` / `gnm_dataformat.h` /
  `gnm_error.h` — Type definitions + creation functions.
- `include/gpuaddr.h` — `sceGpa*` surface computation functions.
- `include/pm4/amdgfxregs.h` + `include/pm4/sid.h` — GCN register defs.
- `include/platform.h` — `GnmGpuMode`, platform detection.
- `CMakeLists.txt` — `OPENGNM_PLATFORM` option (orbis/generic/auto).
- `Makefile` + `config.orbis.mak` + `config.generic.mak` — dual-config build.
- `build.sh` — Docker wrapper for OpenOrbis cross-compile.

**Gate P1 (PASSED):** Headers compile cleanly. `_Static_assert` sizes verified.
`#include <gnm.h>` compiles on host + orbis targets. 207 `sceGnm*` declared.

### Phase 2: Core Implementation [CRITICAL]

Command buffer building, resource setup, shader helpers. Implement against RE docs.

**Deliverables:**
- `src/drawcommandbuffer.c` — All `sceGnmDrawCmd*` functions. Emit PM4 packets per
  RE-1/RE-2 (exact opcodes, counts, validation from `tools/gnm_driver_fw900_analysis.md`).
- `src/rendertarget.c` / `depthrendertarget.c` — RT creation/sizing/address.
- `src/texture.c` — Texture creation/sizing/address.
- `src/shader.c` — Fetch shader generation (uses GCN assembler).
- `src/dataformat.c` — Format utils.
- `src/commandbuffer.c` — Cmd buffer init/reset.
- `src/error.c` — `sceGnmStrError`.
- `src/gpuaddr/` — Port 4,164 LOC AddrLib (AMD PAL math). Rename to `sceGpa*`.
- `src/gcn/` — Port GCN assembler (internal, `opengnm_gcn_*` prefix).
- `src/pm4/` — Port PM4 encoding helpers.

**Gate P2:** `sceGnmDrawCmd*` produce PM4 matching RE-1/RE-2 byte layouts.
`sceGpaComputeSurfaceInfo` matches reference output. Tests pass.

### Phase 3: Runtime Delegation (orbis backend) [CRITICAL]

Thin wrappers delegating to PS4 firmware `libSceGnmDriver.sprx`.

**Deliverables:**
- `src/driver_orbis.c` — All runtime `sceGnm*`:
  - Draw/Dispatch/Shader-set/Shader-update/Submit/Init/SDMA/Compute-queue/
    VGT/Wave/Misc — forward to firmware via proper-signature externs (NOT
    OpenOrbis's `void func()` stubs).
  - Firmware extern declarations inline (matching RE signatures from RE-1 to RE-4).
- `src/platform_orbis.c` — `sceGnmGpuMode` (BASE/NEO via `sceKernelIsNeoMode`),
  `sceGnmPlatGetBufferLabelAddress` (via `sceVideoOutGetBufferLabelAddress`).
  Declare externs inline (avoid OpenOrbis incomplete headers).

**Gate P3:** opengnm orbis build links `-lkernel -lSceGnmDriver`. Trivial draw+submit
compiles and links. No dependency on `orbis/_types/gnm.h`.

### Phase 4: Validation + Resource Registration + Workload [IMPORTANT]

New functionality. Validation is stubs per RE-3 (firmware stubs them too).

**Deliverables:**
- `src/validate.c` — `sceGnmValidate*` (11 functions). Orbis: forward to firmware
  (which returns 0 per RE-3). Generic: PM4 packet validation (walk packets, check
  headers/sizes).
- `src/resource.c` — `sceGnmRegisterOwner`/`Resource`/`FindResources`/`GetResource*`
  (19 functions). Orbis: forward. Generic: in-memory registry.
- `src/workload.c` — `sceGnmBeginWorkload`/`EndWorkload`/`CreateWorkloadStream`/
  `DingDong`/`AreSubmitsAllowed` (8 functions). Orbis: forward. Generic: minimal
  tracking.

**Gate P4:** Validation returns 0 on orbis (matching RE-3). Generic validation
detects malformed PM4. Resource registration tracks allocations.

### Phase 5: Generic Backend (host testing) [IMPORTANT]

Full software implementation for testing without a PS4.

**Deliverables:**
- `src/driver_generic.c` — All runtime `sceGnm*` in software:
  - Draw/dispatch: emit PM4 into command buffer (per RE-1/RE-2)
  - Submit: validate PM4 via decoder, log malformed, return OK
  - Shader set: emit `SET_SH_REG` PM4 (per RE-2)
  - SDMA: emit DMA PM4
  - Init: default hardware state PM4 blob (per RE-5 when complete; use RE'd MMIO
    sequence as reference)
  - Compute queue/workload/resource/validate: in-memory stubs
- `src/platform_generic.c` — `sceGnmGpuMode` returns `GNM_GPU_BASE`,
  `sceGnmPlatGetBufferLabelAddress` returns malloc'd label.

**Gate P5:** Generic build compiles without PS4 SDK. All tests pass. PM4 output
validated by decoder against RE-1/RE-2 byte layouts.

### Phase 6: Debugger/Profiler Stubs [LOW]

Headers match Sony SDK; implementations are no-ops. Signatures from RE-8 when
available, otherwise from shadPS4.

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
- Port test suite — write tests against `sceGnm*` API, validate against RE docs.
- `tests/test_surface.c` — gpuaddr surface computation.
- `tests/test_drawcmd.c` — PM4 command buffer building (compare against RE-1/RE-2).
- `tests/test_validate.c` — PM4 validation (generic backend).
- `tests/test_api.c` — Call every `sceGnm*` once (linkage + signature check).
- Verify Eden builds against opengnm (update `video_core/CMakeLists.txt`).
- Verify example programs compile against opengnm.

**Gate P7:** All tests pass on generic backend. Eden links against opengnm.
Examples compile.

---

## Design Decisions

1. **Port proven algorithms, rewrite structure.** gpuaddr (4K LOC AddrLib), GCN
   assembler (9K LOC), PM4 encoding are battle-tested. The "clean rewrite" applies to
   API surface, header organization, naming, and code structure — implementation
   logic is ported with renaming and refactoring.

2. **`sceGnm*` naming throughout.** All public functions use `sceGnm*` / `sceGpa*`.
   Internal helpers use `opengnm_*` prefix.

3. **`PS4_SYSV_ABI` on all declarations.** `__attribute__((sysv_abi))`.

4. **Binary-compatible struct layouts.** All `Gnm*` struct sizes preserved via
   `_Static_assert`.

5. **No `gnm*` backward compat.** Clean break. Consumers migrate to `sceGnm*`.

6. **No `orbis/_types/gnm.h` dependency.** opengnm provides complete declarations
   (OpenOrbis headers are incomplete — `void func()` stubs, missing types).

7. **Library vs tools split.** Library keeps GCN assembler (fetch shaders), gpuaddr,
   PM4, core GNM. GNF/FNF/PSSL decoder code → `opengnm-tools/`.

8. **Two backends, same headers.** `#include <gnm.h>` works on orbis + generic.
   Backend via build config.

9. **RE-driven implementation.** PM4 packet formats, validation rules, and firmware
   behavior are verified against `libSceGnmDriver.sprx` disassembly (RE-1 to RE-10),
   not guessed from public docs alone.

---

## Appendix A: sceGnm* Function Categories (207 total)

| Category | Count | Phase | RE Status | Notes |
|----------|-------|-------|-----------|-------|
| Draw (DrawIndex/Auto/Indirect/Multi/Offset) | 12 | 2-3 | RE-1 DONE | Core, PM4 RE'd |
| Dispatch (Direct/Indirect/OnMec/Init) | 4 | 2-3 | RE-1 DONE | Core, PM4 RE'd |
| Shader set (Vs/Ps/Ps350/Cs/CsMod/Gs/Es/Hs/Ls/Embedded) | 11 | 2-3 | RE-2 DONE | Core, PM4 RE'd |
| Shader update (Vs/Ps/Ps350/Gs/Hs) | 5 | 3 | RE-2 DONE | Forward to firmware |
| Submit (CommandBuffers/AndFlip/ForWorkload/Done/RequestFlip) | 7 | 3 | RE-4 DONE | Forward to firmware |
| Init (DefaultHardwareState 175/200/350/ContextState 400) | 6 | 2-3 | RE-5 DONE | PM4 blob needs RE |
| SDMA (Open/Close/CopyLinear/CopyTiled/CopyWindow/ConstFill/Flush/GetMinCmd) | 8 | 3 | RE-7 DONE (stubs) | Forward to firmware |
| Compute queue (Map/MapPriority/Unmap/TessRing/GsRing) | 5 | 3 | — | Forward to firmware |
| VGT/Wave (SetVgtControl/Reset/WaveLimit*) | 4 | 3 | — | Forward to firmware |
| Validate (Validate*/GetDiagnostics/Disable/Reset/Register) | 11 | 4 | RE-3 DONE | Stubs (firmware stubs too) |
| Resource (RegisterOwner/Resource/Find/Get/Set/Unregister) | 19 | 4 | RE-9 DONE (stubs) | New |
| Workload (Begin/End/Create/Destroy/DingDong/AreSubmits) | 8 | 4 | — | New |
| EQ (AddEqEvent/Delete/GetEventType/GetTimeStamp) | 4 | 4 | — | Forward to firmware |
| Sqtt (trace buffer profiling) | 25 | 6 | RE-8 DONE (stubs) | Stubs |
| Spm (performance counters) | 12 | 6 | RE-8 DONE (stubs) | Stubs |
| Debugger (GetAddressWatch/Halt/Read/Write/Resume) | 10 | 6 | RE-8 DONE (stubs) | Stubs |
| Markers (Push/Pop/Color/Set/ThreadTrace) | 6 | 6 | — | Stubs |
| Coredump/Misc (GetCoredump*/GetDebugTimestamp/GetLastWaited) | 8 | 6 | — | Stubs |
| DriverInternal (RetrieveGnmInterface* 7 variants/VirtualQuery/TriggerCapture) | 10 | 6 | — | Stubs |
| LogicalCu/GpuPa (CuIndex/Mask/Tca/Physical) | 5 | 6 | — | Stubs |
| MipStats (Setup/Disable/RequestAndReset) | 3 | 6 | — | Stubs |
| Misc (FlushGarlic/GetGpuCoreClock/GetNumTca/GetOffChipTess/IsUserPa/PaHeartbeat) | 14 | 6 | — | Stubs |

---

## Risk Register

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| PM4 emission doesn't match firmware | Low | High | RE-1/RE-2 verified byte layouts; diff test output |
| gpuaddr port introduces surface bugs | Low | High | Port verbatim, diff test output against reference |
| sceGnm* signature mismatch with firmware | Med | High | Cross-check shadPS4 RE + aerolib NIDs + firmware disassembly |
| Missing firmware symbol at link | Med | Med | Declare externs; link test on orbis |
| Struct layout drift | Low | High | `_Static_assert` every struct size |
| OpenOrbis header conflict | Med | Med | Don't include `orbis/_types/gnm.h` |
| RE-5 (InitDefaultHardwareState blob) incomplete | Med | Med | Use MMIO-direct version as fallback; RE blob when needed |
| Eden migration breaks builds | Med | Med | Gate P7: verify Eden links |
| Debugger stubs wrong error codes | Low | Low | Match `ORBIS_GNM_ERROR_*` |

---

## Execution Order

0. **Phase 0 (RE)** — Ongoing. RE-1 to RE-4, RE-6, RE-10 done. RE-5/7/8/9 as needed.
1. **Phase 1 (DONE)** — Headers + build system. Foundation.
2. **Phase 2** — Core implementation (command buffer, RT, texture, shader, gpuaddr).
   Implement against RE-1/RE-2 PM4 byte layouts.
3. **Phase 3** — Runtime delegation (orbis backend). First PS4 link.
4. **Phase 5** — Generic backend (parallel with 3). Host testing.
5. **Phase 4** — Validate + resource + workload. Validation is stubs per RE-3.
6. **Phase 7** — Test porting + Eden integration.
7. **Phase 6** — Debugger/profiler stubs. Polish.

After Phase 3, opengnm builds and links on PS4. After Phase 7, it's validated.
