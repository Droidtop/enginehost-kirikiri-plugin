/*
 * motionplayer.dll: the Motion class that E-mote's KiriKiri plugin provides.
 *
 * Yuzusoft's shared system scripts (system/motion.tjs, system/AffineLayer.tjs,
 * system/AnimKAGLayer.tjs, sysscn/GFX_Motion.tjs) link motionplayer.dll and
 * then build Motion.ResourceManager, Motion.Player and, for a layer that owns
 * its own drawing target, Motion.SeparateLayerAdaptor. Without those classes
 * the script set fails wherever a PSB is loaded.
 *
 * Noble Works does carry PSB motions -- yuzulogo.psb and m2logo.psb in the
 * brand-logo sequence, title_bg.psb and calendar.psb later -- so this sits on
 * the path to the title screen, not on optional scaffolding. Playing them
 * means implementing E-mote itself, which is a separate and much larger piece
 * of work; what this module does is give the scripts the whole surface they
 * call, so a game with PSB assets runs with that art static instead of
 * stopping on a TJS type error.
 *
 * Every method here accepts what the scripts pass and plays nothing. The
 * player always reports "not playing", so the KAG side's motion waits fall
 * through immediately and the scenario continues. Each distinct motion the
 * game asked for is named once in the log, which is what a report needs.
 */
#include "ncbind/ncbind.hpp"
#include "MsgIntf.h"

#include <set>

#define NCB_MODULE_NAME TJS_W("motionplayer.dll")

/**
 * Names a motion in the log the first time it is asked for.
 * progress()/draw() run once per frame, so nothing on the draw path logs.
 */
static void ReportUnplayed(const ttstr &what)
{
	static std::set<ttstr> reported;
	if (what.IsEmpty()) return;
	if (!reported.insert(what).second) return;
	TVPAddLog(ttstr(TJS_W("motionplayer: \"")) + what +
	          TJS_W("\" requested; E-mote playback is not implemented, the art stays static"));
}

/** Motion.ResourceManager: owns the loaded motion storages for one window. */
class MotionResourceManager {
public:
	MotionResourceManager(tTJSVariant window, tjs_int cacheSize) {}

	void load(ttstr path) { ReportUnplayed(path); }
	void unload(ttstr path) {}
	void clearCache() {}
};

/**
 * Motion.SeparateLayerAdaptor: the drawing target a layer hands the player
 * when the motion is composed onto a surface of its own rather than onto the
 * layer itself. The scripts construct it, pass it back to the player as a
 * draw target, assign() one from another when a layer is cloned, and
 * invalidate it. It holds no pixels here because nothing draws into it.
 */
class MotionSeparateLayerAdaptor {
public:
	MotionSeparateLayerAdaptor(tTJSVariant layer) : _layer(layer) {}

	void assign(tTJSVariant other) {}

private:
	tTJSVariant _layer;
};

/** Motion.Player: plays one character's motion onto a layer. */
class MotionPlayer {
public:
	MotionPlayer(tTJSVariant resourceManager) {}

	// --- playback -------------------------------------------------------
	void play(ttstr motion, tjs_int flags) {
		_motion = motion;
		ReportUnplayed(motion);
	}
	void stop() {}
	void skipToSync() {}
	void progress(tjs_int interval) {}

	// --- drawing --------------------------------------------------------
	void draw(tTJSVariant layer) {}
	void clear(tTJSVariant layer, tTJSVariant color) {}
	void setCoord(tjs_real x, tjs_real y) {}
	void setZoom(tjs_real x, tjs_real y) {}
	void setFlip(bool x, bool y) {}
	void setSlant(tjs_real x, tjs_real y) {}

	// --- queries the KAG layers make ------------------------------------
	bool isExistMotion(ttstr motion) { return false; }
	bool contains(tjs_real x, tjs_real y) { return false; }
	/** A per-part layer getter; void means "this motion has no such part". */
	tTJSVariant getLayerGetter(ttstr name) { return tTJSVariant(); }

	// --- properties -----------------------------------------------------
	bool getPlaying() const { return false; }
	void setPlaying(bool value) {}
	bool getAllPlaying() const { return false; }
	/** Motion tag list; the scripts skip the whole block when it is void. */
	tTJSVariant getTags() const { return tTJSVariant(); }
	/** Length of the current motion in ms; nothing plays, so zero. */
	tjs_int getLastTime() const { return 0; }
	ttstr getChara() const { return _chara; }
	void setChara(ttstr value) { _chara = value; }
	ttstr getMotion() const { return _motion; }
	void setMotion(ttstr value) { _motion = value; ReportUnplayed(value); }
	ttstr getStealthChara() const { return _stealthChara; }
	void setStealthChara(ttstr value) { _stealthChara = value; }
	ttstr getStealthMotion() const { return _stealthMotion; }
	void setStealthMotion(ttstr value) { _stealthMotion = value; }
	tjs_int getTickCount() const { return _tickCount; }
	void setTickCount(tjs_int value) { _tickCount = value; }
	tjs_real getSpeed() const { return _speed; }
	void setSpeed(tjs_real value) { _speed = value; }
	tjs_real getAngleDeg() const { return _angleDeg; }
	void setAngleDeg(tjs_real value) { _angleDeg = value; }
	tjs_int getCompletionType() const { return _completionType; }
	void setCompletionType(tjs_int value) { _completionType = value; }

private:
	ttstr _chara, _motion, _stealthChara, _stealthMotion;
	tjs_int _tickCount = 0;
	tjs_real _speed = 1.0;
	tjs_real _angleDeg = 0.0;
	tjs_int _completionType = 0;
};

/** Motion: the namespace object the three classes hang from. */
class Motion {
};

NCB_REGISTER_SUBCLASS(MotionResourceManager) {
	NCB_CONSTRUCTOR((tTJSVariant, tjs_int));
	NCB_METHOD(load);
	NCB_METHOD(unload);
	NCB_METHOD(clearCache);
}

NCB_REGISTER_SUBCLASS(MotionSeparateLayerAdaptor) {
	NCB_CONSTRUCTOR((tTJSVariant));
	NCB_METHOD(assign);
}

NCB_REGISTER_SUBCLASS(MotionPlayer) {
	NCB_CONSTRUCTOR((tTJSVariant));
	NCB_METHOD(play);
	NCB_METHOD(stop);
	NCB_METHOD(skipToSync);
	NCB_METHOD(progress);
	NCB_METHOD(draw);
	NCB_METHOD(clear);
	NCB_METHOD(setCoord);
	NCB_METHOD(setZoom);
	NCB_METHOD(setFlip);
	NCB_METHOD(setSlant);
	NCB_METHOD(isExistMotion);
	NCB_METHOD(contains);
	NCB_METHOD(getLayerGetter);
	NCB_PROPERTY(playing, getPlaying, setPlaying);
	NCB_PROPERTY_RO(allplaying, getAllPlaying);
	NCB_PROPERTY_RO(tags, getTags);
	NCB_PROPERTY_RO(lastTime, getLastTime);
	NCB_PROPERTY(chara, getChara, setChara);
	NCB_PROPERTY(motion, getMotion, setMotion);
	NCB_PROPERTY(stealthChara, getStealthChara, setStealthChara);
	NCB_PROPERTY(stealthMotion, getStealthMotion, setStealthMotion);
	NCB_PROPERTY(tickCount, getTickCount, setTickCount);
	NCB_PROPERTY(speed, getSpeed, setSpeed);
	NCB_PROPERTY(angleDeg, getAngleDeg, setAngleDeg);
	NCB_PROPERTY(completionType, getCompletionType, setCompletionType);
}

NCB_REGISTER_CLASS(Motion) {
	NCB_SUBCLASS(ResourceManager, MotionResourceManager);
	NCB_SUBCLASS(SeparateLayerAdaptor, MotionSeparateLayerAdaptor);
	NCB_SUBCLASS(Player, MotionPlayer);
}

/*
 * Motion.PlayFlag*: the flag bits play()'s second argument takes. ncbind has
 * no macro for a class constant, so they are set on the class object once the
 * module is registered.
 *
 * The values come from the game's own scripts, not from E-mote's headers,
 * which are not public: Action.tjs passes a literal 1 where AffineLayer.tjs
 * passes Motion.PlayFlagForce for the same "start this motion now" case, so
 * Force is bit 0, and AnimKAGLayer.tjs ors Stealth with it, so Stealth is a
 * different bit -- bit 1 is the assumption. Nothing here acts on the flags;
 * they exist so the scripts can name and combine them. Whoever implements
 * real playback must confirm both values first.
 *
 * motion.tjs wraps Plugins.link("motionplayer.dll") in a bare try/catch, so a
 * throw out of here would be swallowed and the constants would silently read
 * as void (and coerce to 0, which this player would happily accept). Say so in
 * the log instead, so a device report shows it rather than hiding it.
 */
static void PostRegistCallback()
{
	static const tjs_char *assignments[] = {
		TJS_W("Motion.PlayFlagForce = 1"),
		TJS_W("Motion.PlayFlagStealth = 2"),
	};
	for (const tjs_char *expr : assignments) {
		try {
			TVPExecuteExpression(ttstr(expr));
		} catch (...) {
			TVPAddLog(ttstr(TJS_W("motionplayer: could not set a play flag (")) +
			          ttstr(expr) + TJS_W("); motions will be requested with flags 0"));
		}
	}
}
NCB_POST_REGIST_CALLBACK(PostRegistCallback);
