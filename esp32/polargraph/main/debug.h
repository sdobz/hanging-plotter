#ifndef DEBUG
#define DEBUG 3

#if defined(DEBUG) && DEBUG > 0
 #define DEBUG_PRINT(...) printf( __VA_ARGS__ );
#else
 #define DEBUG_PRINT(fmt, args...)
#endif

#endif