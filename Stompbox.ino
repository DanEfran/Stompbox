#include <FastLED.h>

#define NUM_LEDS 6
#define DATA_PIN 14
CRGB leds[NUM_LEDS];

void setup() {

  Serial.begin(57600);
	Serial.println("Stompbox resetting");

	FastLED.addLeds<WS2812,DATA_PIN,RGB>(leds,NUM_LEDS);
	FastLED.setBrightness(80);

    leds[0] = CHSV(0, 255, 255);
  	for(int i = 1; i < NUM_LEDS; i++) {
		  leds[i] = CHSV(66, 100, 211);
    }
  FastLED.show();

}

void loop() {
  // put your main code here, to run repeatedly:
  leds[4] = CHSV(66, 255, 255);
  FastLED.show();
  delay(200);
  leds[4] = CHSV(66, 100, 211);
  FastLED.show();
  delay(1700);
 }
