# SprinklerMaster

A bare-metal 8-zone sprinkler controller running on the Raspberry Pi Pico 2W, using dual-core cooperative super loops instead of an RTOS.

---

## The Story

This project started as an experiment in running FreeRTOS on the RP2350 and ended up teaching a hard lesson about why simpler is often better.

### Phase 1: FreeRTOS

The first iteration carved the system into FreeRTOS tasks -- one for WiFi/HTTP, one for sensor reads, one for the scheduler, one for the LCD. It looked clean on paper. In practice, the interactions were brutal.

The DHT11 temperature sensor requires precise bit-banged timing and disables interrupts for roughly two seconds during each read. Flash writes on the RP2350 require halting *both* cores and disabling interrupts while the erase/program cycle completes. The WiFi stack (cyw43/lwIP) demands frequent polling or it drops connections. These three constraints are fundamentally hostile to each other.

Tasks competed for the I2C bus (shared between the LCD and the RTC), fought over access to the shared configuration structure, and triggered priority inversions that ended in watchdog resets. Debugging deadlocks on a dual-core microcontroller with no JTAG attached is an exercise in reading tea leaves. The system worked most of the time, which is the worst kind of failure mode for an irrigation controller -- it would occasionally lock up and leave a zone running for hours.

### Phase 2: Single Super Loop

The fix was to rip out FreeRTOS entirely and replace it with a single polling loop. One loop, one core, no preemption, no locks. Every subsystem got a polling function called at a fixed interval. The scheduler checks every 5 seconds. The DHT11 reads every 3 seconds. The LCD updates every second. Flash writes happen inline with interrupts disabled.

This eliminated every deadlock and priority inversion overnight. The system became completely predictable. But it introduced a new problem: when the DHT11 read blocked for two seconds, the WiFi stack starved. HTTP requests would time out. The cyw43 driver would lose its connection to the wireless chip. For a device that needs to serve a web UI, this was unacceptable.

### Phase 3: Dual-Core Super Loops

The final architecture splits the work across both Cortex-M33 cores on the RP2350.

Core 0 runs the "physical world" loop: sensor reads, schedule evaluation, zone control, LCD updates, and watchdog feeding. Core 1 runs the "network world" loop: cyw43 polling, lwIP/HTTP serving, WiFi reconnection, and its own watchdog. The two cores communicate through a lock-free single-producer single-consumer ring buffer. Core 1 (the network side) enqueues commands -- turn on a zone, reload config, run a program. Core 0 drains the queue each iteration and executes them.

The hardware multicore FIFO (the RP2350's built-in inter-core mailbox) is deliberately *not* used for application messages. It is reserved exclusively for `flash_safe_execute` lockout signaling, which needs the FIFO IRQ to halt Core 1 during flash operations. Using it for anything else would corrupt the lockout protocol.

The result: no mutexes, no deadlocks, no priority inversions, no starvation. Both cores run flat out doing exactly what they are good at. The WiFi stack gets sub-millisecond polling. The DHT11 can block for as long as it needs without affecting network responsiveness.

---

## Architecture Overview

```
Core 0 (Physical World)              Core 1 (Network World)
+---------------------------------+  +---------------------------------+
| Scheduler        (every 5s)    |  | cyw43_arch_poll()  (every ~1ms) |
| DHT11 sensor     (every 3s)    |  | lwIP / HTTP server              |
| LCD display      (every 1s)    |  | WiFi reconnect     (every 30s) |
| Zone safety      (every 1s)    |  | LED heartbeat      (every 1s)  |
| Watchdog feed                  |  | Watchdog feed                  |
| Memory stats     (every 60s)   |  |                                 |
+---------------------------------+  +---------------------------------+
         ^                                      |
         |         SPSC Ring Buffer             |
         +--------------------------------------+
              (zone on/off, config reload,
               program run)

         Flash writes: multicore_lockout
         halts Core 1 via hardware FIFO IRQ
```

### Polling Intervals

| Subsystem | Interval | Core |
|-----------|----------|------|
| cyw43 (WiFi driver) | ~1 ms | 1 |
| LCD display | 1 s | 0 |
| Zone safety timeout check | 1 s | 0 |
| LED heartbeat | 1 s | 1 |
| DHT11 temperature/humidity | 3 s | 0 |
| Scheduler evaluation | 5 s | 0 |
| WiFi connection check | 30 s | 1 |
| Memory statistics | 60 s | 0 |

### Inter-Core Communication

The SPSC ring buffer (`g_cmd_queue`) holds 8 command slots. Each command is a packed 32-bit word:

| Bits | Field |
|------|-------|
| 31-24 | Command type |
| 23-16 | Zone or program ID |
| 15-0 | Duration (minutes, for zone commands) |

Commands:

| Command | Code | Description |
|---------|------|-------------|
| `NET_CMD_CONFIG_RELOAD` | `0x01` | Reload config from flash after a CGI write |
| `NET_CMD_ZONE_ON` | `0x02` | Turn on a zone for a given duration |
| `NET_CMD_ZONE_OFF` | `0x03` | Turn off the active zone |
| `NET_CMD_PROGRAM_RUN` | `0x04` | Start executing a program |

Core 1 (HTTP/CGI handlers) is the sole producer. Core 0 is the sole consumer. No locks required.

### Flash Safety

Writing to flash on the RP2350 requires that no code executes from flash during the erase/program cycle. The sequence:

1. `multicore_lockout_start_timeout_us(1000000)` -- halts Core 1 via the hardware FIFO IRQ (1-second timeout)
2. `save_and_disable_interrupts()` -- disables all interrupts on Core 0
3. `flash_range_erase()` + `flash_range_program()` -- erases and writes the 4 KB config sector
4. `restore_interrupts()` -- re-enables interrupts
5. `multicore_lockout_end_blocking()` -- releases Core 1

The watchdog is fed immediately before the flash operation to prevent a reset during the write window.

---

## Features

### Zone Control

Eight independently controllable irrigation zones, each mapped to a dedicated GPIO pin. Only one zone may be active at any time -- turning on a new zone automatically shuts off the previous one. A 4-hour safety timeout guarantees that no zone can run indefinitely, even if the controlling software hangs or a command is lost.

### Scheduling

Up to 20 schedules, each binding a zone to a time and duration. Two scheduling modes:

- **Weekly**: A 7-bit day mask selects which days of the week (Sunday through Saturday) the schedule fires.
- **Interval**: Fires every N days, tracked via `last_run_day` and `last_run_year` to survive power cycles.

Each schedule stores its hour (0-23), minute (0-59), and duration in minutes. Schedules can be individually enabled or disabled without deleting them.

### Programs

Up to 4 programs, each containing up to 8 sequential steps. Each step specifies a zone and a duration. When a program runs, it advances through its steps automatically -- turning off the current zone and turning on the next when each step's duration expires. Programs support the same weekly and interval scheduling as individual schedules, plus a "Run Now" option from the web UI.

### Temperature and Humidity

A DHT11 sensor on GPIO 15 provides ambient temperature (in Fahrenheit) and relative humidity. The sensor is polled every 3 seconds with bit-banged timing and checksum validation. Readings are displayed on the dashboard and used by the freeze protection system.

### Freeze Protection

When the DHT11 reports a temperature at or below 35 degrees Fahrenheit, all scheduled watering is blocked. Skipped events are logged to the watering history as "freeze_skip" entries so the operator can see what was suppressed and when. Freeze protection can be toggled from the settings page.

### Timekeeping

Dual time sources ensure accurate scheduling:

- **DS3231 RTC**: Battery-backed real-time clock on a dedicated I2C bus. Maintains time across power outages. The oscillator-stop flag (OSF) is monitored to detect battery failure.
- **NTP sync**: On WiFi connection, the system syncs against `pool.ntp.org` and `time.google.com` with a 5-minute timeout. NTP time is compared against the DS3231; if the drift exceeds 2 seconds, the RTC is updated.

Timezone is configured as a POSIX TZ string (e.g., `MST7MDT,M3.2.0,M11.1.0`), supporting arbitrary UTC offsets and daylight saving rules. Default is `MST7` (Arizona, no DST).

### WiFi

The system operates in station mode using saved credentials. If it cannot connect (no saved credentials or the network is unreachable), it falls back to AP mode. In station mode, connectivity is checked every 30 seconds with automatic reconnection on failure.

### AP Setup Mode

When no WiFi credentials are saved or the configured network is unavailable, the device creates an open access point named "SprinklerSetup" at `192.168.4.1` with a built-in DHCP server. A setup page (`setup.html`) allows the user to enter WiFi credentials. On submission, the credentials are saved to flash and the device reboots into station mode.

### HTTP Server

The web interface is served by lwIP's httpd running on Core 1. Dynamic content is injected through 42 SSI (Server Side Include) tags that populate HTML templates with live data -- sensor readings, zone states, schedules, programs, history, and system status. API endpoints use CGI handlers that accept URL query parameters, write to flash when needed, and push reload commands to Core 0 through the SPSC queue.

SSI templates return `.shtml` pages for the UI and `.json` responses for API consumers.

### Web Interface

The device serves a complete single-device web application with the following pages:

- **Dashboard** (`dashboard.html`) -- live sensor readings, active zone status, system overview
- **Zones** (`zones.shtml`) -- zone names, GPIO assignments, enable/disable toggles
- **Schedules** (`schedules.shtml`) -- create, edit, delete, and enable/disable schedules
- **Programs** (`programs.shtml`) -- multi-step program editor with Run Now
- **Settings** (`settings.shtml`) -- timezone, freeze protection, WiFi, system info
- **History** (`history.shtml`) -- watering event log with trigger source and timestamps
- **Setup** (`setup.html`) -- WiFi credential entry (AP mode)

### LCD Display

A 16x2 I2C LCD (PCF8574 backpack at address `0x27`) provides at-a-glance status. Line 0 shows the device IP address (or "AP Mode" / "No WiFi"). Line 1 rotates between the next scheduled run time and the currently active zone with remaining duration.

### Watering History

A 50-event circular buffer records every watering action with the zone number, trigger source (schedule, program, manual, or freeze_skip), duration, and timestamp. The history survives in RAM but not across reboots. It is exported as a JSON array through the `/api/history.json` endpoint and displayed on the history page.

### Fault Tolerance

An 8-second watchdog timer runs on both cores independently. It is enabled after a 60-second stabilization window following boot to allow WiFi connection and NTP sync to complete without false triggers. The system detects watchdog-caused resets and maintains a persistent count of total boots and watchdog resets in flash. Zone safety timeouts provide a hardware-independent backstop: if any zone has been on for more than 4 hours, it is forcibly shut off regardless of software state.

### Configuration Persistence

All settings (zones, schedules, programs, WiFi credentials, timezone, freeze protection) are stored in the last 4 KB flash sector at offset `0x3FF000`. The configuration block includes a magic number, a version field, and a CRC32 checksum. On boot, the CRC is verified; if it fails, defaults are loaded. The version field enables forward migration -- when the firmware expects a newer config version, it reads the raw flash, verifies the old CRC at the old offset, copies the data, zero-fills new fields, and saves. The current config version is 5.

### mDNS

The device registers itself as `sprinkler.local` via mDNS, allowing access without knowing the IP address on networks that support multicast DNS.

### DS3231 Battery Monitor

The DS3231's oscillator-stop flag (OSF) is checked on every RTC read. If the flag is set, it indicates the backup battery has failed and the RTC time may be invalid. This status is exposed on the dashboard as a battery health indicator.

### Boot Statistics

Persistent counters in flash track total boot count and watchdog reset count. These are displayed on the settings page and help diagnose intermittent stability issues in the field.

---

## Pin Map

| Function | GPIO | Notes |
|----------|------|-------|
| Zone 1 | 10 | |
| Zone 2 | 11 | |
| Zone 3 | 12 | |
| Zone 4 | 13 | |
| Zone 5 | 18 | |
| Zone 6 | 19 | |
| Zone 7 | 20 | |
| Zone 8 | 21 | |
| DHT11 data | 15 | Temperature/humidity sensor |
| LCD SDA | 0 | I2C0, 50 kHz |
| LCD SCL | 1 | I2C0, 50 kHz |
| DS3231 SDA | 6 | I2C1, 100 kHz |
| DS3231 SCL | 7 | I2C1, 100 kHz |

---

## Building

```
mkdir -p build && cd build && cmake .. && make
```

This produces `SprinklerMaster.uf2` in the build directory.

### Flashing

```
picotool load ./build/SprinklerMaster.uf2 -f
```

Or hold the BOOTSEL button while plugging in USB and copy the `.uf2` file to the mounted drive.

---

## Web UI

The web interface is served directly from the device -- there is no external server or cloud dependency. All HTML, CSS, and JavaScript are compiled into the firmware binary via lwIP's makefsdata. The pages are listed under [Web Interface](#web-interface) above.
