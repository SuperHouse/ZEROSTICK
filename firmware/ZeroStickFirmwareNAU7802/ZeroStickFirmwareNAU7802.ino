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
#define VERSION "2.4"
/*--------------------------- Configuration ---------------------------------*/
// Configuration should be done in the included file:
#include "config.h"

/*--------------------------- Libraries -------------------------------------*/
#include <Wire.h>
#include "SparkFun_Qwiic_Scale_NAU7802_Arduino_Library.h" // Load cell amplifier
#include <Adafruit_DS3502.h>        // Digital potentiometer
#include <RunningMedian.h>          // By Rob Tillaart

#ifdef ARDUINO_SEEED_XIAO_M0
#include "Adafruit_TinyUSB.h"       // HID emulation
#endif

#ifdef ARDUINO_AVR_LEONARDO
#include "Mouse.h"                  // Mouse emulation
#include <Joystick.h>               // Joystick emulation
#endif

/*--------------------------- Global Variables ------------------------------*/
int16_t  g_zero_tare_offset_x    = -770;   // X axis tare correction
int16_t  g_zero_tare_offset_y    = -320;   // Y axis tare correction

int32_t   g_input_x_position     = 0;   // Most recent force reading from X axis (+/- %)
int32_t   g_input_y_position     = 0;   // Most recent force reading from Y axis (+/- %)

uint32_t g_last_mouse_time       = 0;   // When we last sent a mouse event
uint32_t g_last_joystick_time    = 0;   // When we last sent a joystick event
uint32_t g_last_digipot_time     = 0;   // When we last updated the digipot outputs

uint8_t  g_left_button_state     = 0;
uint8_t  g_right_button_state    = 0;

//float    g_x_calibration_factor  = 0.0;
//float    g_y_calibration_factor  = 0.0;
int32_t  g_x_zero_offset         = 0;
int32_t  g_y_zero_offset         = 0;
uint8_t  g_next_channel_to_read  = 0;   // 0 = X axis, 1 = Y axis
uint8_t  g_channel_read_count    = 0;   // Accumulate how many times channel has been read
int32_t  g_sensor_raw_value_sum  = 0;   // Accumulate channel readings

#ifdef ARDUINO_SEEED_XIAO_M0
//  HID report descriptor using TinyUSB's template.
//  Single Report (no ID) descriptor
uint8_t const g_hid_descriptor_report[] =
{
  TUD_HID_REPORT_DESC_MOUSE()
};
#endif

/*--------------------------- Function Signatures ---------------------------*/
/*
  void     readInputPosition();
  int16_t  getScaledLoadCellValueX();
  int16_t  getScaledLoadCellValueY(); */

/*--------------------------- Instantiate Global Objects --------------------*/
// Load cells
NAU7802 loadcells;

RunningMedian x_samples = RunningMedian(5);
RunningMedian y_samples = RunningMedian(5);

#ifdef ARDUINO_SEEED_XIAO_M0
Adafruit_USBD_HID usb_hid;
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


#define TCAADDR 0x70

void tcaselect(uint8_t i) {
  if (i > 7) return;

  Wire.beginTransmission(TCAADDR);
  Wire.write(1 << i);
  Wire.endTransmission();
}

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

  if (ARDUINO_SEEED_XIAO_M0)
  {
    Serial.println("Xiao");
  }

  Wire.begin();
  //tcaselect(2);

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

  digipot_x.setWiper(63);               // Start with pots in central position
  digipot_y.setWiper(63);               // Start with pots in central position
#endif

  // Start the load cell interface
  Serial.print("Starting load cell interface: ");
  if (loadcells.begin() == false)
  {
    Serial.println("Not detected. Halting.");
    while (1);
  }
  Serial.println("ok.");
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
  //Serial.println(millis());
  checkTareButton();
  checkMouseButtons();

  readInputPosition();

  updateMouseOutput();
  updateJoystickOutput();
  updateDigipotOutputs();
}

/**
  Check if the tare button is pressed
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
  Send mouse movements to the computer
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

      //Serial.println("Moving");
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

*/
void checkMouseButtons()
{
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
#ifdef ARDUINO_SEEED_XIAO_M0
      usb_hid.mouseButtonRelease(0);
#endif
#ifdef ARDUINO_AVR_LEONARDO
      Mouse.release(MOUSE_RIGHT);
#endif
      Serial.println("release");
      g_right_button_state = HIGH;
    }
  }
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
      Joystick.setYAxis((int)g_input_y_position);
      Joystick.setXAxis((int)g_input_x_position * -1);  // X axis is reversed

      g_last_joystick_time = millis();
    }
  }
#endif
}

/**
  Send mouse movements to the wheelchair
*/
void updateDigipotOutputs()
{
#if ENABLE_DIGIPOT_OUTPUT
  if (digitalRead(DISABLE_PIN) == HIGH) // Pull this pin low to disable
  {
    if (millis() > g_last_digipot_time + DIGIPOT_INTERVAL)
    {
      int8_t pot_position_x = 63 + (int)(g_input_x_position); //(g_pressure_level_x);
      int8_t pot_position_y = 63 + (int)(g_input_y_position); //(g_pressure_level_y);
      digipot_x.setWiper(pot_position_x);
      digipot_y.setWiper(pot_position_y);

      Serial.print("X: ");
      Serial.print(pot_position_x);
      Serial.print("    Y: ");
      Serial.println(pot_position_y);
      g_last_digipot_time = millis();
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
      g_input_x_position = map(g_input_x_position, -2800, 2800, -100, 100); // Adjust to a percentage of full force
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
      g_input_y_position = map(g_input_y_position, -2800, 2800, -100, 100); // Adjust to a percentage of full force
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

#if ENABLE_SERIAL_DEBUGGING2
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
#if ENABLE_SERIAL_DEBUGGING
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
#if ENABLE_SERIAL_DEBUGGING
  Serial.print("Y zero offset: ");
  Serial.println(loadcells.getZeroOffset());
  Serial.print("Y calibration factor: ");
  Serial.println(loadcells.getCalibrationFactor());
#endif

  loadcells.setChannel(g_next_channel_to_read); // To leave the selected channel in the correct state

#if ENABLE_SERIAL_DEBUGGING
  Serial.println("Tare complete");
#endif
}
