# Walnut Integration Progress

**Project:** WEGW_1605_4_RL — weware application on the walnut-ZX (GW1NS) SDK
**Reference:** common-gateway 1.5 (`gateway_repos/v3/newArchMain/common-gateway-1.5/modem/firmware`)
**Date:** 2026-09-08 · **Build:** ✅ green end-to-end — all weware_app objects compile with zero warnings, `customer_app.elf` links with zero undefined references, `APP sign DONE!`, and the signed `customer_app.bin` is staged to `kernel/GW1NS-4` (re-verified 2026-09-08 11:39, includes both the UART and SMS work).

> **Correction to the earlier "packaging can't run / bin is stale" note.** `ctools.exe` is **not** missing — it lives in `Code/sdk/tools/win32/` (with `dtools.exe`), which `tools/core_launch.bat` puts on PATH; `Tools/aboot` is present too. The packaging step was failing only because of two Git-Bash artifacts when `build.bat` is launched from a bash shell:
> 1. the exported PATH puts Git's `/usr/bin` ahead of `C:\Windows\System32`, so `core_launch.bat`'s `find /C /I` runs **Unix** `find` recursively over `C:\` — that is the 15+ minutes of `Permission denied` noise, after which PATH is never actually extended, so `dtools`/`ctools` are "not recognized";
> 2. `NoDefaultCurrentDirectoryInExePath` is set, so `call build.bat` cannot even be resolved from the current directory.
>
> Launch it with `C:\Windows\System32` prepended to PATH, that variable cleared, and an absolute path to `build.bat`, and the whole build (compile → link → mkappimg → appSign → stage) finishes in **~20 seconds**. From a normal `cmd.exe` prompt `.\build.bat` works unchanged.

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
| GPS (manager, ops, triggers, config, packet, storage, NMEA parse) | ✅ 2026-08-21 | Triggers/packet/storage/NMEA-parse ~100 % logic parity (HDOP + RMC/GGA cross-check live) | 5 packet fields placeholder; msg_q/BLE append dead |
| TCP (state machine, ops, login, socket layer) | ✅ 2026-08-21 | 15-state FSM + login packet + store-and-forward send path full parity; recv → command_manager (CG-verbatim, 2026-08-27) | Not runtime-tested over TCP |
| Network (16-state manager, config, health check) | ✅ 2026-08-21 | State enum, 4-way health check, timeout table, EVENT_RESET_SOFT escalation, NetworkConfig persistence — CG parity | state_timeout framework hand-rolled; CFUN settle 2+3 s vs CG 1 s |
| SIM (detect + debounce, events) | ✅ 2026-08-21 | Debounce verbatim; EVENT_SIM_* broadcasts + connected tracking | CPIN-4/6 removal detection (CG network_ops) not ported |
| URC processor | ✅ 2026-08-21 | Drain loop + fan-out (CG pattern on bare event codes) | SMS inlining / NMEA assembler moot (kernel emits no such URCs) |
| Vehicle state (ign/motion debounce) | ✅ 2026-08-20 | Logic verbatim | Accelerometer inputs stubbed FALSE |
| Main / boot flow | ✅ 2026-08-20 | CG structure (system → modules → supervised loop) | Init failures log-and-continue (CG reboots) |
| Command manager (manager, handler, config) | ✅ 2026-08-26 compiled | CG command table + waterfall state machine verbatim; API renamed to walnut (`weware_tcp_*`, `weware_sim_*`, `weware_network_*`) | The CG-verbatim binary-frame `STM:` path + PING-STM are written and compile, but `UART_UNAVAILABLE` is **restored** (2026-09-07) pending the STM port, so both still reply "unavailable". DIGOUT likewise gated via `DIGOUT_UNAVAILABLE`; not runtime-tested |
| SMS (manager, config, URC queue types) | ✅ 2026-09-07 compiled | Task loop / URC→read→validate→forward→delete pipeline / `+CMGR` parser / SIM-gated modem config / stats / init-deinit **line-for-line**; inbound feed re-sourced (see §3.6) | walnut `sdk_sms_send` is blocking + text-mode only (no per-message format/len/msgq); not runtime-tested |
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

1. **`sdk_sms_delete_all()` is broken in the SDK.** It calls `wm_sms_delete_message(index = 0, delflag = 4)` → `AT+CMGD=0,4`. SIM store indices are 1-based, so index 0 is out of range and the modem answers ERROR — this call can **never** succeed on this kernel. Fix: `sms_purge_store()`, a bounded per-index sweep that reads occupancy with `sdk_sms_get_storage_status()` (`AT+CPMS?`) and deletes in-range indices via `sdk_sms_delete()` (`AT+CMGD=<i>,0`) until `used` slots are gone, capped at `SMS_PURGE_MAX_INDEX` (60). Runs only as a fallback, after the CG-verbatim `sdk_sms_delete_all()` attempt.

2. **CG's CNMI parameters are wrong for walnut — and would have silently broken reception had they been accepted.** CG passes `(1, 2, 1, 0, 0)` → `AT+CNMI=1,2,1,0,0`: `mt=2` routes SMS-DELIVER **straight to the TE as +CMT** and `bm=1` routes cell broadcasts to the TE. This modem rejects the combination, but the important point is the walnut inbound hook is a **+CMTI** callback (`smsSetCmtiCallback` / `wm_sms_cmti_cb`, kernel log `sms cmti: storage=%s, index=%d`), which only fires when the message is **stored and indicated** — i.e. `mt=1`. `wm_sms_init()` already applies `AT+CNMI=2,1,0,0,0` (plus `AT+CMGF=1`, `AT+CSCS="GSM"`), so the failing override was the only thing that could have taken the pipeline out of +CMTI mode. Changed to assert the kernel's own values: `sdk_sms_set_new_msg_ind(2, 1, 0, 0, 0)`.

**Also changed while fixing these:**
- `sms_configure()` latched `s_sms_modem_config_applied = TRUE` unconditionally (CG behaviour), so a partially-configured modem was never retried. It now retries up to `SMS_CONFIG_MAX_ATTEMPTS` (5) when an *essential* call fails (format / CNMI / charset / purge), then latches anyway with an error — bounded so a permanently failing modem cannot turn the 100 ms task cycle into an AT flood. The counter resets on SIM removal.
- Every `sdk_debug_print()` in the module was missing `\r\n`, which is why the log above runs together — all 15 now terminate their lines.
- Noted in code: the prebuilt `sdk_sms_init()` is `wm_sms_init()` + `wm_sms_set_incoming_cb()` with a hardcoded `return 0` — it can never report failure, so that error branch is dead code (kept for API correctness).

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
3. **Event broadcasts from modules** — TCP done 2026-08-21; SIM done 2026-08-21 (`EVENT_SIM_AVAILABLE/UNAVAILABLE` + connected tracking); network overall-timeout → `EVENT_RESET_SOFT` done 2026-08-21. Still open: GPS connected-disconnected events and the GPS parse-fail `sdk_system_reboot()` → `EVENT_RESET_SOFT` switch.
4. **Config persistence wiring** — network done 2026-08-21 (`weware_network_set_apn`/`network_config_set` persist; NetworkConfig stored, blob v5). Still open: `gps_config_set` / `weware_tcp_config_set` should call `config_get_current() + config_save_to_file(NULL)` (system_config and network already do).
5. **Packet placeholder bytes** — input-wire mV, external mV, battery % (all already computed in `g_power_info`), digout, accel orientation.
6. ~~SENDING_DATA liveness~~ ✅ 2026-08-21 — 100 s keepalive restored (CG table row incl. `reset_overall=FALSE`).
7. ~~Login+GPS~~ ✅ 2026-08-21 — last-valid GPS appended when `loginwithgps` set; login buffer 79 B.

## 5. Not ported yet

~~Command manager~~ (compiled 2026-08-26 — see §1; `STM:`/PING-STM live since 2026-09-07, DIGOUT still stubbed until the digout manager lands) · ~~SMS manager~~ (compiled 2026-09-07 — see §3.6) · ~~UART manager~~ (compiled 2026-09-07 — see §3.7) · BLE manager + ag_dfu · OTA manager · digout (relay) manager · accelerometer (STK8321 — blocked on walnut I2C: `sdk_i2c` has no transfer API; needs vendor `i2cc_*` driver) · file-transfer · CG async log ring · HTTPS ops.

**Blocked behind the UART port question** (the UART module itself is written but disabled — see the status correction at the top): once the STM's physical port is confirmed and `MODULE_ID_UART` is re-enabled, clearing `HEALTH_PACKET_UNAVAILABLE` restores the type-40 health push on `system_alive` (and the `system_alive` main-loop watchdog message, still not wired from the system manager), and clearing `FILE_TRANSFER_UNAVAILABLE` routes STM-OTA / peri-OTA acks and restores `ota_manager_get_status_string()`'s live-progress branch. Both need their own module ports first.

## 6. Validation status

- ✅ Compiles and links (GW1NS-4, XIP, 4 MB flash). ⚠️ Image **not** signed/staged for the 2026-09-07 UART change — see the Build line at the top.
- ⬜ **Storage dirs (2026-08-26 fix, verify first):** boot log must show no `[FLASHP] E: ensure_dir` lines and no `[FILESYS] E: write_file: open failed path='C:/preboot/...'`; `[PREBOOT] E: tcpq_clr` spam gone; after a soft reset the `WEWARE STATUS` line reports `Reboot: SOFT/<module> #n` (preboot record round-trips); `C:/config/system_config.dat` and `C:/queue/*.dat` get created. First boot on an already-flashed unit was observed 2026-08-26 with dirs missing → this fix.
- ⬜ On-target boot: verify module init pass, `WEWARE STATUS` line, `[ADC]` readings plausibility, config file creation on first boot, watchdog doesn't false-trigger (all five tasks feed uptime; loops iterate ≤1 s).
- ⬜ Regression: GPS trigger cadence, TCP login/ACK against stage server, network reconnect ladder, reboot persistence (preboot record + GPS last-valid).
- ⬜ TCP store-and-forward (new 2026-08-21): backlog accumulates while TCP down and drains in ≤5-row batches on reconnect; rows survive a soft reboot (TCP_SEND_Q snapshot); 100 s idle keepalive cycles the connection without wedging; login+GPS frame accepted by stage server (79 B).
- ⬜ GPS NMEA source (new 2026-08-21, **highest on-target risk — the kernel NMEA callback replaces the validated navdata poll as the sole source**): verify `[nmea]` parse lines appear at 1 Hz, HDOP byte non-zero in packets, no parse-fail streak (a silent callback now escalates to reboot after 60 s), talker prefix is $GN or $GP. Revert switch: rebuild with `-DGPS_QUERY_NAVDATA_POLL`.
- ⬜ **UART / STM — currently DISABLED, nothing to test until re-enabled.** ① **Which physical port is the STM on?** The one open unknown, now narrowed to two candidates: `DRV_UART_PORT_3` (idx 2, `0xd401f000`, the default and recommended) or `DRV_UART_PORT_2` (idx 1, `0xd4018000`, the AT channel — try only if idx 2 gives no STM reply, and watch that network attach still works). Never idx 0 (driver rejects) or idx 3 (resets — now blocked by a `_Static_assert`). Re-enable per the 3 steps in `module_config.c`; success looks like `[WUART] port=2 open @115200 8-1-0 flow=0`, and a wrong-but-valid port just yields `STM: FAIL (no response)`. ② Confirm `[UART] I: UART manager ready` with **no** `STM binary protocol self-test FAILED` (a CRC/frame mismatch would silently corrupt every frame). ③ Confirm 115200-8-N-1-no-flow matches the STM — a wrong baud fails silently as CRC-bad frames, so watch for a `0xAA` resync / `STM_FRAME_BAD` pattern rather than an error. ④ Send `STM:get-device-info` by SMS → expect `Ok,<fw>,<hw>,<MAC>,…`; then `PING-STM` → expect `STM: OK`, not `STM: FAIL (no response)` after 3 × 5 s (that verdict now means "wrong port or STM silent", no longer "config failed"). ⑤ Watch for `ERR_UART_RX_OVERFLOW` / `ERR_UART_RSP_TIMEOUT` spam and `drop stale STM frame` warnings (opcode correlation working). ⑥ Verify the driver's RX service context does not corrupt the `thread_safe = FALSE` request queue under back-to-back frames. ⑦ Verify an unmapped command falls through to hex passthrough rather than `ERROR: STM command not supported`. ⑧ Confirm opening the chosen port did not disturb the log console or the modem AT channel (no lost logs, network still attaches).
- ⬜ **SMS (new 2026-09-07):** after SIM insert the boot log shows `[SMS] I: SMS modem configured` once (not every cycle — `s_sms_modem_config_applied` latch); send a command SMS to the device number and confirm `[SMS] D: SMS URC (async): index=…`, a non-`UNKNOWN` sender in the forwarded request, the command reply arriving back as an SMS at the sender's number, and the message being deleted from the store (no index reuse / store-full after several messages); verify `sdk_sms_send`'s blocking call does not trip the task-stall watchdog (60 s) on a slow network; verify SIM removal clears the latch and re-configuration happens on re-insert.
- ⬜ Network/SIM parity pass (new 2026-08-21): first boot recreates the config file at blob v5; 4-way health check runs at 65 s cadence while CONNECTED (watch for false disconnects — GET_IP inside the check must pass on this kernel); SIM insert broadcasts EVENT_SIM_AVAILABLE → network RESTART_CFUN; overall-timeout path performs a persisted soft reset (preboot record written) instead of a bare reset; APN set via `weware_network_set_apn` survives reboot.
