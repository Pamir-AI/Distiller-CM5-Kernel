import machine
import utime
from eink_driver_sam import einkDSP_SAM
import _thread
from machine import WDT
import neopixel

"""
Pamir AI UART Protocol Specification
===================================================

Overview:
---------
This protocol provides a compact, efficient binary communication interface between
a Linux host system and the RP2040 microcontroller. It replaces the previous 
JSON-based approach with a fixed-size binary packet format to reduce CPU usage 
and improve reliability.

Packet Structure:
----------------
Each packet is exactly 4 bytes:
  Byte 0: Type flags (3 bits type + 5 bits subtype/data)
  Byte 1: Data byte 1
  Byte 2: Data byte 2
  Byte 3: Checksum (XOR of bytes 0-2)

Message Types (3 most significant bits of Byte 0):
-------------------------------------------------
0b000 (0x00): Button events      - Reports button state changes
0b001 (0x20): LED control        - Controls RGB LED state and animations
0b010 (0x40): Power management   - Power state and battery reporting
0b011 (0x60): Display commands   - E-ink display control
0b100 (0x80): Debug codes        - System diagnostics and errors
0b101 (0xA0): Debug text         - Debug text messages (multi-packet)
0b110 (0xC0): System commands    - System control and status
0b111 (0xE0): Extended commands  - Reserved for future expansion

Button Events (TYPE_BUTTON = 0x00):
----------------------------------
The 5 least significant bits in Byte 0 indicate which buttons are pressed:
  Bit 0: Up button     (0x01)
  Bit 1: Down button   (0x02)
  Bit 2: Select button (0x04)
  Bit 3: Power button  (0x08)
  Bit 4: Reserved

Data bytes 1-2 are reserved for future use.

Example: 0x06 0x00 0x00 0x06 = Select+Down buttons pressed

LED Control (TYPE_LED = 0x20):
-----------------------------
Byte 0 (5 LSB bits):
  Bits 0-1: LED ID (0x00 = all LEDs)
  Bits 2-3: LED mode:
    00 (0x00): Static color
    01 (0x04): Blink
    10 (0x08): Fade
    11 (0x0C): Rainbow effect
  Bit 4: Command type:
    0 (0x00): Immediate command
    1 (0x10): Sequence command

Byte 1:
  Bits 4-7: Red value (0-15)
  Bits 0-3: Green value (0-15)

Byte 2:
  Bits 4-7: Blue value (0-15)
  Bits 0-3: Value parameter (brightness or timing)

Example: 0x24 0xF0 0x0F 0xDB = Static red color at full brightness

Power Management (TYPE_POWER = 0x40):
------------------------------------
Byte 0 (5 LSB bits):
  Bits 4-5: Command type:
    00 (0x00): Power status report
    01 (0x10): Battery level report
    10 (0x20): Power mode change
    11 (0x30): Shutdown command
  Bits 0-3: Subcommand or parameters

Bytes 1-2: Command-specific data

Example: 0x70 0x00 0x00 0x70 = Shutdown command

Debug Codes (TYPE_DEBUG_CODE = 0x80):
------------------------------------
Byte 0 (5 LSB bits):
  Bits 0-4: Debug category:
    0x00: System
    0x01: Input
    0x02: Display
    0x03: Memory
    0x04: Power

Byte 1: Debug code
Byte 2: Debug parameter

Example: 0x80 0x01 0x00 0x81 = System initialized

Debug Text (TYPE_DEBUG_TEXT = 0xA0):
----------------------------------
Used for sending multi-packet debug text messages:

Byte 0 (5 LSB bits):
  Bit 4: First chunk flag (0x10)
  Bit 3: Continue flag (0x08)
  Bits 0-2: Chunk number (0-7)

Bytes 1-2: Two UTF-8 bytes of the text message

System Commands (TYPE_SYSTEM = 0xC0):
-----------------------------------
Byte 0 (5 LSB bits):
  Bits 0-4: System action:
    0x00: Ping request/response
    0x01: System reset
    0x02: Version information
    0x03: Status request
    0x04: Configuration

Bytes 1-2: Command-specific data

Example: 0xC0 0x00 0x00 0xC0 = Ping request

Usage Examples:
--------------
1. Sending button state:
   [0x03, 0x00, 0x00, 0x03] = Up+Down buttons pressed

2. Setting LED to blink green:
   [0x24, 0x0F, 0x08, 0x2B] = Blink green LED with medium brightness

3. Request system version:
   [0xC2, 0x00, 0x00, 0xC2]

4. System will respond with:
   [0xC2, 0x01, 0x00, 0xC3] = Version 1.0

Implementation Notes:
-------------------
- All packets must include the correct checksum (XOR of first 3 bytes)
- The protocol is designed for efficiency and minimal overhead
- Commands requiring more than 2 bytes of data use multi-packet sequences
- Debug text messages split across multiple packets must be reassembled by the receiver
"""

# Protocol definitions
# Message types (3 most significant bits)
TYPE_BUTTON     = 0x00  # 0b000xxxxx
TYPE_LED        = 0x20  # 0b001xxxxx  
TYPE_POWER      = 0x40  # 0b010xxxxx
TYPE_DISPLAY    = 0x60  # 0b011xxxxx
TYPE_DEBUG_CODE = 0x80  # 0b100xxxxx
TYPE_DEBUG_TEXT = 0xA0  # 0b101xxxxx
TYPE_SYSTEM     = 0xC0  # 0b110xxxxx
TYPE_EXTENDED   = 0xE0  # 0b111xxxxx
TYPE_MASK       = 0xE0  # 0b11100000

# Button event flags (5 least significant bits)
BTN_UP_MASK     = 0x01
BTN_DOWN_MASK   = 0x02
BTN_SELECT_MASK = 0x04
BTN_POWER_MASK  = 0x08

# LED control flags
LED_CMD_IMMEDIATE = 0x00
LED_CMD_SEQUENCE  = 0x10
LED_MODE_STATIC   = 0x00
LED_MODE_BLINK    = 0x04
LED_MODE_FADE     = 0x08
LED_MODE_RAINBOW  = 0x0C
LED_MODE_MASK     = 0x0C
LED_ID_ALL        = 0x00
LED_ID_MASK       = 0x03

# Debug categories
DEBUG_CAT_SYSTEM   = 0x00
DEBUG_CAT_INPUT    = 0x01
DEBUG_CAT_DISPLAY  = 0x02
DEBUG_CAT_MEMORY   = 0x03
DEBUG_CAT_POWER    = 0x04

# System actions
SYSTEM_PING        = 0x00
SYSTEM_RESET       = 0x01
SYSTEM_VERSION     = 0x02
SYSTEM_STATUS      = 0x03
SYSTEM_CONFIG      = 0x04

# Configuration
PRODUCTION = True  # For production, set to true to disable USB debug
UART_DEBUG = False # Enable UART debug messages

# Constants
PACKET_SIZE = 4
DEBOUNCE_TIME_MS = 50
WDT_TIMEOUT_MS = 2000

class PamirProtocol:
    """Handles the ultra-optimized binary protocol for Pamir devices"""
    
    def __init__(self):
        # Reset PMIC - DO NOT REMOVE THIS BLOCK
        self.pmic_enable = machine.Pin(3, machine.Pin.OUT)
        self.pmic_enable.value(0)
        utime.sleep(0.01)
        self.pmic_enable.init(mode=machine.Pin.IN)
        # END OF PMIC RESET BLOCK
        
        # Set up watchdog timer
        self.wdt = WDT(timeout=WDT_TIMEOUT_MS)
        
        # Initialize GPIO pins
        self.btn_select = machine.Pin(16, machine.Pin.IN, machine.Pin.PULL_DOWN)
        self.btn_up = machine.Pin(17, machine.Pin.IN, machine.Pin.PULL_DOWN)
        self.btn_down = machine.Pin(18, machine.Pin.IN, machine.Pin.PULL_DOWN)
        self.eink_status = machine.Pin(9, machine.Pin.OUT)
        self.eink_mux = machine.Pin(22, machine.Pin.OUT)
        self.sam_interrupt = machine.Pin(2, machine.Pin.OUT)
        self.nuke_usb = machine.Pin(19, machine.Pin.OUT, value=0)
        
        # Configure USB behavior based on production mode
        if PRODUCTION:
            self.nuke_usb.high()  # Disable SAM USB in production mode
        
        # Initialize UART for communication with host
        self.uart = machine.UART(0, baudrate=115200, tx=machine.Pin(0), rx=machine.Pin(1))
        
        # Initialize E-ink display
        self.eink_status.low()  # SOM CONTROL E-INK
        self.eink_mux.low()     # EINK OFF
        self.eink = einkDSP_SAM()
        self.eink_running = False
        
        # Initialize Neopixel LED
        self.np = neopixel.NeoPixel(machine.Pin(20), 1)  # Single LED on pin 20
        self.np_brightness = 0.5  # Default brightness 50%
        self.led_animation_running = False
        
        # Protocol state
        self.rx_buffer = bytearray(PACKET_SIZE)
        self.rx_pos = 0
        self.prev_btn_state = 0
        
        # Thread coordination
        self.eink_lock = _thread.allocate_lock()
        self.neopixel_lock = _thread.allocate_lock()
        self.uart_lock = _thread.allocate_lock()
        self.thread_handoff_complete = False
        
        # Set up button interrupts
        self.btn_select.irq(trigger=machine.Pin.IRQ_RISING | machine.Pin.IRQ_FALLING, 
                           handler=self._button_handler)
        self.btn_up.irq(trigger=machine.Pin.IRQ_RISING | machine.Pin.IRQ_FALLING, 
                       handler=self._button_handler)
        self.btn_down.irq(trigger=machine.Pin.IRQ_RISING | machine.Pin.IRQ_FALLING, 
                         handler=self._button_handler)
        self.sam_interrupt.irq(trigger=machine.Pin.IRQ_RISING, 
                              handler=self._loading_terminator)
        
        print("Pamir Protocol initialized")
        self.send_debug_code(DEBUG_CAT_SYSTEM, 0x01, 0x00)  # System initialized
    
    def debug_print(self, message):
        """Print debug messages to UART if enabled"""
        if UART_DEBUG:
            # Send as debug text packet
            self._send_debug_text(message)
        print(message)
    
    def _send_debug_text(self, text):
        """Send a debug text message over UART using protocol"""
        # Break text into chunks of 2 bytes
        text_bytes = text.encode('utf-8')
        chunks = [text_bytes[i:i+2] for i in range(0, len(text_bytes), 2)]
        
        for i, chunk in enumerate(chunks):
            # First chunk flag
            first_chunk = 0x10 if i == 0 else 0x00
            # Continue flag (more chunks follow)
            continue_flag = 0x08 if i < len(chunks) - 1 else 0x00
            # Chunk number (0-7, wrapping around)
            chunk_num = i % 8
            
            type_flags = TYPE_DEBUG_TEXT | first_chunk | continue_flag | chunk_num
            
            # Pad chunk to 2 bytes if needed
            if len(chunk) == 1:
                chunk = chunk + b'\x00'
                
            self.send_packet(type_flags, chunk[0], chunk[1] if len(chunk) > 1 else 0)
    
    def calculate_checksum(self, type_flags, data1, data2):
        """Calculate XOR checksum for packet"""
        return type_flags ^ data1 ^ data2
    
    def send_packet(self, type_flags, data1, data2):
        """Send a packet over UART"""
        checksum = self.calculate_checksum(type_flags, data1, data2)
        with self.uart_lock:
            self.uart.write(bytes([type_flags, data1, data2, checksum]))
    
    def verify_checksum(self, packet):
        """Verify packet checksum"""
        return packet[3] == (packet[0] ^ packet[1] ^ packet[2])
    
    def send_button_state(self):
        """Send current button state as a packet"""
        state = 0
        if self._get_debounced_state(self.btn_up):
            state |= BTN_UP_MASK
        if self._get_debounced_state(self.btn_down):
            state |= BTN_DOWN_MASK
        if self._get_debounced_state(self.btn_select):
            state |= BTN_SELECT_MASK
            
        # Only send if the state has changed
        if state != self.prev_btn_state:
            self.prev_btn_state = state
            self.send_packet(TYPE_BUTTON | state, 0, 0)
    
    def send_debug_code(self, category, code, param):
        """Send a debug code packet"""
        self.send_packet(TYPE_DEBUG_CODE | (category & 0x1F), code, param)
    
    def _button_handler(self, pin):
        """Handle button interrupt by sending the current state"""
        if self._debounce(pin):
            self.send_button_state()
    
    def _loading_terminator(self, pin):
        """Interrupt handler for SAM interrupt to terminate E-ink loading"""
        with self.eink_lock:
            if self.eink_running:
                self.eink_running = False
                self.eink.de_init()
    
    def _debounce(self, pin):
        """Debounce button press"""
        state = pin.value()
        utime.sleep_ms(DEBOUNCE_TIME_MS)
        return pin.value() == state
    
    def _get_debounced_state(self, pin):
        """Get debounced state of a pin"""
        return pin.value() and self._debounce(pin)
    
    def _set_led_color(self, r, g, b, brightness=None):
        """Set the LED to a specific color"""
        with self.neopixel_lock:
            if brightness is not None:
                self.np_brightness = max(0, min(1.0, brightness))
                
            actual_r = int(r * self.np_brightness)
            actual_g = int(g * self.np_brightness)
            actual_b = int(b * self.np_brightness)
            
            self.np[0] = (actual_r, actual_g, actual_b)
            self.np.write()
    
    def _process_led_packet(self, packet):
        """Process LED control packet"""
        mode = packet[0] & LED_MODE_MASK
        
        # Extract RGB values (4 bits each)
        r = (packet[1] >> 4) & 0x0F
        g = packet[1] & 0x0F
        b = (packet[2] >> 4) & 0x0F
        value = packet[2] & 0x0F
        
        # Scale RGB values to 0-255 range
        r = (r * 255) // 15
        g = (g * 255) // 15
        b = (b * 255) // 15
        
        # Set brightness based on value parameter (0-15)
        brightness = value / 15.0
        
        if mode == LED_MODE_STATIC:
            # Static color
            self._set_led_color(r, g, b, brightness)
            
        elif mode == LED_MODE_BLINK and not self.led_animation_running:
            # Start blinking in a separate thread
            self.led_animation_running = True
            _thread.start_new_thread(self._blink_led, (r, g, b, brightness, value))
            
        elif mode == LED_MODE_FADE and not self.led_animation_running:
            # Start fading in a separate thread
            self.led_animation_running = True
            _thread.start_new_thread(self._fade_led, (r, g, b, brightness, value))
    
    def _blink_led(self, r, g, b, brightness, count):
        """Blink the LED a specific number of times"""
        try:
            # Number of blinks is determined by value parameter
            blink_count = max(1, count)
            
            for i in range(blink_count * 2):
                if i % 2 == 0:
                    self._set_led_color(r, g, b, brightness)
                else:
                    self._set_led_color(0, 0, 0, 0)
                utime.sleep_ms(200)  # 200ms on/off
                
            # Always end in the off state
            self._set_led_color(0, 0, 0, 0)
            
            # Send completion notification
            self.send_debug_code(DEBUG_CAT_SYSTEM, 0x10, 0x00)  # LED task completed
        finally:
            self.led_animation_running = False
    
    def _fade_led(self, r, g, b, target_brightness, steps):
        """Fade the LED to a target brightness"""
        try:
            steps = max(1, steps)
            start_brightness = 0
            
            # Fade in
            for i in range(steps + 1):
                current = start_brightness + (target_brightness - start_brightness) * i / steps
                self._set_led_color(r, g, b, current)
                utime.sleep_ms(50)
                
            # Send completion notification
            self.send_debug_code(DEBUG_CAT_SYSTEM, 0x10, 0x00)  # LED task completed
        finally:
            self.led_animation_running = False
    
    def _process_system_packet(self, packet):
        """Process system command packet"""
        action = packet[0] & 0x1F
        command = packet[1]
        subcommand = packet[2]
        
        if action == SYSTEM_PING:
            # Respond with a ping acknowledgment
            self.send_packet(TYPE_SYSTEM | SYSTEM_PING, 0, 0)
            
        elif action == SYSTEM_RESET:
            # Perform a system reset
            self.send_debug_code(DEBUG_CAT_SYSTEM, 0x02, 0x00)  # System resetting
            utime.sleep_ms(100)
            machine.reset()
            
        elif action == SYSTEM_VERSION:
            # Send version information
            major = 1
            minor = 0
            self.send_packet(TYPE_SYSTEM | SYSTEM_VERSION, major, minor)
    
    def _process_power_packet(self, packet):
        """Process power management packet"""
        command = packet[0] & 0x30  # Extract command bits
        
        if command == 0x30:  # POWER_CMD_SHUTDOWN
            # Shutdown sequence
            self.debug_print("Processing shutdown command")
            self.eink_status.low()
            self.eink_mux.low()
            
            if PRODUCTION:
                self.nuke_usb.low()
                
            with self.eink_lock:
                self.eink_running = False
    
    def process_packet(self, packet):
        """Process a complete packet"""
        # Verify checksum
        if not self.verify_checksum(packet):
            self.send_debug_code(DEBUG_CAT_SYSTEM, 0xFF, 0x01)  # Checksum error
            return
        
        # Process based on type
        packet_type = packet[0] & TYPE_MASK
        
        if packet_type == TYPE_LED:
            self._process_led_packet(packet)
        elif packet_type == TYPE_SYSTEM:
            self._process_system_packet(packet)
        elif packet_type == TYPE_POWER:
            self._process_power_packet(packet)
            
    def check_uart(self):
        """Check for and process any available UART data"""
        if self.uart.any():
            data = self.uart.read(self.uart.any())
            
            for byte in data:
                # Store byte in buffer
                if self.rx_pos < PACKET_SIZE:
                    self.rx_buffer[self.rx_pos] = byte
                    self.rx_pos += 1
                
                # Process packet when complete
                if self.rx_pos == PACKET_SIZE:
                    self.process_packet(self.rx_buffer)
                    self.rx_pos = 0
    
    def run_eink_task(self):
        """Run the E-ink initialization and animation task"""
        try:
            with self.eink_lock:
                self.eink_running = True
                
            if not self.eink.init:
                self.eink.re_init()
                
            self.eink_status.high()  # Power on E-ink
            self.eink_mux.high()     # SAM controls E-ink
            
            self.eink.epd_init_fast()
            try:
                self.eink.PIC_display(None, './loading1.bin')
            except OSError:
                self.debug_print("Loading files not found")
                with self.eink_lock:
                    self.eink_running = False
                return
                
            repeat = 0
            while repeat < 3:
                with self.eink_lock:
                    if not self.eink_running:
                        break
                        
                self.eink.epd_init_part()
                self.eink.PIC_display('./loading1.bin', './loading2.bin')
                self.eink.epd_init_part()
                self.eink.PIC_display('./loading2.bin', './loading1.bin')
                self.wdt.feed()
                repeat += 1
            
            self.eink.de_init()
            with self.eink_lock:
                self.eink_running = False
                
            self.eink_mux.low()
            self.debug_print("E-ink task completed")
            
        except Exception as e:
            self.debug_print(f"E-ink exception: {str(e)}")
            self.eink.de_init()
            with self.eink_lock:
                self.eink_running = False
            self.eink_mux.low()
    
    def uart_task(self):
        """Main UART handling loop"""
        self.debug_print("Starting UART handler")
        
        while True:
            self.check_uart()
            self.wdt.feed()
            utime.sleep_ms(1)
    
    def run(self):
        """Main entry point - starts E-ink and UART handling"""
        # Show startup indication on LED
        self._set_led_color(0, 255, 0, 0.3)  # Green at 30%
        utime.sleep_ms(250)
        self._set_led_color(0, 0, 0, 0)      # Off
        
        # Start E-ink task in separate thread
        _thread.start_new_thread(self._core1_task, ())
        
        # Main loop for shutdown monitoring
        while True:
            self.wdt.feed()
            
            # Check for simultaneous UP+SELECT press to trigger shutdown
            if self.thread_handoff_complete:
                if self._get_debounced_state(self.btn_up) and self.btn_select.value() == 1:
                    start_time = utime.ticks_ms()
                    while utime.ticks_diff(utime.ticks_ms(), start_time) < 10000:
                        if self.btn_up.value() == 0 or self.btn_select.value() == 0:
                            break
                        if utime.ticks_diff(utime.ticks_ms(), start_time) >= 2000:
                            # Send shutdown packet
                            self.send_packet(TYPE_POWER | 0x30, 0, 0)  # POWER_SHUTDOWN command
                        self.wdt.feed()
                        utime.sleep_ms(10)
                    
                    # After 10 seconds, actually shut down hardware
                    if utime.ticks_diff(utime.ticks_ms(), start_time) >= 10000:
                        self.eink_status.low()
                        self.eink_mux.low()
                        
                        # Send USB control message (legacy format)
                        self.uart.write(b"xSAM_USB\n")
                        
                        if PRODUCTION:
                            self.nuke_usb.low()
                            
                        with self.eink_lock:
                            self.eink_running = False
            
            utime.sleep_ms(1)
    
    def _core1_task(self):
        """Combined task for E-ink initialization and UART handling"""
        # First run the E-ink task
        self.run_eink_task()
        
        # Signal that E-ink is done and we can transition to UART
        self.thread_handoff_complete = True
        
        # Now handle UART indefinitely
        self.uart_task()

# Initialize and run protocol handler
if __name__ == "__main__":
    protocol = PamirProtocol()
    
    try:
        protocol.run()
    except KeyboardInterrupt:
        protocol._set_led_color(0, 0, 0, 0)  # Turn off LED
        print("Protocol handler stopped") 

