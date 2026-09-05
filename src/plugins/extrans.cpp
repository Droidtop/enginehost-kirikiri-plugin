/*
 * extrans.dll: wamsoft's additional transition-effect plugin (wave, mosaic,
 * turn, rotate, ripple). Ported from wamsoft's extrans plugin
 * (/root/re/wamsoft/extrans -- Main.cpp + wave/mosaic/turn/turntrans_table/
 * rotatebase/rotatetrans/ripple .cpp/.h), license "same as KiriKiri itself"
 * per its readme.txt.
 *
 * The original registered itself through a real DLL's V2Link/V2Unlink entry
 * points (see wamsoft's simplebinder/v2link.cpp): TVPAddTransHandlerProvider
 * (declared in core/visual/TransIntf.h) is a plain extern function in this
 * fork -- not something reached through a dynamic export table -- so an
 * internal, statically-linked plugin can call it directly with no exported
 * V2Link/DllMain scaffolding at all. Registration only needs to happen once
 * a script actually links this module (Plugins.link("extrans.dll")), which
 * is exactly what ncbind's NCB_PRE_REGIST_CALLBACK/NCB_PRE_UNREGIST_CALLBACK
 * hooks give us (see windowex.cpp for the same pattern, and its own commit
 * fixing why the callback must be a plain file-scope function rather than a
 * qualified Class::method name).
 *
 * The five effects (wave/mosaic/turn/rotate/ripple) are unchanged from the
 * original algorithm: each is its own file, split out under this directory
 * as extranswave.*, extransmosaic.*, extransturn.*, extransturntranstable.*,
 * extransrotatebase.*, extransrotatetrans.*, extransripple.* and
 * extranscommon.h (the build globs every *.cpp in src/plugins, so the
 * original's per-plugin filenames -- wave.cpp, common.h, etc, which would
 * collide with extNagano's own wave-unrelated common.h -- were each given a
 * unique, extrans-prefixed name; contents beyond #include paths are
 * untouched). None of the ported code touches windows.h, intrin.h outside
 * an already-existing `#ifdef _M_IX86` guard (extransripple.cpp's MMX/EMMX
 * inline-asm fast paths, dead code on this arm64/gcc build; the portable C
 * fallback is what actually compiles and runs here), or any other
 * non-portable construct, so nothing needed rewriting for portability.
 */
#include "ncbind/ncbind.hpp"
#include "extranswave.h"
#include "extransmosaic.h"
#include "extransturn.h"
#include "extransrotatetrans.h"
#include "extransripple.h"

#define NCB_MODULE_NAME TJS_W("extrans.dll")

// NCB_PRE_REGIST_CALLBACK token-pastes the callback name into an
// identifier (see windowex.cpp's fix for the same issue), so keep these as
// plain, unqualified file-scope functions.
static void ExtransRegist()
{
	RegisterWaveTransHandlerProvider();
	RegisterMosaicTransHandlerProvider();
	RegisterTurnTransHandlerProvider();
	RegisterRotateTransHandlerProvider();
	RegisterRippleTransHandlerProvider();
}

static void ExtransUnregist()
{
	UnregisterWaveTransHandlerProvider();
	UnregisterMosaicTransHandlerProvider();
	UnregisterTurnTransHandlerProvider();
	UnregisterRotateTransHandlerProvider();
	UnregisterRippleTransHandlerProvider();
}

NCB_PRE_REGIST_CALLBACK(ExtransRegist);
NCB_PRE_UNREGIST_CALLBACK(ExtransUnregist);
