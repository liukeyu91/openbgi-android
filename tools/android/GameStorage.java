/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;
import android.Manifest;
import android.app.*;
import android.content.*;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.*;
import android.os.storage.*;
import android.provider.Settings;
import android.widget.*;
import java.io.*;
import java.util.*;
import java.util.function.Consumer;

/** Direct native file access, without staging a second copy of the game. */
final class GameStorage {
    static final int REQUEST=6;
    static boolean granted(Activity activity){
        return Build.VERSION.SDK_INT>=30?Environment.isExternalStorageManager():
            activity.checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE)==PackageManager.PERMISSION_GRANTED;
    }
    static void request(Activity activity){
        if(Build.VERSION.SDK_INT>=30){
            try{activity.startActivityForResult(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                Uri.parse("package:"+activity.getPackageName())),REQUEST);}
            catch(ActivityNotFoundException unavailable){activity.startActivityForResult(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION),REQUEST);}
        }else activity.requestPermissions(new String[]{Manifest.permission.READ_EXTERNAL_STORAGE,Manifest.permission.WRITE_EXTERNAL_STORAGE},REQUEST);
    }
    private static List<File> volumes(Activity activity){
        LinkedHashSet<File> roots=new LinkedHashSet<>();roots.add(Environment.getExternalStorageDirectory());
        if(Build.VERSION.SDK_INT>=30){
            StorageManager manager=(StorageManager)activity.getSystemService(Context.STORAGE_SERVICE);
            if(manager!=null)for(StorageVolume volume:manager.getStorageVolumes())if(volume.getDirectory()!=null)roots.add(volume.getDirectory());
        }else for(File path:activity.getExternalFilesDirs(null))if(path!=null){
            int at=path.getPath().indexOf("/Android/");if(at>0)roots.add(new File(path.getPath().substring(0,at)));
        }
        List<File> result=new ArrayList<>();for(File root:roots)if(root.isDirectory()&&root.canRead())result.add(root);return result;
    }
    static void choose(Activity activity,Consumer<File> selected){
        List<File> roots=volumes(activity);
        if(roots.isEmpty()){Toast.makeText(activity,"未找到可读取的存储位置",Toast.LENGTH_LONG).show();return;}
        LinearLayout box=Ui.column(activity);int pad=Ui.dp(activity,20);box.setPadding(pad,0,pad,0);
        TextView path=Ui.text(activity,"",13,Ui.INK,false);path.setTextIsSelectable(true);box.addView(path);
        TextView help=Ui.text(activity,"直接读取原目录，不复制游戏。存档和展开的汉化资源写入此目录。",12,Ui.MUTED,false);box.addView(help);
        LinearLayout navigation=Ui.row(activity);box.addView(navigation);
        Button up=Ui.button(activity,"上一级",false,()->{}),storage=Ui.button(activity,"存储位置",false,()->{});
        navigation.addView(up,new LinearLayout.LayoutParams(0,-2,1));navigation.addView(storage,new LinearLayout.LayoutParams(0,-2,1));
        ListView list=new ListView(activity);box.addView(list,new LinearLayout.LayoutParams(-1,Ui.dp(activity,300)));
        File[] current={roots.get(0)},boundary={roots.get(0)};List<File> folders=new ArrayList<>();
        Runnable[] refresh={null};
        refresh[0]=()->{
            path.setText(current[0].getPath());folders.clear();File[] children=current[0].listFiles();
            if(children!=null)for(File child:children)if(child.isDirectory())folders.add(child);
            folders.sort(Comparator.comparing(File::getName,String.CASE_INSENSITIVE_ORDER));
            String[] labels=new String[folders.size()];for(int i=0;i<labels.length;i++)labels[i]="▸  "+folders.get(i).getName();
            list.setAdapter(new ArrayAdapter<>(activity,android.R.layout.simple_list_item_1,labels));
            up.setEnabled(!current[0].equals(boundary[0]));
        };
        up.setOnClickListener(v->{if(!current[0].equals(boundary[0])){current[0]=current[0].getParentFile();refresh[0].run();}});
        list.setOnItemClickListener((parent,view,position,id)->{current[0]=folders.get(position);refresh[0].run();});
        storage.setOnClickListener(v->{String[] names=new String[roots.size()];for(int i=0;i<names.length;i++)names[i]=roots.get(i).getPath();
            new AlertDialog.Builder(activity).setTitle("存储位置").setItems(names,(d,at)->{boundary[0]=current[0]=roots.get(at);refresh[0].run();}).show();});
        AlertDialog dialog=new AlertDialog.Builder(activity).setTitle("选择游戏目录").setView(box).setNegativeButton("取消",null).setPositiveButton("使用此目录",null).create();
        dialog.show();Ui.style(dialog);refresh[0].run();
        dialog.getButton(-1).setOnClickListener(v->{
            try{File source=DirectGame.validate(current[0]);dialog.dismiss();selected.accept(source);}
            catch(IOException failure){Toast.makeText(activity,failure.getMessage(),Toast.LENGTH_LONG).show();}
        });
    }
}
