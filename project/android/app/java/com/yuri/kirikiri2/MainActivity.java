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
 * and its choices are clickable layers. The left stick drives a cursor drawn
 * over the game, with acceleration so a tap nudges and a held push crosses
 * the screen, and confirm is a touch at that cursor -- always, whatever holds
 * focus. The cursor is the selection: what a press acts on is what the ring
 * is on, and there is nothing else for a person to keep track of.
 *
 * Pointing at something. The pointer is drawn here, over the game, but the
 * engine is told where it is as well: every step of it is delivered as a real
 * mouse move ({@link #nativePointerMove}), which is what makes a KAG screen
 * answer it. Noble Works' choices take focus from onMouseEnter and its buttons
 * highlight from a mouse move, and none of that could happen while the pointer
 * was only a picture the engine learned about when a touch landed. That same
 * move also gives the engine's keyboard focus to whatever the pointer is over,
 * so hovering and focusing are one thing.
 *
 * Stepping. A pointer you steer reaches everything on a KAG screen but is not
 * how anyone drives a menu, so the D-pad steps between the things on screen
 * instead: {@link #nativeFocusStep} asks the engine for the nearest focusable
 * layer that way, focus moves to it, and the ring is warped onto it. Focus and
 * the pointer are therefore one selection moved from either end -- the ring
 * follows a step, focus follows the ring -- and never two that disagree. Where
 * nothing focusable lies that way, which is most of an ordinary scene, the
 * engine says so and the direction steers the pointer as the stick does.
 *
 * Reading. The right stick sends arrow keys, repeating while it is held.
 * Arrow keys are what a KAG list layer reads once one is open -- the backlog
 * scrolls on them, and the engine's own focus traversal steps on them -- and
 * they are useless as a pointer, so the two sticks never fight.
 *
 * Keys. cancel, menu, skip, auto, history and the page actions become the
 * keys this engine and KAG actually read; what is behind each is spelled out
 * in {@link #vkForAction}. Which pad button carries which action is the
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
	private static native void nativeFocusStep(int dirX, int dirY);
	private static native int nativeTakeFocusStep(float[] at);

	/** What {@link #nativeTakeFocusStep} answers. */
	private static final int STEP_WAITING = 0;
	private static final int STEP_MOVED = 1;
	private static final int STEP_NOTHING_THAT_WAY = 2;
	/**
	 * The screen has a selection of its own and moves it with the arrow keys.
	 *
	 * A KAG menu is not made of layers. Noble Works' title buttons are links
	 * inside one full-screen message layer -- regions of a layer, which a
	 * geometric step cannot land on -- and MessageLayer.tjs sets
	 * "focusable = false" on every one of them on purpose, because the layer
	 * that owns them does the selecting: its onKeyDown walks the links on
	 * up/down/left/right, moves its own cursor onto the chosen one so the
	 * button draws its highlight, and presses it on Return. So this answer
	 * means: hand the direction over as an arrow key and take the ring off the
	 * screen, because the game is already drawing what is selected and two
	 * selections is the thing this design exists to prevent.
	 */
	private static final int STEP_GAME_STEERS = 3;

	/**
	 * What a KAG game reads, as the Windows virtual-key codes
	 * KiriKiri speaks. Every one of these used to be sent as an Android
	 * KeyEvent instead, in the hope that the engine's GL view would pick it
	 * up; the user's own session says it never did -- Escape and Page Up both
	 * came back handled=false, three presses each -- which is why the pad
	 * could go forward and not back.
	 */
	private static final int VK_ESCAPE = 0x1B;
	private static final int VK_CONTROL = 0x11;
	private static final int VK_RETURN = 0x0D;
	private static final int VK_PRIOR = 0x21;
	private static final int VK_NEXT = 0x22;
	private static final int VK_LEFT = 0x25;
	private static final int VK_UP = 0x26;
	private static final int VK_RIGHT = 0x27;
	private static final int VK_DOWN = 0x28;
	private static final int VK_A = 0x41;
	private static final int VK_B = 0x42;
	private static final int VK_F = 0x46;
	private static final int VK_R = 0x52;
	/**
	 * How long a KiriKiri key is held before its release is delivered. KAG
	 * re-tests System.getKeyState when it finally processes the queued press,
	 * so a press and release that arrive in the same millisecond -- which is
	 * what `adb shell input keyevent` sends, and what a quick tap can be --
	 * would be over before the engine ever looked.
	 */
	private static final long VK_HOLD_MS = 80;

	private static final float STICK_DEADZONE = 0.25f;
	/**
	 * Pointer speed at the instant a push begins, and after it has ramped.
	 *
	 * These used to be 300, 1700 and 600 ms, and that is what made the D-pad
	 * look as if only two of its four directions worked. Integrated over the
	 * 16 ms frame, the old ramp carries the pointer 462 px in 600 ms and
	 * 1142 px in a second, on a screen 1920 x 1080. So ONE ordinary press of
	 * up -- half a second, which is what a person does when they are looking
	 * for a pointer they cannot see -- takes it from the middle of the screen
	 * to y = 0 and leaves it there, and one press of left takes it to x = 0.
	 * Pinned in the top-left corner, up and left do nothing whatever, for as
	 * long as the game runs, while down and right still move: exactly the
	 * report ("moves occasionally, and only down and right"). Nothing in the
	 * hat or the axis code has a sign the wrong way round; the screen edge is
	 * the thing that swallowed two directions.
	 *
	 * A pointer has to be aimable, so a press moves it a distance a person
	 * can predict: about 190 px in half a second, and a full sweep of the
	 * long side takes a bit over two seconds of holding.
	 */
	private static final float POINTER_SLOW_PX_S = 260f;
	private static final float POINTER_FAST_PX_S = 900f;
	private static final long POINTER_RAMP_MS = 900;
	/**
	 * How far one tap of a direction moves the pointer where there is nothing
	 * on screen to step to, as a fraction of it. A tap used to be worth
	 * whatever a single 16 ms frame happened to cover -- five pixels, and only
	 * when that frame ran before the key came back up. For a held D-pad that
	 * is invisible; for the synthetic `input keyevent` presses this layer is
	 * meant to be testable with, the down and the up arrive together and the
	 * frame usually loses the race, so a press moved nothing at all.
	 * dq-kirikiri-03 is exactly that: eight DPAD_RIGHT presses left the cursor
	 * where it started and the click landed on background art instead of
	 * START. A tap is a definite step, and a hold still ramps from wherever
	 * that step put it.
	 */
	private static final float TAP_STEP_FRACTION = 0.05f;
	private static final long FRAME_MS = 16;
	/**
	 * How long the cursor sits still before it dims, and how faint it goes.
	 * It used to disappear altogether, which is why dq-kirikiri-05's title
	 * screen shows no ring at all although logcat has every press: nine
	 * `input keyevent` presses half a second apart, then a screenshot, is
	 * longer than the timeout, so the pointer was gone by the time the camera
	 * looked. Worse than the missing proof, a pointer that vanishes takes with
	 * it the only sign of where confirm will land -- on a handheld, where the
	 * pad is the whole input, that is the one thing that must always be on
	 * screen. So an idle cursor fades instead of leaving, and only a real
	 * finger (which is a pointer of its own) puts it away.
	 */
	private static final long CURSOR_DIM_MS = 4000;
	private static final float CURSOR_IDLE_ALPHA = 0.45f;
	/** Arrow-key repeat from the right stick: one key, a pause, then a stream. */
	private static final long SCROLL_FIRST_MS = 350;
	private static final long SCROLL_REPEAT_MS = 110;
	/** How long the menu button must be held to mean "show me the mapping". */
	private static final long LEGEND_HOLD_MS = 400;
	/**
	 * How many frames a direction waits for the engine to say what lies that
	 * way. The engine answers on its own frame, so the usual wait is one; the
	 * rest is allowance for a frame that ran long. After that the direction
	 * steers the pointer instead of being dropped, because a D-pad that does
	 * nothing at all because the engine was briefly busy is the worst of the
	 * outcomes available here.
	 */
	private static final int STEP_POLLS = 5;
	/** Stepping while a direction is held: one step, a pause, then a stream. */
	private static final long STEP_FIRST_MS = 400;
	private static final long STEP_REPEAT_MS = 180;

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
	/**
	 * Whether the pointer has ever been put somewhere. "Nowhere yet" used to
	 * be read off the coordinate being negative, which setCursor clamps to
	 * zero the first time anything moves it -- so the top and left edges of
	 * the screen were indistinguishable from "no pointer", and a pointer
	 * resting there sent the engine no mouse move at all.
	 */
	private boolean cursorPlaced;
	private float stickX, stickY;
	private float scrollX, scrollY;
	private int hatX, hatY;
	private int keyDirX, keyDirY;
	private boolean pointerRunning, scrollRunning;
	private boolean cursorShown;
	private long pointerStart;
	private long lastPadInput;
	private boolean clickHeld;
	/**
	 * The direction being asked about, how many frames it has waited, whether
	 * it has begun repeating, and whether the answer was "nothing that way" --
	 * which is the one case where a held direction steers the pointer.
	 */
	private int stepDirX, stepDirY;
	private int stepPolls;
	private boolean stepRepeating;
	private boolean keyDirSteers;
	/** The screen is moving its own selection with the keys; see STEP_GAME_STEERS. */
	private boolean gameSteers;
	private final float[] stepAt = new float[2];
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
				scheduleCursorDim();
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

	/**
	 * The engine's answer to a direction, collected a frame at a time.
	 *
	 * The ring is not moved at all until the answer arrives. Moving it and
	 * then warping it somewhere else a frame later would be two selections one
	 * after the other, which is the very thing this design exists to rule out;
	 * one frame of waiting is not something a hand can feel.
	 */
	private final Runnable awaitFocusStep = new Runnable() {
		@Override
		public void run() {
			int answer = nativeTakeFocusStep(stepAt);
			if (answer == STEP_WAITING) {
				if (++stepPolls < STEP_POLLS) {
					handler.postDelayed(this, FRAME_MS);
					return;
				}
				answer = STEP_NOTHING_THAT_WAY;
			}
			if (answer == STEP_MOVED) {
				// The ring is in charge again, so it comes back: a screen that
				// had taken the selection over has just given it up.
				gameSteers = false;
				showCursor();
				// The engine answers in its own view's coordinates, which is
				// what nativePointerMove takes; the ring is placed in the
				// window's, so the view's offset goes back on.
				float offsetX = 0f, offsetY = 0f;
				View gl = getGLSurfaceView();
				if (gl != null) {
					gl.getLocationInWindow(viewLocation);
					offsetX = viewLocation[0];
					offsetY = viewLocation[1];
				}
				float wasX = cursorX, wasY = cursorY;
				setCursor(stepAt[0] + offsetX, stepAt[1] + offsetY);
				if (stepLogBudget > 0) {
					stepLogBudget--;
					Log.d(TAG, "menu step " + stepDirX + "," + stepDirY + ": "
							+ wasX + "," + wasY + " -> " + cursorX + "," + cursorY);
				}
				lastPadInput = SystemClock.uptimeMillis();
				if (directionHeld(stepDirX, stepDirY)) {
					handler.postDelayed(repeatFocusStep,
							stepRepeating ? STEP_REPEAT_MS : STEP_FIRST_MS);
				} else {
					stepRepeating = false;
					scheduleCursorDim();
				}
				return;
			}
			if (answer == STEP_GAME_STEERS) {
				// The game's own menu. The arrow key goes to the engine and
				// the ring comes off the screen: KAG highlights the item it
				// selects, and a ring sitting somewhere else beside that
				// highlight would be a second selection saying something
				// different -- exactly what the pointer-and-focus rule
				// forbids. Confirm follows the same handover and sends Return.
				gameSteers = true;
				hideCursor("the screen's own selection has it");
				tapVK(arrowVK(stepDirX, stepDirY));
				if (stepLogBudget > 0) {
					stepLogBudget--;
					Log.d(TAG, "menu keys " + stepDirX + "," + stepDirY
							+ ": handed to the screen's own selection");
				}
				lastPadInput = SystemClock.uptimeMillis();
				if (directionHeld(stepDirX, stepDirY)) {
					handler.postDelayed(repeatFocusStep,
							stepRepeating ? STEP_REPEAT_MS : STEP_FIRST_MS);
				} else {
					stepRepeating = false;
				}
				return;
			}
			// Nothing focusable that way. An ordinary scene is all message
			// window and background art, and there the direction has to steer
			// the pointer, which is what it did before any of this existed:
			// one definite step now -- which is all a tap ever gets, because a
			// tap is over before the ramp's first frame -- and then the ramp
			// for as long as the direction is held.
			gameSteers = false;
			showCursor();
			View root = getWindow().getDecorView();
			float wasX = cursorX, wasY = cursorY;
			setCursor(cursorX + stepDirX * root.getWidth() * TAP_STEP_FRACTION,
					cursorY + stepDirY * root.getHeight() * TAP_STEP_FRACTION);
			if (stepLogBudget > 0) {
				stepLogBudget--;
				// A direction that changed nothing is named as such. Told
				// apart in the log, "the hat never arrived" and "the pointer
				// was already against that edge" look identical from the
				// outside, and the second one cost a whole round trip to the
				// console to find.
				String named = stepDirX < 0 ? "left" : stepDirX > 0 ? "right"
						: stepDirY < 0 ? "up" : "down";
				Log.d(TAG, wasX == cursorX && wasY == cursorY
						? "cursor " + named + ": already against that edge at "
								+ cursorX + "," + cursorY
						: "cursor " + named + ": " + wasX + "," + wasY
								+ " -> " + cursorX + "," + cursorY);
			}
			keyDirSteers = true;
			lastPadInput = SystemClock.uptimeMillis();
			pointerChanged();
		}
	};

	private final Runnable repeatFocusStep = new Runnable() {
		@Override
		public void run() {
			if (!directionHeld(stepDirX, stepDirY)) {
				stepRepeating = false;
				return;
			}
			stepRepeating = true;
			stepPolls = 0;
			nativeFocusStep(stepDirX, stepDirY);
			handler.postDelayed(awaitFocusStep, FRAME_MS);
		}
	};

	private final Runnable repeatScroll = new Runnable() {
		@Override
		public void run() {
			int vk = scrollVK();
			if (vk == 0) {
				scrollRunning = false;
				return;
			}
			tapVK(vk);
			handler.postDelayed(this, SCROLL_REPEAT_MS);
		}
	};

	private final Runnable dimCursor = new Runnable() {
		@Override
		public void run() {
			long idle = SystemClock.uptimeMillis() - lastPadInput;
			if (idle < CURSOR_DIM_MS) {
				// Something moved after this was scheduled; wait out the rest
				// rather than dropping the dim on the floor, which is what the
				// old early return did.
				handler.postDelayed(this, CURSOR_DIM_MS - idle);
				return;
			}
			if (clickHeld || pointerRunning) return;
			if (cursorView != null) cursorView.setAlpha(CURSOR_IDLE_ALPHA);
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
		// The engine adds views of its own after this one (its system bar, for
		// instance), and a later sibling draws on top by default. Elevation
		// settles the order once, without touching the view tree every time
		// the cursor moves.
		overlay.setElevation(dp(8));

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

	private int stickLogBudget = 20;
	private int hatLogBudget = 40;
	private boolean stickActive, scrollActive;

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
		// Every sample this event carries, oldest first, and then the event's
		// own. A joystick event is batched: Android hands over one ACTION_MOVE
		// holding all the samples that arrived since the last frame, and
		// getAxisValue answers only for the newest of them. A D-pad tap short
		// enough to begin and end inside one batch therefore reads as hat
		// 0 -> 0, raises no direction at all, and is lost -- the other half of
		// "the pointer moves only occasionally". A direction is an edge, so
		// every edge in the event has to be looked at.
		int samples = event.getHistorySize();
		for (int sample = 0; sample < samples; sample++) {
			applyHat(direction(event.getHistoricalAxisValue(MotionEvent.AXIS_HAT_X, sample)),
					direction(event.getHistoricalAxisValue(MotionEvent.AXIS_HAT_Y, sample)));
		}
		applyHat(direction(event.getAxisValue(MotionEvent.AXIS_HAT_X)),
				direction(event.getAxisValue(MotionEvent.AXIS_HAT_Y)));
		// Logged when a stick crosses the dead zone, not once per event. A pad
		// streams motion at sixty a second, so the old per-event budget of
		// twelve bought two tenths of a second: the user's whole session shows
		// one flick of the left stick and then nothing, and could not say
		// whether the D-pad had ever arrived. A transition is rare enough that
		// a budget covers a session.
		boolean stickNow = stickX != 0f || stickY != 0f;
		boolean scrollNow = scrollX != 0f || scrollY != 0f;
		if ((stickNow != stickActive || scrollNow != scrollActive) && stickLogBudget > 0) {
			stickLogBudget--;
			stickActive = stickNow;
			scrollActive = scrollNow;
			Log.d(TAG, "Pad sticks source=0x" + Integer.toHexString(event.getSource())
					+ " left=" + stickX + "," + stickY
					+ " right=" + scrollX + "," + scrollY);
		} else {
			stickActive = stickNow;
			scrollActive = scrollNow;
		}
		pointerChanged();
		scrollChanged();
		return true;
	}

	/**
	 * A real pad's D-pad is a hat: it arrives as an axis on a motion event and
	 * never as KEYCODE_DPAD_* at all. The console's pad is one of those -- its
	 * device reports no SOURCE_DPAD, and the user's session has not a single
	 * D-pad key in it -- and a hat used only to lean on the cursor's movement
	 * ramp. So a tap of the D-pad moved the pointer about five pixels, sent no
	 * key, and never even brought the pointer on screen: the D-pad did nothing,
	 * exactly as reported, while the same presses sent as keys had worked.
	 *
	 * A hat now raises and drops the very same direction action a D-pad key
	 * raises, so there is one mechanism for a direction however it arrives:
	 * the step to the next thing on screen, or the pointer's ramp where there
	 * is nothing to step to.
	 */
	private void applyHat(int x, int y) {
		if (x != hatX) {
			int was = hatX;
			hatX = x;
			if (was != 0) act(was < 0 ? "left" : "right", false);
			if (x != 0) act(x < 0 ? "left" : "right", true);
			if (hatLogBudget > 0) {
				hatLogBudget--;
				Log.d(TAG, "Pad hat X " + was + " -> " + x);
			}
		}
		if (y != hatY) {
			int was = hatY;
			hatY = y;
			if (was != 0) act(was < 0 ? "up" : "down", false);
			if (y != 0) act(y < 0 ? "up" : "down", true);
			if (hatLogBudget > 0) {
				hatLogBudget--;
				Log.d(TAG, "Pad hat Y " + was + " -> " + y);
			}
		}
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

	// The stick always steers. A held direction steers only once the engine
	// has said there is nothing on this screen to step to.
	private float pointerX() { return stickX != 0f ? stickX : (keyDirSteers ? keyDirX : 0f); }
	private float pointerY() { return stickY != 0f ? stickY : (keyDirSteers ? keyDirY : 0f); }

	/** The engine's own key for a direction, for a screen that steers itself. */
	private static int arrowVK(int dx, int dy) {
		if (dx < 0) return VK_LEFT;
		if (dx > 0) return VK_RIGHT;
		if (dy < 0) return VK_UP;
		return VK_DOWN;
	}

	/** Is that the direction still being held? */
	private boolean directionHeld(int dx, int dy) {
		return (dx != 0 && keyDirX == dx) || (dy != 0 && keyDirY == dy);
	}

	/**
	 * Ask the engine what lies that way. The layer tree can only be read on
	 * the engine's own thread, so this is a question posted and an answer
	 * collected, not a call.
	 */
	private void askFocusStep(int dx, int dy) {
		stepDirX = dx;
		stepDirY = dy;
		stepPolls = 0;
		stepRepeating = false;
		keyDirSteers = false;
		handler.removeCallbacks(awaitFocusStep);
		handler.removeCallbacks(repeatFocusStep);
		nativeFocusStep(dx, dy);
		handler.postDelayed(awaitFocusStep, FRAME_MS);
	}

	/**
	 * The D-pad was let go of: no more repeats, and nothing steers the
	 * pointer. A question already asked is deliberately left to be answered.
	 * A tap of the D-pad -- and every `input keyevent` press, where the down
	 * and the up arrive in the same breath -- is let go of before the engine
	 * has answered it, and cancelling the answer here would make a tap do
	 * nothing at all.
	 */
	private void endFocusStep() {
		handler.removeCallbacks(repeatFocusStep);
		stepRepeating = false;
		keyDirSteers = false;
	}

	/** Start or stop the cursor after anything that could have moved it. */
	private void pointerChanged() {
		boolean moving = pointerX() != 0f || pointerY() != 0f;
		if (moving) {
			// A stick is a pointer, and a pointer takes the selection back
			// from whatever screen was steering with the keys.
			if (stickX != 0f || stickY != 0f) gameSteers = false;
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
			scheduleCursorDim();
		}
	}

	/**
	 * The right stick scrolls, in the keys KAG's backlog listens for: up and
	 * down a line, left and right a page (backlog.tjs reads VK_UP, VK_DOWN,
	 * VK_PRIOR and VK_NEXT, and takes left and right as the page keys).
	 */
	private int scrollVK() {
		if (scrollY < 0f) return VK_UP;
		if (scrollY > 0f) return VK_DOWN;
		if (scrollX < 0f) return VK_PRIOR;
		if (scrollX > 0f) return VK_NEXT;
		return 0;
	}

	private void scrollChanged() {
		int vk = scrollVK();
		if (vk == 0) {
			scrollRunning = false;
			handler.removeCallbacks(repeatScroll);
			return;
		}
		if (scrollRunning) return;
		scrollRunning = true;
		tapVK(vk);
		handler.postDelayed(repeatScroll, SCROLL_FIRST_MS);
	}

	private void showCursor() {
		if (cursorView == null) return;
		if (!cursorPlaced) {
			View root = getWindow().getDecorView();
			setCursor(root.getWidth() / 2f, root.getHeight() / 2f);
		}
		cursorView.setVisibility(View.VISIBLE);
		cursorView.setAlpha(1f);
		handler.removeCallbacks(dimCursor);
		if (!cursorShown) {
			cursorShown = true;
			// Said once per appearance: a screenshot that shows no ring cannot
			// tell "we never drew it" apart from "something drew over it", and
			// dq-kirikiri-05 had to leave exactly that open.
			Log.d(TAG, "cursor visible at " + cursorX + "," + cursorY);
		}
	}

	private void scheduleCursorDim() {
		handler.removeCallbacks(dimCursor);
		handler.postDelayed(dimCursor, CURSOR_DIM_MS);
	}

	/**
	 * A real finger takes the pointer away; the next pad input brings it back.
	 * Two pointers on one screen, one of them stale, is worse than none. Only
	 * events that arrive through the view hierarchy reach here -- the touches
	 * this class synthesises for the pad go straight to super.
	 */
	@Override
	public boolean dispatchTouchEvent(MotionEvent event) {
		gameSteers = false;
		hideCursor("the screen was touched");
		return super.dispatchTouchEvent(event);
	}

	/** Take the ring off the screen, once, and say why. */
	private void hideCursor(String why) {
		if (!cursorShown || cursorView == null) return;
		cursorShown = false;
		cursorView.setVisibility(View.GONE);
		handler.removeCallbacks(dimCursor);
		Log.d(TAG, "cursor hidden: " + why);
	}

	private int sentPointerX = Integer.MIN_VALUE, sentPointerY = Integer.MIN_VALUE;

	private void setCursor(float x, float y) {
		View root = getWindow().getDecorView();
		cursorX = Math.max(0f, Math.min(root.getWidth() - 1, x));
		cursorY = Math.max(0f, Math.min(root.getHeight() - 1, y));
		cursorPlaced = true;
		if (cursorView != null) {
			// The hotspot may sit on the very edge -- a KAG screen can have
			// something in the corner -- but the RING is kept whole on screen
			// rather than centred on the hotspot and half outside it. At 0,0
			// the old placement drew three quarters of the ring off the panel,
			// over the letterbox, so a pointer that had run into the corner
			// was invisible as well as immovable and there was nothing on
			// screen to say where it had gone.
			int ringW = cursorView.getWidth() > 0 ? cursorView.getWidth() : dp(22);
			int ringH = cursorView.getHeight() > 0 ? cursorView.getHeight() : dp(22);
			int mostX = Math.max(0, root.getWidth() - ringW);
			int mostY = Math.max(0, root.getHeight() - ringH);
			int placeX = (int) Math.max(0f, Math.min(mostX, cursorX - ringW / 2f));
			int placeY = (int) Math.max(0f, Math.min(mostY, cursorY - ringH / 2f));
			// The ring is placed by its LAYOUT and not by setX/setY.
			//
			// dq-kirikiri-08 and -09 both pixel-diffed a whole session and
			// found the ring drawn once, at the position of the very first
			// direction, and never redrawn again while logcat went on
			// reporting correct coordinates: a player saw a frozen ring and
			// reported the D-pad as dead. setX only asks for the view's
			// rendered properties to be re-published, and over the engine's
			// GL surface that damage never reached the screen; the one move
			// that did appear was the first, which came with the GONE ->
			// VISIBLE change and therefore with a real layout pass. So every
			// move now asks for that same layout pass, which is the path
			// proven to reach the screen on this device.
			ViewGroup.LayoutParams params = cursorView.getLayoutParams();
			if (params instanceof FrameLayout.LayoutParams) {
				FrameLayout.LayoutParams place = (FrameLayout.LayoutParams) params;
				if (place.leftMargin != placeX || place.topMargin != placeY) {
					place.leftMargin = placeX;
					place.topMargin = placeY;
					cursorView.setTranslationX(0f);
					cursorView.setTranslationY(0f);
					cursorView.setLayoutParams(place);
					if (overlay != null) overlay.invalidate();
					reportRingPlacement();
				}
			} else {
				cursorView.setX(placeX);
				cursorView.setY(placeY);
			}
		}
		// Only when the pointer is somewhere else than the engine last heard.
		// The movement runnable fires every frame for as long as a stick is
		// held, whether or not that frame moved the cursor a whole pixel, and
		// each call queues a mouse move onto the engine's own event queue --
		// work the engine cannot tell from the last one, done ahead of the
		// input that matters.
		int px = (int) cursorX, py = (int) cursorY;
		if (px != sentPointerX || py != sentPointerY) {
			sentPointerX = px;
			sentPointerY = py;
			sendPointerMove();
		}
	}

	/**
	 * Where the pointer is, as a mouse move the game can answer. The engine
	 * takes view coordinates, so the pointer -- which is placed over the whole
	 * window -- is offset into the GL view first, exactly as a touch dispatched
	 * through the view hierarchy would be.
	 */
	private void sendPointerMove() {
		if (!cursorPlaced) return;
		float x = cursorX, y = cursorY;
		View gl = getGLSurfaceView();
		if (gl != null) {
			gl.getLocationInWindow(viewLocation);
			x -= viewLocation[0];
			y -= viewLocation[1];
		}
		nativePointerMove(x, y);
	}

	private int ringLogBudget = 6;

	/**
	 * Where the ring actually ended up, read back after the frame that placed
	 * it. The two device runs that found the freeze could only say the ring
	 * had not moved on screen; this says whether the view moved, so the next
	 * one can tell "we never asked" from "we asked and nothing came of it".
	 */
	private void reportRingPlacement() {
		if (ringLogBudget <= 0 || cursorView == null) return;
		ringLogBudget--;
		cursorView.post(new Runnable() {
			@Override
			public void run() {
				if (cursorView == null) return;
				int[] where = new int[2];
				cursorView.getLocationInWindow(where);
				Log.d(TAG, "ring laid out at " + where[0] + "," + where[1]
						+ " for cursor " + cursorX + "," + cursorY
						+ " shown=" + (cursorView.getVisibility() == View.VISIBLE));
			}
		});
	}

	private int touchLogBudget = 20;

	/** A touch at the cursor, delivered to the game the way a finger would be. */
	private void sendTouch(int action) {
		if (!cursorPlaced) showCursor();
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
				// The pointer has to exist before a direction can mean
				// anything: showCursor puts it in the middle of the screen the
				// first time, and the step below is measured from there. It
				// also tells the engine where the pointer is, which is what
				// gives the screen under it the focus its keys are delivered
				// to. Once a screen has taken the selection over, the ring
				// stays off rather than flashing back on for every press.
				if (!gameSteers) showCursor();
				askFocusStep(dx, dy);
			} else if (keyDirX == 0 && keyDirY == 0) {
				endFocusStep();
			}
			pointerChanged();
			return true;
		}
		if ("confirm".equals(action)) {
			// A click where the pointer is. Always: the pointer is the
			// selection, so the item the ring is on is the item this presses,
			// and a person never has to wonder which of two things a button
			// will act on. It used to send Enter instead whenever something
			// held keyboard focus, on the reasoning that a focused layer is
			// waiting for Enter -- but that made confirm mean two different
			// things depending on invisible state, and now that a step warps
			// the ring onto the layer it focuses, the click lands on the
			// focused item anyway. One mechanism, and it is the visible one.
			// ...unless the screen is steering its own selection, in which
			// case there is no ring to click at and the selected item is the
			// game's. Return is what KAG's own menu presses, and it presses
			// the item it is highlighting -- still one selection, still the
			// visible one, just drawn by the game instead of by us.
			if (gameSteers) {
				if (down) tapVK(VK_RETURN);
				return true;
			}
			showCursor();
			clickHeld = down;
			sendTouch(down ? MotionEvent.ACTION_DOWN : MotionEvent.ACTION_UP);
			if (!down) scheduleCursorDim();
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
					org.tvp.kirikiri2.KR2Activity.nativeKeyAction(KeyEvent.KEYCODE_MENU, true);
					org.tvp.kirikiri2.KR2Activity.nativeKeyAction(KeyEvent.KEYCODE_MENU, false);
				}
			}
			return true;
		}
		int vk = vkForAction(action);
		if (vk == 0) {
			if (down && unboundLogBudget > 0) {
				unboundLogBudget--;
				Log.d(TAG, "Action " + action + " arrived, and a KAG game has no key for it");
			}
			return true;
		}
		if ("skip".equals(action)) {
			sendVKKey(vk, down); // skip is a held key, so mirror the press
			return true;
		}
		if (down) tapVK(vk);
		return true;
	}

	private int unboundLogBudget = 8;
	private int stepLogBudget = 40;

	/**
	 * What an Enginehost action means to a KAG game, as the virtual key the
	 * engine reads. This is stock KAG3's own keyboard interface, confirmed
	 * against Noble Works' MainWindow.processKeys: Control held is skip, A
	 * toggles auto, R opens the backlog, Escape is what a right click does,
	 * B steps back one text and F runs forward to the next stop.
	 *
	 * page_previous and page_next are B and F rather than the page keys. The
	 * previous page of a visual novel is the line before this one, which is
	 * what KAG's goBackByKey gives; the page keys only mean anything inside
	 * the backlog, and the backlog is scrolled by the right stick below.
	 * Going back was the user's first complaint and it had no button at all.
	 *
	 * quick_save and quick_load stay unbound: KAG's S and L shortcuts only
	 * exist in free-save-data mode, and a shoulder button that silently
	 * overwrites a save is worse than one that does nothing. Saving is on the
	 * system menu. They are logged rather than swallowed, so a person who
	 * presses one can see that it was received and meant nothing.
	 */
	private static int vkForAction(String action) {
		if ("cancel".equals(action)) return VK_ESCAPE;
		if ("skip".equals(action)) return VK_CONTROL;
		if ("auto".equals(action)) return VK_A;
		if ("history".equals(action)) return VK_R;
		if ("page_previous".equals(action)) return VK_B;
		if ("page_next".equals(action)) return VK_F;
		return 0;
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

	/** A press and its release, the release held long enough to be seen. */
	private void tapVK(int vk) {
		sendVKKey(vk, true);
		sendVKKey(vk, false);
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
		StringBuilder text = new StringBuilder(
				"D-pad: step   Left stick: pointer   Right stick: scroll");
		for (Map.Entry<String, String> meaning : ACTION_MEANINGS.entrySet()) {
			String label = buttons.get(meaning.getKey());
			if (label == null) continue;
			text.append("\n").append(label).append(": ").append(meaning.getValue());
		}
		return text.toString();
	}
}
