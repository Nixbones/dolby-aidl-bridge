package com.dolby.atmos;

import android.content.Context;
import android.webkit.JavascriptInterface;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.InputStreamReader;
import java.util.concurrent.TimeUnit;

/**
 * Мост в JavaScript (window.Android).
 *
 * Управляет аудиомостом libdolbyaidlshim.so через два файла:
 *   /data/vendor/dolby/dolby_bypass     - "1" = обработка выключена (прозрачный звук)
 *   /data/vendor/dolby/dolby_params.txt - настройки: строки вида "beon 1", "beb 160"
 *
 * Пишем напрямую (папка 0777 - работает, пока SELinux permissive), а если
 * не вышло - через root (su). После записи через root ставим chmod 666,
 * чтобы следующие переключения шли без запроса root.
 */
public class Bridge {

    private static final String DIR = "/data/vendor/dolby";
    private static final String FLAG = DIR + "/dolby_bypass";
    private static final String FLAG_TMP = "/data/local/tmp/dolby_bypass";
    private static final String PARAMS = DIR + "/dolby_params.txt";
    private static final String LOG = "/data/local/tmp/dolby_shim.log";
    private static final String MARK = "__OK__";

    private final Context ctx;

    public Bridge(Context context) {
        this.ctx = context;
    }

    // ---------- запуск команд ----------

    private static String exec(String[] cmd, boolean root) {
        Process p = null;
        try {
            p = Runtime.getRuntime().exec(cmd);
            final Process fp = p;
            // страховка от «зависшего» запроса root (KernelSU ждёт подтверждения)
            Thread killer = new Thread(() -> {
                try {
                    Thread.sleep(6000);
                    fp.destroy();
                } catch (Exception ignored) {
                }
            });
            killer.setDaemon(true);
            killer.start();

            StringBuilder sb = new StringBuilder();
            BufferedReader r = new BufferedReader(new InputStreamReader(p.getInputStream()));
            String line;
            while ((line = r.readLine()) != null) {
                sb.append(line).append('\n');
            }
            p.waitFor(6, TimeUnit.SECONDS);
            return sb.toString();
        } catch (Exception e) {
            return null;
        } finally {
            if (p != null) p.destroy();
        }
    }

    private static String su(String script) {
        return exec(new String[] { "su", "-c", script + " && echo " + MARK }, true);
    }

    private static boolean ok(String out) {
        return out != null && out.contains(MARK);
    }

    // ---------- файлы ----------

    private static String readFile(String path) {
        try {
            File f = new File(path);
            if (!f.exists() || !f.canRead()) return null;
            BufferedReader r = new BufferedReader(new FileReader(f));
            StringBuilder sb = new StringBuilder();
            String line;
            while ((line = r.readLine()) != null) sb.append(line).append('\n');
            r.close();
            return sb.toString();
        } catch (Exception e) {
            return null;
        }
    }

    /** записать файл: сначала напрямую, потом через root */
    private boolean writeSmart(String path, String content) {
        try {
            FileOutputStream o = new FileOutputStream(path);
            o.write(content.getBytes("UTF-8"));
            o.close();
            return true;
        } catch (Exception ignored) {
        }
        File t;
        try {
            t = new File(ctx.getCacheDir(), "tmp_" + new File(path).getName());
            FileOutputStream o = new FileOutputStream(t);
            o.write(content.getBytes("UTF-8"));
            o.close();
        } catch (Exception e) {
            return false;
        }
        String s = "mkdir -p " + DIR + "; cp -f '" + t.getAbsolutePath() + "' " + path
                + "; chmod 666 " + path;
        return ok(su(s));
    }

    private boolean deleteSmart(String path) {
        try {
            File f = new File(path);
            if (!f.exists() || f.delete()) return !f.exists();
        } catch (Exception ignored) {
        }
        return ok(su("rm -f " + path));
    }

    // ---------- состояние ----------

    /** "direct" | "su" - как работает управление (для показа в интерфейсе) */
    @JavascriptInterface
    public String mode() {
        if (writeSmart(DIR + "/.probe", "1")) {
            deleteSmart(DIR + "/.probe");
            return "direct";
        }
        String r = su("id");
        if (r != null && r.contains("uid=0")) return "su";
        return "none";
    }

    @JavascriptInterface
    public boolean isBypassed() {
        String s = readFile(FLAG);
        if (s == null) s = readFile(FLAG_TMP);
        if (s == null) s = su("cat " + FLAG + " 2>/dev/null; cat " + FLAG_TMP + " 2>/dev/null");
        if (s == null) return false;
        return s.trim().startsWith("1");
    }

    @JavascriptInterface
    public boolean setBypassed(boolean bypassed) {
        if (bypassed) {
            writeSmart(FLAG, "1\n");
            writeSmart(FLAG_TMP, "1\n");
        } else {
            deleteSmart(FLAG);
            deleteSmart(FLAG_TMP);
        }
        return isBypassed() == bypassed;
    }

    // ---------- настройки (профили/ползунки) ----------

    @JavascriptInterface
    public boolean setParams(String text) {
        String body = (text == null ? "" : text);
        boolean a = writeSmart(PARAMS, body);
        writeSmart("/data/local/tmp/dolby_params.txt", body);
        return a;
    }

    @JavascriptInterface
    public String getParams() {
        String s = readFile(PARAMS);
        if (s == null) s = su("cat " + PARAMS + " 2>/dev/null");
        return s == null ? "" : s;
    }

    /** хвост лога аудиомоста - для диагностики прямо в приложении */
    @JavascriptInterface
    public String logTail() {
        String s = su("tail -30 " + LOG + " 2>/dev/null");
        if (s == null || s.trim().isEmpty()) s = readFile(LOG);
        return s == null ? "" : s;
    }

    @JavascriptInterface
    public String info() {
        return "Dolby Atmos bridge v3 | режим: " + mode();
    }
}
