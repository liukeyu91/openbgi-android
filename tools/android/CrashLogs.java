package org.openbgi.port;

import android.app.Application;
import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.os.Build;
import android.content.Context;
import java.io.*;
import java.util.zip.*;

public final class CrashLogs extends Application {
    static File directory(Context context) {
        File dir = new File(context.getFilesDir(), "logs");
        dir.mkdirs();
        return dir;
    }
    @Override public void onCreate() {
        super.onCreate();
        File dir = directory(this);
        Thread.UncaughtExceptionHandler previous = Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler((thread, error) -> {
            try (PrintWriter out = new PrintWriter(new File(dir, "java-crash.txt"))) {
                out.println(new java.util.Date());
                out.println("Thread: " + thread.getName());
                error.printStackTrace(out);
            } catch (Exception ignored) {}
            if (previous != null) previous.uncaughtException(thread, error);
            else { android.os.Process.killProcess(android.os.Process.myPid()); System.exit(1); }
        });
    }
    static void export(Context context, OutputStream target) throws IOException {
        try (ZipOutputStream zip = new ZipOutputStream(target)) {
            zip.putNextEntry(new ZipEntry("device.txt"));
            String info = "OpenBGI " + context.getPackageManager().getPackageInfo(context.getPackageName(), 0).versionName
                + "\nAndroid " + Build.VERSION.RELEASE + " API " + Build.VERSION.SDK_INT
                + "\nDevice " + Build.MANUFACTURER + " " + Build.MODEL + "\n";
            zip.write(info.getBytes("UTF-8")); zip.closeEntry();
            File[] files = directory(context).listFiles();
            if (files != null) for (File file : files) if (file.isFile()) {
                zip.putNextEntry(new ZipEntry(file.getName()));
                try (InputStream in = new FileInputStream(file)) { copy(in, zip); }
                zip.closeEntry();
            }
            if (Build.VERSION.SDK_INT >= 30) {
                ActivityManager manager = (ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE);
                int index = 0;
                for (ApplicationExitInfo exit : manager.getHistoricalProcessExitReasons(context.getPackageName(), 0, 5)) {
                    String prefix = "exit-" + (++index);
                    zip.putNextEntry(new ZipEntry(prefix + ".txt"));
                    zip.write(("Time: " + exit.getTimestamp() + "\nReason: " + exit.getReason()
                        + "\nStatus: " + exit.getStatus() + "\nDescription: " + exit.getDescription()).getBytes("UTF-8"));
                    zip.closeEntry();
                    try (InputStream trace = exit.getTraceInputStream()) {
                        if (trace != null) { zip.putNextEntry(new ZipEntry(prefix + "-trace.bin")); copy(trace, zip); zip.closeEntry(); }
                    } catch (IOException unavailable) { /* System may have removed the trace. */ }
                }
            }
        } catch (android.content.pm.PackageManager.NameNotFoundException error) { throw new IOException(error); }
    }
    private static void copy(InputStream in, OutputStream out) throws IOException {
        byte[] buffer = new byte[16384]; int n;
        while ((n = in.read(buffer)) != -1) out.write(buffer, 0, n);
    }
}
