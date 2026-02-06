# MCP23017 Software Emulation Compatibility Guide

## Overview

The Walter IO Board Firmware (Rev 9.1) includes a software emulation of the Microchip MCP23017 16-bit I/O Expander. This document describes the emulation's compatibility with the physical chip, limitations, and recommendations for client software.

**Reference:** [Microchip MCP23017 Datasheet (DS20001952C)](https://ww1.microchip.com/downloads/en/devicedoc/20001952c.pdf)

---

## I2C Configuration

| Parameter | Value | Notes |
|-----------|-------|-------|
| I2C Address | **0x20** | Fixed (A0=A1=A2=GND on real chip) |
| SDA Pin | GPIO 4 | ESP32 I2C data |
| SCL Pin | GPIO 5 | ESP32 I2C clock |
| Bus Speed | 100 kHz | Standard mode |

---

## Register Map (BANK=0 Mode)

The emulator implements all 22 registers of the MCP23017 in BANK=0 addressing mode (default):

| Address | Register | Read | Write | Notes |
|---------|----------|------|-------|-------|
| 0x00 | IODIRA | ✅ | ✅ | I/O Direction A (default: 0x00 = all outputs) |
| 0x01 | IODIRB | ✅ | ✅ | I/O Direction B (default: 0x01 = bit0 input) |
| 0x02 | IPOLA | ✅ | ✅ | Input Polarity A |
| 0x03 | IPOLB | ✅ | ✅ | Input Polarity B |
| 0x04 | GPINTENA | ✅ | ✅ | Interrupt-on-change enable A |
| 0x05 | GPINTENB | ✅ | ✅ | Interrupt-on-change enable B |
| 0x06 | DEFVALA | ✅ | ✅ | Default compare value A |
| 0x07 | DEFVALB | ✅ | ✅ | Default compare value B |
| 0x08 | INTCONA | ✅ | ✅ | Interrupt control A |
| 0x09 | INTCONB | ✅ | ✅ | Interrupt control B |
| 0x0A | IOCON | ✅ | ✅* | Configuration (*BANK bit forced to 0) |
| 0x0B | IOCON | ✅ | ✅* | Mirror of 0x0A |
| 0x0C | GPPUA | ✅ | ✅ | Pull-up resistor A (not physically implemented) |
| 0x0D | GPPUB | ✅ | ✅ | Pull-up resistor B (not physically implemented) |
| 0x0E | INTFA | ✅ | ❌ | Interrupt flag A (read-only) |
| 0x0F | INTFB | ✅ | ❌ | Interrupt flag B (read-only) |
| 0x10 | INTCAPA | ✅ | ❌ | Interrupt capture A (read-only) |
| 0x11 | INTCAPB | ✅ | ❌ | Interrupt capture B (read-only) |
| 0x12 | GPIOA | ✅ | ✅ | **Port A - Relay Outputs** |
| 0x13 | GPIOB | ✅ | ❌ | **Port B - Overfill Input** |
| 0x14 | OLATA | ✅ | ✅ | Output Latch A |
| 0x15 | OLATB | ✅ | ✅ | Output Latch B |

---

## Port A - Relay Outputs (GPIOA / 0x12)

Port A controls 5 relay outputs mapped to ESP32 GPIO pins:

| Bit | MCP Pin | ESP32 GPIO | Function | Description |
|-----|---------|------------|----------|-------------|
| 0 | GPA0 | GPIO 11 | Motor | Vacuum Pump Motor |
| 1 | GPA1 | GPIO 18 | CR1 | Relay 1 |
| 2 | GPA2 | GPIO 17 | CR2 | Relay 2 |
| 3 | GPA3 | GPIO 16 | CR5 | Relay 5 |
| 4 | GPA4 | GPIO 13 | V6 | Valve 6 Relay |
| 5-7 | GPA5-7 | N/A | Unused | Always read as 0 |

### Writing to GPIOA (0x12)

```c
// Example: Turn on Motor (bit 0) and CR1 (bit 1)
// Write 0x03 to register 0x12
i2c_write(0x20, 0x12, 0x03);

// Example: All relays OFF
i2c_write(0x20, 0x12, 0x00);

// Example: Predefined patterns
// IDLE:  0x00 (all off)
// RUN:   0x0B (Motor + CR1 + CR5)
// PURGE: 0x05 (Motor + CR2)
// BURP:  0x08 (CR5 only)
```

---

## Port B - Overfill Input (GPIOB / 0x13)

Port B bit 0 is configured as an **input** connected to the overfill sensor:

| Bit | MCP Pin | ESP32 GPIO | Function | Description |
|-----|---------|------------|----------|-------------|
| 0 | GPB0 | GPIO 38 | Overfill | Active LOW = Alarm |
| 1-7 | GPB1-7 | N/A | Unused | Always read as 0 |

### Reading GPIOB (0x13)

```c
// Read overfill status
uint8_t status = i2c_read(0x20, 0x13);
if (status & 0x01) {
    // Overfill alarm ACTIVE
}
```

**Note:** The overfill input has built-in debouncing (5 consecutive LOW readings required to trigger alarm).

---

## Differences from Physical MCP23017

### 1. Default IODIR Values

| Register | Real MCP23017 | This Emulator |
|----------|---------------|---------------|
| IODIRA | 0xFF (all inputs) | **0x00 (all outputs)** |
| IODIRB | 0xFF (all inputs) | **0x01 (bit0 input)** |

**Why:** Our application uses Port A for relay outputs and only bit 0 of Port B for input.

### 2. BANK Mode

- **Real chip:** Supports BANK=0 and BANK=1 addressing modes
- **Emulator:** **Only BANK=0 is supported**
- Attempting to set BANK=1 in IOCON will be ignored

### 3. Interrupt Pins (INTA/INTB)

- **Real chip:** Physical interrupt output pins
- **Emulator:** Interrupt registers are emulated but **no physical interrupt pins**
- Software can still poll INTFA/INTFB registers

### 4. Pull-up Resistors (GPPU)

- **Real chip:** Internal 100kΩ pull-ups
- **Emulator:** GPPUA/GPPUB registers are stored but **pull-ups not physically active**
- External pull-ups should be used if needed

### 5. Sequential Operation (SEQOP)

- **Real chip:** SEQOP=0 enables address auto-increment, SEQOP=1 disables
- **Emulator:** SEQOP=0 mode fully supported, SEQOP=1 supported

---

## I2C Transaction Examples

### Basic Write (Set Register, Then Data)

```c
// Turn on Motor relay (bit 0 of GPIOA)
uint8_t data[2] = {0x12, 0x01};  // Register, Value
i2c_write_bytes(0x20, data, 2);
```

### Basic Read (Set Register Pointer, Then Read)

```c
// Read GPIOA
i2c_write_byte(0x20, 0x12);       // Set register pointer
uint8_t value = i2c_read_byte(0x20);  // Read value
```

### Sequential Write (Multiple Registers)

```c
// Write to GPIOA (0x12) and GPIOB (0x13) in one transaction
uint8_t data[3] = {0x12, 0x0B, 0x00};  // Start at GPIOA, then GPIOB
i2c_write_bytes(0x20, data, 3);
```

### Linux i2c-tools Examples

```bash
# Read all relay states (GPIOA)
i2cget -y 1 0x20 0x12

# Set relays (Motor + CR1 = 0x03)
i2cset -y 1 0x20 0x12 0x03

# Read overfill sensor (GPIOB)
i2cget -y 1 0x20 0x13

# Read IOCON register
i2cget -y 1 0x20 0x0A

# Dump all registers
i2cdump -y 1 0x20
```

---

## Noisy I2C Bus Handling

The emulator includes error detection for noisy I2C environments:

### Built-in Protections

1. **Invalid Register Detection:** Addresses > 0x15 are rejected
2. **Empty Transaction Detection:** Zero-byte transactions increment error counter
3. **Transaction Counting:** Tracks total transactions and errors for diagnostics

### Diagnostic Variables (Available via Web API)

| Variable | Description |
|----------|-------------|
| `i2cTransactionCount` | Total I2C transactions processed |
| `i2cErrorCount` | Transactions with errors detected |
| `i2cLastGoodTransaction` | Timestamp of last successful transaction |

### Enabling Debug Logging

Set `i2cDebugEnabled = true` in code to enable verbose I2C logging:

```
I2C WRITE: Reg 0x12 = 0x0B
I2C READ: Reg 0x12 = 0x0B
I2C ERROR: Invalid register 0x1F
```

### Recommendations for Noisy Environments

1. **Use proper I2C pull-ups** (4.7kΩ typical)
2. **Keep I2C wires short** (< 50cm recommended)
3. **Use twisted pair or shielded cable** for SDA/SCL
4. **Avoid routing I2C near high-current relay wiring**
5. **Add decoupling capacitors** near the I2C bus
6. **Implement retry logic** in master software:

```c
// Example retry logic
int retries = 3;
while (retries-- > 0) {
    if (i2c_write(0x20, 0x12, value) == 0) {
        break;  // Success
    }
    usleep(1000);  // Wait 1ms before retry
}
```

---

## Web Override Behavior

**Important:** The web interface can temporarily override I2C relay commands:

- When user interacts with web interface, a 5-second override is activated
- During override, I2C writes to GPIOA are **ignored**
- After 5 seconds of web inactivity, I2C control resumes

This prevents conflicts between web and I2C control sources.

---

## Troubleshooting

### Master Software Won't Communicate

1. **Verify I2C address:** Must be 0x20
2. **Check bus speed:** Use 100kHz (standard mode)
3. **Verify wiring:** SDA to GPIO 4, SCL to GPIO 5
4. **Check pull-ups:** Need external pull-ups on SDA/SCL

### Relays Not Responding to I2C Commands

1. **Check web override:** Wait 5 seconds after last web interaction
2. **Verify register address:** Use 0x12 (GPIOA) not 0x14 (OLATA) - both work
3. **Check bit mapping:** Bit 0 = Motor, Bit 1 = CR1, etc.

### Wrong Values Read Back

1. **Check IPOL registers:** Non-zero IPOL inverts read values
2. **Verify IODIR:** Outputs read back the output latch value
3. **Check BANK mode:** Only BANK=0 is supported

### I2C Errors in Noisy Environment

1. Enable debug logging to identify error patterns
2. Check Web API for error counts: `/api/status` includes `i2cErrors`
3. Implement retry logic in master software
4. Improve physical I2C bus integrity

---

## Version History

| Version | Date | Changes |
|---------|------|---------|

| Rev 9.1d | 1/30/2026 | CRITICAL: Fixed BlueCherry sync failure causing restart loop |
| Rev 9.1c | 1/30/2026 | CRITICAL: Fixed I2C timeout causing OSError [Errno 5] |
| Rev 9.1b | 1/29/2026 | Fixed mutex race condition on relay writes, improved overfill hysteresis |
| Rev 9.1 | 1/29/2026 | Full MCP23017 register support, error counting |
| Rev 9.0 | 1/29/2026 | Initial MCP23017 emulation (basic registers only) |

---

## Known Issues Fixed

### BlueCherry Restart Loop (Fixed in 9.1d)
**Symptom:** System reboots every 15-30 minutes during BlueCherry platform outages
**Cause:** When BlueCherry sync failed, code called `modem.reset()` which disconnected LTE. On next `loop()` iteration, LTE check failed and triggered `ESP.restart()`.
**Fix:** 
- Removed `modem.reset()` and `lteConnect()` from BlueCherry sync failure path
- System now marks BlueCherry as disconnected and continues running
- Added fault code 4096 to alert users when BlueCherry is offline
- Weekly restart still occurs if BlueCherry never connects (for OTA retry)

### I2C Timeout Error (Fixed in 9.1c)
**Symptom:** `OSError: [Errno 5] Input/output error` on Linux master
**Cause:** Mutex timeouts in I2C callbacks were 50-100ms, but I2C slave must respond in microseconds
**Fix:** 
- Reduced all I2C callback mutex timeouts to 1ms maximum
- Added fallback behavior: if mutex unavailable, proceed with cached/direct value
- I2C bus no longer times out waiting for firmware response

### Relay Read-Back Mismatch (Fixed in 9.1b)
**Symptom:** Master writes value to GPIOA, reads back different value
**Cause:** Race condition - gpioA_value was written without mutex protection
**Fix:** All writes to gpioA_value now use mutex synchronization

### Spurious Overfill Alarms (Fixed in 9.1b)
**Symptom:** Overfill alarm triggering randomly
**Cause:** 
1. Input pin configured without pull-up (floating when sensor disconnected)
2. Single HIGH reading immediately cleared alarm (no hysteresis)
**Fix:** 
1. Added INPUT_PULLUP to GPIO38
2. Alarm now requires 3 consecutive HIGH readings to clear (hysteresis)

---

## Contact

For issues with MCP23017 emulation compatibility, check the I2C debug logs and provide:
- Transaction sequence (hex dump if possible)
- Error messages from debug log
- Master software/library being used
- I2C bus configuration (speed, pull-ups, wire length)
