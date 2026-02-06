# IO Board — Unified Daily Status Packet Update Guide

**Purpose:** Update the IO Board's `buildStatusCbor()` to match the new 15-element unified format used by the RMS Board (v0.1.33), and enable daily sending.

---

## What Changed

The daily status CBOR packet sent to `167.172.15.241:5686` has been expanded from **9 elements to 15 elements** so both the RMS Board and IO Board populate a **common server-side table**.

**New fields added:** device_type, sd_status, MCC, MNC, Cell ID, TAC

**Field order changed:** positions have been reorganized — the server decoder must be updated to match.

---

## Unified 15-Element Status Array

| Position | Field | Type | IO Board Value | Source |
|----------|-------|------|----------------|--------|
| 0 | device_id | int | `9199` | Numeric part of device name |
| 1 | device_type | **string** | `"IO_Board"` | **Hardcoded string** |
| 2 | firmware_ver | string | `"9.4e"` | `ver` global |
| 3 | band | int | `17` | `modemBand.toInt()` |
| 4 | network_name | string | `"T-Mobile"` | `modemNetName` |
| 5 | rsrp | string | `"-89.5"` | `modemRSRP` |
| 6 | rsrq | string | `"-10.2"` | `modemRSRQ` |
| 7 | mac | string | `"AA:BB:CC:DD:EE:FF"` | `macStr` |
| 8 | imei | string | `"351234567890123"` | `imei` |
| 9 | plc_type | int | `0` | Always `0` for IO Board |
| 10 | sd_status | **string** | `"OK"` or `"FAULT"` | `isSDCardOK() ? "OK" : "FAULT"` |
| 11 | mcc | **int** | `310` | `modemCC` (already exists) |
| 12 | mnc | **int** | `410` | `modemNC` (already exists) |
| 13 | cell_id | **int** | `20878097` | `modemCID` (already exists) |
| 14 | tac | **int** | `10519` | `modemTAC` (already exists) |

---

## Code Changes Required

### 1. Update `buildStatusCbor()` (~line 3277)

Replace the existing 9-element function with the 15-element version:

```cpp
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
    String sdStatus = isSDCardOK() ? "OK" : "FAULT";
    
    // Create 15-element unified status array (matches RMS Board format)
    cbor_encoder_create_array(&encoder, &arrayEncoder, 15);
    
    // [0] Device ID (integer)
    cbor_encode_int(&arrayEncoder, id);
    
    // [1] Device type (string) - "IO_Board" for this device
    cbor_encode_text_stringz(&arrayEncoder, "IO_Board");
    
    // [2] Firmware version (string)
    cbor_encode_text_stringz(&arrayEncoder, ver.c_str());
    
    // [3] LTE band (integer)
    cbor_encode_int(&arrayEncoder, modemBand.toInt());
    
    // [4] Network/operator name (string)
    cbor_encode_text_stringz(&arrayEncoder, modemNetName.c_str());
    
    // [5] RSRP - signal power in dBm (string)
    cbor_encode_text_stringz(&arrayEncoder, modemRSRP.c_str());
    
    // [6] RSRQ - signal quality in dB (string)
    cbor_encode_text_stringz(&arrayEncoder, modemRSRQ.c_str());
    
    // [7] MAC address (string)
    cbor_encode_text_stringz(&arrayEncoder, macStr.c_str());
    
    // [8] IMEI (string)
    cbor_encode_text_stringz(&arrayEncoder, imei.c_str());
    
    // [9] PLC type (integer) - always 0 for IO Board
    cbor_encode_int(&arrayEncoder, 0);
    
    // [10] SD card status (string)
    cbor_encode_text_stringz(&arrayEncoder, sdStatus.c_str());
    
    // [11] MCC - Mobile Country Code (integer)
    cbor_encode_int(&arrayEncoder, modemCC);
    
    // [12] MNC - Mobile Network Code (integer)
    cbor_encode_int(&arrayEncoder, modemNC);
    
    // [13] Cell ID - E-UTRAN Cell Identity (integer)
    cbor_encode_int(&arrayEncoder, modemCID);
    
    // [14] TAC - Tracking Area Code (integer)
    cbor_encode_int(&arrayEncoder, modemTAC);
    
    cbor_encoder_close_container(&encoder, &arrayEncoder);
    return cbor_encoder_get_buffer_size(&encoder, cborBuf);
}
```

### 2. Add daily status timer and call in `loop()`

Add these globals (near the other timing variables):

```cpp
#define STATUS_INTERVAL 86400000       // 24 hours in milliseconds
unsigned long lastDailyStatusTime = 0;
```

Add this block in `loop()`, after the firmware check section (~line 3937):

```cpp
// Daily status message (unified format - sends to port 5686)
if (currentTime - lastDailyStatusTime >= STATUS_INTERVAL || firstTime) {
    Serial.println("##################################################");
    Serial.println("##### DAILY STATUS - IO_Board (Unified Format) ####");
    Serial.println("##################################################");
    refreshCellSignalInfo();
    if (sendStatusUpdateViaSocket()) {
        Serial.println("##### Status sent successfully #####");
    } else {
        Serial.println("##### Status send failed #####");
    }
    Serial.println("##################################################");
    lastDailyStatusTime = currentTime;
}
```

> **Note:** `sendStatusUpdateViaSocket()` already exists at line 3429 and calls `refreshCellSignalInfo()` + `buildStatusCbor()` + `sendCborDataViaSocket()`. It just needs to be invoked.

### 3. No new globals needed for cell info

The IO Board already has `modemCC`, `modemNC`, `modemCID`, `modemTAC` populated by `refreshCellSignalInfo()`. No new variables required.

---

## Quick Checklist

- [ ] Replace `buildStatusCbor()` with 15-element version
- [ ] Add `STATUS_INTERVAL` define and `lastDailyStatusTime` global
- [ ] Add daily status call in `loop()` (uses existing `sendStatusUpdateViaSocket()`)
- [ ] Update server decoder to handle 15-element array
- [ ] Test: verify Serial Monitor shows all 15 fields on boot
- [ ] Test: verify CBOR decodes correctly on server side

---

## Server-Side Note

The server receiving on port `5686` must now handle **both** the old 9-element format (from devices not yet updated) and the new 15-element format. The simplest approach: check the array length. If 15, use unified format. If 9, use legacy format. The `device_type` field at position 1 (a string) also distinguishes the new format from the old (which had an integer at position 1).
