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

// ** include ** 

// NeoPixel support
#include <FastLED.h>

// OSC support
#include <OSCBundle.h>
// Note: OSC's clockless_trinket.h has a "#define DONE" that conflicts with other library code; I've renamed it to ASM_DONE
#include <OSCBoards.h>

// OSC-over-USB support
#ifdef BOARD_HAS_USB_SERIAL
#include <SLIPEncodedUSBSerial.h>
SLIPEncodedUSBSerial SLIPSerial( thisBoardsSerialUSB );
#else
#include <SLIPEncodedSerial.h>
SLIPEncodedSerial SLIPSerial(Serial);
#endif

// ** types **

// I hate typing uint8_t
typedef uint8_t byte; 

typedef unsigned long time_ms;

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

// note: Reaper DAW sends both 'record ON' and 'play ON' simultaneously; 
// but OSC dispatch paradigm does not make it easy to respond appropriately and track both.
// we can try but it's not clear how much effort it's worth.
// ...indeed, Reaper actually seems to send 'Record OFF Play OFF Stop ON Pause ON' when pausing.
// that bundle would have to be processed atomically as a bundle, not as four messages, to properly track the state.
// maybe someday we can do that, but it's too much work for now. So we track Record or not, but ignore Pause etc.
// (For that matter, Reaper doesn't seem to send these things spontaneously in all cases. So it's risky to expect them.)

typedef struct daw_state_s {

  bool recording;
  bool fx_bypass[8];
  int amp_channel; // modes for plugin "The Anvil (Ignite Amps)" (param 2): saved here as 0, 1, 2 -- but over OSC, normalize to 0.0, 0.5, 1.0

} daw_state_s;

// ** constants **

// FastLED's all-LEDs brightness maximum/scale factor
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

// symbols for those indices
const int PEDAL_X = 0;
const int PEDAL_Y = 1;
const int PEDAL_Z = 2;

const byte PIN_ROTARY_A[NUM_KNOBS] = {
  ROTARY_1_A, ROTARY_2_A, ROTARY_3_A
};

const byte PIN_ROTARY_B[NUM_KNOBS] = {
  ROTARY_1_B, ROTARY_2_B, ROTARY_3_B
};

// ** globals **

// the NeoPixel LEDs (FastLED display buffer: set these and call show to update display)
CRGB leds[NUM_LEDS];

// current (after scan_controls) state of each button (including select press on knobs and joystick, see PIN_BUTTON for the array order)
button_state_e button_state[NUM_BUTTONS];

// current state of each pedal: current value and how far it's changed since last reading.
pedal_state_s pedal_state[NUM_PEDALS];

// current state of each knob: current value and how far it's changed since last reading, plus the raw rotary code data.
knob_state_s knob_state[NUM_KNOBS];

// current state of DAW (based on OSC feedback)
daw_state_s daw_state;

time_ms last_OSC_send_time;
time_ms last_OSC_receive_time;

// ** visual display (LEDs) **

// HSV color values
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

// ** controls **

/// rotary encoder 1A interrupt handler
void handleRotaryInterrupt0() {

  // debounce
  static time_ms previous = millis();
  time_ms current = millis();
  time_ms elapsed = current - previous;
  if (elapsed < 1) {
    return;
  }
  previous = current;

  handleKnobChange(0);

}

/// rotary encoder 2A interrupt handler
void handleRotaryInterrupt1() {
  
  // debounce
  static time_ms previous = millis();
  time_ms current = millis();
  time_ms elapsed = current - previous;
  if (elapsed < 1) {
    return;
  }
  previous = current;
  
  handleKnobChange(1);

}

/// rotary encoder 3A interrupt handler
void handleRotaryInterrupt2() {
  
  // debounce
  static time_ms previous = millis();
  time_ms current = millis();
  time_ms elapsed = current - previous;
  if (elapsed < 1) {
    return;
  }
  previous = current;

  handleKnobChange(2);

}

/// a rotary encoder has been turned. record the change in value.
void handleKnobChange(int ii) {

  // code A has just changed...

  int aa = digitalRead(PIN_ROTARY_A[ii]);
  int bb = digitalRead(PIN_ROTARY_B[ii]);

  // if code B is the same as A, knob is turning in one direction; if different, the other direction. actually pretty simple.
  int direction = (aa == bb) ? -1 : 1;

  // changes are interrupt-driven and thus can arrive faster than once per loop tick.
  // here we accumulate changes until loop consumes them.
  knob_state[ii].delta += direction;
  knob_state[ii].value += direction;
  knob_state[ii].changed = true;

}

/// when loop has read and acted on knob states, we reset knob state members 'delta', 'changed', and optionally (not usually) 'value'
void consumeKnobChanges(int ii, bool resetValue = false) {
  
  knob_state[ii].delta = 0;
  knob_state[ii].changed = false;
  if (resetValue) {
    knob_state[ii].value = 0;
  }
}

void handleRecordButtonStateChange() {
  if (button_state[0] == RELEASING) {
    sendRecordToggle();
  } 
}

void handleStompButtonStateChange(int ii) {

  if (button_state[ii] == RELEASING) {
      
    // debounce.
    // @#@t there are several places we could do this; this may not be the best.
    // (per button? per direction of change? various ways we could implement debouncing....)
    const time_ms minimum_time_between_stomps = 500;
    static time_ms previous = millis();
    time_ms current = millis();
    time_ms elapsed = current - previous;
    if (elapsed < minimum_time_between_stomps) {
      return;
    }
    previous = current;

    // so, what do the buttons do? Adjust to taste...
    // button 1 (first stomp button next to Record button) toggles FX #3 on track 1.
    // (Why #3? FX #1 is MIDI Bad Fader CC Filter, a standard FX Chain item for my tracks due to a flaky fader;
    //  FX #2 on a guitar track is a tuner; generally no need to bypass that)
    // button 2 (second stomp button) toggles FX #4 on track 1.
    // button 3 (second stomp button) toggles FX #5 on track 1.
    // button 4 (second stomp button) toggles FX #6 on track 1.
    // button 5 (second stomp button) toggles FX #7 on track 1.

    const int fx_bypass_index_for_button[NUM_BUTTONS] = { -1, 2, 3, 4, 5, -1, 6, 7, 8 };

    switch (ii) {

      case 1:
        sendFxBypassBool(1, fx_bypass_index_for_button[1], !(daw_state.fx_bypass[0]));
        break;
      case 2:
        sendFxBypassBool(1, fx_bypass_index_for_button[2], !(daw_state.fx_bypass[1]));
        break;
      case 3:
        sendFxBypassBool(1, fx_bypass_index_for_button[3], !(daw_state.fx_bypass[2]));
        break;
      case 4:
        sendFxBypassBool(1, fx_bypass_index_for_button[4], !(daw_state.fx_bypass[3]));
        break;
        
      case 6:
        sendFxBypassBool(1, fx_bypass_index_for_button[6], !(daw_state.fx_bypass[5]));
        break;
      case 7:
        sendFxBypassBool(1, fx_bypass_index_for_button[7], !(daw_state.fx_bypass[6]));
        break;

      case 8:
        sendFxBypassBool(1, fx_bypass_index_for_button[8], !(daw_state.fx_bypass[7]));
        break;

      case 5:
        daw_state.amp_channel = (daw_state.amp_channel + 1) % 3; // cycle 0, 1, 2, 0, 1, 2...
        float value = daw_state.amp_channel / 2.0; // normalize to 0, 0.5, 1.0
        sendFxParamFloat(1, 4, 2, value);
        break;
        
      
    }

  }

}

void handleButtonStateChange(int ii) {
  
  if (ii == 0) {
    handleRecordButtonStateChange();
  } else if (ii < 6) {
    handleStompButtonStateChange(ii);
  } else if (ii < 9) {
    // knob selects 6-8
    handleStompButtonStateChange(ii);
  } else {
    // joystick select
    // best to ignore this button: probably too easily kicked unintentionally
  }

}

/// set up data structure to track remote DAW's status
void initDawState() {
  
  // we're guessing about this initially
  daw_state.recording = false;
  daw_state.fx_bypass[0] = false;
  daw_state.fx_bypass[1] = false;
  daw_state.fx_bypass[2] = false;
  daw_state.fx_bypass[3] = false;
  daw_state.fx_bypass[4] = false;
  daw_state.fx_bypass[5] = false;
  daw_state.fx_bypass[6] = false;
  daw_state.fx_bypass[7] = false;
  
}

/// set up data structures for control inputs
void initControls() {

  for (int ii = 0; ii < NUM_BUTTONS; ii++) {
    button_state[ii] = digitalRead(PIN_BUTTON[ii] == 0) ? PRESSED : UNPRESSED;
  }

  for (int ii = 0; ii < NUM_PEDALS; ii++) {
    pedal_state[ii].value = analogRead(PIN_PEDAL[ii]);
  }

  for (int ii = 0; ii < NUM_KNOBS; ii++) {
    knob_state[ii].value = 0; 
    knob_state[ii].delta = 0;
    knob_state[ii].changed = false;
  }

  attachInterrupt( digitalPinToInterrupt(PIN_ROTARY_A[0]),	handleRotaryInterrupt0, CHANGE);
  attachInterrupt( digitalPinToInterrupt(PIN_ROTARY_A[1]),	handleRotaryInterrupt1, CHANGE);
  attachInterrupt( digitalPinToInterrupt(PIN_ROTARY_A[2]),	handleRotaryInterrupt2, CHANGE);
 
} // init_controls

/// poll all controls once for changes
void scanControls() {

  // buttons

  for (int ii = 0; ii < NUM_BUTTONS; ii++) {

    int result = digitalRead(PIN_BUTTON[ii]);
    int was = button_state[ii];

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

    if (button_state[ii] != was) {
       handleButtonStateChange(ii);
    }
  }

  // pedals
  for (int ii = 0; ii < NUM_PEDALS; ii++) {

    const int threshold = 10;

//    analogRead(PIN_PEDAL[ii]); // extra read to stabilize ADC?
    int result = analogRead(PIN_PEDAL[ii]); 
  
    int was = pedal_state[ii].value;
    int delta = result - was;

    if (abs(delta) > threshold) {

      pedal_state[ii].value = result;

      if (ii == PEDAL_Z) {
        float answer = pedal_state[ii].value / 1023.0;
        answer = 1.0 - answer; // reverse direction
        sendWah(answer);
      }
       
    }
  }

  // knobs

  String msg = "";
	
//  byte oldSREG = SREG; // save interrupts status (on or off; likely on)
//	noInterrupts(); // protect event buffer integrity

  for (int ii = 0; ii < NUM_KNOBS; ii++) {
    
    // @#@#@u handler

    consumeKnobChanges(ii);

  }


//  SREG = oldSREG; // restore interrupts status

}
  
// ** OSC **

// Receive OSC messages...

typedef enum listening_status_e { BUNDLE_OR_MESSAGE_START, BUNDLE, MESSAGE } listening_status_e;

// receive and dispatch OSC bundles
void listenForOSC() {

  static OSCBundle *bundleIN = new OSCBundle;
  static OSCMessage *messageIN = new OSCMessage;
  static listening_status_e listeningFor = BUNDLE_OR_MESSAGE_START;

  bool eot =  SLIPSerial.endofPacket();
  while (SLIPSerial.available() && !eot) {
    uint8_t data = SLIPSerial.read();
    if (listeningFor == BUNDLE_OR_MESSAGE_START) {
      if (data == 35) {
        // "#"
        listeningFor = BUNDLE;
      } else if (data == 47) {
        // "/"
        listeningFor = MESSAGE;
      } else {
        // @#@u error: neither        
      }
    }
    if (listeningFor == BUNDLE) {
      bundleIN->fill(data);
    } else if (listeningFor == MESSAGE) {
      messageIN->fill(data);
    }
    eot =  SLIPSerial.endofPacket();
  }

  if (eot) {
    if ( (listeningFor == BUNDLE) && bundleIN->hasError() ) {
      
      digitalWrite(LED_BUILTIN, 1); // @#@d
      int err = bundleIN->getError();
      char report[99];
      sprintf(report, "%d: '%s' (%d)", err, bundleIN->incomingBuffer, bundleIN->incomingBufferSize);
      sendOSCString("/foobar/error", report);
      sendOSCString("/foobar/error", bundleIN->errorDetails);
      bundleIN->errorDetails[0] = 0;
    } else if (listeningFor == BUNDLE) {

      //flashBuiltInLED(); // @#@t

      char report[99];
      sprintf(report, "got; (%d) '%s'", bundleIN->incomingBufferSize, bundleIN->incomingBuffer);
      //sendOSCString("/foobar/bundle", report);

      dispatchBundleContents(bundleIN);
      bundleIN->empty();
      listeningFor = BUNDLE_OR_MESSAGE_START;

    } else if (listeningFor == MESSAGE) {

      //flashBuiltInLED(); // @#@t

      char report[99];
      sprintf(report, "got; (%d) '%s'", messageIN->incomingBufferSize, messageIN->incomingBuffer);
      //sendOSCString("/foobar/message", report);

      dispatchMessage(messageIN);
      messageIN->empty();
      listeningFor = BUNDLE_OR_MESSAGE_START;
      
    }
    
  }
  // else wait for more...

}

/// handle the contents of an incoming OSC bundle
void dispatchBundleContents(OSCBundle *bundleIN) {

  last_OSC_receive_time = millis();
  
  bundleIN->dispatch("/record", handleOSC_Record);
  bundleIN->dispatch("/track/1/fx/*/bypass", handleOSC_FxBypass);
  bundleIN->dispatch("/track/1/fx/4/fxparam/2/value", handleOSC_Fx4Fxparam2);
}

void dispatchMessage(OSCMessage *messageIN) {

  last_OSC_receive_time = millis();

  messageIN->dispatch("/record", handleOSC_Record);
  messageIN->dispatch("/track/1/fx/*/bypass", handleOSC_FxBypass);
  messageIN->dispatch("/track/1/fx/4/fxparam/2/value", handleOSC_Fx4Fxparam2);
}

// Handle incoming OSC messages...

void handleOSC_Record(OSCMessage &msg) {

  byte status = msg.getFloat(0);
  daw_state.recording = (status != 0.0);
  updateRecordButtonColor();
}

void updateRecordButtonColor() {

  if (daw_state.recording) {
    leds[0] = CHSV(H_RED, S_FULL, V_FULL);
  } else {
    leds[0] = CHSV(H_RED, S_FULL, V_DIM);
  }
  FastLED.show();
}

void handleOSC_FxBypass(OSCMessage &msg) {

  // which plugin index? (1-based)
  
  const int buffer_size = strlen("/track/1/fx/*/bypass") + 1;
  char buffer[buffer_size];
  msg.getAddress(buffer);
  int fx = buffer[12] - '0'; // e.g. "/track/1/fx/3/bypass" -> 3

  int value = msg.getFloat(0);
  daw_state.fx_bypass[fx - 2] = (value == 0);
  updateLampColors();

 // char buffer2[2] = { fx, 0 };
 // sendOSCString("/foobar/bypass", buffer2);

}


void handleOSC_Fx4Fxparam2(OSCMessage &msg) {

  // float in message seems to be always 0?? ignore it

  updateLampColors();

}

void updateLampColors() {

  static CRGB amp_channel_hue[3] = {
    CHSV(H_AQUA, S_VINTAGE_LAMP, V_FULL),
    CHSV(H_BLUE, S_VINTAGE_LAMP, V_FULL), 
    CHSV(H_PINK, S_VINTAGE_LAMP, V_FULL)
  };
  
  for (int ii = 1; ii <= 5; ii++) {   
    if (ii == 5) {
      if (daw_state.fx_bypass[ii-1]) {
        leds[ii] = CHSV(H_VINTAGE_LAMP, S_VINTAGE_LAMP, V_DIM);
      } else {
        leds[ii] = amp_channel_hue[daw_state.amp_channel];
      }
      
    } else {
      if (daw_state.fx_bypass[ii-1]) {
        leds[ii] = CHSV(H_VINTAGE_LAMP, S_VINTAGE_LAMP, V_DIM);
      } else {
        leds[ii] = CHSV(H_VINTAGE_LAMP, S_VINTAGE_LAMP, V_FULL);
      } 
    }
    FastLED.show();
  }

}

// Send OSC messages...

void sendOSCMessage(OSCMessage &msg) {

  SLIPSerial.beginPacket();
  msg.send(SLIPSerial); // send the bytes to the SLIP stream
  SLIPSerial.endPacket(); // mark the end of the OSC Packet
  msg.empty(); // free space occupied by message
  last_OSC_send_time = millis();

}

/// send an OSC message to a specified OSC address, containing a single specified float parameter value
void sendOSCFloat(const char *address, float value) {

  OSCMessage msg(address);
  msg.add(value);
  sendOSCMessage(msg);

}

void sendOSCInt(const char *address, int value) {
  
  OSCMessage msg(address);
  msg.add(value);
  sendOSCMessage(msg);

}

void sendOSCString(const char *address, const char *value) {
  
  OSCMessage msg(address);
  msg.add(value);
  sendOSCMessage(msg);

}

void sendOSCBool(const char *address, bool value) {
  
  OSCMessage msg(address);
  msg.add(value);
  sendOSCMessage(msg);

}
void sendOSCTrigger(const char *address) {

  OSCMessage msg(address);
  sendOSCMessage(msg);

}

void sendRecordToggle() {
  sendOSCTrigger("/record");
}

void sendFxBypassBool(int track, int fx, bool value) {

  // native Reaper OSC FX_BYPASS command doesn't work, or I'm not implementing it properly.
  // (Reaper's OSC default config file is sparsely and tersely documented, with almost no discussion of how Reaper actually behaves over OSC.) 
  // Luckily we have the S&M commands, and this sequence seems to work:

  String addr = "/action/";
  addr = addr + (40938 + track); // track 1: action 40939; track 2: action 40940, etc. No track 0.
  sendOSCInt(addr.c_str(), 1);
  delay(10);
//  String msg = "_S&M_FXBYP_SETO";
//  msg = msg + (value ? "N" : "FF");
//  msg = msg + fx;
  String msg = "_S&M_FXBYP";
  msg = msg + fx; // fx = 1 thru 8 only
  sendOSCString("/action/str", msg.c_str());
  delay(10);
}

void sendFxParamFloat(int track, int fx, int param, float value) {
  // n/track/@/fx/@/fxparam/@/value -- n is for normalized (0.0-1.0)
  String addr = "/track/";
  addr = addr + track;
  addr = addr + "/fx/";
  addr = addr + fx;
  addr = addr + "/fxparam/";
  addr = addr + param;
  addr = addr + "/value";
  sendOSCFloat(addr.c_str(), value);
  delay(10);

}

/// debounce to protect serial connection
bool tooSoon() {
  // if we send OSC messages too fast, we crash the serial connection (error 31 in the node.js bridge) or even crash/freeze the arduino (somehow???)
  // so we throttle them to send no more often that about 5 or 10 ms. 
  // (Adjust to taste for your risk tolerance; the absolute minimum without problems in a simple test seems to be about 4 or 5 ms.
  //  Crashes are nearly immediate at 1 or 0. 5 might be safe enough but 10 seems safer. Perhaps larger message bundles will need more time?)
  // this is mainly needed by analog (pedal) inputs: it's very unlikely we'll try to send other signals so fast. But feel free to throttle all message;
  // note that this method is lossy and not suitable for all inputs. (If any: let's consider it temporary and improve it someday. @#@t)
  const time_ms minimum_time_between_sends = 10;
  static time_ms previous = millis();
  time_ms current = millis();
  time_ms elapsed = current - previous;
  if (elapsed < minimum_time_between_sends) {
    return;
  }
  previous = current;

}

void sendWah(float value) {

  if (tooSoon()) {
    return;
  }
  
  sendOSCFloat("/track/1/fx/2/fxparam/1/value", value);
}

void housekeeping() {

  const time_ms too_long = 1000;

  if (last_OSC_send_time > last_OSC_receive_time + too_long) {
    digitalWrite(LED_BUILTIN, 1);
  } else {
    digitalWrite(LED_BUILTIN, 0);
  }  

}

// ** main **

/// pin configuration
void setupPins() {
  
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
    pinMode(PIN_PEDAL[ii], INPUT);
  }

  // all the NeoPixel LEDs are controlled through one output pin via FastLED
  pinMode(PIN_LED_DATA, OUTPUT);

  // ...and there's a "reset light", a normal amber LED which echoes the built-in LED on pin 13
  pinMode(PIN_LED_BUILTIN, OUTPUT);

}

/// open OSC-over-USB connection
void setupOSC() {
 
  SLIPSerial.begin(115200);

  last_OSC_send_time = millis();
  last_OSC_receive_time = last_OSC_send_time;

}

/// main arduino init
void setup() {

  setupOSC();

  // activate arduino pins for input and output as appropriate
  setupPins();
  
  initDawState();

  // set up and clear controls status
  initControls();

  // configure LEDs
	FastLED.addLeds<WS2812,PIN_LED_DATA,RGB>(leds,NUM_LEDS);
	FastLED.setBrightness(LED_MASTER_BRIGHTNESS);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 500);

  // ...all subsystems ready.

  // make it clear to the user that the device has just been powered on or reset
  startupLightshow();
  
} // setup

/// main arduino loop
void loop() {

  scanControls();

  listenForOSC();

  housekeeping();

//  idleAnimation(); // (optional)
 
} // loop


// EOF.
