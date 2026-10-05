# Ревью opengnm: пригодность как основы для OSS-тестов и репро для shadPS4

**Цель, под которую делалось ревью.** Собирать собственные свободно
распространяемые ELF для запуска на shadPS4:

1. OSS-тесты эмулятора;
2. в будущем — база для воспроизведения багов из игр: нашли баг в игре →
   локализовали → собрали минимальный репро на opengnm → передали сообществу
   на фикс → репро становится регрессионным тестом.

**Ревизии.**

| Что | Ревизия |
|---|---|
| opengnm (этот форк) | `4b295ca`, совпадает с `PS4-OpenGNM/opengnm@main`: форк от апстрима не отличается |
| shadPS4 (для сверки HLE) | `shadps4-emu/shadPS4@dade3af` (2026-10-05) |
| OpenOrbis toolchain (только заголовки) | `OpenOrbis/OpenOrbis-PS4-Toolchain@HEAD` |

> **Обновление 2026-10-05: эмпирическая проверка.** Тест `gpu_solid_rt` из
> `kaaburgh/shadps4-open-test` собран и прогнан на shadPS4 `dade3af` с lavapipe:
> PASS, негативные контроли дают FAIL. Подробности, патчи и логи:
> [SHADPS4_LAVAPIPE_BASELINE.md](SHADPS4_LAVAPIPE_BASELINE.md). По итогам прогона
> добавлен блокер [B6](#-b6-event_write_eop-с-int_sel3-роняет-shadps4) (EOP с
> `INT_SEL=3` роняет shadPS4) и уточнён B4: на shadPS4 бит предикации сейчас
> игнорируется. Ошибка линковки без `-lSceVideoOut` из ответа про `gpu_solid_rt`
> подтвердилась.

---

## TL;DR

Как **фундамент для тест-сьюта и репро** opengnm в текущем виде брать нельзя.
Взять из него стоит отдельные части: `gpuaddr`, бинарные раскладки структур,
заголовки регистров, хелперы DirectMemory/VideoOut. Главные проблемы:

1. **Библиотека в рантайме определяет, эмулятор это или железо, и ведёт себя
   по-разному** (`opengnm_is_hle_runtime()`). Тест, прошедший на PS4, на
   shadPS4 исполняет другой командный поток.
2. **Неявно перемешаны два пути: вызов HLE (`libSceGnmDriver`) и локальная
   генерация PM4.** Кроме того, 177 из 251 экспорта `libSceGnmDriver`, которые
   реализует shadPS4, *определены* прямо в `libopengnm.a` и перекрывают HLE.
3. **Нет `prepareFlip`.** Значит, `sceGnmSubmitAndFlipCommandBuffers`, основной
   путь показа кадра в играх, роняет shadPS4 на `ASSERT`. Пример из
   документации не работает сразу по трём причинам.
4. **`sceGnmCmdInit` не инициализирует `flags`.** Бит предикации у draw-пакетов
   получается случайным, и отрисовки становятся недетерминированными (есть PoC).
5. **В репозитории нет OSS-пути к шейдерам.** Встроенный ассемблер GCN не умеет
   даже `exp`, а shadPS4 требует от шейдера футер `OrbShdr` с уникальным хэшем.
6. Host-тесты (88 шт.) проверяют **generic-бэкенд, а это не тот PM4, который
   уходит в shadPS4**. Часть тестов тавтологична, CI для кода нет, CMake-сборка
   на Linux сломана.

Лицензия: репозиторий помечен как MIT, но `src/hwinit_sequences.h` — почти
дословная копия GPL-2.0-or-later кода shadPS4. Хорошая новость: этот файл
попадает **только в host-сборку**, в orbis-библиотеку (и, значит, в ELF) он не
входит (проверено поиском байтов в собранном `libopengnm.a`). Подробности в
[I1](#i1-лицензия-и-происхождение-кода).

---

## Как проверял

- Собрал generic-бэкенд CMake'ом (gcc 13 и clang 18): **падает на линковке**.
  Собрал через `./build.sh tests` (Makefile): все 88 тестов проходят.
- Прогнал тесты под ASan+UBSan (gcc): чисто.
- Скомпилировал **orbis-бэкенд** локальным clang 18 с заголовками OpenOrbis
  (Docker-демона в окружении нет, поэтому libc-заголовки хоста) и разобрал
  таблицу символов `libopengnm.a`: что определяется локально, а что
  импортируется.
- Сверил с HLE shadPS4: `src/core/libraries/gnmdriver/gnmdriver.cpp`,
  `gnmdriver_init.h`, `src/video_core/amdgpu/regs_shader.h`,
  `src/core/libraries/kernel/process.cpp`.
- Проверил, что все импорты orbis-сборки из libkernel и VideoOut реализованы
  в HLE shadPS4. Реализованы все 23.
- Написал PoC на неинициализированный `flags`: [`poc/uninit_flags.c`](poc/uninit_flags.c).

На реальном PS4 и в самом shadPS4 ничего не запускалось: в окружении нет ни
консоли, ни GPU.

---

## Что хорошо и стоит сохранить

- **`gpuaddr`** (tiling/surface/AddrLib-математика) с тестами; по словам автора,
  сверено с RPCSX. Для тестов текстур и RT это самая ценная часть.
- **Бинарные раскладки** `GnmRenderTarget`, `GnmDepthRenderTarget`,
  `GnmTexture`, `GnmBuffer`, `Gnm*StageRegisters`, `GnmShaderBinaryInfo` с
  `_Static_assert` по размерам.
- **Заголовки регистров** `include/pm4/sid.h` и `amdgfxregs.h` из Mesa (MIT).
- **Хелперы** `sceGnmDirectMemoryAllocate`, `sceGnmVideoOut*`,
  `sceGnmTexCreate2d`, `sceGnmRtCreateColorTarget`: удобная обвязка для тестов.
- Host-тесты чистые под санитайзерами.
- Все функции libkernel/VideoOut, которые импортирует orbis-сборка
  (`sceKernelAllocateDirectMemory`, `sceKernelGetModuleList`,
  `sceVideoOutSubmitFlip`, …), есть в HLE shadPS4.
- Модули рантайма для PKG (`libc.prx`, `libSceFios2.prx`, `right.sprx`)
  OpenOrbis собирает из своих исходников (`src/modules/`), так что их можно
  распространять. freegnm, из которого портирован generic-драйвер, под MIT.

---

## Находки

Уровни: 🔴 блокирует цель, 🟠 важно, 🟡 мелочь или гигиена.

### 🔴 B1. Поведение на эмуляторе и на железе расходится by design

`src/driver_orbis.c:181-251`, `:989-1045`

`opengnm_is_hle_runtime()` перебирает загруженные модули через
`sceKernelGetModuleList`. Если модуля с подстрокой `GnmDriver` нет, библиотека
считает, что работает под HLE-эмулятором. Дальше:

- на **железе** `SetEmbeddedVsShader`/`SetEmbeddedPsShader` эмитят PM4 локально,
  с адресом шейдера из прошивки (`SPI_SHADER_PGM_LO_VS = 0x0fe000f1`, то есть
  `0xfe000f100`);
- на **shadPS4** те же вызовы уходят в HLE `sceGnmSetEmbedded*Shader`.

Почему это блокер:

- Регрессионный тест должен гонять **один и тот же** командный поток на эталоне
  (PS4) и на эмуляторе. Здесь это не так по построению.
- Детектор хрупкий. В shadPS4 (`kernel/process.cpp:244-264`)
  `sceKernelGetModuleList` возвращает `ENOMEM`, если модулей больше, чем
  помещается в массив. opengnm даёт массив на 128 элементов
  (`driver_orbis.c:200`) и при ошибке выбирает «реальное железо». Тогда в
  эмулятор уйдёт PM4 с адресом шейдера из прошивки `0xfe000f100`, которого в
  процессе нет: shadPS4 упадёт при чтении кода шейдера (или в
  `SearchBinaryInfo`, `regs_shader.h`, `UNREACHABLE_MSG("Shader binary info
  not found.")`). Для маленьких тестов 128 модулей недостижимы, но то же
  случится, если shadPS4 однажды начнёт показывать HLE-библиотеки в списке
  модулей.
- Обоснование «на FW 9.00 `sceGnmSetVsShader` падает в контексте GPU-помпы
  Eden» относится к ошибке в стороннем проекте (Eden), а не к GNM.

**Рекомендация.** Убрать рантайм-детект. Путь задавать явно, на этапе
компиляции или в вызове:

- `firmware`: всегда вызывать `sceGnm*` из `libSceGnmDriver`, как делают игры
  через `libSceGnm`;
- `raw`: генерировать PM4 локально.

Для тестов эмулятора по умолчанию нужен `firmware`.

### 🔴 B2. Неявная смесь «HLE или локальный PM4», и перекрытие HLE-экспортов

`src/driver_orbis.c`, `src/drawcommandbuffer.c`

Высокоуровневый API `sceGnmDrawCmd*` в orbis-сборке ведёт себя по-разному:

| Уходит в `libSceGnmDriver` (на shadPS4 это HLE) | Генерируется локально (HLE этого не видит) |
|---|---|
| `DrawIndex`, `DrawIndexOffset`, `DrawIndex/Indirect(Multi, CountMulti)`, `DrawInitDefaultHardwareState350`, `InsertWaitFlipDone`, `SetEmbedded*Shader` (только под HLE, см. B1), `Submit*` (их вызывает сам пользователь) | `DrawIndexAuto`, `SetVsShader`, `SetPsShader`/`350`, `SetCs/Gs/Es/Hs/LsShader`, `DispatchDirect/Indirect`, все сеттеры регистров, `EventWriteEop`, `Fill/CopyMemory`, `WaitMem`, stream-out, query |

Из исходников это видно только по комментариям. Для тест-сьюта критично
знать, *что* проверяет конкретный тест: HLE `gnmdriver` или парсер PM4 и
рендерер (`liverpool`).

Кроме того, по таблице символов orbis-сборки **177 из 251 экспорта
`libSceGnmDriver`**, которые регистрирует shadPS4, **определены внутри
`libopengnm.a`**. Среди них `Sdma*`, `Sqtt*`, `Spm*`, `Debugger*`,
`Register*/Unregister*Resource`, `Validate*`, `DrawOpaqueAuto`,
`DrawIndexMultiInstanced`, `DrawIndirectCountMulti`, `ComputeWaitSemaphore`,
`InsertThreadTraceMarker`, `Razor*` и 39 `Func_XXXXXXXXXXXXXXXX`. Объект
`driver_orbis.o` попадает в линковку всегда, потому что `drawcommandbuffer.o`
нужны его `sceGnmDriver*`. После этого любой вызов перечисленных функций из ELF
идёт в локальную заглушку, а не в эмулятор.

- Таблица заглушек («Part 3: Retail firmware stubs», `driver_orbis.c:1055+`)
  снята с **заглушек shadPS4** (`LOG_ERROR "(STUBBED)"`, «Not available in
  retail firmware»), а в комментарии подаётся как «поведение retail-прошивки».
  Пример: `sceGnmDrawOpaqueAuto` в прошивке настоящая функция (stream-out draw).
  В shadPS4 сейчас это заглушка, возвращающая `ORBIS_OK`, а в opengnm заглушка
  возвращает `ORBIS_GNM_ERROR_FAILURE`. Как только shadPS4 реализует такую
  функцию, тесты на opengnm до реализации не дойдут.
- Ещё **214 собственных функций** opengnm названы с префиксом `sce`
  (`sceGnmDrawCmd*`, `sceGnmDriver*`, `sceGnmVideoOut*`,
  `sceGnmDirectMemory*`, `sceGnmTexCreate2d`, `sceGnmPlatInit`, …). Префикс
  `sceGnmDriver*` у внутренних функций совпадает с пространством имён
  настоящих экспортов (`sceGnmDriverCaptureInProgress`,
  `sceGnmDriverInternalRetrieveGnmInterface`, …). По логам shadPS4 и по NID
  становится трудно понять, что HLE, а что код теста.

**Рекомендация.**

- В orbis-сборке не определять ни одного символа, который экспортирует
  `libSceGnmDriver`: пусть резолвятся через NID-заглушки OpenOrbis.
- Собственный API opengnm переименовать (например, `ognm*`).
- В каждом тесте декларировать, что он покрывает: HLE-путь или PM4-путь.

### 🔴 B3. Нет `prepareFlip`, поэтому `SubmitAndFlip` на shadPS4 падает; пример в документации не работает

- В opengnm нет API `prepareFlip`.
- HLE shadPS4 `sceGnmSubmitAndFlipCommandBuffersForWorkload` → `PatchFlipRequest`
  (`gnmdriver.cpp:2090+`) ищет пакет `0xc03e1000` (NOP на 64 dword с
  `PrepareFlipLabel`) в последних 64 dword DCB и падает на `ASSERT_MSG("Can't
  find prepareFlip packet")`, если его нет. Судя по коду ошибки
  `ORBIS_GNM_ERROR_SUBMISSION_AND_FLIP_FAILED_INVALID_COMMAND_BUFFER`
  («prepareFlip has not been called», `src/error.c:18-19`), прошивка тоже
  требует этот пакет.
- `docs/guides/rendering-pipeline.md:205-290` («Complete Frame Loop»)
  показывает `SubmitAndFlip` без `prepareFlip`, и это не единственная проблема
  примера:
  - размер DCB передаётся как `cmd.sizedwords * 4` (строки 194, 208, 270).
    `sizedwords` — это **ёмкость** буфера, а не заполненная часть, так что GPU
    получит мусор после `cmdptr`. Правильно:
    `(cmd.cmdptr - cmd.beginptr) * 4`;
  - `uint32_t cmdMem[65536];` (строка 254): 256 КиБ на **стеке CPU**. На
    железе это не GPU-видимая direct memory.
- `docs/examples/hardware-smoke.md` описывает шаги, которых нет в
  `tests/hardware_smoke.c`: создание RT, `SubmitAndFlip`, буфер на 256 КБ.

**Почему это блокер.** Игры показывают кадр именно через
`SubmitAndFlip`/EOP-flip. Без `prepareFlip` воспроизвести этот путь (а на нём
много багов: синхронизация, флипы, метки) нельзя.

**Рекомендация.** Добавить `ognmDrawCmdPrepareFlip()` (и вариант с меткой и
EOP) с раскладкой, которую ожидает `PatchFlipRequest`. Поправить документацию.
Добавить пример с флипом, проверенный на shadPS4.

### 🔴 B4. `sceGnmCmdInit` оставляет `flags` неинициализированным, отсюда случайная предикация draw

`src/commandbuffer.c:26` (`GnmCommandBuffer res;`) заполняет только указатели,
callback и `sizedwords`. Поля `flags`, `_unused` и `_unused2` содержат мусор со
стека. Draw-функции копируют `cmd->flags.predication_enabled` в бит предикации
PKT3 (`src/drawcommandbuffer.c:174, 199, 249, …`).

PoC: [`poc/uninit_flags.c`](poc/uninit_flags.c), gcc 13, `-O0`, «грязный» стек:

```
flags.predication_enabled=1  PKT3 header=0xc0012d01 predicate_bit=1
```

Ожидалось `0xc0012d00`. На `-O2` получилось `0`, но это случайность.

Почему это важно: draw с предикацией GPU пропускает, если предикация активна.
Недетерминированные отрисовки убивают доверие к тестам. На orbis-пути мусор
попадает в `DrawIndexAuto` (PM4 генерируется локально) и во `flags`, которые
передаются в прошивку.

**Рекомендация.** `GnmCommandBuffer res = {0};` плюс тест с «грязным» стеком,
как в PoC.

**Уточнение после прогона на shadPS4.** Эмулятор бит `predicate` у draw-пакетов
сейчас не проверяет (`liverpool.cpp`), а `SET_PREDICATION` у него не реализован.
Поэтому на shadPS4 `dade3af` B4 на результат не влияет. Блокером он остаётся
из-за железа и будущих версий эмулятора: тест, зелёный на shadPS4, может
оказаться недетерминированным на PS4.

### 🔴 B5. В репозитории нет OSS-пути к шейдерам

- Ассемблер GCN (`src/gcn/assembler.c`) внутренний (заголовок не
  устанавливается) и умеет только форматы **MUBUF, SMRD, SOP1, SOPP, VOP2**.
  Он нужен для fetch-шейдеров. Без `EXP` нельзя написать ни VS, ни PS, а без
  `VOP3`, `VOPC`, `MIMG`, `DS` даже простой CS писать неудобно.
- Компилятор `opengnm-psbc` (SPIR-V → GCN на Mesa NIR+ACO) живёт в отдельном
  репозитории и тяжёлый как зависимость для тестов.
- shadPS4 ищет в коде шейдера футер `ShaderBinaryInfo` с сигнатурой
  `"OrbShdr"` (`regs_shader.h`, `SearchBinaryInfo`, лимит 0x4000 dword). Если
  футера нет, получаем `UNREACHABLE` (подтверждено прогоном: `gpu_solid_rt`
  копировал в GPU-память код без футера и упал именно так). Поле `shader_hash` из футера служит
  ключом кэша. Значит, **каждому тестовому шейдеру нужен уникальный хэш**,
  иначе тесты будут мешать друг другу через кэш.
- Шейдеры из игр — чужая собственность, класть их бинарники в репро нельзя.
  Репро должен *переписать* проблемный шейдер в минимальный эквивалент.

**Рекомендация.** Завести лёгкий OSS-путь:

- писать на ассемблере GCN и собирать через LLVM
  `llvm-mc -triple=amdgcn -mcpu=bonaire` (GFX7; для Pro gfx8 — проверить),
  либо через psbc;
- добавить маленькую утилиту, которая дописывает к коду валидный футер
  `OrbShdr` (длина и хэш, например, xxhash от кода) и генерирует
  `Gnm*StageRegisters`.

Без этого единственные доступные шейдеры — встроенные (fullscreen VS и dummy
PS), а они проходят через B1.

### 🔴 B6. `EVENT_WRITE_EOP` с `INT_SEL=3` роняет shadPS4

`src/drawcommandbuffer.c:1750` (`sceGnmDrawCmdEventWriteEop`) и `:2030`
(`writeZpassDoneEop`, occlusion query)

Найдено при прогоне `gpu_solid_rt` на shadPS4 + lavapipe
([SHADPS4_LAVAPIPE_BASELINE.md](SHADPS4_LAVAPIPE_BASELINE.md)).

- opengnm при любой записи данных ставит в EOP
  `EOP_INT_SEL(EOP_INT_SEL_SEND_DATA_AFTER_WR_CONFIRM)`, то есть `INT_SEL=3`
  (значение взято из Mesa).
- shadPS4 (`pm4_cmds.h`, `SignalFence`) обрабатывает `INT_SEL` 0, 1 и 2, а 3
  называет `IrqUndocumented`: в играх оно не встречается. На 3 срабатывает
  `UNREACHABLE` → `Emulator::Shutdown()` + `int3`, эмулятор падает.
- Данные метки при этом успевают записаться **до** падения. Тест увидел метку и
  напечатал PASS в гонке с крашем (`<Critical> SignalFence: Unreachable code!`
  стоит в логе раньше `SHADTEST ... PASS`). Это ложно-зелёный результат: при
  другом тайминге маркер не вышел бы.
- Под удар попадает любой тест, который ждёт GPU через
  `sceGnmDrawCmdEventWriteEop` или использует occlusion query.

**Статус: исправлено в форке**, коммит `9a2a77a` в этой ветке. Оба места
переведены на `INT_SEL=2`, host-тесты проверяют литеральное значение поля (при
возврате к 3 они падают). `kaaburgh/shadps4-open-test` закрепил этот коммит в
`deps.lock`.

**Рекомендация.** Эмитить значение, которое встречается в реальных командных
потоках. Проверено: с `INT_SEL=2` (прерывание после подтверждения записи) тест
даёт 3 из 3 PASS без `Critical`
([patches/opengnm-eop-int-sel.patch](patches/opengnm-eop-int-sel.patch), там
исправлено только место в `EventWriteEop`). Какое именно значение эмитит Sony Gnm
для `writeAtEndOfPipe`, без железа не подтверждено. В shadPS4 стоит отдельно
сделать обработку 3 вместо краша.

### 🟠 I1. Лицензия и происхождение кода

- `LICENSE`: MIT. При этом `src/hwinit_sequences.h` прямо говорит, что
  «Extracted from shadPS4 (`src/core/libraries/gnmdriver/gnmdriver_init.h`)».
  У исходника `SPDX-License-Identifier: GPL-2.0-or-later`. Посчитал: из 13
  массивов (1363 dword) **10 побайтно идентичны**, а 3
  (`CTX_INIT_SEQUENCE_400*`) отличаются одним опущенным первым dword тела
  хвостового NOP. Его всё равно заполняет нулём паддинг, то есть по сути это
  та же копия.
- Ещё из shadPS4 взяты: таблица кодов возврата заглушек
  (`driver_orbis.c`, Part 3), регистры embedded-шейдеров
  (`s_embedded_vs_fullscreen` = `0x0fe000f1, 0, 0x000c0000, 4, 0, 4, 0`),
  объявления (`include/gnmdriver.h:17`: «Sourced from the shadPS4 reference
  declarations») и ожидаемые значения тестов (`tests/test_drawcmd.c:5`).
- `OPENGNM_REWRITE_PLAN.md` описывает реверс расшифрованных модулей прошивки
  FW 9.00 (`libSceGnmDriver.sprx` и др.) как «ground truth». Это не
  clean-room. Для OSS-проекта происхождение стоит хотя бы честно
  задокументировать.
- **Что попадает в ELF.** `hwinit_sequences.h` включается только в
  `driver_generic.c`, то есть в host-сборку. В orbis-`libopengnm.a` этих байтов
  нет (проверено). Из пересечений с shadPS4 в ELF уходят только тривиальные
  заглушки (`return ORBIS_GNM_ERROR_FAILURE;`) и 7 чисел регистров
  embedded-VS. Риск для самих ELF низкий; неверна маркировка репозитория.

**Рекомендация (не юридическая консультация).** Варианты:

- (а) перелицензировать форк под GPL-2.0-or-later, расставить SPDX и NOTICE.
  Это проще всего и совпадает с лицензией shadPS4: тесты можно будет без
  проблем отдать в shadPS4 или его организацию;
- (б) чтобы остаться на MIT: убрать `hwinit_sequences.h` (на orbis он и так не
  используется, init делает прошивка или HLE), а для host-тестов брать эталон
  из дампа с собственной консоли.

### 🟠 I2. Host-тесты проверяют не тот PM4, что исполняет эмулятор

- 88 тестов гоняют **generic-бэкенд** (`driver_generic.c`, порт из freegnm).
  Для вызовов, которые на orbis уходят в прошивку или HLE, generic эмитит
  *свой* PM4, и он расходится с HLE shadPS4. Например:
  - `sceGnmDriverDrawIndex` в generic ставит бит предикации из `flags`
    (`driver_generic.c:204`), а HLE `sceGnmDrawIndex` и `sceGnmDrawIndexAuto`
    в shadPS4 предикацию **никогда** не ставят («no predication will be set in
    the packet»);
  - `sceGnmDispatchInitDefaultHardwareState` в generic «simplified»: только
    `CLEAR_STATE` и NOP (`driver_generic.c:1314`).
- В `tests/test_api.c` 19 проверок вида
  `utassert(r == GNM_ERROR_OK || r == GNM_ERROR_CMD_FAILED)`: они принимают и
  успех, и ошибку. Это тесты линковки, а не поведения.
- Ожидаемые значения в `test_drawcmd.c` «derived from shadPS4 and freegnm»,
  а не сняты с железа.

**Рекомендация.**

- Дифференциальные тесты: собрать HLE-функции `gnmdriver` из shadPS4 (это
  обычные писатели PM4) в host-харнесс и сравнивать побайтно с выходом
  opengnm для одних и тех же входов.
- Где можно — golden-значения, снятые на реальной PS4.
- Тавтологичные проверки убрать или переименовать в «linkage».

### 🟠 I3. Ошибки по умолчанию проглатываются молча

- `sceGnmWriteMsg`/`sceGnmWriteMsgf` (`src/error.c:56-73`) ничего не делают,
  пока не установлен обработчик через `sceGnmSetMessageHandler`. Обработчика
  по умолчанию нет.
- Сеттеры регистров (`setcontextregisterrange` и др.,
  `src/drawcommandbuffer.c:33-139`) не вызывают `cmdcanfit` и resize-callback.
  При нехватке места они пишут сообщение (в никуда, см. выше) и
  **возвращаются, не записав состояние**. `SetViewport`, `SetScreenScissor`,
  `SetPsInputUsage`, `SetDepthClearValue` и другие вообще не проверяют место
  заранее.
- В итоге при переполнении буфера тест молча исполнит поток без части
  состояния.

**Рекомендация.** Добавить в `GnmCommandBuffer` липкий флаг ошибки, а в
тест-харнессе проверять его перед submit. Сделать обработчик по умолчанию,
который пишет через `sceKernelDebugOutText`.

### 🟠 I4. `hardware_smoke` — это не тест рендеринга и не годится для CI

`tests/hardware_smoke.c`

- Картинку на экране **заливает CPU** (`fill_status`, `draw_digit`). GPU-часть
  такая: init state, `DrawIndexAuto(0)` (ноль вершин), EOP-запись метки. Ни
  шейдеров, ни RT, ни реального draw. Утверждение «PS4 hardware run passes»
  значит «GPU исполнил буфер и записал 64-битную метку».
- `sceGnmCmdInit(mapped, kCommandDwords, …)` (строка 343): второй аргумент —
  размер **в байтах**, а передаётся число dword (4096). Буфер в 4 раза меньше
  задуманного.
- Результат виден только как цвет экрана и `printf`. При успехе `hold()`
  крутится бесконечно, процесс не завершается, и CI не может узнать исход.
- Таймауты сделаны счётчиком итераций спина (`100000000`), а не временем.

**Рекомендация.** См. раздел «Как строить тест-сьют»: протокол результата,
выход из процесса, таймауты по `sceKernelGetProcessTime`, ожидание EOP через
equeue.

### 🟠 I5. Сборка, CI, воспроизводимость

- **CMake на Linux не собирается**: тестам не линкуется `libm`
  (`log2` в `src/gpuaddr/tilemodes.c:280` и `surface.c`). Инструкция из
  `README.md` («Host (generic, for testing)») падает и на gcc, и на clang.
  Makefile-путь (`./build.sh tests`, clang) работает, потому что
  `config.generic.mak` добавляет `-lm`.
- `CMakeLists.txt:105`: `target_compile_options(opengnm PRIVATE -w)` глушит
  **все** предупреждения.
- **CI для кода нет**: `.github/workflows/docs.yml` собирает только mkdocs.
- Остатки монорепо:
  - `build.sh:78` монтирует в Docker **родительский** каталог (`$ROOT_DIR:/work`);
  - `build.sh:154` вызывает `$ROOT_DIR/tools/setup_openorbis_llvm18_macos.sh`
    вне репозитория;
  - `Makefile:140` берёт иконку и `right.sprx` из
    `../freegnm-examples/videoout-linear`.
  Собрать PKG из одного этого репозитория нельзя.
- `stage_hw_smoke_pkg.sh` и `PS4_PACKAGING.md` хардкодят `10.0.1.157`.
- `make install` ставит **только заголовки** (`Makefile:84`), хотя README
  предлагает `make install DESTDIR=$OO_PS4_TOOLCHAIN` для установки
  библиотеки.
- CMake ставит `platform.h`, `gpuaddr.h`, `gnm.h`, `pm4/*.h` прямо в
  `${prefix}/include`, где возможны коллизии имён; Makefile ставит в
  `include/opengnm/`. Раскладки установки расходятся.
- Для orbis в CMake нет toolchain-файла.
- `driver_orbis.c:83` пишет лог в `/data/eden_ps4_runtime.log` (остаток
  проекта Eden).
- `VULKAN_PS4_PLAN.md` описывает соседний проект `vulkan-ps4`, а не opengnm.

### 🟠 I6. Пробелы в API, критичные для репро

Публичного «escape hatch» нет: нельзя записать произвольный
context/SH/uconfig-регистр или сырой PM4-пакет. Нет сеттеров для:

- `VGT_SHADER_STAGES_EN` (шейдеры GS/HS/ES/LS выставить можно, а включить
  стадии нельзя);
- stencil (`DB_STENCIL_CONTROL`, `DB_STENCILREFMASK`);
- `CB_COLOR_CONTROL` (режимы MRT/ROP);
- `PA_SU_POLY_OFFSET_*`, `DB_DEPTH_BOUNDS_*`, `PA_CL_CLIP_CNTL`, alpha-to-mask,
  MSAA (`PA_SC_AA_CONFIG`), primitive restart, line/point size;
- `SET_PREDICATION`, `CONTEXT_CONTROL`;
- базы аргументов `DispatchIndirect`.

Для репро багов из игр escape hatch важнее любого высокоуровневого сеттера:
он позволяет перенести минимальный кусок реального PM4-потока как есть.

### 🟡 Мелочи

- `getuserdataslot` (`src/drawcommandbuffer.c:1380`): `maxregs` считается в
  байтах (`endreg - basereg + 4` = 64), а сравнивается с номером слота (их
  16). Сейчас это маскируют проверки `GNM_MAX_*_USERDATA_SLOTS` в
  вызывающих функциях, но функция сама по себе неверна.
- `sceGnmDrawCmdEventWriteEop` (`:1755`): для `GNM_CS_DONE`/`GNM_PS_DONE`
  используется пакет `EVENT_WRITE_EOP` с `event_index` 6. Это EOS-события,
  для них предназначен `EVENT_WRITE_EOS`. Стоит сверить с железом.
- `sceGnmDrawCmdWaitMem` не проверяет выравнивание адреса на 4.
- `sceGnmDrawCmdSetScreenScissor` (`:799`) приводит `int16_t[4]` к
  `uint32_t*`: нарушение strict aliasing.
- `GNM_NUM_SHADER_STAGES = 8`, а массивы в `getuserdataslot` заполнены на 7.
- `sceGnmPlatGetBufferLabelAddress` (orbis) не проверяет `outaddr` на NULL.
- `sceGnmGpuMode` (orbis) лениво инициализирует статику без атомиков. Гонка
  безвредная, но в многопоточных тестах её стоит учитывать.
- Клон неглубокий (50 коммитов), полной истории изменений не видно.

---

## Как строить тест-сьют и репро на этой базе

Предлагаемый порядок работ.

1. **Решить вопрос лицензии** (I1). Для экосистемы shadPS4 естественнее всего
   GPL-2.0-or-later.
2. **Сделать поведение детерминированным:** B4 (`flags`), B1 (убрать детект
   HLE), I3 (липкий флаг ошибки и обработчик сообщений по умолчанию).
3. **Развести пути явно** (B2):
   - *API-тесты* вызывают `sceGnm*` из `libSceGnmDriver` так же, как это делают
     игры. Они проверяют HLE `gnmdriver` в shadPS4;
   - *PM4-тесты* собирают поток вручную через escape hatch (I6). Они
     проверяют парсер `liverpool`, рендерер и recompiler шейдеров.
   Из orbis-сборки убрать определения символов `libSceGnmDriver`, а
   собственный API переименовать в `ognm*`.
4. **Тонкий тест-харнесс** (на замену `hardware_smoke`):
   - командные буферы, RT и буферы данных только в direct memory;
   - ожидание завершения GPU через EOP-метку и
     `sceKernelWaitEqueue`/`sceGnmAddEqEvent`, таймаут по
     `sceKernelGetProcessTime`;
   - **машиночитаемый результат**: строка
     `OGNM-TEST <name> PASS|FAIL <detail>` через `sceKernelDebugOutText` и
     `printf`, затем завершение процесса с кодом, без бесконечного `hold()`;
   - проверка результата через readback: CRC или хэш содержимого RT/буфера
     сравнивается с golden-значением, один раз снятым на реальной PS4 и
     закоммиченным рядом с тестом. Для эмулятора, если нужно, допустимы
     отклонения;
   - headless по умолчанию (offscreen RT), VideoOut и `SubmitAndFlip` только в
     тестах, которые проверяют флип (нужен B3).
5. **OSS-шейдеры** (B5): `llvm-mc` (или psbc) плюс утилита, которая дописывает
   футер `OrbShdr` с уникальным хэшем.
6. **CI** (I5): host-тесты (CMake и Makefile, санитайзеры), кросс-сборка ELF
   в образе OpenOrbis, затем прогон ELF в headless shadPS4 с парсингом строк
   `OGNM-TEST`.
7. **Процесс репро.** shadPS4 умеет дампить шейдеры (`dumpShaders`), но
   штатного дампа PM4 на `dade3af` нет. Стоит добавить в shadPS4 опцию дампа
   командных буферов (DCB/CCB, плюс диапазоны памяти, на которые ссылаются
   регистры). Тогда репро строится так: минимизировать захваченный PM4,
   **переписать** шейдеры (а не копировать бинарники игры), положить в
   `tests/repro/<issue>/` вместе с golden-результатом с железа.

---

## Предлагаемая разбивка на задачи (под issues)

| # | Задача | Находки | Приоритет |
|---|---|---|---|
| 1 | Инициализировать `GnmCommandBuffer` целиком, добавить тест с «грязным» стеком | B4 | 🔴 |
| 2 | Убрать рантайм-детект HLE; явный режим `firmware`/`raw` | B1 | 🔴 |
| 3 | Не определять экспорты `libSceGnmDriver` в orbis-сборке; переименовать собственный API в `ognm*` | B2 | 🔴 |
| 4 | Добавить `prepareFlip` и пример `SubmitAndFlip`; исправить `rendering-pipeline.md` и `hardware-smoke.md` | B3 | 🔴 |
| 5 | OSS-путь к шейдерам: `llvm-mc`/psbc и генератор футера `OrbShdr` | B5 | 🔴 |
| 5a | ~~EOP: заменить `INT_SEL=3` в `EventWriteEop` и `writeZpassDoneEop`~~ исправлено в `9a2a77a` | B6 | ✅ |
| 6 | Решение по лицензии, SPDX, NOTICE, документирование происхождения | I1 | 🟠 |
| 7 | Дифференциальные тесты против HLE shadPS4; убрать тавтологичные проверки | I2 | 🟠 |
| 8 | Липкий флаг ошибки в командном буфере, обработчик сообщений по умолчанию | I3 | 🟠 |
| 9 | Тест-харнесс: протокол `OGNM-TEST`, выход из процесса, readback и golden | I4 | 🟠 |
| 10 | Починить CMake (`libm`, убрать `-w`), CI, убрать зависимости от соседних репозиториев | I5 | 🟠 |
| 11 | Escape hatch для сырых регистров и пакетов; недостающие сеттеры состояния | I6 | 🟠 |
| 12 | Мелкие исправления из списка 🟡 | 🟡 | 🟡 |
