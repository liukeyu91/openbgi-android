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
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
public final class LauncherActivity extends Activity {
    private LinearLayout content,grid;
    private TextView status;
    private ProgressBar progress;
    private File coverTarget;
    private String saveExportGame="",saveImportGame="";
    private String storageAction="";
    private SaveRestore.Prepared pendingRestore;
    private String search="",page="library",importStatus="正在读取目录…";
    private boolean byName;
    private boolean importing;
    private File games;
    private static final String FALLBACK="{\"games\":[{\"match\":\"FriendToLoverHD\",\"profile\":\"1.616\",\"product\":\"FriendToLoverHD\"}]}";
    @Override public void onCreate(Bundle state) {
        super.onCreate(state); games=new File(getFilesDir(),"games"); games.mkdirs();
        getWindow().setStatusBarColor(Ui.BG);getWindow().setNavigationBarColor(Ui.BG);
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR|View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR);
        if(state!=null){page=state.getString("page","library");search=state.getString("search","");byName=state.getBoolean("byName",false);storageAction=state.getString("storageAction","");
            String cover=state.getString("cover","");if(cover.matches("[a-z0-9-]{1,79}"))coverTarget=new File(games,cover);
            String exporting=state.getString("saveExportGame","");if(exporting.matches("[a-z0-9-]{1,79}"))saveExportGame=exporting;
            String restoring=state.getString("saveImportGame","");if(restoring.matches("[a-z0-9-]{1,79}"))saveImportGame=restoring;}
        try { migrate(); } catch(IOException error) { toast("旧版数据迁移失败："+error.getMessage()); }
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
        super.onResume();if(games!=null)showLibrary();
        if(!engineRunning()) {
            String failure=CrashLogs.takeFailureNotice(this);
            if(failure!=null)new AlertDialog.Builder(this).setTitle("运行异常").setMessage(failure)
                .setPositiveButton("导出日志",(dialog,which)->exportLogs()).setNegativeButton("稍后",null).show();
        }
    }
    @Override protected void onSaveInstanceState(Bundle state){super.onSaveInstanceState(state);state.putString("page",page);state.putString("search",search);state.putBoolean("byName",byName);state.putString("saveExportGame",saveExportGame);state.putString("saveImportGame",saveImportGame);state.putString("storageAction",storageAction);if(coverTarget!=null)state.putString("cover",coverTarget.getName());}
    private void addGame(){
        if(importing){toast("请等待当前操作完成");return;}
        AlertDialog dialog=new AlertDialog.Builder(this).setTitle("添加游戏")
            .setItems(new String[]{"直接使用目录（不复制）","复制导入到应用存储"},(d,which)->{
                if(which==0)directAccess("add");else copyGame();
            }).setNegativeButton("取消",null).create();dialog.show();Ui.style(dialog);
    }
    private void directAccess(String action){
        storageAction=action;
        if(GameStorage.granted(this)){storageReady();return;}
        AlertDialog dialog=new AlertDialog.Builder(this).setTitle("允许读取游戏目录")
            .setMessage("开启存储访问后，可直接运行手机或存储卡中的游戏，无需再复制一份。")
            .setNegativeButton("取消",(d,w)->storageAction="").setPositiveButton("去授权",(d,w)->{
                try{GameStorage.request(this);}catch(ActivityNotFoundException unavailable){storageAction="";toast("无法打开存储权限设置，可使用复制导入");}
            }).create();dialog.setOnCancelListener(d->storageAction="");dialog.show();Ui.style(dialog);
    }
    private void storageReady(){
        String action=storageAction;storageAction="";
        if(!GameStorage.granted(this)){toast("未授予存储访问，可选择复制导入");return;}
        if(action.equals("add"))GameStorage.choose(this,this::registerDirect);
        else if(action.startsWith("play:")&&action.substring(5).matches("[a-z0-9-]{1,79}"))play(new File(games,action.substring(5)));
    }
    @Override public void onRequestPermissionsResult(int request,String[] permissions,int[] grants){
        super.onRequestPermissionsResult(request,permissions,grants);if(request==GameStorage.REQUEST)storageReady();
    }
    private void copyGame(){
        Intent picker=new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE).addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        try{startActivityForResult(picker,1);}catch(ActivityNotFoundException e){toast("无法打开系统文件选择器");}
    }
    private void exportLogs(){
        Intent save=new Intent(Intent.ACTION_CREATE_DOCUMENT).setType("application/zip").addCategory(Intent.CATEGORY_OPENABLE);
        save.putExtra(Intent.EXTRA_TITLE,"OpenBGI-logs-"+System.currentTimeMillis()+".zip");
        try{startActivityForResult(save,2);}catch(ActivityNotFoundException e){toast("无法打开系统文件选择器");}
    }
    private void exportSaves(File dir,String name){
        if(importing)return;
        if(engineRunning()){toast("请先存档并退出游戏，再导出存档");return;}
        importing=true;importStatus="正在查找存档…";showLibrary();
        new Thread(()->{
            try(GameDataLock lock=GameDataLock.acquire(resourceDirectory(dir))){
                SaveRestore.recover(resourceDirectory(dir));
                if(SaveBackup.count(resourceDirectory(dir))==0)throw new IOException("未找到可导出的存档或全局进度");
                runOnUiThread(()->{
                    importing=false;if(isDestroyed() || isFinishing())return;showLibrary();
                    saveExportGame=dir.getName();
                    Intent save=new Intent(Intent.ACTION_CREATE_DOCUMENT).setType("application/zip").addCategory(Intent.CATEGORY_OPENABLE);
                    save.putExtra(Intent.EXTRA_TITLE,SaveBackup.filename(name));
                    try{startActivityForResult(save,4);}catch(ActivityNotFoundException error){saveExportGame="";toast("无法打开系统文件选择器");}
                });
            }catch(IOException error){runOnUiThread(()->{importing=false;if(!isDestroyed()){showLibrary();toast(error.getMessage());}});}
        },"OpenBGI-SaveInventory").start();
    }
    private void writeSaveBackup(Uri uri,File dir){
        if(importing || engineRunning()){
            toast("请先退出游戏，并等待当前操作完成，再导出存档");discardExport(uri);return;
        }
        importing=true;importStatus="正在导出存档…";showLibrary();
        String name=title(dir),version=versionName();
        new Thread(()->{
            SaveBackup.Backup backup=null;
            try(GameDataLock lock=GameDataLock.acquire(resourceDirectory(dir))){
                SaveRestore.recover(resourceDirectory(dir));
                backup=SaveBackup.create(resourceDirectory(dir),getCacheDir(),name,version,(done,total)->runOnUiThread(()->{
                    importStatus="正在导出存档："+done+" / "+total;if(status!=null)status.setText(importStatus);
                }));
                if(engineRunning())throw new IOException("游戏正在运行，请退出后重新导出");
                try(InputStream in=new FileInputStream(backup.file);OutputStream out=getContentResolver().openOutputStream(uri,"wt")){
                    if(out==null)throw new IOException("无法写入存档文件");
                    byte[] buffer=new byte[64*1024];int n;while((n=in.read(buffer))!=-1)out.write(buffer,0,n);
                }
                final int count=backup.count;
                runOnUiThread(()->{importing=false;if(!isDestroyed()){showLibrary();toast("存档已导出，共 "+count+" 个文件");}});
            }catch(Exception error){
                discardExport(uri);
                runOnUiThread(()->{importing=false;if(!isDestroyed()){showLibrary();toast("存档导出失败："+error.getMessage());}});
            }finally{if(backup!=null)backup.file.delete();}
        },"OpenBGI-SaveExport").start();
    }
    private void importSaves(File dir){
        if(importing){toast("请等待当前操作完成");return;}
        if(engineRunning()){toast("请先存档并退出游戏，再导入存档");return;}
        saveImportGame=dir.getName();
        Intent picker=new Intent(Intent.ACTION_OPEN_DOCUMENT).setType("*/*").addCategory(Intent.CATEGORY_OPENABLE)
            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        picker.putExtra(Intent.EXTRA_MIME_TYPES,new String[]{"application/zip","application/x-zip-compressed","application/octet-stream"});
        try{startActivityForResult(picker,5);}catch(ActivityNotFoundException error){saveImportGame="";toast("无法打开系统文件选择器");}
    }
    private void prepareSaveRestore(Uri uri,File dir){
        if(importing || engineRunning()){toast("请先退出游戏，并等待当前操作完成，再导入存档");return;}
        importing=true;importStatus="正在校验存档备份…";showLibrary();
        new Thread(()->{
            SaveRestore.Prepared prepared=null;
            try(InputStream in=getContentResolver().openInputStream(uri)){
                if(in==null)throw new IOException("无法读取存档备份");
                prepared=SaveRestore.prepare(in,getCacheDir());final SaveRestore.Prepared ready=prepared;
                runOnUiThread(()->{
                    if(isDestroyed() || isFinishing()){ready.close();return;}
                    pendingRestore=ready;importStatus="存档备份校验完成，等待确认";showLibrary();
                    String message="备份游戏："+ready.gameName+"\n目标游戏："+title(dir)+"\n文件："+ready.count+" 个（"+
                        String.format(Locale.ROOT,"%.1f",ready.bytes/(1024.0*1024.0))+" MB）\n\n将覆盖同名存档和全局进度，备份以外的文件保留。请确认来自同版本游戏。";
                    AlertDialog dialog=new AlertDialog.Builder(this).setTitle("导入存档").setMessage(message)
                        .setNegativeButton("取消",null).setPositiveButton("确认导入",(d,w)->{
                            pendingRestore=null;applySaveRestore(ready,dir);
                        }).create();
                    dialog.setOnDismissListener(d->{if(pendingRestore==ready){pendingRestore=null;ready.close();importing=false;if(!isDestroyed())showLibrary();}});
                    dialog.show();Ui.style(dialog);
                });
                prepared=null;
            }catch(Exception error){
                runOnUiThread(()->{importing=false;if(!isDestroyed()){showLibrary();toast("存档导入失败："+error.getMessage());}});
            }finally{if(prepared!=null)prepared.close();}
        },"OpenBGI-SaveCheck").start();
    }
    private void applySaveRestore(SaveRestore.Prepared prepared,File dir){
        importStatus="正在导入存档…";showLibrary();
        new Thread(()->{
            try(SaveRestore.Prepared restore=prepared;GameDataLock lock=GameDataLock.acquire(resourceDirectory(dir))){
                if(engineRunning())throw new IOException("游戏正在运行，请退出后重新导入");
                restore.apply(resourceDirectory(dir),(done,total)->runOnUiThread(()->{
                    importStatus="正在导入存档："+done+" / "+total;if(status!=null)status.setText(importStatus);
                }));
                runOnUiThread(()->{importing=false;if(!isDestroyed()){showLibrary();toast("存档已导入，共 "+prepared.count+" 个文件");}});
            }catch(Exception error){
                runOnUiThread(()->{importing=false;if(!isDestroyed()){showLibrary();toast("存档导入失败："+error.getMessage());}});
            }
        },"OpenBGI-SaveImport").start();
    }
    @Override protected void onDestroy(){
        if(pendingRestore!=null){pendingRestore.close();pendingRestore=null;}
        super.onDestroy();
    }
    private void discardExport(Uri uri){try{DocumentsContract.deleteDocument(getContentResolver(),uri);}catch(Exception ignored){}}
    private LinearLayout nav(boolean rail){
        LinearLayout bar=rail?Ui.column(this):Ui.row(this);bar.setPadding(dp(8),dp(8),dp(8),dp(8));
        String[] names={"游戏库","使用说明","关于"},icons={"library","guide","info"},pages={"library","guide","about"};
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
        TextView sub=Ui.text(this,"BGI / Ethornell 游戏引擎",11,Ui.MUTED,false);if(!wide)wordmark.addView(sub);header.addView(wordmark,new LinearLayout.LayoutParams(0,-2,1));
        Button add=Ui.button(this,"＋ 添加游戏",true,this::addGame);header.addView(add,new LinearLayout.LayoutParams(-2,dp(46)));root.addView(header);
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
        LinearLayout heading=Ui.row(this);LinearLayout caption=Ui.column(this);caption.addView(Ui.text(this,"游戏库",wide?26:30,Ui.INK,true));Ui.space(caption,wide?5:7);
        caption.addView(Ui.text(this,library.size()+" 个游戏",12,Ui.MUTED,false));
        heading.addView(caption,new LinearLayout.LayoutParams(0,-2,1));
        Button sort=Ui.button(this,byName?"名称 ↑":"最近打开 ↓",false,()->{byName=!byName;showLibrary();});sort.setTextSize(11);sort.setPadding(dp(10),0,dp(10),0);heading.addView(sort,new LinearLayout.LayoutParams(-2,dp(42)));content.addView(heading);Ui.space(content,wide?12:22);
        if(library.isEmpty()){emptyLibrary();return;}
        LinearLayout searchBox=Ui.row(this);searchBox.setBackground(Ui.border(this,Ui.PAPER,14));searchBox.setPadding(dp(14),0,dp(12),0);
        ImageView find=new ImageView(this);find.setImageDrawable(new Ui.Icon("search",Ui.MUTED));searchBox.addView(find,new LinearLayout.LayoutParams(dp(20),dp(20)));
        EditText query=new EditText(this);query.setSingleLine();query.setTextSize(14);query.setTextColor(Ui.INK);query.setHintTextColor(Ui.MUTED);query.setBackgroundColor(0);query.setPadding(dp(10),0,0,0);query.setHint("搜索游戏名称");query.setContentDescription("搜索游戏名称");query.setText(search);
        searchBox.addView(query,new LinearLayout.LayoutParams(0,dp(wide?46:50),1));
        if(wide){LinearLayout.LayoutParams findParams=new LinearLayout.LayoutParams(dp(190),dp(46));findParams.setMargins(dp(12),0,dp(12),0);heading.addView(searchBox,1,findParams);}
        else{content.addView(searchBox);Ui.space(content,20);}
        grid=Ui.column(this);content.addView(grid);renderGames(library);
        query.addTextChangedListener(new TextWatcher(){public void beforeTextChanged(CharSequence s,int a,int n,int f){}public void onTextChanged(CharSequence s,int a,int before,int count){search=s.toString();renderGames(library);}public void afterTextChanged(Editable e){}});
        Ui.space(content,16);TextView foot=Ui.text(this,"每个游戏使用独立的存档和设置。",11,Ui.MUTED,false);foot.setGravity(Gravity.CENTER);content.addView(foot);
    }
    private List<File> library(){
        List<File> list=new ArrayList<>();File[] folders=games.listFiles();if(folders!=null)for(File dir:folders)
            if(dir.isDirectory() && (GameImport.gamePresent(dir)||DirectGame.linked(dir)||dir.getName().equals("external")))list.add(dir);
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
        if(shown==0){TextView none=Ui.text(this,"未找到匹配的游戏："+search,15,Ui.MUTED,false);none.setPadding(0,dp(32),0,dp(32));none.setGravity(Gravity.CENTER);grid.addView(none);}
        else while(shown%columns!=0){LinearLayout.LayoutParams blank=new LinearLayout.LayoutParams(0,1,1);if(shown%columns!=columns-1)blank.rightMargin=dp(14);row.addView(new View(this),blank);shown++;}
    }
    private LinearLayout gameCard(File dir,String name,int index,int columns){
        LinearLayout card=Ui.column(this);card.setBackground(Ui.border(this,Ui.PAPER,20));card.setClipToOutline(true);
        int available=getResources().getConfiguration().screenWidthDp;boolean wide=available>=600;int width=(available-(wide?134:40)-(columns-1)*14)/columns;
        LibraryCover cover=new LibraryCover(this,dir,name);card.addView(cover,new LinearLayout.LayoutParams(-1,dp(Math.max(80,width*(available>=600?38:75)/100))));
        cover.setContentDescription("开始游戏："+name);cover.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_YES);cover.setFocusable(true);cover.setOnClickListener(v->{if(!importing)play(dir);});
        LinearLayout details=Ui.column(this);details.setPadding(dp(14),dp(wide?10:12),dp(10),dp(wide?10:14));card.addView(details);
        LinearLayout top=Ui.row(this);top.setGravity(Gravity.TOP);TextView title=Ui.text(this,name,wide?14:16,Ui.INK,true);title.setMaxLines(2);title.setMinLines(2);title.setEllipsize(TextUtils.TruncateAt.END);
        top.addView(title,new LinearLayout.LayoutParams(0,-2,1));ImageView more=Ui.iconButton(this,"more","管理游戏："+name,()->gameMenu(dir,name));top.addView(more,new LinearLayout.LayoutParams(dp(38),dp(44)));details.addView(top);Ui.space(details,wide?4:8);
        String meta,compatibility;try{GameSettings s=GameSettings.load(dir);meta=s.encodingLabel()+"   ·   "+(s.profile.equals("auto")?"自动识别":s.profile);compatibility=ProgramCompatibility.cachedSummary(dir,resourceDirectory(dir),s.profile,versionName());}catch(IOException e){meta="设置读取失败";compatibility="兼容性：未检查";}
        TextView tag=Ui.text(this,meta,10,Ui.MUTED,false);tag.setMaxLines(1);tag.setEllipsize(TextUtils.TruncateAt.END);details.addView(tag);Ui.space(details,wide?8:14);
        TextView check=Ui.text(this,compatibility,10,Ui.MUTED,false);check.setMaxLines(2);check.setEllipsize(TextUtils.TruncateAt.END);details.addView(check);Ui.space(details,wide?6:10);
        Button play=Ui.button(this,"开始游戏",false,()->{if(!importing)play(dir);});play.setTextColor(Ui.ACCENT);play.setTextSize(12);details.addView(play,new LinearLayout.LayoutParams(-1,dp(wide?40:44)));return card;
    }
    private void emptyLibrary(){
        LinearLayout empty=Ui.column(this);empty.setGravity(Gravity.CENTER);empty.setPadding(dp(24),dp(40),dp(24),dp(36));empty.setBackground(Ui.border(this,Ui.PAPER,22));
        ImageView book=new ImageView(this);book.setImageDrawable(new Ui.Icon("library",Ui.ACCENT));book.setPadding(dp(20),dp(20),dp(20),dp(20));book.setBackground(Ui.shape(this,Ui.TINT,24));empty.addView(book,new LinearLayout.LayoutParams(dp(88),dp(88)));Ui.space(empty,24);
        empty.addView(Ui.text(this,"尚未添加游戏",18,Ui.INK,true));Ui.space(empty,12);
        TextView tip=Ui.text(this,"选择已解压的完整游戏目录。\n直接读取原目录，无需复制游戏。",13,Ui.MUTED,false);tip.setGravity(Gravity.CENTER);empty.addView(tip);Ui.space(empty,26);empty.addView(Ui.button(this,"选择游戏目录",true,this::addGame));content.addView(empty);
    }
    private void infoCard(String title,String text){
        LinearLayout box=Ui.column(this);box.setPadding(dp(20),dp(20),dp(20),dp(20));box.setBackground(Ui.border(this,Ui.PAPER,20));box.addView(Ui.text(this,title,17,Ui.INK,true));Ui.space(box,10);TextView body=Ui.text(this,text,14,Ui.MUTED,false);body.setLineSpacing(dp(6),1);box.addView(body);content.addView(box);Ui.space(content,14);
    }
    private void showGuide(){
        content.addView(Ui.text(this,"使用说明",30,Ui.INK,true));Ui.space(content,8);content.addView(Ui.text(this,"导入游戏后，可分别设置文字编码和操作方式。",13,Ui.MUTED,false));Ui.space(content,24);
        infoCard("添加游戏","优先选择“直接使用目录”：授权存储访问后，直接读取手机或存储卡中的完整游戏目录，不复制游戏本体。存档和展开的汉化资源写在原目录，应用只保存游戏库信息和设置。也可选择“复制导入”使用应用内副本。");
        infoCard("文字编码","导入后会抽样检测脚本文字。无法确定时，请在“游戏设置”中预览并选择编码。日文原版通常使用 SJIS，汉化版可能使用 GBK 或 UTF-8；混合编码和 EXE 内的补丁可能无法正确识别。保存设置后重启游戏。");
        infoCard("触摸与按键","单指点击模拟鼠标左键，双指点击或长按模拟右键。阅读模式中，上下滑动模拟滚轮；拖拽模式可按住左键移动。返回键发送 Esc。");
        infoCard("快进","展开游戏内快捷键，点击“快进”开始，再点“停止快进”结束。默认发送持续按住的 Ctrl，可在游戏设置中修改。点击画面、收起快捷键或离开游戏时停止；能否跳过未读内容取决于游戏设置。");
        infoCard("存档备份","先存档并退出游戏。在游戏菜单中选择“导出存档”保存 ZIP，选择“导入存档”恢复应用生成的备份。导入前会校验文件并提示确认，覆盖同名存档，保留备份以外的文件。请使用同版本游戏的备份。");
        Button add=Ui.button(this,"添加游戏",true,this::addGame);content.addView(add,new LinearLayout.LayoutParams(-1,dp(50)));
    }
    private void showAbout(){
        content.addView(Ui.text(this,"关于 OpenBGI",30,Ui.INK,true));Ui.space(content,8);content.addView(Ui.text(this,"版本 "+versionName(),13,Ui.MUTED,false));Ui.space(content,24);
        infoCard("BGI / Ethornell 引擎","OpenBGI 是 BGI / Ethornell 引擎的 C99 重写，按 GPL-2.0 许可证分发。应用不包含游戏资源，需要自行导入。");
        infoCard("兼容性","Android 版已测试 Fureraba HD、与她＊心渐近的启动、部分对白和读档，尚未验证完整流程。部分游戏仍有显示、编码或性能问题。");
        content.addView(Ui.button(this,"导出运行日志",false,this::exportLogs),new LinearLayout.LayoutParams(-1,dp(50)));Ui.space(content,12);
        content.addView(Ui.button(this,"项目源码",false,()->{try{startActivity(new Intent(Intent.ACTION_VIEW,Uri.parse("https://github.com/liukeyu91/openbgi-android")));}catch(ActivityNotFoundException e){toast("未找到浏览器");}}),new LinearLayout.LayoutParams(-1,dp(50)));
    }
    private String versionName(){
        try{String version=getPackageManager().getPackageInfo(getPackageName(),0).versionName;return version==null?"未知":version;}
        catch(android.content.pm.PackageManager.NameNotFoundException ignored){return "未知";}
    }
    private void gameMenu(File dir,String name) {
        if(importing)return;
        boolean linked=DirectGame.linked(dir);
        AlertDialog menu=new AlertDialog.Builder(this).setTitle(name).setItems(new String[]{"开始游戏","游戏设置","重命名","更换封面",linked?"从游戏库移除":"删除游戏及存档","兼容性检查","导出存档","导入存档","处理汉化补丁"},(d,which)->{
            if(which==0) play(dir);
            if(which==1) SettingsDialog.show(this,dir,name,this::showLibrary);
            if(which==2)rename(dir,name);
            if(which==3){coverTarget=dir;Intent pick=new Intent(Intent.ACTION_OPEN_DOCUMENT).setType("image/*").addCategory(Intent.CATEGORY_OPENABLE);try{startActivityForResult(pick,3);}catch(ActivityNotFoundException e){coverTarget=null;toast("无法打开图片选择器");}}
            if(which==5)checkCompatibility(dir);
            if(which==6)exportSaves(dir,name);
            if(which==7)importSaves(dir);
            if(which==8)processTranslation(dir);
            if(which==4 && engineRunning()) { toast("请先退出正在运行的游戏，再删除游戏"); return; }
            if(which==4) new AlertDialog.Builder(this).setTitle((linked?"移除“":"删除“")+name+"”？").setMessage(linked?"只移除游戏库条目和应用设置。原目录中的游戏、存档和汉化资源全部保留。":"将永久删除此游戏的应用内数据（含存档和设置）。导入源目录会保留。")
                .setNegativeButton("取消",null).setPositiveButton("删除",(dialog,w)->new Thread(()->{
                    try { GameImport.remove(dir.toPath()); runOnUiThread(this::showLibrary); }
                    catch(IOException error) { runOnUiThread(()->toast(error.getMessage())); }
                }).start()).show();
        }).setNegativeButton("取消",null).create();menu.show();Ui.style(menu);
    }
    private void rename(File dir,String name){
        EditText input=new EditText(this);input.setSingleLine();input.setText(name);input.selectAll();input.setFilters(new InputFilter[]{new InputFilter.LengthFilter(100)});
        LinearLayout box=Ui.column(this);box.setPadding(dp(24),dp(8),dp(24),0);box.addView(input);
        AlertDialog dialog=new AlertDialog.Builder(this).setTitle("重命名游戏").setView(box).setNegativeButton("取消",null).setPositiveButton("保存",null).create();dialog.show();Ui.style(dialog);
        dialog.getButton(-1).setOnClickListener(v->{String value=input.getText().toString().trim();if(value.isEmpty()){input.setError("请输入游戏名称");return;}try{write(new File(dir,".openbgi/name.txt"),value);dialog.dismiss();showLibrary();}catch(IOException e){toast(e.getMessage());}});
    }
    private void importCover(Uri uri,File dir){
        if(dir==null || !dir.isDirectory())return;
        new Thread(()->{
            File temp=new File(dir,".openbgi/cover-import.tmp"),encoded=new File(dir,".openbgi/cover.jpg.tmp");Bitmap image=null,scaled=null;
            try{
                try(InputStream in=getContentResolver().openInputStream(uri);OutputStream out=new FileOutputStream(temp)){
                    if(in==null)throw new IOException("无法读取图片");byte[] buf=new byte[65536];int n;long total=0;
                    while((n=in.read(buf))!=-1){total+=n;if(total>20*1024*1024)throw new IOException("请选择小于 20 MB 的图片");out.write(buf,0,n);}
                }
                BitmapFactory.Options options=new BitmapFactory.Options();options.inJustDecodeBounds=true;BitmapFactory.decodeFile(temp.toString(),options);
                if(options.outWidth<=0 || options.outHeight<=0)throw new IOException("无法识别这张图片");options.inSampleSize=1;
                while(options.outWidth/options.inSampleSize>1600 || options.outHeight/options.inSampleSize>1600)options.inSampleSize*=2;
                options.inJustDecodeBounds=false;image=BitmapFactory.decodeFile(temp.toString(),options);if(image==null)throw new IOException("无法解码图片");
                float ratio=Math.min(1,1000f/Math.max(image.getWidth(),image.getHeight()));scaled=Bitmap.createScaledBitmap(image,Math.max(1,Math.round(image.getWidth()*ratio)),Math.max(1,Math.round(image.getHeight()*ratio)),true);
                try(OutputStream out=new FileOutputStream(encoded)){if(!scaled.compress(Bitmap.CompressFormat.JPEG,90,out))throw new IOException("无法保存封面");}
                Files.move(encoded.toPath(),new File(dir,".openbgi/cover.jpg").toPath(),StandardCopyOption.ATOMIC_MOVE,StandardCopyOption.REPLACE_EXISTING);
                runOnUiThread(()->{if(!isDestroyed())showLibrary();});
            }catch(Exception e){runOnUiThread(()->toast("封面更新失败："+e.getMessage()));}
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
        return DirectGame.resources(dir);
    }
    private ProgramCompatibility.Result scanCompatibility(File dir,String profile) {
        File resources;ProgramCompatibility.Result result;
        try{resources=resourceDirectory(dir);}catch(IOException error){result=new ProgramCompatibility.Result();result.error=error.getMessage();return result;}
        try {
            synchronized(NativeProgramInspector.class) {
                result=ProgramCompatibility.scan(dir,resources,profile,new NativeProgramInspector(),(done,total)->{
                    if(done%4!=0 && done!=total)return;
                    runOnUiThread(()->{importStatus="正在检查程序："+done+" / "+total;if(status!=null)status.setText(importStatus);});
                });
            }
        }catch(IOException|RuntimeException|LinkageError error){
            result=new ProgramCompatibility.Result();result.error="无法完成检查："+error.getMessage();
            android.util.Log.w("OpenBGI","Compatibility check failed",error);
        }
        try{ProgramCompatibility.save(dir,resources,profile,versionName(),result);}
        catch(IOException error){result.error="无法保存检查结果："+error.getMessage();}
        return result;
    }
    private void showCompatibility(ProgramCompatibility.Result result) {
        TextView body=Ui.text(this,result.report(),13,Ui.INK,false);body.setTextIsSelectable(true);
        int pad=dp(20);body.setPadding(pad,dp(12),pad,dp(20));
        ScrollView scroll=new ScrollView(this);scroll.addView(body);
        AlertDialog dialog=new AlertDialog.Builder(this).setTitle("兼容性检查").setView(scroll).setPositiveButton("确定",null).create();
        dialog.show();Ui.style(dialog);
    }
    private void checkCompatibility(File dir) {
        if(importing)return;
        importing=true;importStatus="正在检查程序…";showLibrary();
        new Thread(()->{
            ProgramCompatibility.Result result;
            try{result=scanCompatibility(dir,GameSettings.load(dir).profile);}
            catch(IOException error){result=new ProgramCompatibility.Result();result.error="无法读取游戏设置："+error.getMessage();}
            final ProgramCompatibility.Result completed=result;
            runOnUiThread(()->{importing=false;if(isDestroyed()||isFinishing())return;showLibrary();showCompatibility(completed);});
        },"OpenBGI-Compatibility").start();
    }
    private void play(File dir) {
        if(importing)return;
        if(DirectGame.linked(dir)&&!GameStorage.granted(this)){directAccess("play:"+dir.getName());return;}
        File resources;
        try{resources=resourceDirectory(dir);}catch(IOException error){toast(error.getMessage());return;}
        if(!GameImport.gamePresent(resources)){
            toast(DirectGame.linked(dir)?"无法读取原游戏目录，请检查目录位置、存储卡和存储权限。":"未找到游戏启动文件，或文件为空。请选择完整的游戏目录。");return;
        }
        if(engineRunning() && !read(new File(getFilesDir(),"active-game.txt"),"").equals(dir.getName())) {
            toast("请先退出当前游戏，再启动其他游戏"); return;
        }
        // Do not change a running VM's settings or repeat the scan on resume.
        if(engineRunning()){launchGame(dir);return;}
        try {
            GameSettings settings=TranslationRuntime.prepare(dir,resources);
            if(!settings.needsEncodingDetection()) { encodingPrepared(dir,settings);return; }
        } catch(IOException error) { toast("读取设置失败："+error.getMessage());return; }
        final File input=resources;
        importing=true;importStatus="正在检测文字编码…";showLibrary();
        new Thread(()->{
            try {
                GameSettings settings=GameEncoding.prepare(dir,input);
                android.util.Log.i("OpenBGI",settings.encodingStatus());
                runOnUiThread(()->{importing=false;if(isDestroyed()||isFinishing())return;showLibrary();encodingPrepared(dir,settings);});
            } catch(IOException error) {
                runOnUiThread(()->{importing=false;if(!isDestroyed()){showLibrary();toast("编码检测失败："+error.getMessage());}});
            }
        },"OpenBGI-Encoding").start();
    }
    private void encodingPrepared(File dir,GameSettings settings) {
        if(settings.encodingReady()) { launchGame(dir);return; }
        AlertDialog dialog=new AlertDialog.Builder(this).setTitle("需要选择文字编码").setMessage(settings.encodingStatus())
            .setNegativeButton("取消",null).setPositiveButton("游戏设置",(d,w)->SettingsDialog.show(this,dir,title(dir),this::showLibrary)).create();
        dialog.show();Ui.style(dialog);
    }
    private void launchGame(File dir) {
        try {
            write(new File(getFilesDir(),"active-game.txt"),dir.getName());
            try{write(new File(dir,".openbgi/last-played.txt"),Long.toString(System.currentTimeMillis()));}catch(IOException ignored){}
            startActivity(new Intent(this,GameActivity.class));
        } catch(IOException error) { toast(error.getMessage()); }
    }
    @Override protected void onActivityResult(int request,int result,Intent data) {
        super.onActivityResult(request,result,data);
        if(request==GameStorage.REQUEST){storageReady();return;}
        if(request==5){
            String id=saveImportGame;saveImportGame="";
            if(result==RESULT_OK && data!=null && data.getData()!=null){
                if(id.matches("[a-z0-9-]{1,79}"))prepareSaveRestore(data.getData(),new File(games,id));
                else toast("未找到需要导入存档的游戏，请重试");
            }
            return;
        }
        if(request==4){
            String id=saveExportGame;saveExportGame="";
            if(result==RESULT_OK && data!=null && data.getData()!=null){
                if(id.matches("[a-z0-9-]{1,79}"))writeSaveBackup(data.getData(),new File(games,id));
                else {discardExport(data.getData());toast("未找到需要导出存档的游戏，请重试");}
            }
            return;
        }
        if(request==3 && result!=RESULT_OK)coverTarget=null;
        if(result!=RESULT_OK||data==null||data.getData()==null)return;
        Uri uri=data.getData();
        if(request==3){File target=coverTarget;coverTarget=null;importCover(uri,target);}
        if(request==2) new Thread(()->{
            try(OutputStream out=getContentResolver().openOutputStream(uri)) {
                if(out==null)throw new IOException("无法写入日志文件"); CrashLogs.export(this,out); runOnUiThread(()->toast("日志已导出"));
            } catch(Exception error) { runOnUiThread(()->toast("导出失败："+error.getMessage())); }
        }).start();
        if(request==1&&!importing) { importing=true;importStatus="正在读取目录…";showLibrary();new Thread(()->importGame(uri)).start(); }
    }
    private void importGame(Uri tree) {
        File destination=new File(games,UUID.randomUUID().toString()); boolean published=false;
        try {
            List<GameImport.Archive> files=new ArrayList<>();
            collect(tree,DocumentsContract.getTreeDocumentId(tree),"",files,new HashSet<>(),0);
            final PatchImport.Result[] patch={null};
            GameImport.install(destination,files,(done,total)->importProgress("正在导入："+done+" / "+total+" 项"),
                staging->{patch[0]=PatchImport.process(staging,PatchProfiles.ALL,this::importProgress);
                    GameSettings settings=GameSettings.load(staging);patch[0].apply(settings);settings.save(staging);});
            published=true;
            defaults(destination,null);
            String title="游戏 "+new java.text.SimpleDateFormat("MM-dd HH:mm",Locale.ROOT).format(new Date());
            Uri root=DocumentsContract.buildDocumentUriUsingTree(tree,DocumentsContract.getTreeDocumentId(tree));
            try(Cursor cursor=getContentResolver().query(root,new String[]{Document.COLUMN_DISPLAY_NAME},null,null,null)) {
                if(cursor!=null&&cursor.moveToFirst())title=cursor.getString(0);
            }
            write(new File(destination,".openbgi/name.txt"),title==null?"游戏":title);
            runOnUiThread(()->{importStatus="正在检测文字编码…";if(status!=null)status.setText(importStatus);});
            GameSettings settings=GameEncoding.prepare(destination,destination);
            android.util.Log.i("OpenBGI",settings.encodingStatus());
            runOnUiThread(()->{importStatus="正在检查程序…";if(status!=null)status.setText(importStatus);});
            ProgramCompatibility.Result compatibility=scanCompatibility(destination,settings.profile);
            String patchStatus=patch[0]==null||(patch[0].containers==0&&patch[0].profile.isEmpty())?"":patch[0].summary();
            runOnUiThread(()->{ importing=false;if(isDestroyed())return;showLibrary();toast("导入完成。"+patchStatus+settings.encodingStatus());showCompatibility(compatibility); });
        } catch(Exception error) {
            if(!published)try { GameImport.remove(destination.toPath()); } catch(IOException ignored) {}
            android.util.Log.w("OpenBGI","Import failed",error);
            runOnUiThread(()->{ importing=false; showLibrary(); toast("导入失败："+error.getMessage()); });
        }
    }
    private void importProgress(String message){
        runOnUiThread(()->{importStatus=message;if(!isDestroyed()&&status!=null)status.setText(message);});
    }
    private void registerDirect(File selected){
        if(importing)return;
        importing=true;importStatus="正在添加游戏，不复制文件…";showLibrary();
        new Thread(()->{
            File entry=new File(games,UUID.randomUUID().toString());boolean published=false;
            try{
                File source=DirectGame.validate(selected);
                if(DirectGame.find(games,source)!=null)throw new IOException("该目录已在游戏库中");
                PatchImport.Result patch;
                try(GameDataLock lock=GameDataLock.acquire(source)){
                    patch=PatchImport.process(source,PatchProfiles.ALL,this::importProgress);
                    DirectGame.register(entry,source);published=true;
                    defaults(entry,null);
                    GameSettings settings=GameSettings.load(source);patch.apply(settings);settings.save(entry);
                    write(new File(entry,".openbgi/name.txt"),source.getName());
                }
                GameSettings settings=GameEncoding.prepare(entry,source);
                ProgramCompatibility.Result compatibility=scanCompatibility(entry,settings.profile);
                runOnUiThread(()->{importing=false;if(isDestroyed())return;showLibrary();toast("已添加，直接使用原目录。"+patch.summary()+settings.encodingStatus());showCompatibility(compatibility);});
            }catch(Exception error){
                // Only the metadata entry is ours to remove, never the source.
                if(!published)try{GameImport.remove(entry.toPath());}catch(IOException ignored){}
                android.util.Log.w("OpenBGI","Direct directory failed",error);
                runOnUiThread(()->{importing=false;if(!isDestroyed()){showLibrary();toast("添加失败："+error.getMessage());}});
            }
        },"OpenBGI-DirectDirectory").start();
    }
    private void processTranslation(File dir){
        if(importing)return;
        if(engineRunning()){toast("请先存档并退出游戏，再处理汉化补丁");return;}
        importing=true;importStatus="正在检测汉化补丁…";showLibrary();
        new Thread(()->{
            try(GameDataLock lock=GameDataLock.acquire(resourceDirectory(dir))){
                PatchImport.Result result=PatchImport.process(resourceDirectory(dir),PatchProfiles.ALL,this::importProgress);
                GameSettings settings=GameSettings.load(dir);result.apply(settings);settings.save(dir);
                ProgramCompatibility.Result compatibility=scanCompatibility(dir,settings.profile);
                runOnUiThread(()->{importing=false;if(isDestroyed())return;showLibrary();toast(result.summary());showCompatibility(compatibility);});
            }catch(Exception error){android.util.Log.w("OpenBGI","Patch import failed",error);runOnUiThread(()->{importing=false;if(isDestroyed())return;showLibrary();toast("汉化处理失败："+error.getMessage());});}
        },"OpenBGI-PatchImport").start();
    }
    private void collect(Uri tree,String id,String prefix,List<GameImport.Archive> files,Set<String> visited,int depth) throws IOException {
        if(depth>64 || !visited.add(id))throw new IOException("目录过深或循环引用");
        Uri children=DocumentsContract.buildChildDocumentsUriUsingTree(tree,id);
        List<String[]> directories=new ArrayList<>();
        try(Cursor cursor=getContentResolver().query(children,new String[]{Document.COLUMN_DOCUMENT_ID,Document.COLUMN_DISPLAY_NAME,Document.COLUMN_MIME_TYPE,Document.COLUMN_SIZE},null,null,null)) {
            if(cursor==null)throw new IOException("无法读取目录："+prefix);
            while(cursor.moveToNext()) {
                String childId=cursor.getString(0),name=cursor.getString(1);
                if(name==null||name.contains("/")||name.contains("\\")||name.equals(".")||name.equals(".."))throw new IOException("非法目录项");
                String path=prefix+name;
                if(prefix.isEmpty()&&name.equals(".openbgi"))throw new IOException(".openbgi 为应用配置保留目录");
                if(Document.MIME_TYPE_DIR.equals(cursor.getString(2))) {
                    files.add(new GameImport.Archive(path,0,null)); directories.add(new String[]{childId,path+"/"});
                } else {
                    Uri document=DocumentsContract.buildDocumentUriUsingTree(tree,childId);
                    files.add(new GameImport.Archive(path,cursor.isNull(3)?-1:cursor.getLong(3),()->getContentResolver().openInputStream(document)));
                }
                if(files.size()>100000)throw new IOException("目录项过多");
            }
        }
        for(String[] directory:directories)collect(tree,directory[0],directory[1],files,visited,depth+1);
    }
    @Override public void onBackPressed() { if(!importing)super.onBackPressed(); }
    private void toast(String text) { Toast.makeText(this,text,Toast.LENGTH_LONG).show(); }
}
