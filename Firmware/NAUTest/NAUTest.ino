#include <NAU7802.h>

NAU7802 adc = NAU7802();

void setup() {
  Serial.begin(115200);
  adc.begin();
}

void loop() {
  Serial.println(adc.readmV());
  delay(1000);
}
