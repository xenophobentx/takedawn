package io.github.xenophobentx.takedawn;

import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.system.Os;
import android.util.Log;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;

import org.libsdl.app.SDLActivity;

/**
 * The game: SDL2's activity with the recompiled title in libmain.so.
 *
 * Before SDL loads the libraries it puts the app's directories into the
 * environment, where the runtime looks for them (the toolkit's
 * src/platform/android_host.h):
 *
 *   RECOMP_ANDROID_DIR  the app's external files directory, the game's
 *                       working directory:
 *                         Burnout 3 Takedown/  the user's disc, unpacked
 *                         takedawn/            saves, settings.toml, crash reports
 *                         enhance.toml         optional
 *                         env.txt              optional, NAME=value lines
 *                         driver/              optional, a Vulkan driver to try (GpuDriver)
 *                         takedawn.log         the log (the one before: .1)
 *   XDG_DATA_HOME       the same directory (the save folder goes under it)
 *   XDG_CACHE_HOME      the app's cache directory (the scaled 2D art)
 *   RECOMP_RENDER_SHADER_CACHE  files/graphics, the shader key lists and driver caches (BO-208)
 *   RECOMP_ANDROID_DRIVER_DIR / _NAME, RECOMP_ANDROID_LIB_DIR
 *                       the Vulkan driver to load in place of the device's
 *
 * env.txt stands in for a shell's environment: every RECOMP_* or BO3_*
 * variable of docs/running.md can be set there for a test.
 */
public class GameActivity extends SDLActivity {
    static final String TAG = "takedawn";
    static final String GAME_DIR = "Burnout 3 Takedown";

    /** The app's files directory: external, so adb and MTP reach it. */
    static File filesDir(Context c) {
        File f = c.getExternalFilesDir(null);
        return f != null ? f : c.getFilesDir();
    }

    @Override
    protected void onCreate(Bundle state) {
        waitForOldGame();
        File files = filesDir(this);
        try {
            String dir = files.getAbsolutePath();
            Os.setenv("RECOMP_ANDROID_DIR", dir, true);
            Os.setenv("XDG_DATA_HOME", dir, true);
            Os.setenv("XDG_CACHE_HOME", getCacheDir().getAbsolutePath(), true);
            Os.setenv("HOME", getFilesDir().getAbsolutePath(), true);
            // BO-208: the shader key lists, SPIR-V and the driver's pipeline caches
            // are what the first start builds (a minute on a handheld); they live in
            // the app's files, which the system does not empty, not in cache/.
            // (The scaled 2D art stays in cache/, the toolkit's ui_scale store.)
            Os.setenv("RECOMP_RENDER_SHADER_CACHE", new File(getFilesDir(), "graphics").getAbsolutePath(), true);
            Os.setenv("RECOMP_ANDROID_LIB_DIR", getApplicationInfo().nativeLibraryDir, true);
            java.util.Map<String, String> env = readEnvFile(new File(files, "env.txt"));
            // A test script's variables (tools/android/scene-launch.sh): their
            // own file, used for this start only and removed, so a script
            // that dies leaves nothing behind for the player's next start.
            File scene = new File(files, "scene-env.txt");
            if (scene.isFile()) {
                env.putAll(readEnvFile(scene));
                scene.delete();
                Log.i(TAG, "scene-env.txt: a test script's settings, for this start only");
            }
            // The Vulkan driver (GpuDriver): the device's own, or a replacement
            // the runtime opens through libadrenotools.
            GpuDriver d = GpuDriver.choose(this, env.get("RECOMP_ANDROID_DRIVER"));
            startedWith = GpuDriver.describe(this, GpuDriver.choice(this));
            startedBundled = GpuDriver.BUNDLED.equals(d.name);
            startedOverride = env.get("RECOMP_ANDROID_DRIVER") != null;
            Log.i(TAG, "GPU driver: " + d.what);
            if (d.name != null) {
                Os.setenv("RECOMP_ANDROID_DRIVER_DIR", d.dir, true);
                Os.setenv("RECOMP_ANDROID_DRIVER_NAME", d.name, true);
            } else {
                Os.unsetenv("RECOMP_ANDROID_DRIVER_DIR");
                Os.unsetenv("RECOMP_ANDROID_DRIVER_NAME");
            }
            // What the game says back: the driver it got (for the driver
            // page), and that a driver on trial has shown frames.
            Os.setenv("RECOMP_ANDROID_DRIVER_INFO", GpuDriver.infoFile(this).getAbsolutePath(), true);
            Os.unsetenv("RECOMP_ANDROID_DRIVER_TRIAL");
            Os.unsetenv("RECOMP_ANDROID_DRIVER_FALLBACK_DIR");
            if (d.trial) {
                File trial = GpuDriver.trialFile(this);
                try (java.io.FileOutputStream o = new java.io.FileOutputStream(trial)) {
                    // For the note when it fails: the player's words.
                    o.write(startedWith.getBytes(java.nio.charset.StandardCharsets.UTF_8));
                }
                Os.setenv("RECOMP_ANDROID_DRIVER_TRIAL", trial.getAbsolutePath(), true);
            }
            if (d.name != null && !GpuDriver.BUNDLED.equals(d.name) && GpuDriver.hasBundled(this)) {
                // A package the loader cannot open: the bundled one, at once.
                Os.setenv("RECOMP_ANDROID_DRIVER_FALLBACK_DIR",
                        getApplicationInfo().nativeLibraryDir, true);
                Os.setenv("RECOMP_ANDROID_DRIVER_FALLBACK_NAME", GpuDriver.BUNDLED, true);
            }
            if (d.note != null) {
                android.widget.Toast.makeText(this, d.note, android.widget.Toast.LENGTH_LONG).show();
                Log.i(TAG, "GPU driver: " + d.note);
            }
            for (java.util.Map.Entry<String, String> e : env.entrySet()) {
                Os.setenv(e.getKey(), e.getValue(), true);
                Log.i(TAG, "env.txt: " + e.getKey() + "=" + e.getValue());
            }
        } catch (Exception e) {
            Log.e(TAG, "environment: " + e);
        }
        super.onCreate(state);
    }

    /**
     * The system Back (button or gesture) must not end the game mid-race:
     * SDL's own handling finishes the activity and the present loop then
     * exits. It is passed to the game as SDL's Back key (SDLK_AC_BACK), which
     * the settings overlay takes: closed, it opens the overlay (as F1 or
     * Select+Start do); open, it goes back like B. Leaving the game is the
     * system's app switcher (the overlay has Restart game, no quit row). If
     * the native side is not up (the call fails), the system's own behaviour
     * stays.
     */
    @Override
    public void onBackPressed() {
        try {
            SDLActivity.onNativeKeyDown(android.view.KeyEvent.KEYCODE_BACK);
            SDLActivity.onNativeKeyUp(android.view.KeyEvent.KEYCODE_BACK);
        } catch (UnsatisfiedLinkError | RuntimeException e) {
            Log.w(TAG, "back: " + e);
            super.onBackPressed();
        }
    }

    /**
     * The menu pictures' run (upscale.c, BO-326), from its thread: a
     * foreground service while it lasts (UpscaleService), so that the app in
     * the background is not frozen in the middle of it.
     */
    @SuppressWarnings("unused")
    public void upscaleService(boolean on) {
        Intent i = new Intent(this, UpscaleService.class);
        try {
            if (on) {
                startService(i);
            } else {
                stopService(i);
            }
        } catch (RuntimeException e) {
            Log.w(TAG, "upscale service: " + e);
        }
    }

    private String startedWith = "";
    private boolean startedBundled, startedOverride;

    /**
     * A game process from before (a restart, or a start right after a quit)
     * that is still ending: the GPU driver (kgsl) must not be opened while
     * it still holds the GPU. Waits for it, up to a few seconds, before SDL
     * loads the game's library. The pid a restart waited for comes with the
     * intent (RestartActivity); any other process of this app under the
     * main process's name is looked for as well.
     */
    private void waitForOldGame() {
        int named = getIntent() != null ? getIntent().getIntExtra(RestartActivity.EXTRA_PID, 0) : 0;
        int other = RestartActivity.otherGameProcess(this);
        long t0 = android.os.SystemClock.uptimeMillis();
        if (!RestartActivity.alive(this, named) && other == 0) {
            Log.i(TAG, "start: no game process from before (pid " + android.os.Process.myPid()
                    + (named > 0 ? ", the restart's " + named + " is gone" : "") + ")");
            return;
        }
        Log.w(TAG, "start: a game process from before is still there (" + (named > 0 ? named : other)
                + "); waiting for it");
        while (android.os.SystemClock.uptimeMillis() - t0 < 3000) {
            other = RestartActivity.otherGameProcess(this);
            if (!RestartActivity.alive(this, named) && other == 0) {
                break;
            }
            android.os.SystemClock.sleep(25);
        }
        long waited = android.os.SystemClock.uptimeMillis() - t0;
        if (RestartActivity.alive(this, named) || other != 0) {
            Log.w(TAG, "start: still there after " + waited + " ms; starting anyway");
        } else {
            Log.i(TAG, "start: the game process from before is gone after " + waited + " ms");
        }
    }

    /**
     * Ends this game: the activity is finished (SDL then sends SDL_QUIT, the
     * window loop ends, the runtime tears the GPU down and calls exit(0):
     * fb_present_sdl.c, "the way out on Android"). Should the process still
     * be there a while later, it is ended from here.
     */
    private void endGame(final String why) {
        Log.i(TAG, "end: " + why + ": finishing the activity; the runtime tears the GPU down"
                + " and ends the process");
        runOnUiThread(new Runnable() {
            @Override public void run() {
                finishAndRemoveTask();
            }
        });
        armKillWatchdog(why);
    }

    private static boolean killArmed;

    /** The last resort: the process ends this long after the way out began. */
    private static synchronized void armKillWatchdog(final String why) {
        if (killArmed) {
            return;
        }
        killArmed = true;
        Thread t = new Thread(new Runnable() {
            @Override public void run() {
                android.os.SystemClock.sleep(8000);
                Log.w(TAG, "end (" + why + "): the process is still there after 8 s; killing it");
                android.os.Process.killProcess(android.os.Process.myPid());
            }
        }, "takedawn-exit-watchdog");
        t.setDaemon(true);
        t.start();
    }

    /**
     * Ends this process; a page in another process (RestartActivity) waits
     * until it is gone and starts the app again. Any thread: the page is
     * started at once, on the calling thread, so it is on its way even when
     * the runtime ends the process right after this returns (the box after
     * a lost device).
     */
    private void restartApp() {
        int pid = android.os.Process.myPid();
        Log.i(TAG, "restart: the restart page waits for this process (" + pid + ") to end");
        startActivity(new android.content.Intent(this, RestartActivity.class)
                .putExtra(RestartActivity.EXTRA_PID, pid));
        endGame("restart");
    }

    /**
     * The overlay's "Restart game" (the toolkit's recomp_restart, through
     * the window layer's JNI call; BO-124): the app starts again, the same
     * way as after a driver change. Returns 1.
     */
    @SuppressWarnings("unused")
    int restartGame() {
        Log.i(TAG, "restart asked for from the game");
        restartApp();
        return 1;
    }

    /**
     * The start-up warning's "Use built-in" (the runtime's window when no
     * usable Vulkan driver was found, fb_present_sdl.c; BO-121): on a driver
     * that cannot run the game there is no settings overlay to choose
     * another one from, so the way back is on that window. doIt 0: what the
     * window may offer, as bits: 1 the driver in the app to go back to (an
     * Adreno, and this start was not on it), 2 a driver package (Turnip
     * drives Adrenos only), 4 this start was on the driver in the app.
     * doIt 1: back to Automatic (GpuDriver.fallback) and start again.
     */
    @SuppressWarnings("unused")
    int resetGraphicsDriver(int doIt) {
        boolean adreno = GpuDriver.isAdreno();
        int ways = (adreno && !startedBundled && GpuDriver.hasBundled(this) ? 1 : 0)
                | (adreno ? 2 : 0) | (startedBundled ? 4 : 0);
        if (doIt != 0 && (ways & 1) != 0) {
            GpuDriver.setChoice(this, GpuDriver.fallback(this));
            Log.i(TAG, "GPU driver: reset to " + GpuDriver.choice(this) + " (the driver in the"
                    + " app); restarting");
            restartApp();
        }
        return ways;
    }

    /**
     * The graphics device was lost while the game ran (fb_present_sdl.c's
     * box; BO-163): a driver other than the one in the app (a package, the
     * device's own on an Adreno) is dropped as a failed trial would be, so
     * a driver that dies later in a race does not come back at every start.
     * Returns 1 when the choice went back (the box says so).
     */
    @SuppressWarnings("unused")
    int graphicsDriverLost() {
        if (startedBundled || startedOverride || !GpuDriver.isAdreno()
                || !GpuDriver.hasBundled(this)) {
            return 0;
        }
        GpuDriver.dropFailed(this, startedWith, true);
        return 1;
    }

    /**
     * The start-up warning's "Driver package…" (fb_present_sdl.c; BO-153):
     * the driver page on the system's file picker, over the game, which goes
     * on on the CPU behind it. A package picked there is chosen and the app
     * starts again on it (restartRunning); a cancelled picker leaves the page
     * open, and Back the game. That is the way out on a GPU the Turnip in the
     * app cannot run (an Adreno A6xx "gen1 low": Vulkan 1.0 only), where the
     * warning has no reset to offer. Returns 1.
     */
    @SuppressWarnings("unused")
    int pickDriverPackage() {
        Log.i(TAG, "GPU driver: the warning's driver package: the driver page on the file picker");
        runOnUiThread(new Runnable() {
            @Override public void run() {
                startActivity(new android.content.Intent(GameActivity.this, DriverActivity.class)
                        .putExtra(DriverActivity.EXTRA_PICK, true));
            }
        });
        return 1;
    }

    /**
     * The driver page's restart, after a package picked from the warning:
     * the game in this process starts again (on the driver now chosen), the
     * same way as the overlay's restart. False when no game is running here.
     */
    static boolean restartRunning() {
        SDLActivity s = mSingleton;
        if (!(s instanceof GameActivity)) {
            return false;
        }
        ((GameActivity) s).restartApp();
        return true;
    }

    /**
     * The overlay's driver rows (main.c's driver_row, on the overlay's
     * thread). Row 0: the driver the next start loads; activated, the next
     * of the choices. Row 1: the driver page, on top of the held game.
     * Row 2: only when the choice is no longer what this start loaded: a
     * restart. The driver is loaded before the title starts, so a change
     * here is for the next start, and the rows say so.
     */
    @SuppressWarnings("unused")
    String driverMenu(int row, int activate) {
        String now = GpuDriver.describe(this, GpuDriver.choice(this));
        if (row == 0) {
            if (activate != 0) {
                GpuDriver.setChoice(this, GpuDriver.nextChoice(this));
                now = GpuDriver.describe(this, GpuDriver.choice(this));
            }
            return "Driver at the next start: " + now;
        }
        if (row == 1) {
            if (activate != 0) {
                runOnUiThread(new Runnable() {
                    @Override public void run() {
                        startActivity(new android.content.Intent(GameActivity.this, DriverActivity.class));
                    }
                });
            }
            return "Driver files... (pick a .zip; the game waits)";
        }
        if (row == 2) {
            if (now.equals(startedWith)) {
                return "";
            }
            if (activate != 0) {
                restartApp();
            }
            return "Restart now to use it (the race in hand is lost)";
        }
        return "";
    }

    /** NAME=value lines; '#' starts a comment line. */
    private static java.util.Map<String, String> readEnvFile(File f) throws Exception {
        java.util.Map<String, String> env = new java.util.LinkedHashMap<>();
        if (!f.isFile()) {
            return env;
        }
        try (BufferedReader r = new BufferedReader(new FileReader(f))) {
            String line;
            while ((line = r.readLine()) != null) {
                line = line.trim();
                int eq = line.indexOf('=');
                if (line.isEmpty() || line.startsWith("#") || eq <= 0) {
                    continue;
                }
                env.put(line.substring(0, eq).trim(), line.substring(eq + 1).trim());
            }
        }
        return env;
    }

    // The whole panel: no status or navigation bar (immersive sticky), into
    // the display cutout. SDL does this itself for a fullscreen window; it is
    // repeated here whenever the window gets the focus back, because a
    // swipe from the edge, a dialog or the volume panel brings the bars back.
    private void immersive() {
        android.view.Window w = getWindow();
        w.addFlags(android.view.WindowManager.LayoutParams.FLAG_FULLSCREEN
                | android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        android.view.WindowManager.LayoutParams lp = w.getAttributes();
        lp.layoutInDisplayCutoutMode =
                android.view.WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        w.setAttributes(lp);
        w.getDecorView().setSystemUiVisibility(android.view.View.SYSTEM_UI_FLAG_FULLSCREEN
                | android.view.View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | android.view.View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                | android.view.View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | android.view.View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | android.view.View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
    }

    // The device's vibrator (the runtime's android_vibration.c drives it for
    // rumble) stops at once when the game leaves the screen.
    @Override
    protected void onPause() {
        stopVibrator();
        super.onPause();
    }

    // Also when the activity is stopped or destroyed without a pause that
    // got through (the system reclaiming it, finish() from another path).
    @Override
    protected void onStop() {
        stopVibrator();
        super.onStop();
    }

    // SDL's onDestroy sends SDL_QUIT and waits up to a second for its
    // thread; the runtime's way out ends the process (exit(0)) once the GPU
    // is torn down. The watchdog covers a native side that never ends.
    @Override
    protected void onDestroy() {
        stopVibrator();
        Log.i(TAG, "onDestroy (finishing " + isFinishing() + ")");
        if (isFinishing()) {
            armKillWatchdog("onDestroy");
        }
        super.onDestroy();
    }

    private void stopVibrator() {
        try {
            ((android.os.Vibrator) getSystemService(VIBRATOR_SERVICE)).cancel();
        } catch (Exception ignored) {
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        Log.i(TAG, "window focus " + hasFocus);
        if (hasFocus) {
            immersive();
            // After the layout pass that applies it: the insets are known then.
            getWindow().getDecorView().post(new Runnable() {
                @Override
                public void run() {
                    touchInsets();
                }
            });
        }
    }

    // The on-screen controls keep out of the display cutout (a notch, a
    // hole punch): its safe insets in pixels, for the runtime's touch layer
    // (RECOMP_TOUCH_INSETS, read again while running).
    private void touchInsets() {
        android.view.WindowInsets wi = getWindow().getDecorView().getRootWindowInsets();
        if (wi == null) {
            return;
        }
        android.view.DisplayCutout dc = wi.getDisplayCutout();
        String v = dc == null ? "0,0,0,0"
                : dc.getSafeInsetLeft() + "," + dc.getSafeInsetTop() + ","
                  + dc.getSafeInsetRight() + "," + dc.getSafeInsetBottom();
        try {
            Os.setenv("RECOMP_TOUCH_INSETS", v, true);
            Log.i(TAG, "touch insets " + v);
        } catch (Exception e) {
            Log.w(TAG, "touch insets: " + e);
        }
    }

    // And whenever the bars come back for any other reason (SDL's own
    // window-style command, a start over an app that shows them): hide them
    // again a moment later.
    @Override
    protected void onResume() {
        super.onResume();
        final android.view.View decor = getWindow().getDecorView();
        decor.setOnSystemUiVisibilityChangeListener(
                new android.view.View.OnSystemUiVisibilityChangeListener() {
            @Override
            public void onSystemUiVisibilityChange(int visibility) {
                Log.i(TAG, "system bars " + ((visibility
                        & android.view.View.SYSTEM_UI_FLAG_FULLSCREEN) == 0 ? "shown" : "hidden"));
                if ((visibility & android.view.View.SYSTEM_UI_FLAG_FULLSCREEN) == 0) {
                    decor.postDelayed(new Runnable() {
                        @Override
                        public void run() {
                            immersive();
                        }
                    }, 300);
                }
            }
        });
        immersive();
        requestDisplayMode();
        tellGameState();
    }

    private int displayModeAsked = -1;

    /**
     * The display's refresh rate is kept where it is, or raised, never
     * lowered: a 120 Hz panel shows the game's 60 fps with each frame held
     * twice and less latency than at 60 Hz. The mode asked for is the one at
     * the current resolution with the highest refresh rate that is a whole
     * multiple of 60 (60, 120, 180, 240) and not below the current one; a
     * phone played with a controller alone sees no touches, and some systems
     * (MIUI) then drop the refresh rate when the app has not asked for one
     * (BO-148). A panel whose current rate has no such mode (90 Hz) is left
     * alone. Asked again at every resume. The surface asks for no frame
     * rate (the toolkit's android_perf.c says why), and the system is told
     * a game is being played (GameManager.setGameState).
     */
    private void requestDisplayMode() {
        try {
            android.view.Display d = getDisplay();
            if (d == null) {
                return;
            }
            android.view.Display.Mode cur = d.getMode();
            android.view.Display.Mode best = null;
            for (android.view.Display.Mode m : d.getSupportedModes()) {
                if (m.getPhysicalWidth() != cur.getPhysicalWidth()
                        || m.getPhysicalHeight() != cur.getPhysicalHeight()) {
                    continue;
                }
                float hz = m.getRefreshRate();
                float k = hz / 60.0f;
                boolean whole = k >= 0.98f && Math.abs(k - Math.round(k)) < 0.02f * Math.round(k);
                if (!whole || hz < cur.getRefreshRate() - 0.5f) {
                    continue;
                }
                if (best == null || hz > best.getRefreshRate()) {
                    best = m;
                }
            }
            if (best == null) {
                if (displayModeAsked != -2) {
                    displayModeAsked = -2;
                    Log.i(TAG, "display mode: left as it is (" + cur.getModeId() + " @ "
                            + String.format(java.util.Locale.ROOT, "%.2f", cur.getRefreshRate())
                            + " Hz: no mode at a multiple of 60 Hz at or above it)");
                }
                return;
            }
            android.view.Window w = getWindow();
            android.view.WindowManager.LayoutParams lp = w.getAttributes();
            lp.preferredDisplayModeId = best.getModeId();
            lp.preferredRefreshRate = best.getRefreshRate();
            w.setAttributes(lp);
            if (displayModeAsked != best.getModeId()) {
                displayModeAsked = best.getModeId();
                StringBuilder all = new StringBuilder();
                for (android.view.Display.Mode m : d.getSupportedModes()) {
                    all.append(all.length() > 0 ? ", " : "").append(m.getModeId()).append(':')
                       .append(m.getPhysicalWidth()).append('x').append(m.getPhysicalHeight())
                       .append('@').append(String.format(java.util.Locale.ROOT, "%.2f", m.getRefreshRate()));
                }
                Log.i(TAG, "display mode: asked for " + best.getModeId() + " ("
                        + best.getPhysicalWidth() + "x" + best.getPhysicalHeight() + " @ "
                        + String.format(java.util.Locale.ROOT, "%.2f", best.getRefreshRate())
                        + " Hz), was " + cur.getModeId() + " @ "
                        + String.format(java.util.Locale.ROOT, "%.2f", cur.getRefreshRate())
                        + " Hz; API " + android.os.Build.VERSION.SDK_INT + "; modes " + all);
            }
        } catch (RuntimeException e) {
            Log.w(TAG, "display mode: " + e);
        }
    }

    private boolean gameStateSaid;

    /** Gameplay, not loading (API 33): a hint some systems' game modes use. */
    private void tellGameState() {
        try {
            android.app.GameManager gm = getSystemService(android.app.GameManager.class);
            if (gm == null) {
                return;
            }
            gm.setGameState(new android.app.GameState(false,
                    android.app.GameState.MODE_GAMEPLAY_UNINTERRUPTIBLE));
            if (!gameStateSaid) {
                gameStateSaid = true;
                Log.i(TAG, "game state: gameplay, uninterruptible (game mode "
                        + gm.getGameMode() + ")");
            }
        } catch (RuntimeException | LinkageError e) {
            Log.w(TAG, "game state: " + e);
        }
    }

    // The installed disc's program (StartActivity.library: libmain.so, libmain_pal.so or
    // libmain_ensv.so, src/game/package.h); SDL calls SDL_main in the last one.
    @Override
    protected String[] getLibraries() {
        String lib = StartActivity.library(this);
        Log.i(TAG, "game library: lib" + lib + ".so");
        return new String[] { "SDL2", lib };
    }

    // Landscape either way up, whatever SDL works out from the window size.
    @Override
    public void setOrientationBis(int w, int h, boolean resizable, String hint) {
        setRequestedOrientation(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
    }
}
