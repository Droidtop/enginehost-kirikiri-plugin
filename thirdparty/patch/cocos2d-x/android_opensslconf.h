#ifdef __aarch64__
#include "opensslconf-arm64.h"
#elif defined(__arm__)
#include "opensslconf-arm32.h"
#elif defined(__x86_64__)
/* OpenSSL's generated Android configuration is LP64 for both 64-bit ABIs. */
#include "opensslconf-arm64.h"
#elif defined(__i386__)
#include "opensslconf-x86.h"
#else
#error "Unsupported architecture!"
#endif
