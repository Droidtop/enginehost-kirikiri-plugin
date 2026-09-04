/*
 * motionplayer.dll: the Motion class that E-mote's KiriKiri plugin provides.
 *
 * Yuzusoft's shared system scripts (system/motion.tjs, sysscn/GFX_Motion.tjs)
 * link motionplayer.dll and then define classes that extend Motion.Player and
 * construct Motion.ResourceManager at load time. Without the class the whole
 * script set fails to load and the game never starts, whether or not it has
 * a single E-mote asset. Noble Works ships the scaffolding and no PSB files.
 *
 * This module gives those scripts the class they reach for. Resource and
 * player objects accept every call the scripts make and play nothing: a game
 * that actually carries PSB motions gets its static art and a line in the
 * log naming each motion it wanted, which is what a report needs. Rendering
 * E-mote itself is a separate piece of work; this is what makes the games
 * without it start.
 */
#include "ncbind/ncbind.hpp"
#include "MsgIntf.h"

#define NCB_MODULE_NAME TJS_W("motionplayer.dll")

/** Motion.ResourceManager: owns loaded motion storages for a window. */
class MotionResourceManager {
public:
	MotionResourceManager(tTJSVariant window, tjs_int cacheSize) {}

	void load(ttstr path) {
		TVPAddLog(ttstr(TJS_W("motionplayer: E-mote motion \"")) + path + TJS_W("\" requested; E-mote playback is not implemented, static art is used"));
	}
	void unload(ttstr path) {}
	void clearCache() {}
};

/** Motion.Player: plays one character's motion onto a layer. */
class MotionPlayer {
public:
	MotionPlayer(tTJSVariant resourceManager) {}

	void play(ttstr motion, tjs_int flags) { _motion = motion; }
	void stop() {}
	void progress(tjs_int interval) {}
	void draw(tTJSVariant layer) {}
	void clear(tTJSVariant layer, tjs_uint32 color) {}
	void setZoom(tjs_real x, tjs_real y) {}

	bool getPlaying() const { return false; }
	void setPlaying(bool value) {}
	ttstr getChara() const { return _chara; }
	void setChara(ttstr value) { _chara = value; }
	ttstr getMotion() const { return _motion; }
	void setMotion(ttstr value) { _motion = value; }
	ttstr getStealthChara() const { return _stealthChara; }
	void setStealthChara(ttstr value) { _stealthChara = value; }
	ttstr getStealthMotion() const { return _stealthMotion; }
	void setStealthMotion(ttstr value) { _stealthMotion = value; }
	tjs_int getTickCount() const { return _tickCount; }
	void setTickCount(tjs_int value) { _tickCount = value; }
	tjs_real getSpeed() const { return _speed; }
	void setSpeed(tjs_real value) { _speed = value; }

private:
	ttstr _chara, _motion, _stealthChara, _stealthMotion;
	tjs_int _tickCount = 0;
	tjs_real _speed = 1.0;
};

/** Motion: the namespace object the two classes hang from. */
class Motion {
};

NCB_REGISTER_SUBCLASS(MotionResourceManager) {
	NCB_CONSTRUCTOR((tTJSVariant, tjs_int));
	NCB_METHOD(load);
	NCB_METHOD(unload);
	NCB_METHOD(clearCache);
}

NCB_REGISTER_SUBCLASS(MotionPlayer) {
	NCB_CONSTRUCTOR((tTJSVariant));
	NCB_METHOD(play);
	NCB_METHOD(stop);
	NCB_METHOD(progress);
	NCB_METHOD(draw);
	NCB_METHOD(clear);
	NCB_METHOD(setZoom);
	NCB_PROPERTY(playing, getPlaying, setPlaying);
	NCB_PROPERTY(chara, getChara, setChara);
	NCB_PROPERTY(motion, getMotion, setMotion);
	NCB_PROPERTY(stealthChara, getStealthChara, setStealthChara);
	NCB_PROPERTY(stealthMotion, getStealthMotion, setStealthMotion);
	NCB_PROPERTY(tickCount, getTickCount, setTickCount);
	NCB_PROPERTY(speed, getSpeed, setSpeed);
}

NCB_REGISTER_CLASS(Motion) {
	NCB_SUBCLASS(ResourceManager, MotionResourceManager);
	NCB_SUBCLASS(Player, MotionPlayer);
}
