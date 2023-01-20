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

typedef uint8_t byte;

const byte LED_MASTER_BRIGHTNESS = 127;

const int NUM_LEDS = 6;
const int PIN_LED_DATA = 14;

const int NUM_BUTTONS = 6;

// ** logging **

void log(const char *) {

#ifdef SERIAL_LOGGING
  Serial.println("Stompbox looping.");
#endif

}

// ** global **

CRGB leds[NUM_LEDS];
long tt; // @#@t

int button_state[NUM_BUTTONS];

// ** visual display (LEDs) **

const byte V_RECORD_IDLE = 140;
const byte V_LAMP_IDLE = 128;
const byte V_FULL = 255;
const byte V_OFF = 0;

const byte H_RED = 0;
const byte H_VINTAGE_LAMP = 50;

const byte S_VINTAGE_LAMP = 200;
const byte S_FULL = 255;

/// brighten or darken an LED
void glow_change(int led, byte hue, byte sat, byte val_from, byte val_to, int slowness, int change_step) {

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
void glow_up(int led, byte hue, byte sat, byte val_from, byte val_to, int slowness, int change_step = 1) {
  glow_change(led, hue, sat, val_from, val_to, slowness, change_step);
}

/// darken an LED
void glow_down(int led, byte hue, byte sat, byte val_from, byte val_to, int slowness, int change_step = -1) {
  glow_change(led, hue, sat, val_from, val_to, slowness, change_step);
}

/// startup lamp test, indicating start of program after power-up or reset
void startup_lightshow() {

  // begin dark
  leds[0] = CHSV(H_RED, S_FULL, V_OFF);
  for (int i = 1; i < NUM_LEDS; i++) {
    leds[i] = CHSV(H_VINTAGE_LAMP, S_VINTAGE_LAMP, V_OFF);
  }
  FastLED.show();

  // each lamp glows on
  for (int i = 1; i < NUM_LEDS; i++) {
    glow_up(i, H_VINTAGE_LAMP, S_VINTAGE_LAMP, V_OFF, V_LAMP_IDLE, 2);
  }

  // record light glows quite bright...
  glow_up(0, H_RED, S_FULL, V_OFF, V_FULL, 3);
  delay(300);

  // ...then dims to idle
  glow_down(0, H_RED, S_FULL, V_FULL, V_RECORD_IDLE, 4);
  delay(700);

  // lamps each sparkle in antici...
  for (int i = 1; i < NUM_LEDS; i++) {
    glow_up(i, H_VINTAGE_LAMP, S_VINTAGE_LAMP, V_LAMP_IDLE, V_FULL, 1, 8);
    delay(30);
    glow_down(i, H_VINTAGE_LAMP, S_VINTAGE_LAMP, V_FULL, V_LAMP_IDLE, 1, -8);
    delay(15);
  }
  // ...pation

}

/// optional idle animation. proof of concept. might be useful when debugging to show program is still running
void idle_animation() {

  // lamps each sparkle in antici...
  for (int i = 1; i < NUM_LEDS; i++) {
    glow_up(i, H_VINTAGE_LAMP, S_VINTAGE_LAMP, V_LAMP_IDLE, V_FULL, 1, 8);
    delay(30);
    glow_down(i, H_VINTAGE_LAMP, S_VINTAGE_LAMP, V_FULL, V_LAMP_IDLE, 1, -8);
    delay(15);
  }
  // ...pation

}


// ** controls **


/// set up data structures for control inputs
void init_controls() {

  for (int ii = 0; ii < NUM_BUTTONS; ii++) {
    button_state[ii] = 0;
  }

}

/// poll all controls once for changes
void scan_controls() {


}
  

// ** main **

/// main arduino init
void setup() {

#ifdef SERIAL_LOGGING
  Serial.begin(57600);
#endif

	FastLED.addLeds<WS2812,PIN_LED_DATA,RGB>(leds,NUM_LEDS);
	FastLED.setBrightness(LED_MASTER_BRIGHTNESS);

  startup_lightshow();
  
  //tt = 0; // @#@t

} // setup

/// main arduino loop
void loop() {

  log("Stompbox looping.");
 /*
  tt++;
  if (0 == tt % 25000) {
    idle_animation();
  }
*/

  scan_controls();

} // loop
