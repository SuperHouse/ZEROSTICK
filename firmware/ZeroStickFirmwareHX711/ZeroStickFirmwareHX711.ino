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
   move from the central position. As more force is applied, the
   virtual position of the joystick is considered to change.

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
     "HX711_ADC" by Olav Kallhovd
     "Arduino TinyUSB" by Adafruit

   External dependencies. Install manually:
     "Arduino Joystick Library" by Matthew Heironimus https://github.com/MHeironimus/ArduinoJoystickLibrary

   More information:
     www.superhouse.tv/zerostick

   To do:
    -

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
     D0:  LOADCELL_X_DOUT_PIN
     D1:  LOADCELL_X_SCK_PIN
     D2:  LOADCELL_Y_DOUT_PIN
     D3:  LOADCELL_Y_SCK_PIN
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

   Copyright 2019-2020 SuperHouse Automation Pty Ltd www.superhouse.tv
*/
#define VERSION "2.2"
/*--------------------------- Configuration ---------------------------------*/
// Configuration should be done in the included file:
#include "config.h"

/*--------------------------- Libraries -------------------------------------*/
#include <HX711_ADC.h>              // Load cell amplifier
#include <Adafruit_DS3502.h>        // Digital potentiometer
#ifdef ARDUINO_SEEED_XIAO_M0
#include "Adafruit_TinyUSB.h"       // HID emulation
//#include "TinyUSB_Mouse_and_Keyboard.h"       // HID emulation

#endif
#ifdef ARDUINO_AVR_LEONARDO
#include "Mouse.h"                  // Mouse emulation
#include <Joystick.h>               // Joystick emulation
#endif

/*--------------------------- Global Variables ------------------------------*/
int16_t  g_zero_tare_offset_x    = 0;   // X axis tare correction
int16_t  g_zero_tare_offset_y    = 0;   // Y axis tare correction

int8_t   g_input_x_position      = 0;   // Most recent force reading from X axis (+/- %)
int8_t   g_input_y_position      = 0;   // Most recent force reading from Y axis (+/- %)

uint32_t g_last_mouse_time       = 0;   // When we last sent a mouse event
uint32_t g_last_joystick_time    = 0;   // When we last sent a joystick event
uint32_t g_last_digipot_time     = 0;   // When we last updated the digipot outputs

uint8_t  g_left_button_state     = 0;
uint8_t  g_right_button_state    = 0;

volatile boolean g_x_new_data_ready = 0; // Flag set in ISR when ADC says it has data
volatile boolean g_y_new_data_ready = 0; // Flag set in ISR when ADC says it has data

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
HX711_ADC scale_x(LOADCELL_X_DOUT_PIN, LOADCELL_X_SCK_PIN);
HX711_ADC scale_y(LOADCELL_Y_DOUT_PIN, LOADCELL_Y_SCK_PIN);

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

  // Average across only 1 sample for minimum latency
  scale_x.setSamplesInUse(SAMPLES_TO_AVERAGE);
  scale_y.setSamplesInUse(SAMPLES_TO_AVERAGE);

  // Start the load cells
  scale_x.begin();
  scale_y.begin();

  // Run the tare operation once at startup
  tareCellReading();

  if (scale_x.getTareTimeoutFlag())
  {
    Serial.println("Tare timeout on X, check HX711 wiring");
  }
  if (scale_y.getTareTimeoutFlag())
  {
    Serial.println("Tare timeout on Y, check HX711 wiring");
  }

  scale_x.setCalFactor(cal_value_x); // Calibration value
  scale_y.setCalFactor(cal_value_y); // Calibration value

  reportAdcSettings();

  attachInterrupt(digitalPinToInterrupt(LOADCELL_X_DOUT_PIN), xDataReadyISR, FALLING);
  attachInterrupt(digitalPinToInterrupt(LOADCELL_Y_DOUT_PIN), yDataReadyISR, FALLING);
}

/**
  Loop
*/
void loop()
{
  checkTareButton();
  checkTareStatus();
  checkMouseButtons();

  readInputPosition();

  updateMouseOutput();
  updateJoystickOutput();
  updateDigipotOutputs();
}

/**
  Report ADC performance values and settings
*/
void reportAdcSettings()
{
  Serial.print("X calibration value: ");
  Serial.println(scale_x.getCalFactor());
  Serial.print("X measured conversion time ms: ");
  Serial.println(scale_x.getConversionTime());
  Serial.print("X measured sampling rate HZ: ");
  Serial.println(scale_x.getSPS());
  Serial.print("X measured settlingtime ms: ");
  Serial.println(scale_x.getSettlingTime());

  Serial.print("Y calibration value: ");
  Serial.println(scale_y.getCalFactor());
  Serial.print("Y measured conversion time ms: ");
  Serial.println(scale_y.getConversionTime());
  Serial.print("Y measured sampling rate HZ: ");
  Serial.println(scale_y.getSPS());
  Serial.print("Y measured settlingtime ms: ");
  Serial.println(scale_y.getSettlingTime());
}

/**
  ISR for X axis load cell
*/
void xDataReadyISR() {
  if (scale_x.update()) {
    g_x_new_data_ready = 1;
  }
}

/**
  ISR for Y axis load cell
*/
void yDataReadyISR() {
  if (scale_y.update()) {
    g_y_new_data_ready = 1;
  }
}

/**
  Check if the tare button is pressed
*/
void checkTareButton()
{
  if (LOW == digitalRead(TARE_BUTTON_PIN))
  {
    // Button is pressed, do the tare!
    delay(200);
    tareCellReading();
    delay(100);
  }
}

/**
  Check if last tare operation is complete
*/
void checkTareStatus()
{
  if (scale_x.getTareStatus() == true) {
#if ENABLE_SERIAL_DEBUGGING
    Serial.println("Tare load cell 1 complete");
#endif
  }
  if (scale_y.getTareStatus() == true) {
#if ENABLE_SERIAL_DEBUGGING
    Serial.println("Tare load cell 2 complete");
#endif
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
      float mouse_x_movement = MOUSE_X_SPEED * g_input_x_position * -1;  // X axis is reversed
      float mouse_y_movement = MOUSE_Y_SPEED * g_input_y_position;

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
      usb_hid.mouseButtonPress(0, 1);
      Serial.println("down");
      g_left_button_state = LOW;
    }
  }
  if (HIGH == left_button_state)
  {
    if (LOW == g_left_button_state)
    {
      usb_hid.mouseButtonRelease(0);
      Serial.println("up");
      g_left_button_state = HIGH;
    }
  }

  uint8_t right_button_state = digitalRead(MOUSE_RIGHT_BUTTON_PIN);
  if (LOW == right_button_state)
  {
    if (HIGH == g_right_button_state)
    {
      usb_hid.mouseButtonPress(0, 2);
      Serial.println("down");
      g_right_button_state = LOW;
    }
  }
  if (HIGH == right_button_state)
  {
    if (LOW == g_right_button_state)
    {
      usb_hid.mouseButtonRelease(0);
      Serial.println("up");
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
      Joystick.setYAxis((int)scale_y_value);
      Joystick.setXAxis((int)scale_x_value * -1);  // X axis is reversed

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
      int8_t pot_position_x = 63 + (int)(g_pressure_level_x);
      int8_t pot_position_y = 63 + (int)(g_pressure_level_y);
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
  if (g_x_new_data_ready)
  {
    float g_sensor_x_raw_value = scale_x.getData();
    g_input_x_position = (int)map(g_sensor_x_raw_value, -30, 30, -100, 100); // Adjust to a percentage of full force
    g_input_x_position = constrain(g_input_x_position, -100, 100);           // Prevent going out of bounds

    if (INPUT_DEAD_SPOT_SIZE < g_input_x_position)
    {
      g_input_x_position -= INPUT_DEAD_SPOT_SIZE;
    } else if (-1 * INPUT_DEAD_SPOT_SIZE > g_input_x_position) {
      g_input_x_position += INPUT_DEAD_SPOT_SIZE;
    } else {
      g_input_x_position = 0;
    }
    g_x_new_data_ready = 0;
  }

  if (g_y_new_data_ready)
  {
    float g_sensor_y_raw_value = scale_y.getData();
    g_input_y_position = (int)map(g_sensor_y_raw_value, -30, 30, -100, 100); // Adjust to a percentage of full force
    g_input_y_position = constrain(g_input_y_position, -100, 100);           // Prevent going out of bounds
    if (INPUT_DEAD_SPOT_SIZE < g_input_y_position)
    {
      g_input_y_position -= INPUT_DEAD_SPOT_SIZE;
    } else if (-1 * INPUT_DEAD_SPOT_SIZE > g_input_y_position) {
      g_input_y_position += INPUT_DEAD_SPOT_SIZE;
    } else {
      g_input_y_position = 0;
    }
    g_y_new_data_ready = 0;
  }

#if ENABLE_SERIAL_DEBUGGING
  Serial.print(g_input_x_position);
  Serial.print("  ");
  Serial.println(g_input_y_position);
#endif
}

/**
  Reset the zero position of the load cell
*/
void tareCellReading()
{
  // Read the load cells to set the zero offset
  uint8_t loadcell_x_rdy = 0;
  uint8_t loadcell_y_rdy = 0;
  while ((loadcell_x_rdy + loadcell_y_rdy) < 2)
  { // Run startup, stabilization, and tare, both modules simultaneously
    if (!loadcell_x_rdy) loadcell_x_rdy = scale_x.startMultiple(SCALE_TARE_TIME);
    if (!loadcell_y_rdy) loadcell_y_rdy = scale_y.startMultiple(SCALE_TARE_TIME);
  }
  scale_x.tareNoDelay();
  scale_y.tareNoDelay();
#if ENABLE_SERIAL_DEBUGGING
  Serial.println("Tare complete");
#endif
}
