package com.yuri.kirikiri2;

import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.InputDevice;
import android.view.KeyCharacterMap;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.TextView;
import java.util.HashMap;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.Map;
import org.json.JSONObject;
import org.tvp.kirikiri2.KR2Activity;

/**
 * The enginehost wrapper's activity, and the KiriKiri family's controller
 * layer.
 *
 * KiriKiri has no gamepad support of its own on Android: cocos2d-x 3.17.2's
 * Java side has no controller plumbing at all, so the engine's
 * EventListenerController never fires and VK_PAD1..VK_PAD10 are dead code.
 * A pad therefore does not need shimming onto an existing path -- this class
 * IS the path, and it decides what a pad means to a KAG game.
 *
 * Pointing. A KAG game is a mouse application: its title screen, its menus
 * and its choices are clickable layers, and its buttons only take focus from
 * a mouse-down (Noble Works' own ButtonLayer.tjs says "TODO: keyboard focus"),
 * so arrow-key focus traversal cannot even start on a title screen. The left
 * stick and the D-pad therefore drive a cursor drawn over the game, with
 * acceleration so a tap nudges and a held push crosses the screen, and
 * confirm presses a touch at the cursor -- or Enter, when something holds
 * keyboard focus for the cursor to have put it there. That is one mechanism
 * for pointing, used by both, rather than a cursor for the stick and keys for
 * the D-pad.
 *
 * Pointing at something. The pointer is drawn here, over the game, but the
 * engine is told where it is as well: every step of it is delivered as a real
 * mouse move ({@link #nativePointerMove}), which is what makes a KAG screen
 * answer it. Noble Works' choices take focus from onMouseEnter and its buttons
 * highlight from a mouse move, and none of that could happen while the pointer
 * was only a picture the engine learned about when a touch landed.
 *
 * Reading. The right stick sends arrow keys, repeating while it is held.
 * Arrow keys are what a KAG list layer reads once one is open -- the backlog
 * scrolls on them, and the engine's own focus traversal steps on them -- and
 * they are useless as a pointer, so the two sticks never fight.
 *
 * Keys. cancel, menu, skip, auto, history and the page actions become the
 * keys this engine and KAG actually read; what is behind each is spelled out
 * in {@link #keyForAction}. Which pad button carries which action is the
 * person's choice in Enginehost's controller settings, handed over as the
 * CONTROLLER_BINDINGS extra; {@link #DEFAULT_KEY_ACTIONS} only stands in when
 * no map arrives at all.
 *
 * Testing. Every action is taken from its key code alone, whatever source the
 * event claims, so `adb shell input keyevent KEYCODE_BUTTON_A` -- which
 * arrives as a keyboard event with no device behind it -- exercises the same
 * code a pad does. That is what lets this be proven without a person holding
 * the console.
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

	// The engine side of the pointer and the pad. What each does is documented
	// over its definition in MainScene.cpp, under "The enginehost wrapper's
	// pointer and pad".
	private static native void nativePointerMove(float x, float y);
	private static native void nativeVKKey(int vk, boolean down);
	private static native boolean nativeHasFocusedLayer();

	/**
	 * KiriKiri's own virtual key codes (the engine's tvpinputdefs.h). The pad
	 * ones are what a KiriKiri game expects from a gamepad and cannot get from
	 * an Android key code: Noble Works' YesNoDialog.tjs moves between its yes
	 * and no buttons on VK_PADLEFT and VK_PADRIGHT.
	 */
	private static final int VK_RETURN = 0x0D;
	private static final int VK_PADLEFT = 0x1B5;
	private static final int VK_PADUP = 0x1B6;
	private static final int VK_PADRIGHT = 0x1B7;
	private static final int VK_PADDOWN = 0x1B8;
	/**
	 * How long a KiriKiri key is held before its release is delivered. KAG
	 * re-tests System.getKeyState when it finally processes the queued press,
	 * so a press and release that arrive in the same millisecond -- which is
	 * what `adb shell input keyevent` sends, and what a quick tap can be --
	 * would be over before the engine ever looked.
	 */
	private static final long VK_HOLD_MS = 80;

	private static final float STICK_DEADZONE = 0.25f;
	/** Pointer speed at the instant a push begins, and after it has ramped. */
	private static final float POINTER_SLOW_PX_S = 300f;
	private static final float POINTER_FAST_PX_S = 1700f;
	private static final long POINTER_RAMP_MS = 600;
	/**
	 * How far one tap of a direction moves the cursor, as a fraction of the
	 * screen. A tap used to be worth whatever a single 16 ms frame happened to
	 * cover -- five pixels, and only when that frame ran before the key came
	 * back up. For a held D-pad that is invisible; for the synthetic
	 * `input keyevent` presses this layer is meant to be testable with, the
	 * down and the up arrive together and the frame usually loses the race, so
	 * a press moved nothing at all. dq-kirikiri-03 is exactly that: eight
	 * DPAD_RIGHT presses left the cursor where it started and the click landed
	 * on background art instead of START. A tap is a definite step now, and a
	 * hold still ramps from wherever that step put it.
	 */
	private static final float TAP_STEP_FRACTION = 0.05f;
	private static final long FRAME_MS = 16;
	private static final long CURSOR_HIDE_MS = 4000;
	/** Arrow-key repeat from the right stick: one key, a pause, then a stream. */
	private static final long SCROLL_FIRST_MS = 350;
	private static final long SCROLL_REPEAT_MS = 110;
	/** How long the menu button must be held to mean "show me the mapping". */
	private static final long LEGEND_HOLD_MS = 400;

	/**
	 * The layout to fall back on when enginehost sends no bindings (a
	 * standalone launch, or an older host). A real map always wins entire: if
	 * a person moves confirm to another button, the button they moved it off
	 * must go quiet rather than keep a default meaning.
	 */
	private static final Map<Integer, String> DEFAULT_KEY_ACTIONS = new HashMap<Integer, String>();
	private static final Map<String, Integer> DEFAULT_AXIS_ACTIONS = new HashMap<String, Integer>();
	static {
		DEFAULT_KEY_ACTIONS.put(KeyEvent.KEYCODE_DPAD_UP, "up");
		DEFAULT_KEY_ACTIONS.put(KeyEvent.KEYCODE_DPAD_DOWN, "down");
		DEFAULT_KEY_ACTIONS.put(KeyEvent.KEYCODE_DPAD_LEFT, "left");
		DEFAULT_KEY_ACTIONS.put(KeyEvent.KEYCODE_DPAD_RIGHT, "right");
		DEFAULT_KEY_ACTIONS.put(KeyEvent.KEYCODE_BUTTON_A, "confirm");
		DEFAULT_KEY_ACTIONS.put(KeyEvent.KEYCODE_BUTTON_B, "cancel");
		DEFAULT_KEY_ACTIONS.put(KeyEvent.KEYCODE_BUTTON_X, "skip");
		DEFAULT_KEY_ACTIONS.put(KeyEvent.KEYCODE_BUTTON_Y, "auto");
		DEFAULT_KEY_ACTIONS.put(KeyEvent.KEYCODE_BUTTON_START, "menu");
		DEFAULT_KEY_ACTIONS.put(KeyEvent.KEYCODE_BUTTON_SELECT, "history");
		DEFAULT_KEY_ACTIONS.put(KeyEvent.KEYCODE_BUTTON_L2, "page_previous");
		DEFAULT_KEY_ACTIONS.put(KeyEvent.KEYCODE_BUTTON_R2, "page_next");
		DEFAULT_AXIS_ACTIONS.put("left_x", MotionEvent.AXIS_X);
		DEFAULT_AXIS_ACTIONS.put("left_y", MotionEvent.AXIS_Y);
		DEFAULT_AXIS_ACTIONS.put("right_x", MotionEvent.AXIS_Z);
		DEFAULT_AXIS_ACTIONS.put("right_y", MotionEvent.AXIS_RZ);
	}

	/** What each action does here, in the words the on-screen legend uses. */
	private static final Map<String, String> ACTION_MEANINGS = new LinkedHashMap<String, String>();
	static {
		ACTION_MEANINGS.put("confirm", "click");
		ACTION_MEANINGS.put("cancel", "back / hide text");
		ACTION_MEANINGS.put("skip", "skip (hold)");
		ACTION_MEANINGS.put("auto", "auto");
		ACTION_MEANINGS.put("history", "backlog");
		ACTION_MEANINGS.put("page_previous", "backlog page up");
		ACTION_MEANINGS.put("page_next", "backlog page down");
		ACTION_MEANINGS.put("menu", "menu (hold: this list)");
	}

	private final Handler handler = new Handler(Looper.getMainLooper());
	private final Map<Integer, String> padKeyActions = new HashMap<Integer, String>();
	private final Map<String, Integer> padAxisActions = new HashMap<String, Integer>();

	private FrameLayout overlay;
	private View cursorView;
	private TextView legendView;
	private float cursorX = -1f, cursorY = -1f;
	private float stickX, stickY;
	private float scrollX, scrollY;
	private int hatX, hatY;
	private int keyDirX, keyDirY;
	private boolean pointerRunning, scrollRunning;
	private long pointerStart;
	private long lastPadInput;
	private boolean clickHeld;
	private boolean confirmAsKey;
	private final int[] viewLocation = new int[2];
	private long clickDownTime;
	private boolean legendShown;

	private final Runnable movePointer = new Runnable() {
		@Override
		public void run() {
			float dx = pointerX(), dy = pointerY();
			if (dx == 0f && dy == 0f) {
				pointerRunning = false;
				pointerStart = 0;
				scheduleCursorHide();
				return;
			}
			long now = SystemClock.uptimeMillis();
			if (pointerStart == 0) pointerStart = now;
			float ramp = Math.min(1f, (now - pointerStart) / (float) POINTER_RAMP_MS);
			float speed = POINTER_SLOW_PX_S + (POINTER_FAST_PX_S - POINTER_SLOW_PX_S) * ramp * ramp;
			float step = speed * FRAME_MS / 1000f;
			setCursor(cursorX + dx * step, cursorY + dy * step);
			if (clickHeld) sendTouch(MotionEvent.ACTION_MOVE);
			lastPadInput = now;
			handler.postDelayed(this, FRAME_MS);
		}
	};

	private final Runnable repeatScroll = new Runnable() {
		@Override
		public void run() {
			int key = scrollKey();
			if (key == KeyEvent.KEYCODE_UNKNOWN) {
				scrollRunning = false;
				return;
			}
			tapKey(key);
			handler.postDelayed(this, SCROLL_REPEAT_MS);
		}
	};

	private final Runnable hideCursor = new Runnable() {
		@Override
		public void run() {
			if (SystemClock.uptimeMillis() - lastPadInput < CURSOR_HIDE_MS) return;
			if (clickHeld || pointerRunning) return;
			if (cursorView != null) cursorView.setVisibility(View.GONE);
		}
	};

	private final Runnable showLegend = new Runnable() {
		@Override
		public void run() {
			legendShown = true;
			if (legendView != null) {
				legendView.setText(legendText());
				legendView.setVisibility(View.VISIBLE);
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
		logInputDevices();
		super.onCreate(savedInstanceState);
		buildOverlay();
	}

	/** The cursor and the mapping legend, drawn over the game. */
	private void buildOverlay() {
		overlay = new FrameLayout(this);
		overlay.setClickable(false);
		overlay.setFocusable(false);

		cursorView = new View(this);
		GradientDrawable ring = new GradientDrawable();
		ring.setShape(GradientDrawable.OVAL);
		ring.setColor(Color.argb(90, 255, 255, 255));
		ring.setStroke(dp(2), Color.argb(230, 20, 20, 20));
		cursorView.setBackground(ring);
		cursorView.setVisibility(View.GONE);
		overlay.addView(cursorView, new FrameLayout.LayoutParams(dp(22), dp(22)));

		legendView = new TextView(this);
		GradientDrawable panel = new GradientDrawable();
		panel.setColor(Color.argb(210, 16, 16, 20));
		panel.setCornerRadius(dp(8));
		panel.setStroke(dp(1), Color.argb(120, 255, 255, 255));
		legendView.setBackground(panel);
		legendView.setTextColor(Color.argb(240, 255, 255, 255));
		legendView.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12f);
		legendView.setPadding(dp(10), dp(8), dp(10), dp(8));
		legendView.setVisibility(View.GONE);
		FrameLayout.LayoutParams legendPlace = new FrameLayout.LayoutParams(
				ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
		legendPlace.gravity = Gravity.BOTTOM | Gravity.START;
		legendPlace.setMargins(dp(12), dp(12), dp(12), dp(12));
		overlay.addView(legendView, legendPlace);

		addContentView(overlay, new ViewGroup.LayoutParams(
				ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
	}

	private void loadControllerBindings(String json) {
		padKeyActions.clear();
		padAxisActions.clear();
		if (json != null) {
			try {
				JSONObject map = new JSONObject(json);
				Iterator<String> actions = map.keys();
				while (actions.hasNext()) {
					String action = actions.next();
					JSONObject binding = map.getJSONObject(action);
					if ("key".equals(binding.getString("type"))) {
						padKeyActions.put(binding.getInt("code"), action);
					} else if ("axis".equals(binding.getString("type"))) {
						padAxisActions.put(action, binding.getInt("axis"));
					}
				}
			} catch (Exception error) {
				Log.w(TAG, "Ignoring an unreadable controller map: " + error);
				padKeyActions.clear();
				padAxisActions.clear();
			}
		}
		if (padKeyActions.isEmpty()) {
			Log.w(TAG, "No controller map from enginehost; using this plugin's own layout");
			padKeyActions.putAll(DEFAULT_KEY_ACTIONS);
		}
		if (padAxisActions.isEmpty()) {
			padAxisActions.putAll(DEFAULT_AXIS_ACTIONS);
		}
		Log.i(TAG, "Controller map: " + padKeyActions.size() + " buttons, " + padAxisActions.size() + " axes");
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

	/**
	 * Every input device this activity can see, once, at startup. The first
	 * question about a pad that does nothing is whether the activity is told
	 * about it at all, and this answers it without a person having to press
	 * anything.
	 */
	private void logInputDevices() {
		for (int id : InputDevice.getDeviceIds()) {
			InputDevice device = InputDevice.getDevice(id);
			if (device == null) continue;
			Log.i(TAG, "Input device " + id + " \"" + device.getName() + "\" sources=0x"
					+ Integer.toHexString(device.getSources()) + " pad=" + isPad(device));
		}
	}

	// ---------------------------------------------------------------- pointer

	private int motionLogBudget = 12;

	@Override
	public boolean dispatchGenericMotionEvent(MotionEvent event) {
		boolean fromStick = (event.getSource() & (InputDevice.SOURCE_JOYSTICK | InputDevice.SOURCE_GAMEPAD)) != 0;
		if (!fromStick || event.getAction() != MotionEvent.ACTION_MOVE) {
			return super.dispatchGenericMotionEvent(event);
		}
		stickX = axis(event, "left_x");
		stickY = axis(event, "left_y");
		scrollX = axis(event, "right_x");
		scrollY = axis(event, "right_y");
		// Many pads report their D-pad as a hat rather than as D-pad keys, and
		// a hat arrives as motion, not as a key. Fold it into the same
		// direction the D-pad keys feed, so the cursor moves either way.
		hatX = direction(event.getAxisValue(MotionEvent.AXIS_HAT_X));
		hatY = direction(event.getAxisValue(MotionEvent.AXIS_HAT_Y));
		if (motionLogBudget > 0) {
			motionLogBudget--;
			Log.d(TAG, "Pad motion source=0x" + Integer.toHexString(event.getSource())
					+ " pointer=" + pointerX() + "," + pointerY()
					+ " scroll=" + scrollX + "," + scrollY);
		}
		pointerChanged();
		scrollChanged();
		return true;
	}

	private float axis(MotionEvent event, String action) {
		Integer which = padAxisActions.get(action);
		if (which == null) return 0f;
		float value = event.getAxisValue(which);
		return Math.abs(value) > STICK_DEADZONE ? value : 0f;
	}

	private static int direction(float value) {
		if (value > 0.5f) return 1;
		if (value < -0.5f) return -1;
		return 0;
	}

	private float pointerX() { return stickX != 0f ? stickX : (keyDirX != 0 ? keyDirX : hatX); }
	private float pointerY() { return stickY != 0f ? stickY : (keyDirY != 0 ? keyDirY : hatY); }

	/** Start or stop the cursor after anything that could have moved it. */
	private void pointerChanged() {
		boolean moving = pointerX() != 0f || pointerY() != 0f;
		if (moving) {
			lastPadInput = SystemClock.uptimeMillis();
			showCursor();
			if (!pointerRunning) {
				pointerRunning = true;
				pointerStart = 0;
				handler.post(movePointer);
			}
		} else {
			// Unconditional: a tap that never started the runnable still left
			// the cursor on screen, and nothing was going to take it away.
			if (pointerRunning) {
				pointerRunning = false;
				pointerStart = 0;
				handler.removeCallbacks(movePointer);
			}
			scheduleCursorHide();
		}
	}

	private int scrollKey() {
		if (scrollY < 0f) return KeyEvent.KEYCODE_DPAD_UP;
		if (scrollY > 0f) return KeyEvent.KEYCODE_DPAD_DOWN;
		if (scrollX < 0f) return KeyEvent.KEYCODE_DPAD_LEFT;
		if (scrollX > 0f) return KeyEvent.KEYCODE_DPAD_RIGHT;
		return KeyEvent.KEYCODE_UNKNOWN;
	}

	private void scrollChanged() {
		int key = scrollKey();
		if (key == KeyEvent.KEYCODE_UNKNOWN) {
			scrollRunning = false;
			handler.removeCallbacks(repeatScroll);
			return;
		}
		if (scrollRunning) return;
		scrollRunning = true;
		tapKey(key);
		handler.postDelayed(repeatScroll, SCROLL_FIRST_MS);
	}

	private void showCursor() {
		if (cursorView == null) return;
		if (cursorX < 0f) {
			View root = getWindow().getDecorView();
			setCursor(root.getWidth() / 2f, root.getHeight() / 2f);
		}
		cursorView.setVisibility(View.VISIBLE);
		handler.removeCallbacks(hideCursor);
	}

	private void scheduleCursorHide() {
		handler.removeCallbacks(hideCursor);
		handler.postDelayed(hideCursor, CURSOR_HIDE_MS);
	}

	private void setCursor(float x, float y) {
		View root = getWindow().getDecorView();
		cursorX = Math.max(0f, Math.min(root.getWidth() - 1, x));
		cursorY = Math.max(0f, Math.min(root.getHeight() - 1, y));
		if (cursorView != null) {
			cursorView.setX(cursorX - cursorView.getWidth() / 2f);
			cursorView.setY(cursorY - cursorView.getHeight() / 2f);
		}
		sendPointerMove();
	}

	/**
	 * Where the pointer is, as a mouse move the game can answer. The engine
	 * takes view coordinates, so the pointer -- which is placed over the whole
	 * window -- is offset into the GL view first, exactly as a touch dispatched
	 * through the view hierarchy would be.
	 */
	private void sendPointerMove() {
		float x = cursorX, y = cursorY;
		if (x < 0f) return;
		View gl = getGLSurfaceView();
		if (gl != null) {
			gl.getLocationInWindow(viewLocation);
			x -= viewLocation[0];
			y -= viewLocation[1];
		}
		nativePointerMove(x, y);
	}

	private int touchLogBudget = 20;

	/** A touch at the cursor, delivered to the game the way a finger would be. */
	private void sendTouch(int action) {
		if (cursorX < 0f) showCursor();
		long now = SystemClock.uptimeMillis();
		if (action == MotionEvent.ACTION_DOWN) clickDownTime = now;
		MotionEvent touch = MotionEvent.obtain(clickDownTime, now, action, cursorX, cursorY, 0);
		touch.setSource(InputDevice.SOURCE_TOUCHSCREEN);
		boolean handled = super.dispatchTouchEvent(touch);
		touch.recycle();
		if (action != MotionEvent.ACTION_MOVE && touchLogBudget > 0) {
			touchLogBudget--;
			Log.d(TAG, "Touch " + (action == MotionEvent.ACTION_DOWN ? "down" : "up")
					+ " at " + cursorX + "," + cursorY + " handled=" + handled);
		}
	}

	// ------------------------------------------------------------------- keys

	private int keyLogBudget = 60;

	@Override
	public boolean dispatchKeyEvent(KeyEvent event) {
		// The action is taken from the key code alone: a pad's own events, a
		// keyboard's, and the synthetic ones `adb shell input keyevent` sends
		// (source keyboard, no device behind them) all have to reach the same
		// code, or this layer cannot be proven without a person holding a pad.
		String action = padKeyActions.get(event.getKeyCode());
		boolean down = event.getAction() == KeyEvent.ACTION_DOWN;
		if (keyLogBudget > 0) {
			keyLogBudget--;
			Log.d(TAG, "Key " + KeyEvent.keyCodeToString(event.getKeyCode()) + (down ? " down" : " up")
					+ " source=0x" + Integer.toHexString(event.getSource())
					+ " pad=" + isPad(event.getDevice()) + " action=" + action);
		}
		if (action == null) {
			return super.dispatchKeyEvent(event);
		}
		if (down && event.getRepeatCount() > 0) {
			return true; // held keys repeat on our own clock, not Android's
		}
		lastPadInput = SystemClock.uptimeMillis();
		return act(action, down);
	}

	private boolean act(String action, boolean down) {
		if ("up".equals(action) || "down".equals(action)
				|| "left".equals(action) || "right".equals(action)) {
			int dx = "left".equals(action) ? -1 : "right".equals(action) ? 1 : 0;
			int dy = "up".equals(action) ? -1 : "down".equals(action) ? 1 : 0;
			// Only the axis this action names, so pressing right while up is
			// held does not restate up as well.
			if (dx != 0) keyDirX = down ? dx : 0;
			else keyDirY = down ? dy : 0;
			if (down) {
				showCursor(); // also centres the cursor the first time
				View root = getWindow().getDecorView();
				setCursor(cursorX + dx * root.getWidth() * TAP_STEP_FRACTION,
						cursorY + dy * root.getHeight() * TAP_STEP_FRACTION);
				// A direction also carries KiriKiri's own pad code, for the
				// screens that read one: a yes/no dialog steps between its
				// buttons on VK_PADLEFT and VK_PADRIGHT. Only while something
				// holds focus -- with nothing focused the pointer is the whole
				// mechanism, and the engine turns a pad direction into a move
				// of its own emulated cursor, which would be a second,
				// invisible pointer fighting this one.
				if (nativeHasFocusedLayer()) {
					int pad = dx < 0 ? VK_PADLEFT : dx > 0 ? VK_PADRIGHT
							: dy < 0 ? VK_PADUP : VK_PADDOWN;
					sendVKKey(pad, true);
					sendVKKey(pad, false);
				}
			}
			pointerChanged();
			return true;
		}
		if ("confirm".equals(action)) {
			// Exactly one of two things, and never both. When something holds
			// keyboard focus -- a choice the pointer is hovering, the button a
			// yes/no dialog focuses for itself -- Enter activates it, which is
			// what the focused layer is waiting for. Otherwise it is a click
			// where the pointer is, which is what a KAG title screen, message
			// window and image map all answer.
			//
			// Both would be wrong rather than merely redundant: a focused
			// ButtonLayer clicks ITSELF on Enter (its own onKeyUp does), so a
			// click on top of that activates it twice, and a yes/no dialog
			// answered twice closes something it was never asked about.
			showCursor();
			if (down) confirmAsKey = nativeHasFocusedLayer();
			if (confirmAsKey) {
				sendVKKey(VK_RETURN, down);
			} else {
				clickHeld = down;
				sendTouch(down ? MotionEvent.ACTION_DOWN : MotionEvent.ACTION_UP);
			}
			if (!down) scheduleCursorHide();
			return true;
		}
		if ("menu".equals(action)) {
			// A short press opens the engine's system menu; holding it shows
			// the mapping, which is the one thing a person needs when they do
			// not know what a button does.
			if (down) {
				handler.postDelayed(showLegend, LEGEND_HOLD_MS);
			} else {
				handler.removeCallbacks(showLegend);
				if (legendShown) {
					legendShown = false;
					if (legendView != null) legendView.setVisibility(View.GONE);
				} else {
					tapKey(KeyEvent.KEYCODE_MENU);
				}
			}
			return true;
		}
		int key = keyForAction(action);
		if (key == KeyEvent.KEYCODE_UNKNOWN) {
			return true; // an action a KAG game has nothing to do with
		}
		if ("skip".equals(action)) {
			injectKey(key, down); // skip is a held key, so mirror the press
			return true;
		}
		if (down) tapKey(key);
		return true;
	}

	/**
	 * What an Enginehost action means to a KAG game, as the key it reads.
	 * Read off Noble Works' own MainWindow.processKeys, which is stock KAG:
	 * Escape is what a right click does, Control held is skip, A is auto, R is
	 * the backlog and the page keys scroll it. Menu is Kirikiroid2's own
	 * system menu (save, load, settings), handled above rather than here.
	 *
	 * quick_save and quick_load are deliberately absent: KAG's S and L
	 * shortcuts only exist in free-save-data mode, and a shoulder button that
	 * silently overwrites a save is worse than one that does nothing. Saving
	 * is on the system menu.
	 */
	private static int keyForAction(String action) {
		if ("cancel".equals(action)) return KeyEvent.KEYCODE_ESCAPE;
		if ("skip".equals(action)) return KeyEvent.KEYCODE_CTRL_LEFT;
		if ("auto".equals(action)) return KeyEvent.KEYCODE_A;
		if ("history".equals(action)) return KeyEvent.KEYCODE_R;
		if ("page_previous".equals(action)) return KeyEvent.KEYCODE_PAGE_UP;
		if ("page_next".equals(action)) return KeyEvent.KEYCODE_PAGE_DOWN;
		return KeyEvent.KEYCODE_UNKNOWN;
	}

	/**
	 * A KiriKiri key, straight to the window's layer tree. Cancel keeps its
	 * Escape and skip its Control rather than gaining VK_PAD2 and VK_PAD4 as
	 * well: Noble Works' YesNoDialog closes on Escape AND on VK_PAD2, so a
	 * button that sent both would answer the dialog twice.
	 */
	private void sendVKKey(final int vk, boolean down) {
		if (down) {
			nativeVKKey(vk, true);
			return;
		}
		handler.postDelayed(new Runnable() {
			@Override
			public void run() {
				nativeVKKey(vk, false);
			}
		}, VK_HOLD_MS);
	}

	private int injectLogBudget = 40;

	private boolean injectKey(int keyCode, boolean down) {
		long now = SystemClock.uptimeMillis();
		KeyEvent event = new KeyEvent(now, now,
				down ? KeyEvent.ACTION_DOWN : KeyEvent.ACTION_UP, keyCode, 0, 0,
				KeyCharacterMap.VIRTUAL_KEYBOARD, 0, 0, InputDevice.SOURCE_KEYBOARD);
		boolean handled = super.dispatchKeyEvent(event);
		if (injectLogBudget > 0) {
			injectLogBudget--;
			Log.d(TAG, "Injected " + KeyEvent.keyCodeToString(keyCode) + (down ? " down" : " up")
					+ ", handled=" + handled);
		}
		return handled;
	}

	private void tapKey(int keyCode) {
		injectKey(keyCode, true);
		injectKey(keyCode, false);
	}

	/** The mapping as it actually stands, so the legend can never lie. */
	private String legendText() {
		Map<String, String> buttons = new LinkedHashMap<String, String>();
		for (Map.Entry<Integer, String> bound : padKeyActions.entrySet()) {
			String meaning = ACTION_MEANINGS.get(bound.getValue());
			if (meaning == null) continue;
			String label = KeyEvent.keyCodeToString(bound.getKey())
					.replace("KEYCODE_BUTTON_", "").replace("KEYCODE_", "");
			String had = buttons.get(bound.getValue());
			buttons.put(bound.getValue(), had == null ? label : had + " / " + label);
		}
		StringBuilder text = new StringBuilder("Stick or D-pad: pointer   Right stick: scroll");
		for (Map.Entry<String, String> meaning : ACTION_MEANINGS.entrySet()) {
			String label = buttons.get(meaning.getKey());
			if (label == null) continue;
			text.append("\n").append(label).append(": ").append(meaning.getValue());
		}
		return text.toString();
	}
}
