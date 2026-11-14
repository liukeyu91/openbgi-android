/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.focusprobe;
import android.app.*;
import android.content.Intent;
import android.media.*;
import android.os.*;
import java.io.*;

/** Opt-in device test helper. Requests real system focus without covering the game. */
public final class FocusProbeService extends Service {
    private AudioManager audio;
    private AudioFocusRequest request;
    private void record(String text) {
        try(FileWriter out = new FileWriter(new File(getFilesDir(), "focus.txt"), true)) { out.write(text + "\n"); }
        catch(IOException error) { throw new RuntimeException(error); }
    }
    @Override public void onCreate() {
        super.onCreate(); audio = (AudioManager)getSystemService(AUDIO_SERVICE);
        NotificationManager manager = (NotificationManager)getSystemService(NOTIFICATION_SERVICE);
        manager.createNotificationChannel(new NotificationChannel("probe", "Focus probe", NotificationManager.IMPORTANCE_LOW));
        startForeground(1, new Notification.Builder(this, "probe").setSmallIcon(android.R.drawable.ic_media_play)
            .setContentTitle("OpenBGI audio focus test").setContentText("Stop the test service to release focus").build());
    }
    @Override public int onStartCommand(Intent intent, int flags, int id) {
        if(request != null) audio.abandonAudioFocusRequest(request);
        int gain = intent == null ? AudioManager.AUDIOFOCUS_GAIN_TRANSIENT : intent.getIntExtra("gain", 2);
        request = new AudioFocusRequest.Builder(gain).setAudioAttributes(new AudioAttributes.Builder()
            .setUsage(AudioAttributes.USAGE_MEDIA).setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
            .setOnAudioFocusChangeListener(change -> record("change " + change)).build();
        record("request " + gain + " result " + audio.requestAudioFocus(request));
        return START_NOT_STICKY;
    }
    @Override public void onDestroy() {
        if(request != null) audio.abandonAudioFocusRequest(request);
        stopForeground(true); super.onDestroy();
    }
    @Override public IBinder onBind(Intent intent) { return null; }
}
