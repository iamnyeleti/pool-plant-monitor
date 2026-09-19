/*
  ============================================================
  vibration.h -- the "menu" for the vibration module
  ============================================================
  A header file's job is just to DECLARE what functions exist
  in this module, without containing the actual code for them.
  Other files (like main.cpp) #include this to know what they're
  allowed to call, without needing to see how it's implemented
  underneath -- that implementation detail lives in vibration.cpp.

  Think of this like a restaurant menu: it tells you what you can
  order, not how the kitchen makes it.
  ============================================================
*/

#ifndef VIBRATION_H   // "include guard" -- stops this file being
#define VIBRATION_H   // accidentally included twice in one build,
                       // which would cause duplicate-definition errors.

// Call this ONCE, from main.cpp's setup(). It connects to the
// MPU6050 sensor and configures it.
void vibrationSetup();

// Call this REPEATEDLY, from main.cpp's loop(). It takes one new
// reading, updates the rolling RMS calculation, and prints the
// current status to Serial.
void vibrationUpdate();

#endif
