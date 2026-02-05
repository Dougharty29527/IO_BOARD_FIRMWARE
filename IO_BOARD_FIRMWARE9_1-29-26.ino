/* ********************************************
 *  
 *  Walter IO Board Firmware - Rev 9.4
 *  Date: 2/5/2026
 *  Written By: Todd Adams & Doug Harty
 *  
 *  Based on:
 *  - RMS_CBOR_1_16 (Walter RMS w/OTA and ZTA)
 *  - MCP_Simple (MCP23017 Emulator for I2C relay control)
 *  - walter_i2c_slave (Improved web interface and SD config)
 *  
 *  =====================================================================
 *  REVISION HISTORY
 *  =====================================================================
 *  
 *  Rev 9.4 (2/5/2026) - Simplified Diagnostic Dashboard
 *  - REMOVED: Web relay control (mode buttons, individual relay buttons)
 *  - REMOVED: Quality Check Test Mode (password section, ADS1015 ADC)
 *  - REMOVED: Web passthrough toggle (passthrough still works via BlueCherry/serial)
 *  - REMOVED: Web I2C reset endpoint
 *  - NEW: Read-only Service Diagnostic Dashboard for field contractors
 *  - Dashboard shows: Serial data, Cellular modem status, IO board health
 *  - Displays: RSRP/RSRQ signal quality with visual bar, LTE/BlueCherry status
 *  - Displays: SD card status, I2C transaction counts, overfill sensor, uptime
 *  - Relay control now exclusively via I2C from Linux master (no web conflicts)
 *  - Kept: Device name configuration, watchdog toggle
 *  
 *  Rev 9.3a (2/5/2026) - Critical Safety Fixes
 *  - FIXED: DISP_SHUTDN relay randomly turning off
 *    * gpioA_value now initialized to 0x10 (V6 ON) to match physical state
 *    * I2C handler forces bit 4 ON during 20-second protection period
 *    * set_relay_mode() now preserves V6 state instead of overwriting
 *    * Web /setrelays endpoint always preserves V6 state (can only change via I2C)
 *  - FIXED: False overfill alarms on ESP32 reboot
 *    * Multi-layer protection: variables reset in setup(), task, and passthrough boot
 *    * I2C handler returns 0x00 for GPIOB during 15-second startup lockout
 *    * GPIO38 pull-up configured before I2C slave initialization
 *  - FIXED: Serial output corruption from MCP emulator thread
 *    * Removed all Serial.print calls from Core 0 functions
 *    * Affects: I2C handlers, bus recovery, health check, overfill/DISP protection
 *  
 *  Rev 9.3 (2/5/2026) - Preference-Based Passthrough Boot Mode
 *  - Passthrough now uses preference-based boot strategy for clean modem access
 *  - When "remote XX" command received, stores flag in preferences and restarts
 *  - On boot, checks preference BEFORE loading WalterModem library
 *  - If passthrough requested: runs minimal mode (MCP emulator + serial bridge only)
 *  - No WiFi, web server, or WalterModem during passthrough (matches walter-as-linux-modem.txt)
 *  - Early exit: Linux device can set MCP GPA5 (bit 5) via I2C to trigger restart
 *  - Auto-restarts to normal operation after timeout (default 60 min, max 24 hours)
 *  - Preference is cleared immediately on boot to prevent getting stuck in passthrough
 *  
 *  Rev 9.2f (2/5/2026) - On-Demand Passthrough - SUPERSEDED by 9.3
 *  - Attempted runtime passthrough by reinitializing ModemSerial
 *  - Replaced by preference-based boot for cleaner modem access
 *  
 *  Rev 9.2e (2/4/2026) - Passthrough-Only Boot Mode - SUPERSEDED
 *  - Used preferences flag - replaced by on-demand approach in 9.2f
 *  
 *  Rev 9.2c (2/4/2026) - BlueCherry Remote Commands
 *  - BlueCherry message containing "remote" (case-insensitive) enters passthrough mode
 *  - BlueCherry message containing "restart" (case-insensitive) performs ESP.restart()
 *  - Moved web config HTML back to .ino file (fixes iOS captive portal blank page)
 *  - Compressed HTML/CSS/JS for smaller size
 *  
 *  Rev 9.2b (2/4/2026) - Passthrough Mode Simplification
 *  - WalterModem library configures modem at 115200 (same as host serial)
 *  - Passthrough is now a simple bridge with no baud rate changes
 *  - Simplified enterPassthroughMode() and exitPassthroughMode()
 *  
 *  Rev 9.2a (2/4/2026) - Critical Relay Protection
 *  - Added DISP_SHUTDN startup protection (20 second lockout)
 *  - Relay forced ON during power cycles/firmware flashes
 *  - Prevents accidental site shutdown during startup
 *  - Enhanced overfill validation (15 sec lockout, 8 readings to trigger)
 *  - Both systems log status to Serial Monitor for diagnostics
 *  
 *  Rev 9.2 (2/4/2026) - BlueCherry Message Receiving
 *  - Added support for receiving non-firmware MQTT messages via BlueCherry
 *  - Messages with topic != 0 are printed to Serial Monitor
 *  - Uses blueCherrySync() built-in mechanism (no separate MQTT connection)
 *  - Topic 0 = firmware update, Topic != 0 = custom messages
 *  
 *  Rev 9.1g (1/29/2026) - Passthrough UI Fix for iOS Safari
 *  - Replaced native confirm() dialog with custom HTML modal
 *  - Custom modal works consistently on iOS Safari, Mac Safari, Chrome, etc.
 *  - Added passthroughPending flag to prevent AJAX refresh race condition
 *  - Improved button event handling (preventDefault, stopPropagation)
 *  
 *  Rev 9.1f (1/30/2026) - Dynamic WiFi AP Naming, Serial Status & Passthrough Mode
 *  - WiFi AP name includes random hex suffix on first boot (e.g., "GM IO Board 5c3dfc")
 *  - AP name auto-updates when device ID received from Linux serial
 *  - Manual device name entry via web portal configuration
 *  - Device name persists across reboots in EPROM
 *  - Sends JSON status to Python device every 15 seconds via RS-232 (Serial1)
 *  - JSON format: {"datetime":"YYYY-MM-DD HH:MM:SS","sdcard":"OK|FAULT","passthrough":0|1}
 *  - Passthrough Mode - bridges RS-232 to modem for AT commands/PPP connections
 *  - Web button to enter/exit passthrough mode (NOT persisted - reboot returns to normal)
 *  - I2C emulator and web server continue running in passthrough mode
 *  
 *  Rev 9.1e (1/30/2026) - MCP23017 Compatibility & Debug Improvements
 *  - Added INTCAP register support for Adafruit library compatibility
 *  - Added periodic I2C status logging (every 10 seconds when debug enabled)
 *  - Fixed serial output corruption from multi-core access
 *  
 *  Rev 9.1d (1/30/2026) - BlueCherry Stability Fix
 *  - CRITICAL: Removed modem.reset() on BlueCherry sync failure
 *  - This was causing LTE disconnect -> ESP.restart() loop
 *  - Added fault code 4096 when BlueCherry is offline
 *  - Fault codes: 1024=watchdog, 4096=BlueCherry, 5120=both
 *  - System now stays running during BlueCherry platform outages
 *  - Weekly restart still occurs if BlueCherry never connects
 *  
 *  Rev 9.1c (1/30/2026) - I2C Timeout Fix
 *  - CRITICAL: Reduced mutex timeouts in I2C callbacks from 100ms to 1ms
 *  - I2C slave callbacks must respond in microseconds, not milliseconds
 *  - Prevents OSError [Errno 5] "Input/output error" on Linux master
 *  - Added fallback behavior when mutex unavailable (proceed anyway)
 *  
 *  Rev 9.1b (1/29/2026) - Thread Safety Fix
 *  - Fixed race condition: gpioA_value writes now mutex-protected
 *  - Fixed overfill false alarms: Added INPUT_PULLUP and hysteresis
 *  - Overfill alarm requires 3 consecutive HIGH readings to clear
 *  
 *  Rev 9.1 (1/29/2026) - MCP23017 Enhancements
 *  - Full MCP23017 register support (all 22 registers)
 *  - I2C transaction counting and error tracking
 *  - Non-fatal BlueCherry init (hourly retry, weekly restart)
 *  
 *  Rev 9.0 (1/29/2026) - Initial Release
 *  - AJAX-based web interface (no page refresh flashing)
 *  - Serial watchdog with configurable GPIO pulse
 *  - Thread-safe relay control with mutex protection
 *  - MCP23017 emulation for I2C relay control
 *  
 *  =====================================================================
 *  FEATURES
 *  =====================================================================
 *  - MCP23017 I2C emulation at address 0x20 (BANK=0 mode)
 *  - 5 relay outputs (Motor, CR1, CR2, CR5, V6) on Port A
 *  - Overfill sensor input on Port B (GPIO38, active-low with pull-up)
 *  - Web interface with AJAX updates (no flashing)
 *  - Serial watchdog: 30-min timeout, GPIO39 pulse, fault code 1024
 *  - BlueCherry OTA support with graceful fallback, fault code 4096
 *  - Thread-safe operation across Core 0 (I2C) and Core 1 (Main/Web)
 *  
 *  =====================================================================
 *  FAULT CODES
 *  =====================================================================
 *  - 1024: Serial watchdog triggered (no serial data for 30+ minutes)
 *  - 4096: BlueCherry platform offline (OTA updates unavailable)
 *  - 5120: Both watchdog AND BlueCherry faults active
 *  - Note: These are ADDED to any existing fault codes from the device
 *  
 ***********************************************/

// Define the software version as a macro
#define VERSION "Rev 9.4"
String ver = VERSION;

// ### Libraries ###
#include <esp_mac.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncDNSServer.h>
#include <HardwareSerial.h>
#include <Preferences.h>
#include "WalterModem.h"
#include "BlueCherryZTP.h"
#include "FS.h"
#include <SD.h>
#include <SPI.h>
#include <tinycbor.h>
#include <Wire.h>
// Adafruit_ADS1X15 removed in Rev 9.4 - QC mode removed for simplification
// Web interface HTML is embedded directly below (after web server setup section)

// =====================================================================
// MCP23017 EMULATOR CONFIGURATION
// =====================================================================
#define I2C_ADDRESS 0x20        // MCP23017 default address for Linux master
#define SDA_PIN 4               // ESP32 I2C data pin
#define SCL_PIN 5               // ESP32 I2C clock pin

// MCP23017 Register addresses (BANK=0 mode, which is default)
// See Microchip datasheet DS20001952C for full register map
#define IODIRA   0x00   // I/O Direction Register A (1=input, 0=output)
#define IODIRB   0x01   // I/O Direction Register B
#define IPOLA    0x02   // Input Polarity Register A (1=inverted)
#define IPOLB    0x03   // Input Polarity Register B
#define GPINTENA 0x04   // Interrupt-on-change enable A
#define GPINTENB 0x05   // Interrupt-on-change enable B
#define DEFVALA  0x06   // Default compare value A
#define DEFVALB  0x07   // Default compare value B
#define INTCONA  0x08   // Interrupt control A
#define INTCONB  0x09   // Interrupt control B
#define IOCON    0x0A   // Configuration register (also at 0x0B)
#define IOCON_B  0x0B   // Configuration register mirror
#define GPPUA    0x0C   // Pull-up resistor A
#define GPPUB    0x0D   // Pull-up resistor B
#define INTFA    0x0E   // Interrupt flag A
#define INTFB    0x0F   // Interrupt flag B
#define INTCAPA  0x10   // Interrupt capture A
#define INTCAPB  0x11   // Interrupt capture B
#define GPIOA    0x12   // Port A GPIO
#define GPIOB    0x13   // Port B GPIO
#define OLATA    0x14   // Output Latch A
#define OLATB    0x15   // Output Latch B

// ESP32 GPIO pins for relay outputs (MCP Port A)
#define CR0_MOTOR 11   // MCP GPA0 - Motor (Vac Pump)
#define CR1       18   // MCP GPA1
#define CR2       17   // MCP GPA2
#define CR5       16   // MCP GPA3
#define DISP_SHUTDN  13   // MCP GPA4

// ESP32 GPIO pin for overfill input (MCP Port B)
#define ESP_OVERFILL  38   // MCP GPB0

// ESP32 GPIO pin for serial watchdog pulse output
#define ESP_WATCHDOG_PIN  39   // Pulses HIGH for 1 second when no serial data for 5 minutes

// Run modes for relay patterns
enum RunMode {
    IDLE_MODE = 0,
    RUN_MODE = 1,
    PURGE_MODE = 2,
    BURP_MODE = 3
};

String mode_names[] = {"Idle", "Run", "Purge", "Burp"};

// MCP emulator global variables (protected by mutex)
// Note: Real MCP23017 defaults IODIR to 0xFF (all inputs), but we override for our application
// CRITICAL: gpioA_value bit 4 (0x10) = DISP_SHUTDN - MUST start ON to match physical state
volatile uint8_t gpioA_value = 0x10;  // V6/DISP_SHUTDN starts ON
volatile uint8_t gpioB_value = 0x00;
volatile uint8_t iodirA_value = 0x00;  // 0x00 = All OUTPUT (GPA0-GPA4 are relay outputs)
volatile uint8_t iodirB_value = 0x01;  // 0x01 = GPB0 is INPUT (overfill sensor)
volatile uint8_t ipolA_value = 0x00;   // No polarity inversion
volatile uint8_t ipolB_value = 0x00;   // No polarity inversion
volatile uint8_t gpintenA_value = 0x00; // Interrupts disabled
volatile uint8_t gpintenB_value = 0x00; // Interrupts disabled
volatile uint8_t defvalA_value = 0x00;
volatile uint8_t defvalB_value = 0x00;
volatile uint8_t intconA_value = 0x00;
volatile uint8_t intconB_value = 0x00;
volatile uint8_t iocon_value = 0x00;   // BANK=0, SEQOP=0 (default)
volatile uint8_t gppuA_value = 0x00;   // No pull-ups
volatile uint8_t gppuB_value = 0x00;   // No pull-ups
volatile uint8_t intfA_value = 0x00;   // No interrupt flags
volatile uint8_t intfB_value = 0x00;
volatile uint8_t intcapA_value = 0x00; // Interrupt capture
volatile uint8_t intcapB_value = 0x00;
volatile uint8_t olatA_value = 0x00;   // Output latch
volatile uint8_t olatB_value = 0x00;
volatile uint8_t registerPointer = 0x00;
unsigned long overfillCheckTimer = 0;
uint8_t overfillLowCount = 0;
bool overfillAlarmActive = false;

// Overfill validation system - prevents false alarms during power cycles/flashes/reboots
// The GPIO38 input can have transient states during startup before pull-up stabilizes
// PROTECTION LAYERS:
//   1. gpioB_value initialized to 0x00 (no alarm) and explicitly reset on task start
//   2. overfillValidationComplete=false until 15 seconds after boot
//   3. I2C handler returns 0x00 for GPIOB if !overfillValidationComplete
//   4. Require 8 consecutive LOW readings (8 seconds) to trigger alarm
//   5. Require 5 consecutive HIGH readings (5 seconds) to clear alarm (hysteresis)
bool overfillValidationComplete = false;    // True after startup validation period
unsigned long overfillStartupTime = 0;      // When overfill monitoring started
const unsigned long OVERFILL_STARTUP_LOCKOUT = 15000;  // 15 second lockout after boot
const uint8_t OVERFILL_TRIGGER_COUNT = 8;   // Consecutive LOW readings to trigger
const uint8_t OVERFILL_CLEAR_COUNT = 5;     // Consecutive HIGH readings to clear

// DISP_SHUTDN (V6) Protection System - CRITICAL SAFETY RELAY
// This relay controls site shutdown - MUST default to ON (HIGH)
// Only allow MCP commands to turn it OFF after startup lockout period
// Prevents accidental shutdown during power cycles and firmware flashes
bool dispShutdownProtectionActive = true;   // True = force ON, ignore MCP commands
unsigned long dispShutdownStartupTime = 0;  // When protection started
const unsigned long DISP_SHUTDN_LOCKOUT = 20000;  // 20 second lockout (longer than overfill)

// I2C transaction tracking for debugging noisy bus
volatile uint32_t i2cTransactionCount = 0;
volatile uint32_t i2cErrorCount = 0;
volatile uint32_t i2cLastGoodTransaction = 0;
volatile uint8_t lastRegisterAccessed = 0xFF;
volatile bool i2cDebugEnabled = false;  // Set true to enable verbose I2C logging

// Thread safety
SemaphoreHandle_t relayMutex = NULL;
// Web override removed in Rev 9.4 - web interface is now read-only
// Relay control is exclusively managed by I2C from the Linux master
bool webOverrideActive = false;       // Kept for compatibility, always false
unsigned long lastWebActivity = 0;
const unsigned long WEB_OVERRIDE_TIMEOUT = 5000;

// Current mode tracking
RunMode current_mode = IDLE_MODE;
unsigned long mode_start_time = 0;

// Serial watchdog variables
bool watchdogEnabled = false;                    // Watchdog feature enable/disable flag
unsigned long lastSerialDataTime = 0;            // Timestamp of last received serial data
bool watchdogTriggered = false;                  // Flag to track if we're in watchdog state (no serial)
int watchdogRebootAttempts = 0;                  // Counter for reboot attempts (0 = no attempts yet)
unsigned long lastWatchdogPulseTime = 0;         // Timestamp of last watchdog pulse
const unsigned long WATCHDOG_TIMEOUT = 1800000;  // 30 minutes in milliseconds (30 * 60 * 1000)
const unsigned long WATCHDOG_FIRST_PULSE = 1000; // First reboot attempt: 1 second pulse
const unsigned long WATCHDOG_LONG_PULSE = 30000; // Subsequent attempts: 30 second pulse
const int WATCHDOG_FAULT_CODE = 1024;            // Fault code to send when no serial data
const int BLUECHERRY_FAULT_CODE = 4096;          // Fault code to send when BlueCherry is disconnected

// BlueCherry connection tracking variables
// Allows system to run without BlueCherry, but schedules weekly restart for OTA retry
bool blueCherryConnected = false;                // True if BlueCherry initialized successfully
unsigned long blueCherryLastAttempt = 0;         // Timestamp of last BlueCherry connection attempt
unsigned long systemStartTime = 0;               // Timestamp when system started (for weekly restart)
const unsigned long WEEKLY_RESTART_MS = 604800000UL;  // 7 days in milliseconds (7 * 24 * 60 * 60 * 1000)
const unsigned long BC_RETRY_INTERVAL = 3600000; // Retry BlueCherry every hour (60 * 60 * 1000)

// =====================================================================
// RMS CONFIGURATION (Updated SD pins)
// =====================================================================
#define BC_DEVICE_TYPE "vaporcf1"
#define BC_TLS_PROFILE 1
#define MISO 7             // SD card MISO
#define MOSI 6             // SD card MOSI
#define SCLK 15            // SD card SCK (clock)
#define CS 40              // SD card CS (chip select)
#define HTTP_PROFILE 0
#define TLS_PROFILE 0
#define COAP_PROFILE 1
#define MAX_BUFFER_SIZE 2000
#define RX_PIN 44
#define TX_PIN 43
#define ModemSerial Serial2

// Walter Modem hardware pins (for direct passthrough without library)
#define WALTER_MODEM_PIN_RX 14
#define WALTER_MODEM_PIN_TX 48
#define WALTER_MODEM_PIN_RTS 21
#define WALTER_MODEM_PIN_CTS 47
#define WALTER_MODEM_PIN_RESET 45
#define ESP_POWER_EN  0

// Modem baud rate - WalterModem library configures modem to this speed
// Both normal operation and passthrough use the same 115200 baud rate
// No baud rate changes needed for passthrough mode

// Cellular network configuration
#define CELLULAR_APN "soracom.io"
#define CELLULAR_USERNAME "sora"
#define CELLULAR_PASSWORD "sora"
#define CELLULAR_AUTH_PROTOCOL WALTER_MODEM_PDP_AUTH_PROTO_PAP

// ### Global Objects and Variables ###
Preferences preferences;
byte otaBuffer[SPI_FLASH_BLOCK_SIZE] = { 0 };
WalterModem modem;
WalterModemRsp rsp = {};
const char *bc_ca_cert = "-----BEGIN CERTIFICATE-----\r\n\
MIIBlTCCATqgAwIBAgICEAAwCgYIKoZIzj0EAwMwGjELMAkGA1UEBhMCQkUxCzAJ\r\n\
BgNVBAMMAmNhMB4XDTI0MDMyNDEzMzM1NFoXDTQ0MDQwODEzMzM1NFowJDELMAkG\r\n\
A1UEBhMCQkUxFTATBgNVBAMMDGludGVybWVkaWF0ZTBZMBMGByqGSM49AgEGCCqG\r\n\
SM49AwEHA0IABJGFt28UrHlbPZEjzf4CbkvRaIjxDRGoeHIy5ynfbOHJ5xgBl4XX\r\n\
hp/r8zOBLqSbu6iXGwgjp+wZJe1GCDi6D1KjZjBkMB0GA1UdDgQWBBR/rtuEomoy\r\n\
49ovMAnj5Hpmk2gTGjAfBgNVHSMEGDAWgBR3Vw0Y1sUvMhkX7xySsX55tvsu8TAS\r\n\
BgNVHRMBAf8ECDAGAQH/AgEAMA4GA1UdDwEB/wQEAwIBhjAKBggqhkjOPQQDAwNJ\r\n\
ADBGAiEApN7DmuufC/aqyt6g2Y8qOWg6AXFUyTcub8/Y28XY3KgCIQCs2VUXCPwn\r\n\
k8jR22wsqNvZfbndpHthtnPqI5+yFXrY4A==\r\n\-----END CERTIFICATE-----\r\n\
-----BEGIN CERTIFICATE-----\r\n\
MIIBmDCCAT+gAwIBAgIUDjfXeosg0fphnshZoXgQez0vO5UwCgYIKoZIzj0EAwMw\r\n\
GjELMAkGA1UEBhMCQkUxCzAJBgNVBAMMAmNhMB4XDTI0MDMyMzE3MzU1MloXDTQ0\r\n\
MDQwNzE3MzU1MlowGjELMAkGA1UEBhMCQkUxCzAJBgNVBAMMAmNhMFkwEwYHKoZI\r\n\
zj0CAQYIKoZIzj0DAQcDQgAEB00rHNthOOYyKj80cd/DHQRBGSbJmIRW7rZBNA6g\r\n\
fbEUrY9NbuhGS6zKo3K59zYc5R1U4oBM3bj6Q7LJfTu7JqNjMGEwHQYDVR0OBBYE\r\n\
FHdXDRjWxS8yGRfvHJKxfnm2+y7xMB8GA1UdIwQYMBaAFHdXDRjWxS8yGRfvHJKx\r\n\
fnm2+y7xMA8GA1UdEwEB/wQFMAMBAf8wDgYDVR0PAQH/BAQDAgGGMAoGCCqGSM49\r\n\
BAMDA0cAMEQCID7AcgACnXWzZDLYEainxVDxEJTUJFBhcItO77gcHPZUAiAu/ZMO\r\n\
VYg4UI2D74WfVxn+NyVd2/aXTvSBp8VgyV3odA==\r\n\-----END CERTIFICATE-----\r\n";

String diagData = "NO SERIAL DATA RECEIVED - CHECK CONNECTION";
char dataBuffer[MAX_BUFFER_SIZE];
static char outgoingMsg[MAX_BUFFER_SIZE] = "";
uint8_t cborBuffer[1024];
char deviceName[20];
unsigned long previousMillis = 0;
String Modem_Status = "Unknown";
String modemBand = "";
String modemNetName = "";
String modemRSRP = "";
String modemRSRQ = "";
String imei = "";
String imeisv = "";
String svn = "";
String macStr = "";
int64_t currentTimestamp = 0;
uint32_t lastMillis = 0;
bool firstTime = true;
static unsigned long lastStatusTime = 0;
static unsigned long statusInterval = 36000000;
static unsigned long lastSendTime = 0;
static unsigned long lastFirmwareTime = 0;
static unsigned long lastFirmwareFreq = 60000;
static unsigned long readTime = 0;
static unsigned int readInterval = 5000;
static unsigned long int seq = 0;
static unsigned long lastSDCheck = 0;
unsigned long currentTime = millis();
int unsigned noSerialCount = 0;
static unsigned int noComInterval = 86400000;
static unsigned int sendInterval;
static unsigned int parseSDInterval=15000;
int lastParseTime = 0 ;
String DeviceName = "";  // Device name (e.g., "RND-0007", "CSX-1234")
bool firstLostComm = 0;
int NoSerial = 0;
AsyncWebServer server(80);
AsyncDNSServer dnsServer;
const byte DNS_PORT = 53;

// Dynamic WiFi AP name - includes device ID or random hex suffix
// Format: "GM IO Board XXXXXX" where XXXXXX is device ID or random hex
char apSSID[32] = "GM IO Board";  // Will be updated with suffix
String apSuffix = "";             // Stores the current suffix (random or device ID)
bool apNameFromSerial = false;    // True if AP name was set from serial device ID

// Status JSON output to Python device (via Serial1/RS-232)
// Sends datetime, SD status, and passthrough value every 15 seconds
unsigned long lastStatusSendTime = 0;
const unsigned long STATUS_SEND_INTERVAL = 15000;  // 15 seconds in milliseconds

// Passthrough mode - allows IO Board to act as a serial bridge to the modem
// When enabled, Serial1 (RS-232) data is forwarded directly to/from the modem
// This allows Linux host to use AT commands and PPP connections via the modem
// NOTE: Passthrough mode is NOT persisted - always returns to normal on reboot
bool passthroughMode = false;      // True when passthrough is active
int passthroughValue = 0;          // 0=normal operation, 1=passthrough active (sent in JSON status)
unsigned long passthroughStartTime = 0;   // Timestamp when passthrough mode was entered
unsigned long passthroughTimeoutMs = 0;   // Timeout in milliseconds (set from "remote XX" message)
const unsigned long DEFAULT_PASSTHROUGH_TIMEOUT_MS = 60UL * 60UL * 1000UL;  // Default 60 minutes

// Forward declarations for passthrough functions (needed for lambda in web server)
bool sendPassthroughRequestToLinux(unsigned long timeoutMinutes);
void enterPassthroughMode(unsigned long timeoutMinutes = 60);
void exitPassthroughMode();
void runPassthroughLoop();

// QC mode removed in Rev 9.4 - web interface is now read-only diagnostic dashboard
// Relay control is exclusively managed by I2C from the Linux master

uint8_t dataBuf[8] = { 0 };
uint8_t incomingBuf[256] = { 0 };
uint16_t counter = 0;

String idStr = "";
int id;
int readings[12][10];
int readingCount = 0;
float pressure = 0.0;
int pres_scaled = 0;
int profile=2;
int temp_scaled = 0;
int cycles = 0, faults = 0, mode = 0;
float current = -99.8;
File logFile;
unsigned long lastFlush = 0;
const unsigned long FLUSH_INTERVAL = 30 * 1000;

// =====================================================================
// SERIAL WATCHDOG FUNCTION
// =====================================================================

/**
 * Print an obnoxious banner message for watchdog events
 * Makes it very easy to spot in log files
 * 
 * Usage example:
 *   printWatchdogBanner("REBOOT ATTEMPT #1");
 */
void printWatchdogBanner(const char* message) {
    Serial.printf("[WATCHDOG] %s (Device: %s, Attempts: %d)\r\n", 
                  message, deviceName, watchdogRebootAttempts);
}

/**
 * Pulse GPIO39 HIGH for specified duration, then LOW
 * This function is called when no serial data has been received for 30 minutes
 * The pulse triggers an external relay to power cycle the device sending serial data
 * 
 * First attempt: 1 second pulse (quick reboot)
 * Subsequent attempts: 30 second pulse (longer power cycle)
 * 
 * Usage example:
 *   pulseWatchdogPin(1000);   // 1 second pulse
 *   pulseWatchdogPin(30000);  // 30 second pulse
 */
void pulseWatchdogPin(unsigned long pulseDuration) {
    Serial.printf("[WATCHDOG] GPIO39 HIGH - %lu sec pulse (Device: %s, Attempt #%d)\r\n", 
                  pulseDuration / 1000, deviceName, watchdogRebootAttempts);
    
    digitalWrite(ESP_WATCHDOG_PIN, HIGH);
    
    // For long pulses, print status every 10 seconds
    if (pulseDuration > 5000) {
        unsigned long startTime = millis();
        while (millis() - startTime < pulseDuration) {
            unsigned long remaining = (pulseDuration - (millis() - startTime)) / 1000;
            Serial.printf("[WATCHDOG] Power cycle... %lu sec remaining\r\n", remaining);
            delay(10000);
        }
    } else {
        delay(pulseDuration);
    }
    
    digitalWrite(ESP_WATCHDOG_PIN, LOW);
    Serial.println("[WATCHDOG] GPIO39 LOW - pulse complete, waiting for serial data");
}

/**
 * Check serial watchdog timer and trigger pulse if timeout exceeded
 * Monitors the time since last serial data reception. If 30 minutes pass
 * without any serial data and the watchdog is enabled, triggers a power cycle
 * pulse on GPIO39 to attempt to restore communications.
 * 
 * First attempt: 1 second pulse
 * All subsequent attempts: 30 second pulse (every 30 minutes)
 * 
 * During no-serial state, data is sent with zeroed values and fault code 1024
 * 
 * The watchdog automatically resets when serial data resumes.
 * 
 * Usage example:
 *   checkSerialWatchdog();  // Call periodically in main loop
 */
void checkSerialWatchdog() {
    // Only check if watchdog feature is enabled
    if (!watchdogEnabled) {
        return;
    }
    
    // Calculate time since last serial data
    unsigned long timeSinceLastData = millis() - lastSerialDataTime;
    
    // Check if we've exceeded the initial 30-minute timeout
    if (timeSinceLastData >= WATCHDOG_TIMEOUT) {
        
        // Mark that we're in watchdog state (no serial data)
        if (!watchdogTriggered) {
            watchdogTriggered = true;
            lastWatchdogPulseTime = 0;  // Force immediate first pulse
            Serial.printf("[WATCHDOG] ACTIVATED - no data for %lu min, fault code 1024\r\n", 
                          timeSinceLastData / 60000);
        }
        
        // Check if it's time for another reboot attempt (every 30 minutes)
        unsigned long timeSinceLastPulse = millis() - lastWatchdogPulseTime;
        
        if (lastWatchdogPulseTime == 0 || timeSinceLastPulse >= WATCHDOG_TIMEOUT) {
            watchdogRebootAttempts++;
            
            // Determine pulse duration: 1 second for first attempt, 30 seconds for subsequent
            unsigned long pulseDuration;
            if (watchdogRebootAttempts == 1) {
                pulseDuration = WATCHDOG_FIRST_PULSE;  // 1 second
            } else {
                pulseDuration = WATCHDOG_LONG_PULSE;   // 30 seconds
            }
            
            // Trigger the watchdog pulse
            pulseWatchdogPin(pulseDuration);
            
            // Record when we pulsed
            lastWatchdogPulseTime = millis();
            
            Serial.printf("[WATCHDOG] Next attempt in 30 min if no serial data\r\n");
        }
    }
}

/**
 * Check if we're currently in watchdog state (no serial data)
 * Used by data sending functions to determine if we should send zeroed data
 * 
 * Returns: true if watchdog has been triggered (no serial for 30+ minutes)
 * 
 * Usage example:
 *   if (isInWatchdogState()) {
 *       // Send zeroed data with fault code 1024
 *   }
 */
bool isInWatchdogState() {
    return watchdogTriggered && watchdogEnabled;
}

/**
 * Get the watchdog fault code (1024) for use in data packets
 * Returns 0 if not in watchdog state
 * 
 * Usage example:
 *   int fault = getWatchdogFaultCode();  // Returns 1024 if in watchdog state
 */
int getWatchdogFaultCode() {
    return isInWatchdogState() ? WATCHDOG_FAULT_CODE : 0;
}

/**
 * Get the BlueCherry fault code (4096) for use in data packets
 * Returns 4096 if BlueCherry is not connected, 0 if connected
 * This alerts the user that OTA updates are unavailable
 * 
 * Usage example:
 *   int fault = getBlueCherryFaultCode();  // Returns 4096 if BC disconnected
 */
int getBlueCherryFaultCode() {
    return blueCherryConnected ? 0 : BLUECHERRY_FAULT_CODE;
}

/**
 * Get combined fault code for all system issues
 * Combines watchdog (1024) + BlueCherry (4096) fault codes
 * 
 * Usage example:
 *   int faults = getCombinedFaultCode();  // Returns sum of active fault codes
 *   // 0 = no faults
 *   // 1024 = watchdog triggered (no serial data)
 *   // 4096 = BlueCherry disconnected
 *   // 5120 = both watchdog AND BlueCherry faults
 */
int getCombinedFaultCode() {
    return getWatchdogFaultCode() + getBlueCherryFaultCode();
}

/**
 * Reset watchdog timer when serial data is received
 * Call this function whenever valid serial data is received to reset
 * the 30-minute timeout counter and clear all watchdog state.
 * 
 * Usage example:
 *   resetSerialWatchdog();  // Call when serial data arrives
 */
void resetSerialWatchdog() {
    // Only print recovery message if we were in watchdog state
    if (watchdogTriggered && watchdogEnabled) {
        Serial.println("\n");
        Serial.println("╔══════════════════════════════════════════════════════════════════════════════╗");
        Serial.println("║  ✅✅✅ SERIAL DATA RESTORED - WATCHDOG RESET ✅✅✅                         ║");
        Serial.println("╠══════════════════════════════════════════════════════════════════════════════╣");
        Serial.print("║  Total reboot attempts before recovery: ");
        Serial.print(watchdogRebootAttempts);
        Serial.println("                                   ║");
        Serial.println("║  Resuming normal operation with live data                                   ║");
        Serial.println("╚══════════════════════════════════════════════════════════════════════════════╝");
        Serial.println("\n");
    }
    
    lastSerialDataTime = millis();
    watchdogTriggered = false;
    watchdogRebootAttempts = 0;
    lastWatchdogPulseTime = 0;
}

// =====================================================================
// THREAD-SAFE RELAY CONTROL
// =====================================================================

/**
 * Thread-safe relay write with mutex protection
 * Can be called from web interface or set_relay_mode
 * Uses 10ms timeout - long enough for normal use, short enough to not block
 * 
 * CRITICAL: DISP_SHUTDN protection enforced - forces ON during startup lockout
 */
void setRelayStateSafe(uint8_t relayMask) {
    // DISP_SHUTDN Protection: Determine what state V6 should be in
    // During protection period, ALWAYS force HIGH (ON) regardless of requested mask
    bool dispShutdownState;
    if (dispShutdownProtectionActive) {
        dispShutdownState = HIGH;  // Protection active - FORCE ON
        // Also ensure the mask bit is set so gpioA_value reflects reality
        relayMask |= 0x10;
    } else {
        // Protection expired - allow requested state (bit 4)
        dispShutdownState = (relayMask & 0x10) ? HIGH : LOW;
    }
    
    if (relayMutex != NULL && xSemaphoreTake(relayMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        gpioA_value = relayMask;
        
        digitalWrite(CR0_MOTOR, (gpioA_value & 0x01) ? HIGH : LOW);
        digitalWrite(CR1, (gpioA_value & 0x02) ? HIGH : LOW);
        digitalWrite(CR2, (gpioA_value & 0x04) ? HIGH : LOW);
        digitalWrite(CR5, (gpioA_value & 0x08) ? HIGH : LOW);
        // V6 (DISP_SHUTDN) - protected during startup
        digitalWrite(DISP_SHUTDN, dispShutdownState);
        
        xSemaphoreGive(relayMutex);
    } else {
        // Mutex timeout - log error but still try to set relays
        // This shouldn't happen in normal operation
        // Note: Serial output removed - may run from Core 0 context
        gpioA_value = relayMask;
        digitalWrite(CR0_MOTOR, (relayMask & 0x01) ? HIGH : LOW);
        digitalWrite(CR1, (relayMask & 0x02) ? HIGH : LOW);
        digitalWrite(CR2, (relayMask & 0x04) ? HIGH : LOW);
        digitalWrite(CR5, (relayMask & 0x08) ? HIGH : LOW);
        // V6 (DISP_SHUTDN) - protected during startup
        digitalWrite(DISP_SHUTDN, dispShutdownState);
    }
}

/**
 * Thread-safe relay read with mutex protection
 * Uses short timeout for I2C callback compatibility
 * 
 * IMPORTANT: I2C slave callbacks must respond within microseconds.
 * If mutex is held, we return the cached value without waiting.
 */
uint8_t getRelayStateSafe() {
    uint8_t value = gpioA_value;  // Default to current value (volatile read)
    
    // Try to get mutex with very short timeout (1ms max)
    // If mutex is busy, return cached value - better than blocking I2C
    if (relayMutex != NULL && xSemaphoreTake(relayMutex, pdMS_TO_TICKS(1)) == pdTRUE) {
        value = gpioA_value;
        xSemaphoreGive(relayMutex);
    }
    return value;
}

/**
 * Set relay mode using predefined patterns
 * Integrates with MCP emulator for web control
 */
void set_relay_mode(RunMode mode) {
    current_mode = mode;
    mode_start_time = millis();
    
    uint8_t relay_pattern = 0x00;
    
    switch (mode) {
        case IDLE_MODE:
            relay_pattern = 0x00;  // All relays OFF (except V6)
            break;
            
        case RUN_MODE:
            relay_pattern = 0x0B;  // Motor + CR1 + CR5 = 0000 1011
            break;
            
        case PURGE_MODE:
            relay_pattern = 0x05;  // Motor + CR2 = 0000 0101
            break;
            
        case BURP_MODE:
            relay_pattern = 0x08;  // CR5 only = 0000 1000
            break;
    }
    
    // V6 (bit 4, 0x10) MUST stay ON - can ONLY be turned off via explicit I2C command
    // CRITICAL: ALWAYS preserve V6 state from current gpioA_value unless I2C explicitly clears it
    // This function is for web/QC control of relays 0-3, NOT for V6 control
    relay_pattern |= (gpioA_value & 0x10);  // Preserve current V6 state
    
    setRelayStateSafe(relay_pattern);
    
    // Mark as web override when called from web interface
    if (webOverrideActive) {
        lastWebActivity = millis();
    }
}

// =====================================================================
// MCP23017 EMULATOR FUNCTIONS
// =====================================================================

/**
 * Overfill Input Validation System
 * 
 * PROBLEM: GPIO38 can have transient false LOW readings during:
 *   - Power cycles (before pull-up stabilizes)
 *   - Firmware flashes (reset transitions)
 *   - Electrical noise on the line
 * 
 * SOLUTION: Multi-layer validation
 *   1. Startup lockout: Ignore readings for first 15 seconds after boot
 *   2. High threshold: Require 8 consecutive LOW readings (was 5) to trigger
 *   3. Hysteresis: Require 5 consecutive HIGH readings (was 3) to clear
 *   4. Validation flag: Track when monitoring is considered reliable
 * 
 * Overfill sensor is ACTIVE LOW: LOW = overfill detected, HIGH = normal
 */
static uint8_t overfillHighCount = 0;  // Counter for HIGH readings to clear alarm

void mcpReadInputsFromPhysicalPins() {
    unsigned long currentMillis = millis();
    
    // Initialize startup time on first call
    if (overfillStartupTime == 0) {
        overfillStartupTime = currentMillis;
        // Note: Serial output removed - runs on Core 0, corrupts Core 1 output
    }
    
    // STARTUP LOCKOUT: Don't process readings for first 15 seconds
    // This allows GPIO38 and pull-up resistor to fully stabilize
    if (!overfillValidationComplete) {
        if (currentMillis - overfillStartupTime >= OVERFILL_STARTUP_LOCKOUT) {
            overfillValidationComplete = true;
            overfillLowCount = 0;
            overfillHighCount = 0;
            overfillAlarmActive = false;  // Start with alarm OFF after validation
            // Note: Serial output removed - runs on Core 0
        } else {
            // During lockout, just set gpioB to normal (no alarm)
            gpioB_value = 0x00;
            return;
        }
    }
    
    // Normal operation: Check GPIO38 state every second for overfill alarm detection
    if (currentMillis - overfillCheckTimer >= 1000) {
        overfillCheckTimer = currentMillis;
        
        uint8_t gpio38State = digitalRead(ESP_OVERFILL);
        
        if (gpio38State == LOW) {
            // Sensor detecting overfill condition
            overfillLowCount++;
            overfillHighCount = 0;  // Reset HIGH counter
            
            // Require OVERFILL_TRIGGER_COUNT consecutive LOW readings to trigger alarm
            if (overfillLowCount >= OVERFILL_TRIGGER_COUNT && !overfillAlarmActive) {
                overfillAlarmActive = true;
                // Note: Serial output removed - runs on Core 0
            }
        } else {
            // Normal state (HIGH due to pull-up)
            overfillHighCount++;
            overfillLowCount = 0;  // Reset LOW counter
            
            // Require OVERFILL_CLEAR_COUNT consecutive HIGH readings to clear alarm (hysteresis)
            if (overfillHighCount >= OVERFILL_CLEAR_COUNT && overfillAlarmActive) {
                overfillAlarmActive = false;
                // Note: Serial output removed - runs on Core 0
            }
        }
        
        // Cap counters to prevent overflow
        if (overfillLowCount > 100) overfillLowCount = 100;
        if (overfillHighCount > 100) overfillHighCount = 100;
    }
    
    // Set GPIOB value based on overfill alarm state
    gpioB_value = overfillAlarmActive ? 0x01 : 0x00;
}

/**
 * DISP_SHUTDN (V6) Startup Protection Check
 * 
 * PROBLEM: During power cycles and firmware flashes, GPIO13 can momentarily
 * go LOW before the firmware fully initializes, causing unintended site shutdown.
 * 
 * SOLUTION: 20-second lockout period after boot where DISP_SHUTDN is FORCED ON
 * regardless of any MCP or web commands. After lockout, normal control resumes.
 * 
 * Call this function periodically (e.g., from MCP emulator task loop)
 */
void checkDispShutdownProtection() {
    unsigned long currentMillis = millis();
    
    // Initialize startup time on first call
    if (dispShutdownStartupTime == 0) {
        dispShutdownStartupTime = currentMillis;
        dispShutdownProtectionActive = true;
        // Force DISP_SHUTDN ON immediately
        digitalWrite(DISP_SHUTDN, HIGH);
        // Note: Serial output removed - runs on Core 0
    }
    
    // Check if lockout period has expired
    if (dispShutdownProtectionActive) {
        if (currentMillis - dispShutdownStartupTime >= DISP_SHUTDN_LOCKOUT) {
            dispShutdownProtectionActive = false;
            // Note: Serial output removed - runs on Core 0
        } else {
            // Still in lockout - ensure relay stays ON
            // This catches any edge cases where the relay might have been set LOW
            if (digitalRead(DISP_SHUTDN) == LOW) {
                digitalWrite(DISP_SHUTDN, HIGH);
                // Note: Serial output removed - runs on Core 0
            }
        }
    }
}

/**
 * Write gpioA_value to physical relay pins
 * Called from I2C handler - must be FAST (no long waits)
 * Returns true if write was performed, false if blocked by web override
 * 
 * CRITICAL: DISP_SHUTDN protection enforced here - forces ON during lockout
 */
bool mcpWriteOutputsToPhysicalPins() {
    // Only apply I2C relay changes if web interface is not active
    if (webOverrideActive && (millis() - lastWebActivity < WEB_OVERRIDE_TIMEOUT)) {
        return false; // Web has priority - ignore I2C relay commands
    }
    
    // Capture gpioA_value atomically ONCE at the start
    // This prevents race conditions where value changes mid-function
    uint8_t capturedValue = gpioA_value;
    
    // DISP_SHUTDN Protection: Determine what state V6 should be in
    // During protection period, ALWAYS force HIGH (ON) regardless of MCP command
    bool dispShutdownState;
    if (dispShutdownProtectionActive) {
        dispShutdownState = HIGH;  // Protection active - FORCE ON
    } else {
        // Protection expired - allow MCP command (bit 4 of captured value)
        dispShutdownState = (capturedValue & 0x10) ? HIGH : LOW;
    }
    
    // Write to physical pins with minimal mutex wait (1ms max)
    // I2C callbacks must respond quickly to avoid bus timeouts
    if (relayMutex != NULL && xSemaphoreTake(relayMutex, pdMS_TO_TICKS(1)) == pdTRUE) {
        digitalWrite(CR0_MOTOR, (capturedValue & 0x01) ? HIGH : LOW);
        digitalWrite(CR1, (capturedValue & 0x02) ? HIGH : LOW);
        digitalWrite(CR2, (capturedValue & 0x04) ? HIGH : LOW);
        digitalWrite(CR5, (capturedValue & 0x08) ? HIGH : LOW);
        // V6 (DISP_SHUTDN) - protected during startup
        digitalWrite(DISP_SHUTDN, dispShutdownState);
        xSemaphoreGive(relayMutex);
        return true;
    } else {
        // Mutex busy - write anyway to keep I2C responsive
        // Use the captured value for consistency
        digitalWrite(CR0_MOTOR, (capturedValue & 0x01) ? HIGH : LOW);
        digitalWrite(CR1, (capturedValue & 0x02) ? HIGH : LOW);
        digitalWrite(CR2, (capturedValue & 0x04) ? HIGH : LOW);
        digitalWrite(CR5, (capturedValue & 0x08) ? HIGH : LOW);
        // V6 (DISP_SHUTDN) - protected during startup
        digitalWrite(DISP_SHUTDN, dispShutdownState);
        return true;
    }
}

/**
 * Handle I2C write transactions from master
 * Uses mutex to protect gpioA_value for thread-safe relay control
 * 
 * Transaction format: [register_address] [data_byte(s)]
 * If only register address is sent, it sets the register pointer for subsequent reads
 */
void handleI2CWrite(int numBytes) {
    if (numBytes == 0) {
        i2cErrorCount++;
        return;
    }
    
    i2cTransactionCount++;
    
    uint8_t reg = Wire.read();
    numBytes--;
    
    // If only register address sent, just set pointer for read
    if (numBytes == 0) {
        registerPointer = reg;
        lastRegisterAccessed = reg;
        return;
    }
    
    // Process data bytes
    while (Wire.available() && numBytes > 0) {
        uint8_t value = Wire.read();
        numBytes--;
        lastRegisterAccessed = reg;
        
        // Note: I2C debug removed - I2C callbacks run on Core 0, corrupt Core 1 serial
        
        switch (reg) {
            case IODIRA:
                iodirA_value = value;
                break;
                
            case IODIRB:
                iodirB_value = value;
                break;
                
            case GPIOA:
            case OLATA:
                // CRITICAL: During protection period, FORCE bit 4 (DISP_SHUTDN) to stay ON
                // This ensures the master cannot accidentally turn off the site during startup
                if (dispShutdownProtectionActive) {
                    value |= 0x10;  // Force V6 bit ON regardless of what master sent
                }
                
                // Write to gpioA_value with short mutex timeout
                // I2C callbacks must be fast - use 1ms timeout max
                if (relayMutex != NULL && xSemaphoreTake(relayMutex, pdMS_TO_TICKS(1)) == pdTRUE) {
                    gpioA_value = value;
                    xSemaphoreGive(relayMutex);
                } else {
                    // Mutex busy - write anyway (volatile write is atomic for uint8_t)
                    gpioA_value = value;
                }
                mcpWriteOutputsToPhysicalPins();
                break;
                
            case GPIOB:
            case OLATB:
                // Port B is input only - ignore writes
                break;
                
            // Store other registers but don't act on them
            case IPOLA:
                ipolA_value = value;
                break;
            case IPOLB:
                ipolB_value = value;
                break;
            case IOCON:
            case IOCON_B:
                iocon_value = value & 0x7F;  // Force BANK=0
                break;
            case GPPUA:
                gppuA_value = value;
                break;
            case GPPUB:
                gppuB_value = value;
                break;
                
            default:
                // Other registers - just increment
                break;
        }
        
        reg++;  // Sequential addressing
    }
    
    i2cLastGoodTransaction = millis();
}

/**
 * Handle I2C read requests from master
 * RESTORED TO ORIGINAL PROVEN BEHAVIOR
 * 
 * Returns register value at current register pointer
 * For GPIOA and IODIRA, writes both A and B bytes (sequential read)
 * NO polarity inversion applied - returns raw values
 */
void handleI2CRead() {
    i2cTransactionCount++;
    lastRegisterAccessed = registerPointer;
    
    // Note: I2C debug removed - I2C callbacks run on Core 0, corrupt Core 1 serial
    
    switch (registerPointer) {
        case IODIRA:
            Wire.write(iodirA_value);
            Wire.write(iodirB_value);
            break;
            
        case IODIRB:
            Wire.write(iodirB_value);
            break;
            
        case GPIOA:
            {
                uint8_t byteA = getRelayStateSafe(); // Thread-safe read
                // CRITICAL: During startup lockout, ALWAYS report no overfill
                // This prevents false alarms from transient GPIO38 states
                uint8_t byteB = overfillValidationComplete ? gpioB_value : 0x00;
                Wire.write(byteA);
                Wire.write(byteB);
            }
            break;
            
        case GPIOB:
            {
                // CRITICAL: During startup lockout, ALWAYS report no overfill
                // This prevents false alarms from transient GPIO38 states
                uint8_t byteB = overfillValidationComplete ? gpioB_value : 0x00;
                Wire.write(byteB);
            }
            break;
            
        case OLATA:
            {
                uint8_t byteA = getRelayStateSafe(); // Thread-safe read
                Wire.write(byteA);
                Wire.write(0x00);
            }
            break;
            
        case OLATB:
            Wire.write(0x00);
            break;
            
        // Return stored values for other registers
        case IPOLA:
            Wire.write(ipolA_value);
            break;
        case IPOLB:
            Wire.write(ipolB_value);
            break;
        case IOCON:
        case IOCON_B:
            Wire.write(iocon_value);
            break;
        case GPPUA:
            Wire.write(gppuA_value);
            break;
        case GPPUB:
            Wire.write(gppuB_value);
            break;
            
        default:
            Wire.write(0x00);
            break;
    }
    
    i2cLastGoodTransaction = millis();
}

// =====================================================================
// I2C BUS RECOVERY AND HEALTH MONITORING
// =====================================================================

// Health monitoring variables
volatile unsigned long i2cLastActivity = 0;      // Timestamp of last I2C activity
volatile bool i2cBusHealthy = true;              // Flag for bus health status
const unsigned long I2C_HEALTH_CHECK_INTERVAL = 30000;  // Check every 30 seconds
const unsigned long I2C_STALE_THRESHOLD = 120000;       // Consider stale after 2 minutes of no activity

/**
 * I2C Bus Recovery - Clears stuck SDA line
 * 
 * When an I2C transaction is interrupted (power cycle, reset, etc.), the SDA line
 * can get stuck LOW by a slave that was in the middle of sending data.
 * This function toggles SCL up to 9 times to allow the slave to complete its byte
 * and release the bus.
 * 
 * Usage: Call before Wire.begin() at startup or when bus errors detected
 */
void i2cBusRecovery() {
    // Note: Serial output removed - runs on Core 0, corrupts Core 1 output
    
    // Temporarily configure pins as GPIO
    pinMode(SDA_PIN, INPUT_PULLUP);
    pinMode(SCL_PIN, OUTPUT);
    
    // Check if SDA is stuck LOW
    if (digitalRead(SDA_PIN) == LOW) {
        // Toggle SCL up to 9 times to free the bus
        for (int i = 0; i < 9; i++) {
            digitalWrite(SCL_PIN, LOW);
            delayMicroseconds(5);
            digitalWrite(SCL_PIN, HIGH);
            delayMicroseconds(5);
            
            // Check if SDA is released
            if (digitalRead(SDA_PIN) == HIGH) {
                break;
            }
        }
        
        // Generate STOP condition (SDA LOW->HIGH while SCL HIGH)
        pinMode(SDA_PIN, OUTPUT);
        digitalWrite(SDA_PIN, LOW);
        delayMicroseconds(5);
        digitalWrite(SCL_PIN, HIGH);
        delayMicroseconds(5);
        digitalWrite(SDA_PIN, HIGH);
        delayMicroseconds(5);
    }
    
    // Brief delay before reinitializing
    delay(10);
}

/**
 * Initialize I2C Slave with bus recovery
 * Call this at startup and when recovering from errors
 */
void initI2CSlave() {
    // First, attempt bus recovery to clear any stuck conditions
    i2cBusRecovery();
    
    // Initialize I2C as slave
    Wire.begin((uint8_t)I2C_ADDRESS, SDA_PIN, SCL_PIN, 100000UL);
    Wire.onReceive(handleI2CWrite);
    Wire.onRequest(handleI2CRead);
    
    // Reset health monitoring
    i2cLastActivity = millis();
    i2cBusHealthy = true;
    i2cTransactionCount = 0;
    i2cErrorCount = 0;
    // Note: Serial output removed - runs on Core 0
}

/**
 * Reset I2C Slave - Full reset with bus recovery
 * Use when bus appears stuck or after errors
 */
void resetI2CSlave() {
    // Note: Serial output removed - runs on Core 0
    Wire.end();
    delay(50);  // Longer delay to let bus settle
    
    initI2CSlave();
}

/**
 * Check I2C bus health and auto-recover if needed
 * Called periodically from the MCP emulator task
 */
void checkI2CHealth() {
    unsigned long now = millis();
    
    // Update activity timestamp when transactions occur
    if (i2cTransactionCount > 0 && i2cLastGoodTransaction > i2cLastActivity) {
        i2cLastActivity = i2cLastGoodTransaction;
    }
    
    // Check for error conditions that warrant a reset
    // If we have errors but no successful transactions, bus might be stuck
    if (i2cErrorCount > 10 && i2cTransactionCount == 0) {
        // Note: Serial output removed - runs on Core 0
        resetI2CSlave();
        return;
    }
    
    // Check if SDA is stuck LOW (indicating a stuck slave condition)
    // Only check if we haven't had recent activity
    // This is informational only - we don't auto-reset just for inactivity
    // because the master might simply not be communicating
}

// =====================================================================
// MCP EMULATOR FREERTOS TASK
// =====================================================================
void mcpEmulatorTask(void *parameter) {
    // =========================================================================
    // CRITICAL: Initialize all MCP state BEFORE I2C starts accepting requests
    // This prevents false alarms and incorrect relay states during boot
    // =========================================================================
    
    // STEP 1: Force overfill state to SAFE (no alarm) FIRST
    // This ensures any I2C read before full init returns correct value
    gpioB_value = 0x00;                    // NO overfill alarm
    overfillAlarmActive = false;           // Alarm flag OFF
    overfillValidationComplete = false;    // Force lockout period
    overfillStartupTime = 0;               // Will be set on first check
    overfillLowCount = 0;                  // Reset trigger counter
    overfillHighCount = 0;                 // Reset clear counter  
    overfillCheckTimer = 0;                // Reset check timer
    
    // STEP 2: Configure overfill input pin EARLY with pull-up
    // Do this BEFORE relay outputs to give pull-up time to stabilize
    // Overfill sensor is active-LOW (pulled LOW when overfill detected)
    // Internal pull-up keeps line HIGH when sensor not active, prevents floating
    pinMode(ESP_OVERFILL, INPUT_PULLUP);
    
    // Brief delay to let pull-up stabilize GPIO38
    vTaskDelay(10 / portTICK_PERIOD_MS);
    
    // STEP 3: Configure relay output pins
    pinMode(CR0_MOTOR, OUTPUT);
    pinMode(CR1, OUTPUT);
    pinMode(CR2, OUTPUT);
    pinMode(CR5, OUTPUT);
    pinMode(DISP_SHUTDN, OUTPUT);
    
    digitalWrite(CR0_MOTOR, LOW);
    digitalWrite(CR1, LOW);
    digitalWrite(CR2, LOW);
    digitalWrite(CR5, LOW);
    // V6 MUST stay ON by default - only turn off via explicit I2C command
    // V6 is ACTIVE HIGH: HIGH = ON, LOW = OFF
    digitalWrite(DISP_SHUTDN, HIGH);
    
    // STEP 4: Ensure gpioA_value matches physical state
    // gpioA_value MUST have bit 4 set to reflect DISP_SHUTDN = ON
    // This prevents I2C read-back issues where master thinks V6 is OFF
    gpioA_value = 0x10;  // Only V6/DISP_SHUTDN ON, all others OFF
    
    // STEP 5: Reset DISP_SHUTDN protection state
    dispShutdownProtectionActive = true;   // Enable protection
    dispShutdownStartupTime = 0;           // Will be set on first check
    
    // STEP 6: Initialize I2C slave with bus recovery
    // ONLY after all state is properly initialized
    // This clears any stuck conditions from previous power cycle
    initI2CSlave();
    
    unsigned long lastUpdate = 0;
    unsigned long lastHealthCheck = 0;
    
    // Main MCP emulator loop
    while (true) {
        // Update input states every 50ms
        if (millis() - lastUpdate >= 50) {
            mcpReadInputsFromPhysicalPins();
            
            // Check DISP_SHUTDN protection status (handles startup lockout)
            checkDispShutdownProtection();
            
            // Check if web override has timed out
            if (webOverrideActive && (millis() - lastWebActivity > WEB_OVERRIDE_TIMEOUT)) {
                webOverrideActive = false;
            }
            
            lastUpdate = millis();
        }
        
        // Periodic I2C health check (every 30 seconds)
        if (millis() - lastHealthCheck >= I2C_HEALTH_CHECK_INTERVAL) {
            checkI2CHealth();
            lastHealthCheck = millis();
        }
        
        vTaskDelay(1 / portTICK_PERIOD_MS);
    }
}

// =====================================================================
// WEB INTERFACE
// =====================================================================
// WEB INTERFACE HTML/CSS/JAVASCRIPT
// =====================================================================
// Embedded directly in .ino file for iOS captive portal compatibility

const char* control_html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Walter IO Board - Rev 9.4</title>
    <style>
        * { box-sizing: border-box; }
        body { font-family: -apple-system, Arial, sans-serif; max-width: 600px; margin: 0 auto; padding: 15px; background: #f0f2f5; }
        .hdr { text-align: center; padding: 15px 0; }
        .hdr h1 { margin: 0; font-size: 1.4em; color: #1a1a2e; }
        .hdr p { margin: 4px 0 0; color: #666; font-size: 0.85em; }
        .badge { position: fixed; top: 8px; right: 8px; padding: 4px 10px; border-radius: 12px; font-size: 11px; font-weight: bold; z-index: 100; }
        .badge.ok { background: #4CAF50; color: #fff; }
        .badge.err { background: #f44336; color: #fff; }
        .card { background: #fff; border-radius: 10px; box-shadow: 0 1px 4px rgba(0,0,0,0.08); margin-bottom: 12px; overflow: hidden; }
        .card-title { font-size: 0.9em; font-weight: 700; color: #fff; padding: 8px 14px; margin: 0; }
        .card-body { padding: 10px 14px; }
        .bg-blue { background: #1565c0; }
        .bg-green { background: #2e7d32; }
        .bg-gray { background: #455a64; }
        .bg-orange { background: #e65100; }
        .row { display: flex; justify-content: space-between; padding: 6px 0; border-bottom: 1px solid #f0f0f0; }
        .row:last-child { border-bottom: none; }
        .lbl { color: #777; font-size: 0.85em; }
        .val { font-weight: 600; font-size: 0.95em; color: #222; text-align: right; transition: background 0.3s; }
        .val.flash { background: #e8f5e9; border-radius: 3px; }
        .ok-text { color: #2e7d32; }
        .warn-text { color: #e65100; }
        .err-text { color: #c62828; font-weight: bold; }
        .signal-bar { display: inline-block; width: 60px; height: 10px; background: #e0e0e0; border-radius: 5px; overflow: hidden; vertical-align: middle; margin-left: 6px; }
        .signal-fill { height: 100%; border-radius: 5px; transition: width 0.5s; }
        .cfg-row { display: flex; align-items: center; gap: 8px; padding: 8px 0; }
        .cfg-row input[type=text] { padding: 6px 8px; border: 1px solid #ccc; border-radius: 4px; font-size: 14px; width: 140px; }
        .btn { padding: 6px 14px; border: none; border-radius: 4px; cursor: pointer; font-weight: 600; font-size: 0.85em; color: #fff; }
        .btn-green { background: #4CAF50; }
        .btn-green:active { background: #388E3C; }
        .ts { text-align: center; font-size: 0.7em; color: #aaa; padding: 6px; }
    </style>
</head>
<body>
    <div class="badge ok" id="connBadge">Connected</div>
    <div class="hdr">
        <h1>Walter IO Board</h1>
        <p>Service Diagnostic Dashboard - Rev 9.4</p>
    </div>
    <div class="card">
        <div class="card-title bg-blue">Serial Data (from Linux Device)</div>
        <div class="card-body">
            <div class="row"><span class="lbl">Serial Status</span><span class="val" id="serialStatus">--</span></div>
            <div class="row"><span class="lbl">Device ID</span><span class="val" id="deviceId">--</span></div>
            <div class="row"><span class="lbl">Pressure (IWC)</span><span class="val" id="pressure">--</span></div>
            <div class="row"><span class="lbl">Current (Amps)</span><span class="val" id="current">--</span></div>
            <div class="row"><span class="lbl">Operating Mode</span><span class="val" id="mode">--</span></div>
            <div class="row"><span class="lbl">Run Cycles</span><span class="val" id="cycles">--</span></div>
            <div class="row"><span class="lbl">Fault Code</span><span class="val" id="fault">--</span></div>
        </div>
    </div>
    <div class="card">
        <div class="card-title bg-green">Cellular Modem</div>
        <div class="card-body">
            <div class="row"><span class="lbl">LTE Status</span><span class="val" id="lteStatus">--</span></div>
            <div class="row"><span class="lbl">RSRP (Signal Strength)</span><span class="val" id="rsrp">--</span></div>
            <div class="row"><span class="lbl">RSRQ (Signal Quality)</span><span class="val" id="rsrq">--</span></div>
            <div class="row"><span class="lbl">Signal Level</span><span class="val" id="signalLevel">--<div class="signal-bar"><div class="signal-fill" id="signalBar" style="width:0%;background:#ccc;"></div></div></span></div>
            <div class="row"><span class="lbl">Operator</span><span class="val" id="operator">--</span></div>
            <div class="row"><span class="lbl">Band</span><span class="val" id="band">--</span></div>
            <div class="row"><span class="lbl">BlueCherry Cloud</span><span class="val" id="blueCherry">--</span></div>
        </div>
    </div>
    <div class="card">
        <div class="card-title bg-gray">IO Board Status</div>
        <div class="card-body">
            <div class="row"><span class="lbl">Firmware Version</span><span class="val" id="version">--</span></div>
            <div class="row"><span class="lbl">Board Temperature</span><span class="val" id="temperature">--</span></div>
            <div class="row"><span class="lbl">SD Card</span><span class="val" id="sdCard">--</span></div>
            <div class="row"><span class="lbl">Overfill Sensor</span><span class="val" id="overfill">--</span></div>
            <div class="row"><span class="lbl">I2C Transactions</span><span class="val" id="i2cTx">--</span></div>
            <div class="row"><span class="lbl">I2C Errors</span><span class="val" id="i2cErr">--</span></div>
            <div class="row"><span class="lbl">Watchdog</span><span class="val" id="watchdog">--</span></div>
            <div class="row"><span class="lbl">Uptime</span><span class="val" id="uptime">--</span></div>
            <div class="row"><span class="lbl">MAC Address</span><span class="val" id="mac">--</span></div>
        </div>
    </div>
    <div class="card">
        <div class="card-title bg-orange">Configuration</div>
        <div class="card-body">
            <div class="cfg-row">
                <span class="lbl">Device Name:</span>
                <input type="text" id="nameInput" maxlength="20" value="%DEVICENAME%">
                <button class="btn btn-green" onclick="saveName()">Save</button>
                <span id="nameMsg" style="font-size:11px;"></span>
            </div>
            <div style="font-size:0.75em;color:#999;padding:2px 0 6px;">Used for WiFi AP name and data identification</div>
            <div class="cfg-row">
                <span class="lbl">Serial Watchdog:</span>
                <label style="display:flex;align-items:center;cursor:pointer;">
                    <input type="checkbox" id="wdToggle" onchange="setWatchdog(this.checked)" style="width:20px;height:20px;margin-right:6px;">
                    <span id="wdLabel" style="font-weight:600;font-size:0.85em;">--</span>
                </label>
            </div>
            <div style="font-size:0.75em;color:#999;padding:2px 0;">Pulses GPIO39 if no serial data for 30 min</div>
        </div>
    </div>
    <div class="ts" id="lastUpdate">Waiting for data...</div>
    <script>
        let errs=0;
        function flash(id){const e=document.getElementById(id);if(e){e.classList.add('flash');setTimeout(()=>e.classList.remove('flash'),500);}}
        function upd(id,v){const e=document.getElementById(id);if(e&&e.textContent!==String(v)){e.textContent=v;flash(id);}}
        function conn(ok){const b=document.getElementById('connBadge');b.textContent=ok?'Connected':'Disconnected';b.className='badge '+(ok?'ok':'err');if(ok)errs=0;}
        function signalColor(rsrp){const v=parseFloat(rsrp);if(isNaN(v))return{pct:0,clr:'#ccc',txt:'--'};if(v>=-80)return{pct:100,clr:'#4CAF50',txt:'Excellent'};if(v>=-90)return{pct:75,clr:'#8BC34A',txt:'Good'};if(v>=-100)return{pct:50,clr:'#FF9800',txt:'Fair'};return{pct:25,clr:'#f44336',txt:'Poor'};}
        function poll(){fetch('/api/status').then(r=>{if(!r.ok)throw 0;return r.json();}).then(d=>{
            conn(true);
            // Serial data
            const ss=document.getElementById('serialStatus');
            if(d.serialActive){ss.textContent='Receiving Data';ss.className='val ok-text';}
            else{ss.textContent='No Data';ss.className='val err-text';}
            upd('deviceId',d.deviceName||'--');
            upd('pressure',d.pressure);upd('current',d.current);upd('mode',d.mode);
            upd('cycles',d.cycles);
            const fe=document.getElementById('fault');if(fe){fe.textContent=d.fault||'0';fe.className='val'+(parseInt(d.fault)>0?' warn-text':'');}
            // Modem
            const le=document.getElementById('lteStatus');
            if(d.lteConnected){le.innerHTML='<span class="ok-text">Connected</span>';}else{le.innerHTML='<span class="err-text">Disconnected</span>';}
            upd('rsrp',d.rsrp+' dBm');upd('rsrq',d.rsrq+' dB');
            const s=signalColor(d.rsrp);upd('signalLevel',s.txt);
            const bar=document.getElementById('signalBar');if(bar){bar.style.width=s.pct+'%';bar.style.background=s.clr;}
            upd('operator',d.operator||'--');upd('band',d.band||'--');
            const bc=document.getElementById('blueCherry');
            if(d.blueCherryConnected){bc.innerHTML='<span class="ok-text">Connected</span>';}else{bc.innerHTML='<span class="warn-text">Offline</span>';}
            // IO Board
            upd('version',d.version);upd('temperature',d.temperature+' F');
            const sd=document.getElementById('sdCard');
            if(d.sdCardOK){sd.innerHTML='<span class="ok-text">OK</span>';}else{sd.innerHTML='<span class="err-text">FAULT</span>';}
            const ov=document.getElementById('overfill');
            if(d.overfillAlarm){ov.innerHTML='<span class="err-text">ALARM</span>';}else{ov.innerHTML='<span class="ok-text">Normal</span>';}
            upd('i2cTx',d.i2cTransactions);upd('i2cErr',d.i2cErrors);
            upd('watchdog',d.watchdogEnabled?'Enabled':'Disabled');
            const wt=document.getElementById('wdToggle'),wl=document.getElementById('wdLabel');
            if(wt){wt.checked=d.watchdogEnabled;}
            if(wl){wl.textContent=d.watchdogEnabled?'ENABLED':'DISABLED';wl.style.color=d.watchdogEnabled?'#2e7d32':'#999';}
            upd('uptime',d.uptime);upd('mac',d.macAddress);
            document.getElementById('lastUpdate').textContent='Last update: '+new Date().toLocaleTimeString();
        }).catch(()=>{errs++;if(errs>=3)conn(false);});}
        function saveName(){const n=document.getElementById('nameInput').value.trim(),m=document.getElementById('nameMsg');
            if(n.length<3||n.length>20){m.textContent='3-20 chars required';m.style.color='#c62828';return;}
            m.textContent='Saving...';m.style.color='#666';
            fetch('/setdevicename?name='+encodeURIComponent(n)).then(r=>r.text()).then(()=>{
                m.textContent='Saved!';m.style.color='#2e7d32';setTimeout(()=>m.textContent='',3000);});}
        function setWatchdog(on){fetch('/setwatchdog?enabled='+(on?'1':'0')).then(()=>{
            document.getElementById('wdLabel').textContent=on?'ENABLED':'DISABLED';
            document.getElementById('wdLabel').style.color=on?'#2e7d32':'#999';});}
        document.addEventListener('DOMContentLoaded',function(){if(document.activeElement)document.activeElement.blur();});
        poll();setInterval(poll,2000);
    </script>
</body>
</html>
)rawliteral";

// =====================================================================
// WEB PROCESSOR FUNCTION
// =====================================================================
// This function handles template variable substitution in the HTML
// It replaces %VARIABLE% placeholders with actual values

String webProcessor(const String& var) {
    if (var == "DEVICENAME") return DeviceName;
    return "";
}

// =====================================================================
// RMS FUNCTIONS
// =====================================================================

void initSdLogging() {
    SPI.begin(SCLK, MISO, MOSI, CS);
    int attempts = 0;
    const int maxAttempts = 5;
    while (attempts < maxAttempts) {
        if (SD.cardType() == CARD_NONE) {
            Serial.println("No SD card detected");
            return;
        }
        
        if (SD.begin(CS, SPI, 4000000)) {
            Serial.println("**** SD Card Mounted on attempt " + String(attempts + 1));
            break;
        }
        attempts++;
        Serial.println("**** ERROR - SD Card not Mounted, attempt " + String(attempts) + " of " + String(maxAttempts));
        if (attempts < maxAttempts) {
            delay(2000);
        }
    }
    if (attempts >= maxAttempts) {
        Serial.println("**** ERROR - Failed to mount SD Card after " + String(maxAttempts) + " attempts");
        return;
    }

    time_t rawTime = (time_t)currentTimestamp;
    struct tm *timeInfo = gmtime(&rawTime);
    int currentYear = timeInfo->tm_year + 1900;
    String logFileName = "/logfile" + String(currentYear) + ".log";

    logFile = SD.open(logFileName.c_str(), FILE_APPEND);
    if (!logFile) {
        SD.remove(logFileName.c_str());
        File f = SD.open(logFileName.c_str(), FILE_WRITE);
        if (f) {
            f.close();
            logFile = SD.open(logFileName.c_str(), FILE_APPEND);
        }
        if (!logFile) {
            Serial.print(F("Error: could not open "));
            Serial.print(logFileName);
            Serial.println(F(" for appending"));
        }
    }

    cleanupOldLogFiles(currentYear);
}

void cleanupOldLogFiles(int currentYear) {
    File root = SD.open("/");
    if (!root) {
        Serial.println("Failed to open root directory for cleanup");
        return;
    }

    File file = root.openNextFile();
    while (file) {
        String fileName = String(file.name());
        if (fileName.startsWith("logfile") && fileName.endsWith(".log")) {
            String yearStr = fileName.substring(7, 11);
            int fileYear = yearStr.toInt();
            if (fileYear > 0 && fileYear < currentYear - 2) {
                String fullPath = "/" + fileName;
                if (SD.remove(fullPath.c_str())) {
                    Serial.println("Deleted old log file: " + fullPath);
                } else {
                    Serial.println("Failed to delete old log file: " + fullPath);
                }
            }
        }
        file = root.openNextFile();
    }
    root.close();
}

void SaveToSD(String id, unsigned long seq, int pres, int cycles, int fault, int mode, int temp, float current) {
    if (!logFile) {
        Serial.println("Log file not open, attempting to reopen");
        if (!SD.cardType()) {
            Serial.println("SD card not mounted, attempting to remount");
            int attempts = 0;
            const int maxAttempts = 3;
            while (attempts < maxAttempts) {
                if (SD.begin(CS)) {
                    Serial.println("SD card remounted successfully on attempt " + String(attempts + 1));
                    break;
                }
                attempts++;
                Serial.println("Failed to remount SD card, attempt " + String(attempts) + " of " + String(maxAttempts));
                delay(1000);
            }
            if (attempts >= maxAttempts) {
                Serial.println("Failed to remount SD card after " + String(maxAttempts) + " attempts");
                return;
            }
        }
        time_t rawTime = (time_t)currentTimestamp;
        struct tm *timeInfo = gmtime(&rawTime);
        int currentYear = timeInfo->tm_year + 1900;
        String logFileName = "/logfile" + String(currentYear) + ".log";
        logFile = SD.open(logFileName.c_str(), FILE_APPEND);
        if (!logFile) {
            Serial.print(F("Error: could not reopen "));
            Serial.print(logFileName);
            Serial.println(F(" for appending"));
            File f = SD.open(logFileName.c_str(), FILE_WRITE);
            if (f) {
                f.close();
                logFile = SD.open(logFileName.c_str(), FILE_APPEND);
                if (logFile) {
                    Serial.println("Created and opened log file: " + logFileName);
                } else {
                    Serial.println("Failed to open log file after creation: " + logFileName);
                    return;
                }
            } else {
                Serial.println("Failed to create log file: " + logFileName);
                return;
            }
        } else {
            Serial.println("Successfully reopened log file: " + logFileName);
        }
    }
    String line;
    line.reserve(80);
    line = formatTimestamp(currentTimestamp);
    line += ',' + String(id);
    line += ',' + String(seq);
    line += ',' + String(pres / 100.0, 2);
    line += ',' + String(cycles);
    line += ',' + String(fault);
    line += ',' + String(mode);
    line += ',' + String(temp / 100.0, 2);
    line += ',' + String(current);
    
    Serial.print("Writing to SD: ");
    Serial.println(line);
    if (logFile.println(line) == 0) {
        Serial.println(F("Error: SD write failed"));
        logFile.close();
        time_t rawTime = (time_t)currentTimestamp;
        struct tm *timeInfo = gmtime(&rawTime);
        int currentYear = timeInfo->tm_year + 1900;
        String logFileName = "/logfile" + String(currentYear) + ".log";
        logFile = SD.open(logFileName.c_str(), FILE_APPEND);
        if (!logFile) {
            Serial.print(F("Error: could not reopen file after write failure "));
            Serial.print(logFileName);
            Serial.println(F(" for appending"));
        } else {
            Serial.println("Reopened file after write failure: " + logFileName);
            if (logFile.println(line) == 0) {
                Serial.println(F("Error: SD write failed on retry"));
            } else {
                Serial.println("Successfully wrote to SD on retry");
            }
        }
    } else {
        Serial.println("Successfully wrote line to SD");
    }

    if (millis() - lastFlush > FLUSH_INTERVAL) {
        Serial.println("Flushing SD card data");
        logFile.flush();
        lastFlush = millis();
        Serial.println("Flush complete");
    }

    time_t rawTime = (time_t)currentTimestamp;
    struct tm *timeInfo = gmtime(&rawTime);
    int currentYear = timeInfo->tm_year + 1900;
    static int lastYear = currentYear;
    if (currentYear != lastYear) {
        logFile.close();
        String newLogFileName = "/logfile" + String(currentYear) + ".log";
        logFile = SD.open(newLogFileName.c_str(), FILE_APPEND);
        if (!logFile) {
            Serial.println(F("Error: could not open new logfile for appending"));
        } else {
            Serial.println("Switched to new log file for year " + String(currentYear));
        }
        lastYear = currentYear;
        cleanupOldLogFiles(currentYear);
    }
}

/**
 * Generate a random 6-character hex suffix for the WiFi AP name
 * Uses MAC address + millis() for randomness
 * 
 * Usage: Called on first boot when no device name is set
 * Returns: String like "5c3dfc"
 */
String generateRandomHexSuffix() {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    
    // Combine MAC bytes with millis for randomness
    uint32_t seed = mac[3] ^ mac[4] ^ mac[5] ^ (millis() & 0xFF);
    randomSeed(seed);
    
    // Generate 6 random hex characters
    char hexBuf[7];
    for (int i = 0; i < 6; i++) {
        uint8_t nibble = random(16);
        hexBuf[i] = (nibble < 10) ? ('0' + nibble) : ('a' + nibble - 10);
    }
    hexBuf[6] = '\0';
    
    return String(hexBuf);
}

/**
 * Update the WiFi AP SSID name
 * Combines "GM IO Board " with the device ID or random suffix
 * 
 * @param suffix The suffix to append (device ID like "RND-0007" or random hex like "5c3dfc")
 * @param restartAP If true, restarts the WiFi AP with new name
 * 
 * Usage examples:
 *   updateAPName("5c3dfc", false);     // Set random suffix, don't restart yet
 *   updateAPName("RND-0007", true);    // Set device ID and restart AP
 */
void updateAPName(String suffix, bool restartAP) {
    apSuffix = suffix;
    snprintf(apSSID, sizeof(apSSID), "GM IO Board %s", suffix.c_str());
    
    Serial.print("[WiFi] AP name updated to: ");
    Serial.println(apSSID);
    
    if (restartAP) {
        Serial.println("[WiFi] Restarting Access Point with new name...");
        WiFi.softAPdisconnect(true);
        delay(100);
        WiFi.softAP(apSSID);
        Serial.print("[WiFi] AP restarted. IP: ");
        Serial.println(WiFi.softAPIP());
    }
}

/**
 * Initialize the WiFi AP name based on saved device name or generate random
 * Called during startup before startConfigAP()
 * 
 * - If DeviceName is set (not default "CSX-9000"), use it as suffix
 * - If DeviceName is default, generate random hex suffix
 * - Saves the suffix to EPROM for persistence
 */
void initializeAPName() {
    preferences.begin("RMS", false);
    String savedSuffix = preferences.getString("APSuffix", "");
    preferences.end();
    
    // Check if we have a real device name from serial
    if (DeviceName.length() > 0 && DeviceName != "CSX-9000") {
        // Use device name as suffix
        updateAPName(DeviceName, false);
        apNameFromSerial = true;
        Serial.println("[WiFi] Using device name for AP: " + DeviceName);
    } else if (savedSuffix.length() > 0) {
        // Use previously saved random suffix
        updateAPName(savedSuffix, false);
        apNameFromSerial = false;
        Serial.println("[WiFi] Using saved random suffix: " + savedSuffix);
    } else {
        // Generate new random suffix
        String newSuffix = generateRandomHexSuffix();
        updateAPName(newSuffix, false);
        apNameFromSerial = false;
        
        // Save to EPROM
        preferences.begin("RMS", false);
        preferences.putString("APSuffix", newSuffix);
        preferences.end();
        Serial.println("[WiFi] Generated new random suffix: " + newSuffix);
    }
}

void startConfigAP() {
    WiFi.softAP(apSSID);
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    
    // Simple test page (for debugging blank page issues)
    server.on("/test", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/html", "<html><body><h1>Server Working!</h1><p>If you see this, the server is running.</p><p><a href='/'>Go to main page</a></p></body></html>");
    });
    
    // Main diagnostic dashboard page (read-only, no relay control)
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/html", control_html, webProcessor);
    });
    
    // Set watchdog enabled/disabled endpoint
    server.on("/setwatchdog", HTTP_GET, [](AsyncWebServerRequest *request){
        if (request->hasParam("enabled")) {
            int enabledVal = request->getParam("enabled")->value().toInt();
            watchdogEnabled = (enabledVal == 1);
            
            // Save to preferences/EPROM
            preferences.begin("RMS", false);
            preferences.putBool("watchdog", watchdogEnabled);
            preferences.end();
            
            Serial.print("Watchdog setting changed: ");
            Serial.println(watchdogEnabled ? "ENABLED" : "DISABLED");
            Serial.println("✓ Watchdog setting saved to EPROM");
            
            // Reset watchdog state when toggled
            if (watchdogEnabled) {
                resetSerialWatchdog();
            }
            
            request->send(200, "text/plain", watchdogEnabled ? "Watchdog ENABLED" : "Watchdog DISABLED");
        } else {
            request->send(400, "text/plain", "Missing enabled parameter");
        }
    });
    
    // Set device name endpoint (manual configuration via web portal)
    // Usage: GET /setdevicename?name=RND-0007
    // This allows manual entry before Linux device is connected
    server.on("/setdevicename", HTTP_GET, [](AsyncWebServerRequest *request){
        if (request->hasParam("name")) {
            String newName = request->getParam("name")->value();
            
            // Validate name (alphanumeric, dashes, 3-20 chars)
            if (newName.length() < 3 || newName.length() > 20) {
                request->send(400, "text/plain", "Name must be 3-20 characters");
                return;
            }
            
            // Update device name
            DeviceName = newName;
            strncpy(deviceName, newName.c_str(), sizeof(deviceName) - 1);
            deviceName[sizeof(deviceName) - 1] = '\0';
            
            // Save to preferences/EPROM
            preferences.begin("RMS", false);
            preferences.putString("DeviceName", DeviceName);
            preferences.putString("APSuffix", DeviceName);  // Also update AP suffix
            preferences.end();
            
            // Update WiFi AP name
            updateAPName(DeviceName, true);  // Restart AP with new name
            apNameFromSerial = false;  // Mark as manually set
            
            Serial.println("[Web] Device name manually set to: " + DeviceName);
            Serial.println("✓ Device name saved to EPROM");
            
            request->send(200, "text/plain", "Device name set to: " + DeviceName + "\nWiFi AP updated to: " + String(apSSID));
        } else {
            request->send(400, "text/plain", "Missing name parameter");
        }
    });
    
    // API endpoint for AJAX status updates (returns JSON for diagnostic dashboard)
    // READ-ONLY: No relay control, no mode changes - diagnostic data only
    // Usage: GET /api/status returns JSON with all diagnostic fields
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request){
        float celsius = temperatureRead();
        float fahrenheit = (celsius * 9.0 / 5.0) + 32;
        
        // Determine if serial data is actively being received
        // Consider "active" if data received within last 60 seconds
        bool serialActive = (millis() - lastSerialDataTime < 60000) && (lastSerialDataTime > 0);
        
        // Calculate uptime as human-readable string
        unsigned long uptimeMs = millis() - systemStartTime;
        unsigned long days = uptimeMs / 86400000;
        unsigned long hours = (uptimeMs % 86400000) / 3600000;
        unsigned long minutes = (uptimeMs % 3600000) / 60000;
        char uptimeStr[32];
        if (days > 0) {
            snprintf(uptimeStr, sizeof(uptimeStr), "%lud %luh %lum", days, hours, minutes);
        } else {
            snprintf(uptimeStr, sizeof(uptimeStr), "%luh %lum", hours, minutes);
        }
        
        // Get signal quality strings (updated by syncBlueCherry)
        String rsrpStr = modemRSRP.length() > 0 ? modemRSRP : "0";
        String rsrqStr = modemRSRQ.length() > 0 ? modemRSRQ : "0";
        String operatorStr = modemNetName.length() > 0 ? modemNetName : "Unknown";
        String bandStr = modemBand.length() > 0 ? modemBand : "--";
        
        // Build JSON response with all diagnostic fields
        String json = "{";
        // Serial data from Linux device
        json += "\"serialActive\":" + String(serialActive ? "true" : "false") + ",";
        json += "\"deviceName\":\"" + DeviceName + "\",";
        json += "\"pressure\":\"" + String(pressure, 2) + "\",";
        json += "\"current\":\"" + String(current, 2) + "\",";
        json += "\"mode\":\"" + mode_names[current_mode] + "\",";
        json += "\"cycles\":\"" + String(cycles) + "\",";
        json += "\"fault\":\"" + String(getCombinedFaultCode()) + "\",";
        // Cellular modem
        json += "\"lteConnected\":" + String(lteConnected() ? "true" : "false") + ",";
        json += "\"rsrp\":\"" + rsrpStr + "\",";
        json += "\"rsrq\":\"" + rsrqStr + "\",";
        json += "\"operator\":\"" + operatorStr + "\",";
        json += "\"band\":\"" + bandStr + "\",";
        json += "\"blueCherryConnected\":" + String(blueCherryConnected ? "true" : "false") + ",";
        // IO Board status
        json += "\"version\":\"" + ver + "\",";
        json += "\"temperature\":\"" + String(fahrenheit, 1) + "\",";
        json += "\"sdCardOK\":" + String(isSDCardOK() ? "true" : "false") + ",";
        json += "\"overfillAlarm\":" + String(overfillAlarmActive ? "true" : "false") + ",";
        json += "\"i2cTransactions\":" + String(i2cTransactionCount) + ",";
        json += "\"i2cErrors\":" + String(i2cErrorCount) + ",";
        json += "\"watchdogEnabled\":" + String(watchdogEnabled ? "true" : "false") + ",";
        json += "\"uptime\":\"" + String(uptimeStr) + "\",";
        json += "\"macAddress\":\"" + macStr + "\"";
        json += "}";
        
        request->send(200, "application/json", json);
    });
    
    // Captive portal redirects - serve lightweight page for iOS/Android captive portal detection
    // The full control_html is too large for captive portal popups, so redirect to main page
    server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/html", 
            "<html><head><meta http-equiv='refresh' content='0;url=http://192.168.4.1/'></head>"
            "<body style='font-family:Arial;text-align:center;padding:50px'>"
            "<h1>Walter IO Board</h1>"
            "<p><a href='http://192.168.4.1/'>Open Control Panel</a></p>"
            "<p style='color:#666;font-size:12px'>Or open Safari and go to 192.168.4.1</p>"
            "</body></html>");
    });
    server.on("/fwlink", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/html", 
            "<html><head><meta http-equiv='refresh' content='0;url=http://192.168.4.1/'></head>"
            "<body style='font-family:Arial;text-align:center;padding:50px'>"
            "<h1>Walter IO Board</h1>"
            "<p><a href='http://192.168.4.1/'>Open Control Panel</a></p>"
            "</body></html>");
    });
    server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/html", 
            "<html><head><meta http-equiv='refresh' content='0;url=http://192.168.4.1/'></head>"
            "<body><a href='http://192.168.4.1/'>Open Control Panel</a></body></html>");
    });
    
    server.onNotFound([](AsyncWebServerRequest *request){
        // For unknown URLs, redirect to main page
        request->redirect("http://192.168.4.1/");
    });
    
    server.begin();
    Serial.println("Async web server started");
    Serial.print("AP IP address: ");
    Serial.println(WiFi.softAPIP());
    delay(100);
}

void getHTTPinfo() {
    if (modem.httpConfigProfile(HTTP_PROFILE, "167.172.15.241", 5687, TLS_PROFILE)) {
        Serial.println("Successfully configured the http profile");
    } else {
        Serial.println("Failed to configure HTTP profile");
        return;
    }
    static char url[128];
    static char ctbuf[128];
    snprintf(url, sizeof(url), "lookup?mac=%s", macStr.c_str());
    if (modem.httpQuery(HTTP_PROFILE, url, WALTER_MODEM_HTTP_QUERY_CMD_GET, ctbuf, sizeof(ctbuf), NULL, &rsp)) {
        Serial.println("Request successful");
    } else {
        Serial.println("http query failed");
        delay(1000);
        ESP.restart();
        return;
    }
    bool responseReceived = false;
    unsigned long startTime = millis();
    const unsigned long timeout = 10000;
    while (!responseReceived && (millis() - startTime < timeout)) {
        while (modem.httpDidRing(HTTP_PROFILE, incomingBuf, sizeof(incomingBuf), &rsp)) {
            Serial.println("Webserver response received");
            Serial.printf("[%s]\r\n", incomingBuf);
            if (rsp.data.httpResponse.httpStatus == 200) {
                char* idStart = strstr((char*)incomingBuf, "\"id\":\"");
                char* timingStart = strstr((char*)incomingBuf, "\"timing\":");
                if (idStart && timingStart) {
                    idStart += 6;
                    char idBuf[20];
                    int i = 0;
                    while (idStart[i] != '"' && i < sizeof(idBuf) - 1) {
                        idBuf[i] = idStart[i];
                        i++;
                    }
                    idBuf[i] = '\0';
                    timingStart += 9;
                    int timing = atoi(timingStart);
                    String timingString = String(timing);
                    timing *= 1000;
                    String newId = String(idBuf);
                    if (DeviceName != newId) {
                        DeviceName = newId;
                        strncpy(deviceName, DeviceName.c_str(), sizeof(deviceName) - 1);
                        deviceName[sizeof(deviceName) - 1] = '\0';
                        preferences.begin("RMS", false);
                        preferences.putString("DeviceName", DeviceName);
                        preferences.putString("DeviceNameBackup", DeviceName);
                        preferences.end();
                    }
                    if (sendInterval != timing) {
                        sendInterval = timing;
                        preferences.begin("RMS", false);
                        preferences.putString("sendFrequency", timingString);
                        preferences.end();
                    }
                }
            }
            responseReceived = true;
        }
        if (!responseReceived) delay(100);
    }
    if (!responseReceived) Serial.println("Timeout waiting for HTTP response");
}

// Removed extractDataBeyondComma3() - no longer needed with JSON parsing

bool initModemTime() {
    while (!modem.getClock(&rsp)) {
        if (rsp.result == WALTER_MODEM_STATE_OK) {
            currentTimestamp = rsp.data.clock.epochTime;
            lastMillis = millis();
            Serial.print("Raw modem timestamp (UTC): ");
            Serial.println(rsp.data.clock.epochTime);
            return true;
        } else {
            Serial.println("Invalid timestamp from modem!");
            return false;
        }
    }
}

void updateTimestamp() {
    uint32_t nowMillis = millis();
    if (nowMillis < lastMillis) {
        currentTimestamp += (0xFFFFFFFF - lastMillis + nowMillis) / 1000;
    } else {
        currentTimestamp += (nowMillis - lastMillis) / 1000;
    }
    lastMillis = nowMillis;
}

String formatTimestamp(int64_t seconds) {
    time_t rawTime = (time_t)seconds;
    struct tm *timeInfo = gmtime(&rawTime);
    char buffer[20];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
             timeInfo->tm_year + 1900, timeInfo->tm_mon + 1, timeInfo->tm_mday,
             timeInfo->tm_hour, timeInfo->tm_min, timeInfo->tm_sec);
    return String(buffer);
}

/**
 * Check if SD card is operational
 * Returns true if SD card is mounted and accessible, false otherwise
 * 
 * Usage example:
 *   if (isSDCardOK()) { ... }
 */
bool isSDCardOK() {
    // Check if SD card type is valid (not CARD_NONE)
    return (SD.cardType() != CARD_NONE);
}

/**
 * Send status JSON to Python device via Serial1 (RS-232)
 * Called every 15 seconds from main loop
 * 
 * JSON Format:
 *   {"datetime":"2026-01-30 12:34:56","sdcard":"OK","passthrough":0,"lte":1,"rsrp":"-85.5","rsrq":"-10.2"}
 * 
 * Fields:
 *   - datetime: Current date/time from modem (UTC)
 *   - sdcard: "OK" or "FAULT" based on SD card status
 *   - passthrough: 0 or 1 (passthrough mode status)
 *   - lte: 1 if connected to LTE, 0 if not
 *   - rsrp: Signal strength in dBm (typical: -80 excellent, -100 poor)
 *   - rsrq: Signal quality in dB (typical: -10 good, -20 poor)
 * 
 * Usage example:
 *   sendStatusToSerial();  // Call every 15 seconds
 */
void sendStatusToSerial() {
    // Get current timestamp from modem
    String dateTimeStr = formatTimestamp(currentTimestamp);
    
    // Check SD card status
    String sdStatus = isSDCardOK() ? "OK" : "FAULT";
    
    // Check LTE connection status
    int lteStatus = lteConnected() ? 1 : 0;
    
    // Get signal quality - use stored values (updated by syncBlueCherry)
    // modemRSRP and modemRSRQ are global strings updated when cell info is retrieved
    String rsrpStr = modemRSRP.length() > 0 ? modemRSRP : "0";
    String rsrqStr = modemRSRQ.length() > 0 ? modemRSRQ : "0";
    
    // Build JSON string
    // Using snprintf for efficiency and to avoid String concatenation overhead
    char jsonBuffer[200];
    snprintf(jsonBuffer, sizeof(jsonBuffer),
             "{\"datetime\":\"%s\",\"sdcard\":\"%s\",\"passthrough\":%d,\"lte\":%d,\"rsrp\":\"%s\",\"rsrq\":\"%s\"}",
             dateTimeStr.c_str(),
             sdStatus.c_str(),
             passthroughValue,
             lteStatus,
             rsrpStr.c_str(),
             rsrqStr.c_str());
    
    // Send to Python device via Serial1 (RS-232)
    Serial1.println(jsonBuffer);
    
    // Debug output to Serial Monitor
    Serial.print("[RS232-TX] ");
    Serial.println(jsonBuffer);
}

/**
 * Check if it's time to send status to Python and send if due
 * Called from main loop - handles timing internally
 * 
 * IMPORTANT: Skips sending during passthrough mode to avoid interfering
 * with modem communication (PPP/AT commands)
 * 
 * Usage: Call in loop() - it will only send every STATUS_SEND_INTERVAL ms
 */
void checkAndSendStatusToSerial() {
    // Skip during passthrough mode - serial ports are dedicated to modem bridge
    if (passthroughMode) {
        return;
    }
    
    unsigned long currentTime = millis();
    
    if (currentTime - lastStatusSendTime >= STATUS_SEND_INTERVAL) {
        sendStatusToSerial();
        lastStatusSendTime = currentTime;
    }
}

// =====================================================================
// QUALITY CHECK TEST MODE - ADC FUNCTIONS
// =====================================================================

// QC mode and ADS1015 ADC removed in Rev 9.4 (web simplification)
// Relay control is exclusively handled by I2C from the Linux master

// =====================================================================
// PASSTHROUGH MODE FUNCTIONS
// =====================================================================
// PASSTHROUGH MODE (Preference-based boot)
// =====================================================================
// When "remote XX" command is received, we store the request in preferences
// and restart. On boot, the firmware checks for this preference BEFORE
// loading WalterModem and runs a minimal passthrough-only mode.
// This ensures a clean serial port without WalterModem interference.
// =====================================================================

/**
 * Send passthrough request to Linux device and wait for "ready" confirmation
 * 
 * Sends JSON message: {"passthrough":"remote XX"} where XX is timeout in minutes
 * Waits up to 10 seconds for Linux to respond with "ready" (case-insensitive)
 * 
 * @param timeoutMinutes Passthrough duration to send to Linux
 * @return true if Linux confirmed ready, false if timeout or no response
 * 
 * Example:
 *   sendPassthroughRequestToLinux(60) sends {"passthrough":"remote 60"}
 *   Linux responds with "ready" or similar containing "ready"
 */
bool sendPassthroughRequestToLinux(unsigned long timeoutMinutes) {
    // Send passthrough request to Linux device
    String request = "{\"passthrough\":\"remote " + String(timeoutMinutes) + "\"}";
    Serial1.println(request);
    Serial.printf("[PASSTHROUGH] Sent to Linux: %s\r\n", request.c_str());
    
    // Wait for "ready" confirmation (up to 10 seconds)
    unsigned long startTime = millis();
    const unsigned long READY_TIMEOUT_MS = 10000;  // 10 second timeout
    String response = "";
    
    Serial.println("[PASSTHROUGH] Waiting for Linux 'ready' confirmation...");
    
    while (millis() - startTime < READY_TIMEOUT_MS) {
        while (Serial1.available()) {
            char c = Serial1.read();
            if (c == '\n' || c == '\r') {
                // Check if response contains "ready" (case-insensitive)
                response.toLowerCase();
                if (response.indexOf("ready") >= 0) {
                    Serial.println("[PASSTHROUGH] Linux confirmed ready");
                    return true;
                }
                response = "";  // Reset for next line
            } else {
                response += c;
                // Safety limit
                if (response.length() > 100) {
                    response = response.substring(response.length() - 50);
                }
            }
        }
        delay(10);  // Small delay to prevent tight loop
    }
    
    Serial.println("[PASSTHROUGH] WARNING: No 'ready' from Linux (timeout)");
    return false;
}

/**
 * Request passthrough mode - notifies Linux, then sets preference and restarts
 * 
 * Sequence:
 * 1. Send {"passthrough":"remote XX"} to Linux device via Serial1
 * 2. Wait up to 10 seconds for "ready" confirmation from Linux
 * 3. If confirmed (or timeout), store preference and restart into passthrough boot
 * 
 * On boot, the firmware checks for this preference BEFORE loading the WalterModem
 * library and runs a minimal passthrough-only mode if set.
 * 
 * @param timeoutMinutes Duration before auto-restart (default 60 minutes, max 1440 = 24hrs)
 * 
 * Example usage (from BlueCherry message):
 * - "remote" → 60 minutes (default)
 * - "remote 30" → 30 minutes
 * - "remote 120" → 120 minutes (2 hours)
 * 
 * Early Exit: Linux device can set MCP GPA5 (bit 5) via I2C to trigger early return
 */
void enterPassthroughMode(unsigned long timeoutMinutes) {
    // Clamp timeout to reasonable range
    if (timeoutMinutes < 1) timeoutMinutes = 1;
    if (timeoutMinutes > 1440) timeoutMinutes = 1440;  // Max 24 hours
    
    Serial.printf("[PASSTHROUGH] Requesting %lu min timeout\r\n", timeoutMinutes);
    
    // Notify Linux device and wait for confirmation
    bool linuxReady = sendPassthroughRequestToLinux(timeoutMinutes);
    
    if (!linuxReady) {
        // Proceed anyway - Linux may still be ready even without confirmation
        Serial.println("[PASSTHROUGH] Proceeding without Linux confirmation");
    }
    
    // Store passthrough request in preferences
    preferences.begin("RMS", false);
    preferences.putBool("ptBoot", true);
    preferences.putULong("ptTimeout", timeoutMinutes);
    preferences.end();
    
    Serial.println("[PASSTHROUGH] Restarting into passthrough mode...");
    delay(500);
    ESP.restart();
}

/**
 * Exit passthrough mode - stub function (not used in preference-based boot)
 * 
 * In the preference-based approach, passthrough runs on boot and
 * auto-restarts when complete. This function is kept for compatibility.
 */
void exitPassthroughMode() {
    Serial.println("[Passthrough] Exit requested - restarting...");
    delay(1000);
    ESP.restart();
}

/**
 * Passthrough loop - stub function (not used in preference-based boot)
 * 
 * The passthrough loop now runs in runPassthroughBootMode() which is called
 * during setup() when the passthrough preference is set. This stub function
 * is kept for compatibility with existing main loop code.
 */
void runPassthroughLoop() {
    // Not used in preference-based boot approach
    // Passthrough runs from runPassthroughBootMode() on boot
}

bool isInteger(const char* str) {
    if (str == NULL || *str == '\0') return false;
    for (int i = 0; str[i] != '\0'; i++) {
        if (!isdigit(str[i]) && (i != 0 || str[i] != '-')) return false;
    }
    return true;
}

/**
 * Read JSON data from Serial1
 * Expected format: {"type":"data","gmid":"RND-0007","press":-14.22,"mode":0,"current":0.07,"fault":0,"cycles":484}
 * This replaces the old CSV parser with a simpler JSON parser
 * 
 * IMPORTANT: Skips during passthrough mode - serial ports are dedicated to modem bridge
 */
void readSerialData() {
    // Skip during passthrough mode - serial ports are dedicated to modem bridge
    if (passthroughMode) {
        return;
    }
    
    static String jsonBuffer = "";
    static unsigned long lastRxTime = 0;
    static bool debugPrinted = false;
    static unsigned long lastDebugTime = 0;
    
    // Debug: Print once to confirm Serial1 is being checked
    if (!debugPrinted) {
        Serial.println("🔍 DEBUG: readSerialData() is active, Serial1 RX=GPIO44 TX=GPIO43");
        debugPrinted = true;
    }
    
    // Debug: Show Serial1.available() every 30 seconds
    if (millis() - lastDebugTime > 30000) {
        Serial.print("🔍 Serial1.available() = ");
        Serial.println(Serial1.available());
        lastDebugTime = millis();
    }
    
    while (Serial1.available() > 0) {
        char ch = Serial1.read();
        
        // Start of JSON object
        if (ch == '{') {
            jsonBuffer = "{";
            noSerialCount = 0;
            lastRxTime = millis();
            resetSerialWatchdog();  // Reset watchdog timer when data arrives
            Serial.print("→ Receiving JSON: ");
        }
        // Accumulate characters
        else if (jsonBuffer.length() > 0) {
            jsonBuffer += ch;
            
            // End of JSON object - process it immediately
            if (ch == '}') {
                Serial.println(jsonBuffer);  // Print complete JSON
                
                // Check for passthrough command from Linux: {"command":"passthrough","timeout":XX}
                if (jsonBuffer.indexOf("\"command\"") >= 0 && 
                    jsonBuffer.indexOf("\"passthrough\"") >= 0) {
                    // Extract timeout value
                    int timeoutIdx = jsonBuffer.indexOf("\"timeout\":");
                    unsigned long timeout = 60;  // Default 60 minutes
                    if (timeoutIdx >= 0) {
                        String timeoutStr = jsonBuffer.substring(timeoutIdx + 10);
                        timeout = timeoutStr.toInt();
                        if (timeout < 1) timeout = 60;
                    }
                    Serial.printf("[SERIAL] Passthrough command received - %lu min\r\n", timeout);
                    enterPassthroughMode(timeout);
                    jsonBuffer = "";
                    return;  // Don't process as normal data
                }
                
                strcpy(dataBuffer, jsonBuffer.c_str());
                diagData = jsonBuffer;
                
                // Parse the data immediately instead of waiting for parseSDInterval
                parsedSerialData();
                
                jsonBuffer = "";  // Reset for next message
                break;
            }
            
            // Safety limit
            if (jsonBuffer.length() >= MAX_BUFFER_SIZE - 1) {
                Serial.println("ERROR: JSON buffer overflow, resetting");
                jsonBuffer = "";
            }
        }
    }
    
    // If no data received, increment counter
    if (jsonBuffer.length() == 0 && Serial1.available() == 0) {
        noSerialCount++;
        
        // Debug: Print if we haven't received data in a while
        if (noSerialCount % 20 == 0 && noSerialCount > 0) {
            Serial.print("⚠ No serial data received for ");
            Serial.print(noSerialCount * 5);  // readInterval is 5 seconds
            Serial.println(" seconds");
        }
    }
}

/**
 * Simple JSON parser for Arduino
 * Extracts key-value pairs from JSON string
 * Usage: getValue(dataBuffer, "press") returns the pressure value as String
 */
String getJsonValue(const char* json, const char* key) {
    String jsonStr = String(json);
    String searchKey = "\"" + String(key) + "\":";
    
    int keyPos = jsonStr.indexOf(searchKey);
    if (keyPos == -1) return "";
    
    int valueStart = keyPos + searchKey.length();
    
    // Skip whitespace
    while (valueStart < jsonStr.length() && jsonStr.charAt(valueStart) == ' ') {
        valueStart++;
    }
    
    // Check if value is a string (starts with ")
    bool isString = (jsonStr.charAt(valueStart) == '"');
    if (isString) valueStart++; // Skip opening quote
    
    // Find end of value
    int valueEnd = valueStart;
    if (isString) {
        // Find closing quote
        valueEnd = jsonStr.indexOf('"', valueStart);
    } else {
        // Find comma or closing brace
        while (valueEnd < jsonStr.length()) {
            char c = jsonStr.charAt(valueEnd);
            if (c == ',' || c == '}' || c == ' ') break;
            valueEnd++;
        }
    }
    
    if (valueEnd == -1 || valueEnd <= valueStart) return "";
    
    return jsonStr.substring(valueStart, valueEnd);
}

/**
 * Parse JSON serial data
 * Expected format: {"type":"data","gmid":"CSX-1234","press":-14.22,"mode":0,"current":0.07,"fault":0,"cycles":484}
 * Much simpler than the old CSV parser!
 */
void parsedSerialData() {
    // Check if we have valid JSON data (must have both braces)
    if (strlen(dataBuffer) < 10 || strstr(dataBuffer, "{") == NULL || strstr(dataBuffer, "}") == NULL) {
        // Only print error if we actually received something
        if (strlen(dataBuffer) > 0) {
            Serial.print("Invalid JSON (buffer has ");
            Serial.print(strlen(dataBuffer));
            Serial.print(" chars): ");
            Serial.println(dataBuffer);
        }
        return;
    }
    
    // Check message type
    String msgType = getJsonValue(dataBuffer, "type");
    if (msgType.length() == 0) {
        Serial.println("No 'type' field in JSON");
        return;
    }
    
    if (msgType != "data") {
        Serial.println("Unknown message type: " + msgType);
        return;
    }
    
    // Extract values from JSON
    String gmidStr = getJsonValue(dataBuffer, "gmid");
    String pressStr = getJsonValue(dataBuffer, "press");
    String modeStr = getJsonValue(dataBuffer, "mode");
    String currentStr = getJsonValue(dataBuffer, "current");
    String faultStr = getJsonValue(dataBuffer, "fault");
    String cyclesStr = getJsonValue(dataBuffer, "cycles");
    
    // Convert to appropriate types (all from Python except temperature)
    if (pressStr.length() > 0) pressure = pressStr.toFloat();
    if (modeStr.length() > 0) mode = modeStr.toInt();
    if (currentStr.length() > 0) current = currentStr.toFloat();
    if (faultStr.length() > 0) faults = faultStr.toInt();
    if (cyclesStr.length() > 0) cycles = cyclesStr.toInt();
    
    // Update device name if provided and different
    if (gmidStr.length() > 0 && gmidStr != String(deviceName)) {
        Serial.println("[Serial] Updating device name from '" + String(deviceName) + "' to '" + gmidStr + "'");
        DeviceName = gmidStr;
        strncpy(deviceName, gmidStr.c_str(), sizeof(deviceName) - 1);
        deviceName[sizeof(deviceName) - 1] = '\0';
        
        // Save to preferences/EPROM
        preferences.begin("RMS", false);
        preferences.putString("DeviceName", DeviceName);
        preferences.putString("APSuffix", DeviceName);  // Also update AP suffix
        preferences.end();
        Serial.println("✓ Device name saved to EPROM: " + DeviceName);
        
        // Update WiFi AP name with the new device ID
        updateAPName(DeviceName, true);  // Restart AP with new name
        apNameFromSerial = true;  // Mark as received from serial
        Serial.println("[Serial] WiFi AP name updated to: " + String(apSSID));
    }
    
    // Log received data
    Serial.println("✓ JSON parsed - Press:" + String(pressure) + " Cycles:" + String(cycles) + 
                   " Mode:" + String(mode) + " Current:" + String(current) + " GMID:" + gmidStr);
    
    // Save to SD if we have valid data
    if (noSerialCount <= 5) {
        pres_scaled = static_cast<int>(round(pressure * 100.0));
        seq++;
        Serial.println("Saving to SD");
        SaveToSD(deviceName, seq - 1, pres_scaled, cycles, faults, mode, temp_scaled, current);
    } else {
        NoSerial = 1;
    }
}

void buildSendDataArray(){
    Serial.println("Device Name");
    Serial.println(deviceName);
    String idStr = String(deviceName);
    
    // Extract numeric ID from device name (handles CSX-#### and RND-####)
    if (idStr.startsWith("CSX-")) {
        idStr = idStr.substring(4);
    } else if (idStr.startsWith("RND-")) {
        idStr = idStr.substring(4);
    }
    // If there are any other prefixes, try to find the last dash
    else if (idStr.indexOf("-") >= 0) {
        idStr = idStr.substring(idStr.lastIndexOf("-") + 1);
    }
    
    int id = idStr.toInt();
    Serial.print("ID String = ");
    Serial.print(idStr);
    Serial.print(" -> ID = ");
    Serial.println(id);
    Serial.println("***********************");

    readingCount++;
    
    // Calculate combined fault code: 1024 (watchdog) + 4096 (BlueCherry offline)
    // This is added to any existing faults from the Python device
    int systemFaultCode = getCombinedFaultCode();
    
    // Check if we're in watchdog state (no serial data for 30+ minutes)
    // If so, send zeroed data with fault codes to alert server
    if (isInWatchdogState()) {
        Serial.println("╔════════════════════════════════════════════════════════════╗");
        Serial.print("║  ⚠️ WATCHDOG STATE: Sending zeroed data with fault=");
        Serial.print(systemFaultCode);
        for (int i = String(systemFaultCode).length(); i < 5; i++) Serial.print(" ");
        Serial.println("  ║");
        Serial.println("╚════════════════════════════════════════════════════════════╝");
        
        // Zero out all values except ID
        readings[readingCount-1][0] = id;                          // Keep device ID
        readings[readingCount-1][1] = static_cast<int>(seq);       // Keep sequence number
        readings[readingCount-1][2] = 0;                           // pressure = 0
        readings[readingCount-1][3] = 0;                           // cycles = 0
        readings[readingCount-1][4] = systemFaultCode;             // fault = 1024 + 4096 if BC down
        readings[readingCount-1][5] = 0;                           // mode = 0
        readings[readingCount-1][6] = 0;                           // temp = 0
        readings[readingCount-1][7] = 0;                           // current = 0
        
        Serial.print("📤 Watchdog data packet: ID=");
        Serial.print(id);
        Serial.print(", seq=");
        Serial.print(seq);
        Serial.print(", fault=");
        Serial.print(systemFaultCode);
        if (!blueCherryConnected) Serial.print(" (includes 4096=BlueCherry offline)");
        Serial.println(" (all other values zeroed)");
        
    } else {
        // Normal operation - use live data from serial
        
        // Read ESP32's own temperature sensor
        float celsius = temperatureRead();
        float fahrenheit = (celsius * 9.0 / 5.0) + 32;
        temp_scaled = static_cast<int>(round(fahrenheit * 100.0));
        
        // Scale current from Python JSON
        int current_scaled = static_cast<int>(round(current * 100.0));
        
        Serial.print("Data summary - Temp(ESP32): ");
        Serial.print(fahrenheit);
        Serial.print("°F, Press(Python): ");
        Serial.print(pres_scaled / 100.0);
        Serial.print(", Cycles(Python): ");
        Serial.print(cycles);
        Serial.print(", Current(Python): ");
        Serial.println(current);
        
        // Add BlueCherry fault code (4096) to existing faults if BC is offline
        int totalFaults = faults + getBlueCherryFaultCode();
        
        readings[readingCount-1][0] = id;
        readings[readingCount-1][1] = static_cast<int>(seq);
        readings[readingCount-1][2] = pres_scaled;
        readings[readingCount-1][3] = cycles;
        readings[readingCount-1][4] = totalFaults;  // Device faults + 4096 if BC offline
        readings[readingCount-1][5] = mode;
        readings[readingCount-1][6] = temp_scaled;
        readings[readingCount-1][7] = current_scaled;
        
        // Log if BlueCherry fault code is active
        if (!blueCherryConnected) {
            Serial.print("📤 Data includes BlueCherry fault (4096): total fault=");
            Serial.println(totalFaults);
        }
    }
    
    // Increment sequence number
    seq++;
    
    if (readingCount >= 12) {
        Serial.print("=== Encoding ");
        Serial.print(readingCount);
        Serial.println(" readings as CBOR ===");
        
        if (isInWatchdogState()) {
            Serial.println("🚨 NOTE: This batch contains WATCHDOG DATA (fault includes 1024)");
        }
        if (!blueCherryConnected) {
            Serial.println("🚨 NOTE: Fault code includes 4096 (BlueCherry offline)");
        }
          
        size_t cborSize = buildCborFromReadings(cborBuffer, sizeof(cborBuffer), readings, readingCount);
        
        if (cborSize > 0) {
            Serial.print("CBOR encoded successfully, size: ");
            Serial.print(cborSize);
            Serial.println(" bytes");
                        
            if (sendCborArrayViaSocket(cborBuffer, cborSize)) {
                Serial.println("CBOR data sent successfully!");
            } else {
                Serial.println("Failed to send CBOR data");
            }
        } else {
            Serial.println("CBOR encoding failed");
        }
             
        readingCount = 0;
        Serial.println("=== Ready for next batch ===");
    }
} 

bool lteConnected() {
    WalterModemNetworkRegState regState = modem.getNetworkRegState();
    return (regState == WALTER_MODEM_NETWORK_REG_REGISTERED_HOME || regState == WALTER_MODEM_NETWORK_REG_REGISTERED_ROAMING);
}

bool lteConnect() {
    Serial.println("Starting LTE connection process...");
    
    if (!modem.setOpState(WALTER_MODEM_OPSTATE_MINIMUM)) {
        Serial.println("Failed to set operational state to minimum");
        return false;
    }
    delay(2000);
	
    Serial.printf("Defining PDP context with APN: %s\n", CELLULAR_APN);
    if (!modem.definePDPContext(1, CELLULAR_APN)) {
        Serial.println("Failed to define PDP context");
        Serial.println("Possible causes:");
        Serial.println("1. Incorrect APN for your cellular provider");
        Serial.println("2. Network not ready for data connection");
        Serial.println("3. SIM card issues");
        return false;
    }
    delay(1000);
    
    Serial.printf("Setting PDP authentication: Protocol=%d, User=%s\n", 
                  CELLULAR_AUTH_PROTOCOL, CELLULAR_USERNAME);
    if (!modem.setPDPAuthParams(CELLULAR_AUTH_PROTOCOL, CELLULAR_USERNAME, CELLULAR_PASSWORD)) {
        Serial.println("Failed to set PDP authentication parameters");
        Serial.println("Note: Some providers don't require authentication");
    }
    delay(1000);
    
    if (!modem.setOpState(WALTER_MODEM_OPSTATE_FULL)) {
        Serial.println("Failed to set operational state to full");
        return false;
    }
    delay(2000);
	
    if (!modem.setNetworkSelectionMode(WALTER_MODEM_NETWORK_SEL_MODE_AUTOMATIC)) {
        Serial.println("Failed to set network selection mode");
        return false;
    }
    delay(1000);
    
    Serial.println("Waiting for network registration...");
    unsigned short timeout = 300, i = 0;
    while (!lteConnected() && i < timeout) {
        i++;
        delay(1000);
        if (i % 10 == 0) {
            Serial.printf("Still waiting for network registration... (%d/%d)\n", i, timeout);
        }
    }
    
    if (i >= timeout) {
        Serial.println("Network registration timeout");
        return false;
    }
    
    Serial.println("Network registration successful");
    
    if (modem.getRAT(&rsp)) {
        Serial.printf("Connected to %s ", rsp.data.rat == WALTER_MODEM_RAT_NBIOT ? "NB-IoT" : "LTE-M");
    }
    
    if (modem.getCellInformation(WALTER_MODEM_SQNMONI_REPORTS_SERVING_CELL, &rsp)) {
        Serial.printf("on band %u using operator %s (%u%02u)\r\n", rsp.data.cellInformation.band,
                      rsp.data.cellInformation.netName, rsp.data.cellInformation.cc, rsp.data.cellInformation.nc);
        Serial.printf("Signal strength: RSRP: %.2f, RSRQ: %.2f\r\n", rsp.data.cellInformation.rsrp, rsp.data.cellInformation.rsrq);
        Modem_Status = "Modem OK ";
    }
    
    Serial.println("LTE connection established successfully");
    return true;
}

/**
 * Attempt to reconnect to BlueCherry platform
 * Called periodically when BlueCherry is not connected
 * Returns true if connection successful
 * 
 * Usage example:
 *   if (!blueCherryConnected && shouldRetryBlueCherry()) {
 *       retryBlueCherryConnection();
 *   }
 */
bool retryBlueCherryConnection() {
    Serial.println("\n🔄 Retrying BlueCherry connection...");
    blueCherryLastAttempt = millis();
    
    if (modem.blueCherryInit(BC_TLS_PROFILE, otaBuffer, &rsp)) {
        blueCherryConnected = true;
        Serial.println("✓ BlueCherry reconnected successfully!");
        return true;
    } else {
        Serial.print("BlueCherry still unavailable, state: ");
        Serial.println(rsp.data.blueCherry.state);
        return false;
    }
}

/**
 * Check if enough time has passed since system start to do weekly restart
 * Only restarts if BlueCherry has never connected (to retry OTA capability)
 * 
 * Usage: Call in main loop
 */
void checkWeeklyRestartForBlueCherry() {
    // Only restart if BlueCherry never connected during this session
    if (blueCherryConnected) {
        return;  // Already connected, no need for weekly restart
    }
    
    unsigned long uptime = millis() - systemStartTime;
    
    // Check for weekly restart (7 days without BlueCherry connection)
    if (uptime >= WEEKLY_RESTART_MS) {
        Serial.println("\n╔═══════════════════════════════════════════════════════╗");
        Serial.println("║  🔄 WEEKLY RESTART - BlueCherry Retry                 ║");
        Serial.println("║  System has been running 7 days without OTA platform  ║");
        Serial.println("║  Restarting to attempt fresh BlueCherry connection    ║");
        Serial.println("╚═══════════════════════════════════════════════════════╝\n");
        delay(2000);
        ESP.restart();
    }
}

/**
 * Check for firmware updates via BlueCherry platform
 * Only attempts sync if BlueCherry is connected
 * If not connected, attempts hourly reconnection
 */
void checkFirmwareUpdate() {
    // If BlueCherry not connected, try to reconnect periodically
    if (!blueCherryConnected) {
        unsigned long timeSinceLastAttempt = millis() - blueCherryLastAttempt;
        
        // Retry every hour
        if (timeSinceLastAttempt >= BC_RETRY_INTERVAL) {
            retryBlueCherryConnection();
        } else {
            // Calculate time until next retry
            unsigned long minutesUntilRetry = (BC_RETRY_INTERVAL - timeSinceLastAttempt) / 60000;
            Serial.print("BlueCherry offline - next retry in ");
            Serial.print(minutesUntilRetry);
            Serial.println(" minutes");
        }
        return;
    }
    
    // BlueCherry is connected - check for firmware updates AND incoming messages
    Serial.println("[BC Sync] Starting sync cycle...");
    do {
        if (!modem.blueCherrySync(&rsp)) {
            Serial.printf("[BC Sync] ERROR: sync failed, state=%d\r\n", rsp.data.blueCherry.state);
            // Mark as disconnected if sync fails - DO NOT reset modem (keeps LTE alive)
            if (rsp.data.blueCherry.state != 0) {
                blueCherryConnected = false;
                blueCherryLastAttempt = millis();  // Reset retry timer
                Serial.println("\n╔═══════════════════════════════════════════════════════╗");
                Serial.println("║  ⚠️ BlueCherry sync failed - platform may be down     ║");
                Serial.println("║  System continues running (fault code 4096 active)    ║");
                Serial.println("║  Will retry hourly, weekly restart if still offline   ║");
                Serial.println("╚═══════════════════════════════════════════════════════╝");
            }
            return;
        }
        
        // Debug: Show sync results
        Serial.printf("[BC Sync] OK - messageCount=%d, syncFinished=%s\r\n", 
            rsp.data.blueCherry.messageCount,
            rsp.data.blueCherry.syncFinished ? "true" : "false");
        
        // Process incoming messages from BlueCherry platform
        // Topic 0 = firmware update request, Topic != 0 = other MQTT messages
        for (uint8_t msgIdx = 0; msgIdx < rsp.data.blueCherry.messageCount; msgIdx++) {
            if (rsp.data.blueCherry.messages[msgIdx].topic == 0) {
                // Topic 0 = Firmware update available
                Serial.println("\n📥 Firmware update available - downloading...");
            } else {
                // Non-firmware message - extract payload and check for commands
                String payload = "";
                for (uint8_t byteIdx = 0; byteIdx < rsp.data.blueCherry.messages[msgIdx].dataSize; byteIdx++) {
                    payload += (char)rsp.data.blueCherry.messages[msgIdx].data[byteIdx];
                }
                
                // Print message to Serial Monitor
                Serial.println("\n╔═══════════════════════════════════════════════════════╗");
                Serial.println("║  📨 Incoming BlueCherry Message                       ║");
                Serial.println("╠═══════════════════════════════════════════════════════╣");
                Serial.printf("║  Message %d/%d\r\n", msgIdx + 1, rsp.data.blueCherry.messageCount);
                Serial.printf("║  Topic ID: 0x%02X\r\n", rsp.data.blueCherry.messages[msgIdx].topic);
                Serial.printf("║  Data size: %d bytes\r\n", rsp.data.blueCherry.messages[msgIdx].dataSize);
                Serial.println("║  Payload: " + payload);
                Serial.println("╚═══════════════════════════════════════════════════════╝");
                
                // Convert payload to lowercase for case-insensitive comparison
                String payloadLower = payload;
                payloadLower.toLowerCase();
                
                // Debug: show what we're checking
                Serial.printf("[BC Cmd] Checking payload: '%s' (len=%d)\r\n", payloadLower.c_str(), payloadLower.length());
                
                // Check for "remote" command - enter passthrough mode
                // Format: "remote" (default 60 min) or "remote XX" (XX minutes)
                int remoteIdx = payloadLower.indexOf("remote");
                Serial.printf("[BC Cmd] indexOf('remote') = %d\r\n", remoteIdx);
                if (remoteIdx >= 0) {
                    // Parse timeout from message (e.g., "remote 30" → 30 minutes)
                    unsigned long timeoutMinutes = 60;  // Default 60 minutes
                    
                    // Look for a number after "remote"
                    int numStart = remoteIdx + 6;  // Length of "remote"
                    while (numStart < (int)payload.length() && (payload[numStart] == ' ' || payload[numStart] == '\t')) {
                        numStart++;  // Skip whitespace
                    }
                    
                    // Extract number if present
                    String numStr = "";
                    while (numStart < (int)payload.length() && isDigit(payload[numStart])) {
                        numStr += payload[numStart];
                        numStart++;
                    }
                    
                    if (numStr.length() > 0) {
                        timeoutMinutes = numStr.toInt();
                        if (timeoutMinutes < 1) timeoutMinutes = 1;      // Minimum 1 minute
                        if (timeoutMinutes > 1440) timeoutMinutes = 1440; // Maximum 24 hours
                    }
                    
                    Serial.printf("\n🔄 BlueCherry REMOTE command - passthrough for %lu minutes\r\n", timeoutMinutes);
                    enterPassthroughMode(timeoutMinutes);
                    return;  // Exit immediately - don't continue with modem operations
                }
                
                // Check for "restart" command - perform ESP restart
                int restartIdx = payloadLower.indexOf("restart");
                Serial.printf("[BC Cmd] indexOf('restart') = %d\r\n", restartIdx);
                if (restartIdx >= 0) {
                    Serial.println("\n🔄 BlueCherry RESTART command received - restarting ESP32...");
                    Serial.println("Restarting in 3 seconds...");
                    delay(3000);
                    ESP.restart();
                }
            }
        }
    } while (!rsp.data.blueCherry.syncFinished);
    
    if (rsp.data.blueCherry.messageCount > 0) {
        Serial.println("✓ BlueCherry sync complete - messages processed");
    }
}

size_t buildCborFromReadings(uint8_t* buf, size_t bufSize, int data[][10], size_t rowCount) {
  CborEncoder encoder, arrayEncoder;
  cbor_encoder_init(&encoder, buf, bufSize, 0);

  cbor_encoder_create_array(&encoder, &arrayEncoder, rowCount);

  for (size_t i = 0; i < rowCount; i++) {
    CborEncoder rowEnc;
    cbor_encoder_create_array(&arrayEncoder, &rowEnc, 8);
    for (int j = 0; j < 8; j++) {
      cbor_encode_int(&rowEnc, data[i][j]);
    }
    cbor_encoder_close_container(&arrayEncoder, &rowEnc);
  }

  cbor_encoder_close_container(&encoder, &arrayEncoder);
  size_t len = cbor_encoder_get_buffer_size(&encoder, buf);
  return len;
}

void buildStatusUpdate() {
    if (modem.getCellInformation(WALTER_MODEM_SQNMONI_REPORTS_SERVING_CELL, &rsp)) {
        modemBand = rsp.data.cellInformation.band;
        modemNetName = rsp.data.cellInformation.netName;
        modemRSRP = rsp.data.cellInformation.rsrp;
        modemRSRQ = rsp.data.cellInformation.rsrq;
    } else {
        modemBand = modemNetName = modemRSRP = modemRSRQ = "Unknown";
    }
}

size_t buildErrorCbor(uint8_t* cborBuf, size_t bufSize, int errorCode) {
    CborEncoder encoder, arrayEncoder;
    cbor_encoder_init(&encoder, cborBuf, bufSize, 0);
    String idStr = String(deviceName);
    
    // Extract numeric ID (handles CSX-####, RND-####, etc.)
    if (idStr.startsWith("CSX-") || idStr.startsWith("RND-")) {
        idStr = idStr.substring(4);
    } else if (idStr.indexOf("-") >= 0) {
        idStr = idStr.substring(idStr.lastIndexOf("-") + 1);
    }
    
    int id = idStr.toInt();
    cbor_encoder_create_array(&encoder, &arrayEncoder, 2);
    cbor_encode_int(&arrayEncoder, id);
    cbor_encode_int(&arrayEncoder, errorCode);
    cbor_encoder_close_container(&encoder, &arrayEncoder);
    Serial.println("Built error CBOR message");
    return cbor_encoder_get_buffer_size(&encoder, cborBuf);
}

size_t buildStatusCbor(uint8_t* cborBuf, size_t bufSize) {
    CborEncoder encoder, arrayEncoder;
    cbor_encoder_init(&encoder, cborBuf, bufSize, 0);
    String idStr = String(deviceName);
    
    // Extract numeric ID (handles CSX-####, RND-####, etc.)
    if (idStr.startsWith("CSX-") || idStr.startsWith("RND-")) {
        idStr = idStr.substring(4);
    } else if (idStr.indexOf("-") >= 0) {
        idStr = idStr.substring(idStr.lastIndexOf("-") + 1);
    }
    
    int id = idStr.toInt();
    int band = modemBand.toInt();
    cbor_encoder_create_array(&encoder, &arrayEncoder, 9);
    cbor_encode_int(&arrayEncoder, id);
    cbor_encode_int(&arrayEncoder, band);
    cbor_encode_text_stringz(&arrayEncoder, modemNetName.c_str());
    cbor_encode_text_stringz(&arrayEncoder, modemRSRP.c_str());
    cbor_encode_text_stringz(&arrayEncoder, modemRSRQ.c_str());
    cbor_encode_text_stringz(&arrayEncoder, ver.c_str());
    cbor_encode_int(&arrayEncoder, 0);  // PlcVer removed (always 0 for JSON mode)
    cbor_encode_text_stringz(&arrayEncoder, macStr.c_str());
    cbor_encode_text_stringz(&arrayEncoder, imei.c_str());
    cbor_encoder_close_container(&encoder, &arrayEncoder);
    return cbor_encoder_get_buffer_size(&encoder, cborBuf);
}

bool sendCborArrayViaSocket(uint8_t* buffer, size_t size) {
    int retries = 3;
    int socketId = -1;
    
    Serial.print("Attempting to send CBOR data via UDP socket, size: ");
    Serial.print(size);
    Serial.println(" bytes");
    
    while (retries > 0) {
        Serial.print("UDP socket send attempt ");
        Serial.print(4 - retries);
        Serial.println("/3");
        
        if (!modem.socketConfig(&rsp, NULL, NULL, 1, 1024, 60, 30, 5000)) {
            Serial.println("Failed to configure UDP socket");
            retries--;
            continue;
        }
        socketId = rsp.data.socketId;
        Serial.print("Configured socket ID: ");
        Serial.println(socketId);

        if(!modem.socketConfigSecure(false, 1, socketId)) {
            Serial.println("Failed to disable TLS on the UDP socket");
            retries--;
            continue;
        }
        
        if (!modem.socketConfigExtended(&rsp, NULL, NULL, socketId)) {
            Serial.println("Failed to configure UDP socket extended parameters");
            retries--;
            continue;
        }
        Serial.println("Socket extended parameters configured successfully");
        
        if (!modem.socketDial("167.172.15.241", 5689, 0, &rsp, NULL, NULL, 
                                WALTER_MODEM_SOCKET_PROTO_UDP, 
                                WALTER_MODEM_ACCEPT_ANY_REMOTE_DISABLED, socketId)) {
            Serial.println("Failed to connect UDP socket to server");
            modem.socketClose(&rsp, NULL, NULL, socketId);
            retries--;
            continue;
        }
        Serial.println("Socket connected to server");
        
        if (!modem.socketSend(buffer, size, &rsp, NULL, NULL, 
                             WALTER_MODEM_RAI_ONLY_SINGLE_RXTX_EXPECTED, socketId)) {
            Serial.println("Failed to send data via UDP socket");
            modem.socketClose(&rsp, NULL, NULL, socketId);
            retries--;
            continue;
        }
        Serial.println("CBOR data sent successfully via UDP socket!");
        
        modem.socketClose(&rsp, NULL, NULL, socketId);
        lastSendTime = currentTime;
        return true;
        
        retries--;
        if (retries > 0) {
            Serial.print("Retrying in 1 second... (");
            Serial.print(retries);
            Serial.println(" attempts remaining)");
            delay(1000);
        }
    }
    Serial.println("All UDP socket send attempts failed");
    return false;
}

bool sendCborDataViaSocket(uint8_t* buffer, size_t size) {
    int socketId = -1;
    
    Serial.print("Sending status/error data via UDP socket, size: ");
    Serial.print(size);
    Serial.println(" bytes");
    
    if (!modem.socketConfig(&rsp, NULL, NULL, 1, 1024, 60, 30, 5000)) {
        Serial.println("Failed to configure UDP socket for status/error");
        return false;
    }
    socketId = rsp.data.socketId;

    if(!modem.socketConfigSecure(false, 1, socketId)) {
        Serial.println("Failed to disable TLS on the UDP socket for status/error");
        return false;
    }
    
    if (!modem.socketConfigExtended(&rsp, NULL, NULL, socketId)) {
        Serial.println("Failed to configure UDP socket extended parameters for status/error");
        return false;
    }
    
    if (!modem.socketDial("167.172.15.241", 5686, 0, &rsp, NULL, NULL, 
                            WALTER_MODEM_SOCKET_PROTO_UDP, 
                            WALTER_MODEM_ACCEPT_ANY_REMOTE_DISABLED, socketId)) {
        Serial.println("Failed to connect UDP socket to status/error server");
        modem.socketClose(&rsp, NULL, NULL, socketId);
        return false;
    }
    
    if (!modem.socketSend(buffer, size, &rsp, NULL, NULL, 
                         WALTER_MODEM_RAI_ONLY_SINGLE_RXTX_EXPECTED, socketId)) {
        Serial.println("Failed to send status/error data via UDP socket");
        modem.socketClose(&rsp, NULL, NULL, socketId);
        return false;
    }
    
    modem.socketClose(&rsp, NULL, NULL, socketId);
    lastSendTime = currentTime;
    buffer[0] = '\0';
    Serial.println("Status/error data sent successfully via UDP socket!");
    return true;
}

bool sendErrorMessageViaSocket() {
    size_t cborSize = buildErrorCbor(cborBuffer, sizeof(cborBuffer), 1);
    if (cborSize == 0) {
        Serial.println("Failed to build error CBOR message");
        return false;
    }
    return sendCborDataViaSocket(cborBuffer, cborSize);
}

bool sendStatusUpdateViaSocket() {
    buildStatusUpdate();
    size_t cborSize = buildStatusCbor(cborBuffer, sizeof(cborBuffer));
    if (cborSize == 0) {
        Serial.println("Failed to build status CBOR message");
        return false;
    }
    return sendCborDataViaSocket(cborBuffer, cborSize);
}

// =====================================================================
// PASSTHROUGH BOOT MODE
// =====================================================================
// Minimal boot mode for serial passthrough - runs INSTEAD of normal firmware
// when passthrough preference is set. WalterModem library is NEVER loaded.
// Only runs: 3.3V power, MCP emulator, and serial passthrough bridge.
// 
// Early Exit: Linux can set MCP GPA5 (bit 5) via I2C to trigger early exit.
// Timer: Auto-restarts after timeout (stored in preferences, default 60 min).
// =====================================================================

#define PASSTHROUGH_EXIT_BIT 0x20  // GPA5 (bit 5) - early exit trigger

// Escape sequence from Linux to stop passthrough: "+++STOPPPP\n"
const char PASSTHROUGH_STOP_SEQ[] = "+++STOPPPP";
const int PASSTHROUGH_STOP_SEQ_LEN = 10;

void runPassthroughBootMode(unsigned long timeoutMinutes) {
    Serial.printf("\n[PASSTHROUGH] Boot mode active - timeout %lu min\r\n", timeoutMinutes);
    Serial.println("[PASSTHROUGH] Exit: MCP GPA5 via I2C, or send +++STOPPPP");
    
    // CRITICAL: Pre-initialize overfill state to SAFE before anything else
    // This ensures no false alarms even if I2C is queried early
    gpioB_value = 0x00;                    // NO overfill alarm
    overfillAlarmActive = false;           // Alarm flag OFF
    overfillValidationComplete = false;    // Force lockout period
    overfillStartupTime = 0;
    overfillLowCount = 0;
    overfillHighCount = 0;
    overfillCheckTimer = 0;
    
    // Pre-initialize DISP_SHUTDN protection state
    gpioA_value = 0x10;                    // V6 ON
    dispShutdownProtectionActive = true;
    dispShutdownStartupTime = 0;
    
    // Initialize relay pins to safe states (same as normal mode)
    pinMode(CR0_MOTOR, OUTPUT);
    digitalWrite(CR0_MOTOR, LOW);     // Motor OFF
    pinMode(CR1, OUTPUT);
    digitalWrite(CR1, LOW);           // Relay 1 OFF
    pinMode(CR2, OUTPUT);
    digitalWrite(CR2, LOW);           // Relay 2 OFF
    pinMode(CR5, OUTPUT);
    digitalWrite(CR5, LOW);           // Relay 5 OFF
    pinMode(DISP_SHUTDN, OUTPUT);
    digitalWrite(DISP_SHUTDN, HIGH);  // CRITICAL: DISP_SHUTDN ON (safety)
    
    // Create mutex for thread-safe relay control
    relayMutex = xSemaphoreCreateMutex();
    if (relayMutex == NULL) {
        Serial.println("[PASSTHROUGH] ERROR: Failed to create mutex!");
        ESP.restart();
    }
    
    // Start MCP emulator on Core 0 (same as normal mode)
    xTaskCreatePinnedToCore(
        mcpEmulatorTask,
        "MCP_Emulator",
        8192,
        NULL,
        1,
        NULL,
        0
    );
    Serial.println("[PASSTHROUGH] MCP23017 emulator started");
    
    // Configure modem pins directly (matching walter-as-linux-modem.txt)
    pinMode(WALTER_MODEM_PIN_RX, INPUT);
    pinMode(WALTER_MODEM_PIN_TX, OUTPUT);
    pinMode(WALTER_MODEM_PIN_CTS, INPUT);
    pinMode(WALTER_MODEM_PIN_RTS, OUTPUT);
    
    // Initialize ModemSerial directly (NO WalterModem library)
    ModemSerial.begin(
        115200,
        SERIAL_8N1,
        WALTER_MODEM_PIN_RX,
        WALTER_MODEM_PIN_TX,
        false,
        WALTER_MODEM_PIN_RTS,
        WALTER_MODEM_PIN_CTS
    );
    
    // Initialize Serial1 for host (Linux device)
    Serial1.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
    
    Serial.println("[PASSTHROUGH] Bridge ready - Serial1 <-> ModemSerial");
    
    // Calculate timeout
    unsigned long timeoutMs = timeoutMinutes * 60UL * 1000UL;
    unsigned long startTime = millis();
    unsigned long lastStatusTime = 0;
    
    // Ring buffer to detect escape sequence from host
    char escapeBuffer[PASSTHROUGH_STOP_SEQ_LEN + 1] = {0};
    int escapeIdx = 0;
    
    // Main passthrough loop (runs until timeout or early exit)
    while (true) {
        // Check for timeout
        if (millis() - startTime >= timeoutMs) {
            Serial.println("[PASSTHROUGH] Timeout - restarting...");
            delay(500);
            ESP.restart();
        }
        
        // Check for early exit (GPA5 set via I2C)
        if (gpioA_value & PASSTHROUGH_EXIT_BIT) {
            Serial.println("[PASSTHROUGH] Early exit (GPA5) - restarting...");
            delay(500);
            ESP.restart();
        }
        
        // Forward data from host (Serial1) to modem (ModemSerial)
        // Also watch for escape sequence "+++STOPPPP"
        if (Serial1.available()) {
            int x = Serial1.read();
            
            // Check for escape sequence
            if (x == PASSTHROUGH_STOP_SEQ[escapeIdx]) {
                escapeIdx++;
                if (escapeIdx >= PASSTHROUGH_STOP_SEQ_LEN) {
                    // Escape sequence detected - restart to normal mode
                    Serial.println("[PASSTHROUGH] Stop signal received - restarting...");
                    delay(500);
                    ESP.restart();
                }
                // Don't forward escape sequence characters to modem
            } else {
                // Not part of escape sequence - forward any buffered chars and this one
                for (int i = 0; i < escapeIdx; i++) {
                    ModemSerial.write(PASSTHROUGH_STOP_SEQ[i]);
                }
                escapeIdx = 0;
                
                // Check if this char starts a new escape sequence
                if (x == PASSTHROUGH_STOP_SEQ[0]) {
                    escapeIdx = 1;
                } else {
                    ModemSerial.write(x);
                }
            }
        }
        
        // Forward data from modem (ModemSerial) to host (Serial1)
        if (ModemSerial.available()) {
            int x = ModemSerial.read();
            Serial1.write(x);
        }
        
        // Periodic status (every 5 minutes - less frequent)
        if (millis() - lastStatusTime >= 300000) {
            unsigned long remaining = (timeoutMs - (millis() - startTime)) / 60000;
            Serial.printf("[PASSTHROUGH] %lu min remaining\r\n", remaining);
            lastStatusTime = millis();
        }
        
        yield();  // Allow other tasks to run
    }
}

// =====================================================================
// SETUP
// =====================================================================
void setup() {
    // =====================================================================
    // CRITICAL: Enable 3.3V power FIRST (before anything else)
    // =====================================================================
    pinMode(ESP_POWER_EN, OUTPUT);
    digitalWrite(ESP_POWER_EN, LOW);
    
    Serial.begin(115200);
    delay(100);  // Brief delay for serial to stabilize
    
    // Check for passthrough boot mode (before loading WalterModem library)
    preferences.begin("RMS", false);
    bool passthroughBoot = preferences.getBool("ptBoot", false);
    unsigned long ptTimeout = preferences.getULong("ptTimeout", 60);
    
    if (passthroughBoot) {
        // Clear preference IMMEDIATELY so next boot is normal
        preferences.putBool("ptBoot", false);
        preferences.end();
        
        // Run passthrough mode (never returns - ends with ESP.restart)
        runPassthroughBootMode(ptTimeout);
    }
    preferences.end();
    
    // =====================================================================
    // CRITICAL: Pre-initialize MCP state to SAFE values before task starts
    // This prevents any race conditions if I2C is queried early
    // =====================================================================
    gpioB_value = 0x00;                    // NO overfill alarm
    overfillAlarmActive = false;           // Alarm flag OFF
    overfillValidationComplete = false;    // Force 15-second lockout period
    overfillStartupTime = 0;
    overfillLowCount = 0;
    overfillHighCount = 0;
    overfillCheckTimer = 0;
    gpioA_value = 0x10;                    // V6/DISP_SHUTDN ON
    dispShutdownProtectionActive = true;   // Force 20-second lockout period
    dispShutdownStartupTime = 0;
    
    // =====================================================================
    // CRITICAL: Create mutex and start MCP emulator EARLY
    // =====================================================================
    relayMutex = xSemaphoreCreateMutex();
    if (relayMutex == NULL) {
        Serial.println("Failed to create relay mutex!");
        ESP.restart();
    }
    Serial.println("✓ Mutex created for thread-safe relay control");
    
    // Start MCP emulator as FreeRTOS task on Core 0 (HIGH PRIORITY)
    xTaskCreatePinnedToCore(
        mcpEmulatorTask,
        "MCP_Emulator",
        8192,
        NULL,
        1,
        NULL,
        0
    );
    Serial.println("✓ MCP23017 emulator started on Core 0 (EARLY START)");
    
    // Now continue with rest of setup
    delay(500);
    
    Serial.println("\n╔═══════════════════════════════════════════════════════╗");
    Serial.println("║  Walter IO Board Firmware - Rev 9.4                  ║");
    Serial.println("║  MCP23017 Emulation + Diagnostic Dashboard           ║");
    Serial.println("╚═══════════════════════════════════════════════════════╝\n");
    
    // Initialize watchdog pin
    pinMode(ESP_WATCHDOG_PIN, OUTPUT);
    digitalWrite(ESP_WATCHDOG_PIN, LOW);
    Serial.println("✓ Serial watchdog pin (GPIO39) initialized");
    
    delay(500);
    
    // Manage SD Card mounting (with updated CS pin)
    initSdLogging(); 

    // Get device name and watchdog setting from EPROM
    // Send frequency is now controlled by Python (15 seconds), not stored here
    preferences.begin("RMS", false);
    DeviceName = preferences.getString("DeviceName", "CSX-9000");
    watchdogEnabled = preferences.getBool("watchdog", false);  // Default to disabled
    preferences.end();
    
    Serial.print("✓ Loaded watchdog setting from EPROM: ");
    Serial.println(watchdogEnabled ? "ENABLED" : "DISABLED");
    
    // Fixed send interval (Python sends every 15 seconds)
    sendInterval = 15000;  // 15 seconds in milliseconds
    
    strncpy(deviceName, DeviceName.c_str(), sizeof(deviceName) - 1);
    deviceName[sizeof(deviceName) - 1] = '\0';
    
    // Extract numeric ID from device name (handles CSX-####, RND-####, etc.)
    if (DeviceName.startsWith("CSX-") || DeviceName.startsWith("RND-")) {
        idStr = DeviceName.substring(4);
    } else if (DeviceName.indexOf("-") >= 0) {
        idStr = DeviceName.substring(DeviceName.lastIndexOf("-") + 1);
    }
    
    int id = idStr.toInt();
    
    Serial.println("Starting WiFi Access Point...");
    initializeAPName();  // Generate/load AP name before starting AP
    startConfigAP();
    Serial.print("WiFi AP '");
    Serial.print(apSSID);
    Serial.println("' ready");
    Serial.println("Connect and navigate to 192.168.4.1");
    
    Serial.println("#########################################");
    Serial.println("##   Walter IO Board Firmware " + ver + "   ##");
    Serial.println("#########################################");
    
    // Start SPI for SD Card (updated CS pin)
    SPI.begin(SCLK, MISO, MOSI, CS);
    if (!SD.begin(CS)) Serial.println("**** ERROR - SD Card not Mounted");
    else Serial.println("**** SD Card Mounted (CS=40)");
    
    // Initialize RS-232 Serial data
    Serial1.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);

    if (!modem.begin(&ModemSerial)) {
        Serial.println("Could not initialize the modem, restarting in 10 seconds");
        Modem_Status = "Could Not Initialize Modem";
        delay(10000);
        ESP.restart();
    }
    
    if (modem.getRAT(&rsp) && rsp.data.rat == WALTER_MODEM_RAT_NBIOT) {
        if (modem.setRAT(WALTER_MODEM_RAT_LTEM)) {
            Serial.println("Switched modem to LTE-M mode");
            modem.reset();
        }
    }
    
    if (!lteConnect()) {
        Serial.println("Unable to connect to cellular network, restarting in 10 seconds");
        Modem_Status = "Unable to connect to cellular network";
        delay(10000);
        ESP.restart();
    }
    
    // BlueCherry initialization - NON-FATAL (system continues if platform is down)
    // BlueCherry is used for OTA firmware updates - core IO functions work without it
    // If BlueCherry fails, system will retry hourly and do a weekly restart to retry
    Serial.println("\nInitializing BlueCherry cloud connection...");
    Serial.println("(BlueCherry is for OTA updates - system will continue if unavailable)");
    
    systemStartTime = millis();  // Record start time for weekly restart tracking
    blueCherryLastAttempt = millis();
    blueCherryConnected = false;
    
    unsigned short attempt = 0;
    const unsigned short maxAttempts = 3;  // Try 3 times before giving up
    
    while (!modem.blueCherryInit(BC_TLS_PROFILE, otaBuffer, &rsp) && attempt < maxAttempts) {
        Serial.print("BlueCherry attempt ");
        Serial.print(attempt + 1);
        Serial.print("/");
        Serial.print(maxAttempts);
        Serial.print(", state: ");
        Serial.println(rsp.data.blueCherry.state);
        
        if (rsp.data.blueCherry.state == WALTER_MODEM_BLUECHERRY_STATUS_NOT_PROVISIONED && attempt <= 2) {
            Serial.println("Device not provisioned, attempting ZTP...");
            if (attempt++ == 0) {
                if (!BlueCherryZTP::begin(BC_DEVICE_TYPE, BC_TLS_PROFILE, bc_ca_cert, &modem)) continue;
                // Read MAC address HERE - inside ZTP block, exactly as original code
                uint8_t mac[8] = {0};
                esp_read_mac(mac, ESP_MAC_WIFI_STA);
                BlueCherryZTP::addDeviceIdParameter(BLUECHERRY_ZTP_DEVICE_ID_TYPE_MAC, mac);
                if (modem.getIdentity(&rsp)) {
                    BlueCherryZTP::addDeviceIdParameter(BLUECHERRY_ZTP_DEVICE_ID_TYPE_IMEI, rsp.data.identity.imei);
                }
            }
            if (!BlueCherryZTP::requestDeviceId() || !BlueCherryZTP::generateKeyAndCsr() || !BlueCherryZTP::requestSignedCertificate() ||
                !modem.blueCherryProvision(BlueCherryZTP::getCert(), BlueCherryZTP::getPrivKey(), bc_ca_cert)) continue;
        } else {
            attempt++;
            if (attempt < maxAttempts) {
                Serial.println("BlueCherry init failed, retrying in 5 seconds...");
                delay(5000);
            }
        }
    }
    
    // Check if BlueCherry connected successfully
    if (modem.blueCherryInit(BC_TLS_PROFILE, otaBuffer, &rsp)) {
        blueCherryConnected = true;
        Serial.println("✓ BlueCherry initialized successfully");
    } else {
        blueCherryConnected = false;
        Serial.println("\n╔═══════════════════════════════════════════════════════╗");
        Serial.println("║  ⚠️ BlueCherry platform unavailable                   ║");
        Serial.println("║  System will continue without OTA update capability   ║");
        Serial.println("║  Will retry hourly, weekly restart if still offline   ║");
        Serial.println("╚═══════════════════════════════════════════════════════╝\n");
    }
    
    if (modem.getIdentity(&rsp)) {
        imei = rsp.data.identity.imei;
        imeisv = rsp.data.identity.imeisv;
        svn = rsp.data.identity.svn;
    }
    
    // Read MAC address AFTER BlueCherry init (matching original code structure)
    // This is for display and for macStr global variable used elsewhere
    uint8_t mac[8] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char buffer[18];
    snprintf(buffer, 18, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    macStr = buffer;
    
    // Display MAC address prominently in Serial Monitor
    Serial.println("\n╔═══════════════════════════════════════════════════════╗");
    Serial.println("║              DEVICE IDENTIFICATION                    ║");
    Serial.println("╠═══════════════════════════════════════════════════════╣");
    Serial.print("║  MAC:  ");
    Serial.print(macStr);
    for (int i = macStr.length(); i < 47; i++) Serial.print(" ");
    Serial.println("║");
    Serial.print("║  IMEI: ");
    Serial.print(imei);
    for (int i = imei.length(); i < 47; i++) Serial.print(" ");
    Serial.println("║");
    Serial.println("╚═══════════════════════════════════════════════════════╝\n");
    
    if (macStr == "00:00:00:00:00:00" || macStr.length() == 0) {
        Serial.println("⚠️ WARNING: MAC address is blank or invalid!");
    }
    
    initModemTime();
    delay(1000);
    
    // Initialize watchdog timer
    resetSerialWatchdog();
    Serial.println("✓ Serial watchdog timer initialized");
    
    Serial.println("\n✅ Walter IO Board Firmware Rev 9.4 initialization complete!");
    Serial.println("✅ MCP I2C slave running on Core 0 (address 0x20)");
    Serial.println("✅ AJAX web interface active (no refresh flashing)");
    Serial.println("✅ Serial watchdog ready");
    if (blueCherryConnected) {
        Serial.println("✅ BlueCherry OTA platform connected");
    } else {
        Serial.println("⚠️ BlueCherry OTA offline - will retry hourly, weekly restart scheduled");
    }
    Serial.println("✅ System ready!\n");
}

// =====================================================================
// MAIN LOOP
// =====================================================================
void loop() {
    // Debug: confirm loop is running
    static unsigned long lastLoopDebug = 0;
    static bool firstLoop = true;
    if (firstLoop || (passthroughMode && millis() - lastLoopDebug > 2000)) {
        Serial.printf("[LOOP] Running, passthroughMode=%d\r\n", passthroughMode);
        lastLoopDebug = millis();
        firstLoop = false;
    }
    
    // =====================================================================
    // PASSTHROUGH MODE - Bridge Serial1 (RS-232) to modem
    // When active, normal firmware operations are suspended (INCLUDING LTE check)
    // I2C emulator continues running on Core 0
    // Web server continues running (handled by AsyncWebServer)
    // MUST be checked FIRST - before any modem operations
    // =====================================================================
    if (passthroughMode) {
        runPassthroughLoop();
        delay(1);  // Small delay to prevent tight loop
        return;    // Skip ALL normal operations including LTE check
    }
    
    // LTE connection check - only runs in normal mode (not passthrough)
    if (!lteConnected() && !lteConnect()) {
        Serial.println("Unable to connect to cellular network, restarting in 10 seconds");
        delay(10000);
        ESP.restart();
    }
    
    // =====================================================================
    // NORMAL OPERATION MODE
    // =====================================================================
    currentTime = millis();
    dataBuf[6] = counter >> 8;
    dataBuf[7] = counter & 0xFF;
    
    if (currentTime - readTime >= readInterval) {
        updateTimestamp();
        readSerialData();
        checkSerialWatchdog();  // Check if watchdog timeout has been exceeded
        readTime = currentTime;
    }
    
    if (currentTime - lastParseTime >= parseSDInterval) {
        Serial.print("*** Parse/SD write interval *****");
        parsedSerialData();
        lastParseTime = currentTime;
    }
    
    if (currentTime - lastSendTime >= sendInterval) {
        buildSendDataArray();
        Serial.print("*** Reading Count= ");
        Serial.println(readingCount);
        lastSendTime = currentTime;
    }

    if ((currentTime - lastSendTime >= noComInterval && NoSerial == 1) || (firstLostComm == 1 && NoSerial == 1)) {
        Serial.print("#################################\n############### Sending Serial Error Message#################\n####################################");
        sendErrorMessageViaSocket();
        firstLostComm = 0;
    }
    
    if (currentTime - lastFirmwareTime >= lastFirmwareFreq || firstTime) {
        Serial.println("%%%%%%%%%%%% Firmware check - init Modem time %%%%%%%%%%%%%%%%%%%%%%%%%%%");
        checkFirmwareUpdate();
        initModemTime();
        lastFirmwareTime = currentTime;
        firstTime = false;
        
        // Check if weekly restart is needed (only if BlueCherry never connected)
        checkWeeklyRestartForBlueCherry();
    }
    
    if (currentTime - lastSDCheck >= 60000) {
        if (!SD.cardType()) {
            Serial.println("SD card not mounted, attempting to remount");
            int attempts = 0;
            const int maxAttempts = 3;
            while (attempts < maxAttempts) {
                if (SD.begin(CS)) {
                    Serial.println("SD card remounted successfully on attempt " + String(attempts + 1));
                    time_t rawTime = (time_t)currentTimestamp;
                    struct tm *timeInfo = gmtime(&rawTime);
                    int currentYear = timeInfo->tm_year + 1900;
                    String logFileName = "/logfile" + String(currentYear) + ".log";
                    logFile = SD.open(logFileName.c_str(), FILE_APPEND);
                    if (!logFile) {
                        Serial.print(F("Error: could not open logfile after remount "));
                        Serial.print(logFileName);
                        Serial.println(F(" for appending"));
                    } else {
                        Serial.println("Log file opened after remount: " + logFileName);
                    }
                    break;
                }
                attempts++;
                Serial.println("Failed to remount SD card, attempt " + String(attempts) + " of " + String(maxAttempts));
                delay(1000);
            }
            if (attempts >= maxAttempts) {
                Serial.println("Failed to remount SD card after " + String(maxAttempts) + " attempts");
            }
        }
        lastSDCheck = currentTime;
    }
    
    // Send status JSON to Python device every 15 seconds via RS-232
    checkAndSendStatusToSerial();
    
    delay(100);
}
