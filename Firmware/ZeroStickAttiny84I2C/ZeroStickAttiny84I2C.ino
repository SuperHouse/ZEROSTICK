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
#define VERSION "1.0"
/*--------------------------- Configuration ---------------------------------*/
// Configuration should be done in the included file:
#include "config.h"

/*--------------------------- Libraries -------------------------------------*/
#include <HX711_ADC.h>              // Load cell amplifier

/*--------------------------- Global Variables ------------------------------*/
int16_t  g_zero_tare_offset_x    = 0;   // X axis tare correction
int16_t  g_zero_tare_offset_y    = 0;   // Y axis tare correction

int8_t   g_input_x_position      = 0;   // Most recent force reading from X axis (+/- %)
int8_t   g_input_y_position      = 0;   // Most recent force reading from Y axis (+/- %)

volatile boolean g_x_new_data_ready = 0; // Flag set in ISR when ADC says it has data
volatile boolean g_y_new_data_ready = 0; // Flag set in ISR when ADC says it has data

/*--------------------------- Function Signatures ---------------------------*/
/*
  void     readInputPosition();
  int16_t  getScaledLoadCellValueX();
  int16_t  getScaledLoadCellValueY(); */

/*--------------------------- Instantiate Global Objects --------------------*/
// Load cells
HX711_ADC scale_x(LOADCELL_X_DOUT_PIN, LOADCELL_X_SCK_PIN);
HX711_ADC scale_y(LOADCELL_Y_DOUT_PIN, LOADCELL_Y_SCK_PIN);

/*--------------------------- Program ---------------------------------------*/
/**
  Setup
*/
void setup()
{
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
    //Serial.println("Tare timeout on X, check HX711 wiring");
    // Set a tare status flag in a register
  }
  if (scale_y.getTareTimeoutFlag())
  {
    //Serial.println("Tare timeout on Y, check HX711 wiring");
    // Set a tare status flag in a register
  }

  scale_x.setCalFactor(cal_value_x); // Calibration value
  scale_y.setCalFactor(cal_value_y); // Calibration value

  GIMSK = 0b00100000;    // turns on pin change interrupts
  PCMSK = 0b00000101;    // turn on interrupts on pins PB0 and PB2
  sei();                 // enables interrupts

  attachInterrupt(digitalPinToInterrupt(LOADCELL_X_DOUT_PIN), xDataReadyISR, FALLING);
  attachInterrupt(digitalPinToInterrupt(LOADCELL_Y_DOUT_PIN), yDataReadyISR, FALLING);
}

/**
  Loop
*/
void loop()
{
  checkTareStatus();
  readInputPosition();
  // updateI2cRegisters();
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
  Check if last tare operation is complete
*/
void checkTareStatus()
{
  if (scale_x.getTareStatus() == true) {
    // Set a flag for tare status
  }
  if (scale_y.getTareStatus() == true) {
    // Set a flag for tare status
  }
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
}
