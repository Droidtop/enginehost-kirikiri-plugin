package com.yuri.kirikiri2;

import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import java.util.HashMap;
import java.util.Iterator;
import java.util.Map;
import org.json.JSONObject;
import org.tvp.kirikiri2.KR2Activity;

/**
 * The enginehost wrapper's activity. Kirikiroid2 is a touch and keyboard
 * application; a handheld drives it with a gamepad, and what each pad button
 * means is the person's choice in Enginehost's controller settings, handed
 * to this activity as the CONTROLLER_BINDINGS extra (action id -> pad key or
 * axis). This class only decides what an action means to a KiriKiri game:
 *
 * Keys. confirm/cancel/menu/skip/auto/history/page_* become the keyboard
 * keys a KAG game is driven by; up/down/left/right become the D-pad keys.
 *
 * Pointer. Many KiriKiri menus, title screens first of all, answer only the
 * mouse. The stick bound to left_x/left_y therefore moves a cursor drawn over
 * the game, and while the cursor is shown the confirm action presses and
 * releases a touch at the cursor instead of sending Enter. The cursor hides
 * after a few seconds without stick input and confirm is a key again.
 */
public class MainActivity extends KR2Activity {
	private static final String TAG = "EnginehostKiriKiri";

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
	private final Map<Integer, String> padKeyActions = new HashMap<>();
	private final Map<Integer, String> padAxisActions = new HashMap<>();
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
		loadControllerBindings(getIntent().getStringExtra("dev.enginehost.runtime.CONTROLLER_BINDINGS"));
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

	private void loadControllerBindings(String json) {
		padKeyActions.clear();
		padAxisActions.clear();
		if (json == null) {
			Log.w(TAG, "No controller map from enginehost; the pad will do nothing");
			return;
		}
		try {
			JSONObject map = new JSONObject(json);
			Iterator<String> actions = map.keys();
			while (actions.hasNext()) {
				String action = actions.next();
				JSONObject binding = map.getJSONObject(action);
				if ("key".equals(binding.getString("type"))) {
					padKeyActions.put(binding.getInt("code"), action);
				} else if ("axis".equals(binding.getString("type"))) {
					padAxisActions.put(binding.getInt("axis"), action);
				}
			}
			Log.i(TAG, "Controller map: " + padKeyActions.size() + " buttons, " + padAxisActions.size() + " axes");
		} catch (Exception error) {
			Log.w(TAG, "Ignoring an unreadable controller map: " + error);
		}
	}

	private int dp(int value) {
		return Math.round(value * getResources().getDisplayMetrics().density);
	}

	private static boolean isPad(InputDevice device) {
		if (device == null) return false;
		int sources = device.getSources();
		return (sources & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
				|| (sources & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
	}

	// ---------------------------------------------------------------- pointer

	private int motionLogBudget = 20;

	@Override
	public boolean dispatchGenericMotionEvent(MotionEvent event) {
		if (motionLogBudget > 0 && (event.getSource() & (InputDevice.SOURCE_JOYSTICK | InputDevice.SOURCE_GAMEPAD)) != 0) {
			motionLogBudget--;
			Log.d(TAG, "Pad motion source=0x" + Integer.toHexString(event.getSource()) + " pad=" + isPad(event.getDevice())
					+ " x=" + event.getAxisValue(MotionEvent.AXIS_X) + " y=" + event.getAxisValue(MotionEvent.AXIS_Y)
					+ " hatX=" + event.getAxisValue(MotionEvent.AXIS_HAT_X) + " axes bound=" + padAxisActions);
		}
		if (isPad(event.getDevice()) && event.getAction() == MotionEvent.ACTION_MOVE) {
			float x = 0f, y = 0f;
			for (Map.Entry<Integer, String> bound : padAxisActions.entrySet()) {
				if ("left_x".equals(bound.getValue())) x = event.getAxisValue(bound.getKey());
				if ("left_y".equals(bound.getValue())) y = event.getAxisValue(bound.getKey());
			}
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
		Log.d(TAG, "Cursor shown at " + cursorX + "," + cursorY);
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
		if (!isPad(event.getDevice())) {
			return super.dispatchKeyEvent(event);
		}
		String action = padKeyActions.get(event.getKeyCode());
		if (action == null) {
			Log.d(TAG, "Pad key " + KeyEvent.keyCodeToString(event.getKeyCode()) + " is not bound to anything");
			return true;
		}
		if ("confirm".equals(action) && cursorShown) {
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
		int key = keyForAction(action);
		if (key == KeyEvent.KEYCODE_UNKNOWN) {
			return true; // an action this engine has no use for
		}
		KeyEvent mapped = new KeyEvent(event.getDownTime(), event.getEventTime(), event.getAction(),
				key, event.getRepeatCount(), event.getMetaState(), event.getDeviceId(),
				event.getScanCode(), event.getFlags(), event.getSource());
		return super.dispatchKeyEvent(mapped);
	}

	/** What an Enginehost action means to a KAG game, as the keyboard key it reads. */
	private static int keyForAction(String action) {
		switch (action) {
			case "up": return KeyEvent.KEYCODE_DPAD_UP;
			case "down": return KeyEvent.KEYCODE_DPAD_DOWN;
			case "left": return KeyEvent.KEYCODE_DPAD_LEFT;
			case "right": return KeyEvent.KEYCODE_DPAD_RIGHT;
			case "confirm": return KeyEvent.KEYCODE_ENTER;
			case "cancel": return KeyEvent.KEYCODE_ESCAPE;
			case "menu": return KeyEvent.KEYCODE_ESCAPE;
			case "skip": return KeyEvent.KEYCODE_CTRL_LEFT;
			case "auto": return KeyEvent.KEYCODE_SPACE;
			case "history": return KeyEvent.KEYCODE_PAGE_UP;
			case "page_previous": return KeyEvent.KEYCODE_PAGE_UP;
			case "page_next": return KeyEvent.KEYCODE_PAGE_DOWN;
			default: return KeyEvent.KEYCODE_UNKNOWN;
		}
	}
}
