/*
 * extnagano.dll: wamsoft-style additional transition-effect plugin
 * (3duniversal, blurfade, book, flutter, honeyturn, imagewipe, morphing,
 * multiripple, rgbfade, scanline, spin, zoomfade). Ported from
 * /root/re/wamsoft/extNagano -- itself reconstructed (per that tree's own
 * Main.cpp header) from a Ghidra decompile of the original extNagano.dll
 * plus extrans' own structure, since the original DLL's source was lost;
 * license "same as KiriKiri itself" per extrans' precedent (extNagano is
 * the same kind of Noble Works/wamsoft-ecosystem transition plugin).
 *
 * As with extrans.cpp, TVPAddTransHandlerProvider (core/visual/TransIntf.h)
 * is a plain extern function in this fork, so this internal, statically
 * linked plugin calls it directly with no exported V2Link/DllMain
 * scaffolding; registration happens on Plugins.link("extnagano.dll") via
 * ncbind's NCB_PRE_REGIST_CALLBACK/NCB_PRE_UNREGIST_CALLBACK hooks (see
 * windowex.cpp for the same pattern, and why the callback must be a plain
 * file-scope function rather than a qualified Class::method name).
 *
 * All twelve effects are unchanged from /root/re/wamsoft/extNagano's own
 * reconstruction: each is its own file, split out under this directory as
 * extnagano<name>.cpp/.h plus extnaganocommon.h (the build globs every
 * *.cpp in src/plugins, so the original's per-plugin filenames -- 3duniversal.cpp,
 * common.h, etc, which would otherwise collide with extrans' own
 * differently-shaped common.h -- were each given a unique,
 * extnagano-prefixed name; contents beyond #include paths are untouched).
 * That reconstruction's own comments already note the MMX/EMMX/x87 inline
 * assembler from the original DLL was deliberately not reproduced -- every
 * effect here is plain, portable C++ over pixel buffers already, so
 * nothing needed rewriting for portability and every effect is ported in
 * full (none stubbed).
 */
#include "ncbind/ncbind.hpp"
#include "extnagano3duniversal.h"
#include "extnaganoblurfade.h"
#include "extnaganobook.h"
#include "extnaganoflutter.h"
#include "extnaganohoneyturn.h"
#include "extnaganoimagewipe.h"
#include "extnaganomorphing.h"
#include "extnaganomultiripple.h"
#include "extnaganorgbfade.h"
#include "extnaganoscanline.h"
#include "extnaganospin.h"
#include "extnaganozoomfade.h"

#define NCB_MODULE_NAME TJS_W("extnagano.dll")

// NCB_PRE_REGIST_CALLBACK token-pastes the callback name into an
// identifier (see windowex.cpp's fix for the same issue), so keep these as
// plain, unqualified file-scope functions.
static void ExtnaganoRegist()
{
	Register3duniversalTransHandlerProvider();
	RegisterBlurFadeTransHandlerProvider();
	RegisterScanLineTransHandlerProvider();
	RegisterZoomFadeTransHandlerProvider();
	RegisterRGBFadeTransHandlerProvider();
	RegisterSpinFadeTransHandlerProvider();
	RegisterFlutterTransHandlerProvider();
	RegisterBookTransHandlerProvider();
	RegisterImageWipeTransHandlerProvider();
	RegisterHoneyTurnTransHandlerProvider();
	RegisterMorphingTransHandlerProvider();
	RegisterMultiRippleTransHandlerProvider();
}

static void ExtnaganoUnregist()
{
	Unregister3duniversalTransHandlerProvider();
	UnregisterBlurFadeTransHandlerProvider();
	UnregisterScanLineTransHandlerProvider();
	UnregisterZoomFadeTransHandlerProvider();
	UnregisterRGBFadeTransHandlerProvider();
	UnregisterSpinFadeTransHandlerProvider();
	UnregisterFlutterTransHandlerProvider();
	UnregisterBookTransHandlerProvider();
	UnregisterImageWipeTransHandlerProvider();
	UnregisterHoneyTurnTransHandlerProvider();
	UnregisterMorphingTransHandlerProvider();
	UnregisterMultiRippleTransHandlerProvider();
}

NCB_PRE_REGIST_CALLBACK(ExtnaganoRegist);
NCB_PRE_UNREGIST_CALLBACK(ExtnaganoUnregist);
