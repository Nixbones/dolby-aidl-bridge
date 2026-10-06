package com.dolby.atmos;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

/** Обработчик кнопок уведомления и внешних запросов на переключение. */
public class ToggleReceiver extends BroadcastReceiver {

    public static final String TOGGLE = "com.dolby.atmos.TOGGLE";
    public static final String SET_ON = "com.dolby.atmos.SET_ON";
    public static final String SET_OFF = "com.dolby.atmos.SET_OFF";

    @Override
    public void onReceive(Context context, Intent intent) {
        String a = intent.getAction();
        if (a == null) return;
        Bridge b = new Bridge(context);
        if (TOGGLE.equals(a)) {
            b.setBypassed(!b.isBypassed());
        } else if (SET_ON.equals(a)) {
            b.setBypassed(false);
        } else if (SET_OFF.equals(a)) {
            b.setBypassed(true);
        }
        Notify.update(context);
    }
}
