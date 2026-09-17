# Walnut Integration Progress

**Project:** WEGW_1605_4_RL — weware application on the walnut-ZX (GW1NS) SDK
**Reference:** common-gateway 1.5 (`gateway_repos/v3/newArchMain/common-gateway-1.5/modem/firmware`)
**Date:** 2026-09-08 · **Build:** ✅ green end-to-end — all weware_app objects compile with zero warnings, `customer_app.elf` links with zero undefined references, `APP sign DONE!`, and the signed `customer_app.bin` is staged to `kernel/GW1NS-4` (re-verified 2026-09-08 11:39, includes both the UART and SMS work).

> **Correction to the earlier "packaging can't run / bin is stale" note.** `ctools.exe` is **not** missing — it lives in `Code/sdk/tools/win32/` (with `dtools.exe`), which `tools/core_launch.bat` puts on PATH; `Tools/aboot` is present too. The packaging step was failing only because of two Git-Bash artifacts when `build.bat` is launched from a bash shell:
> 1. the exported PATH puts Git's `/usr/bin` ahead of `C:\Windows\System32`, so `core_launch.bat`'s `find /C /I` runs **Unix** `find` recursively over `C:\` — that is the 15+ minutes of `Permission denied` noise, after which PATH is never actually extended, so `dtools`/`ctools` are "not recognized";
> 2. `NoDefaultCurrentDirectoryInExePath` is set, so `call build.bat` cannot even be resolved from the current directory.
>
> Launch it with `C:\Windows\System32` prepended to PATH, that variable cleared, and an absolute path to `build.bat`, and the whole build (compile → link → mkappimg → appSign → stage) finishes in **~20 seconds**. From a normal `cmd.exe` prompt `.\build.bat` works unchanged.

**Latest change (2026-09-15) — SDK renamed to the `wm_sdk_*` convention.** The kernel archives now export the SDK only under the new names (`lib_wmsrc_B.a` has zero bare `sdk_*` symbols) and `wm_src/sdk/inc/` is `wm_sdk_*.h`, so `weware_app` was migrated to match: APIs `sdk_* → wm_sdk_*`, enums/macros `SDK_* → WM_SDK_*`, types `Sdk* → wm_Sdk*` — 1614 replacements over 72 files, error codes included (`SDK_RESULT_SUCCESS` → `WM_SDK_RESULT_SUCCESS`, `SdkResult` → `wm_SdkResult`). A name was rewritten only when its `wm_`-prefixed counterpart is genuinely declared in `wm_src/sdk/inc/*.h`, which is what leaves the CG/reference namespace alone: the shim **file** names (`sdk_platform.h`, `sdk_functionality_*`, `sdk_walnut_*`), the `SDK_GET_TICKS`/`SDK_TASK_SLEEP`/`SDK_DEBUG_PRINT`/`SDK_SYSTEM_RESET` macros (bodies now call `wm_sdk_*`), `sdk_task_ref_t`, `sdk_msg_t`, `sdk_https_returncode_t`, `SDK_NETCONN_EVT_*`, `SDK_HTTPS_METHOD_*`, `SdkTcpFunctionalityOps`/`SdkHttpsFunctionalityOps`/`SdkTcpEventCallback` and the CG `sdk_tcp_*` abstraction all keep their names. Two spots needed judgement: **(a)** the `sdk_tcp_*` shadowing trick documented in §3 / `sdk_functionality_tcp.h` is now moot — the kernel member is `wm_sdk_tcp.c.obj` exporting `wm_sdk_tcp_*`, so the two namespaces no longer collide and renaming the CG side would have *created* the duplicate definition it was avoiding; the header rationale was rewritten and the "never include `wm_sdk_tcp.h` alongside this header" warning dropped. **(b)** the five CG HTTPS names remapped by `sdk_functionality_https_compat.h` (`sdk_https_get_response`, `_get_response_len`, `sdk_https_download_configure_ssl` / `_get_file_size` / `_read_chunk`) keep the CG spelling because their kernel namesakes use the opposite success convention; `sdk_walnut_https.c` no longer needs its `#undef` block and calls `wm_sdk_https_download_get_file_size` / `_read_chunk` directly. **No behavioural change. Compiles and links** — full rebuild, one pre-existing warning (`ota_sink_app_package` unused under `OTA_DOWNLOAD_TEST`), `customer_app.elf` with zero undefined references. Names quoted in the historical notes below predate this rename.

**Latest change (2026-09-07) — UART manager ported:** CG `module/uart/*` + its `common/stm_binary_protocol.*` / `common/stm_command_map.*` dependencies replicated and the `MODULE_ID_UART` registry slot enabled with CG's flags (`UART_REQUEST_Q`, capacity 5). All business logic — the IDLE→SENDING→WAITING_RESPONSE→RESPONSE_RECEIVED state machine, the LEN-based binary frame reassembler with `0xAA` resync, every STM response→user-string builder, opcode correlation of replies to the single in-flight request, and the PING-STM probe — is byte-identical to CG. Only the SDK-facing calls differ (full mapping table in §3.7 and in the `uart_manager.c` file header); the one structural adaptation is the RX path: CG's `sdk_uart_register_callback` handed the callback a byte *count* and the handler then called `sdk_uart_read()`, whereas walnut's `sdk_uart_set_rx_callback` hands over the *buffer* (and `sdk_uart_read()` is a documented `SDK_RESULT_NOT_SUPPORTED` no-op), so the malloc+read+free round-trip per RX chunk is gone. `UART_UNAVAILABLE` is now undefined, so `STM:`-prefixed commands are translated to binary frames via `stm_cmd_build_from_ascii` (hex passthrough fallback) and PING-STM arms `uart_manager_start_ping_test()`. Two branches are gated out pending their own ports: `HEALTH_PACKET_UNAVAILABLE` (no type-40 health push on `system_alive`) and `FILE_TRANSFER_UNAVAILABLE` (STM-OTA / peri-OTA acks have nowhere to route). **Compiled + linked; on-target verification pending** (see §6).

**⚠️ Current state (2026-09-07) — the `sdk_walnut_uart` adapter was REMOVED on request, so UART init fails again.** With no local `sdk_uart.h` implementation the prebuilt kernel wrappers are linked, and those discard the `port` argument and ask `drvUartOpen(0)` — the CP debug console, which the driver always refuses. So `uart_manager_init()` returns `ERR_UART_CONFIG_ERROR` on every retry (a failed init, **not** a reset; the device stays up). The module is left `enabled = TRUE`. Everything else — the manager, `stm_binary_protocol`, `stm_command_map`, the `STM:`/PING-STM path — compiles and links; the only missing piece is an `sdk_uart.h` implementation that can reach **drvUart port 2**. A blocker banner with the disassembly evidence and the port map now sits at the top of `uart_manager.c`. The port findings below remain valid and are the answer to "which UART can the app use".

**Port findings (2026-09-07, still valid) — user-configurable UART is drvUart index 2.** The reset was caused by `DRV_UART_PORT_4` (index 3), which has **no UART behind it** on this SoC. The application's free, full-duplex port is **index 2 = `DRV_UART_PORT_3`, base `0xd401f000`**, now exposed as `WALNUT_UART_PORT_USER` in `inc/module/uart/sdk_walnut_uart.h`. Index 0 is the CP/Seagull debug console (ASR boot logs + `RTI_LOG`) and `drvUartOpen` refuses it outright; index 1 is the AT-command channel (`uart_atcmd_init` drives `atuartcfg` @ `0x7e2392d4`, port field initialised to 1, baud 115200). The USB port (flash + `sdk_log_*`/`wm_printf`/`sdk_debug_print`) is USB CDC, not a drvUart port, so it never collides. `sdk_walnut_uart.c` was rewritten as the walnut **implementation of `sdk_uart.h`** and now honours its `port` argument, so the port is selected through the API (`UartConfig.port`) rather than a private define; it refuses ports 0 and 3 at runtime. Full table in §3.7. **Not runtime-tested.**

**Superseded status note (2026-09-07) — UART was briefly re-gated OFF after the reset.** The `drvUart*` adapter below opened `DRV_UART_PORT_4` and **reset the unit on boot**. Everything UART is reverted to a stable state: `sdk_walnut_uart.c` is out of the build, `MODULE_ID_UART` is `enabled = FALSE` again, and `UART_UNAVAILABLE` is restored — the image now contains **no** UART code (verified: 0 occurrences of `sdk_walnut_uart.c.obj` / `uart_manager.c.obj` in `customer_app.map`). Cause, from `kernel/GW1NS-4/cp.axf`: `drvUartOpen` only checks `port < 4`, but the SoC has **three** UARTs (`bspUartNumOfUartsVar == 3`, no `Uart4ClockOnOff`), so index 3 writes an unclocked peripheral → bus fault → reboot. **Usable ports:** index 0 is rejected by the driver itself (CP debug console), index 3 resets, leaving **index 1 (`DRV_UART_PORT_2`, 0xd4018000, the AT channel — risky)** and **index 2 (`DRV_UART_PORT_3`, 0xd401f000 — recommended; the header calls it "BT uart" but no Bluetooth stack is linked, so nothing owns it)**. The adapter's default is now `DRV_UART_PORT_3` with `_Static_assert`s refusing ports 0 and 3. Full table in §3.7.

**Follow-up (2026-09-07) — the walnut `sdk_uart_*` wrappers are unusable; replaced with an in-app driver adapter.** First on-target run failed with `[UART] E: ERR_UART_CONFIG_ERROR: UART port config failed` / `[MODULE] E: UART Manager init failed` on repeat. Root cause found by disassembling the prebuilt `sdk_uart.c.obj` (lib_wmsrc_B.a): `sdk_uart_set_config()` validates `port == 0` and then calls **`drvUartOpen(0, &cfg)` with a hardcoded literal** — and `components/inc/drv_uart.h` documents port 0 as `DRV_UART_PORT_1 = 0, ///< debug uart , APP can not use`. That port is the kernel log console, already open, so `drvUartOpen` returns NULL → `SDK_RESULT_ERROR`. **No argument the caller can pass fixes this** — the wrapper ignores `port` beyond requiring it to be 0. (The other `-1` path, `validate_board != 1`, is ruled out: `sdk_file_open`/`sdk_gps`/`sdk_tcp`/`sdk_ota` carry the identical gate and all work on this unit.) Fix: new `src/module/uart/sdk_walnut_uart.c` implements the same five `sdk_uart_*` entry points over the kernel-exported `drvUart*` driver (all 11 symbols are in `core_stub.o`) with the physical port selectable via `WALNUT_UART_STM_PORT`, defaulting to `DRV_UART_PORT_4`. It deliberately **shadows** the kernel wrappers — the same mechanism `sdk_functionality_tcp.h` documents for TCP — and `uart_manager.c` is unchanged apart from comments. ⚠️ **`WALNUT_UART_STM_PORT` is an assumption, not a verified fact:** `PORT_1` is the console, `PORT_2` is the cellular AT channel (do **not** use), `PORT_3` is the BT uart, `PORT_4` is general-purpose — confirm against the board and switch to `DRV_UART_PORT_3` if the STM is on the BT pins.

**Also fixed (2026-09-07):** `ota_manager_get_status_string` was declared in `ota_manager.h` and called from `cmd_get_ota_status` but never defined (the walnut `ota_manager.c` deliberately omitted it along with `ota_reason_str`) — a pre-existing **undefined reference at link**, unrelated to UART. Both functions are now ported; only the `file_transfer_get_active_progress()` live-progress branch stays omitted, so `GET-OTA-STATUS` reports the last-reason form.

**Latest change (2026-09-07) — SMS manager ported:** CG `module/sms/*` + `module/urc/urc_sms_queue_types.h` replicated line for line and the `MODULE_ID_SMS` registry slot enabled with CG's flags. Only the SDK-facing calls differ (full mapping table in §3.6 and in the `sms_manager.c` file header): walnut's `sdk_sms_send` is blocking/text-mode-only, `sdk_sms_init()`/`sdk_sms_msg_free()` are walnut extras, and inbound SMS arrives as an `SDK_SMS_EVT_INCOMING` queue event (raw `+CMGR` text already read by the kernel) instead of a `+CMTI` URC through `urc_processor` — the SMS task parks those into the same module `urc_q` CG used, so the CG pipeline runs unchanged. Command replies route back to the sender's number through the existing `utils_route_response_to_module` path. **Compiled + linked; on-target verification pending** (see §6).

**Latest change (2026-08-26) — storage fix:** on-target log showed `[FILESYS] E: write_file: open failed path='C:/preboot/weware_preboot.bin' mode=wb+` / `[PREBOOT] E: tcpq_clr ... ret=-1` repeating every loop. Root cause: `flash_paths_ensure_directories` (CG-verbatim) passed `FLASH_DIR_*` **with the trailing slash** (`"C:/preboot/"`) to `sdk_file_exists`/`sdk_file_mkdir`; on walnut these hit the kernel `fs_stat`/`fs_makedir` unmodified (via `wm_create_folder`) and the kernel FS rejects trailing-slash directory paths (the vendor libc wrapper `components/libc_wrap/c_wrap.c` strips it explicitly; vendor demo and `gps_storage` mkdir `"C:/wegwdir"`/`"C:/config"` without one). So none of `C:/config|fota|preboot|queue` were created and every write under them failed. Fix: `flash_paths.c` normalizes the name (`flash_paths_dir_no_slash`) → exists → mkdir → verify, and reports RESULT_ERROR + `[FLASHP]` log if a dir is still missing; `file_system_write_file` self-heals a missing parent dir once and retries the open. **Compiled + linked; on-target verification pending** (see §6).

---

## 1. Status at a glance

| Module / layer | Ported | Fidelity vs CG | Biggest gap |
|---|---|---|---|
| System infra (events, queues, config store, module framework, reset, storage, time) | ✅ 2026-08-20 | Near-verbatim via compat shims | Board pins / ADC channels unconfirmed |
| GPS (manager, ops, triggers, config, packet, storage, NMEA parse) | ✅ 2026-08-21 | Triggers/packet/storage/NMEA-parse ~100 % logic parity (HDOP + RMC/GGA cross-check live). **NMEA source + fix + packet behaviorally validated 2026-09-16** — `fix_real=1 … sats=9 hdop=989` at 1 Hz on a BK1616P receiver, packet sent and ACKed (§6) | 5 packet fields placeholder; msg_q/BLE append dead; constellation mask is GPS+GLO by product requirement, below the receiver's GPS\|BDS\|GLO\|GAL ceiling (§6 follow-up ①) |
| TCP (state machine, ops, login, socket layer) | ✅ 2026-08-21 | 15-state FSM + login packet + store-and-forward send path full parity; recv → command_manager (CG-verbatim, 2026-08-27). **Store-and-forward behaviorally validated 2026-09-08** (offline backlog survives network-timeout reset, drains login → backlog ascending → live, verified server-side) | Recv/command path over TCP and 100 s idle keepalive not runtime-tested |
| Network (16-state manager, config, health check) | ✅ 2026-08-21 | State enum, 4-way health check, timeout table, EVENT_RESET_SOFT escalation, NetworkConfig persistence — CG parity | state_timeout framework hand-rolled; CFUN settle 2+3 s vs CG 1 s |
| SIM (detect + debounce, events) | ✅ 2026-08-21 | Debounce verbatim; EVENT_SIM_* broadcasts + connected tracking | CPIN-4/6 removal detection (CG network_ops) not ported |
| URC processor | ✅ 2026-08-21 | Drain loop + fan-out (CG pattern on bare event codes) | SMS inlining / NMEA assembler moot (kernel emits no such URCs) |
| Vehicle state (ign/motion debounce) | ✅ 2026-08-20 | Logic verbatim | Accelerometer inputs stubbed FALSE |
| Main / boot flow | ✅ 2026-08-20 | CG structure (system → modules → supervised loop) | Init failures log-and-continue (CG reboots) |
| Command manager (manager, handler, config) | ✅ 2026-08-26 compiled | CG command table + waterfall state machine verbatim; API renamed to walnut (`weware_tcp_*`, `weware_sim_*`, `weware_network_*`) | The CG-verbatim binary-frame `STM:` path + PING-STM are written and compile, but `UART_UNAVAILABLE` is **restored** (2026-09-07) pending the STM port, so both still reply "unavailable". DIGOUT likewise gated via `DIGOUT_UNAVAILABLE`; not runtime-tested |
| SMS (manager, config, URC queue types) | ✅ **2026-09-08 working on target** | Task loop / URC→read→validate→forward→delete pipeline / `+CMGR` parser / SIM-gated modem config / stats / init-deinit **line-for-line**; inbound feed re-sourced (see §3.6). Inbound → command → SMS reply verified end-to-end (§3.6.6) | walnut `sdk_sms_send` is blocking + text-mode only (no per-message format/len/msgq) — 60 s task-stall watchdog risk on a slow network still unmeasured; SIM re-insert path untested |
| UART (manager + `sdk_walnut_uart` adapter + `stm_binary_protocol`, `stm_command_map`) | ⚠️ 2026-09-07 compiled, **DISABLED** | State machine / frame reassembler / response builders / opcode correlation / PING-STM **byte-identical**; RX path re-sourced and the SDK's broken `sdk_uart_*` replaced by an in-app `drvUart*` adapter (see §3.7) | Adapter **out of the build** + module `enabled=FALSE` + `UART_UNAVAILABLE` restored, after `DRV_UART_PORT_4` reset the device. Usable ports now identified (idx 1 or **idx 2**); re-enable in 3 steps once the STM's port is confirmed. Also `HEALTH_PACKET_UNAVAILABLE` + `FILE_TRANSFER_UNAVAILABLE` |
| BLE, OTA (partial), digout, accel, file-transfer | ❌ not ported | — | See §5 |

---

## 2. System infrastructure (ported 2026-08-20)

The five reference files supplied (`module_config.c`, `module_manager.c`, `system_config.c`, `system_manager.c`, `time_utils.c`) plus their dependency web are now in the tree.

**Compat shims** — `inc/sdk_platform.h` and `inc/functionality/sdk_functionality_{file,os,system,network}.h` map the reference's `SDK_*` macros and `sdk_functionality_*` includes onto walnut's native `sdk_os/sdk_file/sdk_system/sdk_network` API. This let the following port **verbatim** (unchanged CG logic):

- `common/event_manager.c` — pub/sub bus, all `EVENT_*` types
- `common/queue_manager.c` — ring buffers + file-backed overflow + reboot persistence (incl. TCP compact record format and resume-offset via pre_boot record)
- `common/state_timeout.c`, `common/task_stats.c`
- `config/config.c` — versioned binary config blob (`C:/config/system_config.dat`, `CONFIG_FILE_VERSION 4`), per-module defaults→load→validate→revert
- `module/module_manager.c` — init retry rounds (6 × 10 s), required-module gate, task-stall monitor (60 s), connected tracking via events, reboot-data persist
- `system/reset/{pre,post}_boot_handler.c`, `reset_handler.c` — deferred soft/hard reset with pre-boot persistence (`C:/preboot/weware_preboot.bin`)
- `system/storage/flash_paths.c`, `file_system.c`, `system/time_utils.c`

**Adapted (walnut deltas):**

| File | Delta vs CG |
|---|---|
| `module/module_config.c` | Full CG `ModuleId` order kept; **enabled:** LOG, CMD, URC, SIM, NETWORK, TCP, GPS, SMS, SYSTEM. **Disabled slots:** UART, BLE. TCP_SEND_Q config staged but `msg_q = NULL` until TCP consumes it |
| `system/system_config.c` | ign/motion thresholds + parser verbatim; TCP `ign_det_*` migration/writeback dropped (walnut `WewareTcpConfig` has no such fields); `accel_en` defaults **FALSE** (no accel driver) |
| `system/vehicle_state.c` | Debounce/anchor/stop-hold logic verbatim; accel reads stubbed FALSE; debounced edges now **drive the walnut GPS latches** (`gps_manager_set_ignition/motion_status`) in addition to event broadcasts |
| `system/system_manager.c` | ADC poll via new `adc_manager` (mV→V over `sdk_adc`); charge edge also drives `gps_manager_set_charge_status`; **netlight = GW1NS on-board blue LED, GPIO 69 active high** (2026-09-08) driven by its own `NETLED` task at 500 ms — solid ON when `weware_network_is_connected()`, 500 ms blink while searching/lost (WEMAIN can't drive it: 1 s sleep + inline OTA); status/power-select GPIO still **disabled until pins provided**; A-GPS *not* driven here (walnut GPS task does it itself); OTA/digout/accel are TODO stubs |
| `module/log/*` | CG `log.h` compile-time-filtered `LOG_*` macros kept; backend `log_printf` → `RTI_LOG` (AP UART console); async ring not ported |
| `system/adc/adc_manager.c`, `system/gpio/gpio_manager.c` | New thin wrappers over `sdk_adc` / `sdk_gpio` |
| `common/types.h` | CG `Result` enum (numerically compatible with `SdkResult`, so registry casts work as in CG) |
| `system/storage/flash_paths.c`, `file_system.c` (2026-08-26) | **Walnut FS delta:** directory paths must reach `sdk_file_mkdir`/`sdk_file_exists` **without a trailing '/'** (kernel `fs_makedir` rejects it; SIMCOM tolerated it). `flash_paths_ensure_directory()` strips + verifies; `file_system_write_file` creates a missing parent dir once and retries. Abstraction note for Stage 2: this path normalization belongs in the walnut file adapter (`sdk_file_mkdir`/`sdk_file_exists` wrappers), not in CG business code. |

**Integration wiring added:**
- All five task loops (gps, urc, network, sim, tcp) feed `module_manager_update_uptime()` → main-loop stall watchdog broadcasts `EVENT_RESET_SOFT` (persisted reboot).
- Network connected/disconnected bridged to `EVENT_NETWORK_*` via `weware_network_register_status_callback` (slot was free; the SIM slot is owned by `network.c`).
- `weware_main.c`: `system_manager_init()` → `module_manager_init()` → supervised loop (`system_manager_loop_iteration` + task watchdog).
- ⚠️ `-DLOG_GLOBAL_LEVEL` is not passed by the build; CG log macros default to INFO ceiling with per-file `LOG_MODULE_LEVEL` (mostly ERROR).

---

## 3. Per-module CG parity detail

### 3.1 GPS — highest-fidelity port

- **Triggers (`gps_triggers.c`): 100 % verbatim.** IGN interval (mot ? ign-on : ign-off), angle (20°), distance (50 m, with ign-interval second gate), reason codes 1–13, latch semantics — identical.
- **Packet (`gps_packet.c`): all 55 byte offsets identical.** Real: fix, UTC (BE), lat/lon ×1800000, speed km/h, course ×100, alt ×10, sats, network/TCP state diagnostic bytes, CSQ/MCC/MNC/LAC/CID, ign/charge/SIM/motion status bits, packet counter, XOR over bytes 3–51. **Placeholders:** input-wire mV, digout, external mV, battery % (system_manager already computes it — cheapest fix), accel orientation. **HDOP byte live as of 2026-08-21** (NMEA parse ported).
- **Storage (`gps_storage.c`): on-disk format bit-identical** (`GPSV` magic, v2). Post-boot decision (power-on + RTC valid → load, else clear) verbatim; reset reason via `sdk_get_reset_reason()` ('N' = power-on).
- **Ops (`gps_ops.c`):** jump filter (500 km/h, 5-sample streak), time-source AUTO chain, speed filter/backfill, send-kind decision (charge/fix/last-valid-merged), fix transitions, config retry ladder — all verbatim. Charge/ign/motion inputs come from latches (now fed by vehicle_state/system_manager) instead of `PowerInfo`.
- **Config (`gps_config.c`):** 17-key parser + validate-then-apply verbatim; walnut adds `gps_config_defaults_applied()` so `config_load_from_file` values survive init.
- **NMEA parse family ported 2026-08-21 (CG URC-build parity):** the kernel DOES expose raw NMEA — `sdk_gps_set_nmea_callback()` (sdk_gps.h) delivers complete sentences from the GNSS parser task. Feed: gps_manager.c pairs RMC+GGA (CG's urc_processor pairing; no 768-byte fragment reassembler needed — sentences arrive whole) → `GPS_URC_Q` (capacity 4). Parse: the full CG family verbatim in gps_ops.c — combined GNRMC/GNGGA parser, RMC↔GGA 200 m cross-check, HDOP ×100 (packet byte now real), RMC time+date→unix, drain-keep-newest, 20-cycle stall recovery (re-registers the callback vs CG's NMEA-by-URC re-enable). **Strict CG structure: the NMEA queue is the sole source — empty queue = parse fail → 60-fail reboot escalation.** The validated navdata poll is kept as a compiled-out backup: build with `-DGPS_QUERY_NAVDATA_POLL` to revert (mirrors CG's `#if SDK_URC_GNSS_MASK` split). Walnut deltas: talker accepts `$GP` alongside `$GN`; queue element has no `sdk_msg_t` header.
- **SDK NMEA chain audited 2026-09-09 (disassembly — `sdk_gps.h` has no in-tree implementation).** Prompted by a 1 Hz `GPS query and parse failed` stream on target that turned out to be a run with the **GNSS module not switched on** (empty queue = the documented parse-fail path; no defect, no change made). What the audit established, from `sdk_gps.c.obj` (`lib_wmsrc_B.a`) and `wm_gps_secure.c.obj` (`lib_wmsrc.a`):
  - `sdk_gps_init` carries the same `validate_board == 1` gate as `sdk_uart`/`sdk_ota`/`sdk_file` (passing on this unit — ADC and OTA work); on pass it calls `wm_gps_init` and registers `prv_gps_fix_cb`/`prv_gps_nmea_cb` via `wm_gps_set_fix_cb`/`wm_gps_set_nmea_cb`. `sdk_gps_set_nmea_callback` merely stores into `s_gps_nmea_cb` when `s_gps_inited`, else returns **-4** (`NOT_INITIALIZED`) — a case `gps_manager_nmea_feed_register` already logs, so registration failure is distinguishable from a silent feed.
  - `wm_gps_init` → mutexes + 16-deep `osiMessageQueue` + `gps_task` (4 KB, prio 226) + `wm_nmea_reset` + `wm_gps_power_on`. `wm_gps_power_on` → `wm_GPS_EN(0)`/15 ms/`wm_GPS_EN(1)`, pin-mux check (`GPS_UART_TX/RX_MUX`), attach `gps_uart_rx_cb`, `wm_gps_uart_init` (115200 8N1), then the receiver's ASCII config: `$POLCFGSYS,193` (0xC1 = GPS|GLO|GAL), `$POLCFGNAV,1` (1 Hz), `$POLCFGMSG,0,<id>,<0|1>` per sentence — **GGA (0) ON, RMC (5) ON**, GSA (1)/GSV (2)/VTG (3) OFF — then `$POLCFGMSG,1,…` (`wm_gps_start`).
  - **The receiver is therefore configured to emit exactly the RMC+GGA pair `gps_manager.c` pairs on**, so the feed → `GPS_URC_Q` → combined-parse path needs nothing changed once the module is powered. RX: `gps_uart_rx_cb` feeds bytes into `wm_nmea_feed`; a complete line is `osiMalloc`'d and posted to `gps_task`, which invokes the raw-sentence callback (whole sentences, hence no fragment reassembler).
  - Message-id map (`wm_lib/Inc/wm_gps_secure.h`): GGA 0, GSA 1, GSV 2, VTG 3, RMC 5, ZDA 20 — the ids to pass if `wm_gps_set_sentence` is ever driven directly.
- **Missing vs CG:**
  2. GPS module msg_q (`gps_ops_pop_gps_msg_queue` stubbed) ⇒ BLE send-with-GPS append and trigger reason 5 dead.
  3. ~~Login-with-GPS append~~ ✅ 2026-08-21 — called from TCP `SENDING_LOGIN` when `loginwithgps` set.
  4. GPS event broadcasts (`EVENT_GPS_CONNECTED/DISCONNECTED/CONFIGURED`); parse-fail escalation calls `sdk_system_reboot()` directly instead of `EVENT_RESET_SOFT` (now fixable — event/reset infra exists).
  5. Boot ign/mot seed hardcoded TRUE/TRUE (CG snapshots PowerInfo) — first vehicle_state edge corrects it.
  6. `agps_ref` gate not honored at the call site (moot: walnut A-GPS open returns NOT_SUPPORTED); A-GPS calls run in the GPS task against CG's "system loop only" rule.
  7. `gps_config_set` doesn't call `config_save_to_file` yet (RAM-only changes) — infra now exists to wire it.

### 3.2 TCP

- **State machine:** identical 15-state enum, identical success/error/busy target for every arm. Timeout table matches incl. the 100 s `SENDING_DATA` keepalive (restored 2026-08-21); remaining deltas: CLOSED/RECONNECT_DELAY timeouts go to INIT (CG: CLOSED); CONNECTING timeout is config-driven.
- **Login packet: bit-for-bit identical** (24 B, IMEI BCD, FW/HW swap, session LE, XOR span 18 B).
- **ACK handling:** application logic verbatim (`tcp_bytes_sent/acked`, SENDACKED accumulation, race pre-check). Source differs: CG gets vendor netconn acks; walnut synthesizes SENDACKED from `SO_SNDBUF` drain in the **walnut-only TCPMON socket-poll layer** (`sdk_walnut_socket_poll.c` — synthesizes the whole `SDK_NETCONN_EVT_*` stream over lwIP BSD sockets). Without SO_SNDBUF, "stack accepted" counts as acked.
- **Send path ported 2026-08-21 (CG store-and-forward, full parity):**
  - `TCP_SEND_Q` (capacity 7, ModuleMessage, file overflow 200 KB, persist-on-reboot) created by `weware_tcp_init` / destroyed in deinit (CG `tcp_create_queue` pattern).
  - `SENDING_DATA`: `queue_peek_batch`(5) → `tcp_combine_messages`(512 B) → `live_first` tail-prepend (config key now read) → send; rows popped only in `ACK_RECEIVED` (`tcp_pop_send_queue_batch_and_free` + `queue_persist_clear_snapshot_after_consume`). Unacked rows survive reconnects and persisted reboots.
  - GPS producer (`gps_ops_push_tcp_position_message`) is now CG-verbatim `queue_push` — packets are absorbed while TCP is down instead of dropped. `weware_tcp_send` kept as a generic queue_push wrapper.
  - `EVENT_TCP_CONNECTED/DISCONNECTED` broadcast on edges (module_manager TCP tracking now fires).
  - `login_with_gps` appends last-valid GPS to the login (buffer 64→79 B).
  - `SENDING_DATA` 100 s keepalive restored (idle queue cycles the connection, CG parity; `reset_overall` now FALSE like CG).
- **Still missing vs CG:**
  1. ~~Recv side routes to `weware_tcp_register_rx_callback`~~ **Done 2026-08-27:** `tcp_try_recv_once_and_dispatch` is CG-verbatim — builds a `ModuleMessage` (TCP→CMD, address `ip:port`) and calls `command_manager_accept_request`; rx-callback API removed. Response path (`utils_route_response_to_module` → type-38/39) already present, so TCP commands are end-to-end (pending on-target test).
  2. Default server is stage `13.126.118.139` (CG prod `65.1.190.236`); hostname allowed (CG IPv4-only).
- **Config:** get/set/get_string parity minus `ign-det` key; `config_save_to_file` persistence not wired yet.

### 3.3 Network

- **16-state machine 1:1** (same names/order); URC semantics identical (positive URC: DISCONNECTED→INIT; NETDIS: CONNECTED→DISCONNECTED; PDP_DEACT→DISCONNECTED). CFUN escalation ladder verbatim (+3 s settle added).
- **Improvements over CG:** IPv6 accepted, `SDK_NET_REG_DENIED`→ERROR handled, GET_IP `connected` set only on SUCCESS (fixes a CG bug), CONNECTED gated radio refresh.
- **Gap closure 2026-08-21 (CG parity):**
  - **4-way CONNECTED health check** ported verbatim: 10 s stabilize grace after entry, then CREG+CGREG+CGATT+IP every 65 s (`network_health_check` reusing the pure state-check handlers); any failure → DISCONNECTED. Replaces the walnut CGATT-only/3-bad-poll scheme.
  - **`network_config.c` ported** (defaults/validate/get_storage/set_apn/set/get_string, all CG-verbatim incl. the 5-key validate-then-apply parser): `NetworkConfig` (apn/user/pass/cid/auto) is now a **stored module config** — loaded by `config_load_from_file` before init, saved by `network_config_set/_set_apn` via `config_save_to_file`. `weware_network_set_apn` persists. PDP setup/activate/deactivate/get-IP all use `g_network_config.cid`. **CONFIG_FILE_VERSION bumped 4→5** (blob layout changed; old files delete-and-recreate).
  - **Overall-timeout escalation → `EVENT_RESET_SOFT`** broadcast (reference `network_on_overall_timeout`): queues/preboot state now persist before the reboot; CHECK_SIM still exempt. Was a state-losing direct `sdk_system_reset()`.
  - **Timeout table aligned to CG:** CHECK_REG 150 s, ACTIVATE_PDP 30 s, RESTART_CFUN 60 s, CHECK_CTZU 60 s, SET_CTZU 30 s; SIM_INSERT/DISCONNECTED rows removed (CG has none — overall timeout covers them).
  - **SIM presence via event manager:** registers `EVENT_SIM_AVAILABLE/UNAVAILABLE` handlers (CG live behavior: available → RESTART_CFUN unconditionally — walnut previously carried CG's commented-out SIM_INSERT variant); the single-slot `weware_sim` callback is freed.
  - Note: CG's `NETWORK_REG_POLL_DELAY_CYCLES` ("registration poll throttle") is defined but **never used** in CG — dropped from the gap list, no throttle is parity.
- **Remaining deltas (accepted):** `state_timeout`/task_stats frameworks hand-rolled equivalent; CFUN settle 2 s+3 s (CG 1 s); RESTART_CFUN state-write guard (walnut safety fix).
- ~~URC feed doubly registered~~ **Consolidated 2026-08-21:** urc_processor is now the sole kernel URC registrant; network's `netUrcQ` removed. Network creates its module `urc_q` (`NET_URC_Q`, queue_manager, capacity 8) in init and drains it with `queue_pop` — exactly the CG `network_manager.c` pattern. Elements are bare `urcEvent_e` codes (walnut URCs carry no payload). URC latency now bounded by the URC task's 200 ms idle sleep (same as CG).

### 3.4 SIM

- Debounce verbatim (1 s poll × 3 matches). Detect source swapped to modem `sdk_sim_get_status()` (GPIO path compile-gated). Hotswap absent **in both** CG and walnut.
- **Gap closure 2026-08-21 (CG parity):** `weware_sim_set_sim_available` now drives `module_manager_set_connected(MODULE_ID_SIM)` (SIM presence = connected state, CG semantics) and `sim_notify_sim_status_change` broadcasts `EVENT_SIM_AVAILABLE/UNAVAILABLE` (network consumes them; the single-slot callback is kept as a walnut extra and is now unused).
- Remaining: SimConfig plumbing (CG's is an empty struct — no value until commands need it); CG's CPIN-4/6 removal detection lived in CG network_ops and is still not ported (kernel SIM URCs + polling cover removal).

### 3.5 URC processor

- Task + drain loop near-verbatim (1 ms first recv → 0 ms drain → 5/200 ms sleep; same constants). Registers mask 0xFFFFFFFF; walnut adds proper `sdk_urc_unregister` on deinit (CG lacks it).
- **Fan-out ported 2026-08-21** (CG `urc_process_message` adapted): event-code→module map (walnut URCs are bare `urcEvent_e`, no mask/payload) → `queue_push` into the module's `config.urc_q`. Routing: PDP/NET/NO_PDP_LONG/RADIO_REFRESH → NETWORK (consumes via `NET_URC_Q`); SIM_* → SIM (no `urc_q` yet — dropped with warning; SIM polls instead); USB_* → unrouted. urc_processor is the **sole kernel registrant**; every event still gets the named info log line.
- Deliberately not ported (kernel emits no such URCs): SMS `arg3` inlining, the 768-byte GNRMC/GNGGA reassembler. Both feeds were found elsewhere: NMEA via `sdk_gps_set_nmea_callback()` → GPS module pairing (see §3.1), inbound SMS via the SDK's `SDK_SMS_EVT_INCOMING` queue event (see §3.6). Both bypass the URC processor entirely.

### 3.6 SMS (ported 2026-09-07)

CG source replicated line for line: `module/sms/sms_manager.{c,h}`, `module/sms/sms_config.{c,h}`, `module/urc/urc_sms_queue_types.h`. Task loop, statistics, `sms_manager_send/_delete/_set_format_mode`, `sms_send_internal`, `sms_process_urc`, `sms_read_message`, `sms_extract_fields` (the `+CMGR: "stat","addr",,"ts"\r\n<body>` parser incl. the not-NUL-terminated-safe length walk), `sms_configure`, `sms_on_sim_status`, `sms_manager_init/_deinit` — all unchanged in structure, control flow and ordering.

**SDK API mapping** (CG `functionality/sdk_functionality_sms.h` → walnut `sdk_sms.h`; also documented in the `sms_manager.c` file header):

| CG API | Walnut | Note |
|---|---|---|
| `sdk_sms_read` / `_delete` / `_delete_all` / `_set_format` / `_set_charset` / `_set_new_msg_ind` / `_msgq_poll` | identical signatures | used verbatim |
| `sdk_sms_send(format, msg, len, recipient, msgq)` | `sdk_sms_send(number, text)` | **different:** walnut is **blocking**, text-mode only, body length from the NUL terminator, no msgq. `format_mode` still honoured — applied to the modem via AT+CMGF in `sms_configure`. Payload is copied out of the length-delimited `ModuleMessage` into a bounded NUL-terminated buffer |
| `sdk_platform_register_sms_ops` / `SdkSmsFunctionalityOps` | **missing** | walnut has no functionality dispatcher for SMS; app calls the kernel `sdk_sms_*` directly |
| `SDK_SMS_MAX_ADDRESS_LENGTH` | **missing** | defined in `sms_manager.h` with the CG compat value (21) |
| `sdk_msg_t`, `SDK_MSG_URC`, `SDK_URC_SMS_MASK`, `SDK_URC_NEW_MSG_IND` | **missing** | queue element is `SdkSmsMessage`; the new-message discriminator is `type == SDK_SMS_EVT_INCOMING` |
| `sdk_memory_free(msg.arg3)` | `sdk_sms_msg_free(&msg)` | walnut extra; frees the heap-owned `text` |
| — | `sdk_sms_init()` | **walnut extra, required:** brings up the vendor SMS task/queue and registers the incoming hook. Called from `sms_configure()` once per SIM insert, after the queue is attached |
| — | `sdk_sms_get_storage_status()` | walnut extra; **now used** by the delete-all workaround below |

**Inbound path — the one structural adaptation.** CG: `urc_processor` pushed the raw `+CMTI: "SM",12` line into the SMS module `urc_q`; the task popped it, `atoi`'d the index after the last comma, then `sdk_sms_read()` fetched the body. Walnut: the kernel reads the message itself and the SDK posts `SdkSmsMessage{type=SDK_SMS_EVT_INCOMING, index, text=<raw +CMGR response>}` to whichever queue was last attached by `sdk_sms_msgq_poll()` — i.e. the same `g_temp_msg_queue` that carries read/delete results. So:
- `sms_flush_temp_queue()` **parks** incoming events into the module `urc_q` instead of discarding them (CG could drop freely — its temp queue only held operation results). Text is copied inline (`sms_urc_queued_t.arg3_inline`, 256 B vs CG's 72 B, sized for a full `+CMGR` header + 160-char body) so the ring buffer stays heap-free, exactly as in CG.
- The task then drains `urc_q` → `sms_process_urc()` — the identical CG flow, with `hdr.text = arg3_inline` replacing CG's `hdr.arg3 = arg3_inline`.
- `sms_process_urc` takes the index from the event field (CG parsed it out of the `+CMTI` text) and parses the already-supplied `+CMGR` text directly; `sms_read_message()` is retained and still used whenever the event carries no usable `+CMGR` text.
- `sms_read_message()` gained a park-and-retry recv loop: a stray `SDK_SMS_EVT_INCOMING` can arrive on the shared queue before the read result, so it is parked and the wait continues (CG did a single recv on a results-only queue). Storage selector: CG passed the literal `1`; walnut's `1` is `SDK_SMS_STORAGE_ME` while received messages live in SM, so the named `SDK_SMS_STORAGE_SM` is used.

**Other walnut deltas:** `LOG_ERRC(ERR_SMS_*, …)` → `LOG_ERROR` with the CG error-code name kept in the text and the `/* ERRC */` markers preserved (no `common/error_codes.h` on walnut); the static 2048-byte `task_stack[]` is dropped in favour of a kernel-allocated 4096-byte stack (`sdk_task_create(stack_ptr = NULL)`, the convention every ported module uses); `task_priority` is `TP_TIMED_ACTIVITY` instead of CG's raw `5`; `TaskStats` sampling added (as in GPS/TCP/SIM/URC); `sdk_sms_msg_free`-flush before deleting the temp queue in deinit (walnut has no `sdk_sms_deinit`, so the vendor task keeps the queue as its incoming route); `sms_manager_get_stats()` is now defined (CG declares it but never defines it).

**Registry:** `MODULE_ID_SMS` enabled with CG's exact flags (`required = TRUE`, `continue_on_fail = FALSE`, `config_stored = TRUE`), `SMS_SEND_Q` (5 × `ModuleMessage`) and `SMS_URC_Q` (5 × `sms_urc_queued_t`). Inbound SMS reaches `command_manager_accept_request()` and command replies route back to the sender's number through the already-present `utils_route_response_to_module(MODULE_ID_SMS, …)` path — no change needed in the command module.

#### 3.6.1 First on-target run (2026-09-08) — two AT commands failed, both fixed

Boot log from the first run with a SIM inserted:

```
Configuring SMS settings   SMS: delete all returned -1   SMS format set to 1
[WARN] SMS new message indication setup failed: -1   SMS charset set to GSM 7-bit   [INFO] SMS modem configured
```

`AT+CMGF` and `AT+CSCS` (ME-local) passed; both storage/service-dependent commands returned −1. Diagnosed by disassembling the prebuilt `sdk_sms.c.obj` (lib_wmsrc_B.a) and `wm_sms_secure.c.obj` (lib_wmsrc.a):

1. **`sdk_sms_delete_all()` fails until the SIM's SMS store has loaded.** It calls `wm_sms_delete_message(index = 0, delflag = 4)` → `AT+CMGD=0,4`. Fix: `sms_purge_store()`, a bounded per-index sweep that reads occupancy with `sdk_sms_get_storage_status()` (`AT+CPMS?`) and deletes in-range indices via `sdk_sms_delete()` (`AT+CMGD=<i>,0`) until `used` slots are gone, capped at `SMS_PURGE_MAX_INDEX` (60), plus a retry (below). Runs only as a fallback, after the CG-verbatim `sdk_sms_delete_all()` attempt.

   > **Corrected 2026-09-08 (second on-target run).** The first diagnosis here was that index 0 is out of range for a 1-based SIM store, making `AT+CMGD=0,4` permanently unusable. **That was wrong** — the run below shows it failing on attempts 1-2 and *succeeding* on attempt 3, and `AT+CPMS?` (which takes no index at all) fails in exactly the same window. The real cause is readiness: for the first seconds after the SIM reports `status=2` (READY) its SMS store is still loading and every storage-dependent AT command answers ERROR. The **retry** is what fixes this; the per-index sweep is kept only as a fallback for a modem/SIM that rejects `AT+CMGD=0,4` for good.

2. **CG's CNMI parameters are wrong for walnut — and would have silently broken reception had they been accepted.** CG passes `(1, 2, 1, 0, 0)` → `AT+CNMI=1,2,1,0,0`: `mt=2` routes SMS-DELIVER **straight to the TE as +CMT** and `bm=1` routes cell broadcasts to the TE. This modem rejects the combination, but the important point is the walnut inbound hook is a **+CMTI** callback (`smsSetCmtiCallback` / `wm_sms_cmti_cb`, kernel log `sms cmti: storage=%s, index=%d`), which only fires when the message is **stored and indicated** — i.e. `mt=1`. `wm_sms_init()` already applies `AT+CNMI=2,1,0,0,0` (plus `AT+CMGF=1`, `AT+CSCS="GSM"`), so the failing override was the only thing that could have taken the pipeline out of +CMTI mode. Changed to assert the kernel's own values: `sdk_sms_set_new_msg_ind(2, 1, 0, 0, 0)`.

**Also changed while fixing these:**
- `sms_configure()` latched `s_sms_modem_config_applied = TRUE` unconditionally (CG behaviour), so a partially-configured modem was never retried. It now retries when an *essential* call fails (format / CNMI / charset / purge), then latches anyway with an error. The counter and timestamp reset on SIM removal.
- Every `sdk_debug_print()` in the module was missing `\r\n`, which is why the log above runs together — all 15 now terminate their lines.
- Noted in code: the prebuilt `sdk_sms_init()` is `wm_sms_init()` + `wm_sms_set_incoming_cb()` with a hardcoded `return 0` — it can never report failure, so that error branch is dead code (kept for API correctness).

#### 3.6.2 Second on-target run (2026-09-08) — SMS configuration ✅ working

Boot 1 (cold SIM), showing the retry doing its job:

```
SIM status: UNAVAILABLE -> AVAILABLE
Configuring SMS settings (attempt 1)
SMS: delete all returned -1, sweeping per-index
[WARN] SMS purge: storage query failed: -1        <- AT+CPMS? not ready either
SMS format set to 1
SMS new message indication configured (+CMTI)     <- the CNMI fix works
SMS charset set to GSM 7-bit
[WARN] SMS modem config incomplete, retrying (1/5)
... attempt 2 identical ...
Configuring SMS settings (attempt 3)
SMS: deleted all messages                         <- store now ready
SMS format set to 1
SMS new message indication configured (+CMTI)
SMS charset set to GSM 7-bit
[INFO] SMS modem configured
```

Boot 2 (warm modem) completed on attempt 1. Both CNMI and the delete now succeed, so `sms_configure()` reaches its latch with everything applied and the +CMTI pipeline armed.

**Follow-up fix — retry spacing.** The run above succeeded on attempt 3, but only by luck: attempts were one task cycle apart, so `SMS_CONFIG_MAX_ATTEMPTS` (5) covered just **500 ms**. A slower SIM would exhaust the budget before the store came up and latch a half-configured modem. Retries are now spaced `SMS_CONFIG_RETRY_INTERVAL_MS` (2000) apart with the cap raised to 10 — a ~20 s window — using `utils_monotonic_ms_now()/_elapsed()`. Still bounded, so a permanently failing modem cannot flood the modem with ATs.

#### 3.6.3 Why no SMS arrives yet (2026-09-08) — checked against the vendor demo

Cross-checked `wm_ui_app.c`'s `WM_DEMO_SMS_*` cases against the port and disassembled the delivery path. **The plumbing is correct:**

- `sdk_sms_msgq_poll()` stores the queue pointer into the SDK's incoming-route global **before** reading the pending count, and unconditionally — so the demo's "attach the queue *before* `sdk_sms_init()`, so no arrival is missed" requirement is satisfied by our `sms_flush_temp_queue()` even on the cycle where it finds nothing pending. `sdk_sms_read/_delete/_delete_all` re-register it too.
- `prv_sms_incoming_cb(storage, index, raw)` builds `{type = SDK_SMS_EVT_INCOMING, status = 0, index, text = osiMalloc-copy of raw}` and hands it to `prv_sms_deliver()`, which `osiMessageQueueTryPut`s it into that registered queue. It drops the message (freeing `text`) **only** if the queue pointer is NULL or the queue is full — ours is registered and drained every 100 ms with 16 slots, so neither applies.

**So the blocker is upstream of SMS: the radio never came up in either boot.** Both logs stay in `RESTART_CFUN` (`NET CFUN=0` → `PDP_INACTIVE` → `NET_DISCONNECTED`) and never reach a registered/CONNECTED state; TCP sits in `WAIT_NETWORK` throughout. With no network attach the network cannot deliver an inbound SMS and `AT+CMGS` cannot succeed either. `SMS modem configured` only proves the ME-local (`AT+CMGF`/`AT+CSCS`) and SIM-store (`AT+CMGD`) commands work — it says nothing about service. **Get the network registered first, then re-test SMS.**

**Related interaction, worth knowing (affects SIM + network, not only SMS):** `[INFO] SIM modem status=1` (`SDK_SIM_ABSENT`) appears immediately after `NET CFUN=0`. SIM presence is polled *through the modem* (`sdk_sim_get_status`), so with the radio off it legitimately reads ABSENT. The CFUN restart holds the radio down for `NETWORK_CFUN_OFF_SETTLE_MS` + `NETWORK_CFUN_ON_SETTLE_MS` = 5 s, while the detect task polls at 1 s and needs 3 matches — so **every CFUN restart makes the SIM look removed for ~3 s** and broadcasts `EVENT_SIM_UNAVAILABLE`. For SMS that clears `s_sms_modem_config_applied` and the config is simply re-applied on the next "insert" (self-healing, but it re-runs `delete_all` each cycle). For the network it means a spurious SIM-removed event lands mid-restart. A CFUN-aware suppression in the SIM detect task is the obvious fix; not done here.

**Correction to §3.6.1's storage note:** walnut's `sdk_sms_read()` **ignores its `storage` argument** — the disassembly forwards only the index to `wm_sms_read_message(index, resp, resp_len)`, i.e. a bare `AT+CMGR=<index>` against whatever `AT+CPMS` selected. So the earlier `SDK_SMS_STORAGE_ME` vs `_SM` discussion was moot; the named selector is documentation only.

**Still to watch on send:** `sdk_sms_send()` is blocking (`AT+CMGS` via `atCmdSendWaitResp`). With the network down, a queued command reply will block the SMS task inside that call — the module-manager task-stall watchdog is 60 s.

#### 3.6.4 Network up, inbound SMS still not arriving (2026-09-08) — kernel SMS-task findings

Third run: network reached `NET connection UP` / IP `10.159.178.207`, TCP opened a socket, SMS configured on attempt 2. An SMS was sent to the device and produced no reply and **no `SMS URC (async)` line**, so the SDK never posted an `SDK_SMS_EVT_INCOMING`. Disassembling `wm_sms_task` (lib_wmsrc.a) explains how that can happen silently and turned up two gaps:

1. **`wm_sms_task` drops the message without calling the incoming callback when the read fails.** Its loop is: wait on the kernel SMS queue → check msg id == 8 → `wm_sms_read_message(index, resp, 512)` (a bare `AT+CMGR=<index>`) → **if that returns non-zero it frees and loops**; only on success does it `blx` the registered callback. So one failed `AT+CMGR` and the SMS is gone with no trace anywhere in our code.

2. **Nothing ever issues `AT+CPMS`.** `wm_sms_init()` sets only `AT+CMGF=1`, `AT+CSCS="GSM"` and `AT+CNMI=2,1,0,0,0`; `sdk_sms.h` exposes just the read-only `sdk_sms_get_storage_status()`. So the read/write/receive memories stay at the modem default. Combined with (1) this is a real failure mode: `+CMTI: <mem>,<idx>` reports the *receive* store while `AT+CMGR=<idx>` reads *mem1* — if they differ, every inbound SMS is silently discarded.

   **Fix:** `sms_configure()` now pins all three to SIM storage via `wm_sms_set_preferred_storage("SM", "SM", "SM")`, declared in `wm_lib/Inc/wm_sms_secure.h` and linked from `lib_wmsrc.a` — the same "reach past the SDK to the kernel" pattern the UART port uses, because `sdk_sms.h` has no equivalent. Non-fatal and retried (it fails while the store is still loading, like the other storage commands).

3. **Diagnostics added** (`sms_log_store_status()`): the active store name + `used/total` is logged at three points — `default` (before we change anything), `after CPMS`, and `configured`. This is the discriminator for the next run: if `used` climbs after an inbound SMS but no `SMS URC (async)` appears, delivery works and the notification/read path is at fault; if `used` stays 0, the message never reached the device.

4. **The kernel deletes the message itself.** After invoking the incoming callback, `wm_sms_task` calls `wm_sms_delete_message(index, 0)`. So the CG-verbatim `sms_manager_delete(index)` at the end of `sms_process_urc` normally answers ERROR for the inbound path. Kept (CG parity, and still needed by the `sms_read_message()` fallback, which does not auto-delete) but its log is downgraded from error to `SMS delete index %d failed (%d) - may already be gone`.

**The definitive next diagnostic:** `wm_sms_cmti_cb` logs `sms cmti: storage=%s, index=%d` via `RTI_LOG` *before* doing anything else. If that line appears when an SMS is sent, +CMTI is reaching the kernel and the fault is downstream (the `AT+CMGR` above). If it does not appear, the modem never raised +CMTI — a service/CNMI matter, not an app one. Note `RTI_LOG` targets the CP/Seagull debug console (drvUart port 0), which may not be the USB VCOM stream the app logs go to, so check both.

#### 3.6.5 Vendor-demo comparison (2026-09-08) — outbound works, store is already SM

Ran the vendor demo (`-t wm_app_main`) on the same unit. Two hard facts:

- **`SMS: Storage status` → `storage SM: 0/10 used`.** The modem's default read store is **already SM**, so the mem1-vs-receive-store mismatch theorised in §3.6.4 is **not** what is breaking inbound SMS. The `wm_sms_set_preferred_storage("SM","SM","SM")` pin is kept (it also fixes mem3, which `AT+CPMS?` does not report) but is now **non-essential** — it no longer clears `essential_ok`, because a modem that rejects the three-argument CPMS form would otherwise trigger the full 10-attempt retry ladder and a false "giving up" error. Note the store holds only **10 slots**.
- **Outbound SMS works.** `SMS: Send` → `send "WEGW Common Gateway SMS demo" to 9952929341 -> ok`, ~1.1 s for the blocking `AT+CMGS`. So the modem, SIM and network can originate SMS; `sdk_sms_send()`'s contract and timing are confirmed.

The demo run did **not** exercise inbound, which is the untested half. **Decisive next experiment, entirely inside the demo:** option 3 (Configure) → send an SMS to the device → option 9 (Poll + Drain), with option 4 before/after to watch `used` go 0 → 1. If the demo receives it, the modem/service path is fine and the fault is in the port; if it does not, the fault is below our code and no app change will help. Worth checking in parallel that **MT-SMS is provisioned on this SIM** (mcc=404 mnc=10, ICCID 8991102603410187788x — an M2M/data SIM): MO working does not imply MT is enabled, and that alone would explain everything seen so far.

> ⚠️ **Build-target trap (cost two builds here).** `Code/sdk/out/bcfg` persists the last `build.bat -t <target>`. After the demo run it held `target='wm_app_main'`, so the regenerated top-level `CMakeLists.txt` pulled in `wm_src/wm_ui_app` and the ninja graph contained **zero** references to `sms_manager` — a plain `.\build.bat` then rebuilt and staged the *demo* image while reporting success, and edits to the weware sources were silently not compiled (`ninja: no work to do`, even with the object deleted). **Always check `out/bcfg` (or that ninja actually compiled the file you edited) before trusting a build.** Rebuild the app explicitly with `.\build.bat -t weware_main`; switch back to the demo with `.\build.bat -t wm_app_main`.

#### 3.6.6 SMS working end-to-end (2026-09-08) ✅

Inbound SMS received, parsed, routed to the command manager, and the reply delivered back to the sender:

```
SMS URC (async): index=1
SMS URC: parsed index: 1
SMS URC: inline text parsed - sender=+919952929341, content_len=17
```

…followed by the command reply arriving as an SMS on the sending handset. So the whole ported chain is live: `+CMTI` → kernel `wm_sms_task` read → `SDK_SMS_EVT_INCOMING` → parked in `SMS_URC_Q` → drained by the SMS task → `sms_extract_fields` (`+919952929341`, 17-char body) → `command_manager_accept_request()` → `utils_route_response_to_module(MODULE_ID_SMS, …)` → `SMS_SEND_Q` → `sdk_sms_send()`. The inline-`+CMGR` fast path in `sms_process_urc` is what ran (no `sms_read_message()` re-read), exactly as designed.

**Which change fixed it — and a correction to §3.6.5.** The only functional change to the inbound path between the failing and passing runs was the `wm_sms_set_preferred_storage("SM","SM","SM")` pin, so that is the likely fix, and the mechanism theorised in §3.6.4 was probably right after all. §3.6.5 retracted it too readily: **`AT+CPMS?` reports only the active read/delete store (mem1) — it does not report mem3, the *receive* store.** So the demo's `storage SM: 0/10 used` never ruled out a mem3 mismatch; with mem3 defaulting elsewhere, `+CMTI` would name a store that the subsequent bare `AT+CMGR=<idx>` (against mem1=SM) could not read, and `wm_sms_task` drops such a message without ever calling the incoming callback. Pinning all three memories removes that mismatch. Not proven in isolation — other things (service/registration timing) also differed between runs — so the CPMS call stays **non-essential** (it must not gate the retry ladder) but is definitely kept.

**Also added:** `sms_send_internal()` now logs successful sends (`SMS sent to %s (%u chars, %u ms)`) as well as failures with the elapsed time. The reference logged nothing on success, which left the outbound half of a command round-trip invisible — the kernel's own AT trace goes to the CP console, not the app's.

#### 3.6.7 Two defects from the SMS command soak (2026-09-08)

24 inbound commands exercised (`SMS_test.log` + `z_logs/`). Two faults:

**(a) Stale AT-command text prefixed to a reply — kernel defect in `wm_sms_send_text`.** `MOD: get-ota-status` (no password) came back as:

```
AT+CPIN?
AT+QCELLEX=1
ERROR: Authentication required for 'GET-OTA-STATUS'
```

The reply text itself is correct; the two AT lines are prepended. They cannot come from our code — `cmd_state_handle_check_auth` writes the error with `snprintf(response_buffer, …)` at offset 0, `utils_route_response_to_module` copies into a zeroed local `ModuleMessage`, and no app source contains an `"AT+…"` literal (both strings live in `lib_wmsrc.a`: `AT+CPIN?` from the SIM status poll, `AT+QCELLEX=1` from the cell-info query the GPS packet builder uses).

The mechanism is in the kernel. Disassembly of `wm_sms_send_text` shows the CMGS sequence split across **two separate `atCmdSendWaitResp()` calls** — first `AT+CMGS="<number>"`, then the body + Ctrl-Z — with only its **own** SMS mutex held (`osiMutexTryLock` on the SMS lock, taken before both). Between those two calls the modem sits in **prompt (`>`) mode, where every byte on the AT channel becomes message body**. Our other tasks are issuing ATs continuously — the SIM detect task polls at 1 Hz (`SIM modem status=…` every second in the log) and each GPS packet triggers the radio-info query — so any AT command landing in that window is swallowed into the outgoing SMS. That matches the observed junk exactly: real AT command strings, in the prefix position, from precisely those two subsystems, and intermittent (most replies were clean because the collision must hit the prompt window). The blocking `sdk_sms_send` measured ~1.1 s in the vendor demo, so the window is wide.

There is **no app-side fix**: the SMS lock does not serialise other subsystems' AT traffic, and `atCmdSendWaitResp` exposes no "hold the AT channel" flag. **This needs a vendor fix** — the CMGS prompt sequence must be atomic against all other AT users. Possible interim mitigations if it proves disruptive: gate the SIM 1 Hz poll (and the GPS radio-info query) on an "SMS send in progress" flag, which shrinks but cannot close the window.

**(b) Duplicate reply — both SMS queues were unlocked across tasks (fixed).** `Mod:get-network-config` produced two identical `apn:wheelseye.com,user:,pass:,cid:1,auto:TRUE` replies from a **single** inbound message (one `SMS URC (async)` in the log, so the duplication is downstream of receive). Root cause: `queue_manager_create()` only creates a mutex when `QueueConfig.thread_safe` is set — otherwise `q->mutex` stays NULL and `queue_push`/`queue_pop` run **completely unlocked** — and both SMS-path queues were CG-verbatim `thread_safe = FALSE` while crossing task boundaries:

| Queue | Producer | Consumer |
|---|---|---|
| `CMD_REQUEST_Q` | SMS task (`command_manager_accept_request`) **and** TCP task (recv path) | command task |
| `SMS_SEND_Q` | command task (`utils_route_response_to_module`) | SMS task |

An unsynchronised ring buffer with producer and consumer on different tasks can lose a head/tail update and hand the same element out twice. Both are now `thread_safe = TRUE`. This is a **latent CG bug carried over verbatim, not a port artifact** — the reference has the same cross-task producers with the same flag; it deviates from CG deliberately and the registry entries say why. Note `TCP_SEND_Q` was already `TRUE`, which is why the TCP path never showed this.

**Diagnostics added** so the next soak is conclusive: `sms_forward_inbound()` logs the inbound command text (`SMS cmd from %s [%u]: '…'`) instead of only `content_len`, and `sms_send_internal()` logs the exact body handed to the kernel (`SMS send -> %s [%u]: '…'`). If the AT junk appears in that send line, corruption is upstream of the kernel; if the line is clean and the delivered SMS is not, (a) is confirmed as the sole cause.

#### 3.6.8 CG ordering check: SMS config is gated on SIM presence, never on the network (2026-09-09)

Verified against the reference. **CG's SMS module has zero coupling to the network** — no `EVENT_NETWORK_*` registration and no network call anywhere in `module/sms/sms_manager.{c,h}`. `sms_configure()` is gated solely on SIM presence (`g_sms_sim_inserted`, set from `EVENT_SIM_AVAILABLE` / `sim_manager_get_sim_status()`), so SMS is configured **as soon as the SIM is present — before, and independently of, the network connection**. Module init order is identical to ours (SMS after NETWORK in `g_modules[]`), but that is init order, not configure order. Our port already matched this in code shape, so no ordering change was needed.

**The real divergence is the SIM signal itself, and it is what has been disrupting SMS configuration.** The reference reads SIM presence from a **hardware SIM-detect GPIO** (`SDK_GPIO_SIM_DETECT_PIN` 117, a tray switch), which `AT+CFUN=0` cannot affect — CG's "SIM present" is stable from boot, so `s_sms_modem_config_applied` latches once and `sms_configure()` never runs again. Walnut's `sim.c` has that GPIO path but compile-gated off (`SIM_DETECT_VIA_GPIO 0`); the active path asks the **modem** (`sdk_sim_get_status`), which reports `SDK_SIM_ABSENT` whenever the radio is down. `RESTART_CFUN` holds the radio off for `NETWORK_CFUN_OFF_SETTLE_MS` + `NETWORK_CFUN_ON_SETTLE_MS` = 5 s, while the detect task polls at 1 s needing 3 matches — so **every radio restart faked a SIM removal**, exactly as seen on target:

```
[INFO] NET CFUN=0
[INFO] SIM modem status=1        <- ABSENT, radio is off
[INFO] SIM modem status=1
Configuring SMS settings (attempt 2)
```

Consequences: `EVENT_SIM_UNAVAILABLE` to every listener (the network module gets a spurious SIM-removed mid-restart), and for SMS a cleared config latch followed by a full reconfigure — `delete_all` included, wiping the store — issued precisely while the radio is down, which is when those storage/service AT commands fail. That is the source of the `-1` retry ladder chased in §3.6.1/§3.6.2.

**Fix (follows the reference's behaviour without needing the detect GPIO):** the SIM detect task now skips sampling while `weware_network_get_state() == NETWORK_STATE_RESTART_CFUN` (both already public; no extra AT traffic, unlike polling `sdk_network_get_cfun()`). SIM presence therefore stays stable across radio restarts, the SMS latch holds after the first successful configure, and the spurious SIM-removed events stop. This also closes the "related interaction" flagged in §3.6.3.

#### 3.6.9 CMGS-prompt corruption CONFIRMED (2026-09-09, `z_logs/sms_spam_test.log` 1422-1489)

Reproducible by sending SMS commands back to back. The new `tx2`/`tx3`/`tx4` tracing proves the corruption is **not** in anything the app produces — it happens inside the kernel's `AT+CMGS` prompt window, exactly as theorised in §3.6.7(a).

Two consecutive replies, log vs. what the handset received:

| # | `SMS>tx2` body we hand to the kernel | window (`tx3`→`tx4`) | app log lines inside the window | delivered SMS |
|---|---|---|---|---|
| 1 | `'ERROR: Authentication required for 'GET-OTA-STATUS''` (51 ch, clean) | **4135 ms** | `SIM modem status=2`, ADC, `WEWARE STATUS`, **`[WARN] gsm utc: rtc_get_utc_time failed`**, TCP send/ACK | **`AT+CCLK?`** + the correct reply |
| 2 | `'is:0,wo:5.0,wf:4.9,…,sc:0'` (101 ch, clean) | **1920 ms** | `[nmea]`, ADC | **`AT+CSQ`** + the correct reply |

The match is exact: reply 1 was corrupted with `AT+CCLK?` and the log shows the clock read (`gsm utc: rtc_get_utc_time`) firing inside that window; reply 2 was corrupted with `AT+CSQ`, the signal-quality query the GPS/radio path issues. Both `tx2` bodies are byte-clean, and `sdk_sms_send()` returned **rc=0** in both cases — the modem considers a corrupted send successful, so there is no error for the app to detect.

Confirmed offenders so far — all app-initiated through `sdk_*` wrappers: `AT+CCLK?` (time), `AT+CSQ` (signal), `AT+CPIN?` / SIM status poll at 1 Hz, `AT+QCELLEX=1` (cell info, per GPS packet). Send windows measured 1.9-4.1 s, so with any periodic AT traffic a collision is near-certain under load — hence "reproducible if you spam fast enough".

**No kernel-level lock is available:** `atCmdSendWaitResp` is the only AT symbol exported by `core_stub.o` / the libs (no `atLock`, `at_mutex` or equivalent), and `wm_sms_send_text` guards the sequence only with its own SMS mutex, which other subsystems never take. The proper fix is in the vendor kernel — the CMGS prompt sequence must be atomic against all AT users. **Report to Walnut.**

An app-side mitigation was considered — a global "AT gate" mutex held by the SMS task across `sdk_sms_send()` and taken by the periodic AT users (SIM poll, time read, radio/cell queries, network health check), since every observed offender is ours. **Decision 2026-09-09: not implemented — vendor fix only.** It would only mask a kernel defect, could not cover AT traffic the kernel originates internally, and would spread SMS-specific coupling across the SIM, GPS, time and network modules. The `SMS>tx2/tx3/tx4` tracing stays in place as the reproduction and evidence path.

##### Vendor report (Walnut) — one-line statement of the defect

`wm_sms_send_text()` (lib_wmsrc.a) splits the CMGS exchange into two separate `atCmdSendWaitResp()` calls — `AT+CMGS="<number>"`, then the body + Ctrl-Z — and guards them only with its own SMS mutex. Between the two calls the modem sits in `>` prompt mode, where **every byte arriving on the AT channel is taken as message body**, so any AT command issued by another task in that window (measured 1.9-4.1 s) is transmitted as part of the SMS. Reproduced with `AT+CCLK?` and `AT+CSQ`; `AT+CPIN?` and `AT+QCELLEX=1` seen earlier. `sdk_sms_send()` still returns 0, so the corruption is undetectable by the caller. **Required fix:** hold the AT channel lock across the whole CMGS prompt sequence (or expose a lock so callers can), and/or return an error when the prompt exchange is disturbed.

**Unrelated observation — the device reset at the end of boot 1**, between `NET URC 1 in RESTART_CFUN` and the `[ERROR] NET URC PDP deactivated -> DISCONNECTED` line that boot 2 prints at the same point. SMS had already finished (`SMS modem configured`) and its task was idle, and boot 2 ran the identical SMS code through the same point without incident, so this sits in the **network URC path, not SMS**. Boot 2's banner shows no `Reboot: SOFT/<module>` line, i.e. no pre-boot record was written — an unplanned reset rather than the app's own soft-reset path. Needs its own investigation; not tracked here.

---

### 3.7 UART (ported 2026-09-07)

Files: `module/uart/uart_manager.{h,c}` (CG logic verbatim), the new walnut driver adapter `module/uart/sdk_walnut_uart.c` (no CG counterpart — see "The SDK's own UART API is unusable" below), plus verbatim copies of the two pure-C dependencies `common/stm_binary_protocol.{h,c}` (GW-STM-BIN-001 framing, CRC-16/IBM-SDLC, self-test) and `common/stm_command_map.{h,c}` (ASCII `STM:` command → binary frame, hex passthrough fallback). Both of the latter depend only on `common/types.h`/`common/utils.h`, so neither needed a single change.

**The SDK's own UART API is unusable — this is the single biggest walnut deviation in the module.** Disassembling the prebuilt `sdk_uart.c.obj` shows `sdk_uart_set_config()` validating `port == 0` and then calling `drvUartOpen(0, &cfg)` with a **hardcoded literal 0**, while `components/inc/drv_uart.h` declares `DRV_UART_PORT_1 = 0, ///< debug uart , APP can not use`. Port 0 is the kernel log console and is already open, so `drvUartOpen` returns NULL and every `sdk_uart_set_config()` call returns `SDK_RESULT_ERROR` — the observed `ERR_UART_CONFIG_ERROR` boot loop. The wrapper ignores `port` beyond requiring 0, so it cannot be steered to a usable port from the caller. `src/module/uart/sdk_walnut_uart.c` therefore re-implements all five `sdk_uart_*` entry points directly over the kernel-exported `drvUart*` driver:

- **Port is selected through the `sdk_uart.h` API**, not a define: `uart_manager.c` sets `UartConfig.port = WALNUT_UART_PORT_USER` and the adapter honours the `port` argument (the prebuilt SDK wrapper discards it). Ids and the map live in `inc/module/uart/sdk_walnut_uart.h`. `drv_uart.h` lists four ports but the SoC has **three** UARTs and the header's names are unreliable; the real map, from `kernel/GW1NS-4/cp.axf`:

| idx | `drv_uart.h` name | UART base | verdict |
|-----|-------------------|-----------|---------|
| 0 | `DRV_UART_PORT_1` | `0xd4017000` | **rejected by `drvUartOpen` itself** — it opens with `cbz r6, <fail>`, so port 0 always returns NULL. Seagull/CP debug+diag console (`SEAGULL_UART_BASE_ADDR` holds this base), matching the header's "APP can not use" |
| 1 | `DRV_UART_PORT_2` | `0xd4018000` | **claimed — AT-command channel.** `uart_atcmd_init` drives `atuartcfg` (`0x7e2392d4`), whose port field is initialised to `1` and whose baud field it sets to 115200. Matches the header's "CP UART (AT)". Opening it risks network attach |
| 2 | `DRV_UART_PORT_3` | `0xd401f000` | **← THE user-configurable UART (`WALNUT_UART_PORT_USER`).** Full duplex, free: the header calls it "BT uart" but this firmware links **no** Bluetooth stack (zero `bluetooth`/`btstack` symbols in `cp.axf`), and it carries no logs |
| 3 | `DRV_UART_PORT_4` | `0xd401f800` | **RESETS THE DEVICE.** `drvUartOpen`'s only bounds check is `port < 4` so index 3 is accepted, but `bspUartNumOfUartsVar == 3` and there is no `Uart4ClockOnOff` beside `Uart1/2/3ClockOnOff`. `uartdrv_info[3]` exists as a table entry but its UART is not present/clocked on this part → unclocked peripheral write → bus fault → reboot |

  Ports 0 and 3 are refused at runtime by `walnut_uart_port_ok()` with an explanatory log line, and port 1 warns loudly. `_Static_assert`s keep `WALNUT_UART_PORT_*` in step with `DRV_UART_PORT_*`. Where a configured port is compared against `DRV_UART_PORT_*`, it **must** be `_Static_assert`, not `#if`: those are enum constants, and the preprocessor treats unknown identifiers as 0, so `#if (X == DRV_UART_PORT_1)` compares `0 == 0` and fires for every value.
- **Symbol shadowing**, the same mechanism `inc/module/tcp/sdk_functionality_tcp.h` documents for TCP: `sdk_uart.c.obj` exports exactly five globals and the adapter defines all five, so the member is never extracted from `lib_wmsrc_B.a` and nothing collides. This works *because* the kernel archive is scanned before the app archive and nothing loaded by then references `sdk_uart_*` (every caller lives in the app archive) — verified by `customer_app.map` containing **zero** occurrences of `sdk_uart.c.obj`. Corollary: the adapter must stay in the same archive as its callers.
- **`drvUartCfg_t` ABI is pinned by 12 `_Static_assert`s.** The compiled layout (32 bytes; `baud` u32 then byte-wide enum/bool fields) only matches `drv_uart.h` because `arm-none-eabi-gcc` defaults to `-fshort-enums`; the asserts fail the build loudly if a flag or toolchain change ever shifts a field the driver reads.
- **Event callback** mirrors the kernel's: on `DRV_UART_EVENT_RX_ARRIVED` it calls `osiSetPMEnableSleep(false)` and resets `gi_key_out_timeout`, then drains the FIFO with `drvUartReadAvail`/`drvUartReceive` in a loop. It uses a file-static drain buffer instead of the kernel's per-event `malloc`.
- `sdk_uart_write` maps a short `drvUartSend` count (tx FIFO full) to `SDK_RESULT_ERROR` so the caller does not think a partial frame was sent.

| CG API | Walnut | Note |
|---|---|---|
| `sdk_uart_set_config(port, cfg)` | identical signature | `SdkUartConfig` carries an **extra trailing `flow_control`** byte (0=none, 1=RTS/CTS); set explicitly to 0 |
| `sdk_uart_write(port, buf, size, written)` | identical | used verbatim |
| `sdk_uart_control(port, cmd)` | identical | `SDK_UART_CLOSE` → `SDK_UART_CTRL_CLOSE` |
| `sdk_uart_register_callback(port, void(*)(port, len, param), param)` | `sdk_uart_set_rx_callback(port, SdkUartRxCallback, arg)` where `SdkUartRxCallback = void(*)(UINT32 port, const UINT8 *data, UINT32 len, void *arg)` | **different, the one structural adaptation** — see below |
| `sdk_uart_read(port, buf, size, read)` | kernel: **always returns `SDK_RESULT_NOT_SUPPORTED`**; our adapter implements a real polling read | the callback *is* the receive path — a polling read is pointless while one is armed (the event handler drains the FIFO first). Nothing in `uart_manager.c` calls it |
| `SDK_UART_PORT_MAIN` / `SDK_UART_PORT_LOG` | `SDK_UART_PORT_1` | one logical port (id 0); any other value returns `SDK_RESULT_INVALID_PARAM`. The **physical** port is `WALNUT_UART_STM_PORT` in the adapter |
| `SDK_UART_BAUD_115200`, `SDK_UART_WORD_LEN_8`, `SDK_UART_ONE_STOP_BIT`, `SDK_UART_NO_PARITY_BITS` | **missing** | walnut defines no such enums — plain numeric values (115200 / 8 / 1 / 0) |
| `functionality/sdk_functionality_uart.h` + `SdkUartFunctionalityOps` dispatcher | **missing** | no functionality layer for UART; app calls the kernel `sdk_uart_*` directly (same as SMS, §3.6) |

**RX path — the one structural adaptation.** CG's callback received only a byte *count*, then `malloc`'d a buffer and called `sdk_uart_read()` to pull the bytes out of the driver. Walnut's SDK performs the read itself and hands the callback the *buffer* (`data`, `len`), valid only for the duration of the call — and `sdk_uart_read()` is a documented no-op. `uart_process_received_data()` already copies into `g_uart.response_buffer` before returning, so `data` is fed straight in and the per-chunk malloc/read/free round-trip is deleted. The `UART_MAX_RX_CHUNK_SIZE` clamp is kept. Note the callback runs in the SDK's UART service context (not an ISR, and not the `uartTask`) — the same cross-context arrangement CG had, including `thread_safe = FALSE` on the request queue.

**Other walnut deltas:** `LOG_ERRC(ERR_UART_*, …)` → `LOG_ERROR` with the CG error-code name kept in the text and the `/* ERRC */` markers preserved; the static `g_uart_task_stack[2048]` is dropped for a kernel-allocated 4096-byte stack (`sdk_task_create(stack_ptr = NULL)`); `task_priority` is `TP_TIMED_ACTIVITY` and its field widened from `UINT8` to `UINT32` (walnut `TP_*` values are ~213, outside CG's 0..15 scale); `deinit` and the task-create failure path now clear the RX callback and unwind the request queue — on walnut the callback registration is independent of open/close and survives a reconfigure, so leaving it armed would keep delivering into a torn-down buffer (CG returned without unwinding either).

**Gated-out dependencies** (both `#define`d at the top of `uart_manager.c`; remove the define once the module lands):
- `HEALTH_PACKET_UNAVAILABLE` — `module/tcp/health_packet` is not ported, so the `0x86 system_alive` handler cannot cache the 42-byte STM block or push a type-40 health packet, and a `system_alive` timeout cannot report an all-zero STM half. The frame is still parsed and its human-readable rendering still routes to the requester. `HEALTH_STM_BLOCK_LEN` (42) is defined locally so every payload length check stays identical to CG.
- `FILE_TRANSFER_UNAVAILABLE` — `system/file_transfer` is not ported (the same call `system/ota/ota_manager.c` makes), so `MODULE_ID_FILE_TRANSFER` has no queue and no `g_modules[]` slot. `0xC0` STM-OTA and `0xC1` peri-OTA ack strings are still reconstructed, then dropped with a `LOG_WARN` instead of being routed.

**Registry + command wiring:** `MODULE_ID_UART` enabled with CG's exact flags (`enabled = TRUE`, `required = FALSE`, `continue_on_fail = TRUE`, `has_task = TRUE`, `UART_REQUEST_Q` = 5 × `ModuleMessage`). `ModuleMessage` gained CG's two missing fields, `is_raw` (TX: send payload bytes verbatim) and `payload_is_binary` (RX: payload is length-delimited, not NUL-terminated). `UART_UNAVAILABLE` in `command_config.h` is now undefined and `cmd_state_handle_check_stm_prefix()` replaced with the CG-verbatim version: `SRC_TYPE`/`SRC_ADDR` derived from the originating channel (SMS carries the phone number, TCP carries none), `stm_cmd_build_from_ascii()` → `stm_cmd_try_hex_passthrough()` fallback, `is_raw = TRUE`, and the `system-restart` pre-boot record saved before the STM cuts power (CG's `log_storage_flush()` omitted — no async log ring on walnut). PING-STM in `command_manager.c` needed no change; lifting the gate activates the existing `uart_manager_start_ping_test()` branch.

---

## 4. Cross-cutting gaps (priority order)

1. ~~TCP store-and-forward~~ ✅ 2026-08-21 — TCP_SEND_Q live: batch send / ACK-commit / live-first / reboot persistence; GPS backlog absorbed; command/BLE response path has a landing queue.
2. **Board constants** — GW1NS ADC channels (`ADC_CHANNEL_IGNITION/EXTERNAL`, default 1/2), divider gain (default 131), GPIO pins: netlight **assigned 2026-09-08** (69 = blue LED, active high; 70 = red is free), status/power-select still default disabled. Ignition/voltage readings are untrusted until confirmed.
3. **Event broadcasts from modules** — TCP done 2026-08-21; SIM done 2026-08-21 (`EVENT_SIM_AVAILABLE/UNAVAILABLE` + connected tracking); network overall-timeout → `EVENT_RESET_SOFT` done 2026-08-21. Still open: GPS connected-disconnected events and the GPS parse-fail `sdk_system_reboot()` → `EVENT_RESET_SOFT` switch — **raised in priority 2026-09-09:** with the GNSS module unpowered this fires ~60 s after every boot (60 fails × 1000 ms loop) and will abort any longer OTA download, so the switch should also gate on `wm_sdk_gps_get_power_status` rather than rebooting on a receiver that is simply off. **Confirmed harmless once GPS is on (2026-09-16):** with a live feed the streak never builds, so this only bites GPS-off bench runs.
4. **Config persistence wiring** — network done 2026-08-21 (`weware_network_set_apn`/`network_config_set` persist; NetworkConfig stored, blob v5). Still open: `gps_config_set` / `weware_tcp_config_set` should call `config_get_current() + config_save_to_file(NULL)` (system_config and network already do).
5. **Packet placeholder bytes** — input-wire mV, external mV, battery % (all already computed in `g_power_info`), digout, accel orientation.
6. ~~SENDING_DATA liveness~~ ✅ 2026-08-21 — 100 s keepalive restored (CG table row incl. `reset_overall=FALSE`).
7. ~~Login+GPS~~ ✅ 2026-08-21 — last-valid GPS appended when `loginwithgps` set; login buffer 79 B.

## 5. Not ported yet

~~Command manager~~ (compiled 2026-08-26 — see §1; `STM:`/PING-STM live since 2026-09-07, DIGOUT still stubbed until the digout manager lands) · ~~SMS manager~~ (compiled 2026-09-07 — see §3.6) · ~~UART manager~~ (compiled 2026-09-07 — see §3.7) · BLE manager + ag_dfu · OTA manager · digout (relay) manager · accelerometer (STK8321 — blocked on walnut I2C: `sdk_i2c` has no transfer API; needs vendor `i2cc_*` driver) · file-transfer · CG async log ring · HTTPS ops.

**Blocked behind the UART port question** (the UART module itself is written but disabled — see the status correction at the top): once the STM's physical port is confirmed and `MODULE_ID_UART` is re-enabled, clearing `HEALTH_PACKET_UNAVAILABLE` restores the type-40 health push on `system_alive` (and the `system_alive` main-loop watchdog message, still not wired from the system manager), and clearing `FILE_TRANSFER_UNAVAILABLE` routes STM-OTA / peri-OTA acks and restores `ota_manager_get_status_string()`'s live-progress branch. Both need their own module ports first.

## 6. Validation status

- ✅ Compiles and links (GW1NS-4, XIP, 4 MB flash). ⚠️ Image **not** signed/staged for the 2026-09-07 UART change — see the Build line at the top.
- ✅ **Storage dirs + reboot persistence — behaviorally validated 2026-09-08** (2026-08-26 trailing-slash fix). Test: SIM removed → offline GPS packets accumulated in TCP_SEND_Q (file persistence under `C:/queue/`) → device soft-reset via the network overall-timeout `EVENT_RESET_SOFT` (preboot record written) → SIM re-inserted on next boot → server received, in order: ① login packet, ② the offline backlog in ascending packet-count order, ③ live packets. Proves the flash dirs are created, the queue snapshot + preboot record survive the reset, and the resume path replays without loss or reordering.
- ⬜ On-target boot: verify module init pass, `WEWARE STATUS` line, `[ADC]` readings plausibility, config file creation on first boot, watchdog doesn't false-trigger (all five tasks feed uptime; loops iterate ≤1 s).
- ⬜ Regression: GPS trigger cadence, TCP login/ACK against stage server, network reconnect ladder, reboot persistence (preboot record + GPS last-valid).
- ✅ **TCP store-and-forward — behaviorally validated 2026-09-08** (SIM-removal test above): backlog accumulated while TCP down, survived the network-timeout soft reboot (TCP_SEND_Q snapshot + resume offset), and drained on reconnect in CG order — login first, then offline rows ascending by packet count, then live packets; login frame accepted by the server. Still unobserved: the 100 s idle keepalive cycling the connection without wedging (needs an idle-queue soak).
- ✅ **GPS NMEA source — behaviorally validated 2026-09-16** (was the highest on-target risk: the kernel NMEA callback replaces the validated navdata poll as the sole source). With the GNSS module powered: `[nmea] fix_real=1 lat=28.416043 lon=77.038935 kts=2.51 sats=9 hdop=989` at 1 Hz, talker `$GN`, no parse-fail streak, and the resulting 55-byte packet sent and ACKed by the server (`SENDING_DATA` → `ACK_RECEIVED` → `SENDING_DATA`). Proves callback → RMC+GGA pairing → `GPS_URC_Q` → combined parse → packet, and the **HDOP byte is now real** (989 = 9.89). Revert switch if ever needed: rebuild with `-DGPS_QUERY_NAVDATA_POLL`. Fix quality was marginal (HDOP 9.89 on 9 sats, ~30 m epoch-to-epoch scatter, 0.8–2.5 kt while stationary) — an indoor/near-window fix, so that accuracy is not a ceiling. ⚠️ **A GPS-off run is not neutral:** `GPS_PARSE_FAIL_SOFT_RESET_COUNT` = 60 against the 1000 ms loop reboots the device ~60 s after every boot (`gps_ops.c:1058-1067`), which aborts any OTA download longer than that — raise the count or gate the reset on `wm_sdk_gps_get_power_status` if GPS-off soaks are needed (see §4 item 3). *(The 2026-09-09 `GPS query and parse failed` stream was this same expected empty-queue path with the module unpowered — see §3.1 for the SDK-chain audit it prompted.)*
- ⬜ **No fix-quality gate — low-quality fixes are reported as valid and shipped (observed 2026-09-16).** A restart re-acquired in **~43 s** (banner 085131.11 → first valid fix 085214.61, retained position) but came up with **HDOP 86.3 on 7 satellites**, a position drifting steadily south and altitude climbing 179→193 m, and speeds of **8.6 / 18.9 / 11.1 / 9.0 kt on a stationary desk**. The app accepted all of it (`fix_real=1`) and sent it to the server. Nothing gates on HDOP: `gps_validate_coordinates` checks only lat/lon range, and the jump filter's threshold is 500 km/h, far above phantom motion at 10–19 kt. CG-parity behaviour, so **this is a decision, not automatically a defect** — but in the field it would drive the distance/angle/speed triggers and produce spurious motion packets. Options: gate `fix_valid_real` on an HDOP ceiling, or hold off sending until HDOP settles. *(HDOP packet encoding checked and is correct — `gps_packet.c:132` writes `hdop_x100 / 100`, so 8630 → 86 fits the byte; no overflow.)*
- ⬜ **GPS follow-ups opened by the 2026-09-16 run** (none blocking): ① ~~**constellation mask**~~ **CLOSED 2026-09-16 — working as required, do not change.** `gps_ops.c:388` asks for `WM_SDK_GPS_SYS_GPS | WM_SDK_GPS_SYS_GLO` (0x41) while the BK1616P supports GPS|BDS|GLO|GAL (0xC5); **GPS+GLONASS is a product requirement**, so the narrower mask is deliberate. Consequence to keep in mind when reading GPS numbers: fewer satellites than the part can track, so TTFF and HDOP are inherently worse than the hardware's ceiling — that is an accepted trade, not a defect to chase. ② **⚠️ CONFIRMED 2026-09-16 — receiver config is not re-applied after a receiver-only restart, and this silently breaks the GPS+GLONASS requirement.** The boot path itself is fine (all eight `$POLCFG*` commands `$OK`'d, banner `$POSYS_BM:0x000000C1`, clean 83 B/s GGA+RMC). But `wm_gps_power_on` runs only from `wm_gps_init` at *device* boot, and the SDK never sends `$POLCFGSAVE` — so when the receiver restarts on its own the runtime config is lost. **Directly observed:** a GNSS-only power cycle produced a banner reading **`$POSYS_BM:0x000000C5`** (flash default = GPS\|BDS\|GLO\|GAL) with `$GNGSA` back in the stream and RX chunks at 985–1012 B vs the clean 83 B. The GSA satellite list then showed PRN **194** (QZSS range) in use — a satellite that cannot appear under the required GPS+GLONASS mask. **Consequence: after any receiver-only restart the device runs the wrong constellation set until the next full device reboot**, and nothing in the app detects or recovers it (`g_gps.gnss_mode_set` stays latched and `gps_ops_retry_configuration` early-returns). **Scope confirmed 2026-09-16 by `z_logs/gnss.txt`:** the *boot* path is correct and durable — `$POLCFGSYS,65` yields `$POSYS_BM:0x00000041` and it survives `$POLCFGNAV,1,1` **and `$POLCFGRESET,0`** unchanged (an earlier suspicion that the reset wiped the mask is disproven). Only a true receiver **power cycle** reverts it to `0xC5`. Measured impact at this indoor location: every fix under the correct `0x41` ran at **4 satellites** (HDOP 9.6, the bare minimum for a 3D fix) versus **6** under the reverted `0xC5` — so the requirement is met, but with zero satellite margin; re-measure outdoors. **Field exposure is narrow:** the app never powers the receiver down — `wm_sdk_gps_set_power_status(1)` at `gps_ops.c:302` is the only power call anywhere in `weware_app`, and there is no `(0)` — so a receiver power loss implies a device power loss, which is followed by a boot that re-applies config. This is therefore a robustness gap against a *hardware* event (GNSS-rail brownout, ESD, loose connector, receiver-internal watchdog) or the bench's GNSS-only button, not an active fault in normal operation. A fix would be to detect the receiver's restart banner — `gps_nmea_sentence_cb` already sees every sentence, and `$BKCHIP`/`$POSYS_BM`/`$FWVER` are unambiguous — and re-run the config ladder by clearing the `g_gps.gnss_*_set` latches. See requirement note [[walnut-gps-constellation-requirement]]. ④ ~~**the app hot-resets the receiver mid-acquisition**~~ **CLOSED 2026-09-16 — intentional, working as designed.** `wm_sdk_gps_start_mode()` is `wm_gps_reset(cold = mode==2)` → `$POLCFGRESET,0` (HOT/WARM) or `,1` (COLD); a **warm start** is the intended bring-up (with A-GPS where available), and the config ladder issues it **once per boot** — each step latches on its `g_gps.gnss_*_set` flag and `gps_ops_retry_configuration` early-returns on `all_configured`, so the block is never re-entered. Two facts worth carrying: **(a)** HOT and WARM are identical on the BK1616P (only `mode == 2` sets `cold`), so the `gps_post_boot_is_power_on_reset()` HOT-vs-WARM branch is a no-op on this part; **(b)** `wm_sdk_gps_open_agps_service()` is a documented always-`NOT_SUPPORTED` stub here, so the A-GPS half of the intent has no path on this hardware yet. ③ **watch for a mid-stream receiver banner** (`$BKCHIP` / `$POLRS` / `$POSYS_BM` / `$FWVER`) — that is the GNSS receiver restarting, and a spontaneous one explains a permanent no-fix (acquisition never accumulates the ~30 s per SV needed for ephemeris) long before antenna or sky are to blame.
- ⬜ **UART / STM — currently DISABLED, nothing to test until re-enabled.** ① **Which physical port is the STM on?** The one open unknown, now narrowed to two candidates: `DRV_UART_PORT_3` (idx 2, `0xd401f000`, the default and recommended) or `DRV_UART_PORT_2` (idx 1, `0xd4018000`, the AT channel — try only if idx 2 gives no STM reply, and watch that network attach still works). Never idx 0 (driver rejects) or idx 3 (resets — now blocked by a `_Static_assert`). Re-enable per the 3 steps in `module_config.c`; success looks like `[WUART] port=2 open @115200 8-1-0 flow=0`, and a wrong-but-valid port just yields `STM: FAIL (no response)`. ② Confirm `[UART] I: UART manager ready` with **no** `STM binary protocol self-test FAILED` (a CRC/frame mismatch would silently corrupt every frame). ③ Confirm 115200-8-N-1-no-flow matches the STM — a wrong baud fails silently as CRC-bad frames, so watch for a `0xAA` resync / `STM_FRAME_BAD` pattern rather than an error. ④ Send `STM:get-device-info` by SMS → expect `Ok,<fw>,<hw>,<MAC>,…`; then `PING-STM` → expect `STM: OK`, not `STM: FAIL (no response)` after 3 × 5 s (that verdict now means "wrong port or STM silent", no longer "config failed"). ⑤ Watch for `ERR_UART_RX_OVERFLOW` / `ERR_UART_RSP_TIMEOUT` spam and `drop stale STM frame` warnings (opcode correlation working). ⑥ Verify the driver's RX service context does not corrupt the `thread_safe = FALSE` request queue under back-to-back frames. ⑦ Verify an unmapped command falls through to hex passthrough rather than `ERROR: STM command not supported`. ⑧ Confirm opening the chosen port did not disturb the log console or the modem AT channel (no lost logs, network still attaches).
- ✅ **SMS modem configuration (2026-09-08, see §3.6.2):** `SMS modem configured` printed once per insert (latch holds, no per-cycle repeat); `AT+CMGF`, `AT+CNMI` (+CMTI mode) and `AT+CSCS` all applied; store cleared. Verified over two boots, cold and warm SIM.
- ✅ **SMS end-to-end (2026-09-08, §3.6.6):** command SMS in → `SMS URC (async): index=1`, sender parsed as `+919952929341` (non-`UNKNOWN`), forwarded to the command manager, reply delivered back as an SMS to the sender. Outbound also confirmed independently in the vendor demo (§3.6.5).
- ⬜ **SMS soak / edge cases (still pending):** several messages back-to-back — the store is only **10 slots**, so watch for index reuse or a full store (the kernel auto-deletes after dispatch, and our redundant `AT+CMGD` is expected to warn `may already be gone`); `sdk_sms_send`'s **blocking** call against the 60 s task-stall watchdog on a slow/failing network; SIM removal → re-insert re-runs configuration; the retry spacing (2 s × 10) on a cold SIM (expect a couple of `retrying (n/10)` lines ~2 s apart); a >160-char inbound body and a body containing `\r\n` through `sms_extract_fields`.
- ⬜ Network/SIM parity pass (new 2026-08-21): first boot recreates the config file at blob v5; 4-way health check runs at 65 s cadence while CONNECTED (watch for false disconnects — GET_IP inside the check must pass on this kernel); SIM insert broadcasts EVENT_SIM_AVAILABLE → network RESTART_CFUN; APN set via `weware_network_set_apn` survives reboot. ✅ Partially validated 2026-09-08: overall-timeout path performs a **persisted** soft reset (preboot record + TCP_SEND_Q written before reboot) instead of a bare reset, and SIM re-insert brought the ladder back to CONNECTED — proven by the SIM-removal store-and-forward test.


## Supported external flash:
The Below listed External flash modules are supported by the File system APIs of walnut vis SPI, No direct SPI api calls are needed.

["GD25LQ64C", "GD25LF64E", "W25Q64JW", "FM25M64C", "XM25QU64B", "XM25QU64C", "P25Q64LE", "ZG25LQ64A", "ZB25LQ64A", "EN25S64A", "BY25FQ64EL", "MX25U6432F", "XT25Q64F", "PY25Q64LB"]

1. In file: `Tools\aboot\config\flash\QSPI_NOR_8MB_B64KB_S4KB_P256.json`
```
"GD25LQ64C", "W25Q64JW", "FM25M64C", "XM25QU64B", "XM25QU64C", 
"P25Q64LE", "ZB25LQ64A"
```

2. In file: `Tools\aboot\config\flash\SPI_NOR_8MB_B64KB_S4KB_P256.json`
```
"GD25LF64E", "ZG25LQ64A", "EN25S64A", "BY25FQ64EL", "MX25U6432F", "XT25Q64F", "PY25Q64LB"
```