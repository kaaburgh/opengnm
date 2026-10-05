# Baseline: `gpu_solid_rt` на shadPS4 + lavapipe

Дата проверки: 2026-10-05. Это эмпирическое продолжение [OPENGNM_REVIEW.md](OPENGNM_REVIEW.md).

## Итог

- Тест `gpu_solid_rt` из `kaaburgh/shadps4-open-test@ea584d9` (с исправлениями ниже) даёт
  **PASS** на shadPS4 `dade3af` с Mesa lavapipe (llvmpipe, CPU-рендеринг) под Xvfb, если в
  shadPS4 есть двухстрочная обработка EOP `INT_SEL=3` (строка 8):

  ```text
  SHADTEST name=gpu_solid_rt status=PASS samples=16 expected=ffffffff pitch=64 rt_bytes=16384 dcb_bytes=1524
  ```

  Три прогона подряд, все PASS, по 1–2 секунды каждый (без учёта сборки). Ни одного
  `<Critical>` в логе: [logs/gpu_solid_rt-lavapipe-PASS.log](logs/gpu_solid_rt-lavapipe-PASS.log).
- **Негативные контроли подтверждают, что оракул проверяет реальный вывод GPU**
  ([logs/controls.log](logs/controls.log)):
  - readback выключен (дефолтный конфиг shadPS4) → `FAIL reason=pixel_mismatch got=00000000`;
  - фрагментный шейдер пишет красный вместо белого → `FAIL ... got=ff0000ff`.
- Чтобы дойти до PASS, понадобилось 12 исправлений в пяти местах (см. таблицу ниже).
- **Всё внесено в репозитории** (ветки `claude/magical-ride-7m5mrn`). Вопрос с EOP
  пересмотрен по итогам ревью PR (см. строку 8): OpenGNM остаётся на upstream
  `4b295ca`, а в shadPS4 нужен двухстрочный патч из
  `kaaburgh/shadps4-open-test/patches/shadps4/`. Проверено со свежего клона: со
  стоковым shadPS4 runner возвращает `INFRA_FAIL` (3 из 3), с патченным — PASS
  (3 из 3, без `Critical`). Файлы в [patches/](patches/) — исторический срез первой
  итерации.

## Окружение

| Компонент | Версия |
|---|---|
| ОС | Ubuntu 24.04.4, 4 vCPU, 16 ГБ RAM, без GPU |
| shadPS4 | `dade3af` (2026-10-05), RelWithDebInfo, clang 19.1.1 + libstdc++ 14; для PASS — плюс патч обработки EOP `INT_SEL=3` |
| Vulkan | Mesa 25.2.8 lavapipe (llvmpipe, LLVM 20.1.2), Vulkan 1.4.318 |
| Дисплей | Xvfb 21.1.12 |
| OpenOrbis | v0.5.4 `toolchain-llvm-18.tar.gz` (sha256 из `deps.lock` совпал) |
| opengnm | `4b295ca` (upstream, без изменений) |
| opengnm-psbc | `a92a122` |
| shadps4-open-test | `ea584d9` + [patches/shadps4-open-test-lavapipe-baseline.patch](patches/shadps4-open-test-lavapipe-baseline.patch) |

## Что пришлось исправить

| # | Компонент | Симптом | Причина | Что сделано | Куда отдавать |
|---|---|---|---|---|---|
| 1 | opengnm-psbc | psbc не собирается из чистого клона: нет `util/format/u_format_gen.h` | Makefile не генерирует около 10 файлов кодогенерации Mesa, которые сам же перечисляет в `.gitignore` (`u_format_gen.h`, `u_format_pack.h`, `builtin_types.h/c`, `shader_stats.h`, `amd_cp_packets_gfx11/12.h`, `gfx10_format_table.c`, `vk_struct_type_cast.h`, …) | `tooling/psbc-codegen.sh` повторяет `custom_target()` из вендорных `meson.build`; вызывается из `bootstrap-deps.sh` | psbc upstream (Makefile) |
| 2 | opengnm-psbc | `pthread_barrier_t`, `asprintf` не объявлены; NEON-интринсики на x86 | Linux-конфиг ставит `-D_XOPEN_SOURCE=500`; Makefile жёстко включает `blake3_neon.c` (автор собирает на ARM64) | `-D_GNU_SOURCE`, `override BLAKE3_SRCS` без NEON, `BLAKE3_NO_*` в `tooling/opengnm-psbc-linux.mak` | psbc upstream + этот репозиторий |
| 3 | shadps4-open-test | `ld.lld: undefined symbol: sceVideoOutGetBufferLabelAddress` | `drawcommandbuffer.o` тянет `platform_orbis.o`, который импортирует VideoOut (было предсказано ревью) | `-lSceVideoOut` | этот репозиторий; в opengnm — документация или разрыв зависимости |
| 4 | shadps4-open-test | shadPS4 не загрузил бы ELF | `ld.lld` выдаёт `ET_DYN`/SysV; загрузчик shadPS4 (`loader/elf.cpp`) требует OSABI FreeBSD и `e_type` `0xFE00/0xFE10/0xFE18` | шаг `create-fself` из OpenOrbis, как в их сэмплах | этот репозиторий |
| 5 | shadps4-open-test | прогон висит, если используется только `config.toml` | shadPS4 `dade3af` читает `config.json`; при одном старом TOML показывает модальный SDL-диалог «Config Migration» | runner пишет `config.json` (`GPU.readbacks_mode=2`, `GPU.readback_linear_images_enabled=true`) | этот репозиторий |
| 6 | **shadPS4** | первый запуск в свежем каталоге пользователя висит бесконечно | `UserSettings.Load()` → `UserManager::CreateDefaultUsers()` (`user_manager.cpp:300`) → `AskMigrationOption()` показывает модальный «Save Migration» **безусловно**, даже когда мигрировать нечего; наличие старых сейвов проверяется только после диалога | runner заранее создаёт `home/1000/{savedata,trophy,inputs}` | **shadPS4 upstream**: спрашивать, только если старые сейвы или трофеи существуют |
| 7 | shadps4-open-test | shadPS4: `SearchBinaryInfo: Unreachable code! Shader binary info not found.` | тест копировал в GPU-память только код шейдера, без идущего следом футера `OrbShdr` (`ShaderBinaryInfo`), а shadPS4 ищет его рядом с кодом | `load_shader` копирует код вместе с футером, если тот идёт сразу за ним | этот репозиторий |
| 8 | **shadPS4** | shadPS4: `SignalFence: Unreachable code!` (`pm4_cmds.h:508`), эмулятор падает (`int3`); PASS успевал выйти **в гонке** с падением | `sceGnmDrawCmdEventWriteEop` ставит `INT_SEL=3` (`SEND_DATA_ON_CONFIRM`); обработчик `EVENT_WRITE_EOP` в shadPS4 знает только 0/1/2, хотя `RELEASE_MEM` значение 3 принимает | первая попытка меняла OpenGNM на `INT_SEL=2` (`SEND_INT_ON_CONFIRM`, добавляет прерывание) и откачена по ревью. Итог: патч shadPS4 `shadps4-open-test/patches/shadps4/` + runner отвергает `<Critical>` до маркера (код 2) | **shadPS4 upstream** |
| 9 | окружение | сборка shadPS4 падает: `std::ranges::to`, `std::optional` не найдены; `CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS-NOTFOUND` | на Ubuntu 24.04 clang-19 по умолчанию берёт libstdc++ 13; для C++23-модулей CMake нужен `clang-scan-deps` | `libstdc++-14-dev`, `clang-tools-19` | документация shadPS4 (`building-linux.md` этого не упоминает) |
| 10 | shadps4-open-test | (превентивно) | B4 из ревью (случайная предикация) и I3 (молча проглатываемые ошибки) | обнуление `cmd.flags`; обработчик сообщений opengnm и FAIL при его ошибках | этот репозиторий; B4 исправить в opengnm |
| 11 | shadps4-open-test | маркер захватывает ANSI-хвост `\x1b[m` | лог shadPS4 цветной | runner вырезает ANSI перед разбором | этот репозиторий |
| 12 | opengnm-psbc | на **свежем** клоне линковка psbc падает: `undefined reference to nir_intrinsic_infos`, `nir_op_infos`, `spirv_op_to_string`, … | списки исходников в Makefile строятся через `$(wildcard …)` при разборе, до кодогенерации, поэтому сгенерированные `.c` не компилируются. В рабочей копии это маскировал первый упавший запуск make | bootstrap сначала вызывает `make generated`, затем основную сборку. Найдено проверкой с чистого клона | psbc upstream (Makefile) |

Про B4: на shadPS4 мусорный бит предикации **сейчас не влияет на результат**. В
`liverpool.cpp` бит `predicate` у draw-пакетов не проверяется, а `SET_PREDICATION`
не реализован. На железе и после будущих изменений shadPS4 это может проявиться,
поэтому обход в тесте оставлен.

Сеть: FetchContent в spdlog тянет fmt архивом с `github.com/.../archive/*.tar.gz`. Прокси
этой среды отдавал на такие архивы 403, поэтому использован
`-DFETCHCONTENT_SOURCE_DIR_FMT=externals/fmt`. На обычной машине это, скорее всего, не нужно.

## Наблюдения о shadPS4 для тест-инфраструктуры

- **Гостевой `exit()` реализован как `UNREACHABLE_MSG("Exiting with status code {}")`**
  (`kernel/process.cpp:295`). Любая программа, вернувшаяся из `main`, завершает
  эмулятор по `SIGTRAP` (код 133), и код выхода наружу не передаётся. Решение
  research-документа не полагаться на exit code верное. Код при этом пишется в лог,
  и его можно использовать как второй оракул. Upstream-улучшение: чистое завершение
  с передачей кода.
- **Headless без X-сервера пока не работает.** С `SDL_VIDEO_DRIVER=offscreen` SDL
  инициализируется, но `vk_platform.cpp:120 CreateSurface` падает с «Presentation not
  supported on this platform». `WindowSystemType::Headless` в shadPS4 уже есть и
  используется при создании инстанса; не хватает ветки в `CreateSurface` (например,
  через `VK_EXT_headless_surface`, который lavapipe поддерживает). Пока Xvfb полностью
  закрывает задачу.
- Без `sce_module/libc.prx` и `libSceFios2.prx` shadPS4 пишет `<Error>`, но работает
  на HLE. Шум можно убрать, положив собранные OpenOrbis модули рядом с ELF.
- Последовательность первого запуска (диалоги, миграции конфига) — главный источник
  зависаний в CI. Каждый такой диалог стоит закрывать заранее подготовленным
  каталогом пользователя, а runner'у нужен общий таймаут (он есть).

## Как воспроизвести

```bash
# 1. Пакеты (Ubuntu 24.04)
sudo apt install clang-19 clang-tools-19 lld-19 llvm-19 libstdc++-14-dev ninja-build cmake \
    mesa-vulkan-drivers xvfb glslc python3-mako libvulkan-dev \
    libasound2-dev libpulse-dev libopenal-dev libssl-dev zlib1g-dev libedit-dev libudev-dev \
    libevdev-dev libjack-dev libsndio-dev libpng-dev libx11-dev libxext-dev libxrandr-dev \
    libxcursor-dev libxi-dev libxss-dev libxfixes-dev libxtst-dev libxkbcommon-dev \
    libwayland-dev wayland-protocols libdecor-0-dev

# 2. shadPS4 dade3af (около 40–60 минут на 4 ядрах)
git clone https://github.com/shadps4-emu/shadPS4 && cd shadPS4
git checkout dade3afda7ea3102faad8e39e616556a9cdde90b
git submodule update --init --recursive --depth=1
git apply /path/to/shadps4-open-test/patches/shadps4/*.patch   # EOP INT_SEL=3
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_C_COMPILER=clang-19 -DCMAKE_CXX_COMPILER=clang++-19 \
    -DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=/usr/bin/clang-scan-deps-19
cmake --build build --parallel "$(nproc)"

# 3. Тест (ветка с исправлениями; OpenGNM — upstream 4b295ca)
git clone -b claude/magical-ride-7m5mrn https://github.com/kaaburgh/shadps4-open-test
cd shadps4-open-test
bash scripts/bootstrap-deps.sh
bash scripts/build-test.sh gpu_solid_rt
SHADPS4=/path/to/shadPS4/build/shadps4 bash scripts/run-test-lavapipe.sh gpu_solid_rt
```

Без патча shadPS4 runner вернёт `HOST_RESULT INFRA_FAIL shadPS4 critical before marker`
(код 2): так и задумано.

## Что не проверено

- Только lavapipe. Настоящие GPU (AMD, NVIDIA) не проверялись; в research-документе
  это отдельный пункт.
- Реальная PS4 не проверялась. Какой `INT_SEL` эмитит Sony Gnm для
  `writeAtEndOfPipe` и даёт ли 3 прерывание на железе, не установлено.
- Одна ревизия shadPS4. Многие находки (диалоги, конфиг) завязаны на `dade3af` и
  могут измениться.
- lavapipe — это CPU-рендеринг, но shadPS4 всё равно ведёт RT через свой texture
  cache и readback. Оговорка README про комбинированный оракул (растеризация плюс
  readback) остаётся в силе, и контроль №1 это подтверждает.
- psbc выдаёт `SPIR-V WARNING: Unsupported SPIR-V capability: SpvCapabilityShader`.
  Это безвредно (шейдеры работают), но psbc стоит научить знать capability `Shader`.

## Кандидаты в upstream-задачи

shadPS4:

1. Не показывать «Save Migration», если старых сейвов и трофеев нет (блокирует CI).
2. Гостевой `exit(status)`: чистое завершение с кодом вместо `UNREACHABLE`.
3. Headless-путь: ветка `CreateSurface` для `WindowSystemType::Headless` (`VK_EXT_headless_surface`).
4. `EVENT_WRITE_EOP INT_SEL=3`: обрабатывать вместо `UNREACHABLE`, как уже делает
   `RELEASE_MEM` (готовый патч: `shadps4-open-test/patches/shadps4/`).
5. `building-linux.md`: для Ubuntu 24.04 упомянуть `libstdc++-14-dev` и `clang-tools-19`.

opengnm:

6. Документировать или разорвать зависимость `platform_orbis.o` от `libSceVideoOut`.

opengnm-psbc:

8. Makefile: генерировать все выходы кодогенерации Mesa и делать это до раскрытия
   `$(wildcard)`; x86_64-сборка без `blake3_neon.c`; Linux-конфиг без `_XOPEN_SOURCE=500`.
