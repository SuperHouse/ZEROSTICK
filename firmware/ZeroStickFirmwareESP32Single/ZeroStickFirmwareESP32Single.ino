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
     "SoftWire" by Steve Marple
     "AsyncDelay" by Steve Marple

   External dependencies. Install manually:
     "Arduino Joystick Library" by Matthew Heironimus https://github.com/MHeironimus/ArduinoJoystickLibrary

   More information:
     www.superhouse.tv/zerostick

   To do:
    - Does the tare operation act on both inputs, or only the currently
      selected channel?
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
#define VERSION "2.3"
/*--------------------------- Configuration ---------------------------------*/
// Configuration should be done in the included file:
#include "config.h"

/*--------------------------- Libraries -------------------------------------*/
#include <Wire.h>
#include "SparkFun_Qwiic_Scale_NAU7802_Arduino_Library.h" // Load cell amplifier

/*--------------------------- Global Variables ------------------------------*/
//int16_t  g_zero_tare_offset_x    = 0;   // X axis tare correction
//int16_t  g_zero_tare_offset_y    = 0;   // Y axis tare correction

int32_t   g_input_x_position      = 0;   // Most recent force reading from X axis (+/- %)
int32_t   g_input_y_position      = 0;   // Most recent force reading from Y axis (+/- %)

uint32_t g_last_mouse_time       = 0;   // When we last sent a mouse event
uint32_t g_last_joystick_time    = 0;   // When we last sent a joystick event
uint32_t g_last_digipot_time     = 0;   // When we last updated the digipot outputs
uint32_t g_last_serial_time      = 0;   // When we last reported to the serial console

uint8_t  g_left_button_state     = 0;
uint8_t  g_right_button_state    = 0;

float    g_x_calibration_factor  = 1.0;
float    g_y_calibration_factor  = 1.0;
int32_t  g_x_zero_offset         = 0;
int32_t  g_y_zero_offset         = 0;
uint8_t  g_next_axis_to_read     = 0;   // 0 = X axis, 1 = Y axis
uint8_t  g_channel_read_count    = 0;   // Accumulate how many times channel has been read
int32_t  g_sensor_raw_value_sum  = 0;   // Accumulate channel readings

/*--------------------------- Function Signatures ---------------------------*/
/*
  void     readInputPosition();
  int16_t  getScaledLoadCellValueX();
  int16_t  getScaledLoadCellValueY(); */

/*--------------------------- Instantiate Global Objects --------------------*/
// Load cells
NAU7802 loadcell;

/*--------------------------- Program ---------------------------------------*/
/**
  Setup
*/
void setup()
{
  Serial.begin(SERIAL_BAUD_RATE);
  delay(1000);
  //while (!Serial) {
  //  ; // wait for serial port to connect. Needed for native USB port only
  //}
  //Serial.print("ZeroStick starting up, v");
  //Serial.println(VERSION);

  //Wire.begin();
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN); //uses default SDA and SCL and 100000HZ freq

  pinMode(DISABLE_PIN,            INPUT_PULLUP);
  pinMode(TARE_BUTTON_PIN,        INPUT_PULLUP);
  pinMode(MOUSE_LEFT_BUTTON_PIN,  INPUT_PULLUP);
  pinMode(MOUSE_RIGHT_BUTTON_PIN, INPUT_PULLUP);

  // Start the X-axis load cell interface
  //Serial.print("Starting X-axis load cell interface: ");
  if (loadcell.begin() == false)
  {
    Serial.println("Load cell not detected. Halting.");
    while (1);
  }
  //Serial.println("ok.");
  loadcell.setGain(NAU7802_GAIN_64);        // Gain can be set to 1, 2, 4, 8, 16, 32, 64, or 128.
  loadcell.setSampleRate(NAU7802_SPS_80); // Sample rate can be set to 10, 20, 40, 80, or 320Hz

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

  //updateMouseOutput();
  //updateJoystickOutput();
  //updateDigipotOutputs();
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

*/
void checkMouseButtons()
{
  uint8_t left_button_state = digitalRead(MOUSE_LEFT_BUTTON_PIN);
  if (LOW == left_button_state)
  {
    if (HIGH == g_left_button_state)
    {
      //Serial.println("press");
      g_left_button_state = LOW;
    }
  }
  if (HIGH == left_button_state)
  {
    if (LOW == g_left_button_state)
    {
      //Serial.println("up");
      g_left_button_state = HIGH;
    }
  }

  uint8_t right_button_state = digitalRead(MOUSE_RIGHT_BUTTON_PIN);
  if (LOW == right_button_state)
  {
    if (HIGH == g_right_button_state)
    {
      //Serial.println("down");
      g_right_button_state = LOW;
    }
  }
  if (HIGH == right_button_state)
  {
    if (LOW == g_right_button_state)
    {
      //Serial.println("up");
      g_right_button_state = HIGH;
    }
  }
}


/**
  Read the sensor pressure levels
*/
void readInputPosition()
{
  if (1 == g_next_axis_to_read) // X axis
  {
    if (loadcell.available() == true)
    {
      g_sensor_raw_value_sum += (loadcell.getReading() + g_x_zero_offset);
      g_channel_read_count++;

      if (SAMPLE_COUNT == g_channel_read_count)
      {
        g_input_x_position = (g_sensor_raw_value_sum / SAMPLE_COUNT);

        /*
          int32_t sensor_raw_value = (g_sensor_raw_value_sum / SAMPLE_COUNT) - g_x_zero_offset;
          //Serial.println(sensor_raw_value);
          //Serial.print("X: ");
          //Serial.println(sensor_raw_value);
          g_input_x_position = sensor_raw_value;
          //g_input_x_position = (int8_t)map(sensor_raw_value, -10000, 10000, -100, 100); // Adjust to a percentage of full force
          //g_input_x_position = constrain(g_input_x_position, -100, 100);          // Prevent going out of bounds

          if (INPUT_DEAD_SPOT_SIZE < g_input_x_position)
          {
          g_input_x_position -= INPUT_DEAD_SPOT_SIZE;
          } else if (-1 * INPUT_DEAD_SPOT_SIZE > g_input_x_position) {
          g_input_x_position += INPUT_DEAD_SPOT_SIZE;
          } else {
          g_input_x_position = 0;
          }
        */

        g_channel_read_count   = 0;
        g_sensor_raw_value_sum = 0;
        g_next_axis_to_read = 0;
        loadcell.setChannel(g_next_axis_to_read);
        loadcell.calibrateAFE();
      }
    }
  }

  if (0 == g_next_axis_to_read) // Y axis
  {
    if (loadcell.available() == true)
    {
      //Serial.println("Reading Y");
      g_sensor_raw_value_sum += (loadcell.getReading() + g_x_zero_offset);
      g_channel_read_count++;

      if (SAMPLE_COUNT == g_channel_read_count)
      {
        g_input_y_position = (g_sensor_raw_value_sum / SAMPLE_COUNT);

        /*
          int32_t sensor_raw_value = (g_sensor_raw_value_sum / SAMPLE_COUNT) - g_y_zero_offset;
          sensor_raw_value = (int)sensor_raw_value * Y_SCALING_FACTOR;
          //Serial.print("           Y:");
          //Serial.println(sensor_raw_value);
          g_input_y_position = sensor_raw_value;
          //g_input_y_position = (int8_t)map(sensor_raw_value, -10000, 10000, -100, 100); // Adjust to a percentage of full force
          //g_input_y_position = constrain(g_input_y_position, -100, 100);          // Prevent going out of bounds

          if (INPUT_DEAD_SPOT_SIZE < g_input_y_position)
          {
          g_input_y_position -= INPUT_DEAD_SPOT_SIZE;
          } else if (-1 * INPUT_DEAD_SPOT_SIZE > g_input_y_position) {
          g_input_y_position += INPUT_DEAD_SPOT_SIZE;
          } else {
          g_input_y_position = 0;
          }
        */

        g_channel_read_count   = 0;
        g_sensor_raw_value_sum = 0;
        g_next_axis_to_read = 1;
        loadcell.setChannel(g_next_axis_to_read);
        loadcell.calibrateAFE();
      }
    }
  }

#if ENABLE_SERIAL_DEBUGGING2
  if (millis() > (SERIAL_REPORT_INTERVAL + g_last_serial_time))
  {
    //Serial.print(g_input_x_position);
    //Serial.print("  ");
    Serial.print(g_input_y_position);
    Serial.println();
    g_last_serial_time = millis();
  }
#endif
}

/**
  Reset the zero position of the load cells. Select each channel in turn and tare them.
*/
void tareCellReadings()
{
  loadcell.setChannel(0);
  loadcell.calibrateAFE();                 // Internal calibration. Recommended after power up, gain changes, sample rate changes, or channel changes.
  loadcell.calculateZeroOffset(64);        // Tare operation, averaged across 64 readings
  //loadcell.setCalibrationFactor(g_x_calibration_factor);
  g_x_zero_offset = loadcell.getZeroOffset();
#if ENABLE_SERIAL_DEBUGGING
  Serial.println("\n-----------");
  Serial.print("Y zero offset: ");
  Serial.println(loadcell.getZeroOffset());
  Serial.print("Y calibration factor: ");
  Serial.println(loadcell.getCalibrationFactor());
#endif

  loadcell.setChannel(1);
  loadcell.calibrateAFE();                 // Internal calibration. Recommended after power up, gain changes, sample rate changes, or channel changes.
  loadcell.calculateZeroOffset(64);        // Tare operation, averaged across 64 readings
  //loadcell.setCalibrationFactor(g_y_calibration_factor);
  g_y_zero_offset = loadcell.getZeroOffset();
#if ENABLE_SERIAL_DEBUGGING
  Serial.print("X zero offset: ");
  Serial.println(loadcell.getZeroOffset());
  Serial.print("X calibration factor: ");
  Serial.println(loadcell.getCalibrationFactor());
  Serial.println("-----------");
#endif

  loadcell.setChannel(g_next_axis_to_read); // To leave the selected channel in the correct state
  loadcell.calibrateAFE();

#if ENABLE_SERIAL_DEBUGGING
  Serial.println("Tare complete");
#endif
}
