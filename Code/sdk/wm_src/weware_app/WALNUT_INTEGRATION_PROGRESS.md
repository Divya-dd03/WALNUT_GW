# Walnut Integration Progress

**Project:** WEGW_1605_4_RL — weware application on the walnut-ZX (GW1NS) SDK
**Reference:** common-gateway 1.5 (`gateway_repos/v3/newArchMain/common-gateway-1.5/modem/firmware`)
**Date:** 2026-08-20 · **Build:** ✅ green — all 38 weware_app objects compile, `customer_app.elf` links, signed `customer_app.bin` staged to `kernel/GW1NS-4`

---

## 1. Status at a glance

| Module / layer | Ported | Fidelity vs CG | Biggest gap |
|---|---|---|---|
| System infra (events, queues, config store, module framework, reset, storage, time) | ✅ 2026-08-20 | Near-verbatim via compat shims | Board pins / ADC channels unconfirmed |
| GPS (manager, ops, triggers, config, packet, storage, NMEA parse) | ✅ 2026-08-21 | Triggers/packet/storage/NMEA-parse ~100 % logic parity (HDOP + RMC/GGA cross-check live) | 5 packet fields placeholder; msg_q/BLE append dead |
| TCP (state machine, ops, login, socket layer) | ✅ 2026-08-21 | 15-state FSM + login packet + store-and-forward send path full parity | Recv side goes to rx callback (command manager not ported) |
| Network (16-state manager, config, health check) | ✅ 2026-08-21 | State enum, 4-way health check, timeout table, EVENT_RESET_SOFT escalation, NetworkConfig persistence — CG parity | state_timeout framework hand-rolled; CFUN settle 2+3 s vs CG 1 s |
| SIM (detect + debounce, events) | ✅ 2026-08-21 | Debounce verbatim; EVENT_SIM_* broadcasts + connected tracking | CPIN-4/6 removal detection (CG network_ops) not ported |
| URC processor | ✅ 2026-08-21 | Drain loop + fan-out (CG pattern on bare event codes) | SMS inlining / NMEA assembler moot (kernel emits no such URCs) |
| Vehicle state (ign/motion debounce) | ✅ 2026-08-20 | Logic verbatim | Accelerometer inputs stubbed FALSE |
| Main / boot flow | ✅ 2026-08-20 | CG structure (system → modules → supervised loop) | Init failures log-and-continue (CG reboots) |
| Command manager, SMS, UART, BLE, OTA, digout, accel, file-transfer | ❌ not ported | — | See §5 |

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
| `module/module_config.c` | Full CG `ModuleId` order kept; **enabled:** LOG, URC, SIM, NETWORK, TCP, GPS, SYSTEM. **Disabled slots:** UART, CMD, SMS, BLE. TCP_SEND_Q config staged but `msg_q = NULL` until TCP consumes it |
| `system/system_config.c` | ign/motion thresholds + parser verbatim; TCP `ign_det_*` migration/writeback dropped (walnut `WewareTcpConfig` has no such fields); `accel_en` defaults **FALSE** (no accel driver) |
| `system/vehicle_state.c` | Debounce/anchor/stop-hold logic verbatim; accel reads stubbed FALSE; debounced edges now **drive the walnut GPS latches** (`gps_manager_set_ignition/motion_status`) in addition to event broadcasts |
| `system/system_manager.c` | ADC poll via new `adc_manager` (mV→V over `sdk_adc`); charge edge also drives `gps_manager_set_charge_status`; netlight/status/power-select GPIO **disabled until pins provided** (`-DSDK_GPIO_NETLIGHT_PIN=…`); A-GPS *not* driven here (walnut GPS task does it itself); OTA/digout/accel are TODO stubs |
| `module/log/*` | CG `log.h` compile-time-filtered `LOG_*` macros kept; backend `log_printf` → `RTI_LOG` (AP UART console); async ring not ported |
| `system/adc/adc_manager.c`, `system/gpio/gpio_manager.c` | New thin wrappers over `sdk_adc` / `sdk_gpio` |
| `common/types.h` | CG `Result` enum (numerically compatible with `SdkResult`, so registry casts work as in CG) |

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
  1. Recv side routes to `weware_tcp_register_rx_callback` instead of CG's command_manager (not ported); command/BLE response path (`utils_route_response_to_module`) now reaches the queue but nothing generates responses yet.
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
- Deliberately not ported (kernel emits no such URCs): SMS `arg3` inlining, the 768-byte GNRMC/GNGGA reassembler. The NMEA path found its feed elsewhere: `sdk_gps_set_nmea_callback()` → GPS module pairing (see §3.1) — it bypasses the URC processor entirely.

---

## 4. Cross-cutting gaps (priority order)

1. ~~TCP store-and-forward~~ ✅ 2026-08-21 — TCP_SEND_Q live: batch send / ACK-commit / live-first / reboot persistence; GPS backlog absorbed; command/BLE response path has a landing queue.
2. **Board constants** — GW1NS ADC channels (`ADC_CHANNEL_IGNITION/EXTERNAL`, default 1/2), divider gain (default 131), GPIO pins (netlight/status/power-select, default disabled). Ignition/voltage readings are untrusted until confirmed.
3. **Event broadcasts from modules** — TCP done 2026-08-21; SIM done 2026-08-21 (`EVENT_SIM_AVAILABLE/UNAVAILABLE` + connected tracking); network overall-timeout → `EVENT_RESET_SOFT` done 2026-08-21. Still open: GPS connected-disconnected events and the GPS parse-fail `sdk_system_reboot()` → `EVENT_RESET_SOFT` switch.
4. **Config persistence wiring** — network done 2026-08-21 (`weware_network_set_apn`/`network_config_set` persist; NetworkConfig stored, blob v5). Still open: `gps_config_set` / `weware_tcp_config_set` should call `config_get_current() + config_save_to_file(NULL)` (system_config and network already do).
5. **Packet placeholder bytes** — input-wire mV, external mV, battery % (all already computed in `g_power_info`), digout, accel orientation.
6. ~~SENDING_DATA liveness~~ ✅ 2026-08-21 — 100 s keepalive restored (CG table row incl. `reset_overall=FALSE`).
7. ~~Login+GPS~~ ✅ 2026-08-21 — last-valid GPS appended when `loginwithgps` set; login buffer 79 B.

## 5. Not ported yet

Command manager (SMS/TCP command handling — GPS/TCP/system config setters are compiled but unreachable without it) · SMS manager · UART manager (incl. `system_alive` main-loop watchdog message) · BLE manager + ag_dfu · OTA manager · digout (relay) manager · accelerometer (STK8321 — blocked on walnut I2C: `sdk_i2c` has no transfer API; needs vendor `i2cc_*` driver) · file-transfer · CG async log ring · HTTPS ops.

## 6. Validation status

- ✅ Compiles and links (GW1NS-4, XIP, 4 MB flash); image signed and staged.
- ⬜ On-target boot: verify module init pass, `WEWARE STATUS` line, `[ADC]` readings plausibility, config file creation on first boot, watchdog doesn't false-trigger (all five tasks feed uptime; loops iterate ≤1 s).
- ⬜ Regression: GPS trigger cadence, TCP login/ACK against stage server, network reconnect ladder, reboot persistence (preboot record + GPS last-valid).
- ⬜ TCP store-and-forward (new 2026-08-21): backlog accumulates while TCP down and drains in ≤5-row batches on reconnect; rows survive a soft reboot (TCP_SEND_Q snapshot); 100 s idle keepalive cycles the connection without wedging; login+GPS frame accepted by stage server (79 B).
- ⬜ GPS NMEA source (new 2026-08-21, **highest on-target risk — the kernel NMEA callback replaces the validated navdata poll as the sole source**): verify `[nmea]` parse lines appear at 1 Hz, HDOP byte non-zero in packets, no parse-fail streak (a silent callback now escalates to reboot after 60 s), talker prefix is $GN or $GP. Revert switch: rebuild with `-DGPS_QUERY_NAVDATA_POLL`.
- ⬜ Network/SIM parity pass (new 2026-08-21): first boot recreates the config file at blob v5; 4-way health check runs at 65 s cadence while CONNECTED (watch for false disconnects — GET_IP inside the check must pass on this kernel); SIM insert broadcasts EVENT_SIM_AVAILABLE → network RESTART_CFUN; overall-timeout path performs a persisted soft reset (preboot record written) instead of a bare reset; APN set via `weware_network_set_apn` survives reboot.
