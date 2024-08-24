#include <OSCBundle.h>

void setup() {
  Serial.begin(115200);
  while (!Serial);
}
/*
const uint8_t data[] = {
  0x23, 0x62, 0x75, 0x6E, 0x64, 0x6C, 0x65, 0x00,  //  #bundle·
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  //  time = 0
  0x00, 0x00, 0x00, 0x18,                          //  size = 24
  0x2F, 0x74, 0x72, 0x61, 0x63, 0x6B, 0x2F, 0x31,  //  /track/1
  0x2F, 0x6D, 0x75, 0x74, 0x65, 0x00, 0x00, 0x00,  //  /mute···
  0x2C, 0x66, 0x00, 0x00,                          //  ,f··
  0x00, 0x00, 0x00, 0x00                           //  value = 0.0
};
*/

/*
const uint8_t data[] = {
  35, 98, 117, 110,  100, 108, 101, 0,            //  #bundle·
  0, 0, 0, 0,  1, 0, 0, 0,                        // time
  0, 0, 0, 40,                                    // size = 40
  47, 116, 114, 97,   99, 107, 47, 49,              // /track/1
  47, 102, 120, 47,   51,                           // /fx/3
  47, 102, 120, 112,  97, 114, 97, 109,   47, 53,   // /fxparam/5
  47, 118, 97, 108,   117, 101, 0, 0,     0,        // /value···
  44, 102, 0, 0,                                    // ,f··
  0, 0, 0, 0,                                       // [zero]
  0, 0, 0, 32,                                      // size = 32
  47, 116, 114, 97, 99, 107, 47, 49,                // /track/1
  47, 102, 120, 47, 51,                             // /fx/3
  47, 98, 121, 112, 97, 115, 115, 0, 0, 0, 0,       // /bypass····
  44, 102, 0, 0,                                    // ,f··
  63, 128, 0, 0                                     // [nonzero]
};
*/

const uint8_t data[] = {
  47, 116, 114, 97, 99, 107, 47, 49,                // /track/1
  47, 102, 120, 47, 51,                             // /fx/3
  47, 98, 121, 112, 97, 115, 115, 0, 0, 0, 0,       // /bypass···· 
  44, 102, 0, 0,                                    // ,f··
  63, 128, 0, 0                                     // [nonzero]
};

void muteHandler(OSCMessage &msg) {
  Serial.println("Mute");
}

// OSCBundle bundleIN;  // doesn't work
void loop() {
  static OSCBundle bundleIN;  // doesn't work

  bundleIN.fill(data, sizeof(data)/sizeof(*data));

  if (!bundleIN.hasError()) {
    bundleIN.dispatch("/track/*/fx/*/bypass", muteHandler);
  } else {
    Serial.print("Error: ");
  }
  bundleIN.empty();
  delay(2000);
}