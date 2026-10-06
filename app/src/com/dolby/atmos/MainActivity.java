package com.dolby.atmos;

import android.app.Activity;
import android.os.Bundle;
import android.webkit.WebSettings;
import android.webkit.WebView;

/**
 * Dolby Atmos — переключатель обработки.
 *
 * Интерфейс — WebView с локальной страницей (assets/dolby.html).
 * Переключение идёт через Bridge (window.Android), который пишет файл-флаг,
 * читаемый аудиомостом libdolbyaidlshim.so.
 */
public class MainActivity extends Activity {

    private WebView web;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        web = new WebView(this);
        web.setBackgroundColor(0xFF0C0A08);

        WebSettings settings = web.getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setAllowFileAccess(true);

        web.addJavascriptInterface(new Bridge(this), "Android");
        web.loadUrl("file:///android_asset/dolby.html");

        setContentView(web);

        // уведомление с кнопкой переключения (нужно разрешение на Android 13+)
        if (android.os.Build.VERSION.SDK_INT >= 33
                && checkSelfPermission("android.permission.POST_NOTIFICATIONS")
                   != android.content.pm.PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[] { "android.permission.POST_NOTIFICATIONS" }, 1);
        }
        Notify.update(this);
    }

    @Override
    protected void onDestroy() {
        if (web != null) {
            web.destroy();
            web = null;
        }
        super.onDestroy();
    }
}
