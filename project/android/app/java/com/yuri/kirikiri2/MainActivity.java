package com.yuri.kirikiri2;
import org.tvp.kirikiri2.KR2Activity;

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
	@Override
	public int get_res_sd_operate_step() { return R.drawable.sd_operate_step; }
}
