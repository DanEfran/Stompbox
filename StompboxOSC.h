#ifndef INCLUDED_StompboxOSC_ALREADY

// OSC support
#include <OSCBundle.h>
// Note: OSC's clockless_trinket.h has a "#define DONE" that conflicts with other library code; I've renamed it to ASM_DONE
#include <OSCBoards.h>

// OSC-over-USB support
#include <SLIPEncodedSerial.h>
#ifdef BOARD_HAS_USB_SERIAL
extern SLIPEncodedUSBSerial SLIPSerial;
#else
extern SLIPEncodedSerial SLIPSerial;
#endif


typedef unsigned long time_ms;

typedef enum listening_status_e { BUNDLE_OR_MESSAGE_START, BUNDLE, MESSAGE } listening_status_e;

// timeliness of OSC feedback. (too slow probably means disconnected PC bridge)
extern time_ms last_OSC_send_time;
extern time_ms last_OSC_receive_time;
extern const time_ms MINIMUM_TIME_BETWEEN_OSC_SENDS;

void setupOSC();
void listenForOSC();

void sendOSCFloat(const char *address, float value);
void sendOSCInt(const char *address, int value);
void sendOSCString(const char *address, const char *value);
void sendOSCBool(const char *address, bool value);
void sendOSCTrigger(const char *address);

// caller provides these
void dispatchBundleContents(OSCBundle *bundleIN);
void dispatchMessage(OSCMessage *messageIN);

#define INCLUDED_StompboxOSC_ALREADY
#endif