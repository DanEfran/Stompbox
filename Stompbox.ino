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
// that bundle would have to be processed atomically as a bundle, not as four messages, to properly track the state without lots of complexity.
// (I haven't checked whether they always arrive in the same order, but either way this is a fairly complex situation to handle correctly.)
// maybe someday we can do that, but it's too much work for now. So we track Record or not, but ignore Pause etc.

/// status of the DAW controls we interact with.
// (imperfect knowledge: what we've been told, with guesses for what we haven't been told.)
typedef struct daw_state_s {

  bool recording; // our recording studio red light
  bool fx_bypass[9]; // we control bypass on fx 2-8. Array elements 0 and 1 are ignored.
  int amp_channel; // modes for plugin "The Anvil (Ignite Amps)" (param 2): saved here as 0, 1, 2 -- but over OSC, normalize to 0.0, 0.5, 1.0

} daw_state_s;

/// a button can behave in one of several useful ways, currently:
// as an fx bypass button (like a typical guitar pedal main stomp);
// to cycle between fx parameter values;
// or doing nothing at all.
typedef enum button_mode_e { 

  IGNORED_BUTTON,               // does nothing
  FX_BYPASS,                    // sends bypass on/off messages
  FXPARAM_CYCLE_3               // sends fxparam value cycle among 0, 0.5, 1

} button_mode_e;

/// each button knows which fx (and which fx parameter, where appropriate) it should control, and what it should do to it. 
typedef struct button_configuration_s {

  int fx_index; // fx index in DAW track 1 fx chain: we use indices 2 through 8
  int fx_param; // parameter index in that fx plugin's controls
  button_mode_e button_mode; // how the button behaves

} button_configuration_s;

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

// fx parameter number for a particular control we're interested in, 'The Anvil' amp's channel select
const int FXPARAM_ANVIL_AMP_CHANNEL = 2;

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

// timeliness of OSC feedback. (too slow probably means disconnected PC bridge)
time_ms last_OSC_send_time;
time_ms last_OSC_receive_time;

// behavior modes and control targets of the buttons
button_configuration_s button_config[NUM_BUTTONS];

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

/// something happened to the record button
void handleRecordButtonStateChange() {
  if (button_state[0] == RELEASING) {
    sendRecordToggle();
  } 
}

/// something happened to a stomp button
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

    int fx = button_config[ii].fx_index;
    int fxparam = FXPARAM_ANVIL_AMP_CHANNEL; // @#@#@t

    float value;

    switch (button_config[ii].button_mode) {

      case FX_BYPASS:
        sendFxBypassBool(1, fx, !(daw_state.fx_bypass[fx]));
        break;

      case FXPARAM_CYCLE_3:
        daw_state.amp_channel = (daw_state.amp_channel + 1) % 3; // cycle 0, 1, 2, 0, 1, 2...
        value = daw_state.amp_channel / 2.0; // normalize to 0, 0.5, 1.0
        sendFxParamFloat(1, fx, fxparam, value);
        break;
      
      case IGNORED_BUTTON:
        break;      

    }

  }

}

/// something happened to a button
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
void setupDawState() {
  
  // we're guessing about this initially
  daw_state.recording = false;

  // we're guessing about these initially
  for (int ii = 2; ii <= 8; ii++) {
    daw_state.fx_bypass[ii] = true;
  }
}

/// configure buttons with default behavior modes and control targets
void setupButtons() {

  // note: record button (button 0) is not included in this scheme
  // most buttons are FX bypass buttons, emulating the fundamental control scheme of a guitar pedalboard
  for (int ii = 1; ii <= 8; ii++) {
    button_config[ii].button_mode = FX_BYPASS;
    button_config[ii].fx_index = (ii > 5) ? ii : ii + 1; // 2, 3, 4, 5, (6), 6, 7, 8
    // .fx_param irrelevant for this mode
  }

  // exception: amp channel button (button 5)
  button_config[5].button_mode = FXPARAM_CYCLE_3;
  button_config[5].fx_index = 4; // 'The Anvil' amp: current position in fx chain
  button_config[5].fx_param = FXPARAM_ANVIL_AMP_CHANNEL; // amp: channel select control

  // exception: joystick select button is ignored (probably too easily kicked)
  button_config[9].button_mode = IGNORED_BUTTON;
  // .fx_index and .fx_param irrelevant for this mode

}

/// set up data structures for control inputs
void setupControls() {

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

//    analogRead(PIN_PEDAL[ii]); // @#@? extra read to stabilize ADC?
    int result = analogRead(PIN_PEDAL[ii]); 
  
    int was = pedal_state[ii].value;
    int delta = result - was;

    if (abs(delta) > threshold) {

      pedal_state[ii].value = result;

      if (ii == PEDAL_Z) {
        float answer = pedal_state[ii].value / 1023.0;
        answer = 1.0 - answer; // reverse direction
        sendWah(answer);
      } else {
        // @#@u PEDAL_X and PEDAL_Y (the joystick) are not working. Possibly a wiring problem.
        // The joystick is an optional bonus feature anyway, so for now we ignore these non-functional inputs. 
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

const time_ms MINIMUM_TIME_BETWEEN_OSC_SENDS = 10; // adjust to taste, but not less than about 5.

// receive and dispatch OSC bundles
void listenForOSC() {

  static OSCBundle *bundleIN = new OSCBundle;
  static OSCMessage *messageIN = new OSCMessage;
  static listening_status_e listeningFor = BUNDLE_OR_MESSAGE_START;

  bool eot =  SLIPSerial.endofPacket();
  while (SLIPSerial.available() && !eot) {

    uint8_t data = SLIPSerial.read();

    // Reaper may send either OSC bundles or raw messages, unpredictably, so we must accept both equally
    // (surprisingly, the arduino OSC library does not handle this logic; it (unreasonably) expects you 
    // to know in advance which you'll be receiving. So we must do this peek-ahead here and track some state.)
    if (listeningFor == BUNDLE_OR_MESSAGE_START) {
      if (data == 35) {
        // "#"
        listeningFor = BUNDLE;
      } else if (data == 47) {
        // "/"
        listeningFor = MESSAGE;
      } else {
        char report[99];
        sprintf(report, "expected # (35) or / (47), got %c (%d)", data, data);
        sendOSCString("/foobar/error", "OSC got start of neither bundle nor message!");     
        sendOSCString("/foobar/error", report);
      }
    }

    // once we know what we're getting, we can just keep reading.
    if (listeningFor == BUNDLE) {
      bundleIN->fill(data);
    } else if (listeningFor == MESSAGE) {
      messageIN->fill(data);
    }

    eot =  SLIPSerial.endofPacket();
  }

  if (eot) {
    if ( (listeningFor == BUNDLE) && bundleIN->hasError() ) {
      
      // turn on a warning light for an OSC error
      digitalWrite(LED_BUILTIN, 1); // @#@d

      int err = bundleIN->getError();
      char report[99];
      sprintf(report, "%d: '%s' (%d)", err, bundleIN->incomingBuffer, bundleIN->incomingBufferSize);
      sendOSCString("/foobar/error", report);
      sendOSCString("/foobar/error", bundleIN->errorDetails);
      bundleIN->errorDetails[0] = 0;

    } else if (listeningFor == BUNDLE) {

      char report[99];
      sprintf(report, "got; (%d) '%s'", bundleIN->incomingBufferSize, bundleIN->incomingBuffer);
      //sendOSCString("/foobar/bundle", report);

      dispatchBundleContents(bundleIN);
      bundleIN->empty();
      listeningFor = BUNDLE_OR_MESSAGE_START;

    } else if (listeningFor == MESSAGE) {

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

/// act on the contents of an incoming OSC bundle
void dispatchBundleContents(OSCBundle *bundleIN) {

  last_OSC_receive_time = millis();
  
  bundleIN->dispatch("/record", handleOSC_Record);
  bundleIN->dispatch("/track/1/fx/*/bypass", handleOSC_FxBypass);
  bundleIN->dispatch("/track/1/fx/4/fxparam/2/value", handleOSC_Fx4Fxparam2);
}

/// act on an incoming OSC message
void dispatchMessage(OSCMessage *messageIN) {

  last_OSC_receive_time = millis();

  messageIN->dispatch("/record", handleOSC_Record);
  messageIN->dispatch("/track/1/fx/*/bypass", handleOSC_FxBypass);
  messageIN->dispatch("/track/1/fx/4/fxparam/2/value", handleOSC_Fx4Fxparam2);
}

// Handle incoming OSC messages...

/// handle Record status update
void handleOSC_Record(OSCMessage &msg) {

  byte status = msg.getFloat(0);
  daw_state.recording = (status != 0.0);
  updateRecordButtonColor();
}

/// make Record lamp show Record status
void updateRecordButtonColor() {

  if (daw_state.recording) {
    leds[0] = CHSV(H_RED, S_FULL, V_FULL);
  } else {
    leds[0] = CHSV(H_RED, S_FULL, V_DIM);
  }
  FastLED.show();
}

/// handle fx bypass status update
void handleOSC_FxBypass(OSCMessage &msg) {

  // which plugin index? (1-based)
  const int buffer_size = strlen("/track/1/fx/*/bypass") + 1;
  char buffer[buffer_size];
  msg.getAddress(buffer);
  int fx = buffer[12] - '0'; // e.g. "/track/1/fx/3/bypass" -> 3

  // track the change
  int value = msg.getFloat(0);
  daw_state.fx_bypass[fx] = (value == 0);
  updateLampColors();

}

/// handle Anvil amp channel param
void handleOSC_Fx4Fxparam2(OSCMessage &msg) {

  // float in message seems to be always 0?? ignore it
  msg.empty();

  updateLampColors();

}

/// make lamps reflect status of DAW controls
// (as well as we can; we may not have full knowledge of the ground truth)
void updateLampColors() {

  // three pretty colors for the amp channels (clean = green, rhythm = blue, lead = hot pink)
  static CRGB amp_channel_hue[3] = {
    CHSV(H_AQUA, S_VINTAGE_LAMP, V_FULL),
    CHSV(H_BLUE, S_VINTAGE_LAMP, V_FULL), 
    CHSV(H_PINK, S_VINTAGE_LAMP, V_FULL)
  };
  
  // set lamp colors
  for (int ii = 1; ii <= 5; ii++) {

    if (ii == 5) {

      // I'm using lamp 5 (and button 5) for amp channel cycle (for now @#@t)
      if (daw_state.fx_bypass[button_config[ii].fx_index]) {
        leds[ii] = amp_channel_hue[daw_state.amp_channel].scale8(V_DIM);
      } else {
        leds[ii] = amp_channel_hue[daw_state.amp_channel];
      }
      
    } else {

      // the rest of the lamps are fx bypass toggles (for now @#@t)

      if (daw_state.fx_bypass[button_config[ii].fx_index]) {
        leds[ii] = CHSV(H_VINTAGE_LAMP, S_VINTAGE_LAMP, V_DIM);
      } else {
        leds[ii] = CHSV(H_VINTAGE_LAMP, S_VINTAGE_LAMP, V_FULL);
      } 
    }
    FastLED.show();
  }

}

// Send OSC messages...

/// send an OSC message over the serial port
void sendOSCMessage(OSCMessage &msg) {

  SLIPSerial.beginPacket();
  msg.send(SLIPSerial); // send the bytes to the SLIP stream
  SLIPSerial.endPacket(); // mark the end of the OSC Packet
  msg.empty(); // free space occupied by message
  last_OSC_send_time = millis();
  delay(MINIMUM_TIME_BETWEEN_OSC_SENDS); // throttle traffic to avoid crashing the connection. @#@t short blocking delay here is probably fine, but maybe not the best solution
}

/// send an OSC message to a specified OSC address, containing a single specified float parameter value
void sendOSCFloat(const char *address, float value) {

  OSCMessage msg(address);
  msg.add(value);
  sendOSCMessage(msg);

}

/// send an OSC message to a specified OSC address, containing a single specified int parameter value
void sendOSCInt(const char *address, int value) {
  
  OSCMessage msg(address);
  msg.add(value);
  sendOSCMessage(msg);

}

/// send an OSC message to a specified OSC address, containing a single specified string parameter value
void sendOSCString(const char *address, const char *value) {
  
  OSCMessage msg(address);
  msg.add(value);
  sendOSCMessage(msg);

}

/// send an OSC message to a specified OSC address, containing a single specified boolean parameter value
void sendOSCBool(const char *address, bool value) {
  
  OSCMessage msg(address);
  msg.add(value);
  sendOSCMessage(msg);

}

/// send an OSC message to a specified OSC address, containing no parameter values
void sendOSCTrigger(const char *address) {

  OSCMessage msg(address);
  sendOSCMessage(msg);

}

/// send an OSC message asking the DAW to start or stop recording
void sendRecordToggle() {
  sendOSCTrigger("/record");
}

/// send an OSC message asking the DAW to enable or disable the specified fx plugin.
void sendFxBypassBool(int track, int fx, bool value) {

  // native Reaper OSC FX_BYPASS command doesn't work, or I'm not implementing it properly.
  // (Reaper's OSC default config file is sparsely and tersely documented, with almost no discussion of how Reaper actually behaves over OSC.) 
  // Luckily we have the S&M commands, and this sequence seems to work:

  String addr = "/action/";
  addr = addr + (40938 + track); // track 1: action 40939; track 2: action 40940, etc. No track 0.
  sendOSCInt(addr.c_str(), 1);
  
  String msg = "_S&M_FXBYP";
  msg = msg + fx; // fx = 1 thru 8 only
  sendOSCString("/action/str", msg.c_str());
  
}

/// send an OSC message asking the DAW to set the specified fx plugin parameter to the specified (normalized float) value
void sendFxParamFloat(int track, int fx, int param, float value) {

  // Reaper OSC pattern "n/track/@/fx/@/fxparam/@/value" -- n is for normalized (0.0-1.0)

  String addr = "/track/";
  addr = addr + track;
  addr = addr + "/fx/";
  addr = addr + fx;
  addr = addr + "/fxparam/";
  addr = addr + param;
  addr = addr + "/value";
  sendOSCFloat(addr.c_str(), value);

}

// currently, we just wait a bit after each send, so we don't need tooSoon() anymore.
// (there are several ways to deal with traffic throttling; we might change schemes again at some point)

/*
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
*/

/// send an OSC message controlling the "NA Wah" fx plugin (currently hardcoded) at position 2.
void sendWah(float value) {
  
  sendOSCFloat("/track/1/fx/2/fxparam/1/value", value);

}

/// keep track of OSC traffic and warn if commands are not receiving timely feedback 
// (indicating likely serial port disconnection: PC Bridge stopped, DAW not running, DAW OSC ports misconfigured, etc.)
void watchForDisconnection() {

  const time_ms too_long = 3000;

  time_ms now = millis();
  
  static bool connected = false;
  static bool status_changed = true;
  
  // if there hasn't been a reply in a while...
  if (last_OSC_send_time > last_OSC_receive_time + too_long) {

    // ...it means we got disconnected, but only IF the last send itself was a while ago...
    // (we need to give the reply a chance to arrive, so the above test is invalid immediately after a send)
    if (now > last_OSC_send_time + too_long) { 

      if (connected) {
        status_changed = true;
      }
      connected = false;
    }

  } else {
    // ...but if there HAS been a reply recently, we're connected

    if (!connected) {
      status_changed = true;
    }
    connected = true;
  }

  if (status_changed) {
    digitalWrite(LED_BUILTIN, connected ? 0 : 1);
    status_changed = false;
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
  
  setupDawState();
  setupButtons();

  // set up and clear controls status
  setupControls();

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

  watchForDisconnection();

//  idleAnimation(); // (optional; not real-time optimized)
 
} // loop


// EOF.
