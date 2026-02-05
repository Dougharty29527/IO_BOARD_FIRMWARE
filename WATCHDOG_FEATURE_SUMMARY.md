# Serial Watchdog Feature Implementation Summary

## Overview
Added a serial watchdog feature that monitors Serial1 communication and triggers a GPIO39 pulse when no data is received for 5 minutes. This feature can be toggled on/off via the web configuration interface, and the setting is saved to EPROM.

---

## Features Implemented

### 1. **GPIO39 Watchdog Pin**
- **Pin**: GPIO39 (defined as `ESP_WATCHDOG_PIN`)
- **Behavior**: Pulses HIGH for 1 second, then returns to LOW
- **Purpose**: Can be used to trigger external device to restore serial communications
- **No conflicts**: GPIO39 was verified to be available

### 2. **Watchdog Functions**

#### `pulseWatchdogPin()`
- Triggers a 1-second HIGH pulse on GPIO39
- Prints debug messages to Serial monitor
- Called automatically when timeout is reached

#### `checkSerialWatchdog()`
- Monitors time since last serial data reception
- Triggers pulse if 5 minutes (300,000 ms) pass without data
- Only activates when watchdog is enabled
- Prevents repeated triggers until serial data resumes

#### `resetSerialWatchdog()`
- Resets the 5-minute timer
- Clears the triggered flag
- Called automatically when serial data arrives
- Called when watchdog is toggled on

### 3. **Web Interface Configuration**

Added new "Configuration" panel to the web interface with:
- **Toggle Switch**: Checkbox to enable/disable watchdog
- **Status Display**: Shows "ENABLED" or "DISABLED" in real-time
- **Info Section**: Explains the watchdog function
- **Real-time Updates**: Changes apply immediately via AJAX

### 4. **Preferences/EPROM Storage**

- **Key**: `"watchdog"` (boolean)
- **Default**: `false` (disabled)
- **Persistence**: Setting survives ESP32 reboots
- **Load on Startup**: Automatically restored from EPROM
- **Save on Change**: Immediately saved when toggled via web

### 5. **Web Endpoint**

New HTTP GET endpoint: `/setwatchdog?enabled=[0|1]`
- `enabled=1`: Enables the watchdog
- `enabled=0`: Disables the watchdog
- Saves setting to EPROM
- Returns confirmation message

---

## Implementation Details

### Global Variables Added
```cpp
bool watchdogEnabled = false;                    // Feature enable/disable
unsigned long lastSerialDataTime = 0;            // Last data timestamp
bool watchdogTriggered = false;                  // Prevents repeated triggers
const unsigned long WATCHDOG_TIMEOUT = 300000;   // 5 minutes
```

### Pin Configuration
```cpp
#define ESP_WATCHDOG_PIN  39   // GPIO39 output pin
```

### Integration Points

1. **Serial Data Reception** (`readSerialData()`)
   - Calls `resetSerialWatchdog()` when JSON data starts arriving
   - Resets 5-minute timer on each valid data packet

2. **Main Loop** (`loop()`)
   - Calls `checkSerialWatchdog()` every 5 seconds
   - Monitors timeout and triggers pulse if needed

3. **Setup** (`setup()`)
   - Initializes GPIO39 as OUTPUT (LOW)
   - Loads watchdog setting from EPROM
   - Initializes watchdog timer

4. **Web Interface**
   - New "Configuration" panel with toggle
   - JavaScript function to handle toggle changes
   - Template variables for checkbox state

---

## Usage Instructions

### For End Users

1. **Enable Watchdog**:
   - Connect to WiFi AP "Device Config"
   - Navigate to http://192.168.4.1
   - Scroll to "Configuration" section
   - Check "Enable Serial Watchdog Function"
   - Setting is saved automatically

2. **Disable Watchdog**:
   - Uncheck the same box
   - Setting is saved automatically

3. **Monitor Status**:
   - Serial Monitor shows watchdog messages:
     - `"✓ Loaded watchdog setting from EPROM: ENABLED"`
     - `"🚨 WATCHDOG TRIGGERED: Pulsing GPIO39 HIGH for 1 second..."`
     - `"✓ Watchdog pulse complete, GPIO39 returned to LOW"`

### For Developers

**Test the watchdog**:
```cpp
// In Arduino IDE Serial Monitor:
// 1. Enable watchdog via web interface
// 2. Disconnect Serial1 data source
// 3. Wait 5 minutes
// 4. GPIO39 will pulse HIGH for 1 second
// 5. Reconnect Serial1 data source
// 6. Watchdog resets and waits for next timeout
```

**Manually trigger (for testing)**:
```cpp
// Add this temporarily to loop() for testing:
if (Serial.available()) {
    char c = Serial.read();
    if (c == 'T') {  // Press 'T' to test
        pulseWatchdogPin();
    }
}
```

---

## Technical Notes

### Timeout Behavior
- **First timeout**: Triggers pulse after 5 minutes of no data
- **Subsequent behavior**: Does NOT repeatedly pulse
- **Reset condition**: Pulse can trigger again only after serial data resumes
- **Rationale**: Prevents continuous pulsing if problem persists

### Optional Auto-Restart
There's a commented-out section in `checkSerialWatchdog()` that can restart the ESP32:
```cpp
// Uncomment these lines if you want automatic restart:
// Serial.println("Restarting ESP32 in 10 seconds...");
// delay(10000);
// ESP.restart();
```

### Thread Safety
- Watchdog runs on Core 1 (main loop)
- MCP emulator runs on Core 0
- No mutex needed (watchdog doesn't interact with relays)

---

## Code Changes Summary

### Files Modified
- `IO_BOARD_FIRMWARE5.ino` - All changes in single file

### Lines Added
- Pin definition: Line ~65
- Global variables: Lines ~95-99
- Watchdog functions: Lines ~204-275 (3 functions with comments)
- Serial data integration: Line ~1119 (resetSerialWatchdog call)
- Web HTML: Lines ~650-680 (Configuration panel)
- Web JavaScript: Lines ~694-703 (toggleWatchdog function)
- Web processor: Lines ~717-718 (template variables)
- Web endpoint: Lines ~959-982 (setwatchdog handler)
- Preferences loading: Lines ~1706-1709
- GPIO initialization: Lines ~1689-1692
- Timer initialization: Lines ~1804-1805
- Main loop check: Line ~1824

### Total Lines Added: ~120 lines (including extensive comments)

---

## Testing Checklist

- [x] GPIO39 defined and initialized
- [x] Watchdog functions implemented with error handling
- [x] Web interface toggle added
- [x] Web endpoint handles enable/disable
- [x] Preferences save on toggle
- [x] Preferences load on startup
- [x] Serial data reception resets timer
- [x] Watchdog checked in main loop
- [x] Comments and usage examples added
- [x] No conflicts with existing GPIO pins
- [x] Thread-safe (runs on Core 1 only)

---

## Version Information
- **Firmware Version**: Rev 9 - Walter IO Board Firmware
- **Feature**: Serial Watchdog + AJAX Web Interface
- **Date**: January 29, 2026
- **Tested**: Code review complete, ready for Arduino IDE compilation

---

## Support Notes

If the watchdog isn't working:
1. Check Serial Monitor for initialization message
2. Verify GPIO39 is connected properly
3. Confirm watchdog is enabled in web interface
4. Check that Serial1 data has actually stopped
5. Wait full 5 minutes for first trigger
6. Monitor for "WATCHDOG TRIGGERED" message

For questions or issues, refer to the extensive inline comments in the code.

