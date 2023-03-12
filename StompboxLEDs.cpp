#include "StompboxLEDs.h"

typedef unsigned long time_ms;

// ** CONSTANTS **

// FastLED's all-LEDs brightness maximum/scale factor
const byte LED_MASTER_BRIGHTNESS = 127;

// total NeoPixels in display (including illuminated Record button)
const int NUM_LEDS = 6;

// ** GLOBALS **

// the NeoPixel LEDs (FastLED display buffer: set these and call show to update display)
CRGB leds[NUM_LEDS];

// ** glowy stuff **

void setupLEDs() {
  
  // configure LEDs
	FastLED.addLeds<WS2812,PIN_LED_DATA,RGB>(leds,NUM_LEDS);
	FastLED.setBrightness(LED_MASTER_BRIGHTNESS);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 500);

}

/// brighten or darken an LED
// animation blocks processing until complete. 
// slowness parameter is ms of delay between change steps; change_step is size of each step. Both together control animation speed.
// If change_step is 0, change is instantaneous. If slowness is 0, animation runs as fast as possible while showing each step. 
void glowChange(int led, byte hue, byte sat, byte val_from, byte val_to, int slowness, int change_step) {

  if (change_step == 0) {
    leds[led] = CHSV(hue, sat, val_to);
    FastLED.show();
    return;
  }

  for (int j = val_from; (change_step > 0) ? (j < val_to) : (j > val_to); j += change_step) {
    leds[led] = CHSV(hue, sat, j);
    FastLED.show();
    delay(slowness);
  }
}

/// brighten an LED
void glowUp(int led, byte hue, byte sat, byte val_from, byte val_to, int slowness, int change_step = 1) {
  glowChange(led, hue, sat, val_from, val_to, slowness, change_step);
}

/// darken an LED
void glowDown(int led, byte hue, byte sat, byte val_from, byte val_to, int slowness, int change_step = -1) {
  glowChange(led, hue, sat, val_from, val_to, slowness, change_step);
}

/// startup lamp test, indicating start of program after power-up or reset
void startupLightshow() {

  // begin dark
  leds[0] = CHSV(H_RED, S_FULL, V_OFF);
  for (int i = 1; i < NUM_LEDS; i++) {
    leds[i] = CHSV(H_VINTAGE_LAMP, S_VINTAGE_LAMP, V_OFF);
  }
  FastLED.show();

  // each lamp glows on
  for (int i = 1; i < NUM_LEDS; i++) {
    glowUp(i, H_VINTAGE_LAMP, S_VINTAGE_LAMP, V_OFF, V_LAMP_IDLE, 2);
  }

  // record light glows quite bright...
  glowUp(0, H_RED, S_FULL, V_OFF, V_FULL, 3);
  delay(300);

  // ...then dims to idle
  glowDown(0, H_RED, S_FULL, V_FULL, V_DIM, 4);
  // Best UI for Record button is not yet clear. Adjust to taste.
  delay(700);

  // lamps each sparkle in antici...
  for (int i = 1; i < NUM_LEDS; i++) {
    glowUp(i, H_VINTAGE_LAMP, S_VINTAGE_LAMP, V_LAMP_IDLE, V_FULL, 1, 8);
    delay(30);
    glowDown(i, H_VINTAGE_LAMP, S_VINTAGE_LAMP, V_FULL, V_LAMP_IDLE, 1, -8);
    delay(15);
  }
  // ...pation

}

/// stand down
void hibernateLightshow() {

  // each lamp dims to dark
  for (int i = 1; i < NUM_LEDS; i++) {
    glowDown(i, H_VINTAGE_LAMP, S_VINTAGE_LAMP, V_LAMP_IDLE, V_DIM, 2);
  }

  // and goes out completely
  for (int i = 1; i < NUM_LEDS; i++) {
    leds[i] = CHSV(H_VINTAGE_LAMP, S_VINTAGE_LAMP, V_OFF);
    FastLED.show();
    delay(100);
  }

  // record light changes color and glows quite bright...
  glowDown(0, H_RED, S_FULL, V_DIM, V_OFF, 4);

  delay(200);
  
  glowUp(0, H_GREEN, S_FULL, V_DIM, V_FULL, 3);

  delay(300);

  // ...then dims to dim
  glowDown(0, H_GREEN, S_FULL, V_FULL, V_DIM, 3);

  delay(700);
}

/// optional idle animation. proof of concept. useful when debugging to show program is still running
void idleAnimation() {

  // periodically...
  const time_ms time_between_sparkle_waves = 4000;
  static time_ms previous = millis();
  time_ms current = millis();
  time_ms elapsed = current - previous;
  if (elapsed < time_between_sparkle_waves) {
    return;
  }
  previous = current;

  // lamps each sparkle in antici...
  for (int i = 1; i < NUM_LEDS; i++) {
    glowUp(i, H_VINTAGE_LAMP, S_VINTAGE_LAMP, V_LAMP_IDLE, V_FULL, 1, 8);
    delay(30);
    glowDown(i, H_VINTAGE_LAMP, S_VINTAGE_LAMP, V_FULL, V_LAMP_IDLE, 1, -8);
    delay(15);
  }
  // ...pation

}

/// quickly flash the built-in ('reset') LED. Intended as a debugging tool.
void flashBuiltInLED()
{ 
  digitalWrite(LED_BUILTIN, 1);
  delay(50);
  digitalWrite(LED_BUILTIN, 0);
}