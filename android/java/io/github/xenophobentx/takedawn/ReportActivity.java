package io.github.xenophobentx.takedawn;

import android.app.Activity;
import android.content.ContentValues;
import android.content.Intent;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.MediaStore;
import android.system.Os;
import android.util.Log;
import android.util.TypedValue;
import android.view.Display;
import android.view.Gravity;
import android.view.InputDevice;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * The bug report from the start page, where no game runs (docs/running.md,
 * "Reporting a bug"): the same archive the in-game settings write, made by
 * the same native code (main.c's --bug-report, the toolkit's xbox_report.c).
 *
 * It runs in a process of its own (android:process=":report" in the
 * manifest): the native library reads the environment and the settings once
 * per process, and a game started afterwards must find neither already read.
 * The process ends with the activity.
 *
 * The archive is written to the app's files directory,
 * takedawn/reports/takedawn-report-<time>.zip, which a PC reaches over USB
 * (file transfer) and adb; "Save to Downloads" copies it to the device's
 * Download folder, where a browser or a file manager on the device finds it,
 * and "Share" hands that copy to another app. Nothing is sent by this app:
 * it has no network permission.
 */
public class ReportActivity extends Activity {
    static final String EXTRA_NOTE = "note";

    private static native String nativeBugReport(String note);

    private TextView text;
    private Button saveButton, shareButton;
    private File archive;
    private Uri downloaded;

    /** The start page's view with a "Create bug report" button under it. */
    static View withButton(final Activity a, View page, final String note) {
        LinearLayout box = new LinearLayout(a);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setBackgroundColor(Color.BLACK);
        box.addView(page, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));
        Button b = new Button(a);
        b.setText("Create bug report (Y)");
        b.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                open(a, note);
            }
        });
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        lp.gravity = Gravity.CENTER_HORIZONTAL;
        lp.bottomMargin = 32;
        box.addView(b, lp);
        return box;
    }

    static void open(Activity a, String note) {
        a.startActivity(new Intent(a, ReportActivity.class).putExtra(EXTRA_NOTE, note));
    }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setBackgroundColor(Color.BLACK);
        box.setGravity(Gravity.CENTER);
        text = new TextView(this);
        text.setTextColor(Color.WHITE);
        text.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16);
        text.setGravity(Gravity.CENTER);
        text.setPadding(48, 48, 48, 32);
        text.setText("Creating the bug report...");
        box.addView(text);
        saveButton = button(box, "Save a copy to Downloads");
        shareButton = button(box, "Share...");
        Button close = button(box, "Close");
        saveButton.setVisibility(View.GONE);
        shareButton.setVisibility(View.GONE);
        saveButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                if (toDownloads() != null) {
                    text.setText(text.getText() + "\n\nCopied to Download/" + archive.getName());
                    saveButton.setVisibility(View.GONE);
                }
            }
        });
        shareButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                Uri u = toDownloads();
                if (u == null) {
                    return;
                }
                Intent send = new Intent(Intent.ACTION_SEND);
                send.setType("application/zip");
                send.putExtra(Intent.EXTRA_STREAM, u);
                send.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
                startActivity(Intent.createChooser(send, "Share the bug report"));
            }
        });
        close.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                finish();
            }
        });
        setContentView(box);

        final String page = getIntent().getStringExtra(EXTRA_NOTE);
        new Thread(new Runnable() {
            @Override
            public void run() {
                final String r = make(page);
                runOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        done(r);
                    }
                });
            }
        }, "bug-report").start();
    }

    private Button button(LinearLayout box, String label) {
        Button b = new Button(this);
        b.setText(label);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        lp.gravity = Gravity.CENTER_HORIZONTAL;
        box.addView(b, lp);
        return b;
    }

    /** The native report; its path, or "!" and why there is none. */
    private String make(String page) {
        try {
            // The directories, as GameActivity gives them to the game.
            String dir = GameActivity.filesDir(this).getAbsolutePath();
            Os.setenv("RECOMP_ANDROID_DIR", dir, true);
            Os.setenv("XDG_DATA_HOME", dir, true);
            Os.setenv("XDG_CACHE_HOME", getCacheDir().getAbsolutePath(), true);
            Os.setenv("HOME", getFilesDir().getAbsolutePath(), true);
            Os.setenv("RECOMP_RENDER_SHADER_CACHE", new File(getFilesDir(), "graphics").getAbsolutePath(), true);   // BO-208
            Os.setenv("RECOMP_ANDROID_LIB_DIR", getApplicationInfo().nativeLibraryDir, true);
            System.loadLibrary("SDL2");
            System.loadLibrary(StartActivity.library(this));
            return nativeBugReport(note(page));
        } catch (Throwable e) {
            Log.e(GameActivity.TAG, "bug report: " + e);
            return "!" + e;
        }
    }

    /** What only this side knows: the device, its screen, its pads, its Vulkan. */
    private String note(String page) {
        StringBuilder s = new StringBuilder();
        s.append("Made from the Android start page; no game was running.\n");
        s.append("device: ").append(Build.MANUFACTURER).append(' ').append(Build.MODEL)
                .append(" (").append(Build.DEVICE).append("), SoC ").append(Build.SOC_MANUFACTURER)
                .append(' ').append(Build.SOC_MODEL).append(", Android ")
                .append(Build.VERSION.RELEASE).append(" (API ").append(Build.VERSION.SDK_INT)
                .append(")\n");
        try {
            Display.Mode m = getWindowManager().getDefaultDisplay().getMode();
            s.append("display: ").append(m.getPhysicalWidth()).append('x')
                    .append(m.getPhysicalHeight()).append(" at ").append(m.getRefreshRate())
                    .append(" Hz\n");
        } catch (Exception e) {
            s.append("display: ").append(e).append('\n');
        }
        try {
            s.append("Vulkan of the device's own driver: ")
                    .append(GpuDriver.versionString(GpuDriver.systemVulkanVersion(this)))
                    .append(GpuDriver.isAdreno() ? ", an Adreno GPU" : ", not an Adreno GPU")
                    .append(GpuDriver.usable(this) ? "; a usable driver is there\n"
                                                   : "; no driver the game can use\n");
        } catch (Exception e) {
            s.append("Vulkan: ").append(e).append('\n');
        }
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice d = InputDevice.getDevice(id);
            if (d != null && (d.getSources() & InputDevice.SOURCE_GAMEPAD)
                    == InputDevice.SOURCE_GAMEPAD) {
                s.append("pad: \"").append(d.getName()).append("\" ")
                        .append(String.format("%04x:%04x", d.getVendorId(), d.getProductId()))
                        .append(d.isExternal() ? ", external\n" : ", built in\n");
            }
        }
        if (page != null && !page.isEmpty()) {
            s.append("The start page said: ").append(page.replace('\n', ' ')).append('\n');
        }
        return s.toString();
    }

    private void done(String r) {
        if (r == null || r.startsWith("!")) {
            text.setText("The bug report could not be made.\n\n"
                    + (r == null ? "" : r.substring(1)));
            return;
        }
        archive = new File(r);
        text.setText("Bug report saved:\n\n" + r + "\n\n"
                + "It is in the app's own folder: copy it over USB (file transfer), or save a"
                + " copy to Downloads and attach it from there. Nothing was sent anywhere.");
        saveButton.setVisibility(View.VISIBLE);
        shareButton.setVisibility(View.VISIBLE);
        saveButton.requestFocus();
    }

    /** The archive copied into the device's Download folder, once. */
    private Uri toDownloads() {
        if (downloaded != null || archive == null) {
            return downloaded;
        }
        try {
            ContentValues v = new ContentValues();
            v.put(MediaStore.MediaColumns.DISPLAY_NAME, archive.getName());
            v.put(MediaStore.MediaColumns.MIME_TYPE, "application/zip");
            v.put(MediaStore.MediaColumns.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS);
            Uri u = getContentResolver().insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, v);
            try (InputStream in = new FileInputStream(archive);
                 OutputStream out = getContentResolver().openOutputStream(u)) {
                byte[] buf = new byte[65536];
                int n;
                while ((n = in.read(buf)) > 0) {
                    out.write(buf, 0, n);
                }
            }
            downloaded = u;
        } catch (Exception e) {
            Log.e(GameActivity.TAG, "bug report to Downloads: " + e);
            text.setText(text.getText() + "\n\nThe copy to Downloads failed: " + e);
        }
        return downloaded;
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (isFinishing()) {
            // Its own process: gone with the page, so the next report (and
            // nothing else) starts from a clean one.
            android.os.Process.killProcess(android.os.Process.myPid());
        }
    }
}
