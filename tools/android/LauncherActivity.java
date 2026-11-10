/*
 * This file is part of OpenBGI (https://openbgi.net).
 * SPDX-License-Identifier: GPL-2.0-only
 */
package org.openbgi.port;

import android.app.Activity;
import android.app.NativeActivity;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.provider.DocumentsContract.Document;
import android.util.Log;
import android.view.Gravity;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

public final class LauncherActivity extends Activity {
    private static final int CHOOSE_GAME = 1;
    private static final String[] ROOT_ARCHIVES = {
        "system.arc", "sysprg.arc", "sysprg2.arc", "sysgrp.arc"
    };
    private TextView status;
    private Button choose;
    private boolean importing;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        File external = getExternalFilesDir(null);
        if (GameImport.gamePresent(new File(getFilesDir(), "game"))
                || (external != null && GameImport.gamePresent(new File(external, "game")))) {
            play();
            return;
        }

        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setGravity(Gravity.CENTER);
        int padding = (int) (24 * getResources().getDisplayMetrics().density);
        content.setPadding(padding, padding, padding, padding);
        status = new TextView(this);
        status.setGravity(Gravity.CENTER);
        status.setText("选择已解压的《フレラバ HD》汉化游戏目录。\n该目录应包含 system.arc 和 Archive 文件夹。");
        choose = new Button(this);
        choose.setText("选择游戏目录");
        choose.setOnClickListener(view -> pickDirectory());
        content.addView(status);
        content.addView(choose);
        setContentView(content);
    }

    private void pickDirectory() {
        Intent picker = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        picker.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        try {
            startActivityForResult(picker, CHOOSE_GAME);
        } catch (ActivityNotFoundException error) {
            status.setText("此设备无法打开系统文件选择器。");
        }
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != CHOOSE_GAME || result != RESULT_OK || data == null
                || data.getData() == null || importing || choose == null) {
            return;
        }
        Uri tree = data.getData();
        importing = true;
        choose.setEnabled(false);
        status.setText("正在检查游戏目录…");
        new Thread(() -> importGame(tree), "OpenBGI import").start();
    }

    @Override
    public void onBackPressed() {
        if (!importing) {
            super.onBackPressed();
        }
    }

    private void play() {
        Intent game = new Intent(this, NativeActivity.class);
        game.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_SINGLE_TOP);
        startActivity(game);
        finish();
    }

    private void importGame(Uri tree) {
        try {
            Map<String, Entry> root = entries(tree, DocumentsContract.getTreeDocumentId(tree));
            List<GameImport.Archive> archives = new ArrayList<>();
            for (String name : ROOT_ARCHIVES) {
                Entry entry = root.get(name);
                if (entry == null || entry.directory) {
                    throw new IOException("所选目录缺少 " + name);
                }
                archives.add(toArchive(tree, name, entry));
            }
            Entry archiveDir = root.get("archive");
            if (archiveDir == null || !archiveDir.directory) {
                throw new IOException("所选目录缺少 Archive 文件夹。");
            }
            for (Map.Entry<String, Entry> item : entries(tree, archiveDir.id).entrySet()) {
                if (!item.getValue().directory && item.getKey().endsWith(".arc")) {
                    archives.add(toArchive(tree, "Archive/" + item.getKey(), item.getValue()));
                }
            }
            if (archives.size() == ROOT_ARCHIVES.length) {
                throw new IOException("Archive 文件夹中没有 .arc 文件。");
            }
            GameImport.install(getFilesDir(), archives, (done, total) -> runOnUiThread(() -> {
                if (!isDestroyed()) {
                    status.setText("正在导入游戏文件 " + done + " / " + total);
                }
            }));
            runOnUiThread(() -> {
                if (!isDestroyed() && !isFinishing()) {
                    play();
                }
            });
        } catch (IOException | RuntimeException error) {
            Log.w("OpenBGI", "Game import failed", error);
            runOnUiThread(() -> {
                if (!isDestroyed()) {
                    importing = false;
                    choose.setEnabled(true);
                    String message = error.getLocalizedMessage();
                    status.setText("导入失败。\n" + (message == null ? "请检查游戏文件是否完整。" : message));
                }
            });
        }
    }

    private GameImport.Archive toArchive(Uri tree, String name, Entry entry) {
        Uri document = DocumentsContract.buildDocumentUriUsingTree(tree, entry.id);
        return new GameImport.Archive(name, entry.size,
                () -> getContentResolver().openInputStream(document));
    }

    private Map<String, Entry> entries(Uri tree, String directory) throws IOException {
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, directory);
        String[] columns = {Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME,
            Document.COLUMN_MIME_TYPE, Document.COLUMN_SIZE};
        Map<String, Entry> entries = new LinkedHashMap<>();
        try (Cursor cursor = getContentResolver().query(children, columns, null, null, null)) {
            if (cursor == null) {
                throw new IOException("无法读取所选目录。");
            }
            while (cursor.moveToNext()) {
                String name = cursor.getString(1);
                if (name == null || name.indexOf('/') >= 0 || name.indexOf('\\') >= 0) {
                    continue;
                }
                Entry entry = new Entry(cursor.getString(0), cursor.isNull(3) ? -1 : cursor.getLong(3),
                        Document.MIME_TYPE_DIR.equals(cursor.getString(2)));
                entries.put(name.toLowerCase(Locale.ROOT), entry);
            }
        }
        return entries;
    }

    private static final class Entry {
        final String id;
        final long size;
        final boolean directory;

        Entry(String id, long size, boolean directory) {
            this.id = id;
            this.size = size;
            this.directory = directory;
        }
    }
}
