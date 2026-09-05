// Part of extnagano.dll, ported from wamsoft's extNagano plugin (src/re/wamsoft/extNagano, reconstructed from the original DLL). License: same as KiriKiri itself.

#ifndef extNagano_commonH
#define extNagano_commonH

#include "tp_stub.h"
#include "RenderManager.h"

//---------------------------------------------------------------------------
// extNagano 共通ヘルパ (extrans/common.h 由来 + 追加分)
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
static inline bool Clip(tjs_int &l, tjs_int &r, tjs_int cl, tjs_int cr)
{
	// 線分 l <-> r (l<r) を cl <-> cr (cl<cr) でクリッピングし結果を l r に返す。
	// 残れば真、消滅すれば偽。
	if(l < cl) l = cl;
	if(r > cr) r = cr;
	if(l >= r) return false;
	return true;
}
//---------------------------------------------------------------------------
static inline tjs_uint32 Blend(tjs_uint32 a, tjs_uint32 b, tjs_int opa)
{
	// a と b を混合比 opa で混合 ( opa = 0..255, 0 = a, 255 = b )
	tjs_uint32 ret;
	tjs_uint32 tmp;
	tmp = a & 0x000000ff;  ret   = 0x000000ff & (tmp + (( (b & 0x000000ff) - tmp ) * opa >> 8));
	tmp = a & 0x0000ff00;  ret  |= 0x0000ff00 & (tmp + (( (b & 0x0000ff00) - tmp ) * opa >> 8));
	tmp = a & 0x00ff0000;  ret  |= 0x00ff0000 & (tmp + (( (b & 0x00ff0000) - tmp ) * opa >> 8));
	tmp = a >> 24;
	ret  |= (0x000000ff & (tmp + (( (b >> 24) - tmp ) * opa >> 8))) << 24;
	return ret;
}
//---------------------------------------------------------------------------
static inline void Swap_tjs_int(tjs_int &a, tjs_int &b)
{
	tjs_int tmp = a; a = b; b = tmp;
}
//---------------------------------------------------------------------------
// iTVPScanLineProvider CPU-pixel compatibility shim
//   This fork's iTVPScanLineProvider (core/visual/transhandler.h) dropped its
//   CPU-side GetPixelFormat/GetPitchBytes/GetScanLine/GetScanLineForWrite --
//   the built-in transition handlers (TransIntf.cpp) moved to the GPU
//   RenderManager path instead. GetTexture()/GetTextureForRender() are still
//   part of the interface, and iTVPTexture2D itself still supports real
//   CPU readback/write (GetScanLineForRead/GetScanLineForWrite, implemented
//   for the live OpenGL texture class in ogl/RenderManager_ogl.cpp), so these
//   wrappers restore the same scanline access this plugin's algorithms need
//   without touching any core file. (Same shim as extranscommon.h; duplicated
//   rather than shared since extrans.dll and extnagano.dll are independent
//   internal modules that must not depend on each other's headers.)
static inline tjs_error TVPSLPGetPixelFormat(iTVPScanLineProvider *p, tjs_int *bpp)
{
	if(!p) return TJS_E_FAIL;
	if(bpp) *bpp = 32; // every provider here is 32bpp ARGB
	return TJS_S_OK;
}
static inline tjs_error TVPSLPGetPitchBytes(iTVPScanLineProvider *p, tjs_int *pitch)
{
	if(!p) return TJS_E_FAIL;
	iTVPTexture2D *tex = p->GetTexture();
	if(!tex) return TJS_E_FAIL;
	if(pitch) *pitch = tex->GetPitch();
	return TJS_S_OK;
}
static inline tjs_error TVPSLPGetScanLine(iTVPScanLineProvider *p, tjs_int line, const void **scanline)
{
	if(!p || !scanline) return TJS_E_FAIL;
	iTVPTexture2D *tex = p->GetTexture();
	if(!tex) return TJS_E_FAIL;
	const void *sl = tex->GetScanLineForRead((tjs_uint)line);
	if(!sl) return TJS_E_FAIL;
	*scanline = sl;
	return TJS_S_OK;
}
static inline tjs_error TVPSLPGetScanLineForWrite(iTVPScanLineProvider *p, tjs_int line, void **scanline)
{
	if(!p || !scanline) return TJS_E_FAIL;
	iTVPTexture2D *tex = p->GetTextureForRender();
	if(!tex) return TJS_E_FAIL;
	void *sl = tex->GetScanLineForWrite((tjs_uint)line);
	if(!sl) return TJS_E_FAIL;
	*scanline = sl;
	return TJS_S_OK;
}
//---------------------------------------------------------------------------

#endif
