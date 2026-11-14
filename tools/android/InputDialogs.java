/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;

import android.app.Activity;
import android.app.AlertDialog;
import android.text.InputFilter;
import android.text.Spanned;
import android.text.InputType;
import android.view.View;
import android.view.WindowManager;
import android.view.inputmethod.EditorInfo;
import android.widget.*;
import java.nio.charset.Charset;
import java.util.ArrayList;
import java.util.List;

/** Script dialogs B0 84/85/87/8F. Buffers stay in the game's chosen encoding. */
final class InputDialogs {
    interface Result { void done(byte[][] values, int month, int day); }
    private static boolean composing(CharSequence text, int start, int end) {
        if(text instanceof Spanned) {
            Spanned spans = (Spanned)text;
            for(Object span : spans.getSpans(start, end, Object.class))
                if((spans.getSpanFlags(span) & Spanned.SPAN_COMPOSING) != 0) return true;
        }
        return false;
    }
    private static void numbers(Activity activity, Spinner spinner, int count, int selected) {
        List<String> names = new ArrayList<>();
        for(int i = 1; i <= count; i++) names.add(Integer.toString(i));
        ArrayAdapter<String> adapter = new ArrayAdapter<>(activity, android.R.layout.simple_spinner_item, names);
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        spinner.setAdapter(adapter); spinner.setSelection(selected >= 0 && selected < count ? selected : 0);
    }
    private static boolean fullWidth(String text, Charset charset, int encoding) {
        for(int i = 0; i < text.length();) {
            int cp = text.codePointAt(i); i += Character.charCount(cp);
            if(encoding != 1) {
                if(new String(Character.toChars(cp)).getBytes(charset).length != 2) return false;
            } else if(!((cp >= 0x2e80 && cp <= 0xff60) || (cp >= 0xffe0 && cp <= 0xffe6) || (cp >= 0x20000 && cp <= 0x3134f))) return false;
        }
        return true;
    }
    static AlertDialog show(Activity activity, int encoding, byte[] title, byte[][] labels, byte[][] initial,
            int[] limits, int[] digits, int month, int day, boolean compact, Result result) {
        Charset charset = GameTextCodec.charset(encoding);
        boolean profile = initial.length == 4;
        final AlertDialog[] parentDialog=new AlertDialog[1];
        String[] profileNames = {"姓", "名", "昵称", "第一人称"};
        LinearLayout fields = new LinearLayout(activity); fields.setOrientation(LinearLayout.VERTICAL);
        int padding = (int)(16 * activity.getResources().getDisplayMetrics().density);
        fields.setPadding(padding, 0, padding, 0);
        ScrollView scroll = new ScrollView(activity); scroll.addView(fields);
        LinearLayout row = new LinearLayout(activity); row.setOrientation(compact ? LinearLayout.HORIZONTAL : LinearLayout.VERTICAL);
        fields.addView(row);
        EditText[] edits = new EditText[initial.length];
        int focus = 0;
        boolean foundEmpty = false;
        for(int i = 0; i < initial.length; i++) {
            final int index = i;
            LinearLayout cell = new LinearLayout(activity); cell.setOrientation(LinearLayout.VERTICAL);
            row.addView(cell, new LinearLayout.LayoutParams(compact ? 0 : -1, -2, compact ? 1 : 0));
            String label = profile ? profileNames[i] : new String(labels[i], charset);
            if(!label.isEmpty()) { TextView heading = new TextView(activity); heading.setText(label); cell.addView(heading); }
            EditText edit = new EditText(activity); edits[i] = edit; edit.setSingleLine(true);
            edit.setSelectAllOnFocus(true);
            edit.setPadding(0, 0, 0, 0); edit.setTextSize(16);
            edit.setMinHeight(0); edit.setMinimumHeight((int)(32 * activity.getResources().getDisplayMetrics().density));
            edit.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
            if(digits[i] != 0) edit.setRawInputType(InputType.TYPE_CLASS_NUMBER | InputType.TYPE_NUMBER_FLAG_SIGNED);
            edit.setImeOptions(EditorInfo.IME_FLAG_NO_EXTRACT_UI | (i + 1 == initial.length ? EditorInfo.IME_ACTION_DONE : EditorInfo.IME_ACTION_NEXT));
            String value = new String(initial[i], charset);
            value = GameTextCodec.insertion(value, 0, value.length(), "", 0, 0, charset, limits[i], false);
            edit.setText(value);
            edit.setFilters(new InputFilter[]{(source, start, end, dest, dstart, dend) -> {
                if(composing(source, start, end)) return null;
                if(digits[index] != 0 && end - start == 1) {
                    char c = source.charAt(start);
                    if((c < '0' || c > '9') && !(c == '-' && dest.length() == dend - dstart)) return "";
                }
                String accepted = GameTextCodec.insertion(source, start, end, dest, dstart, dend, charset, limits[index], false);
                return accepted.contentEquals(source.subSequence(start, end)) ? null : accepted;
            }});
            cell.addView(edit);
            final InitialTextRepair snapshot=new InitialTextRepair(initial[i]);
            InitialTextRepairDialog.attach(edit,()->edit.isShown() && digits[index]==0 && snapshot.hasNonAscii(),()->{
                final String before=edit.getText().toString();
                InitialTextRepairDialog.show(activity,snapshot,charset,limits[index],false,
                    ()->parentDialog[0]!=null && parentDialog[0].isShowing() && edit.isShown() && edit.getText().toString().equals(before),
                    replacement->{edit.clearComposingText();edit.setText(replacement);edit.setSelection(edit.length());});
            });
            if(!foundEmpty && value.isEmpty()) { focus = i; foundEmpty = true; }
        }
        final Spinner months = new Spinner(activity), days = new Spinner(activity);
        if(profile) {
            LinearLayout birthday = new LinearLayout(activity); birthday.setOrientation(LinearLayout.HORIZONTAL);
            TextView heading = new TextView(activity); heading.setText("生日（月／日）"); birthday.addView(heading);
            birthday.addView(months); birthday.addView(days); fields.addView(birthday);
            int[] counts = {31,29,31,30,31,30,31,31,30,31,30,31};
            numbers(activity, months, 12, month);
            numbers(activity, days, counts[Math.max(0, Math.min(11, month))], day);
            months.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
                public void onNothingSelected(AdapterView<?> parent) {}
                public void onItemSelected(AdapterView<?> parent, View v, int selected, long id) {
                    if(days.getCount() != counts[selected]) numbers(activity, days, counts[selected], days.getSelectedItemPosition());
                }
            });
        }
        TextView caption = new TextView(activity);
        caption.setText(profile ? "玩家资料" : new String(title, charset));
        caption.setTextSize(14); caption.setPadding(padding, 0, padding, 0);
        caption.setMinHeight((int)(24 * activity.getResources().getDisplayMetrics().density));
        AlertDialog.Builder builder = new AlertDialog.Builder(activity).setCustomTitle(caption)
            .setView(scroll).setPositiveButton("确定", null);
        if(!profile) builder.setNegativeButton("取消", (d, w) -> result.done(null, 0, 0));
        AlertDialog dialog = builder.create(); dialog.setCancelable(!profile); dialog.setCanceledOnTouchOutside(false);
        parentDialog[0]=dialog;
        dialog.setOnCancelListener(d -> result.done(null, 0, 0));
        dialog.show();
        // Landscape IMEs leave little vertical space. Give the scrollable form
        // the full available window so the focused row is not clipped.
        dialog.getWindow().setLayout(WindowManager.LayoutParams.MATCH_PARENT, WindowManager.LayoutParams.MATCH_PARENT);
        dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v -> {
            byte[][] values = new byte[edits.length][];
            for(int i = 0; i < edits.length; i++) {
                edits[i].clearComposingText();
                String value = edits[i].getText().toString();
                value = GameTextCodec.insertion(value, 0, value.length(), "", 0, 0, charset, limits[i], false);
                if(profile && !fullWidth(value, charset, encoding)) {
                    edits[i].setError("请使用全角字符"); edits[i].requestFocus(); return;
                }
                values[i] = value.getBytes(charset);
            }
            result.done(values, profile ? months.getSelectedItemPosition() : 0, profile ? days.getSelectedItemPosition() : 0);
            dialog.dismiss();
        });
        for(EditText edit : edits) edit.setOnEditorActionListener((v, action, event) -> {
            if(action == EditorInfo.IME_ACTION_DONE) { dialog.getButton(AlertDialog.BUTTON_POSITIVE).performClick(); return true; }
            return false;
        });
        if(profile) {
            dialog.getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_HIDDEN);
            dialog.getButton(AlertDialog.BUTTON_POSITIVE).setFocusableInTouchMode(true);
            dialog.getButton(AlertDialog.BUTTON_POSITIVE).requestFocus();
        } else {
            edits[focus].requestFocus(); edits[focus].selectAll();
            dialog.getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE | WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);
        }
        return dialog;
    }
}
