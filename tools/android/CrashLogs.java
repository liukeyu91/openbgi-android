package org.openbgi.port;

import android.app.Application;
import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.os.Build;
import android.content.Context;
import java.io.*;
import java.util.zip.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.security.*;
import java.util.*;
import android.os.Process;

public final class CrashLogs extends Application {
    private static volatile long sessionEpoch;
    static File directory(Context context) {
        File dir = new File(context.getFilesDir(), "logs");
        dir.mkdirs();
        return dir;
    }
    /** A launch that stopped before the session began: the exported bundle has
     *  no other trace, because the engine log is opened only once native code
     *  runs. The reason is the same text the player saw as a toast. */
    static void recordStartFailure(Context context, File game, String reason) {
        if(reason == null || reason.isEmpty()) return;
        try {
            File file = new File(directory(context), "start-failure.txt");
            String text = "Time: " + System.currentTimeMillis() + "\n"
                + identity(context)
                + "Game: " + (game == null ? "unknown" : game.getName()) + "\n"
                + "Reason: " + reason.replace('\n', ' ') + "\n";
            Files.write(file.toPath(), text.getBytes(StandardCharsets.UTF_8),
                StandardOpenOption.CREATE, StandardOpenOption.TRUNCATE_EXISTING);
        } catch (IOException | RuntimeException ignored) {}
    }
    @Override public void onCreate() {
        super.onCreate();
        File dir = directory(this);
        Thread.UncaughtExceptionHandler previous = Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler((thread, error) -> {
            try {
                Path current=new File(dir,"java-crash.txt").toPath();
                if(Files.exists(current))Files.move(current,new File(dir,"java-crash-previous.txt").toPath(),StandardCopyOption.REPLACE_EXISTING);
            } catch(IOException ignored) {}
            try (PrintWriter out = new PrintWriter(new File(dir, "java-crash.txt"))) {
                out.println(identity(this));
                if(sessionEpoch!=0)out.println("Session epoch ms: "+sessionEpoch);
                out.println("PID: "+Process.myPid()+" Native TID: "+Process.myTid());
                out.println(new java.util.Date());
                out.println("Thread: " + thread.getName());
                error.printStackTrace(out);
            } catch (Exception ignored) {}
            if (previous != null) previous.uncaughtException(thread, error);
            else { android.os.Process.killProcess(android.os.Process.myPid()); System.exit(1); }
        });
    }
    private static String identity(Context context) {
        try {
            android.content.pm.PackageInfo pkg=context.getPackageManager().getPackageInfo(context.getPackageName(),0);
            return "Package: "+context.getPackageName()+"\nOpenBGI: "+pkg.versionName+" ("+pkg.versionCode+")\n";
        } catch(Exception unavailable) { return "Package: "+context.getPackageName()+"\n"; }
    }
    private static String nativeHash(Context context,String library) {
        try(ZipFile apk=new ZipFile(context.getApplicationInfo().sourceDir)) {
            ZipEntry entry=apk.getEntry("lib/arm64-v8a/"+library);
            if(entry==null)return "unavailable";
            MessageDigest hash=MessageDigest.getInstance("SHA-256");
            try(InputStream in=apk.getInputStream(entry)) {
                byte[] buffer=new byte[16384];int n;
                while((n=in.read(buffer))!=-1)hash.update(buffer,0,n);
            }
            StringBuilder out=new StringBuilder();
            for(byte b:hash.digest())out.append(String.format(java.util.Locale.ROOT,"%02x",b&255));
            return out.toString();
        } catch(Exception unavailable) { return "unavailable: "+unavailable.getClass().getSimpleName(); }
    }
    private static String buildInfo(Context context) {
        try (InputStream input=context.getAssets().open("build-info.txt");
             ByteArrayOutputStream output=new ByteArrayOutputStream()) {
            copy(input,output,1024);
            return new String(output.toByteArray(),StandardCharsets.UTF_8);
        } catch(IOException unavailable) { return "Source baseline: unavailable\n"; }
    }
    private static String gameTitle(File game) {
        Path name=game.toPath().resolve(".openbgi/name.txt");
        try {
            if(Files.isRegularFile(name)&&Files.size(name)<=1024)
                return new String(Files.readAllBytes(name),StandardCharsets.UTF_8).trim().replace('\r',' ').replace('\n',' ');
        } catch(IOException ignored) {}
        return game.getName();
    }
    static long beginSession(Context context, File game, File storage, File data, GameSettings settings, boolean upstream) {
        sessionEpoch=System.currentTimeMillis();
        File dir=directory(context);
        String overlayId="none", overlayHash="none", extracted="unavailable";
        try {
            File patch=GamePatch.active(game);
            if(patch!=null){
                overlayId=patch.getName();
                overlayHash=GamePatch.recordedFingerprint(patch);
                if(overlayHash.isEmpty())overlayHash="unavailable";
            }
        }catch(IOException error){overlayHash="unavailable";}
        try{extracted=GamePatch.extractionFingerprint(data,storage);}
        catch(IOException|IllegalArgumentException ignored){}
        String info=identity(context)+buildInfo(context)+"Session epoch ms: "+sessionEpoch+"\nPID: "+Process.myPid()
            +"\nGame entry: "+game.getName()+"\nGame title: "+gameTitle(game)+"\nEncoding: "+settings.encoding+
            "\nEngine selection: "+settings.profile+"\nArchive language: "+(settings.archiveLanguage.isEmpty()?"default":settings.archiveLanguage)
            +"\nOverlay id: "+overlayId+"\nOverlay content SHA256: "+overlayHash+"\nExtracted patch record SHA256: "+extracted
            +"\nTranslation adapter: "+settings.translationProfile+"\nFont override: "+settings.fontFamily
            +"\nScale mode: "+settings.scale+"\nNative SHA256: "+nativeHash(context,"libopenbgi.so")+"\n";
        if(upstream)info+="Upstream SHA256: "+nativeHash(context,"libopenbgi-arm64.so")
            +"\nBridge SHA256: "+nativeHash(context,"libopenbgi_compare.so")+"\n";
        try {
            Path current=new File(dir,"session-current.txt").toPath();
            if(Files.exists(current))Files.move(current,new File(dir,"session-previous.txt").toPath(),StandardCopyOption.REPLACE_EXISTING);
            Path temp=new File(dir,"session.tmp").toPath();
            Files.write(temp,info.getBytes(StandardCharsets.UTF_8));
            Files.move(temp,current,StandardCopyOption.REPLACE_EXISTING,StandardCopyOption.ATOMIC_MOVE);
        } catch(IOException error) { android.util.Log.w("OpenBGI","Could not record session metadata",error); }
        return sessionEpoch;
    }
    static String takeFailureNotice(Context context) {
        File latest=null;
        for(String name:new String[]{"native-crash.txt","native-crash-previous.txt","java-crash.txt","java-crash-previous.txt"}) {
            File file=new File(directory(context),name);
            if(file.isFile() && file.length()>0 && (latest==null || file.lastModified()>latest.lastModified()))latest=file;
        }
        if(latest==null)return null;
        // Renaming current -> previous must not show the same failure again.
        String id=latest.lastModified()+":"+latest.length();
        android.content.SharedPreferences seen=context.getSharedPreferences("crash-reports",Context.MODE_PRIVATE);
        if(id.equals(seen.getString("lastNotice","")))return null;
        seen.edit().putString("lastNotice",id).apply();
        return context.getString(R.string.failure_notice,
            android.text.format.DateFormat.getMediumDateFormat(context).format(latest.lastModified())+" "+
            android.text.format.DateFormat.getTimeFormat(context).format(latest.lastModified()));
    }
    private static String reason(int value) {
        switch(value) {
            case 1:return "SELF_EXIT (nonzero status can be OpenBGI's fatal-signal fallback)";
            case 2:return "SIGNAL";
            case 3:return "LOW_MEMORY";
            case 4:return "JAVA_CRASH";
            case 5:return "NATIVE_CRASH";
            case 6:return "ANR";
            case 10:return "USER_REQUESTED / SYSTEM_FORCE_STOP";
            default:return "OTHER";
        }
    }
    private static void entry(ZipOutputStream zip,String name,String value) throws IOException {
        zip.putNextEntry(new ZipEntry(name));zip.write(value.getBytes(StandardCharsets.UTF_8));zip.closeEntry();
    }
    static void export(Context context, OutputStream target) throws IOException {
        StringBuilder notes=new StringBuilder();
        try (ZipOutputStream zip = new ZipOutputStream(target)) {
            String device="Report schema: 3\nExport epoch ms: "+System.currentTimeMillis()+"\n"+identity(context)
                +"Android: "+Build.VERSION.RELEASE+" API "+Build.VERSION.SDK_INT+"\nDevice: "+Build.MANUFACTURER+" "+Build.MODEL
                +"\nABIs: "+java.util.Arrays.toString(Build.SUPPORTED_ABIS)+"\n";
            entry(zip,"device.txt",device);
            String summary;
            try {
                File logs=directory(context);
                Map<String,String> session=RunReport.fields(new File(logs,"session-current.txt"));
                String id=session.getOrDefault("Game entry","");
                if(id.matches("[a-z0-9-]{1,79}") && "unavailable".equals(session.get("Overlay content SHA256"))){
                    try {
                        File patch=GamePatch.active(new File(context.getFilesDir(),"games/"+id));
                        if(patch!=null && patch.getName().equals(session.get("Overlay id")))
                            session.put("Overlay content SHA256",GamePatch.fingerprint(patch));
                    }catch(IOException error){notes.append("Overlay hash unavailable\n");}
                }
                summary=RunReport.render(logs,session);
            }catch(IOException error){summary="Game session: unavailable\n";notes.append("Run summary unavailable\n");}
            entry(zip,"run-report.txt",device+buildInfo(context)+"\n"+summary);
            entry(zip,"READ-ME.txt","Native crashes: native-crash*.txt contains signal, fault address, registers, caller candidates, VM activity and maps.\n"
                +"Run-report.txt summarizes the latest session. A VM program is the observed script location. A player-facing scene label may be unavailable.\n"
                +"A VM breadcrumb is the last observed instruction, not proof of the faulting thread. Match native SHA256 before symbolizing.\n"
                +"Frame-pointer walking and /proc access are best effort; a complete native backtrace/tombstone is not guaranteed.\n"
                +"Native fallback exits use status 128+signal (139=SIGSEGV, 134=SIGABRT). Android may label these SELF_EXIT.\n"
                +"Previous session records may be older than the current APK. Session epoch/PID in each report identifies it.\n"
                +"No save files or game resources are exported. Logs can contain error messages and file/device names.\n");
            String[] names={"native-crash.txt","native-crash-previous.txt","session-current.txt","session-previous.txt",
                "java-crash.txt","java-crash-previous.txt","engine-current.txt","engine-previous.txt","start-failure.txt"};
            for(String name:names) {
                File file=new File(directory(context),name);
                if(!file.isFile())continue;
                try(InputStream in=new FileInputStream(file)) {
                    zip.putNextEntry(new ZipEntry(name));
                    if(copy(in,zip,4*1024*1024))notes.append(name).append(": truncated at 4 MiB\n");
                    zip.closeEntry();
                } catch(FileNotFoundException removed) { notes.append(name).append(": disappeared during export\n"); }
            }
            if (Build.VERSION.SDK_INT >= 30) {
                ActivityManager manager = (ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE);
                int index = 0;
                try {
                    for (ApplicationExitInfo exit : manager.getHistoricalProcessExitReasons(context.getPackageName(), 0, 10)) {
                        String prefix = "exit-" + (++index);
                        entry(zip,prefix+".txt","Time: "+exit.getTimestamp()+"\nPID: "+exit.getPid()+"\nProcess: "+exit.getProcessName()
                            +"\nReason: "+exit.getReason()+" "+reason(exit.getReason())+"\nStatus: "+exit.getStatus()
                            +"\nPSS KiB: "+exit.getPss()+"\nRSS KiB: "+exit.getRss()+"\nDescription: "+exit.getDescription()+"\n");
                        InputStream trace;
                        try { trace=exit.getTraceInputStream(); }
                        catch(IOException|SecurityException unavailable) {
                            notes.append(prefix).append(": trace unavailable: ").append(unavailable).append('\n');continue;
                        }
                        if(trace==null)notes.append(prefix).append(": no system trace available\n");
                        else try(InputStream input=trace) {
                            zip.putNextEntry(new ZipEntry(prefix+"-trace.bin"));
                            if(copy(input,zip,8*1024*1024))notes.append(prefix).append(" trace: truncated at 8 MiB\n");
                            zip.closeEntry();
                        }
                    }
                } catch(RuntimeException unavailable) { notes.append("Process exit history unavailable: ").append(unavailable).append('\n'); }
            }
            entry(zip,"export-notes.txt",notes.length()==0?"No export warnings.\n":notes.toString());
        }
    }
    private static boolean copy(InputStream in, OutputStream out, int limit) throws IOException {
        byte[] buffer = new byte[16384]; int n;
        while(limit>0 && (n=in.read(buffer,0,Math.min(buffer.length,limit)))!=-1) { out.write(buffer,0,n);limit-=n; }
        return limit==0 && in.read()!=-1;
    }
}
