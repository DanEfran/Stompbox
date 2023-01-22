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
2 Rotary 1 A
3 Rotary 2 A
4 Knob 1 select button
5 Button 3
6 Knob 2 select button
7 Rotary 3 A
8 Knob 3 select button
9 Button 5
10 Button 1
11 Rotary 1 B
12 Button 2
13 N/C (LED_BUILTIN)
14 (MISO) LED Data
15 (SCK) Joystick select button
16 (MOSI) Rotary 2 B
A0 Joystick analog X
A1 Joystick analog Y
A2 External Pedal analog Z
A3 Button 0 (record button)
A4 Button 4
A5 Rotary 3 B
RST (Reset button is connected to reset pin.)

*/

// ** compiler **

#define SERIAL_LOGGING

// ** include ** 

#include <FastLED.h>

// ** types **

// I hate typing uint8_t
typedef uint8_t byte; 

/* 
  Note: We will mostly use buttons as toggle triggers, toggling a controlled value exactly when pressed,
  but we may also use buttons as momentary switches, setting a controlled value when pressed and clearing it when released.
  To support those behaviors, at a lower level, we track the exact status of the button itself. 
*/

// button states
typedef enum button_state_e { UNPRESSED, PRESSING, PRESSED, RELEASING} button_state_e;

// pedal states
typedef struct pedal_state_s {
  int value;
  int delta;
} pedal_state_s;

// knob states
typedef struct knob_state_s {
  int value;
  int delta;
  int codeA;
  int codeB;
  int direction;
  bool changed;
} knob_state_s;
// ** constants **

const byte LED_MASTER_BRIGHTNESS = 127;

// total NeoPixels in display (including illuminated Record button)
const int NUM_LEDS = 6;

// Record button plus five footswitch stomp buttons
const int NUM_BASIC_BUTTONS = 6;

// Knob and joystick push-select action: also counts as "buttons" for control scan
const int NUM_SELECT_BUTTONS = 4;

// total buttons of both kinds
const int NUM_BUTTONS = NUM_BASIC_BUTTONS + NUM_SELECT_BUTTONS;

// Three rotary encoder knobs
const int NUM_KNOBS = 3;

// Two joystick axes and one external expression pedal input
const int NUM_PEDALS = 3;

// arduino pin assignments...

const byte PIN_LED_BUILTIN = LED_BUILTIN; // arduino on-board LED. May or may not be visible in final version of hardware.
const byte PIN_LED_DATA = 14; // NeoPixel data out. Controlled via FastLED library.
const byte PIN_BUTTON_0 = A3; // 'Record' button. LED 0 illuminates the clear button itself (other buttons' LEDs are just adjacent display lamps)
const byte PIN_BUTTON_1 = 10; // footswitch button 1. (Nominally, a stomp button as found on guitar pedals. I don't like those so I'm using softer-press buttons.)
const byte PIN_BUTTON_2 = 12;
const byte PIN_BUTTON_3 = 5;
const byte PIN_BUTTON_4 = A4;
const byte PIN_BUTTON_5 = 9;
const byte ROTARY_1_A = 2; // 30-step rotary encoder knobs have A and B encoded outputs
const byte ROTARY_1_B = 11;
const byte ROTARY_2_A = 3; // pins 2, 3, and 7 are interrupt-capable; we use them for Rotary 1/2/3 Code A.
const byte ROTARY_2_B = 16;
const byte ROTARY_3_A = 7;
const byte ROTARY_3_B = A5;
const byte PIN_KNOB_SELECT_1 = 4; // rotary knobs can also push-to-select, a "button" input
const byte PIN_KNOB_SELECT_2 = 6;
const byte PIN_KNOB_SELECT_3 = 8;
const byte PIN_PEDAL_X = A0; // integrated "pedal" joystick depressed left, i.e. SW (@#@?)
const byte PIN_PEDAL_Y = A1; // integrated "pedal" joystick depressed right, i.e. SE (@#@?)
const byte PIN_PEDAL_Z = A2; // external expression pedal input (note: pedal polarity may vary, software must be adjustable)
const byte PIN_PEDAL_SELECT = 15; // integrated "pedal" joystick has push-to-select button input. (may be ergonomically prone to accidental presses)

// the same pins grouped for bulk processing...

const byte PIN_BUTTON[NUM_BUTTONS] = {
  PIN_BUTTON_0, PIN_BUTTON_1, PIN_BUTTON_2, PIN_BUTTON_3, PIN_BUTTON_4, PIN_BUTTON_5,
  PIN_KNOB_SELECT_1, PIN_KNOB_SELECT_2, PIN_KNOB_SELECT_3, PIN_PEDAL_SELECT
};

const byte PIN_PEDAL[NUM_PEDALS] = {
  PIN_PEDAL_X, PIN_PEDAL_Y, PIN_PEDAL_Z
};

const byte PIN_ROTARY_A[NUM_KNOBS] = {
  ROTARY_1_A, ROTARY_2_A, ROTARY_3_A
};

const byte PIN_ROTARY_B[NUM_KNOBS] = {
  ROTARY_1_B, ROTARY_2_B, ROTARY_3_B
};


// ** logging **

/// print a message over the serial I/O line...only if relevant #define is operative. No-op otherwise.
void log(const char *message) {

#ifdef SERIAL_LOGGING
  Serial.println(message);
#endif

}

/// print a message over the serial I/O line...only if relevant #define is operative. No-op otherwise.
void log(const String& message) {

#ifdef SERIAL_LOGGING
  Serial.println(message);
#endif

}

// ** globals **

// the NeoPixel LEDs (FastLED display buffer: set these and call show to update display)
CRGB leds[NUM_LEDS];

// current (after scan_controls) state of each button (including select press on knobs and joystick, see PIN_BUTTON for the array order)
button_state_e button_state[NUM_BUTTONS];

// current state of each pedal: current value and how far it's changed since last reading.
pedal_state_s pedal_state[NUM_PEDALS];

// current state of each knob: current value and how far it's changed since last reading, plus the raw rotary code data.
knob_state_s knob_state[NUM_KNOBS];

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

  static long tt = 0;

  if (0 != ++tt % 50000) {
    return;
   }

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

void handleRotaryInterrupt0() {
  handleKnobChange(0);
}
void handleRotaryInterrupt1() {
  handleKnobChange(1);
}
void handleRotaryInterrupt2() {
  handleKnobChange(2);
}

void handleKnobChange(int ii) {

  // code A has changed. 
  // if code B is the same as A, knob is turning in one direction; if different, the other direction. actually pretty simple.

  int aa = digitalRead(PIN_ROTARY_A[ii]);
  int bb = digitalRead(PIN_ROTARY_B[ii]);

  int direction = (aa == bb) ? -1 : 1;

  knob_state[ii].delta += direction; // @#@? should this use += or = ? How will it be used? Will user consume/reset it? Too soon to tell...
  knob_state[ii].value += direction;
  knob_state[ii].changed = true;
  
  
  knob_state[ii].direction = direction;

  knob_state[ii].codeA = aa;
  knob_state[ii].codeB = bb;

}

void consumeKnobChanges(int ii, bool resetValue = false) {
  
  knob_state[ii].delta = 0;
  knob_state[ii].changed = false;
  if (resetValue) {
    knob_state[ii].value = 0;
  }
}

/// set up data structures for control inputs
void init_controls() {

  for (int ii = 0; ii < NUM_BUTTONS; ii++) {
    button_state[ii] = digitalRead(PIN_BUTTON[ii] == 0) ? PRESSED : UNPRESSED;
  }

  for (int ii = 0; ii < NUM_PEDALS; ii++) {
    pedal_state[ii].value = analogRead(PIN_PEDAL[ii]);
    pedal_state[ii].delta = 0;
  }

  for (int ii = 0; ii < NUM_KNOBS; ii++) {
    knob_state[ii].value = 0; 
    knob_state[ii].delta = 0;
    knob_state[ii].codeA = digitalRead(PIN_ROTARY_A[ii]);
    knob_state[ii].codeB = digitalRead(PIN_ROTARY_B[ii]);
    knob_state[ii].direction = (knob_state[ii].codeA == knob_state[ii].codeB) ? -1 : 1;
    knob_state[ii].changed = false;
  }

  attachInterrupt( digitalPinToInterrupt(PIN_ROTARY_A[0]),	handleRotaryInterrupt0, CHANGE);
  attachInterrupt( digitalPinToInterrupt(PIN_ROTARY_A[1]),	handleRotaryInterrupt1, CHANGE);
  attachInterrupt( digitalPinToInterrupt(PIN_ROTARY_A[2]),	handleRotaryInterrupt2, CHANGE);
 
} // init_controls

/// poll all controls once for changes
void scan_controls() {

  // buttons

  for (int ii = 0; ii < NUM_BUTTONS; ii++) {

    int result = digitalRead(PIN_BUTTON[ii]);

    // using internal pullup resistors and grounding buttons, so 0 = make, 1 = break
    if (result == 0) {
      // button is depressed
      if (button_state[ii] == PRESSING) {
        // caller had a chance to respond to PRESSING state after last call, so we can move on.
        button_state[ii] = PRESSED;
      } else if (button_state[ii] != PRESSED) {
        // was UNPRESSED or RELEASING, not anymore
        button_state[ii] = PRESSING;
      }
    } else {
      // button is not depressed      
      if (button_state[ii] == RELEASING) {
        // caller had a chance to respond to RELEASING state after last call, so we can move on.
        button_state[ii] = UNPRESSED;
      } else if (button_state[ii] != UNPRESSED) {
        // was PRESSING or PRESSED, not anymore        
        button_state[ii] = RELEASING;
      }
    }
  }

  // pedals
  for (int ii = 0; ii < NUM_PEDALS; ii++) {
    int result = analogRead(PIN_PEDAL[ii]);
    int was = pedal_state[ii].value;
    pedal_state[ii].delta = result - was;
    pedal_state[ii].value = result;
  }

  // knobs

  String msg = "";
	
//  byte oldSREG = SREG; // save interrupts status (on or off; likely on)
//	noInterrupts(); // protect event buffer integrity

  bool anyChanged = false;

  for (int ii = 0; ii < NUM_KNOBS; ii++) {
    
    msg = msg + "knob " + ii + ": " + knob_state[ii].value;
    msg = msg + "   delta " + knob_state[ii].delta;
    msg = msg + "   direction " + knob_state[ii].direction;
    msg = msg + "   ";

    anyChanged = anyChanged || knob_state[ii].changed;    
    
    consumeKnobChanges(ii);

  }

  if (anyChanged) {
    log(msg);
  }

//  SREG = oldSREG; // restore interrupts status

}
  

// ** main **

/// pin configuration
void setup_pins() {
  
    // buttons make to ground and expect internal pullups
  for (int ii = 0; ii < NUM_BUTTONS; ii++) {
    pinMode(PIN_BUTTON[ii], INPUT_PULLUP);
  }

  // rotary encoders have two code pins that make to ground
  for (int ii = 0; ii < NUM_KNOBS; ii++) {
    pinMode(PIN_ROTARY_A[ii], INPUT_PULLUP);
    pinMode(PIN_ROTARY_B[ii], INPUT_PULLUP);
  }

  // pedals are analog inputs with their own +5v and ground connections (nominal analog range endpoints; actual results may vary)
  for (int ii = 0; ii < NUM_PEDALS; ii++) {
    pinMode(PIN_PEDAL, INPUT);
  }

  // all the NeoPixel LEDs are controlled through one output pin via FastLED
  pinMode(PIN_LED_DATA, OUTPUT);

}

/// main arduino init
void setup() {

  // get ready for USB I/O...

#ifdef SERIAL_LOGGING
  // establish the standard arduino serial connection for debug logging
  Serial.begin(57600);
#endif

  // @#@u OSC init goes here

  // get ready for control surface controls and lamps...

  setup_pins();
  
  init_controls();

	FastLED.addLeds<WS2812,PIN_LED_DATA,RGB>(leds,NUM_LEDS);
	FastLED.setBrightness(LED_MASTER_BRIGHTNESS);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 500);

  // make it clear to the user that the device has just been powered on or reset
  startup_lightshow();
  
} // setup

/// main arduino loop
void loop() {

  scan_controls();

  //idle_animation(); // (optional)
 
} // loop


// EOF.
