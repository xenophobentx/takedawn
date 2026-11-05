package io.github.xenophobentx.takedawn;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.os.IBinder;
import android.util.Log;

/**
 * Keeps the app's process going while the menu pictures are made (BO-326,
 * docs/upscale-art.md): the run is a thread of the game or of the setup page,
 * five to fifteen minutes on the CPU, and an app in the background without a
 * foreground service is frozen or ended by the system. The service does
 * nothing itself; it is started (with startService, while the app is in
 * front) when the run begins and stopped when it ends, and ends with the
 * app's task. Its notification says what is going on (shown only if the
 * player allows the app's notifications; the service runs either way).
 *
 * One service for each process the run can be in: this one in the game's,
 * SetupUpscaleService in the setup page's.
 */
public class UpscaleService extends Service {
    private static final String CHANNEL = "upscale";
    private static final int ID = 326;

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        try {
            NotificationManager nm = getSystemService(NotificationManager.class);
            if (nm != null) {
                nm.createNotificationChannel(new NotificationChannel(CHANNEL, "Sharper menu pictures",
                        NotificationManager.IMPORTANCE_LOW));
            }
            Notification n = new Notification.Builder(this, CHANNEL)
                    .setSmallIcon(android.R.drawable.stat_sys_download)
                    .setContentTitle(getString(R.string.app_name))
                    .setContentText("Making sharper menu pictures (AI)")
                    .setStyle(new Notification.BigTextStyle().bigText("Making sharper menu pictures"
                            + " (AI). It goes on with the app in the background or its settings open,"
                            + " and waits while you race. The MODS page of the settings shows how far"
                            + " it is."))
                    .setOngoing(true)
                    .build();
            startForeground(ID, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC);
            Log.i(GameActivity.TAG, "upscale: foreground service on");
        } catch (RuntimeException e) {
            // Without it the run still goes on while the app is in front.
            Log.w(GameActivity.TAG, "upscale: no foreground service: " + e);
        }
        return START_NOT_STICKY;
    }

    @Override
    public void onDestroy() {
        Log.i(GameActivity.TAG, "upscale: foreground service off");
        super.onDestroy();
    }
}
