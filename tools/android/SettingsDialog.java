/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;

import android.app.*;
import android.content.res.ColorStateList;
import android.text.TextUtils;
import android.view.*;
import android.widget.*;
import java.io.*;
import java.util.*;

/** Per-game settings, edited in memory and committed only by Save. */
final class SettingsDialog {
    private static final int[] KEYS={0,0x1b,0x0d,0x20,0x11,0x21,0x22,0x53,0x4c,0x41,0x48,0x51,0x57,0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x7b};
    private static final String[] LABELS={"滚轮向上","Esc","Enter","Space","Ctrl","PageUp","PageDown","S","L","A","H","Q","W","F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12"};
    private static int index(int key){for(int i=0;i<KEYS.length;i++)if(KEYS[i]==key)return i;return -1;}
    private static final class Choice extends LinearLayout {
        final String[] options;final TextView value;int selected;
        Choice(Activity a,String label,String[] options,int selected){
            super(a);this.options=options;this.selected=Math.max(0,Math.min(options.length-1,selected));
            int pad=Ui.dp(a,16);setGravity(Gravity.CENTER_VERTICAL);setPadding(pad,Ui.dp(a,12),pad,Ui.dp(a,12));setBackground(Ui.touch(a,Ui.PAPER,14));setFocusable(true);
            LinearLayout labels=Ui.column(a);labels.addView(Ui.text(a,label,12,Ui.MUTED,false));Ui.space(labels,5);
            value=Ui.text(a,options[this.selected],14,Ui.INK,true);value.setMaxLines(2);labels.addView(value);addView(labels,new LinearLayout.LayoutParams(0,-2,1));
            ImageView arrow=new ImageView(a);arrow.setImageDrawable(new Ui.Icon("down",Ui.MUTED));addView(arrow,new LinearLayout.LayoutParams(Ui.dp(a,18),Ui.dp(a,18)));
            setContentDescription(label+"："+options[this.selected]);setOnClickListener(v->{
                AlertDialog dialog=new AlertDialog.Builder(a).setTitle(label).setSingleChoiceItems(options,this.selected,(d,at)->{select(at);d.dismiss();}).setNegativeButton("取消",null).create();dialog.show();Ui.style(dialog);
            });
        }
        void select(int at){selected=at;value.setText(options[at]);setContentDescription(((TextView)((LinearLayout)getChildAt(0)).getChildAt(0)).getText()+"："+options[at]);}
    }
    private static Choice choose(Activity a,LinearLayout box,String label,String[] options,int selected){Choice choice=new Choice(a,label,options,selected);LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(-1,-2);p.bottomMargin=Ui.dp(a,2);box.addView(choice,p);return choice;}
    private static Choice key(Activity a,LinearLayout box,String label,int vk,boolean wheel){
        ArrayList<String> labels=new ArrayList<>(Arrays.asList(LABELS));if(index(vk)<0)labels.add("键码 "+vk);if(!wheel)labels.remove(0);
        int selected=index(vk);if(selected<0)selected=labels.size()-1;else if(!wheel)selected--;
        return choose(a,box,label,labels.toArray(new String[0]),selected);
    }
    private static int selectedKey(Choice choice,boolean wheel,int previous){int i=choice.selected+(wheel?0:1);return i<KEYS.length?KEYS[i]:previous;}
    private static LinearLayout group(Activity a,LinearLayout parent,String title){parent.addView(Ui.section(a,title));LinearLayout group=Ui.column(a);group.setPadding(Ui.dp(a,2),Ui.dp(a,2),Ui.dp(a,2),Ui.dp(a,2));group.setBackground(Ui.border(a,Ui.PAPER,17));parent.addView(group);return group;}
    private static Switch toggle(Activity a,LinearLayout parent,String title,String description,boolean checked){
        LinearLayout row=Ui.row(a);row.setPadding(Ui.dp(a,16),Ui.dp(a,14),Ui.dp(a,16),Ui.dp(a,14));LinearLayout label=Ui.column(a);label.addView(Ui.text(a,title,14,Ui.INK,true));Ui.space(label,5);label.addView(Ui.text(a,description,11,Ui.MUTED,false));row.addView(label,new LinearLayout.LayoutParams(0,-2,1));
        Switch s=new Switch(a);s.setChecked(checked);s.setContentDescription(title);s.setShowText(false);int[][] states={{android.R.attr.state_checked},{}};s.setThumbTintList(new ColorStateList(states,new int[]{Ui.ACCENT,0xffa7afa7}));s.setTrackTintList(new ColorStateList(states,new int[]{0xffb9d2c2,Ui.LINE}));row.addView(s);row.setOnClickListener(v->s.setChecked(!s.isChecked()));parent.addView(row);return s;
    }
    static void show(Activity a,File game,String name,Runnable saved){
        final GameSettings settings;try{settings=GameSettings.load(game);}catch(IOException error){Toast.makeText(a,error.getMessage(),Toast.LENGTH_LONG).show();return;}
        Dialog dialog=new Dialog(a);dialog.requestWindowFeature(Window.FEATURE_NO_TITLE);LinearLayout root=Ui.column(a);root.setBackground(Ui.shape(a,Ui.BG,24));
        LinearLayout header=Ui.row(a);header.setPadding(Ui.dp(a,22),Ui.dp(a,22),Ui.dp(a,14),Ui.dp(a,16));LinearLayout heading=Ui.column(a);heading.addView(Ui.text(a,"游戏设置",25,Ui.INK,true));Ui.space(heading,6);
        TextView subtitle=Ui.text(a,name,12,Ui.MUTED,false);subtitle.setMaxLines(1);subtitle.setEllipsize(TextUtils.TruncateAt.END);heading.addView(subtitle);header.addView(heading,new LinearLayout.LayoutParams(0,-2,1));header.addView(Ui.iconButton(a,"back","关闭设置",dialog::dismiss));root.addView(header);
        ScrollView scroll=new ScrollView(a);LinearLayout box=Ui.column(a);box.setPadding(Ui.dp(a,22),0,Ui.dp(a,22),Ui.dp(a,22));scroll.addView(box);root.addView(scroll,new LinearLayout.LayoutParams(-1,0,1));
        LinearLayout text=group(a,box,"文字与引擎");String[] enc={"auto","sjis","gbk","utf8"};Choice encoding=choose(a,text,"文字编码",new String[]{"自动识别","SJIS","GBK","UTF-8"},settings.automaticEncoding?0:Arrays.asList(enc).indexOf(settings.encoding));
        TextView encodingStatus=Ui.text(a,settings.encodingStatus(),11,Ui.MUTED,false);encodingStatus.setPadding(Ui.dp(a,16),Ui.dp(a,6),Ui.dp(a,16),Ui.dp(a,10));text.addView(encodingStatus);
        Button probe=Ui.button(a,"编码预览",false,()->EncodingPreview.show(a,game,value->encoding.select(Arrays.asList(enc).indexOf(value))));LinearLayout.LayoutParams probeParams=new LinearLayout.LayoutParams(-1,Ui.dp(a,46));probeParams.setMargins(Ui.dp(a,12),Ui.dp(a,4),Ui.dp(a,12),Ui.dp(a,12));text.addView(probe,probeParams);
        String[] profiles=EngineProfiles.NAMES.clone();profiles[0]="自动识别";Choice profile=choose(a,text,"引擎版本",profiles,Arrays.asList(EngineProfiles.NAMES).indexOf(settings.profile));
        LinearLayout display=group(a,box,"显示与操作");Choice scale=choose(a,display,"画面缩放",new String[]{"保持比例","拉伸全屏","整数倍（过大时等比缩小）"},settings.scale);Choice touch=choose(a,display,"触摸方式",new String[]{"阅读：点击左键，滑动滚轮","拖拽：按住左键移动"},settings.touch);
        Switch virtual=toggle(a,display,"屏幕按键","显示游戏内快捷键按钮",settings.virtualKeys),swap=toggle(a,display,"交换手柄 A / B","交换 A、B 对应的 Enter 和 Esc",settings.swapAB);
        box.addView(Ui.section(a,"按键映射"));LinearLayout mappings=Ui.column(a);mappings.setBackground(Ui.border(a,Ui.PAPER,17));Choice save=key(a,mappings,"存档",settings.saveKey,false),load=key(a,mappings,"读档",settings.loadKey,false),backlog=key(a,mappings,"回看",settings.backlogKey,true),menu=key(a,mappings,"菜单",settings.menuKey,false);
        Choice skip=key(a,mappings,"快进（点按切换）",settings.skipKey,false);
        Button expand=Ui.button(a,"展开按键映射  ⌄",false,()->{});expand.setOnClickListener(v->{boolean show=mappings.getVisibility()!=View.VISIBLE;mappings.setVisibility(show?View.VISIBLE:View.GONE);expand.setText(show?"收起按键映射  ⌃":"展开按键映射  ⌄");});box.addView(expand,new LinearLayout.LayoutParams(-1,Ui.dp(a,48)));Ui.space(box,8);mappings.setVisibility(View.GONE);box.addView(mappings);
        Ui.space(box,18);box.addView(Ui.text(a,"自动识别会抽样读取脚本文字，无法确定时需手动选择。\n设置仅对当前游戏生效，保存后需重启游戏。\n存档、读档等快捷键是否有效，取决于游戏的按键设置。",11,Ui.MUTED,false));
        LinearLayout footer=Ui.row(a);footer.setPadding(Ui.dp(a,22),Ui.dp(a,12),Ui.dp(a,22),Ui.dp(a,18));footer.addView(Ui.button(a,"取消",false,dialog::dismiss),new LinearLayout.LayoutParams(0,Ui.dp(a,48),1));
        Button commit=Ui.button(a,"保存设置",true,()->{
            settings.chooseEncoding(enc[encoding.selected]);settings.profile=EngineProfiles.NAMES[profile.selected];settings.scale=scale.selected;settings.touch=touch.selected;settings.virtualKeys=virtual.isChecked();settings.swapAB=swap.isChecked();
            settings.saveKey=selectedKey(save,false,settings.saveKey);settings.loadKey=selectedKey(load,false,settings.loadKey);settings.backlogKey=selectedKey(backlog,true,settings.backlogKey);settings.menuKey=selectedKey(menu,false,settings.menuKey);
            settings.skipKey=selectedKey(skip,false,settings.skipKey);
            try{settings.save(game);dialog.dismiss();saved.run();}catch(IOException error){Toast.makeText(a,error.getMessage(),Toast.LENGTH_LONG).show();}
        });LinearLayout.LayoutParams commitParams=new LinearLayout.LayoutParams(0,Ui.dp(a,48),1);commitParams.leftMargin=Ui.dp(a,12);footer.addView(commit,commitParams);root.addView(footer);dialog.setContentView(root);dialog.show();
        Window window=dialog.getWindow();if(window!=null){window.setBackgroundDrawableResource(android.R.color.transparent);android.util.DisplayMetrics m=a.getResources().getDisplayMetrics();window.setLayout(Math.min(Ui.dp(a,620),m.widthPixels-Ui.dp(a,24)),Math.min(Ui.dp(a,820),(int)(m.heightPixels*.90f)));window.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);}
    }
}
