package com.yuri.kirikiri2;
import android.view.KeyEvent;
import org.tvp.kirikiri2.KR2Activity;

public class MainActivity extends KR2Activity {
	static {
		// Enginehost loads this Activity directly rather than through Android's
		// manifest-level android.app.lib_name hook.
		System.loadLibrary("krkr2yuri");
	}
	@Override
	public boolean isTaskRoot() {
		// Cocos treats a non-root Activity as a duplicate launcher instance and
		// finishes it. Enginehost intentionally owns the task beneath us.
		return true;
	}
	@Override
	public ClassLoader getClassLoader() {
		// Cocos caches context.getClassLoader() for JNI calls made on its GL
		// thread. The base Enginehost context reports the host loader, while the
		// runtime classes belong to this signed bundle's DexClassLoader.
		return MainActivity.class.getClassLoader();
	}
	@Override
	public int get_res_sd_operate_step() { return R.drawable.sd_operate_step; }

	/**
	 * A gamepad plays a visual novel the way a keyboard does. Kirikiroid2's
	 * surface forwards keyboard keys to the engine but knows nothing of
	 * KEYCODE_BUTTON_*, so the wrapper translates them here, before the
	 * surface sees the event, into the keys a KAG game is driven by:
	 * A advance, B cancel/menu, X skip (Ctrl), Y hide/advance (Space),
	 * L1/R1 backlog (PageUp/PageDown), Start menu (Escape), Select system
	 * menu. D-pad keys already arrive as DPAD_* and pass through untouched.
	 */
	@Override
	public boolean dispatchKeyEvent(KeyEvent event) {
		int translated = gamepadToKey(event.getKeyCode());
		if (translated == KeyEvent.KEYCODE_UNKNOWN) {
			return super.dispatchKeyEvent(event);
		}
		KeyEvent mapped = new KeyEvent(event.getDownTime(), event.getEventTime(), event.getAction(),
				translated, event.getRepeatCount(), event.getMetaState(), event.getDeviceId(),
				event.getScanCode(), event.getFlags(), event.getSource());
		return super.dispatchKeyEvent(mapped);
	}

	private static int gamepadToKey(int keyCode) {
		switch (keyCode) {
			case KeyEvent.KEYCODE_BUTTON_A: return KeyEvent.KEYCODE_ENTER;
			case KeyEvent.KEYCODE_BUTTON_B: return KeyEvent.KEYCODE_ESCAPE;
			case KeyEvent.KEYCODE_BUTTON_X: return KeyEvent.KEYCODE_CTRL_LEFT;
			case KeyEvent.KEYCODE_BUTTON_Y: return KeyEvent.KEYCODE_SPACE;
			case KeyEvent.KEYCODE_BUTTON_L1: return KeyEvent.KEYCODE_PAGE_UP;
			case KeyEvent.KEYCODE_BUTTON_R1: return KeyEvent.KEYCODE_PAGE_DOWN;
			case KeyEvent.KEYCODE_BUTTON_START: return KeyEvent.KEYCODE_ESCAPE;
			case KeyEvent.KEYCODE_BUTTON_SELECT: return KeyEvent.KEYCODE_MENU;
			default: return KeyEvent.KEYCODE_UNKNOWN;
		}
	}
}
