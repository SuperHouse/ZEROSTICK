/* ----------------- General config -------------------------------- */
/* I2C options */
#define   I2C_SLAVE_ADDRESS     0x4

/* Input behaviour */
#define   INPUT_DEAD_SPOT_SIZE        2  // +/- this % will be ignored

/* Mouse behaviour */
// 0.1 is super slow. 1.0 is really fast. 0.5 is generally ok.
#define   MOUSE_X_SPEED              (0.5)
#define   MOUSE_Y_SPEED              (0.5)

/* Joystick behaviour */
//#define JOYSTICK_AXIS_RANGE          50

float cal_value_x               = 10000.0; // ?
float cal_value_y               = 10000.0; // ?

#define   SCALE_TARE_TIME          2000  // The load cells need time before the first reading
#define   SAMPLES_TO_AVERAGE          1  // Must be a simple binary multiple (1, 2, 4, 8, 16, etc)

/* ----------------- Hardware-specific config ---------------------- */

/* Load cell connections */
#define   LOADCELL_X_DOUT_PIN       PA0
#define   LOADCELL_X_SCK_PIN        PA1
#define   LOADCELL_Y_DOUT_PIN       PA2
#define   LOADCELL_Y_SCK_PIN        PA3
