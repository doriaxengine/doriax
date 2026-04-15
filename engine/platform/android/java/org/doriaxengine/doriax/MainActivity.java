package org.doriaxengine.doriax;

import android.os.Build.VERSION;
import android.os.Build.VERSION_CODES;
import android.os.Bundle;
import android.view.View;
import android.view.WindowManager.LayoutParams;
import androidx.core.view.WindowCompat;
import androidx.core.view.WindowInsetsCompat;
import androidx.core.view.WindowInsetsControllerCompat;

import com.google.androidgamesdk.GameActivity;

// A minimal extension of GameActivity. For this sample, it is only used to invoke
// a workaround for loading the runtime shared library on old Android versions
public class MainActivity extends GameActivity {

	// Load our native library:
	static {
		System.loadLibrary("doriax-android");
	}

	private UserSettings userSettings;
	private AdMobWrapper admobWrapper;

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

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		// When true, the app will fit inside any system UI windows.
		// When false, we render behind any system UI windows.
		WindowCompat.setDecorFitsSystemWindows(getWindow(), false);
		hideSystemUI();

		userSettings = new UserSettings(this);
		admobWrapper = new AdMobWrapper(this);

		super.onCreate(savedInstanceState);
	}

	@Override
	protected void onResume() {
		super.onResume();
		hideSystemUI();
	}

	public UserSettings getUserSettings() {
		return userSettings;
	}

	public AdMobWrapper getAdMobWrapper() {
		return admobWrapper;
	}
}
