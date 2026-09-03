package com.yuri.kirikiri2;

import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import org.tvp.kirikiri2.KR2Activity;

/**
 * The enginehost wrapper's activity. Kirikiroid2 is a touch and keyboard
 * application; a handheld drives it with a gamepad, so this activity
 * translates the pad two ways:
 *
 * Keys. Buttons become the keys a KAG game is driven by (A advance, B
 * cancel/menu, X skip, Y space, L1/R1 backlog, Start menu, Select system
 * menu). The D-pad already arrives as DPAD_* keys.
 *
 * Pointer. Many KiriKiri menus, title screens first of all, respond only to
 * the mouse, never to keys. The left stick therefore moves a cursor drawn
 * over the game, and while the cursor is shown A presses and releases a
 * touch at the cursor instead of sending Enter. The cursor hides after a few
 * seconds without stick input, and the buttons go back to being keys.
 */
public class MainActivity extends KR2Activity {
	static {
		// Standalone, Cocos2dx loads the engine from the manifest's
		// android.app.lib_name meta-data. Under enginehost this activity is
		// instantiated for the host's BundledActivityProxy, which carries no
		// meta-data of ours, so nothing loads the library and the first native
		// call dies with UnsatisfiedLinkError. Load it here; a second load of an
		// already-loaded library is a no-op.
		System.loadLibrary("krkr2yuri");
	}

	private static final long CURSOR_HIDE_MS = 3000;
	private static final float STICK_DEADZONE = 0.2f;
	private static final float CURSOR_SPEED_PX_PER_S = 900f;
	private static final long FRAME_MS = 16;

	private final Handler handler = new Handler(Looper.getMainLooper());
	private FrameLayout cursorLayer;
	private View cursorView;
	private float cursorX = -1f, cursorY = -1f;
	private float stickX, stickY;
	private long lastStickInput;
	private boolean cursorShown;
	private boolean clickHeld;
	private long clickDownTime;

	private final Runnable moveCursor = new Runnable() {
		@Override
		public void run() {
			if (Math.abs(stickX) > STICK_DEADZONE || Math.abs(stickY) > STICK_DEADZONE) {
				float step = CURSOR_SPEED_PX_PER_S * FRAME_MS / 1000f;
				setCursor(cursorX + stickX * step, cursorY + stickY * step);
				if (clickHeld) sendTouch(MotionEvent.ACTION_MOVE);
				handler.postDelayed(this, FRAME_MS);
			}
		}
	};

	private final Runnable hideCursor = new Runnable() {
		@Override
		public void run() {
			if (SystemClock.uptimeMillis() - lastStickInput >= CURSOR_HIDE_MS && !clickHeld) {
				cursorShown = false;
				if (cursorView != null) cursorView.setVisibility(View.GONE);
			}
		}
	};

	@Override
	public boolean isTaskRoot() {
		// Cocos2dx treats a non-root activity as a duplicate launcher instance
		// and finishes it in onCreate. Under enginehost the task is owned by the
		// host's launch screen beneath us, on purpose; this activity is the game.
		return true;
	}

	@Override
	public ClassLoader getClassLoader() {
		// Cocos2dx caches context.getClassLoader() for the JNI calls it makes on
		// its GL thread. The base enginehost context reports the host's loader,
		// while the runtime classes belong to this signed bundle's
		// DexClassLoader; without this the engine's first lookup finds nothing
		// and dies calling getFilesDir() on a null context.
		return MainActivity.class.getClassLoader();
	}

	@Override
	public int get_res_sd_operate_step() { return R.drawable.sd_operate_step; }

	@Override
	public void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		// A layer over the game for the cursor. Drawn, not an asset: a ring
		// with a translucent fill, sized for a handheld screen.
		cursorLayer = new FrameLayout(this);
		cursorLayer.setClickable(false);
		cursorLayer.setFocusable(false);
		cursorView = new View(this);
		GradientDrawable ring = new GradientDrawable();
		ring.setShape(GradientDrawable.OVAL);
		ring.setColor(Color.argb(90, 255, 255, 255));
		ring.setStroke(dp(2), Color.argb(230, 20, 20, 20));
		cursorView.setBackground(ring);
		cursorView.setVisibility(View.GONE);
		cursorLayer.addView(cursorView, new FrameLayout.LayoutParams(dp(22), dp(22)));
		addContentView(cursorLayer, new ViewGroup.LayoutParams(
				ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
	}

	private int dp(int value) {
		return Math.round(value * getResources().getDisplayMetrics().density);
	}

	// ---------------------------------------------------------------- pointer

	@Override
	public boolean dispatchGenericMotionEvent(MotionEvent event) {
		if ((event.getSource() & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK
				&& event.getAction() == MotionEvent.ACTION_MOVE) {
			float x = event.getAxisValue(MotionEvent.AXIS_X);
			float y = event.getAxisValue(MotionEvent.AXIS_Y);
			boolean wasMoving = stickX != 0f || stickY != 0f;
			stickX = Math.abs(x) > STICK_DEADZONE ? x : 0f;
			stickY = Math.abs(y) > STICK_DEADZONE ? y : 0f;
			boolean moving = stickX != 0f || stickY != 0f;
			if (moving) {
				lastStickInput = SystemClock.uptimeMillis();
				showCursor();
				if (!wasMoving) handler.post(moveCursor);
			} else {
				handler.removeCallbacks(hideCursor);
				handler.postDelayed(hideCursor, CURSOR_HIDE_MS);
			}
			return true;
		}
		return super.dispatchGenericMotionEvent(event);
	}

	private void showCursor() {
		if (cursorView == null) return;
		if (cursorX < 0f) {
			View root = getWindow().getDecorView();
			setCursor(root.getWidth() / 2f, root.getHeight() / 2f);
		}
		cursorShown = true;
		cursorView.setVisibility(View.VISIBLE);
	}

	private void setCursor(float x, float y) {
		View root = getWindow().getDecorView();
		cursorX = Math.max(0f, Math.min(root.getWidth() - 1, x));
		cursorY = Math.max(0f, Math.min(root.getHeight() - 1, y));
		if (cursorView != null) {
			cursorView.setX(cursorX - cursorView.getWidth() / 2f);
			cursorView.setY(cursorY - cursorView.getHeight() / 2f);
		}
	}

	/** A touch at the cursor, delivered to the game the way a finger would be. */
	private void sendTouch(int action) {
		long now = SystemClock.uptimeMillis();
		if (action == MotionEvent.ACTION_DOWN) clickDownTime = now;
		MotionEvent touch = MotionEvent.obtain(clickDownTime, now, action, cursorX, cursorY, 0);
		touch.setSource(InputDevice.SOURCE_TOUCHSCREEN);
		super.dispatchTouchEvent(touch);
		touch.recycle();
	}

	// ------------------------------------------------------------------- keys

	@Override
	public boolean dispatchKeyEvent(KeyEvent event) {
		int keyCode = event.getKeyCode();
		if (keyCode == KeyEvent.KEYCODE_BUTTON_A && cursorShown) {
			if (event.getAction() == KeyEvent.ACTION_DOWN && event.getRepeatCount() == 0) {
				clickHeld = true;
				lastStickInput = SystemClock.uptimeMillis();
				sendTouch(MotionEvent.ACTION_DOWN);
			} else if (event.getAction() == KeyEvent.ACTION_UP) {
				clickHeld = false;
				sendTouch(MotionEvent.ACTION_UP);
				handler.removeCallbacks(hideCursor);
				handler.postDelayed(hideCursor, CURSOR_HIDE_MS);
			}
			return true;
		}
		int translated = gamepadToKey(keyCode);
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
