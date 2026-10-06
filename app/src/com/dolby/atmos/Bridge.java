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
 * Управляет аудиомостом libdolbyaidlshim.so через файлы в /data/vendor/dolby:
 *   dolby_bypass     - "1" = обработка выключена (прозрачный звук)
 *   dolby_params.txt - настройки: строки вида "beon 1", "beb 160", "gebs 20 ..."
 *   dolby_status     - состояние от моста: "on=1;device=0x2;headphone=0;params=32;"
 *   presets/*.txt    - пользовательские пресеты
 *
 * Пишем напрямую (каталог доступен на запись), при неудаче - через root (su).
 */
public class Bridge {

    private static final String DIR = "/data/vendor/dolby";
    static final String FLAG = DIR + "/dolby_bypass";
    private static final String FLAG_TMP = "/data/local/tmp/dolby_bypass";
    private static final String PARAMS = DIR + "/dolby_params.txt";
    private static final String STATUS = DIR + "/dolby_status";
    private static final String PRESETS = DIR + "/presets";
    private static final String LOG = "/data/local/tmp/dolby_shim.log";
    private static final String MARK = "__OK__";

    private final Context ctx;

    public Bridge(Context context) {
        this.ctx = context;
    }

    // ---------- запуск команд ----------

    static String exec(String[] cmd) {
        Process p = null;
        try {
            p = Runtime.getRuntime().exec(cmd);
            final Process fp = p;
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

    static String su(String script) {
        return exec(new String[] { "su", "-c", script + " && echo " + MARK });
    }

    static boolean ok(String out) {
        return out != null && out.contains(MARK);
    }

    // ---------- файлы ----------

    static String readFile(String path) {
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

    /** запись, которую видит и аудио-процесс, и другие версии приложения */
    private static boolean tryWrite(String path, String content) {
        try {
            FileOutputStream o = new FileOutputStream(path);
            o.write(content.getBytes("UTF-8"));
            o.close();
        } catch (Exception e) {
            return false;
        }
        File f = new File(path);
        f.setReadable(true, false);   // читает мост из аудио-процесса
        f.setWritable(true, false);   // и смогут писать будущие версии приложения
        return true;
    }

    /** записать файл: сначала напрямую, потом через root */
    boolean writeSmart(String path, String content) {
        File parent = new File(path).getParentFile();
        if (parent != null && !parent.exists()) parent.mkdirs();
        if (tryWrite(path, content)) return true;

        // файл мог остаться от прежней установки (другой uid) - удаляем и создаём заново
        File old = new File(path);
        if (old.exists() && old.delete() && tryWrite(path, content)) return true;
        File t;
        try {
            t = new File(ctx.getCacheDir(), "tmp_" + new File(path).getName());
            FileOutputStream o = new FileOutputStream(t);
            o.write(content.getBytes("UTF-8"));
            o.close();
        } catch (Exception e) {
            return false;
        }
        String dir = parent != null ? parent.getAbsolutePath() : DIR;
        String s = "mkdir -p '" + dir + "'; cp -f '" + t.getAbsolutePath() + "' '" + path
                + "'; chmod 666 '" + path + "'";
        return ok(su(s));
    }

    private static void makeWorldReadable(String path) {
        // мост читает файл из аудио-процесса: права 666, иначе не увидит
        su("chmod 666 '" + path + "' 2>/dev/null");
    }

    boolean deleteSmart(String path) {
        try {
            File f = new File(path);
            if (!f.exists() || f.delete()) return !f.exists();
        } catch (Exception ignored) {
        }
        return ok(su("rm -f '" + path + "'"));
    }

    // ---------- состояние обработки ----------

    /** "direct" | "su" | "none" - как работает управление (для интерфейса) */
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

    /** текущее устройство вывода по данным системы: "headphone" | "speaker" | "" */
    @JavascriptInterface
    public String currentOutput() {
        try {
            android.media.AudioManager am =
                    (android.media.AudioManager) ctx.getSystemService(Context.AUDIO_SERVICE);
            if (am == null) return "";
            boolean hp = false, spk = false;
            for (android.media.AudioDeviceInfo d
                    : am.getDevices(android.media.AudioManager.GET_DEVICES_OUTPUTS)) {
                switch (d.getType()) {
                    case android.media.AudioDeviceInfo.TYPE_WIRED_HEADPHONES:
                    case android.media.AudioDeviceInfo.TYPE_WIRED_HEADSET:
                    case android.media.AudioDeviceInfo.TYPE_BLUETOOTH_A2DP:
                    case android.media.AudioDeviceInfo.TYPE_BLUETOOTH_SCO:
                    case android.media.AudioDeviceInfo.TYPE_USB_HEADSET:
                    case android.media.AudioDeviceInfo.TYPE_HEARING_AID:
                        hp = true;
                        break;
                    case android.media.AudioDeviceInfo.TYPE_BUILTIN_SPEAKER:
                        spk = true;
                        break;
                    default:
                        break;
                }
            }
            if (hp) return "headphone";
            if (spk) return "speaker";
        } catch (Exception ignored) {
        }
        return "";
    }

    /** подсказка мосту, какое устройство сейчас играет (мост сам его не видит) */
    @JavascriptInterface
    public void setDeviceHint(String device) {
        String v = "headphone".equals(device) ? "headphone" : "speaker";
        writeSmart(DIR + "/dolby_device", v + (char) 10);
    }

    /** состояние от моста: "on=1;device=0x2;headphone=0;params=32;" */
    @JavascriptInterface
    public String status() {
        String s = readFile(STATUS);
        return s == null ? "" : s.trim();
    }

    // ---------- настройки ----------

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

    // ---------- пресеты ----------

    @JavascriptInterface
    public String listPresets() {
        StringBuilder sb = new StringBuilder();
        File dir = new File(PRESETS);
        File[] files = dir.listFiles();
        if (files != null) {
            java.util.Arrays.sort(files, (a, b) -> a.getName().compareToIgnoreCase(b.getName()));
            for (File f : files) {
                String n = f.getName();
                if (n.endsWith(".txt")) sb.append(n.substring(0, n.length() - 4)).append('\n');
            }
        }
        if (sb.length() == 0) {
            String viaSu = su("ls -1 " + PRESETS + " 2>/dev/null");
            if (viaSu != null) {
                for (String line : viaSu.split("\n")) {
                    String t = line.trim();
                    if (t.endsWith(".txt")) sb.append(t.substring(0, t.length() - 4)).append('\n');
                }
            }
        }
        return sb.toString().trim();
    }

    @JavascriptInterface
    public boolean savePreset(String name, String text) {
        String clean = (name == null ? "" : name).replaceAll("[^\\p{L}\\p{N}_ \\-]", "").trim();
        if (clean.isEmpty()) return false;
        return writeSmart(PRESETS + "/" + clean + ".txt", text == null ? "" : text);
    }

    @JavascriptInterface
    public String loadPreset(String name) {
        String clean = (name == null ? "" : name).replaceAll("[^\\p{L}\\p{N}_ \\-]", "").trim();
        if (clean.isEmpty()) return "";
        String s = readFile(PRESETS + "/" + clean + ".txt");
        if (s == null) s = su("cat '" + PRESETS + "/" + clean + ".txt' 2>/dev/null");
        return s == null ? "" : s;
    }

    @JavascriptInterface
    public boolean deletePreset(String name) {
        String clean = (name == null ? "" : name).replaceAll("[^\\p{L}\\p{N}_ \\-]", "").trim();
        if (clean.isEmpty()) return false;
        return deleteSmart(PRESETS + "/" + clean + ".txt");
    }

    // ---------- диагностика ----------

    @JavascriptInterface
    public String logTail() {
        String s = su("tail -25 " + LOG + " 2>/dev/null");
        if (s == null || s.trim().isEmpty()) s = readFile(LOG);
        return s == null ? "" : s;
    }

    @JavascriptInterface
    public String info() {
        return "Dolby Atmos bridge v4 | режим: " + mode();
    }
}
