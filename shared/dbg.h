/*
 * dbg.h — Shared debug output macros
 *
 * Controlled by the DEBUG compile-time flag (Makefile: DEBUG=0 or DEBUG=1).
 * Uses Serial.printf — do NOT include in sketches that use Serial for
 * other purposes (e.g., range_test uses Serial for GPS).
 *
 * Named dbg.h (not debug.h) to avoid collision with CubeCell core's
 * board/inc/debug.h which is on the include path.
 */

#ifndef DBG_H
#define DBG_H

#ifndef DEBUG
#define DEBUG 1
#endif

#if DEBUG
  #define DBG(fmt, ...)  Serial.printf(fmt, ##__VA_ARGS__)
  #define DBGLN(msg)     Serial.println(msg)
  #define DBGP(msg)      Serial.print(msg)
#else
  #define DBG(fmt, ...)  ((void)0)
  #define DBGLN(msg)     ((void)0)
  #define DBGP(msg)      ((void)0)
#endif

#endif /* DBG_H */
