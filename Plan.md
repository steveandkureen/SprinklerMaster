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
- **SD Card Module** (SPI interface for web asset storage and data persistence)

---

## Pin Assignments

| GPIO Pin | Component | Purpose |
|----------|-----------|---------|
| GP0 | LCD | I2C SDA |
| GP1 | LCD | I2C SCL |
| GP2 | SD Card | SPI SCK (Clock) |
| GP3 | SD Card | SPI MOSI (Master Out Slave In) |
| GP4 | SD Card | SPI MISO (Master In Slave Out) |
| GP5 | SD Card | SPI CS (Chip Select) |
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
The Pico W hosts a web server that serves pages from the SD card.

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
- HTML templates stored on SD card
- CSS files stored on SD card
- JavaScript files stored on SD card
- Images and icons stored on SD card

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
- **SD Card Storage (FAT32):**
  - Schedules saved to SD card
  - Zone configurations saved to SD card
  - WiFi credentials saved to SD card
  - All data persists through power outages
  - SD card can be removed and edited on any computer
  - Easy backup by copying SD card contents
- **Pico W Flash:** Only stores the main program code

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
   - Serves files from SD card
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
- **Semaphores:** Coordinate access to I2C, SPI, and SD card
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

#### 4. SD Card Interface
- SPI communication with SD card
- File system access (FAT32)
- Read HTML/CSS/JS/image files for web server
- Read/write schedule and configuration files
- JSON or CSV format for configuration data

#### 5. Web Server
- HTTP server running on Pico W
- Serve static files from SD card
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
- Load/save WiFi credentials from SD card
- Load/save zone configurations from SD card
- Load/save schedules from SD card
- Persistent storage in JSON format on SD card

---

## Data Structures

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

## SD Card File Structure

```
/sdcard/
  /web/
    /html/
      index.html
      schedules.html
      zones.html
      status.html
    /css/
      style.css
    /js/
      app.js
      api.js
    /images/
      logo.png
      icons/
  /config/
    wifi.json
    zones.json
    schedules.json
  /logs/
    watering_log.txt
```

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
- Test SD card read/write (FAT32)

### Phase 2: Core Scheduling System
- Create FreeRTOS tasks for scheduler and zone control
- Implement task synchronization (mutexes, queues, semaphores)
- Implement time synchronization (NTP)
- Build scheduler engine
- Implement single-zone constraint
- Test basic scheduling
- Implement SD card configuration save/load

### Phase 3: Web Server and Interface
- Create FreeRTOS tasks for web server, LCD display, and sensors
- Set up HTTP server on Pico W (running in dedicated task)
- Create HTML/CSS/JS templates
- Store templates on SD card
- Implement REST API endpoints
- Build web UI for schedule management
- Implement WiFi management task

### Phase 4: Integration and Testing
- Integrate all FreeRTOS tasks
- Test task synchronization and communication
- Test complete workflows
- Test conflict detection
- Test manual overrides
- Test persistence across reboots and power outages
- Verify task priorities and timing constraints

### Phase 5: Polish and Optimization
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
9. **SD Card Failure:** What should happen if SD card is removed or fails during operation?

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
- SD card can be removed, backed up, and edited on a computer

---

**End of Plan**
