/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;

import android.app.Activity;
import android.graphics.Color;
import android.text.Editable;
import android.text.InputFilter;
import android.text.TextWatcher;
import android.text.Spannable;
import android.text.Spanned;
import android.text.SpanWatcher;
import android.view.inputmethod.BaseInputConnection;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import android.widget.FrameLayout;
import java.nio.charset.Charset;

/** The script's single edit control, positioned in the same viewport as the canvas. */
final class TextInput {
    interface Listener { void changed(byte[] text, int revision, boolean ime); void finished(); }
    private final Activity activity;
    private final FrameLayout root;
    private final View surface;
    private final EditText edit;
    private final Listener listener;
    private final int scaleMode;
    private Charset charset = GameTextCodec.charset(0);
    private int revision, limit = 1023, x, y, w = 1, h = 1, canvasW = 1280, canvasH = 720, fontH = 24, fontW = 12;
    private boolean suppress, rejectAscii, imeOpen, dirty, wantKeyboard;
    private InitialTextRepair initial=new InitialTextRepair(null);
    private final SpanWatcher composition = new SpanWatcher() {
        public void onSpanAdded(Spannable s, Object span, int start, int end) {}
        public void onSpanChanged(Spannable s, Object span, int os, int oe, int ns, int ne) {}
        public void onSpanRemoved(Spannable s, Object span, int start, int end) {
            if(!suppress && span != this) edit.post(() -> publish());
        }
    };

    TextInput(Activity activity, FrameLayout root, View surface, int scaleMode, Listener listener) {
        this.activity = activity; this.root = root; this.surface = surface; this.listener = listener; this.scaleMode = scaleMode;
        edit = new EditText(activity);
        edit.setSingleLine(true); edit.setPadding(0, 0, 0, 0); edit.setIncludeFontPadding(false);
        edit.setBackgroundColor(Color.TRANSPARENT); edit.setTextColor(Color.BLACK);
        edit.setGravity(Gravity.CENTER); edit.setSelectAllOnFocus(true);
        edit.setInputType(android.text.InputType.TYPE_CLASS_TEXT | android.text.InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        edit.setImeOptions(EditorInfo.IME_ACTION_DONE | EditorInfo.IME_FLAG_NO_EXTRACT_UI);
        edit.setVisibility(View.GONE);
        root.addView(edit, new FrameLayout.LayoutParams(1, 1));
        InitialTextRepairDialog.attach(edit,()->edit.isShown() && initial.hasNonAscii(),this::repairInitial);
        edit.setFilters(new InputFilter[]{(source, start, end, dest, dstart, dend) -> {
            if(suppress) return null;
            // Pinyin may exceed the final encoded byte limit. Preserve IME spans
            // while composing, then clamp and publish only the committed text.
            if(source instanceof Spanned) {
                Spanned spans = (Spanned)source;
                for(Object span : spans.getSpans(start, end, Object.class))
                    if((spans.getSpanFlags(span) & Spanned.SPAN_COMPOSING) != 0) return null;
            }
            String accepted = GameTextCodec.insertion(source, start, end, dest, dstart, dend, charset, limit, rejectAscii);
            return accepted.contentEquals(source.subSequence(start, end)) ? null : accepted;
        }});
        edit.addTextChangedListener(new TextWatcher() {
            public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
            public void onTextChanged(CharSequence s, int start, int before, int count) {}
            public void afterTextChanged(Editable s) {
                if(s.getSpanStart(composition) < 0) s.setSpan(composition, 0, s.length(), Spanned.SPAN_INCLUSIVE_INCLUSIVE);
                if(!suppress) { dirty = true; publish(); }
            }
        });
        edit.setOnEditorActionListener((v, action, event) -> {
            if(action == EditorInfo.IME_ACTION_DONE || (event != null && event.getKeyCode() == KeyEvent.KEYCODE_ENTER)) {
                focus(false); edit.selectAll(); listener.finished(); return true;
            }
            return false;
        });
        edit.setOnKeyListener((v, key, event) -> {
            if(key == KeyEvent.KEYCODE_ENTER || key == KeyEvent.KEYCODE_TAB) {
                // Consume DOWN too: TextView may otherwise invoke the editor
                // action on DOWN, then this listener submits again on UP.
                if(event.getAction() == KeyEvent.ACTION_UP) {
                    focus(false); edit.selectAll(); listener.finished();
                }
                return true;
            }
            return false;
        });
        root.addOnLayoutChangeListener((v, l, t, r, b, ol, ot, or, ob) -> layout());
    }
    private void publish() {
        if(suppress || !dirty || BaseInputConnection.getComposingSpanStart(edit.getText()) >= 0) return;
        String current = edit.getText().toString();
        String valid = GameTextCodec.insertion(current, 0, current.length(), "", 0, 0, charset, limit, rejectAscii);
        if(!valid.equals(current)) {
            suppress = true; edit.setText(valid); edit.setSelection(edit.length()); suppress = false;
        }
        listener.changed(valid.getBytes(charset), revision, imeOpen);
        dirty = false;
    }
    void commit() { edit.clearComposingText(); publish(); }
    void windowFocusGained() {
        if(wantKeyboard && edit.hasFocus()) edit.post(() -> {
            if(wantKeyboard && edit.hasFocus()) ((InputMethodManager)activity.getSystemService(Activity.INPUT_METHOD_SERVICE)).showSoftInput(edit, InputMethodManager.SHOW_IMPLICIT);
        });
    }
    private void imeState(boolean open) {
        if(imeOpen != open) { imeOpen = open; listener.changed(null, revision, open); }
    }
    boolean hasFocus() { return edit.hasFocus(); }
    private void repairInitial(){
        final InitialTextRepair snapshot=initial;
        final int atRevision=revision,atLimit=limit;
        final boolean atReject=rejectAscii;
        final Charset target=charset;
        final String before=edit.getText().toString();
        InitialTextRepairDialog.show(activity,snapshot,target,atLimit,atReject,
            ()->edit.isShown() && initial==snapshot && revision==atRevision && limit==atLimit && rejectAscii==atReject &&
                charset.equals(target) && edit.getText().toString().equals(before),
            value->{edit.clearComposingText();edit.setText(value);edit.setSelection(edit.length());});
    }
    private void focus(boolean focus) {
        InputMethodManager manager = (InputMethodManager)activity.getSystemService(Activity.INPUT_METHOD_SERVICE);
        wantKeyboard = focus && edit.getVisibility() == View.VISIBLE;
        if(wantKeyboard) {
            // B0 23 reports the edit session, including keyboard startup and
            // temporary popup/background occlusion, not keyboard pixel height.
            imeState(true);
            edit.requestFocus();
            edit.post(() -> { if(edit.hasFocus()) manager.showSoftInput(edit, InputMethodManager.SHOW_IMPLICIT); });
        } else {
            commit();
            manager.hideSoftInputFromWindow(edit.getWindowToken(), 0);
            edit.clearFocus(); surface.requestFocus(); imeState(false);
        }
    }
    private void layout() {
        if(canvasW <= 0 || canvasH <= 0 || surface.getWidth() == 0 || surface.getHeight() == 0) return;
        Viewport v=new Viewport(surface.getWidth(),surface.getHeight(),canvasW,canvasH,scaleMode);
        int vw=v.w,vh=v.h;if(vw<=0 || vh<=0)return;
        FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(Math.max(1, w * vw / canvasW), Math.max(1, h * vh / canvasH));
        params.leftMargin = v.x + x * vw / canvasW;
        params.topMargin = v.y + y * vh / canvasH;
        edit.setLayoutParams(params);
        edit.setTextSize(TypedValue.COMPLEX_UNIT_PX, Math.max(1, Math.abs(fontH) * (float)vh / canvasH));
        edit.setTextScaleX(fontH == 0 ? 1 : Math.max(.1f, Math.abs(2f * fontW / fontH) * ((float)vw / canvasW) / ((float)vh / canvasH)));
    }
    // Keep op numbers aligned with android_internal.h.
    void apply(int op, int a, int b, int c, int d, int nextRevision, int encoding, byte[] text) {
        switch(op) {
            case 0: edit.setVisibility(a != 0 ? View.VISIBLE : View.GONE); if(a == 0) { InitialTextRepairDialog.dismissAll(activity);focus(false); } break;
            case 1: x = a; y = b; w = c; h = d; layout(); break;
            case 2: fontH = a; fontW = b; layout(); break;
            case 3: edit.selectAll(); break;
            case 4: focus(a != 0); break;
            case 5: edit.setTextColor(0xff000000 | a); break;
            case 6: edit.invalidate(); break;
            case 7: rejectAscii = a != 0; break;
            case 8: limit = a > 0 ? Math.min(1023, a) : 1023; break;
            case 9:
                InitialTextRepairDialog.dismissAll(activity);initial=new InitialTextRepair(text);
                revision = nextRevision; charset = GameTextCodec.charset(encoding);
                suppress = true; edit.setText(text == null ? "" : new String(text, charset)); suppress = false; dirty = false;
                break;
            case 10:
                InitialTextRepairDialog.dismissAll(activity);initial=new InitialTextRepair(null);
                revision = nextRevision; focus(false); edit.setVisibility(View.GONE);
                suppress = true; edit.setText(""); suppress = false; dirty = false;
                limit = 1023; rejectAscii = false; break;
            case 11: canvasW = a; canvasH = b; layout(); break;
            case 12:
                int alignment = a == 0 ? Gravity.LEFT : a == 2 ? Gravity.RIGHT : Gravity.CENTER_HORIZONTAL;
                edit.setGravity(alignment | Gravity.CENTER_VERTICAL);
                break;
        }
    }
    void pause() { focus(false); }
    void suspend() {
        commit();
        ((InputMethodManager)activity.getSystemService(Activity.INPUT_METHOD_SERVICE))
            .hideSoftInputFromWindow(edit.getWindowToken(),0);
    }
}
