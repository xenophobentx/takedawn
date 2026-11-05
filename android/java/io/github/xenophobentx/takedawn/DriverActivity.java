package io.github.xenophobentx.takedawn;

import android.app.Activity;
import android.content.Intent;
import android.database.Cursor;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.util.TypedValue;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.InputStream;
import java.util.Map;

/**
 * "Takedawn: graphics driver", a second entry in the launcher: which Vulkan
 * driver the game opens (GpuDriver), chosen before the game starts. The Mesa
 * Turnip driver in the app, the device's own, or a driver package picked with
 * the system's file picker (the .zip with meta.json that the Android
 * emulators' driver pages distribute, or a bare .so). The pad's d-pad and A
 * work the buttons.
 */
public class DriverActivity extends Activity {
    private static final int PICK = 1;
    /** From the start-up warning (GameActivity.pickDriverPackage): the file
     *  picker at once, and the game started again on the package picked. */
    static final String EXTRA_PICK = "io.github.xenophobentx.takedawn.PICK";
    private LinearLayout box;
    private String message;
    private boolean restartAfterPick;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        ScrollView sv = new ScrollView(this);
        sv.setBackgroundColor(Color.BLACK);
        box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(64, 40, 64, 40);
        sv.addView(box);
        setContentView(sv);
        build();
        // From adb, for a test or a device without a file picker (a
        // debuggable build only: the page is exported, as the launcher's
        // second entry, so any app could send these):
        //   am start -n <package>/.DriverActivity --es import <file under the app's files folder>
        //   am start -n <package>/.DriverActivity --es choose auto|bundled|system|custom|remove
        handle(getIntent());
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        message = null;
        build();
        handle(intent);
    }

    @Override
    protected void onResume() {
        super.onResume();
        build();
    }

    private void handle(Intent intent) {
        if (intent.getBooleanExtra(EXTRA_PICK, false)) {
            intent.removeExtra(EXTRA_PICK);
            restartAfterPick = true;
            pick();
            return;
        }
        String imp = intent.getStringExtra("import");
        String ch = intent.getStringExtra("choose");
        if ((imp != null || ch != null) && (getApplicationInfo().flags
                & android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE) == 0) {
            android.util.Log.w(GameActivity.TAG, "driver page: the test extras are for a"
                    + " debuggable build; ignored");
            return;
        }
        if (imp != null) {
            try {
                java.io.File f = new java.io.File(imp).getCanonicalFile();
                String root = GameActivity.filesDir(this).getCanonicalPath() + "/";
                if (!f.getPath().startsWith(root)) {
                    throw new Exception("only a file under " + root + " is read this way");
                }
                take(new java.io.FileInputStream(f), f.getName());
            } catch (Exception e) {
                message = imp + " wasn't loaded: " + e.getMessage() + ". Nothing changed.";
                android.util.Log.e(GameActivity.TAG, "driver package " + imp + ": " + e);
                build();
            }
        } else if ("remove".equals(ch)) {
            GpuDriver.removeCustom(this);
            message = "Driver file removed.";
            build();
        } else if (ch != null && (ch.equals("auto") || ch.equals("bundled") || ch.equals("system")
                || (ch.equals("custom") && !GpuDriver.customMeta(this).isEmpty()))) {
            choose(ch);
        }
    }

    /** Reads a package from a stream (closed here) on a worker thread and shows the result. */
    private void take(final InputStream stream, final String name) {
        message = "Reading " + name + "\u2026";
        build();
        new Thread(new Runnable() {
            @Override public void run() {
                String m;
                boolean ok = false;
                try (InputStream in = stream) {
                    String got = GpuDriver.importPackage(DriverActivity.this, in, name);
                    GpuDriver.setChoice(DriverActivity.this, "custom");
                    m = "Driver file \"" + got + "\" loaded and chosen.";
                    ok = true;
                } catch (Exception e) {
                    m = name + " wasn't loaded: " + e.getMessage() + ". Nothing changed.";
                }
                android.util.Log.i(GameActivity.TAG, "driver package: " + m);
                final String msg = m;
                final boolean restart = ok && restartAfterPick;
                runOnUiThread(new Runnable() {
                    @Override public void run() {
                        message = msg;
                        build();
                        // Picked from the start-up warning: the game behind
                        // this page is on the CPU, so it starts again on the
                        // package at once (on trial: if it shows no frames,
                        // the next start goes back to the driver in the app).
                        if (restart && GameActivity.restartRunning()) {
                            android.util.Log.i(GameActivity.TAG,
                                    "driver package: the game starts again on it");
                            finish();
                        }
                    }
                });
            }
        }).start();
    }

    private void text(String s, int sp, int color) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextColor(color);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        t.setPadding(0, 10, 0, 10);
        box.addView(t);
    }

    private Button button(String label, boolean chosen, View.OnClickListener l) {
        Button b = new Button(this);
        b.setText((chosen ? "●  " : "○  ") + label);
        b.setAllCaps(false);
        b.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16);
        b.setGravity(android.view.Gravity.START | android.view.Gravity.CENTER_VERTICAL);
        b.setOnClickListener(l);
        box.addView(b);
        return b;
    }

    private void choose(String c) {
        GpuDriver.setChoice(this, c);
        message = null;
        build();
    }

    private void build() {
        box.removeAllViews();
        String ch = GpuDriver.choice(this);
        int sys = GpuDriver.systemVulkanVersion(this);
        Map<String, String> custom = GpuDriver.customMeta(this);
        Map<String, String> last = GpuDriver.readKeys(GpuDriver.infoFile(this));
        String note = GpuDriver.prefs(this).getString("note", null);

        text("Graphics driver", 22, Color.WHITE);
        if (!last.isEmpty()) {
            text("Last start: " + last.get("device") + ", Vulkan " + last.get("vulkan")
                    + (last.get("mesa") != null && !last.get("mesa").isEmpty()
                        ? ", Mesa " + last.get("mesa") : "")
                    + " (" + last.get("driver") + ")", 14, 0xFFB0B0B0);
        }
        if (note != null) {
            text(note, 14, 0xFFF0C85A);
        }
        if (message != null) {
            text(message, 14, 0xFFF0C85A);
        }
        if (GpuDriver.BUNDLED.equals(last.get("driver")) && last.get("mesa") != null
                && !last.get("mesa").isEmpty()) {
            GpuDriver.prefs(this).edit().putString("bundledMesa", last.get("mesa")).apply();
        }
        String mesa = GpuDriver.prefs(this).getString("bundledMesa", null);
        boolean adreno = GpuDriver.isAdreno();
        Button first = button("Automatic (recommended): " + GpuDriver.describe(this, "auto"),
                "auto".equals(ch) || (!adreno && !"system".equals(ch)),
                new View.OnClickListener() {
                    @Override public void onClick(View v) { choose("auto"); }
                });
        // Turnip drives Adrenos only: elsewhere the bundled driver and the
        // packages are not offered (GpuDriver never loads them there).
        if (GpuDriver.hasBundled(this) && adreno) {
            button("Built-in Turnip, always" + (mesa != null ? " (Mesa " + mesa + ")" : ""),
                    "bundled".equals(ch), new View.OnClickListener() {
                        @Override public void onClick(View v) { choose("bundled"); }
                    });
        }
        button("This device's driver (Vulkan " + GpuDriver.versionString(sys) + ")"
                        + (sys < 0x403000 ? ": too old, the game runs on the processor" : ""),
                "system".equals(ch), new View.OnClickListener() {
                    @Override public void onClick(View v) { choose("system"); }
                });
        if (!custom.isEmpty() && adreno) {
            button("Driver file: " + custom.get("name")
                            + (custom.get("version").isEmpty() ? "" : ", " + custom.get("version")),
                    "custom".equals(ch), new View.OnClickListener() {
                        @Override public void onClick(View v) { choose("custom"); }
                    });
        }
        text("", 6, Color.BLACK);
        if (adreno) {
            button("Load a driver file (.zip)\u2026", false, new View.OnClickListener() {
                @Override public void onClick(View v) { pick(); }
            }).setText("Load a driver file (.zip)\u2026");
        }
        if (!custom.isEmpty()) {
            button("Remove the driver file", false, new View.OnClickListener() {
                @Override public void onClick(View v) {
                    GpuDriver.removeCustom(DriverActivity.this);
                    message = "Driver file removed.";
                    build();
                }
            }).setText("Remove the driver file");
        }
        button("Start the game", false, new View.OnClickListener() {
            @Override public void onClick(View v) {
                startActivity(new Intent(DriverActivity.this, StartActivity.class));
                finish();
            }
        }).setText("Start the game");
        // The game's data: another disc (src/game/package.h; the setup page takes any of the three).
        button("Game data: install another disc\u2026", false, new View.OnClickListener() {
            @Override public void onClick(View v) {
                startActivity(new Intent(DriverActivity.this, SetupActivity.class)
                        .putExtra(SetupActivity.EXTRA_ANOTHER, true));
            }
        }).setText("Game data: install another disc\u2026");
        button("Licences", false, new View.OnClickListener() {
            @Override public void onClick(View v) {
                startActivity(new Intent(DriverActivity.this, LicensesActivity.class));
            }
        }).setText("Licences");
        // The driver folder (<files>/driver, a developer's way) comes before
        // the bundled driver under Automatic: said, so it is not a surprise.
        java.io.File legacy = GpuDriver.userFile(this);
        if (legacy != null && adreno) {
            text("Under Automatic, the driver in the app's driver folder (" + legacy.getName()
                    + ") comes first. Delete it from that folder to use the built-in driver.",
                    13, 0xFFF0C85A);
            android.util.Log.i(GameActivity.TAG, "driver page: the driver folder's "
                    + legacy.getName() + " comes first under Automatic");
        }
        text(adreno
                ? "If the game doesn't reach its first frames with a driver you picked, or the"
                  + " driver stops working later, the next start goes back to Automatic."
                : "Driver files work on Adreno graphics only. This device uses its own driver.",
                13, 0xFF909090);
        first.requestFocus();
    }

    /** The system's file picker for a driver package (onActivityResult takes it). */
    private void pick() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        try {
            startActivityForResult(i, PICK);
        } catch (Exception e) {
            message = "This device has no file picker (" + e.getMessage()
                    + "). Copy the driver file into "
                    + GpuDriver.userDir(DriverActivity.this).getAbsolutePath()
                    + " instead and choose Automatic.";
            build();
        }
    }

    private String displayName(Uri u) {
        try (Cursor c = getContentResolver().query(u, null, null, null, null)) {
            if (c != null && c.moveToFirst()) {
                int i = c.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (i >= 0 && c.getString(i) != null) {
                    return c.getString(i);
                }
            }
        } catch (Exception e) {
            // the last path segment will do
        }
        String s = u.getLastPathSegment();
        return s != null ? s : "driver";
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != PICK || result != RESULT_OK || data == null || data.getData() == null) {
            if (request == PICK && restartAfterPick) {
                message = "No file picked. Back returns to the game, which runs on the processor.";
                build();
            }
            return;
        }
        final Uri u = data.getData();
        try {
            take(getContentResolver().openInputStream(u), displayName(u));
        } catch (Exception e) {
            message = "The file could not be opened: " + e.getMessage();
            build();
        }
    }
}
