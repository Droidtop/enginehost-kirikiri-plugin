/*
 * windowEx.dll: wamsoft's Windows-only window-chrome extension plugin
 * (main.cpp is ~2600 lines of HWND/dbt.h/imm32 code: custom non-client
 * hit-testing, device-change/hotkey registration, IME control, per-monitor
 * DPI, overlay bitmaps during load, etc). None of that has a meaningful
 * equivalent on this engine's Android/SDL window, so this is not a port of
 * that logic -- it is a stub that makes Plugins.link("windowEx.dll")
 * succeed and gives scripts the same named members to probe, per the task.
 *
 * Noble Works' own scripts already treat windowEx.dll as optional: every
 * link site wraps Plugins.link("windowEx.dll") in try/catch, and every
 * script call site checked (MainWindow.tjs, override.tjs, Menus.tjs,
 * KAGEnvPlayer.tjs, custom.tjs, and their patch3/ counterparts) guards the
 * call with `typeof win.<member> != "undefined"` (or `== "Object"`) first.
 * So the game already tolerates windowEx.dll being entirely absent -- this
 * stub is for closer parity/fewer surprises, not because anything here is
 * launch-blocking the way scriptsEx was.
 *
 * What this file actually provides, and why:
 *  - Window.<member>: every member the original attached to the Window
 *    class (minimize, maximize, maximizeBox/minimizeBox, maximized/minimized,
 *    showRestore, resetWindowIcon/setWindowIcon, getWindowRect/getClientRect/
 *    setClientRect/getNormalRect, disableResize/disableMove, setOverlayBitmap,
 *    exSystemMenu/resetExSystemMenu, enableNCMouseEvent, ncHitTest,
 *    focusMenuByKey, setMessageHook, bringTo/sendToBack, registerDeviceChange,
 *    registerHotKey, acquireImeControl/resetImeContext,
 *    setWindowCornerPreference) plus the nchtXXX hit-test constants (their
 *    fixed WM_NCHITTEST values, hardcoded below -- no windows.h needed) --
 *    all INERT: getters return false/0/a zeroed rect, setters and actions
 *    are no-ops. This is enough for `typeof` probes and for any call that
 *    slips through a probe to not throw.
 *  - Scripts.eval / Scripts.setEvalErrorLog: ported for real. This part of
 *    the original has nothing to do with windows.h -- it wraps the existing
 *    Scripts.eval to optionally swallow eval errors instead of logging them,
 *    which is pure TJS-level logic.
 *  - System.breathe/isBreathing/clearGraphicCache/getAboutString/getCPUType:
 *    ported for real. The original only bundled these here as a convenience;
 *    each one forwards to a portable C++ export this engine already has
 *    (TVPBreathe/TVPGetBreathing/TVPClearGraphicCache/TVPGetAboutString/
 *    TVPGetCPUType, declared in EventIntf.h/GraphicsLoaderIntf.h/MsgIntf.h/
 *    DetectCPU.h) and none of them touch Win32. Noble Works' KAGEnvPlayer.tjs
 *    calls System.breathe() (behind its own typeof guard) while an overlay
 *    is up during a slow load, so making it real is strictly better than a
 *    no-op.
 *  - Deliberately NOT provided at all (left undefined, matching what
 *    Noble Works' own typeof-guards already expect when windowEx.dll isn't
 *    present): MenuItem.popupEx and the biXXX constants, Pad, Debug.console's
 *    extra members, and every other System.* member from the original
 *    (getDisplayMonitors, getMonitorInfo, getCursorPos, setCursorPos,
 *    setClipCursor, getSystemMetrics, readEnvValue, expandEnvString,
 *    setApplicationIcon, setIconicPreview, getDoubleClickTime,
 *    setDpiAwareness, findWindowEx, classLongPtr, loadCursor, mapVirtualKey).
 *    These are genuinely Win32 API calls (HWND, GetSystemMetrics, monitor
 *    enumeration, etc.) with no portable equivalent here, and grepping
 *    Noble Works' extracted scripts found no call site that doesn't already
 *    check `typeof System.<member>` first, so leaving them undefined is
 *    exactly what those scripts already handle.
 */
#include "ncbind/ncbind.hpp"
#include "EventIntf.h"
#include "GraphicsLoaderIntf.h"
#include "MsgIntf.h"
#include "DetectCPU.h"

#define NCB_MODULE_NAME TJS_W("windowEx.dll")

// Fixed WM_NCHITTEST values (Win32 winuser.h). Hardcoded since this build
// has no windows.h; only used here as opaque integer constants scripts
// compare ncHitTest()'s (always-inert) return value against.
enum {
	kHTERROR         = -2,
	kHTTRANSPARENT   = -1,
	kHTNOWHERE       = 0,
	kHTCLIENT        = 1,
	kHTCAPTION       = 2,
	kHTSYSMENU       = 3,
	kHTGROWBOX       = 4,
	kHTSIZE          = 4,
	kHTMENU          = 5,
	kHTHSCROLL       = 6,
	kHTVSCROLL       = 7,
	kHTMINBUTTON     = 8,
	kHTREDUCE        = 8,
	kHTMAXBUTTON     = 9,
	kHTZOOM          = 9,
	kHTLEFT          = 10,
	kHTRIGHT         = 11,
	kHTTOP           = 12,
	kHTTOPLEFT       = 13,
	kHTTOPRIGHT      = 14,
	kHTBOTTOM        = 15,
	kHTBOTTOMLEFT    = 16,
	kHTBOTTOMRIGHT   = 17,
	kHTBORDER        = 18
};

static tTJSVariant MakeZeroRect()
{
	iTJSDispatch2 *dict = TJSCreateDictionaryObject();
	tTJSVariant v0((tjs_int)0);
	dict->PropSet(TJS_MEMBERENSURE, TJS_W("x"), NULL, &v0, dict);
	dict->PropSet(TJS_MEMBERENSURE, TJS_W("y"), NULL, &v0, dict);
	dict->PropSet(TJS_MEMBERENSURE, TJS_W("w"), NULL, &v0, dict);
	dict->PropSet(TJS_MEMBERENSURE, TJS_W("h"), NULL, &v0, dict);
	tTJSVariant result(dict, dict);
	dict->Release();
	return result;
}

/**
 * WindowEx: inert stand-ins for wamsoft's Window chrome-control surface.
 */
class WindowEx {
public:
	WindowEx(iTJSDispatch2 * /*obj*/) {}

	// Actions: no-ops.
	void minimize() {}
	void maximize() {}
	void showRestore() {}
	void resetWindowIcon() {}
	void setWindowIcon(tTJSVariant) {}
	void setClientRect(tjs_int, tjs_int, tjs_int, tjs_int) {}
	void setOverlayBitmap() {}
	void resetExSystemMenu() {}
	void focusMenuByKey(tjs_int) {}
	void setMessageHook(tTJSVariant) {}
	void bringTo() {}
	void sendToBack() {}
	void registerDeviceChange(bool) {}
	bool registerHotKey(tjs_int, tjs_int, tjs_int) { return false; }
	void acquireImeControl() {}
	void resetImeContext() {}
	void setWindowCornerPreference(tjs_int) {}

	// Getters/setters backed by harmless local state.
	bool getMaximizeBox() const { return false; }
	void setMaximizeBox(bool) {}
	bool getMinimizeBox() const { return false; }
	void setMinimizeBox(bool) {}
	bool getMaximized() const { return false; }
	void setMaximized(bool) {}
	bool getMinimized() const { return false; }
	void setMinimized(bool) {}
	bool getDisableResize() const { return false; }
	void setDisableResize(bool) {}
	bool getDisableMove() const { return false; }
	void setDisableMove(bool) {}
	bool getExSystemMenu() const { return false; }
	void setExSystemMenu(tTJSVariant) {}
	bool getEnNCMEvent() const { return false; }
	void setEnNCMEvent(bool) {}

	tTJSVariant getWindowRect() { return MakeZeroRect(); }
	tTJSVariant getClientRect() { return MakeZeroRect(); }
	tTJSVariant getNormalRect() { return MakeZeroRect(); }

	tjs_int nonClientHitTest(tjs_int, tjs_int) { return kHTNOWHERE; }
};

NCB_GET_INSTANCE_HOOK(WindowEx)
{
	NCB_INSTANCE_GETTER(objthis) {
		ClassT* obj = GetNativeInstance(objthis);
		if (!obj) {
			obj = new ClassT(objthis);
			SetNativeInstance(objthis, obj);
		}
		return obj;
	}
	~NCB_GET_INSTANCE_HOOK_CLASS() {}
};

NCB_ATTACH_CLASS_WITH_HOOK(WindowEx, Window)
{
	Variant(TJS_W("nchtError"),       (tjs_int)kHTERROR);
	Variant(TJS_W("nchtTransparent"), (tjs_int)kHTTRANSPARENT);
	Variant(TJS_W("nchtNoWhere"),     (tjs_int)kHTNOWHERE);
	Variant(TJS_W("nchtClient"),      (tjs_int)kHTCLIENT);
	Variant(TJS_W("nchtCaption"),     (tjs_int)kHTCAPTION);
	Variant(TJS_W("nchtSysMenu"),     (tjs_int)kHTSYSMENU);
	Variant(TJS_W("nchtSize"),        (tjs_int)kHTSIZE);
	Variant(TJS_W("nchtGrowBox"),     (tjs_int)kHTGROWBOX);
	Variant(TJS_W("nchtMenu"),        (tjs_int)kHTMENU);
	Variant(TJS_W("nchtHScroll"),     (tjs_int)kHTHSCROLL);
	Variant(TJS_W("nchtVScroll"),     (tjs_int)kHTVSCROLL);
	Variant(TJS_W("nchtMinButton"),   (tjs_int)kHTMINBUTTON);
	Variant(TJS_W("nchtReduce"),      (tjs_int)kHTREDUCE);
	Variant(TJS_W("nchtMaxButton"),   (tjs_int)kHTMAXBUTTON);
	Variant(TJS_W("nchtZoom"),        (tjs_int)kHTZOOM);
	Variant(TJS_W("nchtLeft"),        (tjs_int)kHTLEFT);
	Variant(TJS_W("nchtRight"),       (tjs_int)kHTRIGHT);
	Variant(TJS_W("nchtTop"),         (tjs_int)kHTTOP);
	Variant(TJS_W("nchtTopLeft"),     (tjs_int)kHTTOPLEFT);
	Variant(TJS_W("nchtTopRight"),    (tjs_int)kHTTOPRIGHT);
	Variant(TJS_W("nchtBottom"),      (tjs_int)kHTBOTTOM);
	Variant(TJS_W("nchtBottomLeft"),  (tjs_int)kHTBOTTOMLEFT);
	Variant(TJS_W("nchtBottomRight"), (tjs_int)kHTBOTTOMRIGHT);
	Variant(TJS_W("nchtBorder"),      (tjs_int)kHTBORDER);

	NCB_METHOD(minimize);
	NCB_METHOD(maximize);
	NCB_PROPERTY(maximizeBox, getMaximizeBox, setMaximizeBox);
	NCB_PROPERTY(minimizeBox, getMinimizeBox, setMinimizeBox);
	NCB_PROPERTY(maximized,   getMaximized,   setMaximized);
	NCB_PROPERTY(minimized,   getMinimized,   setMinimized);
	NCB_METHOD(showRestore);
	NCB_METHOD(resetWindowIcon);
	NCB_METHOD(setWindowIcon);
	NCB_METHOD(getWindowRect);
	NCB_METHOD(getClientRect);
	NCB_METHOD(setClientRect);
	NCB_METHOD(getNormalRect);
	NCB_PROPERTY(disableResize, getDisableResize, setDisableResize);
	NCB_PROPERTY(disableMove,   getDisableMove,   setDisableMove);
	NCB_METHOD(setOverlayBitmap);
	NCB_PROPERTY(exSystemMenu, getExSystemMenu, setExSystemMenu);
	NCB_METHOD(resetExSystemMenu);
	NCB_PROPERTY(enableNCMouseEvent, getEnNCMEvent, setEnNCMEvent);
	NCB_METHOD_DIFFER(ncHitTest, nonClientHitTest);
	NCB_METHOD(focusMenuByKey);
	NCB_METHOD(setMessageHook);
	NCB_METHOD(bringTo);
	NCB_METHOD(sendToBack);
	NCB_METHOD(registerDeviceChange);
	NCB_METHOD(registerHotKey);
	NCB_METHOD(acquireImeControl);
	NCB_METHOD(resetImeContext);
	NCB_METHOD(setWindowCornerPreference);
}

// ---------------------------------------------------------------------
// Scripts.eval / Scripts.setEvalErrorLog: ported for real (pure TJS-level
// logic in the original, no Win32 dependency).
struct ScriptsEvalOverride
{
	static iTJSDispatch2 *evalOrig;
	static bool outputErrorLogOnEval;

	// property Scripts.outputErrorLogOnEval-equivalent setter
	static bool setEvalErrorLog(bool v) {
		bool ret = outputErrorLogOnEval;
		outputErrorLogOnEval = v;
		return ret;
	}

	// Scripts.eval override
	static tjs_error TJS_INTF_METHOD eval(tTJSVariant *r, tjs_int n, tTJSVariant **p, iTJSDispatch2 *objthis) {
		if (outputErrorLogOnEval && evalOrig) return evalOrig->FuncCall(0, NULL, NULL, r, n, p, objthis);

		if (n < 1) return TJS_E_BADPARAMCOUNT;
		ttstr content = *p[0], name;
		tjs_int lineofs = 0;
		if (n >= 2) name    = *p[1];
		if (n >= 3) lineofs = *p[2];

		TVPExecuteExpression(content, name, lineofs, r);
		return TJS_S_OK;
	}
	static void Regist() {
		tTJSVariant var;
		TVPExecuteExpression(TJS_W("Scripts.eval"), &var);
		evalOrig = var.AsObject();
	}
	static void UnRegist() {
		if (evalOrig) { evalOrig->Release(); evalOrig = NULL; }
	}
};
iTJSDispatch2 *ScriptsEvalOverride::evalOrig = NULL;
bool           ScriptsEvalOverride::outputErrorLogOnEval = true;

NCB_ATTACH_FUNCTION(eval,            Scripts, ScriptsEvalOverride::eval);
NCB_ATTACH_FUNCTION(setEvalErrorLog, Scripts, ScriptsEvalOverride::setEvalErrorLog);

NCB_PRE_REGIST_CALLBACK(ScriptsEvalOverride::Regist);
NCB_PRE_UNREGIST_CALLBACK(ScriptsEvalOverride::UnRegist);

// ---------------------------------------------------------------------
// System.breathe/isBreathing/clearGraphicCache/getAboutString/getCPUType:
// ported for real -- portable forwards to existing engine exports, bundled
// in the original only as a convenience alongside the Win32-only members.
NCB_ATTACH_FUNCTION(breathe,           System, TVPBreathe);
NCB_ATTACH_FUNCTION(isBreathing,       System, TVPGetBreathing);
NCB_ATTACH_FUNCTION(clearGraphicCache, System, TVPClearGraphicCache);
NCB_ATTACH_FUNCTION(getAboutString,    System, TVPGetAboutString);
NCB_ATTACH_FUNCTION(getCPUType,        System, TVPGetCPUType);
