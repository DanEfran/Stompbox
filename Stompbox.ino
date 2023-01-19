/*

Stompbox.ino
Dan Efran, 2023

A multiple foot switch control surface interfacing via OSC over USB.
The hardware UI has a row of six foot-convenient buttons, 
six corresponding LEDs, 
three 30-step rotary encoder knobs, 
a small analog joystick (acting as a crude expression pedal,
and mounted diagonally to allow semi-separate control of two channels),
and a standard 1/4" expression pedal input.
There is also a small reset button, hardwired to reset the arduino.


Arduino Device: Adafruit ItsyBitsy 32u4
Pinout:
0 N/C (reserved for USB serial)
1 N/C (reserved for USB serial)
2 Button 1
3 Button 2
4 Knob 1 select button
5 Button 3
6 Knob 2 select button
7 Button 4
8 Knob 3 select button
9 Button 5
10 Rotary 1 A
11 Rotary 1 B
12 Rotary 2 A
13 N/C (LED_BUILTIN)
14 (MISO) LED Data
15 (SCK) Joystick select button
16 (MOSI) Rotary 2 B
A0 Joystick analog X
A1 Joystick analog Y
A2 External Pedal analog Z
A3 Button 0 (record button)
A4 Rotary 3 B
A5 Rotary 3 A
RST (Reset button is connected to reset pin.)

*/


// ** compiler **

#define SERIAL_LOGGING

// ** include ** 

#include <FastLED.h>

// ** define **

#define LED_MASTER_BRIGHTNESS 127

#define NUM_LEDS 6
#define LED_DATA 14

// ** logging **

void log(const char *) {

#ifdef SERIAL_LOGGING
  Serial.println("Stompbox looping.");
#endif

}

// ** global **

CRGB leds[NUM_LEDS];
long tt; // @#@t

// ** visual **

void startup_lightshow() {

  // begin dark
  leds[0] = CHSV(0, 255, 0);
  for (int i = 1; i < NUM_LEDS; i++) {
    leds[i] = CHSV(50, 200, 0);
  }
  FastLED.show();

  // each lamp glows on
  for (int i = 1; i < NUM_LEDS; i++) {
    for (int j = 0; j < 128; j += 1) {
      leds[i] = CHSV(50, 200, j);
      FastLED.show();
      delay(2);
    }
  }

  // record light glows quite bright...
  for (int m = 0; m < 256; m += 1) {
    leds[0] = CHSV(0, 255, m);
    FastLED.show();
    delay(3);
  }

  delay(500);

  // ...then dims to idle
  for (int m = 255; m > 127; m -= 1) {
    leds[0] = CHSV(0, 255, m);
    FastLED.show();
    delay(3);
  }

  delay(500);

  // lamps each sparkle in antici...
  for (int i = 1; i < NUM_LEDS; i++) {
    int j;
    for (j = 127; j < 256; j += 8) {
      leds[i] = CHSV(50, 200, j);
      FastLED.show();
      delay(1);
    }
    delay(30);
    for (; j > 127; j -= 8) {
      leds[i] = CHSV(50, 200, j);
      FastLED.show();
      delay(1);
    }
    delay(15);
  }
  // ...pation

}

void idle_animation() {

  // lamps each sparkle in antici...
  for (int i = 1; i < NUM_LEDS; i++) {
    int j;
    for (j = 127; j < 256; j += 8) {
      leds[i] = CHSV(50, 200, j);
      FastLED.show();
      delay(1);
    }
    delay(30);
    for (; j > 127; j -= 8) {
      leds[i] = CHSV(50, 200, j);
      FastLED.show();
      delay(1);
    }
    delay(15);
  }
  // ...pation

}

// ** main **

void setup() {

#ifdef SERIAL_LOGGING
  Serial.begin(57600);
#endif

	FastLED.addLeds<WS2812,LED_DATA,RGB>(leds,NUM_LEDS);
	FastLED.setBrightness(LED_MASTER_BRIGHTNESS);

  startup_lightshow();
  
  tt = 0; // @#@t
}

void loop() {

  log("Stompbox looping.");
 /*
  tt++;
  if (0 == tt % 25000) {
    idle_animation();
  }
*/
}
