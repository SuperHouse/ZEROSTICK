/* ----------------- General config -------------------------------- */
/* Output options */
#define   ENABLE_SERIAL_DEBUGGING  false
#define   ENABLE_SERIAL_DEBUGGING2 true
#define   ENABLE_DIGIPOT_OUTPUT   false
#define   ENABLE_MOUSE_OUTPUT     false
#define   ENABLE_JOYSTICK_OUTPUT  false

/* Input behaviour */
#define   INPUT_DEAD_SPOT_SIZE        3  // +/- this % will be ignored
#define   SAMPLE_COUNT                5  // Average across this many samples
//#define   X_SCALING_FACTOR         (2.5)
//#define   Y_SCALING_FACTOR         (2.5)

/* Mouse behaviour */
// 0.1 is super slow. 1.0 is really fast. 0.5 is generally ok.
#define   MOUSE_X_SPEED              (0.5)
#define   MOUSE_Y_SPEED              (0.5)
#define   MOUSE_INTERVAL             10  // ms between mouse position updates

/* Joystick behaviour */
#define   JOYSTICK_INTERVAL          10  // ms between joystick position updates

/* Serial */
#define     SERIAL_BAUD_RATE     115200  // Speed for USB serial console

/* ----------------- Hardware-specific config ---------------------- */
/*
   The hardware config for this project depends on a variable set by the
   Arduino environment to detect the target board type. Currently only
   2 target boards are supported: Arduino Leonardo, and Seeeduino XIAO.

   Update the pin definitions in the section for your target board.

   This board detection method is also used in the program to load a
   different HID library and configure it differently depending on the
   board type. If using a Seeeduino XIAO, you must select Tools ->
   USB Stack -> TinyUSB.
*/

/**** Pins for Seeeduino XIAO ****/
#define   TARE_BUTTON_PIN            26  // Pull this pin to GND to tare
#define   DISABLE_PIN                 2  // Pull this pin to GND to disable
#define   MOUSE_LEFT_BUTTON_PIN      25  // Pull this pin to GND for left click
#define   MOUSE_RIGHT_BUTTON_PIN     27  // Pull this pin to GND for right click

#define   I2C_Y_SDA_PIN              23
#define   I2C_Y_SCL_PIN              19

#define   I2C_X_SDA_PIN              16
#define   I2C_X_SCL_PIN              17


/* I2C addresses */
#define   LOADCELL_I2C_ADDR        0x2A  // Currently ignoring this!
