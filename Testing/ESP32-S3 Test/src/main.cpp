#include <Arduino.h>

const int POT1_PIN = 4;

void setup() {
  Serial.begin(115200);
  analogSetPinAttenuation(POT1_PIN, ADC_11db);
}

void loop() {
  uint16_t potValue = analogRead(POT1_PIN);
  Serial.printf("Potentiometer Value: %u\n", potValue);
  delay(1000);
}