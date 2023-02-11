#ifndef INCLUDED_StompboxLEDs_ALREADY

// NeoPixel support
#include <FastLED.h>

// I hate typing uint8_t
typedef uint8_t byte; 

#define PIN_LED_BUILTIN LED_BUILTIN
#define PIN_LED_DATA 14

const byte V_RECORD_IDLE = 140;
const byte V_LAMP_IDLE = 128;
const byte V_FULL = 255;
const byte V_DIM = 45;
const byte V_OFF = 0;

// HSV color hues
const byte H_RED = 0;
const byte H_GREEN = 100;
const byte H_AQUA = 115;
const byte H_BLUE = 150;
const byte H_PURPLE = 170;
const byte H_PINK = 225;

const byte H_VINTAGE_LAMP = 50;

// HSV color saturations
const byte S_VINTAGE_LAMP = 200;
const byte S_FULL = 255;

extern CRGB leds[];

void setupLEDs();
void startupLightshow();

#define INCLUDED_StompboxLEDs_ALREADY
#endif