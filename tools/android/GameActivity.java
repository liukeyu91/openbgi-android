/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;

import android.app.Activity;
import android.app.AlertDialog;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Button;
import android.widget.HorizontalScrollView;
import android.hardware.input.InputManager;
import android.view.InputDevice;
import android.view.ViewConfiguration;
import android.view.Gravity;
import android.view.Choreographer;
import android.media.AudioManager;
import java.nio.charset.StandardCharsets;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.Process;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowManager;
import android.widget.Toast;
import java.io.File;

/** A single session in :engine. Android UI never calls into the VM directly. */
public final class GameActivity extends Activity implements SurfaceHolder.Callback, InputManager.InputDeviceListener {
    static { System.loadLibrary("openbgi"); }
    static final String UPSTREAM = "upstream";
    private boolean ready, resumed, focused, surfaceReady, framePosted;
    private SurfaceView surface;
    private GameSettings settings;
    private GameDataLock saveLock,patchLock;
    private TouchInput touch;
    private InputKeys keys;
    private InputManager inputManager;
    private int canvasW=1280,canvasH=720,mouseButtons;
    private GameAudioFocus audioFocus;
    private LinearLayout interruption;
    private TextView interruptionText;
    private Button skipButton;
    private TextInput textInput;
    private AlertDialog messageDialog;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private native void nativeInputResult(int serial, byte[][] values, int month, int day);
    private native void nativeDialogResult(int serial, int result);
    private native void nativeEditChanged(byte[] text, int revision, boolean ime);
    private native boolean nativeCreate(String internal, String external, int scaleMode,long sessionEpoch,boolean upstream);
    private native void nativeFontFamily(String family);
    private native void nativeSurface(Surface surface);
    private native void nativeVsync();
    private native int nativeAudioStatus();
    private native int nativeMovieStatus();
    private boolean audioWarningShown;
    private boolean movieWarningShown;
    private final Runnable audioCheck = new Runnable() {
        public void run() {
            if(!ready || !resumed)return;
            int status=nativeAudioStatus();
            if(status<0 && !audioWarningShown) {
                audioWarningShown=true;
                Toast.makeText(GameActivity.this,getString(R.string.audio_output_error),Toast.LENGTH_LONG).show();
            } else if(status>0)audioWarningShown=false;
            int movie=nativeMovieStatus();
            if(movie<0&&!movieWarningShown){movieWarningShown=true;Toast.makeText(GameActivity.this,getString(R.string.movie_skipped),Toast.LENGTH_LONG).show();}
            else if(movie>=0)movieWarningShown=false;
            ui.postDelayed(this,1000);
        }
    };
    private final Choreographer.FrameCallback frames = time -> onFrame();
    private boolean rendering() { return ready && resumed && surfaceReady && audioFocus!=null && audioFocus.running(); }
    private void onFrame() {
        framePosted=false;
        if(rendering()) { nativeVsync(); updateFrames(); }
    }
    private void updateFrames() {
        if(rendering() && !framePosted) { framePosted=true;Choreographer.getInstance().postFrameCallback(frames); }
        else if(!rendering() && framePosted) { framePosted=false;Choreographer.getInstance().removeFrameCallback(frames); }
    }
    private native void nativeState(boolean resumed, boolean focused);
    private native void nativeDestroy();
    private native void nativeKey(int action, int key, int repeat);
    private native void nativePointer(int action,int button,float x,float y,float wheel,int width,int height);

    @Override protected void attachBaseContext(android.content.Context base){super.attachBaseContext(AppLanguage.wrap(base));}
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_PAN);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN | WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        File game=null,storage=null,data=null;
        try {
            game=GameSettings.activeGame(getFilesDir());
            storage=DirectGame.resources(game);
            if(game.getName().equals("external")){
                File base=getExternalFilesDir(null);if(base==null)throw new java.io.IOException(getString(R.string.game_folder_unavailable));storage=new File(base,"game");
            }
            if(DirectGame.linked(game)||ContainerGame.linked(game))saveLock=GameDataLock.acquire(game);
            else {
                if(!game.equals(storage))patchLock=GameDataLock.acquire(game);
                saveLock=GameDataLock.acquire(storage);
            }
            // The container is read-only: the writable layer lives beside the entry.
            if(ContainerGame.linked(game)){
                data=ContainerGame.data(game);
                if(data==null)data=ContainerGame.data(game,null);
            } else data=DirectGame.data(game,storage);
            SaveRestore.recover(data);settings=TranslationRuntime.prepare(game,storage);
        }
        catch(java.io.IOException error) {
            CrashLogs.recordStartFailure(this,game,error.getMessage());
            Toast.makeText(this,error.getMessage(),Toast.LENGTH_LONG).show();engineFinished();return;
        }
        if(!settings.encodingReady()) {
            CrashLogs.recordStartFailure(this,game,getString(R.string.encoding_unset));
            Toast.makeText(this,getString(R.string.encoding_unset),Toast.LENGTH_LONG).show();engineFinished();return;
        }
        boolean upstream=BuildFeatures.DUAL_CORE && getIntent().getBooleanExtra(UPSTREAM,false);
        long sessionEpoch=CrashLogs.beginSession(this,game,storage,data,settings,upstream);
        nativeFontFamily(settings.fontFamily);
        File external = getExternalFilesDir(null);
        ready = nativeCreate(getFilesDir().getAbsolutePath(), external == null ? "" : external.getAbsolutePath(), settings.scale,sessionEpoch,upstream);
        if(!ready) {
            CrashLogs.recordStartFailure(this,game,getString(R.string.engine_start_error));
            Toast.makeText(this, getString(R.string.engine_start_error), Toast.LENGTH_LONG).show();
            engineFinished(); return;
        }
        keys=new InputKeys((action,vk,repeat)->{if(ready)nativeKey(action,vk,repeat);});
        touch=new TouchInput((action,button,x,y,w,h,wheel)->{if(ready)nativePointer(action,button,x,y,wheel,w,h);},
            (action,ms)->ui.postDelayed(action,ms),settings.touch==1,
            ViewConfiguration.get(this).getScaledTouchSlop(),ViewConfiguration.getLongPressTimeout());
        inputManager=(InputManager)getSystemService(INPUT_SERVICE);
        inputManager.registerInputDeviceListener(this,ui);
        surface = new SurfaceView(this) {
            @Override public boolean onTouchEvent(MotionEvent e) { return handleTouch(e); }
            @Override public boolean onGenericMotionEvent(MotionEvent e) {
                if(ready && e.getActionMasked()==MotionEvent.ACTION_SCROLL) {
                    nativePointer(TouchInput.WHEEL,0,e.getX(),e.getY(),e.getAxisValue(MotionEvent.AXIS_VSCROLL),getWidth(),getHeight());return true;
                }
                if(e.isFromSource(InputDevice.SOURCE_MOUSE) &&
                    (e.getActionMasked()==MotionEvent.ACTION_BUTTON_PRESS || e.getActionMasked()==MotionEvent.ACTION_BUTTON_RELEASE)) {
                    int button=e.getActionButton()==MotionEvent.BUTTON_SECONDARY?1:0;
                    if(ready)nativePointer(e.getActionMasked()==MotionEvent.ACTION_BUTTON_PRESS?TouchInput.DOWN:TouchInput.UP,
                        button,e.getX(),e.getY(),0,getWidth(),getHeight());return true;
                }
                return super.onGenericMotionEvent(e);
            }
        };
        surface.setFocusableInTouchMode(true);
        surface.getHolder().addCallback(this);
        FrameLayout root = new FrameLayout(this);
        root.addView(surface, new FrameLayout.LayoutParams(-1, -1));
        textInput = new TextInput(this, root, surface, settings.scale, new TextInput.Listener() {
            public void changed(byte[] text, int revision, boolean ime) { if(ready) nativeEditChanged(text, revision, ime); }
            public void finished() {
                if(ready) keys.tap(0x0d);
            }
        });
        if(settings.virtualKeys) addVirtualKeys(root);
        interruption = new LinearLayout(this);
        interruption.setOrientation(LinearLayout.VERTICAL); interruption.setGravity(Gravity.CENTER);
        interruption.setBackgroundColor(0xcc000000); interruption.setClickable(true);
        interruptionText = new TextView(this); interruptionText.setTextColor(0xffffffff); interruptionText.setTextSize(18); interruptionText.setGravity(Gravity.CENTER);
        interruption.addView(interruptionText, new LinearLayout.LayoutParams(-2, -2));
        Button resumeGame = new Button(this); resumeGame.setText(getString(R.string.resume_game));
        resumeGame.setOnClickListener(v -> { if(audioFocus != null) audioFocus.retry(); });
        interruption.addView(resumeGame, new LinearLayout.LayoutParams(-2, -2)); interruption.setVisibility(View.GONE);
        root.addView(interruption, new FrameLayout.LayoutParams(-1, -1));
        audioFocus = new GameAudioFocus(this, this::stateChanged);
        setVolumeControlStream(AudioManager.STREAM_MUSIC);
        setContentView(root);
        surface.requestFocus();
        immersive();
    }
    private boolean handleTouch(MotionEvent e) {
        if(!ready || !resumed || !focused || audioFocus==null || !audioFocus.running())return true;
        int action=e.getActionMasked();
        if(action==MotionEvent.ACTION_DOWN)stopFastForward();
        if(action==MotionEvent.ACTION_DOWN && textInput!=null)textInput.commit();
        int w=surface.getWidth(),h=surface.getHeight();
        if(e.isFromSource(InputDevice.SOURCE_MOUSE)) {
            if(action==MotionEvent.ACTION_DOWN)mouseButtons=e.getButtonState();
            int button=(mouseButtons&MotionEvent.BUTTON_SECONDARY)!=0?1:0;
            nativePointer(action==MotionEvent.ACTION_DOWN?TouchInput.DOWN:action==MotionEvent.ACTION_UP?TouchInput.UP:
                action==MotionEvent.ACTION_CANCEL?TouchInput.CANCEL:TouchInput.MOVE,button,e.getX(),e.getY(),0,w,h);
            return true;
        }
        if(action==MotionEvent.ACTION_DOWN)touch.down(e.getPointerId(0),e.getX(),e.getY(),w,h,
            new Viewport(w,h,canvasW,canvasH,settings.scale).contains(e.getX(),e.getY()));
        else if(action==MotionEvent.ACTION_POINTER_DOWN)touch.secondPointer();
        else if(action==MotionEvent.ACTION_MOVE){int i=e.findPointerIndex(touch.pointerId());if(i<0)touch.cancel();else touch.move(e.getX(i),e.getY(i));}
        else if(action==MotionEvent.ACTION_UP || (action==MotionEvent.ACTION_POINTER_UP &&
            (touch.twoFingers() || e.getPointerId(e.getActionIndex())==touch.pointerId()))) {
            int i=e.findPointerIndex(touch.pointerId());if(i<0)i=e.getActionIndex();touch.up(e.getX(i),e.getY(i));
        } else if(action==MotionEvent.ACTION_CANCEL)touch.cancel();
        return true;
    }
    private void cancelInput() {
        if(touch!=null)touch.cancel();if(keys!=null)keys.clear();mouseButtons=0;
        updateSkipButton();
        if(ready && surface!=null)nativePointer(TouchInput.CANCEL,0,0,0,0,surface.getWidth(),surface.getHeight());
    }
    private void shortcut(int vk) {
        if(!ready || !resumed || audioFocus==null || !audioFocus.running())return;
        stopFastForward();
        if(textInput!=null)textInput.pause();
        if(vk!=0)keys.tap(vk);
        else nativePointer(TouchInput.WHEEL,0,surface.getWidth()/2f,surface.getHeight()/2f,1,surface.getWidth(),surface.getHeight());
    }
    private void updateSkipButton(){
        if(skipButton!=null){boolean on=keys!=null && keys.shortcutHeld();skipButton.setText(on?getString(R.string.stop_skip):getString(R.string.skip));skipButton.setSelected(on);
            skipButton.setBackground(Ui.touch(this,on?0xe091452d:0xe0246f5d,12));}
    }
    private void stopFastForward(){if(keys!=null)keys.releaseShortcut();updateSkipButton();}
    private void toggleFastForward(){
        if(!ready || !resumed || !focused || keys==null || audioFocus==null || !audioFocus.running())return;
        if(textInput!=null)textInput.pause();
        keys.toggleShortcut(settings.skipKey);updateSkipButton();
    }
    private void addVirtualKeys(FrameLayout root) {
        LinearLayout container=Ui.column(this);
        Button toggle=Ui.button(this,getString(R.string.shortcuts),false,()->{});toggle.setTextSize(12);toggle.setTextColor(0xfff5f5f0);
        toggle.setBackground(Ui.touch(this,0xc0202d2a,14));
        container.addView(toggle,new LinearLayout.LayoutParams(-2,Ui.dp(this,48)));
        HorizontalScrollView scroll=new HorizontalScrollView(this);scroll.setVisibility(View.GONE);
        LinearLayout row=Ui.row(this);scroll.addView(row,new FrameLayout.LayoutParams(-2,-2));
        LinearLayout.LayoutParams scrollParams=new LinearLayout.LayoutParams(-2,-2);scrollParams.topMargin=Ui.dp(this,6);container.addView(scroll,scrollParams);
        String[] names={getString(R.string.menu),getString(R.string.backlog),getString(R.string.quick_save),getString(R.string.quick_load)};int[] codes={settings.menuKey,settings.backlogKey,settings.saveKey,settings.loadKey};
        for(int i=0;i<names.length;i++){
            final int code=codes[i];Button button=Ui.button(this,names[i],false,()->shortcut(code));button.setTextSize(12);button.setTextColor(0xfff5f5f0);button.setBackground(Ui.touch(this,0xe0246f5d,12));
            LinearLayout.LayoutParams keyParams=new LinearLayout.LayoutParams(-2,Ui.dp(this,48));keyParams.rightMargin=Ui.dp(this,6);row.addView(button,keyParams);
        }
        skipButton=Ui.button(this,getString(R.string.skip),false,this::toggleFastForward);skipButton.setTextSize(12);skipButton.setTextColor(0xfff5f5f0);
        skipButton.setBackground(Ui.touch(this,0xe0246f5d,12));row.addView(skipButton,new LinearLayout.LayoutParams(-2,Ui.dp(this,48)));
        toggle.setOnClickListener(v->{boolean open=scroll.getVisibility()!=View.VISIBLE;if(!open)stopFastForward();scroll.setVisibility(open?View.VISIBLE:View.GONE);toggle.setText(open?getString(R.string.shortcuts_collapse):getString(R.string.shortcuts));});
        FrameLayout.LayoutParams floating=new FrameLayout.LayoutParams(-2,-2,Gravity.TOP|Gravity.LEFT);floating.setMargins(Ui.dp(this,12),Ui.dp(this,12),Ui.dp(this,12),0);root.addView(container,floating);
    }
    @Override public void onInputDeviceAdded(int deviceId) {}
    @Override public void onInputDeviceChanged(int deviceId) { if(keys!=null)keys.releaseDevice(deviceId); }
    @Override public void onInputDeviceRemoved(int deviceId) { if(keys!=null)keys.releaseDevice(deviceId); }
    @Override public boolean dispatchGenericMotionEvent(MotionEvent e) {
        if(ready && resumed && focused && audioFocus!=null && audioFocus.running() && !textInput.hasFocus() && e.isFromSource(InputDevice.SOURCE_JOYSTICK)) {
            keys.axes(e.getDeviceId(),e.getAxisValue(MotionEvent.AXIS_X),e.getAxisValue(MotionEvent.AXIS_Y),
                e.getAxisValue(MotionEvent.AXIS_HAT_X),e.getAxisValue(MotionEvent.AXIS_HAT_Y),
                Math.max(e.getAxisValue(MotionEvent.AXIS_LTRIGGER),e.getAxisValue(MotionEvent.AXIS_BRAKE)),
                Math.max(e.getAxisValue(MotionEvent.AXIS_RTRIGGER),e.getAxisValue(MotionEvent.AXIS_GAS)));return true;
        }
        return super.dispatchGenericMotionEvent(e);
    }
    private void immersive() {
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN |
            View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
    }
    private void stateChanged() {
        boolean running = resumed && audioFocus != null && audioFocus.running();
        if(!running)InitialTextRepairDialog.dismissAll(this);
        if(!running || !focused)cancelInput();
        if(ready) nativeState(running, focused);
        updateFrames();
        if(interruption != null) {
            boolean paused = resumed && audioFocus != null && !audioFocus.running();
            interruption.setVisibility(paused ? View.VISIBLE : View.GONE);
            if(paused) { interruptionText.setText(audioFocus.message()); if(textInput != null) textInput.suspend(); }
        }
    }
    @Override protected void onResume() { super.onResume(); resumed = true; if(audioFocus != null) audioFocus.resume(); stateChanged(); immersive(); ui.removeCallbacks(audioCheck); if(ready)ui.postDelayed(audioCheck,1000); }
    @Override protected void onPause() {
        resumed=false;ui.removeCallbacks(audioCheck);InitialTextRepairDialog.dismissAll(this);
        if(textInput!=null)textInput.suspend();if(audioFocus!=null)audioFocus.pause();stateChanged();super.onPause();
    }
    @Override public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus); focused = hasFocus; stateChanged(); if(hasFocus) { immersive(); if(textInput != null) textInput.windowFocusGained(); }
    }
    @Override protected void onDestroy() {
        InitialTextRepairDialog.dismissAll(this);
        cancelInput();if(inputManager!=null)inputManager.unregisterInputDeviceListener(this);
        resumed = false; if(audioFocus != null) audioFocus.close();
        surfaceReady=false;updateFrames();
        if(ready) { ready = false; nativeDestroy(); }
        if(saveLock!=null){saveLock.close();saveLock=null;}
        if(patchLock!=null){patchLock.close();patchLock=null;}
        if(messageDialog != null) messageDialog.dismiss();
        ui.removeCallbacksAndMessages(null);
        super.onDestroy();
    }
    @Override public void surfaceCreated(SurfaceHolder holder) { /* Attach once dimensions are known. */ }
    @Override public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        cancelInput();surfaceReady=true;if(ready) nativeSurface(holder.getSurface());updateFrames();
    }
    @Override public void surfaceDestroyed(SurfaceHolder holder) { cancelInput();surfaceReady=false;updateFrames();if(ready) nativeSurface(null); }
    @Override public boolean dispatchKeyEvent(KeyEvent event) {
        if(resumed && audioFocus != null && !audioFocus.running()) return super.dispatchKeyEvent(event);
        if(textInput != null && textInput.hasFocus()) return super.dispatchKeyEvent(event);
        int vk=InputKeys.map(event.getKeyCode(),settings!=null && settings.swapAB);
        if(event.getAction()==KeyEvent.ACTION_DOWN && vk!=0 && (settings==null || vk!=settings.skipKey))stopFastForward();
        if(ready && !focused && vk!=0)return true;
        if(ready && vk!=0 && (event.getAction()==KeyEvent.ACTION_DOWN || event.getAction()==KeyEvent.ACTION_UP)){
            keys.physical(event.getDeviceId(),event.getKeyCode(),vk,event.getAction()==KeyEvent.ACTION_DOWN,event.getRepeatCount());return true;
        }
        return super.dispatchKeyEvent(event);
    }
    @Override public void onBackPressed() {
        if(resumed && audioFocus != null && !audioFocus.running()) return;
        stopFastForward();
        if(textInput != null && textInput.hasFocus()) textInput.pause();
        else if(ready) keys.tap(0x1b);
    }
    /** Worker calls only enqueue UI work, so modal waits never block Android's UI. */
    public void editCommand(int op, int a, int b, int c, int d, int revision, int encoding, byte[] text) {
        ui.post(() -> {
            if(op==11){canvasW=a;canvasH=b;}
            if(op==4 && a!=0)cancelInput();
            if(ready && !isDestroyed() && textInput!=null)textInput.apply(op,a,b,c,d,revision,encoding,text);
        });
    }
    public void showMessageBox(byte[] text, byte[] caption, int flags, int serial) {
        ui.post(() -> {
            int kind = flags & 15;
            int cancel = kind == 4 ? 7 : kind == 0 ? 1 : 2;
            if(!ready || isFinishing() || isDestroyed()) { nativeDialogResult(serial, cancel); return; }
            AlertDialog.Builder builder = new AlertDialog.Builder(this)
                .setTitle(new String(caption, StandardCharsets.UTF_8)).setMessage(new String(text, StandardCharsets.UTF_8));
            if(kind == 4) builder.setPositiveButton(getString(R.string.yes), (d, w) -> nativeDialogResult(serial, 6)).setNegativeButton(getString(R.string.no), (d, w) -> nativeDialogResult(serial, 7));
            else if(kind == 5) builder.setPositiveButton(getString(R.string.retry), (d, w) -> nativeDialogResult(serial, 4)).setNegativeButton(getString(R.string.cancel), (d, w) -> nativeDialogResult(serial, 2));
            else {
                builder.setPositiveButton(getString(R.string.ok), (d, w) -> nativeDialogResult(serial, 1));
                if(kind == 1) builder.setNegativeButton(getString(R.string.cancel), (d, w) -> nativeDialogResult(serial, 2));
            }
            builder.setOnCancelListener(d -> nativeDialogResult(serial, cancel));
            messageDialog = builder.create(); messageDialog.setCanceledOnTouchOutside(false);
            messageDialog.setOnDismissListener(d -> { InitialTextRepairDialog.dismissAll(this);messageDialog = null; immersive(); });
            messageDialog.show();
            if((flags & 256) != 0 && kind != 0) messageDialog.getButton(AlertDialog.BUTTON_NEGATIVE).requestFocus();
        });
    }
    public void showInputDialog(int serial, int encoding, byte[] title, byte[][] labels, byte[][] initial,
            int[] limits, int[] digits, int month, int day, boolean compact) {
        ui.post(() -> {
            if(!ready || isFinishing() || isDestroyed()) { nativeInputResult(serial, null, 0, 0); return; }
            InitialTextRepairDialog.dismissAll(this);
            messageDialog = InputDialogs.show(this, encoding, title, labels, initial, limits, digits, month, day, compact,
                (values, selectedMonth, selectedDay) -> nativeInputResult(serial, values, selectedMonth, selectedDay));
            messageDialog.setOnDismissListener(d -> { InitialTextRepairDialog.dismissAll(this);messageDialog = null; immersive(); });
        });
    }
    /** Called after OS_Shutdown on the worker, including when this Activity was destroyed. */
    public void engineFinished() {
        new Handler(Looper.getMainLooper()).post(() -> {
            finish();
            new Handler(Looper.getMainLooper()).post(() -> Process.killProcess(Process.myPid()));
        });
    }
}
