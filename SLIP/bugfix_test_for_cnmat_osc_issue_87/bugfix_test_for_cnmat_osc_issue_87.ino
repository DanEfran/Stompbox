#include <OSCBundle.h>

void setup() {
  Serial.begin(115200);
  while (!Serial);
}

const uint8_t data[] = {
  0x23, 0x62, 0x75, 0x6E, 0x64, 0x6C, 0x65, 0x00,  //  #bundle·
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  //  time = 0
  0x00, 0x00, 0x00, 0x18,                          //  size = 24
  0x2F, 0x74, 0x72, 0x61, 0x63, 0x6B, 0x2F, 0x31,  //  /track/1
  0x2F, 0x6D, 0x75, 0x74, 0x65, 0x00, 0x00, 0x00,  //  /mute···
  0x2C, 0x66, 0x00, 0x00,                          //  ,f··
  0x00, 0x00, 0x00, 0x00                           //  value = 0.0
};

void muteHandler(OSCMessage &msg) {
  Serial.println("Mute");
}

// OSCBundle bundleIN;  // doesn't work
void loop() {
  static OSCBundle bundleIN;  // doesn't work

  bundleIN.fill(data, sizeof(data)/sizeof(*data));

  if (!bundleIN.hasError()) {
    bundleIN.dispatch("/track/*/mute", muteHandler);
  } else {
    Serial.print("Error: ");
  }
  bundleIN.empty();
  delay(2000);
}