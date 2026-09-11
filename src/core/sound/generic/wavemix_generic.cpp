// The volume-mixing route for a target with no hand-written one.
//
// sound/win32/WaveMixer.cpp fills _AudioMixS16 and _AudioMixF32 with its own
// C++ templates for one to eight channels and then calls this, giving the
// target a chance to replace the entries it can do better; sound/ARM/wavemix_arm.c
// replaces the stereo 16-bit one with a NEON version. Leaving both tables as
// they came in is a complete answer, so that is what this does.

#include <stdint.h>

typedef void(FAudioMix)(void *dst, const void *src, int samples, int16_t *volume);

#ifdef __cplusplus
extern "C" {
#endif

void TVPWaveMixer_ASM_Init(FAudioMix **func16, FAudioMix **func32)
{
	(void)func16;
	(void)func32;
}

#ifdef __cplusplus
}
#endif
