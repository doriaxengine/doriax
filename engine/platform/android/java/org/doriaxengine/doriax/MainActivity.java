package org.doriaxengine.doriax;

import android.app.Activity;
import android.os.Build.VERSION;
import android.os.Build.VERSION_CODES;
import android.os.Bundle;
import android.view.KeyEvent;
import android.view.View;
import android.view.WindowManager.LayoutParams;
import android.view.inputmethod.EditorInfo;
import androidx.activity.OnBackPressedCallback;
import androidx.core.view.WindowCompat;
import androidx.core.view.WindowInsetsCompat;
import androidx.core.view.WindowInsetsControllerCompat;

import com.google.androidgamesdk.GameActivity;
import com.google.androidgamesdk.gametextinput.InputConnection;
import com.google.androidgamesdk.gametextinput.State;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

// A minimal extension of GameActivity. For this sample, it is only used to invoke
// a workaround for loading the runtime shared library on old Android versions
public class MainActivity extends GameActivity {

	// Load our native library:
	static {
		System.loadLibrary("doriax-android");
	}

	private UserSettings userSettings;
	// Null when the export leaves AdMob or Google Play Billing out
	private Object adMobWrapper;
	private Object billingWrapper;
	private final List<ActivityListener> activityListeners = new ArrayList<>();
	private InputConnection textInput;
	// Held keys, and whether the text input got their press
	private final Map<Long, Boolean> typedKeys = new HashMap<>();

	private native void onBackPressedNative();
	private native void onTextShownNative(String text, int session);

	private void hideSystemUI() {
		// This will put the game behind any cutouts and waterfalls on devices which have
		// them, so the corresponding insets will be non-zero.
		if (VERSION.SDK_INT >= VERSION_CODES.P) {
			getWindow().getAttributes().layoutInDisplayCutoutMode
					= LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS;
		}
		// From API 30 onwards, this is the recommended way to hide the system UI, rather than
		// using View.setSystemUiVisibility.
		View decorView = getWindow().getDecorView();
		WindowInsetsControllerCompat controller = new WindowInsetsControllerCompat(getWindow(),
				decorView);
		controller.hide(WindowInsetsCompat.Type.systemBars());
		controller.hide(WindowInsetsCompat.Type.displayCutout());
		controller.setSystemBarsBehavior(
				WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
	}

	// The wrapper sources are compiled only when the export enables their service
	private Object createOptionalWrapper(String className) {
		try {
			Object wrapper = Class.forName(className).getConstructor(Activity.class).newInstance(this);
			if (wrapper instanceof ActivityListener) {
				activityListeners.add((ActivityListener) wrapper);
			}
			return wrapper;
		} catch (ReflectiveOperationException | LinkageError e) {
			return null;
		}
	}

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		// When true, the app will fit inside any system UI windows.
		// When false, we render behind any system UI windows.
		WindowCompat.setDecorFitsSystemWindows(getWindow(), false);
		hideSystemUI();

		userSettings = new UserSettings(this);
		adMobWrapper = createOptionalWrapper("org.doriaxengine.doriax.AdMobWrapper");
		billingWrapper = createOptionalWrapper("org.doriaxengine.doriax.BillingWrapper");

		// Games get Back as Escape. A callback, since Android 16 no longer sends it as a key
		getOnBackPressedDispatcher().addCallback(this, new OnBackPressedCallback(true) {
			@Override
			public void handleOnBackPressed() {
				onBackPressedNative();
			}
		});

		super.onCreate(savedInstanceState);

		// GameTextInput's listener takes every key, so dispatchKeyEvent gives it the ones it types
		textInput = mSurfaceView.onCreateInputConnection(new EditorInfo());
		mSurfaceView.setOnKeyListener(null);
	}

	private static boolean isTextKey(KeyEvent event) {
		return event.getKeyCode() == KeyEvent.KEYCODE_DEL || event.getUnicodeChar() != 0;
	}

	// A key stays with the text input or the game, whichever got its press, until it is released
	@Override
	public boolean dispatchKeyEvent(KeyEvent event) {
		long key = ((long) event.getDeviceId() << 32) | event.getKeyCode();
		boolean down = event.getAction() == KeyEvent.ACTION_DOWN;

		Boolean typed;
		if (down && event.getRepeatCount() == 0) {
			typed = mSurfaceView.isFocused() && isTextKey(event);
			typedKeys.put(key, typed);
		} else {
			typed = down ? typedKeys.get(key) : typedKeys.remove(key);
			if (typed == null) {
				// The on-screen keyboard took the press
				typed = isTextKey(event);
			}
		}

		if (!typed) {
			return super.dispatchKeyEvent(event);
		}
		if (down && mSurfaceView.isFocused()) {
			textInput.sendKeyEvent(event);
		}
		return true;
	}

	// The on-screen keyboard can end its session after the native side is gone
	@Override
	public void stateChanged(State newState, boolean dismissed) {
		if (!mDestroyed) {
			super.stateChanged(newState, dismissed);
		}
	}

	// Called from native threads, like hideTextInput
	public void showTextInput(String text, int session) {
		runOnUiThread(() -> {
			if (mDestroyed) {
				return;
			}
			onTextShownNative(text, session);
			textInput.setState(new State(text, text.length(), text.length(), -1, -1));
			textInput.setSoftKeyboardActive(true, 0);
		});
	}

	public void hideTextInput() {
		runOnUiThread(() -> {
			textInput.setSoftKeyboardActive(false, 0);
			if (mSurfaceView.isFocused()) {
				releaseTextInputFocus();
			}
		});
	}

	// The surface view would keep the focus, and the keyboard the keys
	private void releaseTextInputFocus() {
		View content = (View) mSurfaceView.getParent();
		if (VERSION.SDK_INT >= VERSION_CODES.O) {
			content.setDefaultFocusHighlightEnabled(false);
		}
		content.setFocusableInTouchMode(true);
		content.requestFocus();
		mSurfaceView.setFocusable(false);
	}

	@Override
	protected void onResume() {
		super.onResume();
		hideSystemUI();
		for (ActivityListener listener : activityListeners) {
			listener.onActivityResume();
		}
	}

	@Override
	protected void onPause() {
		for (ActivityListener listener : activityListeners) {
			listener.onActivityPause();
		}
		super.onPause();
	}

	@Override
	protected void onDestroy() {
		for (ActivityListener listener : activityListeners) {
			listener.onActivityDestroy();
		}
		super.onDestroy();
	}

	public UserSettings getUserSettings() {
		return userSettings;
	}

	public Object getAdMobWrapper() {
		return adMobWrapper;
	}

	public Object getBillingWrapper() {
		return billingWrapper;
	}
}
