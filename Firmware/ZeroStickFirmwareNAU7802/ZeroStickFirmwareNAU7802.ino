/**
   "Zero Deflection" Assistive Technology Joystick

   Uses strain gauges to measure force on a joystick that doesn't
   need to deflect. The joystick output is taken from the force
   applied to it, instead of the physical deflection of the stick.
   This allows it to be used by people with limited range of motion.

   The sensitivity of the joystick can be adjusted in software by
   changing the scaling of the values detected by the strain
   gauges.

   The term "position" is used through the code to signify the
   virtual position of the joystick, even though it doesn't physically
   move. As more force is applied, the virtual position of the
   joystick is considered to change.

   Position is measured as % deflection from the zero (central)
   position, ie: from -100% to +100%. This is equivalent to full
   effort by the user in each direction.

                     Y axis
                     +100%
                       ^
                       |
    X axis   -100% <-- 0 --> +100%
                       |
                       v
                     -100%

   These percentages are scaled to suit different output methods,
   including mouse emulation and joystick emulation.

   Internal dependencies. Install using Arduino library manager:
     "SparkFun Qwiic Scale NAU7802" by SparkFun Electronics
     "Arduino TinyUSB" by Adafruit

   External dependencies. Install manually:
     "Arduino Joystick Library" by Matthew Heironimus https://github.com/MHeironimus/ArduinoJoystickLibrary

   NOTE: The official Joystick library doesn't support SAMD, so if
   you want to enable joystick output on a board such as the Seeed
   XIAO you must download and install this patched version:  
     https://github.com/JingleheimerSE/ArduinoJoystickLibrary

   Compiling for mouse output:  
     For Seeed XIAO, select "Tools -> USB Stack -> TinyUSB"

   Compiling for joystick output:  
     For Seeed XIAO, select "Tools -> USB Stack -> Arduino"

   Handy utility to test game controller emulation:  
     https://gamepad-tester.com/

   More information:  
     www.superhouse.tv/zerostick

   To do:  
    - Zero offsets are currently hard-coded. These should be set at startup
      and when tare is run.
    - Do we need setZeroOffset() after calculateZeroOffset() in setup?

   Leonardo pin assignments:
     A0:
     A1:
     A2:
     A3:
     A4:
     A5:
     D0:
     D1:
     D2:  SDA
     D3:  SCL
     D4:  LOADCELL_X_SCK_PIN
     D5:  LOADCELL_X_DOUT_PIN
     D6:  LOADCELL_Y_SCK_PIN
     D7:  LOADCELL_Y_DOUT_PIN
     D8:  Disable pin
     D9:
     D10:
     D11:
     D12:
     D13: LED_PIN

   XIAO pin assignments:
     D0:
     D1:
     D2:
     D3:
     D4:  SDA
     D5:  SCL
     D6:  Tare pin
     D7:  Disable pin
     D8:
     D9:
     D10:

   227g (Pocophone) = 920000 reading

   By:
    Chris Fryer <chris.fryer78@gmail.com>
    Jonathan Oxer <jon@oxer.com.au>

   Copyright 2019-2021 SuperHouse Automation Pty Ltd www.superhouse.tv
*/
#define VERSION "2.5"
/*--------------------------- Configuration ---------------------------------*/
// Configuration should be done in the included file:
#include "config.h"

/*--------------------------- Libraries -------------------------------------*/
#include <Wire.h>
#include "SparkFun_Qwiic_Scale_NAU7802_Arduino_Library.h" // Load cell amplifier
#include <RunningMedian.h>          // By Rob Tillaart

// Conditionally include the correct library based on target architecture
#if ENABLE_MOUSE_OUTPUT
#ifdef ARDUINO_SEEED_XIAO_M0
#include "Adafruit_TinyUSB.h"       // HID emulation
#endif
#ifdef ARDUINO_AVR_LEONARDO
#include "Mouse.h"                  // Mouse emulation
#endif
#endif

#if ENABLE_JOYSTICK_OUTPUT
#include <Joystick.h>               // Joystick emulation
#endif

#if ENABLE_DIGIPOT_OUTPUT
#include <Adafruit_DS3502.h>        // Digital potentiometer
#endif

/*--------------------------- Global Variables ------------------------------*/
int16_t  g_zero_tare_offset_x    = -770;   // X axis tare correction
int16_t  g_zero_tare_offset_y    = -320;   // Y axis tare correction

int32_t  g_input_x_position      = 0;   // Most recent force reading from X axis (+/- %)
int32_t  g_input_y_position      = 0;   // Most recent force reading from Y axis (+/- %)

uint32_t g_last_mouse_time       = 0;   // When we last sent a mouse event
uint32_t g_last_joystick_time    = 0;   // When we last sent a joystick event
uint32_t g_last_digipot_time     = 0;   // When we last updated the digipot outputs

uint8_t  g_left_button_state     = 0;
uint8_t  g_right_button_state    = 0;

int32_t  g_x_zero_offset         = 0;
int32_t  g_y_zero_offset         = 0;
uint8_t  g_next_channel_to_read  = 0;   // 0 = X axis, 1 = Y axis
uint8_t  g_channel_read_count    = 0;   // Accumulate how many times channel has been read
int32_t  g_sensor_raw_value_sum  = 0;   // Accumulate channel readings

#if ENABLE_MOUSE_OUTPUT
#ifdef ARDUINO_SEEED_XIAO_M0
//  HID report descriptor using TinyUSB's template.
//  Single Report (no ID) descriptor
uint8_t const g_hid_descriptor_report[] =
{
  TUD_HID_REPORT_DESC_MOUSE()
};
#endif
#endif

/*--------------------------- Function Signatures ---------------------------*/


/*--------------------------- Instantiate Global Objects --------------------*/
// Load cells
NAU7802 loadcells;

RunningMedian x_samples = RunningMedian(5);
RunningMedian y_samples = RunningMedian(5);

#if ENABLE_MOUSE_OUTPUT
#ifdef ARDUINO_SEEED_XIAO_M0
Adafruit_USBD_HID usb_hid;
#endif
#endif

#if ENABLE_JOYSTICK_OUTPUT
// Configure the virtual joystick device
Joystick_ Joystick(JOYSTICK_DEFAULT_REPORT_ID, JOYSTICK_TYPE_GAMEPAD,
                   1, 0,                  // Button Count, Hat Switch Count
                   true, true, false,     // X and Y, but no Z Axis
                   false, false, false,   // No Rx, Ry, or Rz
                   false, false,          // No rudder or throttle
                   false, false, false);  // No accelerator, brake, or steering
#endif

#if ENABLE_DIGIPOT_OUTPUT
Adafruit_DS3502 digipot_x = Adafruit_DS3502();
Adafruit_DS3502 digipot_y = Adafruit_DS3502();
#endif

/*--------------------------- Program ---------------------------------------*/
/**
  Setup
*/
void setup()
{
  Serial.begin(SERIAL_BAUD_RATE);
  //while (!Serial) {
  //  ; // wait for serial port to connect. Needed for native USB port only
  //}
  Serial.print("ZeroStick starting up, v");
  Serial.println(VERSION);

#ifdef ARDUINO_SEEED_XIAO_M0
  Serial.println("Xiao");
#endif

  Wire.begin();

  pinMode(DISABLE_PIN,            INPUT_PULLUP);
  pinMode(TARE_BUTTON_PIN,        INPUT_PULLUP);
  pinMode(MOUSE_LEFT_BUTTON_PIN,  INPUT_PULLUP);
  pinMode(MOUSE_RIGHT_BUTTON_PIN, INPUT_PULLUP);

#if ENABLE_MOUSE_OUTPUT
#ifdef ARDUINO_SEEED_XIAO_M0
  usb_hid.setPollInterval(2);
  usb_hid.setReportDescriptor(g_hid_descriptor_report, sizeof(g_hid_descriptor_report));
  usb_hid.begin();
  // wait until device mounted
  while ( !USBDevice.mounted() ) delay(1);
#endif
#endif

#if ENABLE_JOYSTICK_OUTPUT
  // Initialise joystick emulation
  Joystick.begin();
  Joystick.setXAxisRange(-JOYSTICK_AXIS_RANGE, JOYSTICK_AXIS_RANGE);
  Joystick.setYAxisRange(-JOYSTICK_AXIS_RANGE, JOYSTICK_AXIS_RANGE);
#endif

#if ENABLE_DIGIPOT_OUTPUT
  if (!digipot_x.begin(DIGIPOT_X_I2C_ADDR)) {
    Serial.println("Couldn't find X-axis DS3502 chip");
  }
  Serial.println("Found X-axis DS3502 chip");

  if (!digipot_y.begin(DIGIPOT_Y_I2C_ADDR)) {
    Serial.println("Couldn't find Y-axis DS3502 chip");
  }
  Serial.println("Found Y-axis DS3502 chip");

  digipot_x.setWiper(DIGIPOT_CENTER);               // Start with pots in central position
  digipot_y.setWiper(DIGIPOT_CENTER);               // Start with pots in central position
#endif

  // Start the load cell interface
#if ENABLE_INPUT_DEBUGGING
  Serial.print("Starting load cell interface: ");
#endif
  if (loadcells.begin() == false)
  {
#if ENABLE_INPUT_DEBUGGING
    Serial.println("Not detected. Halting.");
#endif
    while (1);
  }
#if ENABLE_INPUT_DEBUGGING
  Serial.println("ok.");
#endif
  loadcells.setGain(NAU7802_GAIN_1);        // Gain can be set to 1, 2, 4, 8, 16, 32, 64, or 128.
  loadcells.setSampleRate(NAU7802_SPS_320); // Sample rate can be set to 10, 20, 40, 80, or 320Hz
  delay(100);
  tareCellReadings();
}

/**
  Loop
*/
void loop()
{
  checkTareButton();
  checkMouseButtons();

  readInputPosition();

  updateMouseOutput();
  updateJoystickOutput();
  updateDigipotOutputs();
}

/**
  Check if the tare button is pressed.
*/
void checkTareButton()
{
  if (LOW == digitalRead(TARE_BUTTON_PIN))
  {
    // Button is pressed, do the tare!
    delay(100);
    tareCellReadings();
    delay(100);
  }
}

/**
  Send mouse movements to the computer.
*/
void updateMouseOutput()
{
#if ENABLE_MOUSE_OUTPUT
  if (digitalRead(DISABLE_PIN) == HIGH) // Pull this pin low to disable
  {
#ifdef ARDUINO_SEEED_XIAO_M0
    // Remote wakeup
    if ( USBDevice.suspended() )
    {
      // Wake up host if we are in suspend mode
      // and REMOTE_WAKEUP feature is enabled by host
      USBDevice.remoteWakeup();
    }
#endif

    if (millis() > g_last_mouse_time + MOUSE_INTERVAL)
    {
      // Computer screens use the top left corner as the coordinate origin. This
      // means the Y axis is inverted: plus is down, minus is up. We have to
      // reverse the Y axis value to match it to mouse movement.
      float mouse_x_movement = MOUSE_X_SPEED * g_input_x_position * 1;  // X axis is not reversed
      float mouse_y_movement = MOUSE_Y_SPEED * g_input_y_position * -1;  // Y axis is reversed

#if ENABLE_MOUSE_DEBUGGING
      Serial.println("Moving");
#endif

#ifdef ARDUINO_SEEED_XIAO_M0
      if ( usb_hid.ready() )
      {
        usb_hid.mouseMove(0, mouse_x_movement, mouse_y_movement);  // no ID
      }
#endif

#ifdef ARDUINO_AVR_LEONARDO
      Mouse.move(mouse_x_movement, mouse_y_movement);
#endif

      g_last_mouse_time = millis();
    }
  }
#endif
}

/**
  Check whether mouse buttons have been pressed or released, and
  send appropriate mouse events.
*/
void checkMouseButtons()
{
#if ENABLE_MOUSE_OUTPUT
  uint8_t left_button_state = digitalRead(MOUSE_LEFT_BUTTON_PIN);
  if (LOW == left_button_state)
  {
    if (HIGH == g_left_button_state)
    {
#ifdef ARDUINO_SEEED_XIAO_M0
      usb_hid.mouseButtonPress(0, 1);
      Serial.println("Xiao left click");
#endif
#ifdef ARDUINO_AVR_LEONARDO
      Mouse.press(MOUSE_LEFT);
      Serial.println("Leonardo left click");
#endif
      g_left_button_state = LOW;
    }
  }
  if (HIGH == left_button_state)
  {
    if (LOW == g_left_button_state)
    {
#ifdef ARDUINO_SEEED_XIAO_M0
      usb_hid.mouseButtonRelease(0);
#endif
#ifdef ARDUINO_AVR_LEONARDO
      Mouse.release(MOUSE_LEFT);
#endif
      Serial.println("release");
      g_left_button_state = HIGH;
    }
  }

  uint8_t right_button_state = digitalRead(MOUSE_RIGHT_BUTTON_PIN);
  if (LOW == right_button_state)
  {
    if (HIGH == g_right_button_state)
    {
#ifdef ARDUINO_SEEED_XIAO_M0
      usb_hid.mouseButtonPress(0, 2);
      Serial.println("Xiao right click");
#endif
#ifdef ARDUINO_AVR_LEONARDO
      Mouse.press(MOUSE_RIGHT);
      Serial.println("Leonardo right click");
#endif
      g_right_button_state = LOW;
    }
  }
  if (HIGH == right_button_state)
  {
    if (LOW == g_right_button_state)
    {
#if ENABLE_MOUSE_OUTPUT
#ifdef ARDUINO_SEEED_XIAO_M0
      usb_hid.mouseButtonRelease(0);
#endif
#ifdef ARDUINO_AVR_LEONARDO
      Mouse.release(MOUSE_RIGHT);
#endif
#endif
      Serial.println("release");
      g_right_button_state = HIGH;
    }
  }
#endif
}

/**
  Send joystick values to the computer
*/
void updateJoystickOutput()
{
#if ENABLE_JOYSTICK_OUTPUT
  if (digitalRead(DISABLE_PIN) == HIGH) // Pull this pin low to disable
  {
    if (millis() > g_last_joystick_time + JOYSTICK_INTERVAL)
    {
#if ENABLE_JOYSTICK_DEBUGGING
      Serial.print(g_input_x_position);
      Serial.print("  ");
      Serial.println(g_input_y_position);
#endif
      Joystick.setYAxis((int)g_input_y_position);
      Joystick.setXAxis((int)g_input_x_position * -1);  // X axis is reversed

      g_last_joystick_time = millis();
    }
  }
#endif
}

/**
  Send joystick movements to a pair of digital potentiometers using
  I2C. These can be connected to the joystick input of an electric
  wheelchair to simulate the normal joystick.

  Input (zerostick) position is centered at 0, with deflection to
  +/-100 full scale. This range of -100 to +100 must to mapped to the
  required range for the digital potentiometers, which is 0 to 127
  with the center point at 63.
*/
void updateDigipotOutputs()
{
#if ENABLE_DIGIPOT_OUTPUT
  if (digitalRead(DISABLE_PIN) == HIGH) // Pull this pin low to disable
  {
    if (millis() > g_last_digipot_time + DIGIPOT_INTERVAL)
    {
      /* Scale the -100 to +100 input to suit the required 0 to 127 output range */
      // X and Y are inverted for driving the pots
      int16_t pot_position_x = DIGIPOT_CENTER - (int)(g_input_x_position * DIGIPOT_SPEED * 63 / 100);
      int16_t pot_position_y = DIGIPOT_CENTER - (int)(g_input_y_position * DIGIPOT_SPEED * 63 / 100);

      /* Apply constraints to prevent going out of range on Permobil wheelchair joystick input */
      pot_position_x = constrain(pot_position_x, 50, 80);
      pot_position_y = constrain(pot_position_y, 50, 80);

      /* Update the positions of the digital potentiometers */
      digipot_x.setWiper(pot_position_x);
      digipot_y.setWiper(pot_position_y);

      g_last_digipot_time = millis();

#if ENABLE_DIGIPOT_DEBUGGING
      Serial.print(g_input_x_position);
      Serial.print("   ");
      Serial.print(g_input_y_position);
      Serial.print("   ");
      Serial.print("X: ");
      Serial.print(pot_position_x);
      Serial.print("   Y: ");
      Serial.println(pot_position_y);
#endif
    }
  }
#endif
}

/**
  Read the sensor pressure levels
*/
void readInputPosition()
{
  //Serial.println("Reading");

  if (loadcells.available() == true)
  {
    if (1 == g_next_channel_to_read) // X axis
    {
      int32_t latest_reading = loadcells.getReading();
      x_samples.add(latest_reading + g_zero_tare_offset_x);
      g_channel_read_count++;

      g_input_x_position = x_samples.getMedian();
      // LOAD_CELL_RATING_GRAMS is the spec on the load cell.
      // INPUT_FULL_SCALE_GRAMS is effort required to reach full scale.
      g_input_x_position = g_input_x_position * LOAD_CELL_RATING_GRAMS / INPUT_FULL_SCALE_GRAMS;
      g_input_x_position = map(g_input_x_position, -LOAD_CELL_FULL_SCALE, LOAD_CELL_FULL_SCALE, -100, 100); // Adjust to a percentage of full force
      g_input_x_position = constrain(g_input_x_position, -100, 100);          // Prevent going out of bounds

      if (INPUT_DEAD_SPOT_SIZE < g_input_x_position)
      {
        g_input_x_position -= INPUT_DEAD_SPOT_SIZE;
      } else if (-1 * INPUT_DEAD_SPOT_SIZE > g_input_x_position) {
        g_input_x_position += INPUT_DEAD_SPOT_SIZE;
      } else {
        g_input_x_position = 0;
      }

      g_next_channel_to_read = 0;
      loadcells.setChannel(0);
      loadcells.calibrateAFE();

    } else {                         // Y axis
      int32_t latest_reading = loadcells.getReading();
      y_samples.add(latest_reading + g_zero_tare_offset_y);
      g_channel_read_count++;

      g_input_y_position = y_samples.getMedian();
      g_input_y_position = g_input_y_position * LOAD_CELL_RATING_GRAMS / INPUT_FULL_SCALE_GRAMS;
      g_input_y_position = map(g_input_y_position, -LOAD_CELL_FULL_SCALE, LOAD_CELL_FULL_SCALE, -100, 100); // Adjust to a percentage of full force
      g_input_y_position = constrain(g_input_y_position, -100, 100);          // Prevent going out of bounds

      if (INPUT_DEAD_SPOT_SIZE < g_input_y_position)
      {
        g_input_y_position -= INPUT_DEAD_SPOT_SIZE;
      } else if (-1 * INPUT_DEAD_SPOT_SIZE > g_input_y_position) {
        g_input_y_position += INPUT_DEAD_SPOT_SIZE;
      } else {
        g_input_y_position = 0;
      }

      g_channel_read_count   = 0;
      g_next_channel_to_read = 1;
      loadcells.setChannel(1);
      loadcells.calibrateAFE();
    }

#if ENABLE_INPUT_DEBUGGING
    Serial.print(g_input_x_position);
    Serial.print("  ");
    Serial.println(g_input_y_position);
#endif
  }
}

/**
  Reset the zero position of the load cells. Select each channel in turn and tare them.
*/
void tareCellReadings()
{
  loadcells.setChannel(0);
  loadcells.calibrateAFE();                 // Internal calibration. Recommended after power up, gain changes, sample rate changes, or channel changes.
  loadcells.calculateZeroOffset(64);        // Tare operation, averaged across 64 readings
  //loadcells.setCalibrationFactor(g_x_calibration_factor);
  g_x_zero_offset = loadcells.getZeroOffset();
#if ENABLE_INPUT_DEBUGGING
  Serial.print("X zero offset: ");
  Serial.println(loadcells.getZeroOffset());
  Serial.print("X calibration factor: ");
  Serial.println(loadcells.getCalibrationFactor());
#endif

  loadcells.setChannel(1);
  loadcells.calibrateAFE();                 // Internal calibration. Recommended after power up, gain changes, sample rate changes, or channel changes.
  loadcells.calculateZeroOffset(64);        // Tare operation, averaged across 64 readings
  //loadcells.setCalibrationFactor(g_y_calibration_factor);
  g_y_zero_offset = loadcells.getZeroOffset();
#if ENABLE_INPUT_DEBUGGING
  Serial.print("Y zero offset: ");
  Serial.println(loadcells.getZeroOffset());
  Serial.print("Y calibration factor: ");
  Serial.println(loadcells.getCalibrationFactor());
#endif

  loadcells.setChannel(g_next_channel_to_read); // To leave the selected channel in the correct state

#if ENABLE_INPUT_DEBUGGING
  Serial.println("Tare complete");
#endif
}
