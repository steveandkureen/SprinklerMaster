# SprinklerMaster Project Plan

## Project Overview
SprinklerMaster is an automated irrigation system controller built on the Raspberry Pi Pico W. It manages up to 8 irrigation zones with web-based scheduling, real-time monitoring, and environmental sensor integration.

---

## Hardware Requirements

### Microcontroller
- **Raspberry Pi Pico W** (with wireless capability)

### Software Requirements
- **FreeRTOS Kernel** (located at ../../FreeRTOS-Kernel)
  - Real-time operating system for task management
  - Provides multitasking capabilities
  - Handles timing and scheduling

### Components
- **8-Channel Relay Module** (for zone control)
- **LCD Display** (I2C interface)
- **Temperature/Humidity Sensor** (DHT22 or similar)

---

## Pin Assignments

| GPIO Pin | Component | Purpose |
|----------|-----------|---------|
| GP0 | LCD | I2C SDA |
| GP1 | LCD | I2C SCL |
| GP10 | Relay 1 | Zone 1 Control |
| GP11 | Relay 2 | Zone 2 Control |
| GP12 | Relay 3 | Zone 3 Control |
| GP13 | Relay 4 | Zone 4 Control |
| GP15 | Temp/Humidity | DHT22 Sensor Data |
| GP18 | Relay 5 | Zone 5 Control |
| GP19 | Relay 6 | Zone 6 Control |
| GP20 | Relay 7 | Zone 7 Control |
| GP21 | Relay 8 | Zone 8 Control |

---

## Core Features

### 1. Zone Management
- Support for up to 8 irrigation zones
- Configurable pin-to-zone mapping
- One relay per zone
- **Safety Constraint:** Only one zone can be active at a time
- Conflict detection and prevention between overlapping schedules

### 2. Scheduling System

#### Permanent Schedules
- Recurring schedules with specific days of week
- Example: "Run Zone 1 on Monday, Wednesday, Friday at 5:00 AM for 30 minutes"
- Multiple schedules can be created per zone

#### Interval Schedules
- Run every N days
- Example: "Run Zone 3 every 2 days at 6:30 AM for 10 minutes"

#### Manual/Temporary Run
- Immediate one-time execution
- Example: "Run Zone 2 for 5 minutes right now"
- Bypasses scheduling system but respects single-zone-active constraint

### 3. Web Interface
The Pico W hosts a web server that serves embedded web assets from firmware.

#### Features
- Create, edit, and delete schedules
- Configure zone/pin assignments and zone names
- Manual zone control (start/stop)
- View current system status
- Display active zone and remaining runtime
- Show upcoming scheduled runs
- Display current temperature and humidity readings
- View schedule conflict warnings

#### Storage
- HTML, CSS, and JavaScript files are embedded in firmware at compile time
- Served directly from flash memory
- No external storage required for web assets

### 4. LCD Display

#### Startup Mode (first 3 minutes OR until web connection)
- Display WiFi IP address
- Display connection status
- Purpose: Allow user to easily access web interface on first boot

#### Normal Operation Mode
- Current zone status (which zone is running, time remaining)
- Temperature and humidity readings
- Next scheduled run (zone, time, duration)
- Rotating display if more info needs to be shown

### 5. Environmental Monitoring
- Read temperature and humidity from DHT22 sensor
- Display on LCD and web interface
- Data can be used for future smart watering decisions

---

## System Constraints and Safety

### Single Zone Operation
- Only one zone can be active at any given time
- Prevents water pressure issues
- Reduces power consumption
- System must queue or reject conflicting requests

### Conflict Detection
- Check for schedule overlaps when creating/editing schedules
- Warn user of conflicts in web interface
- Resolve conflicts by priority or timestamp

### Data Persistence
- **Flash Storage:**
  - Configuration stored in dedicated flash sector (last 4KB at `0x3FF000`)
  - Schedules, zone configurations, and WiFi credentials persist through power outages
  - CRC32 validation ensures data integrity
  - Uses `flash_safe_execute()` for FreeRTOS-safe flash operations
- **Pico W Flash:** Stores both program code and configuration data

---

## Technical Implementation

### FreeRTOS Task Architecture

The system will be organized into multiple FreeRTOS tasks for concurrent operation:

#### Core Tasks
1. **Scheduler Task** (High Priority)
   - Monitors time and triggers scheduled zone activations
   - Checks schedules every minute
   - Enforces single-zone constraint
   - Manages zone run queue

2. **Web Server Task** (Medium Priority)
   - Handles HTTP requests
   - Serves embedded web assets from flash
   - Processes REST API calls
   - Manages client connections

3. **LCD Display Task** (Low Priority)
   - Updates LCD with current status
   - Handles startup mode (3 minutes or until web connection)
   - Rotates display information in normal mode
   - Updates every 1-2 seconds

4. **Sensor Reading Task** (Low Priority)
   - Reads temperature/humidity from DHT22
   - Updates every 30-60 seconds
   - Provides data to other tasks via shared variables

5. **WiFi Management Task** (Medium Priority)
   - Maintains WiFi connection
   - Handles NTP time synchronization
   - Monitors connection health
   - Attempts reconnection on failure

6. **Zone Control Task** (High Priority)
   - Executes zone on/off commands
   - Monitors active zone runtime
   - Automatically stops zones when time expires
   - Enforces single-zone safety constraint

#### Task Synchronization
- **Mutexes:** Protect shared resources (relay states, schedules, configuration)
- **Queues:** Inter-task communication for zone control commands
- **Semaphores:** Coordinate access to I2C and flash operations
- **Event Groups:** Signal system state changes (WiFi connected, zone active, etc.)

### Software Components

#### 1. Relay Control Module
- Initialize GPIO pins for relay control
- Provide functions to turn zones on/off
- Enforce single-zone-active constraint
- Track active zone and runtime

#### 2. LCD Driver
- I2C communication with LCD
- Display formatting functions
- Startup mode vs normal mode handling

#### 3. Sensor Interface
- DHT22 communication protocol
- Periodic reading of temperature/humidity
- Data caching for display and web interface

#### 4. Flash Storage Module
- Configuration stored in last 4KB flash sector (`0x3FF000`)
- Safe from firmware growth
- FreeRTOS-safe operations using `flash_safe_execute()`
- CRC32 validation on read/write
- Automatic loading of defaults if flash is empty or corrupted

#### 5. Web Server
- HTTP server running on Pico W
- Serve embedded static files from flash
- REST API endpoints for:
  - Get/set schedules
  - Get/set zone configurations
  - Manual zone control
  - Get current status (active zone, sensor data)
  - Get upcoming schedules

#### 6. Scheduler
- Time-based execution engine
- Check schedule triggers each minute
- Execute zone runs based on schedule
- Handle temporary/manual runs
- Queue management for conflicting schedules

#### 7. WiFi Management
- Connect to configured WiFi network
- Obtain IP address via DHCP
- NTP time synchronization for accurate scheduling
- Handle reconnection on network loss

#### 8. Configuration Manager
- Load/save WiFi credentials from flash
- Load/save zone configurations from flash
- Load/save schedules from flash
- Binary format with CRC32 integrity check

---

## Data Structures

### Flash Configuration Structure (~800 bytes)
```c
typedef struct {
    uint16_t magic;              // 0xCAFE for validity
    uint16_t version;            // Config format version

    // WiFi (98 bytes)
    char ssid[33];
    char password[65];

    // Zones (8 * 36 = 288 bytes)
    struct {
        char name[32];
        uint8_t gpio_pin;
        uint8_t enabled;
        uint8_t reserved[2];
    } zones[8];

    // Schedules (20 * 8 = 160 bytes)
    struct {
        uint8_t zone_id;
        uint8_t type;             // 0=permanent, 1=interval, 2=manual
        uint8_t day_mask;         // Bits: Sun=0, Mon=1, etc.
        uint8_t hour;
        uint8_t minute;
        uint8_t enabled;
        uint16_t duration_mins;
    } schedules[20];

    uint32_t crc32;              // Data integrity check
} sprinkler_config_t;
```

### Zone Configuration
```
Zone {
  id: 1-8
  name: string (e.g., "Front Lawn", "Garden Bed 1")
  pin: GPIO pin number
  enabled: boolean
}
```

### Schedule
```
Schedule {
  id: unique identifier
  zone_id: 1-8
  type: "permanent" | "interval" | "manual"

  // For permanent schedules
  days_of_week: [0-6] (Sunday=0)
  time: HH:MM

  // For interval schedules
  interval_days: integer
  start_date: date
  time: HH:MM

  // Common fields
  duration_minutes: integer
  enabled: boolean
  last_run: timestamp
}
```

### System Status
```
Status {
  active_zone: zone_id or null
  zone_start_time: timestamp
  zone_end_time: timestamp
  temperature: float
  humidity: float
  next_scheduled_run: Schedule
  wifi_connected: boolean
  ip_address: string
}
```

---

## Flash Storage Layout

### Configuration Sector
- **Location:** Last 4KB sector at `0x3FF000`
- **Size:** 4096 bytes (one flash sector)
- **Format:** Binary structure with CRC32 validation

### Key Functions
```c
void config_init(void);                    // Load from flash or defaults
bool config_save(void);                    // Write to flash
sprinkler_config_t* config_get(void);      // Get current config
void config_set_wifi(const char* ssid, const char* password);
void config_set_zone(uint8_t id, const char* name, uint8_t pin, bool enabled);
void config_set_schedule(uint8_t id, schedule_t* sched);
```

### Web Assets
- HTML, CSS, and JavaScript files are embedded in firmware
- Converted to C arrays at compile time using `makefsdata` or similar tool
- Served directly from program flash memory

---

## Future Enhancements (Not in Initial Scope)

- Weather data integration for smart watering
- Rain delay functionality (skip watering if recent rain)
- Master enable/disable switch for entire system
- Historical watering logs
- Water usage statistics
- Mobile app integration
- Email/push notifications
- Soil moisture sensor integration
- Flow meter for leak detection

---

## Development Phases

### Phase 1: Hardware Setup and Basic Control
- Integrate FreeRTOS kernel into project build system
- Create basic FreeRTOS configuration (FreeRTOSConfig.h)
- Test FreeRTOS task creation and scheduling
- Configure GPIO pins
- Test relay control
- Test LCD display
- Test temperature/humidity sensor

### Phase 2: Flash Configuration Storage
- Implement flash storage module (`config_flash.c/h`)
- Define configuration data structure
- Implement CRC32 validation
- Implement `flash_safe_execute()` for FreeRTOS safety
- Test save/load across power cycles
- Implement default configuration loading

### Phase 3: Core Scheduling System
- Create FreeRTOS tasks for scheduler and zone control
- Implement task synchronization (mutexes, queues, semaphores)
- Implement time synchronization (NTP)
- Build scheduler engine
- Implement single-zone constraint
- Test basic scheduling
- Integrate flash configuration for schedule persistence

### Phase 4: Web Server and Interface
- Create FreeRTOS tasks for web server, LCD display, and sensors
- Set up HTTP server on Pico W (running in dedicated task)
- Embed HTML/CSS/JS templates in firmware
- Implement REST API endpoints
- Build web UI for schedule management
- Implement WiFi management task

### Phase 5: Integration and Testing
- Integrate all FreeRTOS tasks
- Test task synchronization and communication
- Test complete workflows
- Test conflict detection
- Test manual overrides
- Test persistence across reboots and power outages
- Verify task priorities and timing constraints

### Phase 6: Polish and Optimization
- Optimize FreeRTOS heap and stack sizes for each task
- Fine-tune task priorities for optimal performance
- Optimize memory usage
- Improve web UI responsiveness
- Add error handling and logging
- Create user documentation

---

## Questions to Address

1. **FreeRTOS Configuration:** What heap size and task stack sizes should be allocated? What tick rate?
2. **Task Priorities:** Should priorities be configurable or fixed? How to handle priority inversion?
3. **Time Synchronization:** How should the system handle NTP failures? Use RTC backup?
4. **Schedule Priority:** If schedules conflict, which should take precedence?
5. **WiFi Setup:** How should initial WiFi credentials be configured? Access point mode?
6. **Backup Power:** Should there be battery backup for graceful shutdown?
7. **Error Handling:** What should happen if a relay fails or sensor becomes unavailable?
8. **Security:** Should the web interface have authentication?
9. **Flash Wear:** How to minimize flash write cycles? Consider wear leveling for frequently updated data?

---

## Success Criteria

- FreeRTOS tasks run concurrently without deadlocks or resource conflicts
- All tasks meet their timing requirements
- System can control 8 zones independently
- Only one zone active at any time
- Schedules execute accurately (within 1 minute of scheduled time)
- Web interface accessible on local network and responsive during zone operations
- LCD displays accurate status and IP address on startup
- Temperature/humidity readings update regularly
- Schedules persist across power cycles and power outages
- No schedule conflicts go undetected
- System runs reliably 24/7 without task starvation or crashes
- Flash configuration survives power loss with CRC validation

---

**End of Plan**
