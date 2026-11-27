/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;
import android.app.*;
import android.content.*;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.content.res.Configuration;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.view.*;
import android.text.*;
import android.provider.DocumentsContract;
import android.provider.DocumentsContract.Document;
import android.widget.*;
import java.io.*;
import java.lang.ref.WeakReference;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
public final class LauncherActivity extends Activity {
    private LinearLayout content,grid;
    private TextView status;
    private ProgressBar progress;
    private File coverTarget;
    private String saveExportGame="",saveImportGame="",patchGame="";
    private static final int PATCH_ZIP=7, PATCH_FOLDER=8, PATCH_EXPORT=9;
    private static final int IMPORT_FOLDER=1;
    private static boolean folderImportRunning;
    private static WeakReference<LauncherActivity> folderImportScreen=new WeakReference<>(null);
    private String storageAction="";
    private SaveRestore.Prepared pendingRestore;
    private GamePatch.Prepared pendingPatch;
    private String search="",page="library",importStatus="";
    private boolean byName;
    private boolean importing;
    private File games;
    private static final String FALLBACK="{\"games\":[{\"match\":\"FriendToLoverHD\",\"profile\":\"1.616\",\"product\":\"FriendToLoverHD\"}]}";
    @Override protected void attachBaseContext(Context base){super.attachBaseContext(AppLanguage.wrap(base));}
    @Override public void onCreate(Bundle state) {
        super.onCreate(state); games=new File(getFilesDir(),"games"); games.mkdirs();
        getWindow().setStatusBarColor(Ui.BG);getWindow().setNavigationBarColor(Ui.BG);
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR|View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR);
        if(state!=null){page=state.getString("page","library");search=state.getString("search","");byName=state.getBoolean("byName",false);storageAction=state.getString("storageAction","");
            String cover=state.getString("cover","");if(cover.matches("[a-z0-9-]{1,79}"))coverTarget=new File(games,cover);
            String exporting=state.getString("saveExportGame","");if(exporting.matches("[a-z0-9-]{1,79}"))saveExportGame=exporting;
            String restoring=state.getString("saveImportGame","");if(restoring.matches("[a-z0-9-]{1,79}"))saveImportGame=restoring;
            patchGame=state.getString("patchGame","");}
        try { migrate(); } catch(IOException error) { toast(getString(R.string.migration_failed)+error.getMessage()); }
        showLibrary();
    }
    private void migrate() throws IOException {
        File old=new File(getFilesDir(),"game"), migrated=new File(games,"legacy");
        if(GameImport.gamePresent(old) && !migrated.exists()) Files.move(old.toPath(),migrated.toPath(),StandardCopyOption.ATOMIC_MOVE);
        if(GameImport.gamePresent(migrated)) {
            File oldConfig=new File(getFilesDir(),"openbgi"), config=new File(migrated,".openbgi");
            if(oldConfig.isDirectory()&&!config.exists()) Files.move(oldConfig.toPath(),config.toPath(),StandardCopyOption.ATOMIC_MOVE);
            defaults(migrated,"gbk");
        }
        File external=getExternalFilesDir(null);
        if(external!=null && GameImport.gamePresent(new File(external,"game")) && !GameImport.gamePresent(migrated)) {
            File entry=new File(games,"external"); entry.mkdirs();
            File oldConfig=new File(getFilesDir(),"openbgi"), config=new File(entry,".openbgi");
            if(oldConfig.isDirectory()&&!config.exists())Files.move(oldConfig.toPath(),config.toPath(),StandardCopyOption.ATOMIC_MOVE);
            defaults(entry,"gbk");
        }
    }
    private void defaults(File dir,String encoding) throws IOException {
        File config=new File(dir,".openbgi"); config.mkdirs();
        File fallback=new File(config,"games.json"); if(!fallback.exists())write(fallback,FALLBACK);
        File enc=new File(config,"encoding.txt"); if(encoding!=null&&!enc.exists()) write(enc,encoding);
        File table=new File(dir,"games.json"); if(!table.exists()) write(table,FALLBACK);
    }
    private static void write(File file,String value) throws IOException {
        Path temp=file.toPath().resolveSibling(file.getName()+".tmp");
        Files.write(temp,value.getBytes(StandardCharsets.UTF_8));
        Files.move(temp,file.toPath(),StandardCopyOption.ATOMIC_MOVE,StandardCopyOption.REPLACE_EXISTING);
    }
    private String read(File file,String fallback) {
        try { return new String(Files.readAllBytes(file.toPath()),StandardCharsets.UTF_8).trim(); }
        catch(IOException error) { return fallback; }
    }
    private int dp(float n){return Ui.dp(this,n);}
    @Override public void onConfigurationChanged(Configuration c){super.onConfigurationChanged(c);showLibrary();}
    @Override protected void onResume(){
        super.onResume();
        folderImportScreen=new WeakReference<>(this);
        if(folderImportRunning&&!importing){importing=true;importStatus=getString(R.string.folder_loading);}
        if(games!=null)showLibrary();
        if(!importing)resumeFolderImport();
        if(!engineRunning()) {
            String failure=CrashLogs.takeFailureNotice(this);
            if(failure!=null)new AlertDialog.Builder(this).setTitle(getString(R.string.runtime_error)).setMessage(failure)
                .setPositiveButton(getString(R.string.export_logs),(dialog,which)->exportLogs()).setNegativeButton(getString(R.string.later),null).show();
        }
    }
    @Override protected void onSaveInstanceState(Bundle state){super.onSaveInstanceState(state);state.putString("page",page);state.putString("search",search);state.putBoolean("byName",byName);state.putString("saveExportGame",saveExportGame);state.putString("saveImportGame",saveImportGame);state.putString("storageAction",storageAction);state.putString("patchGame",patchGame);if(coverTarget!=null)state.putString("cover",coverTarget.getName());}
    private void addGame(){
        if(importing){toast(getString(R.string.operation_busy));return;}
        if(!GameStorage.directAvailable(this)){copyGame();return;}
        AlertDialog dialog=new AlertDialog.Builder(this).setTitle(getString(R.string.add_game))
            .setItems(new String[]{getString(R.string.add_direct),getString(R.string.add_image),getString(R.string.add_zip),getString(R.string.add_copy)},(d,which)->{
                if(which==0)directAccess("add");else if(which==1)directAccess("add-image");else if(which==2)directAccess("add-zip");else copyGame();
            }).setNegativeButton(getString(R.string.cancel),null).create();dialog.show();Ui.style(dialog);
    }
    private void directAccess(String action){
        if(!GameStorage.directAvailable(this))return;
        storageAction=action;
        if(GameStorage.granted(this)){storageReady();return;}
        AlertDialog dialog=new AlertDialog.Builder(this).setTitle(getString(R.string.storage_permission))
            .setMessage(getString(R.string.storage_permission_body))
            .setNegativeButton(getString(R.string.cancel),(d,w)->storageAction="").setPositiveButton(getString(R.string.grant_permission),(d,w)->{
                try{GameStorage.request(this);}catch(ActivityNotFoundException unavailable){storageAction="";toast(getString(R.string.storage_settings_failed));}
            }).create();dialog.setOnCancelListener(d->storageAction="");dialog.show();Ui.style(dialog);
    }
    private void storageReady(){
        String action=storageAction;storageAction="";
        if(!GameStorage.granted(this)){toast(getString(R.string.storage_denied));return;}
        if(action.equals("add"))GameStorage.choose(this,this::registerDirect);
        else if(action.equals("add-image"))GameStorage.chooseContainer(this,ContainerGame.Kind.IMAGE,this::registerImage);
        else if(action.equals("add-zip"))GameStorage.chooseContainer(this,ContainerGame.Kind.ZIP,this::registerZip);
        else if(action.startsWith("play:")&&action.substring(5).matches("[a-z0-9-]{1,79}"))play(new File(games,action.substring(5)));
    }
    @Override public void onRequestPermissionsResult(int request,String[] permissions,int[] grants){
        super.onRequestPermissionsResult(request,permissions,grants);if(request==GameStorage.REQUEST)storageReady();
    }
    private void copyGame(){
        Intent picker=new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE)
            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION|Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        try{startActivityForResult(picker,IMPORT_FOLDER);}catch(ActivityNotFoundException e){toast(getString(R.string.file_picker_failed));}
    }
    private void exportLogs(){
        Intent save=new Intent(Intent.ACTION_CREATE_DOCUMENT).setType("application/zip").addCategory(Intent.CATEGORY_OPENABLE);
        save.putExtra(Intent.EXTRA_TITLE,"OpenBGI-logs-"+System.currentTimeMillis()+".zip");
        try{startActivityForResult(save,2);}catch(ActivityNotFoundException e){toast(getString(R.string.file_picker_failed));}
    }
    private void exportSaves(File dir,String name){
        if(importing)return;
        if(engineRunning()){toast(getString(R.string.save_export_exit));return;}
        importing=true;importStatus=getString(R.string.save_search);showLibrary();
        new Thread(()->{
            try(GameDataLock lock=GameDataLock.acquire(lockDirectory(dir))){
                File data=dataDirectory(dir);
                SaveRestore.recover(data);
                if(SaveBackup.count(data)==0)throw new IOException(getString(R.string.save_export_empty));
                runOnUiThread(()->{
                    importing=false;if(isDestroyed() || isFinishing())return;showLibrary();
                    saveExportGame=dir.getName();
                    Intent save=new Intent(Intent.ACTION_CREATE_DOCUMENT).setType("application/zip").addCategory(Intent.CATEGORY_OPENABLE);
                    save.putExtra(Intent.EXTRA_TITLE,SaveBackup.filename(name));
                    try{startActivityForResult(save,4);}catch(ActivityNotFoundException error){saveExportGame="";toast(getString(R.string.file_picker_failed));}
                });
            }catch(IOException error){runOnUiThread(()->{importing=false;if(!isDestroyed()){showLibrary();toast(error.getMessage());}});}
        },"OpenBGI-SaveInventory").start();
    }
    private void writeSaveBackup(Uri uri,File dir){
        if(importing || engineRunning()){
            toast(getString(R.string.save_export_wait));discardExport(uri);return;
        }
        importing=true;importStatus=getString(R.string.save_export_busy);showLibrary();
        String name=title(dir),version=versionName();
        new Thread(()->{
            SaveBackup.Backup backup=null;
            try(GameDataLock lock=GameDataLock.acquire(lockDirectory(dir))){
                File data=dataDirectory(dir);
                SaveRestore.recover(data);
                backup=SaveBackup.create(data,getCacheDir(),name,version,(done,total)->runOnUiThread(()->{
                    importStatus=getString(R.string.save_export_progress,done,total);if(status!=null)status.setText(importStatus);
                }));
                if(engineRunning())throw new IOException(getString(R.string.save_export_running));
                try(InputStream in=new FileInputStream(backup.file);OutputStream out=getContentResolver().openOutputStream(uri,"wt")){
                    if(out==null)throw new IOException(getString(R.string.save_write_failed));
                    byte[] buffer=new byte[64*1024];int n;while((n=in.read(buffer))!=-1)out.write(buffer,0,n);
                }
                final int count=backup.count;
                runOnUiThread(()->{importing=false;if(!isDestroyed()){showLibrary();toast(getString(R.string.save_export_done,count));}});
            }catch(Exception error){
                discardExport(uri);
                runOnUiThread(()->{importing=false;if(!isDestroyed()){showLibrary();toast(getString(R.string.save_export_failed)+error.getMessage());}});
            }finally{if(backup!=null)backup.file.delete();}
        },"OpenBGI-SaveExport").start();
    }
    private void importSaves(File dir){
        if(importing){toast(getString(R.string.operation_busy));return;}
        if(engineRunning()){toast(getString(R.string.save_import_exit));return;}
        saveImportGame=dir.getName();
        Intent picker=new Intent(Intent.ACTION_OPEN_DOCUMENT).setType("*/*").addCategory(Intent.CATEGORY_OPENABLE)
            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        picker.putExtra(Intent.EXTRA_MIME_TYPES,new String[]{"application/zip","application/x-zip-compressed","application/octet-stream"});
        try{startActivityForResult(picker,5);}catch(ActivityNotFoundException error){saveImportGame="";toast(getString(R.string.file_picker_failed));}
    }
    private void prepareSaveRestore(Uri uri,File dir){
        if(importing || engineRunning()){toast(getString(R.string.save_import_wait));return;}
        importing=true;importStatus=getString(R.string.save_checking);showLibrary();
        new Thread(()->{
            SaveRestore.Prepared prepared=null;
            try(InputStream in=getContentResolver().openInputStream(uri)){
                if(in==null)throw new IOException(getString(R.string.save_read_failed));
                prepared=SaveRestore.prepare(in,getCacheDir());final SaveRestore.Prepared ready=prepared;
                runOnUiThread(()->{
                    if(isDestroyed() || isFinishing()){ready.close();return;}
                    pendingRestore=ready;importStatus=getString(R.string.save_checked);showLibrary();
                    String message=getString(R.string.save_import_confirm_body,
                        ready.gameName,title(dir),ready.count,ready.bytes/(1024.0*1024.0));
                    AlertDialog dialog=new AlertDialog.Builder(this).setTitle(getString(R.string.save_import)).setMessage(message)
                        .setNegativeButton(getString(R.string.cancel),null).setPositiveButton(getString(R.string.save_import_confirm),(d,w)->{
                            pendingRestore=null;applySaveRestore(ready,dir);
                        }).create();
                    dialog.setOnDismissListener(d->{if(pendingRestore==ready){pendingRestore=null;ready.close();importing=false;if(!isDestroyed())showLibrary();}});
                    dialog.show();Ui.style(dialog);
                });
                prepared=null;
            }catch(Exception error){
                runOnUiThread(()->{importing=false;if(!isDestroyed()){showLibrary();toast(getString(R.string.save_import_failed)+error.getMessage());}});
            }finally{if(prepared!=null)prepared.close();}
        },"OpenBGI-SaveCheck").start();
    }
    private void applySaveRestore(SaveRestore.Prepared prepared,File dir){
        importStatus=getString(R.string.save_import_busy);showLibrary();
        new Thread(()->{
            try(SaveRestore.Prepared restore=prepared;GameDataLock lock=GameDataLock.acquire(lockDirectory(dir))){
                if(engineRunning())throw new IOException(getString(R.string.save_import_running));
                restore.apply(dataDirectory(dir),(done,total)->runOnUiThread(()->{
                    importStatus=getString(R.string.save_import_progress,done,total);if(status!=null)status.setText(importStatus);
                }));
                runOnUiThread(()->{importing=false;if(!isDestroyed()){showLibrary();toast(getString(R.string.save_import_done,prepared.count));}});
            }catch(Exception error){
                runOnUiThread(()->{importing=false;if(!isDestroyed()){showLibrary();toast(getString(R.string.save_import_failed)+error.getMessage());}});
            }
        },"OpenBGI-SaveImport").start();
    }
    @Override protected void onDestroy(){
        if(pendingRestore!=null){pendingRestore.close();pendingRestore=null;}
        if(pendingPatch!=null){discardPatch(pendingPatch);pendingPatch=null;}
        super.onDestroy();
    }
    private void discardExport(Uri uri){try{DocumentsContract.deleteDocument(getContentResolver(),uri);}catch(Exception ignored){}}
    private LinearLayout nav(boolean rail){
        LinearLayout bar=rail?Ui.column(this):Ui.row(this);bar.setPadding(dp(8),dp(8),dp(8),dp(8));
        String[] names={getString(R.string.library),getString(R.string.guide),getString(R.string.about)},icons={"library","guide","info"},pages={"library","guide","about"};
        for(int i=0;i<names.length;i++){
            final String next=pages[i];boolean selected=page.equals(next);
            LinearLayout item=Ui.column(this);item.setGravity(Gravity.CENTER);item.setPadding(dp(8),dp(10),dp(8),dp(10));
            item.setBackground(Ui.touch(this,selected?Ui.TINT:0x00000000,16));item.setFocusable(true);item.setContentDescription(names[i]);
            ImageView icon=new ImageView(this);icon.setImageDrawable(new Ui.Icon(icons[i],selected?Ui.ACCENT:Ui.MUTED));
            item.addView(icon,new LinearLayout.LayoutParams(dp(22),dp(22)));Ui.space(item,5);
            TextView label=Ui.text(this,names[i],11,selected?Ui.ACCENT:Ui.MUTED,selected);label.setGravity(Gravity.CENTER);item.addView(label);
            item.setOnClickListener(v->{page=next;showLibrary();});
            LinearLayout.LayoutParams lp=rail?new LinearLayout.LayoutParams(-1,dp(72)):new LinearLayout.LayoutParams(0,dp(72),1);
            if(rail)lp.bottomMargin=dp(8);bar.addView(item,lp);
        }return bar;
    }
    private void showLibrary() {
        boolean wide=getResources().getConfiguration().screenWidthDp>=600;
        LinearLayout root=Ui.column(this);root.setBackgroundColor(Ui.BG);root.setFocusableInTouchMode(true);
        LinearLayout header=Ui.row(this);header.setPadding(dp(20),dp(wide?6:10),dp(20),dp(wide?6:12));
        ImageView mark=new ImageView(this);mark.setImageDrawable(new Ui.Icon("brand",Ui.PAPER));mark.setPadding(dp(10),dp(10),dp(10),dp(10));mark.setBackground(Ui.shape(this,Ui.ACCENT,13));
        header.addView(mark,new LinearLayout.LayoutParams(dp(wide?36:42),dp(wide?36:42)));
        LinearLayout wordmark=Ui.column(this);wordmark.setPadding(dp(12),0,0,0);wordmark.addView(Ui.text(this,"OpenBGI",21,Ui.INK,true));
        TextView sub=Ui.text(this,getString(R.string.tagline),11,Ui.MUTED,false);if(!wide)wordmark.addView(sub);header.addView(wordmark,new LinearLayout.LayoutParams(0,-2,1));
        Button add=Ui.button(this,getString(R.string.add_game_icon),true,this::addGame);header.addView(add,new LinearLayout.LayoutParams(-2,dp(46)));root.addView(header);
        LinearLayout body=Ui.row(this);body.setGravity(Gravity.TOP);root.addView(body,new LinearLayout.LayoutParams(-1,0,1));
        if(wide)body.addView(nav(true),new LinearLayout.LayoutParams(dp(84),-1));
        ScrollView scroll=new ScrollView(this);scroll.setFillViewport(true);scroll.setClipToPadding(false);
        content=Ui.column(this);content.setPadding(dp(wide?22:20),dp(wide?8:18),dp(wide?28:20),dp(24));scroll.addView(content);
        body.addView(scroll,new LinearLayout.LayoutParams(0,-1,1));
        if(!wide){LinearLayout bottom=nav(false);bottom.setBackgroundColor(Ui.PAPER);root.addView(bottom);}
        setContentView(root);root.requestFocus();
        if(importing){
            LinearLayout importingBox=Ui.column(this);importingBox.setPadding(dp(16),dp(14),dp(16),dp(14));importingBox.setBackground(Ui.shape(this,Ui.TINT,16));
            status=Ui.text(this,importStatus,13,Ui.ACCENT,true);importingBox.addView(status);progress=new ProgressBar(this,null,android.R.attr.progressBarStyleHorizontal);progress.setIndeterminate(true);
            importingBox.addView(progress,new LinearLayout.LayoutParams(-1,dp(12)));content.addView(importingBox);Ui.space(content,18);
        }else{status=null;progress=null;}
        if(page.equals("guide")){showGuide();return;}if(page.equals("about")){showAbout();return;}
        List<File> library=library();
        LinearLayout heading=Ui.row(this);LinearLayout caption=Ui.column(this);caption.addView(Ui.text(this,getString(R.string.library),wide?26:30,Ui.INK,true));Ui.space(caption,wide?5:7);
        caption.addView(Ui.text(this,getString(R.string.game_count,library.size()),12,Ui.MUTED,false));
        heading.addView(caption,new LinearLayout.LayoutParams(0,-2,1));
        Button sort=Ui.button(this,byName?getString(R.string.sort_name):getString(R.string.sort_recent),false,()->{byName=!byName;showLibrary();});sort.setTextSize(11);sort.setPadding(dp(10),0,dp(10),0);heading.addView(sort,new LinearLayout.LayoutParams(-2,dp(42)));content.addView(heading);Ui.space(content,wide?12:22);
        if(library.isEmpty()){emptyLibrary();return;}
        LinearLayout searchBox=Ui.row(this);searchBox.setBackground(Ui.border(this,Ui.PAPER,14));searchBox.setPadding(dp(14),0,dp(12),0);
        ImageView find=new ImageView(this);find.setImageDrawable(new Ui.Icon("search",Ui.MUTED));searchBox.addView(find,new LinearLayout.LayoutParams(dp(20),dp(20)));
        EditText query=new EditText(this);query.setSingleLine();query.setTextSize(14);query.setTextColor(Ui.INK);query.setHintTextColor(Ui.MUTED);query.setBackgroundColor(0);query.setPadding(dp(10),0,0,0);query.setHint(getString(R.string.search_games));query.setContentDescription(getString(R.string.search_games));query.setText(search);
        searchBox.addView(query,new LinearLayout.LayoutParams(0,dp(wide?46:50),1));
        if(wide){LinearLayout.LayoutParams findParams=new LinearLayout.LayoutParams(dp(190),dp(46));findParams.setMargins(dp(12),0,dp(12),0);heading.addView(searchBox,1,findParams);}
        else{content.addView(searchBox);Ui.space(content,20);}
        grid=Ui.column(this);content.addView(grid);renderGames(library);
        query.addTextChangedListener(new TextWatcher(){public void beforeTextChanged(CharSequence s,int a,int n,int f){}public void onTextChanged(CharSequence s,int a,int before,int count){search=s.toString();renderGames(library);}public void afterTextChanged(Editable e){}});
    }
    private List<File> library(){
        List<File> list=new ArrayList<>();File[] folders=games.listFiles();if(folders!=null)for(File dir:folders)
            if(dir.isDirectory() && (GameImport.gamePresent(dir)||DirectGame.linked(dir)||ContainerGame.linked(dir)||dir.getName().equals("external")))list.add(dir);
        Comparator<File> names=Comparator.comparing(this::title,String.CASE_INSENSITIVE_ORDER);
        list.sort(byName?names:Comparator.<File>comparingLong(dir->new File(dir,".openbgi/last-played.txt").lastModified()).reversed().thenComparing(names));return list;
    }
    private String title(File dir){return read(new File(dir,".openbgi/name.txt"),dir.getName().equals("legacy")?"Fureraba HD":dir.getName());}
    private void renderGames(List<File> library){
        grid.removeAllViews();int available=getResources().getConfiguration().screenWidthDp;
        int columns=available>=720?3:available<360?1:2;LinearLayout row=null;int shown=0;
        for(int i=0;i<library.size();i++){
            File dir=library.get(i);String name=title(dir);if(!name.toLowerCase(Locale.ROOT).contains(search.toLowerCase(Locale.ROOT).trim()))continue;
            if(shown%columns==0){row=Ui.row(this);row.setGravity(Gravity.TOP);LinearLayout.LayoutParams r=new LinearLayout.LayoutParams(-1,-2);r.bottomMargin=dp(16);grid.addView(row,r);}
            LinearLayout card=gameCard(dir,name,i,columns);LinearLayout.LayoutParams cell=new LinearLayout.LayoutParams(0,-2,1);if(shown%columns!=columns-1)cell.rightMargin=dp(14);row.addView(card,cell);shown++;
        }
        if(shown==0){TextView none=Ui.text(this,getString(R.string.no_matches,search),15,Ui.MUTED,false);none.setPadding(0,dp(32),0,dp(32));none.setGravity(Gravity.CENTER);grid.addView(none);}
        else while(shown%columns!=0){LinearLayout.LayoutParams blank=new LinearLayout.LayoutParams(0,1,1);if(shown%columns!=columns-1)blank.rightMargin=dp(14);row.addView(new View(this),blank);shown++;}
    }
    private LinearLayout gameCard(File dir,String name,int index,int columns){
        LinearLayout card=Ui.column(this);card.setBackground(Ui.border(this,Ui.PAPER,20));card.setClipToOutline(true);
        int available=getResources().getConfiguration().screenWidthDp;boolean wide=available>=600;int width=(available-(wide?134:40)-(columns-1)*14)/columns;
        LibraryCover cover=new LibraryCover(this,dir,name);card.addView(cover,new LinearLayout.LayoutParams(-1,dp(Math.max(80,width*(available>=600?38:75)/100))));
        cover.setContentDescription(getString(R.string.play_title,name));cover.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_YES);cover.setFocusable(true);cover.setOnClickListener(v->{if(!importing)play(dir);});
        LinearLayout details=Ui.column(this);details.setPadding(dp(14),dp(wide?10:12),dp(10),dp(wide?10:14));card.addView(details);
        LinearLayout top=Ui.row(this);top.setGravity(Gravity.TOP);TextView title=Ui.text(this,name,wide?14:16,Ui.INK,true);title.setMaxLines(2);title.setMinLines(2);title.setEllipsize(TextUtils.TruncateAt.END);
        top.addView(title,new LinearLayout.LayoutParams(0,-2,1));ImageView more=Ui.iconButton(this,"more",getString(R.string.manage_title,name),()->gameMenu(dir,name));top.addView(more,new LinearLayout.LayoutParams(dp(38),dp(44)));details.addView(top);Ui.space(details,wide?4:8);
        String meta,compatibility;try{GameSettings s=GameSettings.load(dir);meta=AppMessages.encodingLabel(this,s)+"   ·   "+(s.profile.equals("auto")?getString(R.string.auto_detect):s.profile);compatibility=AppMessages.cachedCompatibility(this,dir,resourceDirectory(dir),s.profile,versionName());}catch(IOException e){meta=getString(R.string.settings_load_error);compatibility=getString(R.string.compat_unchecked);}
        TextView tag=Ui.text(this,meta,10,Ui.MUTED,false);tag.setMaxLines(1);tag.setEllipsize(TextUtils.TruncateAt.END);details.addView(tag);Ui.space(details,wide?8:14);
        TextView check=Ui.text(this,compatibility,10,Ui.MUTED,false);check.setMaxLines(2);check.setEllipsize(TextUtils.TruncateAt.END);details.addView(check);Ui.space(details,wide?6:10);
        Button play=Ui.button(this,getString(R.string.play_game),false,()->{if(!importing)play(dir);});play.setTextColor(Ui.ACCENT);play.setTextSize(12);details.addView(play,new LinearLayout.LayoutParams(-1,dp(wide?40:44)));return card;
    }
    private void emptyLibrary(){
        LinearLayout empty=Ui.column(this);empty.setGravity(Gravity.CENTER);empty.setPadding(dp(24),dp(40),dp(24),dp(36));empty.setBackground(Ui.border(this,Ui.PAPER,22));
        ImageView book=new ImageView(this);book.setImageDrawable(new Ui.Icon("library",Ui.ACCENT));book.setPadding(dp(20),dp(20),dp(20),dp(20));book.setBackground(Ui.shape(this,Ui.TINT,24));empty.addView(book,new LinearLayout.LayoutParams(dp(88),dp(88)));Ui.space(empty,24);
        empty.addView(Ui.text(this,getString(R.string.empty_library),18,Ui.INK,true));Ui.space(empty,12);
        TextView tip=Ui.text(this,getString(R.string.empty_library_body),13,Ui.MUTED,false);tip.setGravity(Gravity.CENTER);empty.addView(tip);Ui.space(empty,26);empty.addView(Ui.button(this,getString(R.string.choose_folder),true,this::addGame));content.addView(empty);
    }
    private void infoCard(String title,String text){
        LinearLayout box=Ui.column(this);box.setPadding(dp(20),dp(20),dp(20),dp(20));box.setBackground(Ui.border(this,Ui.PAPER,20));box.addView(Ui.text(this,title,17,Ui.INK,true));Ui.space(box,10);TextView body=Ui.text(this,text,14,Ui.MUTED,false);body.setLineSpacing(dp(6),1);box.addView(body);content.addView(box);Ui.space(content,14);
    }
    private void showGuide(){
        content.addView(Ui.text(this,getString(R.string.guide),30,Ui.INK,true));Ui.space(content,18);
        infoCard(getString(R.string.add_game),getString(GameStorage.directAvailable(this)?R.string.guide_add:R.string.guide_add_saf));
        if(GameStorage.directAvailable(this)){infoCard(getString(R.string.add_image),getString(R.string.guide_image));infoCard(getString(R.string.add_zip),getString(R.string.guide_zip));}
        infoCard(getString(R.string.text_encoding),getString(R.string.guide_encoding));
        infoCard(getString(R.string.guide_controls_title),getString(R.string.guide_controls));
        infoCard(getString(R.string.guide_saves_title),getString(R.string.guide_saves));
    }
    private void showAbout(){
        content.addView(Ui.text(this,getString(R.string.about_openbgi),30,Ui.INK,true));Ui.space(content,8);content.addView(Ui.text(this,getString(R.string.app_version,versionName()),13,Ui.MUTED,false));Ui.space(content,24);
        content.addView(Ui.text(this,getString(R.string.about_compat),13,Ui.MUTED,false));Ui.space(content,16);
        content.addView(Ui.button(this,getString(R.string.app_language_button),false,this::showLanguagePicker),
            new LinearLayout.LayoutParams(-1,dp(50)));Ui.space(content,12);
        content.addView(Ui.button(this,getString(R.string.export_runtime_logs),false,this::exportLogs),new LinearLayout.LayoutParams(-1,dp(50)));Ui.space(content,12);
        content.addView(Ui.button(this,getString(R.string.source_code),false,()->{try{startActivity(new Intent(Intent.ACTION_VIEW,Uri.parse("https://github.com/liukeyu91/openbgi-android")));}catch(ActivityNotFoundException e){toast(getString(R.string.browser_missing));}}),new LinearLayout.LayoutParams(-1,dp(50)));
        Ui.space(content,12);content.addView(Ui.text(this,"BGI / Ethornell · GPL-2.0",11,Ui.MUTED,false));
    }
    private void showLanguagePicker(){
        if(importing){toast(getString(R.string.operation_busy));return;}
        if(engineRunning()){toast(getString(R.string.language_exit));return;}
        String[] labels=AppLanguage.labels(this);
        int current=AppLanguage.selected(this);
        AlertDialog dialog=new AlertDialog.Builder(this).setTitle(R.string.app_language)
            .setSingleChoiceItems(labels,current,(d,which)->{
                d.dismiss();
                if(which==current)return;
                if(!AppLanguage.set(this,which))toast(getString(R.string.language_save_failed));
                else if(android.os.Build.VERSION.SDK_INT<33)recreate();
            }).setNegativeButton(R.string.cancel,null).create();
        dialog.show();Ui.style(dialog);
    }
    private String versionName(){
        try{String version=getPackageManager().getPackageInfo(getPackageName(),0).versionName;return version==null?getString(R.string.unknown):version;}
        catch(android.content.pm.PackageManager.NameNotFoundException ignored){return getString(R.string.unknown);}
    }
    private void gameMenu(File dir,String name) {
        if(importing)return;
        boolean linked=DirectGame.linked(dir),container=ContainerGame.linked(dir),removable=linked||container;
        boolean zip=container && ContainerGame.kind(dir)==ContainerGame.Kind.ZIP;
        AlertDialog menu=new AlertDialog.Builder(this).setTitle(name).setItems(new String[]{getString(R.string.play_game),getString(R.string.game_settings),getString(R.string.rename),getString(R.string.change_cover),removable?getString(R.string.remove_library):getString(R.string.delete_game),getString(R.string.compat_check),getString(R.string.save_export),getString(R.string.save_import),getString(R.string.process_patch),getString(R.string.patch_overlay)},(d,which)->{
            if(which==0) play(dir);
            if(which==1) SettingsDialog.show(this,dir,name,this::showLibrary);
            if(which==2)rename(dir,name);
            if(which==3){coverTarget=dir;Intent pick=new Intent(Intent.ACTION_OPEN_DOCUMENT).setType("image/*").addCategory(Intent.CATEGORY_OPENABLE);try{startActivityForResult(pick,3);}catch(ActivityNotFoundException e){coverTarget=null;toast(getString(R.string.image_picker_failed));}}
            if(which==5)checkCompatibility(dir);
            if(which==6)exportSaves(dir,name);
            if(which==7)importSaves(dir);
            if(which==8){
                if(container){toast(getString(zip?R.string.zip_patch_unsupported:R.string.image_patch_unsupported));return;}
                processTranslation(dir);
            }
            if(which==9)patchMenu(dir);
            if(which==4 && engineRunning()) { toast(getString(R.string.delete_game_exit)); return; }
            if(which==4) new AlertDialog.Builder(this).setTitle(getString(removable?R.string.remove_game_prompt:R.string.delete_game_prompt,name)).setMessage(linked?getString(R.string.remove_game_body):container?getString(zip?R.string.remove_zip_body:R.string.remove_image_body):getString(R.string.delete_game_body))
                .setNegativeButton(getString(R.string.cancel),null).setPositiveButton(removable?R.string.remove:R.string.delete,(dialog,w)->new Thread(()->{
                    try { GameImport.remove(dir.toPath()); runOnUiThread(this::showLibrary); }
                    catch(IOException error) { runOnUiThread(()->toast(error.getMessage())); }
                }).start()).show();
        }).setNegativeButton(getString(R.string.cancel),null).create();menu.show();Ui.style(menu);
    }
    private void rename(File dir,String name){
        EditText input=new EditText(this);input.setSingleLine();input.setText(name);input.selectAll();input.setFilters(new InputFilter[]{new InputFilter.LengthFilter(100)});
        LinearLayout box=Ui.column(this);box.setPadding(dp(24),dp(8),dp(24),0);box.addView(input);
        AlertDialog dialog=new AlertDialog.Builder(this).setTitle(getString(R.string.rename_game)).setView(box).setNegativeButton(getString(R.string.cancel),null).setPositiveButton(getString(R.string.save),null).create();dialog.show();Ui.style(dialog);
        dialog.getButton(-1).setOnClickListener(v->{String value=input.getText().toString().trim();if(value.isEmpty()){input.setError(getString(R.string.game_name_needed));return;}try{write(new File(dir,".openbgi/name.txt"),value);dialog.dismiss();showLibrary();}catch(IOException e){toast(e.getMessage());}});
    }
    private void importCover(Uri uri,File dir){
        if(dir==null || !dir.isDirectory())return;
        new Thread(()->{
            File temp=new File(dir,".openbgi/cover-import.tmp"),encoded=new File(dir,".openbgi/cover.jpg.tmp");Bitmap image=null,scaled=null;
            try{
                try(InputStream in=getContentResolver().openInputStream(uri);OutputStream out=new FileOutputStream(temp)){
                    if(in==null)throw new IOException(getString(R.string.image_read_error));byte[] buf=new byte[65536];int n;long total=0;
                    while((n=in.read(buf))!=-1){total+=n;if(total>20*1024*1024)throw new IOException(getString(R.string.image_limit));out.write(buf,0,n);}
                }
                BitmapFactory.Options options=new BitmapFactory.Options();options.inJustDecodeBounds=true;BitmapFactory.decodeFile(temp.toString(),options);
                if(options.outWidth<=0 || options.outHeight<=0)throw new IOException(getString(R.string.image_unknown));options.inSampleSize=1;
                while(options.outWidth/options.inSampleSize>1600 || options.outHeight/options.inSampleSize>1600)options.inSampleSize*=2;
                options.inJustDecodeBounds=false;image=BitmapFactory.decodeFile(temp.toString(),options);if(image==null)throw new IOException(getString(R.string.image_decode_error));
                float ratio=Math.min(1,1000f/Math.max(image.getWidth(),image.getHeight()));scaled=Bitmap.createScaledBitmap(image,Math.max(1,Math.round(image.getWidth()*ratio)),Math.max(1,Math.round(image.getHeight()*ratio)),true);
                try(OutputStream out=new FileOutputStream(encoded)){if(!scaled.compress(Bitmap.CompressFormat.JPEG,90,out))throw new IOException(getString(R.string.cover_save_failed));}
                Files.move(encoded.toPath(),new File(dir,".openbgi/cover.jpg").toPath(),StandardCopyOption.ATOMIC_MOVE,StandardCopyOption.REPLACE_EXISTING);
                runOnUiThread(()->{if(!isDestroyed())showLibrary();});
            }catch(Exception e){runOnUiThread(()->toast(getString(R.string.cover_update_error)+e.getMessage()));}
            finally{if(scaled!=null && scaled!=image)scaled.recycle();if(image!=null)image.recycle();temp.delete();encoded.delete();}
        },"OpenBGI-Cover").start();
    }
    private boolean engineRunning() {
        ActivityManager manager=(ActivityManager)getSystemService(ACTIVITY_SERVICE);
        List<ActivityManager.RunningAppProcessInfo> processes=manager.getRunningAppProcesses();
        if(processes!=null)for(ActivityManager.RunningAppProcessInfo process:processes)
            if(process.processName.equals(getPackageName()+":engine"))return true;
        return false;
    }
    private File resourceDirectory(File dir) throws IOException {
        if(dir.getName().equals("external")){
            File external=getExternalFilesDir(null);if(external!=null)return new File(external,"game");
        }
        // A container game has no resource directory: the mount answers through the volume.
        return DirectGame.resources(dir);
    }
    private File lockDirectory(File dir) throws IOException {
        return DirectGame.linked(dir)||ContainerGame.linked(dir)?dir:resourceDirectory(dir);
    }
    private File dataDirectory(File dir) throws IOException {
        if(ContainerGame.linked(dir)){
            File data=ContainerGame.data(dir);
            if(data!=null)return data;
            try(DiscImage.Source source=DiscImage.open(ContainerGame.source(dir))){return ContainerGame.data(dir,source);}
        }
        return DirectGame.data(dir,resourceDirectory(dir));
    }
    private ProgramCompatibility.Result scanCompatibility(File dir,String profile) {
        File resources;ProgramCompatibility.Result result;
        try{resources=resourceDirectory(dir);}catch(IOException error){result=new ProgramCompatibility.Result();result.error=error.getMessage();return result;}
        try(ResourceLayers layers=ResourceLayers.of(dir,resources)) {
            try {
                synchronized(NativeProgramInspector.class) {
                    result=ProgramCompatibility.scan(layers,profile,new NativeProgramInspector(),(done,total)->{
                        if(done%4!=0 && done!=total)return;
                        runOnUiThread(()->{importStatus=getString(R.string.compat_progress,done,total);if(status!=null)status.setText(importStatus);});
                    });
                }
            }catch(IOException|RuntimeException|LinkageError error){
                result=new ProgramCompatibility.Result();result.error=getString(R.string.compat_failed)+error.getMessage();
                android.util.Log.w("OpenBGI","Compatibility check failed",error);
            }
            try{ProgramCompatibility.save(layers,profile,versionName(),result,AppMessages.compatibilityReport(this,result));}
            catch(IOException error){result.error=getString(R.string.compat_save_failed)+error.getMessage();}
        }catch(IOException error){
            result=new ProgramCompatibility.Result();result.error=getString(R.string.compat_failed)+error.getMessage();
        }
        return result;
    }
    private void showCompatibility(ProgramCompatibility.Result result) {
        TextView body=Ui.text(this,AppMessages.compatibilityReport(this,result),13,Ui.INK,false);body.setTextIsSelectable(true);
        int pad=dp(20);body.setPadding(pad,dp(12),pad,dp(20));
        ScrollView scroll=new ScrollView(this);scroll.addView(body);
        AlertDialog dialog=new AlertDialog.Builder(this).setTitle(getString(R.string.compat_check)).setView(scroll).setPositiveButton(getString(R.string.ok),null).create();
        dialog.show();Ui.style(dialog);
    }
    private void checkCompatibility(File dir) {
        if(importing)return;
        importing=true;importStatus=getString(R.string.compat_busy);showLibrary();
        new Thread(()->{
            ProgramCompatibility.Result result;
            try{result=scanCompatibility(dir,GameSettings.load(dir).profile);}
            catch(IOException error){result=new ProgramCompatibility.Result();result.error=getString(R.string.settings_read_error)+error.getMessage();}
            final ProgramCompatibility.Result completed=result;
            runOnUiThread(()->{importing=false;if(isDestroyed()||isFinishing())return;showLibrary();showCompatibility(completed);});
        },"OpenBGI-Compatibility").start();
    }
    private void play(File dir) {
        if(importing)return;
        if(engineRunning()){
            if(read(new File(getFilesDir(),"active-game.txt"),"").equals(dir.getName()))launchGame(dir);
            else toast(getString(R.string.exit_other_game));
            return;
        }
        boolean container=ContainerGame.linked(dir);
        if((container||DirectGame.linked(dir))&&!GameStorage.granted(this)){
            if(!GameStorage.directAvailable(this))toast(getString(R.string.guide_add_saf));
            else directAccess("play:"+dir.getName());
            return;
        }
        File resources;
        boolean refreshContainer=false;
        try{
            resources=resourceDirectory(dir);
            if(container){
                File source=ContainerGame.requireSource(dir);
                ContainerGame.Summary known=ContainerGame.summary(dir);
                refreshContainer=known==null||!ContainerGame.stamp(source).same(known.stamp);
                if(!refreshContainer&&!known.boot){
                    toast(getString(ContainerGame.kind(dir)==ContainerGame.Kind.ZIP?R.string.zip_missing_files:R.string.image_missing_files));return;}
            }else if(!resources.isDirectory()||!ArchiveLanguage.hasBootFiles(GamePatch.resourceLayers(dir,resources))){
                toast(DirectGame.linked(dir)?getString(R.string.game_folder_unreadable):getString(R.string.launch_files_missing));return;
            }
        }catch(IOException error){toast(error.getMessage());return;}
        try {
            GameSettings settings=TranslationRuntime.prepare(dir,resources);
            if(!refreshContainer){
                try(ResourceLayers layers=ResourceLayers.summary(dir,resources)){
                    if(GameEncoding.applyArchiveLanguage(layers,settings))settings.save(dir);
                }
                if(!settings.needsEncodingDetection() && privateLayerReady(dir)) {
                    encodingPrepared(dir,settings);return;
                }
            }
        } catch(IOException error) { toast(getString(R.string.settings_read_failed)+error.getMessage());return; }
        final File input=resources;
        importing=true;importStatus=getString(R.string.encoding_detect_busy);showLibrary();
        new Thread(()->{
            try {
                if(DirectGame.linked(dir)&&DirectGame.privateLayer(dir)==null)
                    try(GameDataLock lock=GameDataLock.acquire(dir)){DirectGame.data(dir,input);}
                GameSettings settings=GameEncoding.prepare(dir,input);
                android.util.Log.i("OpenBGI",AppMessages.encodingStatus(this,settings));
                runOnUiThread(()->{importing=false;if(isDestroyed()||isFinishing())return;showLibrary();encodingPrepared(dir,settings);});
            } catch(IOException error) {
                runOnUiThread(()->{importing=false;if(!isDestroyed()){showLibrary();toast(getString(R.string.encoding_detect_failed)+error.getMessage());}});
            }
        },"OpenBGI-Encoding").start();
    }
    /** Whether the writable layer already exists, so a launch needs no preparation pass. */
    private boolean privateLayerReady(File dir) {
        if(ContainerGame.linked(dir)) return ContainerGame.data(dir)!=null;
        if(!DirectGame.linked(dir)) return true;
        return DirectGame.privateLayer(dir)!=null;
    }
    private void encodingPrepared(File dir,GameSettings settings) {
        if(settings.encodingReady()) { launchGame(dir);return; }
        AlertDialog dialog=new AlertDialog.Builder(this).setTitle(getString(R.string.encoding_selection_needed)).setMessage(AppMessages.encodingStatus(this,settings))
            .setNegativeButton(getString(R.string.cancel),null).setPositiveButton(getString(R.string.game_settings),(d,w)->SettingsDialog.show(this,dir,title(dir),this::showLibrary)).create();
        dialog.show();Ui.style(dialog);
    }
    private void launchGame(File dir) {
        if(GameActivity.COMPARE_PACKAGE.equals(getPackageName())) {
            AlertDialog dialog=new AlertDialog.Builder(this).setTitle(getString(R.string.play_game))
                .setItems(new String[]{getString(R.string.core_android),getString(R.string.core_upstream)},
                    (d,which)->launchGame(dir,which==1)).create();
            dialog.show();Ui.style(dialog);return;
        }
        launchGame(dir,false);
    }
    private void launchGame(File dir,boolean upstream) {
        try {
            write(new File(getFilesDir(),"active-game.txt"),dir.getName());
            try{write(new File(dir,".openbgi/last-played.txt"),Long.toString(System.currentTimeMillis()));}catch(IOException ignored){}
            startActivity(new Intent(this,GameActivity.class).putExtra(GameActivity.UPSTREAM,upstream));
        } catch(IOException error) { toast(error.getMessage()); }
    }
    @Override protected void onActivityResult(int request,int result,Intent data) {
        super.onActivityResult(request,result,data);
        if(request==GameStorage.REQUEST){storageReady();return;}
        if(request>=PATCH_ZIP && request<=PATCH_EXPORT){
            String id=patchGame;patchGame="";
            if(result==RESULT_OK && data!=null && data.getData()!=null && id.matches("[a-z0-9-]{1,79}"))
                runPatchAction(new File(games,id),request,data.getData());
            return;
        }
        if(request==5){
            String id=saveImportGame;saveImportGame="";
            if(result==RESULT_OK && data!=null && data.getData()!=null){
                if(id.matches("[a-z0-9-]{1,79}"))prepareSaveRestore(data.getData(),new File(games,id));
                else toast(getString(R.string.save_import_game_missing));
            }
            return;
        }
        if(request==4){
            String id=saveExportGame;saveExportGame="";
            if(result==RESULT_OK && data!=null && data.getData()!=null){
                if(id.matches("[a-z0-9-]{1,79}"))writeSaveBackup(data.getData(),new File(games,id));
                else {discardExport(data.getData());toast(getString(R.string.save_export_game_missing));}
            }
            return;
        }
        if(request==3 && result!=RESULT_OK)coverTarget=null;
        if(result!=RESULT_OK||data==null||data.getData()==null)return;
        Uri uri=data.getData();
        if(request==3){File target=coverTarget;coverTarget=null;importCover(uri,target);}
        if(request==2) new Thread(()->{
            try(OutputStream out=getContentResolver().openOutputStream(uri)) {
                if(out==null)throw new IOException(getString(R.string.log_write_failed)); CrashLogs.export(this,out); runOnUiThread(()->toast(getString(R.string.logs_exported)));
            } catch(Exception error) { runOnUiThread(()->toast(getString(R.string.export_failed)+error.getMessage())); }
        }).start();
        if(request==IMPORT_FOLDER&&!importing&&!folderImportRunning)beginFolderImport(data);
    }
    private SharedPreferences folderImportState(){
        return getSharedPreferences("folder-import",MODE_PRIVATE);
    }
    private void beginFolderImport(Intent selection){
        Uri tree=selection.getData();
        if(!DocumentsContract.isTreeUri(tree)){toast(getString(R.string.folder_read_error));return;}
        String id=UUID.randomUUID().toString();
        boolean persisted=false;
        if((selection.getFlags()&Intent.FLAG_GRANT_READ_URI_PERMISSION)!=0){
            try{
                getContentResolver().takePersistableUriPermission(tree,Intent.FLAG_GRANT_READ_URI_PERMISSION);
                persisted=folderImportState().edit().putString("tree",tree.toString()).putString("id",id).commit();
                if(!persisted)releaseFolder(tree);
            }catch(SecurityException unavailable){
                // Some providers only offer access for the current session.
            }
        }
        startFolderImport(tree,id,persisted);
    }
    private void resumeFolderImport(){
        SharedPreferences state=folderImportState();
        String id=state.getString("id",""),tree=state.getString("tree","");
        if(!tree.isEmpty()&&id.matches("[0-9a-f-]{36}"))
            startFolderImport(Uri.parse(tree),id,true);
    }
    private void releaseFolder(Uri tree){
        try{getContentResolver().releasePersistableUriPermission(tree,Intent.FLAG_GRANT_READ_URI_PERMISSION);}
        catch(SecurityException unavailable){}
    }
    private void startFolderImport(Uri tree,String id,boolean persisted){
        folderImportRunning=true;
        folderImportScreen=new WeakReference<>(this);
        importing=true;importStatus=getString(R.string.folder_loading);showLibrary();
        new Thread(()->importGame(tree,new File(games,id),persisted),"OpenBGI-FolderImport").start();
    }
    private void finishFolderImport(String message,ProgramCompatibility.Result compatibility){
        runOnUiThread(()->{
            folderImportRunning=false;
            LauncherActivity screen=folderImportScreen.get();
            if(screen==null||screen.isDestroyed())return;
            screen.importing=false;
            screen.showLibrary();
            screen.toast(message);
            if(compatibility!=null)screen.showCompatibility(compatibility);
        });
    }
    private void importGame(Uri tree,File destination,boolean persisted) {
        boolean published=destination.isDirectory();
        String message;
        ProgramCompatibility.Result compatibility=null;
        try {
            final PatchImport.Result[] patch={null};
            if(!published){
                List<GameImport.Archive> files=new ArrayList<>();
                String rootId=DocumentsContract.getTreeDocumentId(tree);
                collect(tree,rootId,"",files,new HashSet<>(),0,false);
                String title=getString(R.string.unnamed_game);
                Uri root=DocumentsContract.buildDocumentUriUsingTree(tree,rootId);
                try(Cursor cursor=getContentResolver().query(root,new String[]{Document.COLUMN_DISPLAY_NAME},null,null,null)) {
                    if(cursor!=null&&cursor.moveToFirst()&&cursor.getString(0)!=null)title=cursor.getString(0);
                }
                String gameTitle=title;
                GameImport.install(destination,files,(done,total)->importProgress(getString(R.string.game_import_progress,done,total)),
                    staging->{
                        patch[0]=PatchImport.process(staging,PatchProfiles.ALL,this::patchProgress);
                        GameSettings settings=GameSettings.load(staging);patch[0].apply(settings);settings.save(staging);
                        defaults(staging,null);
                        write(new File(staging,".openbgi/name.txt"),gameTitle);
                    });
                published=true;
            }
            importProgress(getString(R.string.encoding_detect_busy));
            GameSettings settings=GameEncoding.prepare(destination,destination);
            android.util.Log.i("OpenBGI",AppMessages.encodingStatus(this,settings));
            importProgress(getString(R.string.compat_busy));
            compatibility=scanCompatibility(destination,settings.profile);
            String patchStatus=patch[0]==null||(patch[0].containers==0&&patch[0].profile.isEmpty())?"":AppMessages.patchSummary(this,patch[0]);
            String details=patchStatus.isEmpty()?"":"\n"+patchStatus;
            message=getString(R.string.game_import_done)+details+"\n"+AppMessages.encodingStatus(this,settings);
        } catch(Exception error) {
            if(!published)try {
                GameImport.remove(destination.toPath().resolveSibling(".import-"+destination.getName()));
            }catch(IOException cleanup){error.addSuppressed(cleanup);}
            android.util.Log.w("OpenBGI","Import failed",error);
            message=getString(R.string.game_import_failed)+error.getMessage();
        } finally {
            if(persisted){
                releaseFolder(tree);
                folderImportState().edit().clear().commit();
            }
        }
        finishFolderImport(message,compatibility);
    }
    private void patchProgress(int done,int total){
        importProgress(total==0?getString(R.string.patch_busy):getString(R.string.patch_progress,done,total));
    }
    private void importProgress(String message){
        runOnUiThread(()->{
            LauncherActivity screen=folderImportRunning?folderImportScreen.get():this;
            if(screen==null||screen.isDestroyed())return;
            screen.importStatus=message;
            if(screen.status!=null)screen.status.setText(message);
        });
    }
    private void registerDirect(File selected){
        if(importing)return;
        importing=true;importStatus=getString(R.string.add_direct_busy);showLibrary();
        new Thread(()->{
            File entry=new File(games,UUID.randomUUID().toString());boolean published=false;
            try{
                File source=DirectGame.validate(selected);
                if(DirectGame.find(games,source)!=null)throw new IOException(getString(R.string.directory_exists));
                PatchImport.Result[] result=new PatchImport.Result[1];
                DirectGame.register(entry,source,staging->{
                    File data=DirectGame.data(staging,source);
                    result[0]=PatchImport.process(source,data,PatchProfiles.ALL,this::patchProgress);
                    defaults(staging,null);
                    GameSettings settings=GameSettings.load(source);result[0].apply(settings);settings.save(staging);
                    write(new File(staging,".openbgi/name.txt"),source.getName());
                });
                PatchImport.Result patch=result[0];
                GameSettings settings=GameEncoding.prepare(entry,source);
                published=true;
                ProgramCompatibility.Result compatibility=scanCompatibility(entry,settings.profile);
                runOnUiThread(()->{importing=false;if(isDestroyed())return;showLibrary();toast(String.join("\n",getString(R.string.add_direct_done),AppMessages.patchSummary(this,patch),AppMessages.encodingStatus(this,settings)));showCompatibility(compatibility);});
            }catch(Exception error){
                // Only the metadata entry is ours to remove, never the source.
                if(!published)try{GameImport.remove(entry.toPath());}catch(IOException ignored){}
                android.util.Log.w("OpenBGI","Direct directory failed",error);
                runOnUiThread(()->{importing=false;if(!isDestroyed()){showLibrary();toast(getString(R.string.add_failed)+error.getMessage());}});
            }
        },"OpenBGI-DirectDirectory").start();
    }
    private void registerImage(File selected){registerContainer(selected,ContainerGame.Kind.IMAGE);}
    private void registerZip(File selected){registerContainer(selected,ContainerGame.Kind.ZIP);}
    /** A container stays where it is: only its index, settings and private layer are ours. */
    private void registerContainer(File selected,ContainerGame.Kind kind){
        if(importing)return;
        final boolean zip=kind==ContainerGame.Kind.ZIP;
        importing=true;importStatus=getString(zip?R.string.add_zip_busy:R.string.add_image_busy);showLibrary();
        new Thread(()->{
            File entry=new File(games,UUID.randomUUID().toString());boolean published=false;
            DiscImage.Source source=null;
            try{
                File container=ContainerGame.validate(selected);
                if(ContainerGame.find(games,container)!=null)throw new IOException(getString(zip?R.string.zip_exists:R.string.image_exists));
                try{source=DiscImage.open(container);}
                catch(IOException error){throw new IOException(getString(zip?R.string.zip_unreadable:R.string.image_unreadable)+error.getMessage());}
                if(source==null)throw new IOException(getString(zip?R.string.zip_format:R.string.image_format));
                final DiscImage.Source index=source;
                String label=container.getName();int dot=label.lastIndexOf('.');
                if(dot>0)label=label.substring(0,dot);
                final String name=label;
                ContainerGame.register(entry,container,kind,index,staging->{
                    ContainerGame.data(staging,index);
                    defaults(staging,null);
                    write(new File(staging,".openbgi/name.txt"),name);
                });
                published=true;
                GameSettings settings=GameEncoding.prepare(entry,entry);
                ProgramCompatibility.Result compatibility=scanCompatibility(entry,settings.profile);
                runOnUiThread(()->{importing=false;if(isDestroyed())return;showLibrary();
                    toast(String.join("\n",getString(zip?R.string.add_zip_done:R.string.add_image_done),AppMessages.encodingStatus(this,settings)));
                    showCompatibility(compatibility);});
            }catch(Exception error){
                if(!published)try{GameImport.remove(entry.toPath());}catch(IOException ignored){}
                android.util.Log.w("OpenBGI",zip?"ZIP archive failed":"Disc image failed",error);
                runOnUiThread(()->{importing=false;if(!isDestroyed()){showLibrary();toast(getString(R.string.add_failed)+error.getMessage());}});
            }finally{if(source!=null)source.close();}
        },zip?"OpenBGI-ZipGame":"OpenBGI-DiscImage").start();
    }
    private void patchMenu(File dir){
        if(engineRunning()){toast(getString(R.string.patch_exit));return;}
        try {
            boolean installed=GamePatch.active(dir)!=null;
            File report=new File(dir,".openbgi/patch-check.txt");
            List<String> options=new ArrayList<>(Arrays.asList(getString(R.string.patch_zip),getString(R.string.patch_folder)));
            if(installed){options.add(getString(R.string.patch_export));options.add(getString(R.string.patch_remove));}
            int reportIndex=options.size();
            if(report.isFile())options.add(getString(R.string.patch_check_last));
            AlertDialog menu=new AlertDialog.Builder(this).setTitle(R.string.patch_overlay).setItems(options.toArray(new String[0]),(d,which)->{
                if(which==reportIndex){showPatchCheck(read(report,""),null);return;}
                if(which==3){runPatchAction(dir,0,null);return;}
                int request=which==0?PATCH_ZIP:which==1?PATCH_FOLDER:PATCH_EXPORT;
                Intent pick=new Intent(which==0?Intent.ACTION_OPEN_DOCUMENT:
                    which==1?Intent.ACTION_OPEN_DOCUMENT_TREE:Intent.ACTION_CREATE_DOCUMENT);
                if(which!=1)pick.setType("application/zip").addCategory(Intent.CATEGORY_OPENABLE);
                if(which==0)pick.setType("*/*").putExtra(Intent.EXTRA_MIME_TYPES,
                    new String[]{"application/zip","application/x-zip-compressed","application/octet-stream"});
                if(which==2)pick.putExtra(Intent.EXTRA_TITLE,"OpenBGI-patch.zip");
                patchGame=dir.getName();
                try{startActivityForResult(pick,request);}
                catch(ActivityNotFoundException error){patchGame="";toast(getString(R.string.browser_missing));}
            }).setNegativeButton(R.string.cancel,null).create();
            menu.show();Ui.style(menu);
        }catch(IOException error){toast(getString(R.string.patch_failed)+error.getMessage());}
    }
    private void runPatchAction(File dir,int request,Uri uri){
        if(importing || engineRunning()){
            if(request==PATCH_EXPORT)discardExport(uri);
            toast(getString(R.string.patch_exit));return;
        }
        if(request==PATCH_ZIP||request==PATCH_FOLDER){checkPatch(dir,request,uri);return;}
        importing=true;importStatus=getString(R.string.patch_busy);showLibrary();
        new Thread(()->{
            int message;
            try(GameDataLock lock=GameDataLock.acquire(dir)){
                if(request==PATCH_EXPORT){
                    try(OutputStream out=getContentResolver().openOutputStream(uri)){
                        if(out==null)throw new IOException(getString(R.string.log_write_failed));
                        GamePatch.export(dir,out);
                    }
                    message=R.string.patch_exported;
                }else{
                    GamePatch.remove(dir);
                    invalidatePatchEncoding(dir);
                    message=R.string.patch_removed;
                }
                runOnUiThread(()->{importing=false;if(!isDestroyed()){showLibrary();toast(getString(message));}});
            }catch(IOException|RuntimeException error){
                if(request==PATCH_EXPORT)discardExport(uri);
                runOnUiThread(()->{importing=false;if(!isDestroyed()){showLibrary();toast(getString(R.string.patch_failed)+error.getMessage());}});
            }
        },"OpenBGI-Overlay").start();
    }

    private void checkPatch(File dir,int request,Uri uri){
        importing=true;importStatus=getString(R.string.patch_check_title);showLibrary();
        new Thread(()->{
            GamePatch.Prepared patch=null;
            try(GameDataLock lock=GameDataLock.acquire(dir)){
                PatchDoctor.Result result;
                try{
                    if(request==PATCH_ZIP)
                        patch=GamePatch.prepareZip(dir,()->getContentResolver().openInputStream(uri),this::patchProgress);
                    else{
                        List<GameImport.Archive> files=new ArrayList<>();
                        collect(uri,DocumentsContract.getTreeDocumentId(uri),"",files,new HashSet<>(),0,true);
                        patch=GamePatch.prepare(dir,files,false,this::patchProgress);
                    }
                    importProgress(getString(R.string.patch_check_title));
                    GameSettings settings=GameSettings.load(dir);
                    synchronized(NativeProgramInspector.class){
                        PatchDoctor.Target target=new File(patch.files,GamePatch.MANIFEST).isFile()
                            ?patchTarget(dir,settings):new PatchDoctor.Target("","","");
                        result=PatchDoctor.check(patch.files,settings,target,(family,points)->{
                            try{return NativeProgramInspector.missingGlyphs(family,points);}
                            catch(LinkageError unavailable){return null;}
                        });
                    }
                }catch(GamePatch.NameConflict conflict){
                    result=new PatchDoctor.Result();
                    result.add(PatchDoctor.Status.REJECT,PatchDoctor.Code.NAME_CONFLICT,conflict.path);
                }catch(IOException|RuntimeException error){
                    result=new PatchDoctor.Result();
                    result.add(PatchDoctor.Status.REJECT,PatchDoctor.Code.IMPORT_FAILURE,
                        error.getMessage()==null?error.getClass().getSimpleName():error.getMessage());
                }
                String report=AppMessages.patchCheck(this,result);
                write(new File(dir,".openbgi/patch-check.txt"),report);
                if(result.status==PatchDoctor.Status.REJECT){discardPatch(patch);patch=null;}
                GamePatch.Prepared ready=patch;
                runOnUiThread(()->{
                    if(isDestroyed()||isFinishing()){discardPatch(ready);return;}
                    pendingPatch=ready;
                    if(ready==null){importing=false;showLibrary();}
                    showPatchCheck(report,ready);
                });
                patch=null;
            }catch(IOException error){
                runOnUiThread(()->{importing=false;if(!isDestroyed()){showLibrary();toast(getString(R.string.patch_failed)+error.getMessage());}});
            }finally{discardPatch(patch);}
        },"OpenBGI-PatchCheck").start();
    }

    private PatchDoctor.Target patchTarget(File dir,GameSettings settings){
        byte[] boot=null;
        String[] identity=null;
        try{
            File resources=resourceDirectory(dir);
            boot=ProgramCompatibility.bootProgram(dir,resources,settings);
            File table=ProgramCompatibility.table(dir,resources);
            if(boot!=null)identity=NativeProgramInspector.identify(table.isFile()?table.toString():"",boot);
        }catch(IOException|LinkageError unavailable){}
        return new PatchDoctor.Target(identity==null||identity[0]==null?"":identity[0],
            identity==null||identity[1]==null?"":identity[1],PatchDoctor.bootHash(boot));
    }

    private void showPatchCheck(String report,GamePatch.Prepared ready){
        TextView body=Ui.text(this,report,14,Ui.INK,false);
        body.setTextIsSelectable(true);
        body.setPadding(dp(20),dp(12),dp(20),dp(12));
        ScrollView scroll=new ScrollView(this);scroll.addView(body);
        AlertDialog.Builder builder=new AlertDialog.Builder(this).setTitle(R.string.patch_check_title).setView(scroll);
        if(ready==null)builder.setPositiveButton(R.string.ok,null);
        else builder.setNegativeButton(R.string.cancel,null).setPositiveButton(R.string.patch_check_install,(dialog,which)->{
            pendingPatch=null;
            attachPatch(ready);
        });
        AlertDialog dialog=builder.create();
        if(ready!=null)dialog.setOnDismissListener(d->{
            if(pendingPatch==ready){
                pendingPatch=null;importing=false;
                discardPatch(ready);
                if(!isDestroyed())showLibrary();
            }
        });
        dialog.show();Ui.style(dialog);
    }

    private void attachPatch(GamePatch.Prepared prepared){
        new Thread(()->{
            try(GamePatch.Prepared patch=prepared;GameDataLock lock=GameDataLock.acquire(patch.game)){
                if(engineRunning())throw new IOException(getString(R.string.patch_exit));
                patch.attach();
                invalidatePatchEncoding(patch.game);
                runOnUiThread(()->{importing=false;if(!isDestroyed()){showLibrary();toast(getString(R.string.patch_attached));}});
            }catch(IOException error){
                runOnUiThread(()->{importing=false;if(!isDestroyed()){showLibrary();toast(getString(R.string.patch_failed)+error.getMessage());}});
            }
        },"OpenBGI-AttachPatch").start();
    }

    private static void discardPatch(GamePatch.Prepared patch){
        if(patch==null)return;
        try{patch.close();}catch(IOException error){android.util.Log.w("OpenBGI","Could not remove patch staging",error);}
    }

    private static void invalidatePatchEncoding(File dir)throws IOException{
        GameSettings settings=GameSettings.load(dir);
        if(settings.automaticEncoding){
            settings.encodingDetection="";settings.encodingDetectorVersion=0;settings.save(dir);
        }
    }
    private void processTranslation(File dir){
        if(importing)return;
        if(engineRunning()){toast(getString(R.string.patch_exit));return;}
        importing=true;importStatus=getString(R.string.patch_busy);showLibrary();
        new Thread(()->{
            try(GameDataLock lock=GameDataLock.acquire(lockDirectory(dir))){
                File resources=resourceDirectory(dir);
                PatchImport.Result result=PatchImport.process(resources,DirectGame.data(dir,resources),PatchProfiles.ALL,this::patchProgress);
                GameSettings settings=GameSettings.load(dir);result.apply(settings);settings.save(dir);
                ProgramCompatibility.Result compatibility=scanCompatibility(dir,settings.profile);
                runOnUiThread(()->{importing=false;if(isDestroyed())return;showLibrary();toast(AppMessages.patchSummary(this,result));showCompatibility(compatibility);});
            }catch(Exception error){android.util.Log.w("OpenBGI","Patch import failed",error);runOnUiThread(()->{importing=false;if(isDestroyed())return;showLibrary();toast(getString(R.string.patch_failed)+error.getMessage());});}
        },"OpenBGI-PatchImport").start();
    }
    private void collect(Uri tree,String id,String prefix,List<GameImport.Archive> files,Set<String> visited,int depth,boolean patch) throws IOException {
        if(depth>64 || !visited.add(id))throw new IOException(getString(R.string.folder_loop));
        Uri children=DocumentsContract.buildChildDocumentsUriUsingTree(tree,id);
        List<String[]> directories=new ArrayList<>();
        try(Cursor cursor=getContentResolver().query(children,new String[]{Document.COLUMN_DOCUMENT_ID,Document.COLUMN_DISPLAY_NAME,Document.COLUMN_MIME_TYPE,Document.COLUMN_SIZE},null,null,null)) {
            if(cursor==null)throw new IOException(getString(R.string.folder_read_error)+prefix);
            while(cursor.moveToNext()) {
                String childId=cursor.getString(0),name=cursor.getString(1);
                if(name==null||name.contains("/")||name.contains("\\")||name.equals(".")||name.equals(".."))throw new IOException(getString(R.string.folder_entry_invalid));
                String path=prefix+name;
                if(!patch&&prefix.isEmpty()&&name.equalsIgnoreCase(".openbgi"))continue;
                if(Document.MIME_TYPE_DIR.equals(cursor.getString(2))) {
                    files.add(new GameImport.Archive(path,0,null)); directories.add(new String[]{childId,path+"/"});
                } else {
                    Uri document=DocumentsContract.buildDocumentUriUsingTree(tree,childId);
                    files.add(new GameImport.Archive(path,cursor.isNull(3)?-1:cursor.getLong(3),()->getContentResolver().openInputStream(document)));
                }
                if(files.size()>100000)throw new IOException(getString(R.string.folder_entries_limit));
            }
        }
        for(String[] directory:directories)collect(tree,directory[0],directory[1],files,visited,depth+1,patch);
    }
    @Override public void onBackPressed() { if(!importing)super.onBackPressed(); }
    private void toast(String text) { Toast.makeText(this,text,Toast.LENGTH_LONG).show(); }
}
