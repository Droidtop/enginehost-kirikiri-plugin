package com.yuri.kirikiri2;
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
	public int get_res_sd_operate_step() { return R.drawable.sd_operate_step; }
}
