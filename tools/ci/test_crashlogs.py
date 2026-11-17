"""Exercise the real Java ZIP exporter against a small fake Android service.

This tests archive/session contracts, not Android framework/device behavior.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
STUBS={
 'android/app/Application.java':'package android.app; public class Application extends android.content.Context {public void onCreate(){}}',
 'android/app/ActivityManager.java':'''package android.app; public class ActivityManager {
 public java.util.List<ApplicationExitInfo> getHistoricalProcessExitReasons(String p,int id,int n){return java.util.Arrays.asList(new ApplicationExitInfo(1,139),new ApplicationExitInfo(10,0));}}''',
 'android/app/ApplicationExitInfo.java':'''package android.app; public class ApplicationExitInfo {
 int reason,status; ApplicationExitInfo(int r,int s){reason=r;status=s;}
 public long getTimestamp(){return 1234;} public int getPid(){return 42;} public String getProcessName(){return "test:engine";}
 public int getReason(){return reason;} public int getStatus(){return status;} public String getDescription(){return "test";}
 public long getPss(){return 100;}public long getRss(){return 200;}
 public java.io.InputStream getTraceInputStream() throws java.io.IOException {return null;}}''',
 'android/content/Context.java':'''package android.content; public class Context {
 public java.io.File root,apk;public static final String ACTIVITY_SERVICE="activity";public static final int MODE_PRIVATE=0;
 public java.io.File getFilesDir(){return root;}public String getPackageName(){return "test.package";}
 public android.content.pm.PackageManager getPackageManager(){return new android.content.pm.PackageManager();}
 public android.content.pm.ApplicationInfo getApplicationInfo(){android.content.pm.ApplicationInfo info=new android.content.pm.ApplicationInfo();info.sourceDir=apk.toString();return info;}
 public Object getSystemService(String name){return new android.app.ActivityManager();}
 private SharedPreferences prefs=new SharedPreferences();public SharedPreferences getSharedPreferences(String n,int m){return prefs;}}''',
 'android/content/SharedPreferences.java':'''package android.content;public class SharedPreferences {
 String value="";public String getString(String k,String d){return value;}public Editor edit(){return new Editor();}
 public class Editor {public Editor putString(String k,String v){value=v;return this;}public void apply(){}}}''',
 'android/content/pm/PackageManager.java':'''package android.content.pm;public class PackageManager {
 public static class NameNotFoundException extends Exception {}
 public PackageInfo getPackageInfo(String p,int f) throws NameNotFoundException{return new PackageInfo();}}''',
 'android/content/pm/PackageInfo.java':'package android.content.pm;public class PackageInfo {public String versionName="local-test";public int versionCode=35;}',
 'android/content/pm/ApplicationInfo.java':'package android.content.pm;public class ApplicationInfo {public String sourceDir;}',
 'android/os/Build.java':'package android.os;public class Build {public static final String MANUFACTURER="test",MODEL="test";public static final String[] SUPPORTED_ABIS={"arm64-v8a"};public static class VERSION {public static int SDK_INT=36;public static String RELEASE="16";}}',
 'android/os/Process.java':'package android.os;public class Process {public static int myPid(){return 42;}public static int myTid(){return 43;}public static void killProcess(int id){}}',
 'android/util/Log.java':'package android.util;public class Log {public static int w(String tag,String text,Throwable error){return 0;}}',
 'org/openbgi/port/GameSettings.java':'package org.openbgi.port;class GameSettings {String encoding="utf8",profile="1.659.8",translationProfile="",fontFamily="";int scale=0;}',
}
HARNESS=r'''package org.openbgi.port;
import java.io.*;import java.nio.file.*;import java.util.*;import java.util.zip.*;import java.nio.charset.StandardCharsets;
public class CrashLogsTest {
 static void check(boolean ok){if(!ok)throw new AssertionError();}
 static String text(byte[] b){return new String(b,StandardCharsets.UTF_8);}
 public static void main(String[] args)throws Exception {
  android.content.Context context=new android.content.Context();context.root=new File(args[0]);context.apk=new File(context.root,"fixture.apk");
  try(ZipOutputStream zip=new ZipOutputStream(new FileOutputStream(context.apk))){zip.putNextEntry(new ZipEntry("lib/arm64-v8a/libopenbgi.so"));zip.write(new byte[]{1,2,3});}
  File logs=CrashLogs.directory(context);File crash=new File(logs,"native-crash.txt");Files.write(crash.toPath(),"Native fatal signal 11\n".getBytes(StandardCharsets.UTF_8));
  check(CrashLogs.takeFailureNotice(context)!=null);check(CrashLogs.takeFailureNotice(context)==null);
  File previous=new File(logs,"native-crash-previous.txt");Files.move(crash.toPath(),previous.toPath());check(CrashLogs.takeFailureNotice(context)==null);
  CrashLogs.beginSession(context,new File(context.root,"game-id"),new GameSettings());
  CrashLogs.beginSession(context,new File(context.root,"game-id"),new GameSettings());
  Files.write(new File(logs,"game.sav").toPath(),new byte[]{9});
  try(RandomAccessFile file=new RandomAccessFile(new File(logs,"engine-current.txt"),"rw")){file.setLength(5*1024*1024);}
  ByteArrayOutputStream bytes=new ByteArrayOutputStream();CrashLogs.export(context,bytes);
  Map<String,byte[]> entries=new HashMap<>();
  try(ZipInputStream zip=new ZipInputStream(new ByteArrayInputStream(bytes.toByteArray()))) {
   ZipEntry entry;byte[] buffer=new byte[16384];int n;
   while((entry=zip.getNextEntry())!=null){ByteArrayOutputStream value=new ByteArrayOutputStream();while((n=zip.read(buffer))!=-1)value.write(buffer,0,n);entries.put(entry.getName(),value.toByteArray());}
  }
  check(!entries.containsKey("game.sav"));check(entries.containsKey("native-crash-previous.txt"));
  String session=text(entries.get("session-current.txt"));check(session.contains("Encoding: utf8"));check(session.contains("Engine selection: 1.659.8"));
  check(session.contains("039058c6f2c0cb492c533b0a4d14ef77cc0f78abccced5287d84a1a2011cfb81"));check(entries.containsKey("session-previous.txt"));
  check(entries.get("engine-current.txt").length==4*1024*1024);check(text(entries.get("export-notes.txt")).contains("truncated at 4 MiB"));
  check(text(entries.get("export-notes.txt")).contains("no system trace available"));
  String exit=text(entries.get("exit-1.txt"));check(exit.contains("SELF_EXIT"));check(exit.contains("Status: 139"));check(exit.contains("Process: test:engine"));check(exit.contains("PID: 42"));
  check(text(entries.get("exit-2.txt")).contains("SYSTEM_FORCE_STOP"));
  boolean failed=false;try{CrashLogs.export(context,new OutputStream(){public void write(int value)throws IOException{throw new IOException("full");}});}catch(IOException expected){failed=true;}check(failed);
  System.out.println("Crash ZIP/session/retention contracts passed");
 }
}'''

class CrashLogsArchive(unittest.TestCase):
    def test_real_exporter(self):
        with tempfile.TemporaryDirectory() as directory:
            base=Path(directory);classes=base/'classes';classes.mkdir();sources=[]
            for name,text in {**STUBS,'org/openbgi/port/CrashLogsTest.java':HARNESS}.items():
                file=base/name;file.parent.mkdir(parents=True,exist_ok=True);file.write_text(text,encoding='utf-8');sources.append(str(file))
            sources.append(str(ROOT/'tools/android/CrashLogs.java'))
            subprocess.run(['javac','--release','8','-encoding','UTF-8','-d',str(classes),*sources],check=True,timeout=40)
            data=base/'data';data.mkdir()
            result=subprocess.run(['java','-Xmx96m','-cp',str(classes),'org.openbgi.port.CrashLogsTest',str(data)],capture_output=True,text=True,timeout=20)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('contracts passed',result.stdout)

if __name__=='__main__':unittest.main()
