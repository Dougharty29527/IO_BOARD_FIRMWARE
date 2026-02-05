'''
SerialSend.py

Sends sensor data to Walter ESP32 via RS-232 Serial (not I2C)
Formats data as JSON compatible with IO_BOARD_FIRMWARE1.ino

Version: 3.0 - JSON format, bidirectional communication ready
Works with IO_BOARD_FIRMWARE1.ino

Expected Serial Port: /dev/ttyUSB0 or /dev/ttyAMA0 (configurable)
Baud Rate: 9600
Format: JSON string (no line termination needed, ESP32 detects {} pairs)

JSON Format:
  {"type":"data","gmid":"CSX-1234","press":-14.22,"mode":0,"current":0.07,"fault":0,"cycles":484}

Future: Will also read JSON commands from ESP32 to Python (SSH passthrough)

'''

import time
import json
import logging
import serial
import sqlite3
import os

# Try to import redis, but don't fail if it's not available
try:
    import redis
    REDIS_AVAILABLE = True
except ImportError:
    REDIS_AVAILABLE = False
    print("Redis module not available - using SQLite mode")

logger = logging.getLogger("pylog")

logging.basicConfig(
    filename='/home/pi/python/serialsend.log', 
    filemode='a', 
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s', 
    level=logging.INFO
)

# Configuration
SERIAL_PORT = '/dev/ttyAMA0'  # Updated to match your hardware UART
BAUD_RATE = 9600
TRANSMIT_INTERVAL = 15  # seconds
DEBUGGING = False
EVENT_MONITORING = False

# Database configuration
USE_REDIS = True  # Changed to True - control.py uses Redis for live data
DB_PATH = '/home/pi/python/rms.db'  # SQLite database path
JSON_PATH = '/home/pi/python/cont2_data.json'  # JSON file path (fallback)

# Global variables
rconn = None
cont = {}
alarms = {}

# Auto-detect which database to use
# Priority: Redis (live data) > JSON (stale) > SQLite (backup)
if REDIS_AVAILABLE:
    try:
        rconn = redis.Redis('localhost', decode_responses=True)
        rconn.ping()  # Test connection
        USE_DB = 'redis'
        print("✓ Using Redis for data storage (LIVE DATA)")
    except Exception as e:
        print(f"⚠ Redis connection failed: {e}")
        # Fallback to JSON file
        if os.path.exists(JSON_PATH):
            USE_DB = 'json'
            print("⚠ Using JSON file for data storage (may be stale)")
        else:
            USE_DB = 'sqlite'
            print("⚠ Using SQLite for data storage")
else:
    print("⚠ Redis module not installed - install with: sudo pip3 install redis")
    # Try JSON file first (most common with control.py), then SQLite
    if os.path.exists(JSON_PATH):
        USE_DB = 'json'
        print("⚠ Using JSON file for data storage (may be stale)")
    elif os.path.exists(DB_PATH):
        USE_DB = 'sqlite'
        print("✓ Using SQLite for data storage")
    else:
        USE_DB = 'json'
        print("⚠ No data source found - will use JSON when available")


def get_json_data():
    """
    Retrieve current data from JSON file (control.py format)
    Reads from cont2_data.json that control.py writes to
    """
    cont = {
        'pressure': 0.0,
        'current': 0.0,
        'temp': 0.0,
        'runcycles': 0,
        'faults': 0,
        'mode': 0,
        'seq': 0,
        'gmid': 'CSX-9000'
    }
    alarms = {}
    
    try:
        if os.path.exists(JSON_PATH):
            with open(JSON_PATH, 'r') as f:
                data = json.load(f)
                
            # Extract values from control.py JSON format
            cont['pressure'] = data.get('pressure', 0.0)
            cont['current'] = data.get('current', 0.0)
            cont['temp'] = data.get('temp', 0.0)
            cont['runcycles'] = data.get('runcycles', 0)
            cont['faults'] = data.get('faults', 0)
            cont['mode'] = data.get('mode', 0)
            cont['seq'] = data.get('seq', 0)
            cont['gmid'] = data.get('gmid', 'CSX-9000')
            
    except json.JSONDecodeError as e:
        logging.error(f"JSON decode error: {e}")
    except Exception as e:
        logging.error(f"Error reading JSON data: {e}")
    
    return cont, alarms


def get_sqlite_data():
    """
    Retrieve current data from SQLite database
    Reads the most recent controller state from rms.db
    """
    cont = {
        'pressure': 0.0,
        'current': 0.0,
        'temp': 0.0,
        'runcycles': 0,
        'faults': 0,
        'mode': 0,
        'seq': 0,
        'gmid': 'CSX-9000'
    }
    alarms = {}
    
    try:
        if not os.path.exists(DB_PATH):
            logging.warning(f"Database not found at {DB_PATH}")
            return cont, alarms
        
        conn = sqlite3.connect(DB_PATH)
        cur = conn.cursor()
        
        # Check if tables exist
        cur.execute("SELECT name FROM sqlite_master WHERE type='table'")
        tables = [row[0] for row in cur.fetchall()]
        
        if 'controller' in tables:
            # Get the latest controller state
            cur.execute("""
                SELECT pressure, current, temp, runcycles, faults, mode, seq, gmid 
                FROM controller 
                ORDER BY rowid DESC 
                LIMIT 1
            """)
            row = cur.fetchone()
            if row:
                cont['pressure'] = row[0] if row[0] is not None else 0.0
                cont['current'] = row[1] if row[1] is not None else 0.0
                cont['temp'] = row[2] if row[2] is not None else 0.0
                cont['runcycles'] = row[3] if row[3] is not None else 0
                cont['faults'] = row[4] if row[4] is not None else 0
                cont['mode'] = row[5] if row[5] is not None else 0
                cont['seq'] = row[6] if row[6] is not None else 0
                cont['gmid'] = row[7] if row[7] is not None else 'CSX-9000'
        
        conn.close()
        
    except sqlite3.Error as e:
        logging.error(f"SQLite error: {e}")
    except Exception as e:
        logging.error(f"Error reading SQLite data: {e}")
    
    return cont, alarms


def get_redis_data():
    """Retrieve current data from Redis"""
    try:
        cont = json.loads(rconn.get("cont"))
        alarms = json.loads(rconn.get("alarms"))
    except:
        cont = {}
        alarms = {}
    
    return cont, alarms


def get_data():
    """
    Universal data getter - automatically uses Redis, JSON, or SQLite
    """
    if USE_DB == 'redis':
        return get_redis_data()
    elif USE_DB == 'json':
        return get_json_data()
    else:
        return get_sqlite_data()


def get_payload(cont, alarms):
    """
    Extract payload from Redis data
    Returns dict with all sensor values
    """
    try:
        with open('profile.json') as f:
            profile = json.load(f)
    except:
        profile = {}
    
    pressure = round(cont.get('pressure', 0.0), 2)
    current = round(cont.get('current', 0.0), 2)
    temp = round(cont.get('temp', 0.0), 2)
    
    payload = {
        'id': cont.get('gmid', 'CSX-9000'),
        's': cont.get('seq', 0),
        'p': pressure,
        'r': cont.get('runcycles', 0),
        'f': cont.get('faults', 0),
        'm': cont.get('mode', 0),
        't': temp,
        'c': current,
        'pr': profile
    }
    
    return payload


def format_json_string(payload):
    """
    Format payload as JSON string compatible with ESP32 firmware
    
    Expected format:
      {"type":"data","gmid":"CSX-1234","press":-14.22,"mode":0,"current":0.07,"fault":0,"cycles":484}
    
    Field mapping:
      - type: Message type (always "data" for sensor data)
      - gmid: Green Machine ID (e.g., "CSX-1234")
      - press: Tank pressure in IWC (float)
      - mode: Operating mode 0=idle, 1=run, 2=purge, 3=burp (int)
      - current: Motor current in amps (float)
      - fault: Fault code (int)
      - cycles: Run cycles count (int)
    
    Example output:
      '{"type":"data","gmid":"CSX-1234","press":0.45,"mode":1,"current":5.23,"fault":2,"cycles":150}'
    """
    
    # Build JSON object
    data = {
        "type": "data",
        "gmid": payload['id'],
        "press": round(payload['p'], 2),
        "mode": payload['m'],
        "current": round(payload['c'], 2),
        "fault": payload['f'],
        "cycles": payload['r']
    }
    
    # Convert to compact JSON string (no spaces)
    json_string = json.dumps(data, separators=(',', ':'))
    
    return json_string


def transmit_serial_data(payload, ser):
    """
    Send JSON data over RS-232 Serial to Walter ESP32
    
    Args:
        payload: Dict with sensor data
        ser: pySerial object (already opened)
    
    Returns:
        bool: True if successful, False otherwise
    """
    try:
        # Format as JSON string
        json_string = format_json_string(payload)
        
        if DEBUGGING:
            print(f"DEBUG: JSON string = [{json_string}]")
            print(f"DEBUG: String length = {len(json_string)} bytes")
        
        # Send via serial (no line termination needed, ESP32 detects {} pairs)
        bytes_written = ser.write(json_string.encode('ascii'))
        ser.flush()  # Ensure data is sent immediately
        
        logging.info(f"Serial data sent: {json_string}")
        print(f"✓ Serial data sent ({bytes_written} bytes): {json_string}")
        
        return True
        
    except Exception as e:
        logging.error(f"Error sending serial data: {e}")
        print(f"✗ Error sending serial data: {e}")
        return False


def initialize_serial_port():
    """
    Initialize and return serial port connection
    
    Returns:
        serial.Serial object or None if failed
    """
    try:
        ser = serial.Serial(
            port=SERIAL_PORT,
            baudrate=BAUD_RATE,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=1,
            xonxoff=False,
            rtscts=False,
            dsrdtr=False
        )
        
        print(f"✓ Serial port {SERIAL_PORT} opened at {BAUD_RATE} baud")
        logging.info(f"Serial port {SERIAL_PORT} opened at {BAUD_RATE} baud")
        
        # Give it a moment to stabilize
        time.sleep(0.5)
        
        return ser
        
    except serial.SerialException as e:
        print(f"✗ Failed to open serial port {SERIAL_PORT}: {e}")
        logging.error(f"Failed to open serial port {SERIAL_PORT}: {e}")
        return None


def main():
    """
    Main loop:
    1. Connect to serial port
    2. Read data from Redis/SQLite every second
    3. Send via serial every TRANSMIT_INTERVAL seconds
    """
    print(f"SerialSend.py v3.0 initializing (JSON mode)...\n")
    print(f"Database: {USE_DB.upper()}")
    print(f"Serial Port: {SERIAL_PORT}")
    print(f"Baud Rate: {BAUD_RATE}")
    print(f"Transmit Interval: {TRANSMIT_INTERVAL}s")
    print(f"Format: JSON\n")
    
    # Initialize serial port
    ser = initialize_serial_port()
    if ser is None:
        print("Cannot continue without serial port. Exiting.")
        return
    
    # Get initial data
    cont, alarms = get_data()
    
    if 'debugging' not in cont:
        DEBUGGING = False
        cont['debugging'] = DEBUGGING
    else:
        DEBUGGING = cont['debugging']
    
    transmit_time = time.time()
    
    try:
        while True:
            # Get current data from database
            cont, alarms = get_data()
            
            if DEBUGGING:
                print(f"DEBUG: Database data = {cont}")
            
            # Build payload
            payload = get_payload(cont, alarms)
            
            # Store payload back to Redis if using Redis
            if USE_DB == 'redis' and rconn:
                try:
                    rconn.set("payload", json.dumps(payload))
                except:
                    pass
            
            # Check if it's time to transmit
            if time.time() - transmit_time >= TRANSMIT_INTERVAL:
                
                # Event monitoring logic (optional)
                if EVENT_MONITORING:
                    # Only send if there's an alarm or active mode
                    if (cont.get('faults', 0) > 0) or (cont.get('mode', 0) > 0):
                        transmit_serial_data(payload, ser)
                else:
                    # Always send (default)
                    transmit_serial_data(payload, ser)
                
                # Reset timer
                transmit_time = time.time()
            
            # Sleep for 1 second before next check
            time.sleep(1)
    
    except KeyboardInterrupt:
        print("\n\nShutting down gracefully...")
        logging.info("SerialSend.py stopped by user")
    
    except Exception as e:
        print(f"\n\nUnexpected error: {e}")
        logging.error(f"Unexpected error in main loop: {e}")
    
    finally:
        # Clean up serial port
        if ser and ser.is_open:
            ser.close()
            print("Serial port closed.")
            logging.info("Serial port closed")


if __name__ == '__main__':
    DEBUGGING = False
    main()
