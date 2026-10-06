package com.dolby.atmos;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;

/**
 * Уведомление с кнопкой переключения - быстрый доступ к тумблеру
 * (альтернатива плитке в шторке).
 */
public class Notify {

    private static final String CHANNEL = "dolby_atmos";
    private static final int ID = 1001;

    static void ensureChannel(Context c) {
        NotificationManager nm = c.getSystemService(NotificationManager.class);
        if (nm == null) return;
        if (nm.getNotificationChannel(CHANNEL) == null) {
            NotificationChannel ch = new NotificationChannel(CHANNEL, "Dolby Atmos",
                    NotificationManager.IMPORTANCE_LOW);
            ch.setDescription("Быстрое переключение обработки");
            nm.createNotificationChannel(ch);
        }
    }

    private static PendingIntent action(Context c, String act, int code) {
        Intent i = new Intent(c, ToggleReceiver.class).setAction(act);
        return PendingIntent.getBroadcast(c, code, i,
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
    }

    static void update(Context c) {
        NotificationManager nm = c.getSystemService(NotificationManager.class);
        if (nm == null) return;
        ensureChannel(c);

        boolean on = !new Bridge(c).isBypassed();
        Intent open = new Intent(c, MainActivity.class);
        PendingIntent pi = PendingIntent.getActivity(c, 0, open,
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);

        Notification.Builder b = new Notification.Builder(c, CHANNEL)
                .setSmallIcon(android.R.drawable.ic_media_play)
                .setContentTitle("Dolby Atmos")
                .setContentText(on ? "обработка включена" : "обработка выключена")
                .setContentIntent(pi)
                .setShowWhen(false)
                .setOnlyAlertOnce(true)
                .addAction(new Notification.Action.Builder(null, on ? "Выключить" : "Включить",
                        action(c, ToggleReceiver.TOGGLE, 1)).build());
        try {
            nm.notify(ID, b.build());
        } catch (SecurityException ignored) {
            // нет разрешения на уведомления - плитка всё равно работает
        }
    }
}
