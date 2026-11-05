package io.github.xenophobentx.takedawn;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.os.Bundle;
import android.os.Handler;
import android.os.SystemClock;
import android.util.Log;
import android.view.Gravity;
import android.widget.TextView;

import java.io.File;
import java.util.List;

/**
 * "Restart the game now" (the overlay's Restart game and driver rows, the
 * box after a lost device): in a process of its own
 * (android:process=":restart"), so it outlives the game's. The game's
 * process tears its GPU down and ends itself (fb_present_sdl.c, "the way
 * out on Android"); this page waits until that process is really gone, so
 * the next one never opens the GPU (kgsl) while the old one still holds it,
 * then starts the app again with the driver that was chosen, and goes.
 */
public class RestartActivity extends Activity {
    static final String EXTRA_PID = "io.github.xenophobentx.takedawn.OLD_PID";
    /** "setup": open the setup page in the new process, not the game (SetupActivity: another
     * disc's library after an install). */
    static final String EXTRA_THEN = "io.github.xenophobentx.takedawn.THEN";
    private static final String TAG = GameActivity.TAG;
    /** How long the old process is given before it is ended from here. */
    private static final long WAIT_MS = 4000;
    private static final long KILLED_WAIT_MS = 1000;
    private static final long POLL_MS = 25;

    private int oldPid;
    private long t0;
    private boolean killed;
    private final Handler handler = new Handler();

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        TextView t = new TextView(this);
        t.setText("Restarting…");
        t.setTextColor(Color.WHITE);
        t.setBackgroundColor(Color.BLACK);
        t.setGravity(Gravity.CENTER);
        setContentView(t);
        oldPid = getIntent().getIntExtra(EXTRA_PID, 0);
        t0 = SystemClock.uptimeMillis();
        Log.i(TAG, "restart: waiting for the game's process " + oldPid + " to end");
        handler.post(poll);
    }

    private final Runnable poll = new Runnable() {
        @Override public void run() {
            long waited = SystemClock.uptimeMillis() - t0;
            if (!alive(RestartActivity.this, oldPid)) {
                Log.i(TAG, "restart: the game's process " + oldPid + " is gone after "
                        + waited + " ms" + (killed ? " (ended from here)" : "")
                        + "; starting the game again");
                startActivity(next());
                finish();
                return;
            }
            if (!killed && waited >= WAIT_MS) {
                Log.w(TAG, "restart: the game's process " + oldPid + " is still there after "
                        + waited + " ms: ending it");
                android.os.Process.killProcess(oldPid);
                killed = true;
            } else if (killed && waited >= WAIT_MS + KILLED_WAIT_MS) {
                Log.w(TAG, "restart: the game's process " + oldPid + " did not go in "
                        + waited + " ms; starting anyway");
                startActivity(next());
                finish();
                return;
            }
            handler.postDelayed(this, POLL_MS);
        }
    };

    /** What opens once the old process is gone: the game (StartActivity), or the setup page. */
    private Intent next() {
        Class<?> c = "setup".equals(getIntent().getStringExtra(EXTRA_THEN)) ? SetupActivity.class
                : StartActivity.class;
        return new Intent(RestartActivity.this, c).putExtra(EXTRA_PID, oldPid)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
    }

    /**
     * Whether the process is still there: its /proc entry (this app's own
     * processes are visible to it) or the activity manager's list. A pid
     * of 0 is none.
     */
    static boolean alive(Context c, int pid) {
        if (pid <= 0 || pid == android.os.Process.myPid()) {
            return false;
        }
        if (new File("/proc/" + pid).exists()) {
            return true;
        }
        try {
            ActivityManager am = (ActivityManager) c.getSystemService(Context.ACTIVITY_SERVICE);
            List<ActivityManager.RunningAppProcessInfo> ps = am.getRunningAppProcesses();
            if (ps != null) {
                for (ActivityManager.RunningAppProcessInfo p : ps) {
                    if (p.pid == pid) {
                        return true;
                    }
                }
            }
        } catch (Exception e) {
            Log.w(TAG, "restart: process list: " + e);
        }
        return false;
    }

    /**
     * Another process of this app running the game (the app's main process,
     * where the game lives, under a pid other than this one's): a game
     * process from before that has not finished ending. 0 when there is
     * none. Reads /proc, where an app sees its own processes.
     */
    static int otherGameProcess(Context c) {
        String name = c.getPackageName();
        int me = android.os.Process.myPid();
        File[] ds = new File("/proc").listFiles();
        if (ds == null) {
            return 0;
        }
        for (File d : ds) {
            int pid;
            try {
                pid = Integer.parseInt(d.getName());
            } catch (NumberFormatException e) {
                continue;
            }
            if (pid == me) {
                continue;
            }
            try (java.io.FileInputStream in = new java.io.FileInputStream(new File(d, "cmdline"))) {
                byte[] b = new byte[256];
                int n = in.read(b);
                if (n <= 0) {
                    continue;
                }
                int end = 0;
                while (end < n && b[end] != 0) {
                    end++;
                }
                if (name.equals(new String(b, 0, end, java.nio.charset.StandardCharsets.UTF_8))) {
                    return pid;
                }
            } catch (Exception ignored) {
                // Another user's process, or gone meanwhile.
            }
        }
        return 0;
    }
}
