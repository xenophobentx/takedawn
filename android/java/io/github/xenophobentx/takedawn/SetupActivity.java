package io.github.xenophobentx.takedawn;

import android.app.Activity;
import android.content.Intent;
import android.database.Cursor;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.util.Log;
import android.util.TypedValue;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.util.HashMap;
import java.util.Locale;
import java.util.Map;

/**
 * The first start: the game's data from the player's own disc image.
 *
 * The app holds the programs and none of the game's files. This page asks for
 * an image of the player's Burnout 3: Takedown disc (North America, NTSC-U, or
 * one of the two European, PAL, discs: the app has a program for each,
 * src/game/package.h) with the system's file picker, which needs no storage
 * permission, and hands the open file to the installer in the native library
 * (the toolkit's disc_install.c through install.c), which tells the disc by
 * its executable: every file is checked against that disc's manifest while it
 * is copied to
 *
 *   <files>/Burnout 3 Takedown/      and at the end    <files>/.complete
 *
 * The game folder of a disc that was extracted (the picker's "Choose game
 * folder"; ACTION_OPEN_DOCUMENT_TREE, BO-223) goes through the same
 * installer: a content:// tree has no file names, so the installer asks
 * openSource() below for each file by its path on the disc and reads it from
 * the descriptor that comes back. Same manifest check, same journal, same
 * messages; nothing is staged, so no second copy of the data is made.
 *
 * An install that was stopped goes on where it was when the same image (or
 * folder) is chosen again. A game folder copied there by hand (the way before this
 * page) is checked where it lies; nothing is copied then.
 *
 * In a process of its own (android:process=":setup"), as the bug report: the
 * native library is loaded here for the installer alone, and the game starts
 * in a fresh process afterwards. The library is the installed disc's
 * (StartActivity.library), else libmain.so; after another disc's install the
 * page opens again in a fresh process with that disc's library (through
 * RestartActivity), so that the menu pictures' offer is that disc's.
 */
public class SetupActivity extends Activity {
    private static final int PICK = 1;
    private static final int PICK_TREE = 2;
    /** Open with the data installed to choose another disc (DriverActivity's "Game data"). */
    static final String EXTRA_ANOTHER = "io.github.xenophobentx.takedawn.ANOTHER_DISC";
    private LinearLayout box;
    private TextView status, detail;
    private ProgressBar bar;
    private Button pick, pickTree, cancel, start, driver, upscale;
    private TextView upscaleNote;
    private volatile boolean running, upscaling;
    private boolean upscaleOffered;
    private String root;
    private String loaded = "main";     // the native library this process loaded
    // nativeAbout: the title, the version line, then a line per disc of the package:
    // "<build>\t<manifest id>\t<disc>\t<files>\t<bytes>\t<bytes wanted free>".
    private String[] about = { "Burnout 3: Takedown", "",
            "ntsc-u-retail\t-\tNorth America (NTSC-U) retail\t716\t2476107072\t3000000000" };

    private static native int nativeState(String root);
    private static native String nativeAbout();
    private static native void nativeCancel();
    private native String nativeInstall(int fd, String folder, String root);
    private native String nativeInstallTree(String root);
    // The menu pictures (BO-326, upscale.c): made here once after the install, if the player wants.
    private native String nativeUpscale(String xbe);
    private static native void nativeUpscaleCancel();
    private static native String nativeUpscaleState();
    private static native void nativeUpscaleDismiss();

    // The picked document tree, read by the installer through openSource().
    private Uri tree;
    private boolean fromTree;
    /** The size of the file openSource() returned a descriptor of (read by the native side). */
    @SuppressWarnings("unused")
    private volatile long srcSize;
    private String treeBase;
    private final Map<String, Map<String, Entry>> listings = new HashMap<>();

    private static final class Entry {
        String id;
        boolean dir;
        long size;
    }

    /** Called by the installer, on its thread. */
    @SuppressWarnings("unused")
    private void onProgress(final double fraction, final String file) {
        runOnUiThread(new Runnable() {
            @Override public void run() {
                bar.setProgress((int) (fraction * 1000));
                detail.setText(String.format("%.0f %%   %s", fraction * 100, file));
            }
        });
    }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        root = GameActivity.filesDir(this).getAbsolutePath();
        ScrollView sv = new ScrollView(this);
        sv.setBackgroundColor(Color.BLACK);
        box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(64, 40, 64, 40);
        sv.addView(box);
        setContentView(sv);

        text("Takedawn: the game's data", 22, Color.WHITE);
        status = text("", 15, 0xFFD0D0D0);
        bar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        bar.setMax(1000);
        bar.setVisibility(View.GONE);
        box.addView(bar);
        detail = text("", 13, 0xFF909090);
        pick = button("Choose your Burnout 3: Takedown disc image", new View.OnClickListener() {
            @Override public void onClick(View v) {
                Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                i.addCategory(Intent.CATEGORY_OPENABLE);
                i.setType("*/*");
                try {
                    startActivityForResult(i, PICK);
                } catch (Exception e) {
                    status.setText("This device has no file picker (" + e.getMessage() + ").\n\n"
                            + "Copy the unpacked disc (the folder with default.xbe in it) to\n"
                            + root + "/" + GameActivity.GAME_DIR + "\nand open the app again:"
                            + " the files are then checked where they are.");
                }
            }
        });
        pickTree = button("Choose game folder\u2026 (a disc already extracted to a folder)",
                new View.OnClickListener() {
            @Override public void onClick(View v) {
                Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
                try {
                    startActivityForResult(i, PICK_TREE);
                } catch (Exception e) {
                    status.setText("This device cannot choose a folder (" + e.getMessage() + ").\n\n"
                            + "Choose the disc image instead, or copy the folder with default.xbe"
                            + " in it to\n" + root + "/" + GameActivity.GAME_DIR
                            + "\nand open the app again: the files are then checked where they are.");
                }
            }
        });
        cancel = button("Cancel", new View.OnClickListener() {
            @Override public void onClick(View v) {
                if (upscaling) {
                    nativeUpscaleCancel();
                    detail.setText("Stopping after the picture in hand…");
                    return;
                }
                nativeCancel();
                detail.setText("Stopping after the file in hand…");
            }
        });
        start = button("Start the game", new View.OnClickListener() {
            @Override public void onClick(View v) {
                // Seen and not used: the offer is not made here again (the
                // settings' MODS page keeps it).
                if (upscaleOffered) {
                    try {
                        nativeUpscaleDismiss();
                    } catch (Throwable e) {
                        Log.w(GameActivity.TAG, "setup: " + e);
                    }
                }
                startActivity(new Intent(SetupActivity.this, StartActivity.class));
                finish();
            }
        });
        upscale = button("Make sharper menu pictures (AI, a few minutes)", new View.OnClickListener() {
            @Override public void onClick(View v) {
                runUpscale();
            }
        });
        upscale.setVisibility(View.GONE);
        upscaleNote = text("Optional: an AI upscaler makes the loading screens and menu pictures four"
                + " times sharper, here on this device from your disc (5 to 15 minutes, about"
                + " 400 MB). It can also be done later, or removed, on the MODS page of the"
                + " game's settings.", 13, 0xFF909090);
        upscaleNote.setVisibility(View.GONE);
        driver = button("", new View.OnClickListener() {
            @Override public void onClick(View v) {
                startActivity(new Intent(SetupActivity.this, DriverActivity.class));
            }
        });
        text("The graphics driver is chosen automatically.", 13, 0xFF909090);
        text("Nothing of the game is in this app: its files come from your own disc. Make an"
                + " image of it (an .iso from the Xbox or a PC drive that reads Xbox discs)"
                + " and put the file on this device or its SD card, or choose the folder the"
                + " disc was extracted to (the one with default.xbe in it). The image or"
                + " folder is only read.",
                13, 0xFF909090);
        button("Licences", new View.OnClickListener() {
            @Override public void onClick(View v) {
                startActivity(new Intent(SetupActivity.this, LicensesActivity.class));
            }
        });
        int st;
        try {
            System.loadLibrary("SDL2");
            loaded = StartActivity.library(this);
            System.loadLibrary(loaded);
            about = nativeAbout().split("\n", -1);
            st = nativeState(root);
        } catch (Throwable e) {
            Log.e(GameActivity.TAG, "setup: " + e);
            show(false, false, false);
            status.setText("The program's library could not be loaded (" + e + ").");
            return;
        }
        handle(st);
        // From adb, for a test or a device without a file picker:
        //   am start -n <package>/.SetupActivity --es image <an image under the app's files folder>
        String image = getIntent().getStringExtra("image");
        if (image != null && st != 1 && st != 3) {
            try {
                File f = new File(image).getCanonicalFile();
                if (!f.getPath().startsWith(new File(root).getCanonicalPath() + "/")) {
                    throw new Exception("only a file under " + root + " is read this way");
                }
                run(ParcelFileDescriptor.open(f, ParcelFileDescriptor.MODE_READ_ONLY), null);
            } catch (Exception e) {
                status.setText(image + " could not be opened: " + e.getMessage());
            }
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (driver != null) {
            driver.setText("Graphics driver: " + GpuDriver.describe(this, GpuDriver.choice(this))
                    + "   (change\u2026)");
        }
    }

    /** The package's discs nativeAbout named: { build, manifest id, disc, files, bytes, free }. */
    private String[][] discs() {
        int n = Math.max(0, about.length - 2);
        String[][] d = new String[n][];
        for (int i = 0; i < n; i++) {
            d[i] = about[i + 2].split("\t", -1);
        }
        return d;
    }

    /** The disc of the package with this build id, or null. */
    private String[] disc(String build) {
        for (String[] d : discs()) {
            if (d.length >= 6 && d[0].equals(build)) {
                return d;
            }
        }
        return null;
    }

    private void handle(int st) {
        String[][] ds = discs();
        int at = StartActivity.installedDisc(new File(root));
        String[] here = at >= 0 ? disc(BuildInfo.BUILDS[at]) : null;
        if (st == 1) {
            // From the driver page's "Game data" button: another disc may be chosen here.
            boolean another = getIntent().getBooleanExtra(EXTRA_ANOTHER, false);
            show(another, false, true);
            status.setText("The game's data is installed and checked ("
                    + (here != null ? here[2] + ", " + here[3] + " files" : "the disc's files") + ")."
                    + (another ? "\n\nTo play another disc (North America, or one of the two European"
                        + " ones), choose its image or folder: its data replaces this one's here,"
                        + " and the game starts that disc from then on. Each disc keeps its own"
                        + " saves and settings (the two European discs share theirs)." : ""));
            offerUpscale();
        } else if (st == 3) {
            status.setText("There are game files in the app's folder from an earlier copy. They"
                    + " are checked now, where they are (a minute or two); nothing is copied.");
            run(null, new File(root, GameActivity.GAME_DIR).getAbsolutePath());
        } else {
            boolean begun = new File(root, "install.journal").isFile();
            show(true, false, false);
            StringBuilder which = new StringBuilder();
            long free = 0;
            for (String[] d : ds) {
                if (d.length < 6) {
                    continue;
                }
                which.append("\n   ").append(d[2]).append(": ").append(d[3]).append(" files, ")
                        .append(String.format(Locale.ROOT, "%.2f", Long.parseLong(d[4]) / 1e9)).append(" GB");
                free = Math.max(free, Long.parseLong(d[5]));
            }
            status.setText((st == 2 ? "The data here was installed for another build of the"
                        + " game and has to be installed again.\n\n" : "")
                    + (begun ? "An install was begun and not finished. Choose the same image"
                        + " or folder again: the files already copied are kept.\n\n" : "")
                    + "The game needs the files of " + about[0] + ", from one of these retail"
                    + " discs:" + which + "\nand up to "
                    + String.format(Locale.ROOT, "%.1f", free / 1e9) + " GB free here ("
                    + String.format(Locale.ROOT, "%.1f", new File(root).getUsableSpace() / 1e9)
                    + " GB is free now). Installing another disc later replaces the data here;"
                    + " each disc keeps its own saves and settings (the two European discs share"
                    + " theirs).");
            pick.requestFocus();
        }
    }

    private TextView text(String s, int sp, int color) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextColor(color);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        t.setPadding(0, 12, 0, 12);
        box.addView(t);
        return t;
    }

    private Button button(String label, View.OnClickListener l) {
        Button b = new Button(this);
        b.setText(label);
        b.setAllCaps(false);
        b.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16);
        b.setOnClickListener(l);
        box.addView(b);
        return b;
    }

    private void show(boolean p, boolean c, boolean s) {
        pick.setVisibility(p ? View.VISIBLE : View.GONE);
        pickTree.setVisibility(p ? View.VISIBLE : View.GONE);
        cancel.setVisibility(c ? View.VISIBLE : View.GONE);
        start.setVisibility(s ? View.VISIBLE : View.GONE);
        bar.setVisibility(c ? View.VISIBLE : View.GONE);
        if (s) {
            start.requestFocus();
        }
    }

    /** What to do about each of the installer's answers, after its own sentence. */
    private String advice(String code) {
        if (fromTree) {
            String f = adviceFolder(code);
            if (f != null) {
                return f;
            }
        }
        return adviceImage(code);
    }

    /** The same answers for a chosen folder; null where the image's words fit. */
    private static String adviceFolder(String code) {
        switch (code) {
        case "no-xbe":
        case "wrong-game":
            return "Choose the folder of Burnout 3: Takedown (the one with default.xbe in it).";
        case "truncated-image":
        case "damaged-source":
            return "The folder is incomplete or not a clean copy of the disc. Extract the disc"
                    + " again, or copy the folder to the device again, and choose it once"
                    + " more: the files already checked are kept.";
        case "disk-full":
            return "Free some space on the device and choose the folder again: the files"
                    + " already copied are kept.";
        case "source-unreadable":
        case "no-source":
            return "A file of the folder could not be read. If it is on an SD card or a network"
                    + " place, copy the folder to the device and choose it there.";
        case "cancelled":
            return "Stopped. Choose the same folder again to go on: the files already copied"
                    + " are kept.";
        default:
            return null;
        }
    }

    private static String adviceImage(String code) {
        switch (code) {
        case "not-xbox-image":
            return "Choose the image of the game disc itself: one file, about 7 GB (a full"
                    + " disc image) or 2.5 to 3 GB (an .xiso).";
        case "xbox360-disc":
            return "That is an Xbox 360 disc. The game wanted here is the one for the first Xbox.";
        case "no-xbe":
        case "wrong-game":
            return "Choose the image of Burnout 3: Takedown.";
        case "wrong-version":
            return "Takedawn takes three retail discs: North America (NTSC-U) and the two"
                    + " European (PAL) ones, French, German and Italian, and English, Spanish,"
                    + " Dutch and Swedish. The Japanese disc, the demo and patched copies are"
                    + " different programs.";
        case "truncated-image":
        case "damaged-source":
            return "The image is incomplete or not a clean copy of the disc. Make it again,"
                    + " or copy it to the device again, and choose it once more: the files"
                    + " already checked are kept.";
        case "disk-full":
            return "Free some space on the device and choose the image again: the files"
                    + " already copied are kept.";
        case "no-permission":
        case "write-failed":
            return "The app's own folder could not be written. Check the device's storage and"
                    + " try again.";
        case "source-unreadable":
        case "no-source":
            return "The file could not be read. If it is on an SD card or a network place,"
                    + " copy it to the device and choose it there.";
        case "cancelled":
            return "Stopped. Choose the same image again to go on: the files already copied"
                    + " are kept.";
        default:
            return "";
        }
    }

    private void run(final ParcelFileDescriptor pfd, final String folder) {
        run(pfd, folder, false);
    }

    private void run(final ParcelFileDescriptor pfd, final String folder, final boolean asTree) {
        running = true;
        fromTree = asTree;
        show(false, true, false);
        bar.setProgress(0);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        new Thread(new Runnable() {
            @Override public void run() {
                String res;
                try {
                    res = asTree ? nativeInstallTree(root)
                            : nativeInstall(pfd != null ? pfd.getFd() : -1, folder, root);
                } catch (Throwable e) {
                    res = "internal\n" + e + "\n0 0 0 0\n-";
                }
                try {
                    if (pfd != null) {
                        pfd.close();
                    }
                } catch (Exception e) {
                    // read only: nothing to lose
                }
                final String[] r = res.split("\n", -1);
                Log.i(GameActivity.TAG, "setup: " + r[0] + ": " + (r.length > 1 ? r[1] : "")
                        + " (" + (r.length > 2 ? r[2] : "") + ")");
                runOnUiThread(new Runnable() {
                    @Override public void run() {
                        running = false;
                        getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
                        detail.setText("");
                        if ("ok".equals(r[0])) {
                            String build = r.length > 3 ? r[3] : "-";
                            String[] d = disc(build);
                            String lib = BuildInfo.LIBRARIES[0];
                            for (int i = 0; i < BuildInfo.BUILDS.length; i++) {
                                if (BuildInfo.BUILDS[i].equals(build)) {
                                    lib = BuildInfo.LIBRARIES[i];
                                }
                            }
                            if (!lib.equals(loaded)) {
                                // Another disc's program: the page again in a fresh
                                // process, with that disc's library (its saves, its
                                // menu pictures).
                                Log.i(GameActivity.TAG, "setup: " + build + " installed; reopening with lib"
                                        + lib + ".so");
                                startActivity(new Intent(SetupActivity.this, RestartActivity.class)
                                        .putExtra(RestartActivity.EXTRA_PID, android.os.Process.myPid())
                                        .putExtra(RestartActivity.EXTRA_THEN, "setup"));
                                finish();
                                android.os.Process.killProcess(android.os.Process.myPid());
                                return;
                            }
                            show(false, false, true);
                            status.setText("The game's data is installed and checked ("
                                    + (d != null ? d[2] + ", " + d[3] + " files" : "the disc's files")
                                    + ").");
                            offerUpscale();
                        } else {
                            show(true, false, false);
                            status.setText((r.length > 1 ? r[1] : r[0]) + "\n\n" + advice(r[0]));
                            pick.requestFocus();
                        }
                    }
                });
            }
        }, "install").start();
    }

    /** The menu pictures, offered once: not when they are made or the offer was turned down. */
    private void offerUpscale() {
        String st;
        try {
            st = nativeUpscaleState();
        } catch (Throwable e) {
            Log.w(GameActivity.TAG, "setup: " + e);
            return;
        }
        upscaleOffered = "none".equals(st);
        upscale.setVisibility(upscaleOffered ? View.VISIBLE : View.GONE);
        upscaleNote.setVisibility(upscaleOffered ? View.VISIBLE : View.GONE);
    }

    /**
     * The run in this process (the game is not running), with the install's
     * bar and Cancel; a foreground service (SetupUpscaleService) while it
     * lasts. Stopped, it goes on from there next time (here is no second
     * offer, but the MODS page has it).
     */
    private void runUpscale() {
        upscaling = true;
        upscaleOffered = false;
        upscale.setVisibility(View.GONE);
        upscaleNote.setVisibility(View.GONE);
        show(false, true, false);
        bar.setProgress(0);
        status.setText("Making sharper menu pictures with AI, on this device. The game is"
                + " installed either way.");
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        final Intent service = new Intent(this, SetupUpscaleService.class);
        try {
            startService(service);
        } catch (RuntimeException e) {
            Log.w(GameActivity.TAG, "setup: upscale service: " + e);
        }
        new Thread(new Runnable() {
            @Override public void run() {
                String res;
                try {
                    // The game's folders, as GameActivity names them, and the
                    // model out of the APK (the run reads files; SDL, which
                    // reads assets in the game, is not running here).
                    android.system.Os.setenv("RECOMP_ANDROID_DIR", root, true);
                    android.system.Os.setenv("XDG_DATA_HOME", root, true);
                    android.system.Os.setenv("XDG_CACHE_HOME", getCacheDir().getAbsolutePath(), true);
                    android.system.Os.setenv("HOME", getFilesDir().getAbsolutePath(), true);
                    File engine = new File(getCacheDir(), "upscaler");
                    copyAsset("upscaler/realesr-general-dn030-x4v3.param", new File(engine, "models"));
                    copyAsset("upscaler/realesr-general-dn030-x4v3.bin", new File(engine, "models"));
                    android.system.Os.setenv("TAKEDAWN_UPSCALE_ENGINE", engine.getAbsolutePath(), true);
                    res = nativeUpscale(new File(new File(root, GameActivity.GAME_DIR), "default.xbe")
                            .getAbsolutePath());
                } catch (Throwable e) {
                    res = "internal\n" + e;
                }
                final String[] r = res.split("\n", -1);
                Log.i(GameActivity.TAG, "setup: upscale: " + r[0] + ": " + (r.length > 1 ? r[1] : ""));
                runOnUiThread(new Runnable() {
                    @Override public void run() {
                        upscaling = false;
                        try {
                            stopService(service);
                        } catch (RuntimeException e) {
                            // gone already
                        }
                        getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
                        detail.setText("");
                        show(false, false, true);
                        status.setText("ok".equals(r[0])
                                ? "The game's data is installed and checked, and the menu pictures"
                                  + " are upscaled (on the MODS page of the settings they can be"
                                  + " turned off or removed)."
                                : "cancelled".equals(r[0])
                                ? "The menu pictures were stopped; the game is installed. The MODS"
                                  + " page of the settings goes on from where they stopped."
                                : "The menu pictures could not be made (" + (r.length > 1 ? r[1] : r[0])
                                  + "); the game is installed. The MODS page of the settings can"
                                  + " try again.");
                    }
                });
            }
        }, "upscale").start();
    }

    /** An asset of the APK into a folder, unless it is there with the same size. */
    private void copyAsset(String name, File dir) throws java.io.IOException {
        File out = new File(dir, new File(name).getName());
        if (!dir.isDirectory() && !dir.mkdirs()) {
            throw new java.io.IOException("cannot make " + dir);
        }
        try (java.io.InputStream in = getAssets().open(name)) {
            long want = -1;
            try (android.content.res.AssetFileDescriptor fd = getAssets().openFd(name)) {
                want = fd.getLength();
            } catch (java.io.IOException compressed) {
                // a compressed asset has no descriptor: copied every time
            }
            if (want >= 0 && out.length() == want) {
                return;
            }
            File part = new File(dir, out.getName() + ".part");
            try (java.io.OutputStream o = new java.io.FileOutputStream(part)) {
                byte[] buf = new byte[1 << 16];
                int n;
                while ((n = in.read(buf)) > 0) {
                    o.write(buf, 0, n);
                }
            }
            if (!part.renameTo(out)) {
                throw new java.io.IOException("cannot write " + out);
            }
        }
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request == PICK_TREE) {
            if (result == RESULT_OK && data != null && data.getData() != null) {
                tree = data.getData();
                treeBase = null;
                listings.clear();
                status.setText("Checking the folder and copying the game's data. The device may"
                        + " be left alone; the screen stays on.");
                run(null, null, true);
            }
            return;
        }
        if (request != PICK || result != RESULT_OK || data == null || data.getData() == null) {
            return;
        }
        Uri u = data.getData();
        try {
            ParcelFileDescriptor pfd = getContentResolver().openFileDescriptor(u, "r");
            if (pfd == null) {
                throw new Exception("no file behind it");
            }
            status.setText("Checking the disc and copying the game's data. The device may be"
                    + " left alone; the screen stays on.");
            run(pfd, null);
        } catch (Exception e) {
            status.setText("The file could not be opened: " + e.getMessage() + "\n\n"
                    + advice("source-unreadable"));
        }
    }

    /** The children of a directory of the tree, by lower-case name; cached. */
    private Map<String, Entry> list(String docId) {
        Map<String, Entry> m = listings.get(docId);
        if (m != null) {
            return m;
        }
        m = new HashMap<>();
        Uri kids = DocumentsContract.buildChildDocumentsUriUsingTree(tree, docId);
        Cursor q = null;
        try {
            q = getContentResolver().query(kids, new String[] {
                    DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                    DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                    DocumentsContract.Document.COLUMN_MIME_TYPE,
                    DocumentsContract.Document.COLUMN_SIZE }, null, null, null);
            while (q != null && q.moveToNext()) {
                Entry e = new Entry();
                e.id = q.getString(0);
                e.dir = DocumentsContract.Document.MIME_TYPE_DIR.equals(q.getString(2));
                e.size = q.isNull(3) ? -1 : q.getLong(3);
                String name = q.getString(1);
                if (name != null) {
                    m.put(name.toLowerCase(Locale.ROOT), e);
                }
            }
        } catch (Exception e) {
            Log.w(GameActivity.TAG, "setup: listing " + docId + ": " + e);
        } finally {
            if (q != null) {
                q.close();
            }
        }
        listings.put(docId, m);
        return m;
    }

    /**
     * The directory of the tree that holds default.xbe: the tree's own, or one
     * level below it (a disc extracted into a folder of its own name).
     */
    private String base() {
        if (treeBase == null) {
            String rootId = DocumentsContract.getTreeDocumentId(tree);
            treeBase = rootId;
            if (!list(rootId).containsKey("default.xbe")) {
                for (Entry e : list(rootId).values()) {
                    if (e.dir && list(e.id).containsKey("default.xbe")) {
                        treeBase = e.id;
                        break;
                    }
                }
            }
        }
        return treeBase;
    }

    /**
     * Called by the installer, on its thread: the file at `rel` (as on the
     * disc, forward slashes; any case) as a descriptor to read, whose size is
     * left in srcSize. -1: the tree has no such file. -2: it has and cannot be
     * read.
     */
    @SuppressWarnings("unused")
    private int openSource(String rel) {
        Entry e = null;
        String dir = base();
        String[] parts = rel.split("/");
        for (int i = 0; i < parts.length; i++) {
            e = list(dir).get(parts[i].toLowerCase(Locale.ROOT));
            if (e == null || (e.dir != (i < parts.length - 1))) {
                return -1;
            }
            dir = e.id;
        }
        if (e == null) {
            return -1;
        }
        try {
            ParcelFileDescriptor pfd = getContentResolver().openFileDescriptor(
                    DocumentsContract.buildDocumentUriUsingTree(tree, e.id), "r");
            if (pfd == null) {
                return -2;
            }
            long size = pfd.getStatSize();
            srcSize = size >= 0 ? size : e.size;
            return pfd.detachFd();
        } catch (Exception ex) {
            Log.w(GameActivity.TAG, "setup: " + rel + ": " + ex);
            return -2;
        }
    }

    @Override
    public void onBackPressed() {
        if (upscaling) {
            nativeUpscaleCancel();
            detail.setText("Stopping after the picture in hand…");
        } else if (running) {
            nativeCancel();
            detail.setText("Stopping after the file in hand…");
        } else {
            super.onBackPressed();
        }
    }
}
