// The graphics route for a target with no hand-written one.
//
// tvpgl.cpp holds a C implementation of every operation and TVPInitTVPGL()
// points the function table at them; visual/ARM/tvpgl_arm.cpp then replaces the
// entries it has a NEON version of. Where there is no such version the table is
// already correct, so the only thing missing is the entry point itself, which
// base/win32/SysInitImpl.cpp calls unconditionally after TVPDetectCPU().
//
// This is deliberately not a weak symbol or an #ifdef inside the ARM file: the
// ARM directory and this one are alternatives, and src/core/CMakeLists.txt
// compiles exactly one of them.

#ifdef __cplusplus
extern "C" {
#endif

void TVPGL_ASM_Init()
{
	// Nothing to swap in: the C routes tvpgl.cpp installed stand.
}

#ifdef __cplusplus
}
#endif
