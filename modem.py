'''
Serial data transmission module for ESP32 communication.

Sends sensor data to Walter ESP32 via RS-232 Serial.
Formats data as JSON compatible with IO_BOARD_FIRMWARE.ino.

JSON Format (Outgoing to ESP32):
    {"type":"data","gmid":"CSX-1234","press":-14.22,"mode":0,"current":0.07,"fault":0,"cycles":484}

JSON Format (Incoming from ESP32 - Status):
    {"datetime":"2026-01-29 12:34:56","sdcard":"OK","passthrough":0,"lte":1,"rsrp":"-85.5","rsrq":"-10.2"}
    
    - datetime: Current timestamp from modem (UTC)
    - sdcard: "OK" or "FAULT" 
    - passthrough: 0=normal, 1=passthrough mode active
    - lte: 1=connected to LTE network, 0=not connected
    - rsrp: Signal strength in dBm (typical: -80 excellent, -100 poor)
    - rsrq: Signal quality in dB (typical: -10 good, -20 poor)

JSON Format (Incoming from ESP32 - Passthrough Request):
    {"passthrough":"remote 60"}
    
    - passthrough: "remote XX" where XX is timeout in minutes
    - Linux responds with "ready" then starts PPP
    - PPP runs for XX minutes then stops automatically

JSON Format (Outgoing to ESP32 - Passthrough Command):
    {"command":"passthrough","timeout":60}
    
    - command: "passthrough" to request passthrough mode
    - timeout: Duration in minutes
'''

import json
import serial
import time
import subprocess
import threading
import re

from kivy.logger import Logger
from kivy.clock import Clock

from .data_handler import DataHandler


class SerialManager:
    '''Manages serial data transmission to ESP32'''

    # Serial configuration
    SERIAL_PORT = '/dev/serial0'
    BAUD_RATE = 115200
    UPDATE_INTERVAL = 15      # Send data to ESP32 every 15 seconds
    RECEIVE_INTERVAL = 1      # Check for incoming ESP32 data every 1 second
    RECONNECT_DELAY = 5

    # Alarm bitmask mapping
    ALARM_MAP = {
        'vac_pump': 1,
        'panel_power': 2,
        'overfill': 4,
        'digital_storage': 8,
        'under_pressure': 16,
        'over_pressure': 32,
        'zero_pressure': 64,
        'variable_pressure': 128,
        'pressure_sensor': 256,
        '72_hour_shutdown': 512
    }

    def __init__(self, data_handler):
        self.data_handler = data_handler
        self.serial_port = None
        self.online = False
        self._send_event = None      # Scheduled event for sending data (15s)
        self._receive_event = None   # Scheduled event for receiving data (1s)
        
        # ESP32 status variables (received from IO Board)
        self.esp32_datetime = None       # Timestamp from modem (string)
        self.esp32_sdcard_status = None  # "OK" or "FAULT"
        self.esp32_passthrough = 0       # 0=normal, 1=passthrough mode active
        self.esp32_last_update = None    # time.time() of last received message
        
        # Cellular signal quality (received from IO Board)
        self.esp32_lte_connected = False # True if connected to LTE network
        self.esp32_rsrp = None           # Signal strength in dBm (e.g., -85.5)
        self.esp32_rsrq = None           # Signal quality in dB (e.g., -10.2)
        
        # Passthrough mode callback (called when passthrough value changes)
        # Set this to a function that takes (old_value, new_value) as arguments
        # Example: manager.on_passthrough_change = my_ppp_handler
        self.on_passthrough_change = None
        
        # PPP (Point-to-Point Protocol) management
        self.ppp_process = None          # subprocess.Popen object for pppd
        self.ppp_timeout_minutes = 0     # Requested timeout in minutes
        self.ppp_start_time = None       # time.time() when PPP started
        self.ppp_timer_thread = None     # Background thread for timeout
        self.ppp_active = False          # True while PPP is running

    def _log(self, level, message):
        '''Log message with specified level'''
        level = level.lower()
        if hasattr(Logger, level):
            getattr(Logger, level)(f'Serial: {message}')
        else:
            Logger.warning(f'Serial: Invalid log level "{level}". Message: {message}')

    # -------------------------------------------------------------------------
    # Serial Port Management
    # -------------------------------------------------------------------------

    def _initialize_serial(self):
        '''Initialize serial port connection'''
        try:
            if self.serial_port and self.serial_port.is_open:
                return True

            self.serial_port = serial.Serial(
                port=self.SERIAL_PORT,
                baudrate=self.BAUD_RATE,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=1,
                xonxoff=False,
                rtscts=False,
                dsrdtr=False
            )
            time.sleep(0.1)  # Brief stabilization
            self.online = True
            self._log('info', f'Serial port {self.SERIAL_PORT} opened at {self.BAUD_RATE} baud')
            return True

        except serial.SerialException as e:
            self._log('error', f'Failed to open serial port: {e}')
            self.online = False
            return False
        except Exception as e:
            self._log('error', f'Unexpected error initializing serial: {e}')
            self.online = False
            return False

    def _close_serial(self):
        '''Close serial port connection'''
        if self.serial_port and self.serial_port.is_open:
            try:
                self.serial_port.close()
                self._log('info', 'Serial port closed')
            except Exception as e:
                self._log('error', f'Error closing serial port: {e}')
        self.serial_port = None
        self.online = False

    # -------------------------------------------------------------------------
    # PPP (Point-to-Point Protocol) Management
    # -------------------------------------------------------------------------
    
    # Escape sequence to signal ESP32 to exit passthrough mode
    # ESP32 watches for this sequence and restarts to normal mode when detected
    PPP_STOP_SEQUENCE = b'+++STOPPPP\n'

    def connect_ppp(self, timeout_minutes=60):
        '''
        Initiate PPP connection from Linux side.
        
        Sends command to ESP32 to enter passthrough mode, waits for ESP32 to
        restart, then starts PPP connection.
        
        Args:
            timeout_minutes: Duration for PPP connection (default 60 min)
            
        Returns:
            bool: True if PPP started successfully, False otherwise
            
        Example:
            manager.connect_ppp(30)  # Start 30-minute PPP session
        '''
        if self.ppp_active:
            self._log('warning', 'PPP already active')
            return False
            
        self._log('info', f'Initiating PPP connection ({timeout_minutes} min)...')
        
        # Ensure serial port is open
        if not self.serial_port or not self.serial_port.is_open:
            if not self._initialize_serial():
                self._log('error', 'Cannot open serial port')
                return False
        
        # Send passthrough request to ESP32
        # Format: {"command":"passthrough","timeout":XX}
        try:
            request = json.dumps({
                'command': 'passthrough',
                'timeout': timeout_minutes
            })
            self.serial_port.write((request + '\n').encode('ascii'))
            self.serial_port.flush()
            self._log('info', f'Sent passthrough request to ESP32: {request}')
        except Exception as e:
            self._log('error', f'Failed to send passthrough request: {e}')
            return False
        
        # Wait for ESP32 to process and restart (up to 5 seconds)
        self._log('info', 'Waiting for ESP32 to restart into passthrough mode...')
        time.sleep(5)
        
        # Close serial port - pppd will take over
        self._close_serial()
        
        # Wait for ESP32 passthrough boot (additional 3 seconds)
        time.sleep(3)
        
        # Start PPP
        self.ppp_timeout_minutes = timeout_minutes
        self._start_ppp()
        
        return self.ppp_active

    def disconnect_ppp(self):
        '''
        Disconnect PPP and signal ESP32 to return to normal mode.
        
        Stops pppd process, reopens serial port, sends escape sequence
        to ESP32 to trigger restart to normal operation.
        
        Returns:
            bool: True if disconnect successful, False otherwise
            
        Example:
            manager.disconnect_ppp()  # Stop PPP and return to normal
        '''
        if not self.ppp_active:
            self._log('info', 'PPP not active, nothing to disconnect')
            return True
            
        self._log('info', 'Disconnecting PPP...')
        
        # Stop pppd process
        self._stop_ppp()
        
        # Wait for serial port to be released
        time.sleep(2)
        
        # Reopen serial port to send stop signal
        if self._initialize_serial():
            try:
                # Send escape sequence to ESP32 to trigger restart
                self._log('info', 'Sending stop signal to ESP32...')
                self.serial_port.write(self.PPP_STOP_SEQUENCE)
                self.serial_port.flush()
                self._log('info', 'Stop signal sent, ESP32 will restart to normal mode')
                
                # Wait for ESP32 to restart
                time.sleep(3)
                
                # Reopen serial for normal operation
                self._close_serial()
                time.sleep(2)
                self._initialize_serial()
                
            except Exception as e:
                self._log('error', f'Error sending stop signal: {e}')
        
        return not self.ppp_active

    def handle_passthrough_request(self, timeout_minutes):
        '''
        Handle passthrough request from ESP32 - start PPP connection.
        
        Called when ESP32 sends: {"passthrough":"remote XX"}
        
        Sequence:
        1. Close serial port (PPP will use it)
        2. Send "ready" confirmation to ESP32
        3. Start pppd process
        4. Start timeout timer thread
        5. After timeout, stop PPP and resume normal operation
        
        Args:
            timeout_minutes: Duration for PPP connection in minutes
        '''
        self._log('info', f'Passthrough request received: {timeout_minutes} minutes')
        
        # Send "ready" confirmation to ESP32 before closing serial
        try:
            if self.serial_port and self.serial_port.is_open:
                self.serial_port.write(b'ready\n')
                self.serial_port.flush()
                self._log('info', 'Sent "ready" confirmation to ESP32')
                time.sleep(0.5)  # Brief delay to ensure message is sent
        except Exception as e:
            self._log('error', f'Error sending ready confirmation: {e}')
        
        # Close serial port - pppd will take over
        self._close_serial()
        
        # Store timeout and start PPP
        self.ppp_timeout_minutes = timeout_minutes
        self._start_ppp()

    def _start_ppp(self):
        '''
        Start PPP daemon process.
        
        Runs: sudo pppd call walter nodetach debug
        
        The "nodetach" option keeps pppd in foreground so we can monitor it.
        The "debug" option provides verbose logging to syslog.
        '''
        if self.ppp_active:
            self._log('warning', 'PPP already active, ignoring start request')
            return
            
        self._log('info', 'Starting PPP connection...')
        
        try:
            # Start pppd process
            self.ppp_process = subprocess.Popen(
                ['sudo', 'pppd', 'call', 'walter', 'nodetach', 'debug'],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE
            )
            self.ppp_active = True
            self.ppp_start_time = time.time()
            
            self._log('info', f'PPP started (PID: {self.ppp_process.pid}), '
                             f'timeout: {self.ppp_timeout_minutes} min')
            
            # Start timeout timer in background thread
            self.ppp_timer_thread = threading.Thread(
                target=self._ppp_timeout_monitor,
                daemon=True
            )
            self.ppp_timer_thread.start()
            
        except Exception as e:
            self._log('error', f'Failed to start PPP: {e}')
            self.ppp_active = False
            self._resume_normal_operation()

    def _ppp_timeout_monitor(self):
        '''
        Background thread to monitor PPP timeout and process status.
        
        Checks every 10 seconds:
        - If timeout has expired
        - If pppd process has terminated unexpectedly
        
        When timeout expires or process ends, calls _stop_ppp().
        '''
        timeout_seconds = self.ppp_timeout_minutes * 60
        check_interval = 10  # Check every 10 seconds
        
        self._log('info', f'PPP timeout monitor started: {timeout_seconds}s')
        
        while self.ppp_active:
            time.sleep(check_interval)
            
            # Check if pppd process has terminated
            if self.ppp_process and self.ppp_process.poll() is not None:
                exit_code = self.ppp_process.returncode
                self._log('warning', f'PPP process terminated (exit code: {exit_code})')
                self._stop_ppp()
                return
            
            # Check if timeout expired
            elapsed = time.time() - self.ppp_start_time
            remaining = timeout_seconds - elapsed
            
            if remaining <= 0:
                self._log('info', 'PPP timeout expired')
                self._stop_ppp()
                return
            
            # Log remaining time every minute
            if int(elapsed) % 60 < check_interval:
                self._log('debug', f'PPP running, {int(remaining/60)} min remaining')

    def _stop_ppp(self):
        '''
        Stop PPP daemon process and resume normal operation.
        
        Terminates pppd gracefully (SIGTERM), then forcefully (SIGKILL) if needed.
        After PPP stops, reopens serial port for normal communication.
        '''
        if not self.ppp_active:
            return
            
        self._log('info', 'Stopping PPP connection...')
        
        try:
            if self.ppp_process:
                # Try graceful termination first
                self.ppp_process.terminate()
                
                # Wait up to 5 seconds for graceful shutdown
                try:
                    self.ppp_process.wait(timeout=5)
                    self._log('info', 'PPP terminated gracefully')
                except subprocess.TimeoutExpired:
                    # Force kill if still running
                    self._log('warning', 'PPP not responding, forcing kill')
                    self.ppp_process.kill()
                    self.ppp_process.wait(timeout=2)
                    
        except Exception as e:
            self._log('error', f'Error stopping PPP: {e}')
            # Try to kill via sudo as fallback
            try:
                subprocess.run(['sudo', 'pkill', '-9', 'pppd'], timeout=5)
            except Exception:
                pass
        
        self.ppp_process = None
        self.ppp_active = False
        self.ppp_start_time = None
        
        # Resume normal serial operation
        self._resume_normal_operation()

    def _resume_normal_operation(self):
        '''
        Resume normal serial communication after PPP ends.
        
        Waits briefly for serial port to be released, then reopens it.
        '''
        self._log('info', 'Resuming normal serial operation...')
        
        # Wait for serial port to be released
        time.sleep(2)
        
        # Reopen serial port
        if self._initialize_serial():
            self._log('info', 'Serial port reopened, normal operation resumed')
        else:
            self._log('error', 'Failed to reopen serial port after PPP')
            # Schedule retry
            Clock.schedule_once(lambda dt: self._initialize_serial(), 5)

    def is_ppp_active(self):
        '''Check if PPP connection is currently active'''
        return self.ppp_active

    # -------------------------------------------------------------------------
    # ESP32 Data Reception
    # -------------------------------------------------------------------------

    def receive_esp32_status(self):
        '''
        Check for and parse incoming JSON status from ESP32.
        
        Expected JSON format:
            {"datetime":"2026-01-29 12:34:56","sdcard":"OK","passthrough":0}
        
        Returns:
            dict: Parsed status data, or None if no data available
            
        Updates instance variables:
            - self.esp32_datetime
            - self.esp32_sdcard_status  
            - self.esp32_passthrough
            - self.esp32_last_update
            
        Triggers on_passthrough_change callback if passthrough value changes.
        '''
        if not self.serial_port or not self.serial_port.is_open:
            return None
            
        try:
            # Check if data is waiting
            if self.serial_port.in_waiting == 0:
                return None
                
            # Read line (JSON ends with newline)
            raw_data = self.serial_port.readline()
            if not raw_data:
                return None
                
            # Decode and strip whitespace
            line = raw_data.decode('ascii', errors='ignore').strip()
            if not line:
                return None
                
            # Must start with { to be valid JSON
            if not line.startswith('{'):
                self._log('debug', f'Non-JSON data received: {line}')
                return None
                
            # Parse JSON
            try:
                data = json.loads(line)
            except json.JSONDecodeError as e:
                self._log('warning', f'Invalid JSON from ESP32: {line} - {e}')
                return None
                
            # Check for passthrough REQUEST (string like "remote 60")
            # vs passthrough STATUS (integer 0 or 1)
            if 'passthrough' in data:
                pt_value = data['passthrough']
                
                # Passthrough REQUEST: {"passthrough":"remote XX"}
                if isinstance(pt_value, str) and 'remote' in pt_value.lower():
                    # Extract timeout from "remote XX" format
                    match = re.search(r'remote\s*(\d+)?', pt_value, re.IGNORECASE)
                    if match:
                        timeout_str = match.group(1)
                        timeout_minutes = int(timeout_str) if timeout_str else 60
                        self._log('info', f'Passthrough REQUEST detected: {timeout_minutes} min')
                        
                        # Handle the passthrough request (sends ready, starts PPP)
                        self.handle_passthrough_request(timeout_minutes)
                        return data  # Exit early - serial will be closed
                
                # Passthrough STATUS: {"passthrough":0} or {"passthrough":1}
                else:
                    try:
                        self.esp32_passthrough = int(pt_value)
                    except (ValueError, TypeError):
                        self._log('warning', f'Invalid passthrough value: {pt_value}')
            
            # Extract and store other values (normal status message)
            old_passthrough = self.esp32_passthrough
            
            if 'datetime' in data:
                self.esp32_datetime = data['datetime']
                
            if 'sdcard' in data:
                self.esp32_sdcard_status = data['sdcard']
            
            # Parse LTE connection status
            if 'lte' in data:
                self.esp32_lte_connected = bool(int(data['lte']))
            
            # Parse signal quality (RSRP and RSRQ)
            if 'rsrp' in data:
                try:
                    self.esp32_rsrp = float(data['rsrp'])
                except (ValueError, TypeError):
                    self.esp32_rsrp = None
                    
            if 'rsrq' in data:
                try:
                    self.esp32_rsrq = float(data['rsrq'])
                except (ValueError, TypeError):
                    self.esp32_rsrq = None
                
            self.esp32_last_update = time.time()
            
            self._log('debug', f'ESP32 status: datetime={self.esp32_datetime}, '
                              f'sdcard={self.esp32_sdcard_status}, '
                              f'passthrough={self.esp32_passthrough}, '
                              f'lte={self.esp32_lte_connected}, '
                              f'rsrp={self.esp32_rsrp}, rsrq={self.esp32_rsrq}')
            
            # Trigger callback if passthrough status changed
            if self.esp32_passthrough != old_passthrough:
                self._log('warning', f'PASSTHROUGH STATUS CHANGED: {old_passthrough} -> {self.esp32_passthrough}')
                if self.on_passthrough_change:
                    try:
                        self.on_passthrough_change(old_passthrough, self.esp32_passthrough)
                    except Exception as e:
                        self._log('error', f'Error in passthrough callback: {e}')
                        
            return data
            
        except serial.SerialException as e:
            self._log('error', f'Serial read error: {e}')
            return None
        except Exception as e:
            self._log('error', f'Error receiving ESP32 status: {e}')
            return None

    def is_passthrough_active(self):
        '''Check if ESP32 is currently in passthrough mode'''
        return self.esp32_passthrough == 1
    
    def is_lte_connected(self):
        '''Check if ESP32 reports LTE connection is active'''
        return self.esp32_lte_connected
    
    def get_signal_quality(self):
        '''
        Get cellular signal quality information.
        
        Returns:
            dict: Signal quality data, or None if not available
            
        Example:
            {
                'rsrp': -85.5,       # dBm (signal strength)
                'rsrq': -10.2,       # dB (signal quality)
                'quality': 'Good',   # Human-readable quality
                'lte_connected': True
            }
            
        Signal Quality Interpretation:
            RSRP (Reference Signal Received Power):
                >= -80 dBm: Excellent
                -80 to -90 dBm: Good
                -90 to -100 dBm: Fair
                < -100 dBm: Poor
                
            RSRQ (Reference Signal Received Quality):
                >= -10 dB: Good
                -10 to -15 dB: Fair
                < -15 dB: Poor
        '''
        if self.esp32_rsrp is None and self.esp32_rsrq is None:
            return None
            
        # Determine quality description
        quality = 'Unknown'
        if self.esp32_rsrp is not None:
            if self.esp32_rsrp >= -80:
                quality = 'Excellent'
            elif self.esp32_rsrp >= -90:
                quality = 'Good'
            elif self.esp32_rsrp >= -100:
                quality = 'Fair'
            else:
                quality = 'Poor'
        
        return {
            'rsrp': self.esp32_rsrp,
            'rsrq': self.esp32_rsrq,
            'quality': quality,
            'lte_connected': self.esp32_lte_connected
        }
        
    def get_esp32_status(self):
        '''
        Get current ESP32 status as dictionary.
        
        Returns:
            dict: Current status values, or None if never received
            
        Example:
            {
                'datetime': '2026-01-29 12:34:56',
                'sdcard': 'OK',
                'passthrough': 0,
                'lte_connected': True,
                'rsrp': -85.5,
                'rsrq': -10.2,
                'signal_quality': 'Good',
                'last_update': 1738171234.567,
                'age_seconds': 5.2
            }
        '''
        if self.esp32_last_update is None:
            return None
        
        # Determine signal quality description based on RSRP
        # RSRP: -80 or better = Excellent, -80 to -90 = Good, -90 to -100 = Fair, below -100 = Poor
        signal_quality = 'Unknown'
        if self.esp32_rsrp is not None:
            if self.esp32_rsrp >= -80:
                signal_quality = 'Excellent'
            elif self.esp32_rsrp >= -90:
                signal_quality = 'Good'
            elif self.esp32_rsrp >= -100:
                signal_quality = 'Fair'
            else:
                signal_quality = 'Poor'
            
        return {
            'datetime': self.esp32_datetime,
            'sdcard': self.esp32_sdcard_status,
            'passthrough': self.esp32_passthrough,
            'lte_connected': self.esp32_lte_connected,
            'rsrp': self.esp32_rsrp,
            'rsrq': self.esp32_rsrq,
            'signal_quality': signal_quality,
            'last_update': self.esp32_last_update,
            'age_seconds': round(time.time() - self.esp32_last_update, 1)
        }

    # -------------------------------------------------------------------------
    # Data Collection (from Kivy app)
    # -------------------------------------------------------------------------

    def _get_device_id(self):
        '''Get device name from app'''
        return self.data_handler.app.device_name or 'RND-9999'

    def _get_run_cycles(self):
        '''Get run cycle count'''
        try:
            return int(self.data_handler.app.current_run_cycle_count or 0)
        except (ValueError, TypeError, AttributeError):
            return 0

    def _get_pressure(self):
        '''Get pressure value as float'''
        try:
            raw = self.data_handler.app.current_pressure
            if raw:
                return round(float(str(raw).replace(' IWC', '')), 2)
            return 0.0
        except (ValueError, AttributeError):
            return 0.0

    def _get_current(self):
        '''Get motor current as float'''
        try:
            raw = self.data_handler.app.current_amps
            if raw:
                return round(float(str(raw).replace(' A', '')), 2)
            return 0.0
        except (ValueError, AttributeError):
            return 0.0

    def _get_cpu_temp(self):
        '''Get Raspberry Pi CPU temperature'''
        try:
            with open('/sys/class/thermal/thermal_zone0/temp', 'r') as f:
                return round(float(f.read()) / 1000.0, 1)
        except Exception:
            return 0.0

    def _get_active_alarms(self):
        '''Get active alarms as bitmask'''
        try:
            active_alarms = self.data_handler.app.get_active_alarm_names()
            return sum(
                num for name, num in self.ALARM_MAP.items()
                if name in active_alarms
            )
        except Exception:
            return 0

    def _get_mode(self):
        '''Get current operating mode'''
        return self.data_handler.get_current_mode()

    # -------------------------------------------------------------------------
    # Payload Creation
    # -------------------------------------------------------------------------

    def create_payload(self):
        '''Create payload dict from sensor data'''
        try:
            return {
                'gmid': self._get_device_id(),
                'press': self._get_pressure(),
                'mode': self._get_mode(),
                'current': self._get_current(),
                'fault': self._get_active_alarms(),
                'cycles': self._get_run_cycles(),
                'temp': self._get_cpu_temp()
            }
        except Exception as e:
            self._log('error', f'Error creating payload: {e}')
            return None

    def _format_json(self, payload):
        '''Format payload as compact JSON string for ESP32'''
        data = {
            'type': 'data',
            'gmid': payload['gmid'],
            'press': payload['press'],
            'mode': payload['mode'],
            'current': payload['current'],
            'fault': payload['fault'],
            'cycles': payload['cycles']
        }
        return json.dumps(data, separators=(',', ':'))

    # -------------------------------------------------------------------------
    # Data Transmission
    # -------------------------------------------------------------------------

    def send_data(self, payload):
        '''Send payload over serial to ESP32'''
        if not payload:
            return False

        # Ensure serial port is open
        if not self.serial_port or not self.serial_port.is_open:
            if not self._initialize_serial():
                return False

        try:
            json_string = self._format_json(payload)
            bytes_written = self.serial_port.write(json_string.encode('ascii'))
            self.serial_port.flush()
            self._log('debug', f'Sent {bytes_written} bytes: {json_string}')
            return True

        except serial.SerialException as e:
            self._log('error', f'Serial write error: {e}')
            self._close_serial()
            return False
        except Exception as e:
            self._log('error', f'Error sending data: {e}')
            return False

    # -------------------------------------------------------------------------
    # Update Cycle
    # -------------------------------------------------------------------------

    def _receive_cycle(self, *args):
        '''Check for incoming data from ESP32 (runs every 1 second)'''
        # Skip if PPP is active (serial port is in use by pppd)
        if self.ppp_active:
            return
            
        try:
            self.receive_esp32_status()
        except Exception as e:
            self._log('error', f'Error in receive cycle: {e}')
    
    def _send_cycle(self, *args):
        '''Send sensor data to ESP32 (runs every 15 seconds)'''
        # Skip if PPP is active (serial port is in use by pppd)
        if self.ppp_active:
            return
            
        try:
            # If passthrough mode is active, skip sending data
            # (ESP32 is bridging serial to modem, don't interfere)
            if self.is_passthrough_active():
                self._log('debug', 'Passthrough active - skipping data send')
                return
            
            # Normal operation - send sensor data to ESP32
            payload = self.create_payload()
            if payload:
                self.send_data(payload)
        except Exception as e:
            self._log('error', f'Error in send cycle: {e}')

    # -------------------------------------------------------------------------
    # Lifecycle
    # -------------------------------------------------------------------------

    def start(self):
        '''Start periodic send/receive using Kivy Clock'''
        # Initialize serial port
        self._initialize_serial()

        # Initial send after brief delay
        Clock.schedule_once(self._send_cycle, 5)

        # Schedule periodic data sending (every 15 seconds)
        self._send_event = Clock.schedule_interval(
            self._send_cycle,
            self.UPDATE_INTERVAL
        )
        
        # Schedule frequent receive checking (every 1 second)
        # This ensures passthrough mode changes are detected quickly
        self._receive_event = Clock.schedule_interval(
            self._receive_cycle,
            self.RECEIVE_INTERVAL
        )
        
        self._log('info', f'Started: send every {self.UPDATE_INTERVAL}s, receive check every {self.RECEIVE_INTERVAL}s')

    def stop(self):
        '''Stop periodic updates, PPP if running, and close serial'''
        if self._send_event:
            Clock.unschedule(self._send_event)
            self._send_event = None
        if self._receive_event:
            Clock.unschedule(self._receive_event)
            self._receive_event = None
        
        # Stop PPP if running
        if self.ppp_active:
            self._log('info', 'Stopping PPP before shutdown...')
            self._stop_ppp()
        
        self._close_serial()
        self._log('info', 'Stopped')


def main():
    '''Initialize and start serial manager (standalone test)'''
    data_handler = DataHandler()
    manager = SerialManager(data_handler)
    manager.start()


if __name__ == '__main__':
    main()
