#include "StompboxOSC.h"
#include "StompboxLEDs.h"

// OSC-over-USB support
#include <SLIPEncodedSerial.h>
#ifdef BOARD_HAS_USB_SERIAL
SLIPEncodedUSBSerial SLIPSerial( thisBoardsSerialUSB );
#else
SLIPEncodedSerial SLIPSerial(Serial);
#endif


const time_ms MINIMUM_TIME_BETWEEN_OSC_SENDS = 10; // adjust to taste, but not less than about 5.
time_ms last_OSC_send_time;
time_ms last_OSC_receive_time;

/// open OSC-over-USB connection
void setupOSC() {
 
  SLIPSerial.begin(115200);

  last_OSC_send_time = millis();
  last_OSC_receive_time = last_OSC_send_time;

}

// Receive OSC messages...

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
      flashBuiltInLED();
      // setBuiltInLED(1); // @#@d
      // int err = bundleIN->getError();
      // char report[99];
      // sprintf(report, "OSC Error %d", err);
      // sendOSCString("/foobar/error", report);

      // sprintf(report, "%d: '%s' (%d)", err, bundleIN->incomingBuffer, bundleIN->incomingBufferSize);
      // sendOSCString("/foobar/error", bundleIN->errorDetails);
      // bundleIN->errorDetails[0] = 0;

      /*
        @#@#@ Known Issue: for some reason, reaper sends an OSC bundle sometimes; separate messages at other times. OSC is reporting an error in a bundle sometimes, which currently involves ignoring the bundle's contents. It seems like reaper may be misconfiguring the bundle regarding the word-alignment that OSC requires?
        The result is that the first stompbox button (after Record) is sending its messages but the feedback is getting clobbered, so it flashes the amber warning lamp rather than properly setting its own lamp state as the others do. Weird and annoying; but the workaround is to ignore that lamp, I guess.

        Example I/O: button 2's reply (2 messages) vs button 1's (bundle):

              <-   /action/40939,i☺

              <-   /action/str,s_S&M_FXBYP3

              =>/track/1/fx/3/fxparam/5/value~~~,f~~?~~<=
              =>/track/1/fx/3/bypass~~~~,f~~~~~~<=
              <-   /action/40939,i☺

              <-   /action/str,s_S&M_FXBYP2

              =>#bundle~~~~~☺~~~~~~(/track/1/fx/2/fxparam/3/value~~~,f~~~~~~~~~ /track/1/fx/2/bypass~~~~,f~~?~~<=
        
        Somewhere in there is a problem apparently.
      */

      dispatchBundleContents(bundleIN);
      bundleIN->empty();
      listeningFor = BUNDLE_OR_MESSAGE_START;

    } else if (listeningFor == BUNDLE) {

      // char report[99];
      // sprintf(report, "got; (%d) '%s'", bundleIN->incomingBufferSize, bundleIN->incomingBuffer);
      //sendOSCString("/foobar/bundle", report);

      dispatchBundleContents(bundleIN);
      bundleIN->empty();
      listeningFor = BUNDLE_OR_MESSAGE_START;

    } else if (listeningFor == MESSAGE) {

      // char report[99];
      // sprintf(report, "got; (%d) '%s'", messageIN->incomingBufferSize, messageIN->incomingBuffer);
      //sendOSCString("/foobar/message", report);

      dispatchMessage(messageIN);
      messageIN->empty();
      listeningFor = BUNDLE_OR_MESSAGE_START;
      
    }
    
  }
  // else wait for more...

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