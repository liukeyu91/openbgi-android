/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.hostspike;

import android.app.AlertDialog;
import android.app.NativeActivity;
import android.os.Handler;
import android.os.Looper;
import android.view.WindowManager;
import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;

public final class HostActivity extends NativeActivity {
    private final Handler ui = new Handler(Looper.getMainLooper());
    private volatile Message current;
    private AlertDialog dialog;

    private static final class Message {
        int choice; // Set by the UI before the dialog is dismissed.
        volatile int result;

        Message(int defaultChoice) { choice = defaultChoice; }
    }

    public boolean showEngineMessage(byte[] text, byte[] caption, int positive, int negative,
            boolean focusNegative, boolean utf8) {
        int dismissResult = negative == 0 ? positive : negative;
        Message request = new Message(dismissResult);
        current = request;
        return ui.post(() -> {
            if (current != request) return;
            if (isFinishing() || isDestroyed()) {
                request.result = dismissResult;
                return;
            }
            Charset encoding = utf8 ? StandardCharsets.UTF_8 : Charset.forName("windows-31j");
            AlertDialog.Builder builder = new AlertDialog.Builder(this)
                .setTitle(new String(caption, encoding))
                .setMessage(new String(text, encoding))
                .setPositiveButton(label(positive), (view, which) -> request.choice = positive);
            if (negative != 0)
                builder.setNegativeButton(label(negative), (view, which) -> request.choice = negative);
            AlertDialog message = builder.create();
            dialog = message;
            message.setCanceledOnTouchOutside(false);
            message.setOnDismissListener(view -> {
                if (dialog == message) dialog = null;
                request.result = request.choice;
            });
            try {
                message.show();
                if (focusNegative && negative != 0)
                    message.getButton(AlertDialog.BUTTON_NEGATIVE).requestFocus();
            } catch (WindowManager.BadTokenException expiredWindow) {
                if (dialog == message) dialog = null;
                request.result = dismissResult;
            }
        });
    }

    public int engineMessageResult() {
        Message request = current;
        return request == null ? 0 : request.result;
    }

    private CharSequence label(int button) {
        switch (button) {
        case 2: return getText(android.R.string.cancel);
        case 4: return getText(R.string.retry);
        case 6: return getText(android.R.string.yes);
        case 7: return getText(android.R.string.no);
        default: return getText(android.R.string.ok);
        }
    }

    @Override protected void onDestroy() {
        if (dialog != null) dialog.dismiss();
        super.onDestroy();
    }
}
