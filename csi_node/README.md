# Unified CSI node (ESP32-S3 / ESP-IDF 6.0.1)

One binary supports logical nodes A, B, C, and D. `1X3` fixes A as the sensing transmitter. `2X2` alternates A/B transmitting with C/D receiving, then C/D transmitting with A/B receiving. Each active transmitter targets **40 Hz** by default. Nominal phase duration defaults to **5000 ms**. Radio contention can extend slots and reduce the measured rate; the firmware reports these extensions rather than shortening another node's response window.

The firmware always boots stopped, including after configuration has been saved. An unconfigured board sends no application packets. NVS namespace `csi_node_v2` is independent of legacy `csi_ctrl`; legacy rate/power settings are never loaded or erased. The partition-table offset stays `0xa000`, matching the existing sender/receiver projects.

## Build

In an ESP-IDF 6.0.1 shell:

```sh
cd 2x2-firmware/csi_node
idf.py set-target esp32s3
idf.py build
```

The console is UART at **921600 baud, 8N1**. Before replacing an existing device image, retain its flash/NVS backup. Flashing this project changes that device's firmware; configuring its role is a separate UART command. The node uses channel 11, HT40 with the secondary channel below, MCS0 long guard interval, and power setting 80 quarter-dBm units.

## UART commands

Each command is ASCII followed by LF. Tokens and node IDs are case sensitive. Configure each physical board with a **different** ID:

```text
NODE CONFIG A 1X3 40 5000
NODE CONFIG B 1X3 40 5000
NODE CONFIG C 1X3 40 5000
NODE CONFIG D 1X3 40 5000
```

The four commands above go to four respective boards. `CONFIG` stops that board, saves its ID/mode/rate/phase duration, and leaves it stopped. Supported rates are 5–40 Hz where `1000000 % (rate * active_tx_count) == 0`; phase duration is 1000–600000 ms and must satisfy `(phase_ms * rate) % 1000 == 0`. These constraints preserve integer microsecond slots and complete transmitter pairs per phase. Defaults 40/5000 satisfy them.

To start, send `NODE START 1234` to B, C, and D **first**, then to A. Session is a shared, nonzero unsigned 32-bit integer. Use a new session number for every experiment. A begins slot 0 after a 100 ms startup delay. Only A starts the over-the-air schedule; the other nodes wait for grants. Absent nodes leave empty slots, so A+B can test `1X3`, and A+C can test both directions of `2X2`.

```text
NODE STATUS
NODE STOP
NODE CONFIG A 2X2 40 5000
NODE START 1235
```

Apply the new mode and same settings to every participating board, retaining its own ID. Arm receivers/other nodes before A. `STOP` is local and disables that board's radio; stop A to remove all subsequent grants. A remote node's already granted, in-flight packet can finish before its lease expires. Stopping every board ends the session completely. Restart after a fault with a fresh session on all participants.

## Replies and status

```text
NODE_ACK,CONFIG,OK,saved
NODE_ACK,START,OK,armed
NODE_ACK,STOP,OK,stopped
NODE_ACK,STATUS,OK,status
NODE_ACK,<operation>,ERROR,<reason>
```

Successful commands are followed by these three lines:

```text
NODE_STATUS,id,mode,rate_hz,phase_ms,state,session,phase,slot,tx_count,csi_count,csi_dropped,late_dropped,send_errors,mac,factory_mac
NODE_TIMING,received_grants,grant_age_min_us,grant_age_max_us,expired_grants
NODE_TX_TIMING,grant_completions,data_completions,grant_min_us,grant_max_us,data_min_us,data_max_us,guard_extensions,guard_extension_us,fault_packet,fault_age_us
```

Those examples name fields; actual lines contain values. `id` is A/B/C/D or `UNSET`. `state` is STOPPED, ARMED (started slave), RUNNING (started coordinator A), or FAULT. RUNNING/ARMED identify scheduling state, not the current sensing role. `tx_count` counts successful local DATA send callbacks, `csi_count` counts records queued, and `csi_dropped` counts full CSI queues. `late_dropped` counts expired/skipped submissions or deadline faults, and `send_errors` counts driver submission/send failures. Counts reset on START. Phase/slot identify the latest accepted/issued grant and remain available after STOP/FAULT; no-grant status reports zero.

`NODE_TX_TIMING` measures submission-to-callback durations separately for GRANT and DATA, including failed callbacks. Minimum zero means no completed transmission of that type. `guard_extensions` counts coordinator slot deadlines pushed forward after a delayed GRANT; `guard_extension_us` sums these shifts. `fault_packet` is NONE, GRANT, DATA, or SLOT; `fault_age_us` records the first fault's elapsed send time (zero for slot exhaustion). A watchdog fault before a callback can appear in fault fields without a completion count.

Configured IDs map to locally administered MACs `02:43:53:49:02:01` through `02:43:53:49:02:04`. The immutable factory STA MAC is also reported to identify each physical board. These logical MACs must be unique in the experiment; two boards configured with A conflict. This explicit mapping lets every node reject packets whose logical sender and source MAC disagree.

`NODE_EVENT,BOOT,stopped` marks boot. `NODE_EVENT,FAULT,radio_deadline_or_slot_exhausted` marks a stopped radio after an overlong send or exhausted 32-bit slot counter. Fault recovery requires an explicit START; there is no automatic restart.

## Scheduling and packet safety

At 40 Hz, `1X3` has 25,000 µs slots, A sending one DATA packet per slot to B/C/D. `2X2` has 12,500 µs slots, alternating the two active transmitters. With 5000 ms phases:

| Global slots | Phase | Sensing TX | Sensing RX |
| --- | --- | --- | --- |
| 0–399 | 0 | A on even slots, B on odd slots | C and D |
| 400–799 | 1 | C on even slots, D on odd slots | A and B |
| 800–1199 | 2 | A on even slots, B on odd slots | C and D |

A broadcasts one GRANT per slot. Only its named transmitter may submit one DATA response, 1000–2500 µs after the grant. Slaves never transmit from their own periodic timers. Missing, duplicate, wrong-session, invalid-schedule, or expired grants do not permit DATA. Coordinator timing uses an absolute schedule and skips missed deadlines; it never sends catch-up bursts. Missing boards and isolated packet loss do not stop the schedule.

The grant carries the full session, phase, slot, sequence, mode/rate/phase duration, transmitter ID, and receiver mask. Receivers keep only DATA matching their current grant and receiver mask, within an 11,000 µs receive lease, once per slot. Control packets are excluded before CSI buffering. A still emits these short control grants while logically acting as a sensing receiver in odd `2X2` phases.

Each radio has at most one pending ESP-NOW transmission. A DATA submission rechecks its grant and deadline immediately before calling the driver. DATA has an **8000 µs** completion watchdog. Coordinator GRANT has a separate **100,000 µs** driver-failure watchdog: a delayed GRANT stalls the schedule, and A issues neither its DATA response nor another GRANT while that send is pending. Slot IDs are not advanced or replaced during this wait. After its completion, the full response window below is reserved. Exceeding the corresponding watchdog faults that node and stops its radio. There are no application retransmissions.

After every GRANT completion, A sets the next slot deadline to `max(nominal_deadline, grant_completion + 11000 µs)`. This reserves 2500 µs for a response submission, 8000 µs for that sender's DATA completion, and a 500 µs guard; the longer GRANT watchdog does not increase another node's DATA allowance. A GRANT completing within the first 1500 µs of a nominal 12.5 ms slot leaves the target cadence unchanged. Slower GRANTs extend the next deadline. The extension carries forward, so the scheduler never bursts to catch up. Phase boundaries remain based on slot count: 400 slots per default `2X2` phase, so contention can make an actual phase longer than 5 seconds. Report measured per-link rates and phase spans alongside the nominal configuration.

Grant age is measured from the RF receive timestamp, using `esp_wifi_internal_get_mac_clock_time() - rx_ctrl.timestamp` modulo 32 bits; callback-delivery delay cannot renew a lease. This private API is intentionally tied to the specified IDF version. `NODE_TIMING` exposes its measured ages and rejection count for real-board verification. Wi-Fi power saving is disabled. The ESP-NOW driver does not expose a hard per-frame RF deadline, so software guards and fault shutdown are not a hard real-time RF guarantee under arbitrary driver/interrupt stalls. Validate send-deadline and grant-age counters on the actual boards under channel interference.

## CSI CSV v2

The UART header and row prefix are:

```text
type,session,phase,slot,tx_id,rx_id,seq,rx_timestamp,rssi,channel,secondary_channel,sig_mode,mcs,bandwidth,noise_floor,first_word_invalid,csi_len,csi_b64
CSI_DATA_V2,...
```

`tx_id`/`rx_id` are letters. `seq` equals the **global slot**, not a contiguous per-transmitter counter. Phase is an increasing integer in `2X2`, always zero in `1X3`. `rx_timestamp` is the receiver's 32-bit local Wi-Fi microsecond timestamp and wraps approximately every 71 minutes; it is not a shared wall clock. Compare links using session/phase/slot/tx_id, not absolute receiver timestamp. RSSI/noise are signed dBm fields. `bandwidth` is the IDF `cwb` bit, and `first_word_invalid=1` means the first four CSI bytes are invalid.

`csi_b64` is RFC 4648 base64 of exactly `csi_len` raw CSI bytes (1–768). Decode each byte as signed int8; for example `AAF/gP8=` represents `[0,1,127,-128,-1]`. Preserve the original bytes and invalid-word flag; the firmware performs no gain compensation. The compact encoding keeps the 80-record/s active receiver stream within 921600-baud capacity for typical 384-byte CSI records. At the maximum 768 bytes the line budget has little headroom; monitor `csi_dropped` and avoid sustained status polling while collecting.

The Wi-Fi CSI callback validates the bounded ESP-NOW action body using the IDF `payload_len`, copies a record into a 32-entry queue with a zero wait, and returns. A separate task performs base64 encoding and all CSV printing. Queue overflow increments counters instead of blocking the Wi-Fi task. `NODE_*` diagnostics and boot logs are not CSI records.

ESP-NOW framing is based on the [Espressif v6.0 protocol description](https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32/api-reference/network/esp_now.html). The parser validates the category, OUIs, vendor IE size/type/version, protocol header, and full packet size before accessing fields. Only a single unfragmented vendor IE is accepted; all protocol packets are 36 bytes.

## Verification

On 2026-10-14, two ESP32-S3 boards configured as A and C passed a physical pair test:
301 A-to-C records in an 8-second `1X3` test; 499 A-to-C and 373 C-to-A
records across phases 0–4 in a 24-second `2X2` test. No malformed CSI,
firmware CSI queue overflow, send errors, or fault state was observed in
the final run. Stopping A stopped further C transmissions. Both boards
retain configuration and boot STOPPED after reset.

This is a two-board test with B/D absent, not a full four-board or long-duration
validation. `2X2` measured schedule rate was approximately 39.05–39.09 Hz per
active transmitter; recorded receive rate was 35.75–37.09 Hz. Radio/grant loss
remains, and the timing guard extended the schedule by about 0.555 seconds
over the run. Do not interpret zero queue overflow as zero over-the-air loss.

```sh
cd 2x2-firmware
python -m pytest tests/test_node_protocol.py -q
```

The test compiles the actual firmware protocol header with a native C compiler and runs exhaustive short-frame truncation checks, schedule/phase counts, wrong-session/source/schedule rejection, lease boundary checks, MAC-clock rollover, base64 vectors, and next-slot guard checks across every completion delay from 0 to 8000 µs. A firmware build checks the IDF API integration. Physical tests must additionally verify UART integrity, `NODE_TIMING` ages, `NODE_TX_TIMING` send durations/extensions, both phase directions, reboot-stopped behavior, and no continuing DATA when grants stop.
