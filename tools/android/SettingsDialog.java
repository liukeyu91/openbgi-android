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
    private static final String[] LABELS={"Esc","Enter","Space","Ctrl","PageUp","PageDown","S","L","A","H","Q","W","F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12"};
    private static int index(int key){for(int i=0;i<KEYS.length;i++)if(KEYS[i]==key)return i;return -1;}
    private static final class Choice extends LinearLayout {
        final String[] options;final TextView value;int selected;
        Runnable changed=()->{};
        Choice(Activity a,String label,String[] options,int selected){
            super(a);this.options=options;this.selected=Math.max(0,Math.min(options.length-1,selected));
            int pad=Ui.dp(a,16);setGravity(Gravity.CENTER_VERTICAL);setPadding(pad,Ui.dp(a,12),pad,Ui.dp(a,12));setBackground(Ui.touch(a,Ui.PAPER,14));setFocusable(true);
            LinearLayout labels=Ui.column(a);labels.addView(Ui.text(a,label,12,Ui.MUTED,false));Ui.space(labels,5);
            value=Ui.text(a,options[this.selected],14,Ui.INK,true);value.setMaxLines(2);labels.addView(value);addView(labels,new LinearLayout.LayoutParams(0,-2,1));
            ImageView arrow=new ImageView(a);arrow.setImageDrawable(new Ui.Icon("down",Ui.MUTED));addView(arrow,new LinearLayout.LayoutParams(Ui.dp(a,18),Ui.dp(a,18)));
            setContentDescription(a.getString(R.string.compat_finding,label,options[this.selected]));setOnClickListener(v->{
                AlertDialog dialog=new AlertDialog.Builder(a).setTitle(label).setSingleChoiceItems(options,this.selected,(d,at)->{select(at);d.dismiss();}).setNegativeButton(a.getString(R.string.cancel),null).create();dialog.show();Ui.style(dialog);
            });
        }
        void select(int at){if(selected==at)return;selected=at;value.setText(options[at]);setContentDescription(getContext().getString(R.string.compat_finding,((TextView)((LinearLayout)getChildAt(0)).getChildAt(0)).getText(),options[at]));changed.run();}
    }
    private static Choice choose(Activity a,LinearLayout box,String label,String[] options,int selected){Choice choice=new Choice(a,label,options,selected);LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(-1,-2);p.bottomMargin=Ui.dp(a,2);box.addView(choice,p);return choice;}
    private static Choice key(Activity a,LinearLayout box,String label,int vk,boolean wheel){
        ArrayList<String> labels=new ArrayList<>(Arrays.asList(LABELS));if(wheel)labels.add(0,a.getString(R.string.wheel_up));
        if(index(vk)<0)labels.add(a.getString(R.string.key_code,vk));
        int selected=index(vk);if(selected<0)selected=labels.size()-1;else if(!wheel)selected--;
        return choose(a,box,label,labels.toArray(new String[0]),selected);
    }
    private static int selectedKey(Choice choice,boolean wheel,int previous){int i=choice.selected+(wheel?0:1);return i<KEYS.length?KEYS[i]:previous;}
    private static LinearLayout group(Activity a,LinearLayout parent,String title){parent.addView(Ui.section(a,title));LinearLayout group=Ui.column(a);group.setPadding(Ui.dp(a,2),Ui.dp(a,2),Ui.dp(a,2),Ui.dp(a,2));group.setBackground(Ui.border(a,Ui.PAPER,17));parent.addView(group);return group;}
    private static Switch toggle(Activity a,LinearLayout parent,String title,String description,boolean checked){
        LinearLayout row=Ui.row(a);row.setPadding(Ui.dp(a,16),Ui.dp(a,14),Ui.dp(a,16),Ui.dp(a,14));LinearLayout label=Ui.column(a);label.addView(Ui.text(a,title,14,Ui.INK,true));
        if(description!=null){Ui.space(label,5);label.addView(Ui.text(a,description,11,Ui.MUTED,false));}
        row.addView(label,new LinearLayout.LayoutParams(0,-2,1));
        Switch s=new Switch(a);s.setChecked(checked);s.setContentDescription(title);s.setShowText(false);int[][] states={{android.R.attr.state_checked},{}};s.setThumbTintList(new ColorStateList(states,new int[]{Ui.ACCENT,0xffa7afa7}));s.setTrackTintList(new ColorStateList(states,new int[]{0xffb9d2c2,Ui.LINE}));row.addView(s);row.setOnClickListener(v->s.setChecked(!s.isChecked()));parent.addView(row);return s;
    }
    static void show(Activity a,File game,String name,Runnable saved){
        final GameSettings settings;final ResourceLayers layers;final File resources;try{
            File dir=DirectGame.resources(game);
            if(game.getName().equals("external")){File external=a.getExternalFilesDir(null);if(external!=null)dir=new File(external,"game");}
            resources=dir;
            layers=ResourceLayers.summary(game,dir);
            settings=TranslationRuntime.prepare(game,dir);
            GameEncoding.applyArchiveLanguage(layers,settings);
        }catch(IOException error){Toast.makeText(a,error.getMessage(),Toast.LENGTH_LONG).show();return;}
        Dialog dialog=new Dialog(a);dialog.requestWindowFeature(Window.FEATURE_NO_TITLE);LinearLayout root=Ui.column(a);root.setBackground(Ui.shape(a,Ui.BG,24));
        LinearLayout header=Ui.row(a);header.setPadding(Ui.dp(a,22),Ui.dp(a,22),Ui.dp(a,14),Ui.dp(a,16));LinearLayout heading=Ui.column(a);heading.addView(Ui.text(a,a.getString(R.string.game_settings),25,Ui.INK,true));Ui.space(heading,6);
        TextView subtitle=Ui.text(a,name,12,Ui.MUTED,false);subtitle.setMaxLines(1);subtitle.setEllipsize(TextUtils.TruncateAt.END);heading.addView(subtitle);header.addView(heading,new LinearLayout.LayoutParams(0,-2,1));header.addView(Ui.iconButton(a,"back",a.getString(R.string.close_settings),dialog::dismiss));root.addView(header);
        ScrollView scroll=new ScrollView(a);LinearLayout box=Ui.column(a);box.setPadding(Ui.dp(a,22),0,Ui.dp(a,22),Ui.dp(a,22));scroll.addView(box);root.addView(scroll,new LinearLayout.LayoutParams(-1,0,1));
        LinearLayout text=group(a,box,a.getString(R.string.text_engine));
        List<ArchiveLanguage> languages=new ArrayList<>(layers.available());
        if(!languages.isEmpty() && layers.hasDefault())languages.add(0,null);
        int languageIndex=languages.indexOf(ArchiveLanguage.fromId(settings.archiveLanguage));
        String[] languageLabels=languages.stream().map(language->language==null?a.getString(R.string.game_default):language.label).toArray(String[]::new);
        Choice language=languages.size()>1?choose(a,text,a.getString(R.string.game_language),languageLabels,languageIndex):null;
        String[] enc={"auto","sjis","gbk","utf8","big5"};
        Choice encoding=choose(a,text,a.getString(R.string.text_encoding),new String[]{a.getString(R.string.auto_detect),"SJIS","GBK","UTF-8","Big5 (CP950)"},settings.automaticEncoding?0:Arrays.asList(enc).indexOf(settings.encoding));
        TextView encodingStatus=Ui.text(a,AppMessages.encodingStatus(a,settings),11,Ui.MUTED,false);encodingStatus.setPadding(Ui.dp(a,16),Ui.dp(a,6),Ui.dp(a,16),Ui.dp(a,10));text.addView(encodingStatus);
        java.util.function.Supplier<ArchiveLanguage> selectedLanguage=()->language==null?ArchiveLanguage.fromId(settings.archiveLanguage):languages.get(language.selected);
        if(language!=null)language.changed=()->{
            encoding.select(0);
            ArchiveLanguage selected=selectedLanguage.get();
            encodingStatus.setText(selected==null?a.getString(R.string.encoding_detect_pending):
                a.getString(R.string.encoding_detected,AppMessages.encodingName(selected.encoding)));
        };
        Button probe=Ui.button(a,a.getString(R.string.encoding_preview),false,()->EncodingPreview.show(a,game,resources,selectedLanguage.get(),value->encoding.select(Arrays.asList(enc).indexOf(value))));LinearLayout.LayoutParams probeParams=new LinearLayout.LayoutParams(-1,Ui.dp(a,46));probeParams.setMargins(Ui.dp(a,12),Ui.dp(a,4),Ui.dp(a,12),Ui.dp(a,12));text.addView(probe,probeParams);
        String[] profiles=EngineProfiles.NAMES.clone();profiles[0]=a.getString(R.string.auto_detect);Choice profile=choose(a,text,a.getString(R.string.engine_version),profiles,Arrays.asList(EngineProfiles.NAMES).indexOf(settings.profile));
        ArrayList<String> fontNames=new ArrayList<>(Arrays.asList("","Noto Sans CJK SC","Noto Serif CJK SC","Noto Sans CJK TC","Noto Serif CJK TC","Noto Sans CJK JP","Noto Serif CJK JP"));
        if(!fontNames.contains(settings.fontFamily))fontNames.add(settings.fontFamily);
        String[] fontLabels=fontNames.toArray(new String[0]);fontLabels[0]=a.getString(R.string.game_default);
        Choice font=choose(a,text,a.getString(R.string.font_override),fontLabels,fontNames.indexOf(settings.fontFamily));
        LinearLayout display=group(a,box,a.getString(R.string.display_controls));Choice scale=choose(a,display,a.getString(R.string.display_scale),new String[]{a.getString(R.string.scale_fit),a.getString(R.string.scale_stretch),a.getString(R.string.scale_integer)},settings.scale);Choice touch=choose(a,display,a.getString(R.string.touch_mode),new String[]{a.getString(R.string.touch_reading),a.getString(R.string.touch_drag)},settings.touch);
        Switch virtual=toggle(a,display,a.getString(R.string.on_screen_keys),null,settings.virtualKeys),swap=toggle(a,display,a.getString(R.string.swap_buttons),a.getString(R.string.swap_buttons_help),settings.swapAB);
        box.addView(Ui.section(a,a.getString(R.string.key_mapping)));LinearLayout mappings=Ui.column(a);mappings.setBackground(Ui.border(a,Ui.PAPER,17));Choice save=key(a,mappings,a.getString(R.string.quick_save),settings.saveKey,false),load=key(a,mappings,a.getString(R.string.quick_load),settings.loadKey,false),backlog=key(a,mappings,a.getString(R.string.backlog),settings.backlogKey,true),menu=key(a,mappings,a.getString(R.string.menu),settings.menuKey,false);
        Choice skip=key(a,mappings,a.getString(R.string.skip_toggle),settings.skipKey,false);
        Button expand=Ui.button(a,a.getString(R.string.show_key_mapping),false,()->{});expand.setOnClickListener(v->{boolean show=mappings.getVisibility()!=View.VISIBLE;mappings.setVisibility(show?View.VISIBLE:View.GONE);expand.setText(show?a.getString(R.string.hide_key_mapping):a.getString(R.string.show_key_mapping));});box.addView(expand,new LinearLayout.LayoutParams(-1,Ui.dp(a,48)));Ui.space(box,8);mappings.setVisibility(View.GONE);box.addView(mappings);
        Ui.space(box,18);box.addView(Ui.text(a,a.getString(R.string.settings_help),11,Ui.MUTED,false));
        LinearLayout footer=Ui.row(a);footer.setPadding(Ui.dp(a,22),Ui.dp(a,12),Ui.dp(a,22),Ui.dp(a,18));footer.addView(Ui.button(a,a.getString(R.string.cancel),false,dialog::dismiss),new LinearLayout.LayoutParams(0,Ui.dp(a,48),1));
        Button commit=Ui.button(a,a.getString(R.string.save_settings),true,()->{
            settings.chooseEncoding(enc[encoding.selected]);settings.profile=EngineProfiles.NAMES[profile.selected];settings.scale=scale.selected;settings.touch=touch.selected;settings.virtualKeys=virtual.isChecked();settings.swapAB=swap.isChecked();
            ArchiveLanguage selected=selectedLanguage.get();
            String id=selected==null?"":selected.id;
            if(!id.equals(settings.archiveLanguage)){
                settings.encodingDetection="";settings.encodingDetectorVersion=0;
                settings.archiveLanguage=id;
            }
            GameEncoding.applyArchiveLanguage(layers,settings);
            settings.fontFamily=fontNames.get(font.selected);
            settings.saveKey=selectedKey(save,false,settings.saveKey);settings.loadKey=selectedKey(load,false,settings.loadKey);settings.backlogKey=selectedKey(backlog,true,settings.backlogKey);settings.menuKey=selectedKey(menu,false,settings.menuKey);
            settings.skipKey=selectedKey(skip,false,settings.skipKey);
            try{settings.save(game);dialog.dismiss();saved.run();}catch(IOException error){Toast.makeText(a,error.getMessage(),Toast.LENGTH_LONG).show();}
        });LinearLayout.LayoutParams commitParams=new LinearLayout.LayoutParams(0,Ui.dp(a,48),1);commitParams.leftMargin=Ui.dp(a,12);footer.addView(commit,commitParams);root.addView(footer);dialog.setContentView(root);dialog.show();
        Window window=dialog.getWindow();if(window!=null){window.setBackgroundDrawableResource(android.R.color.transparent);android.util.DisplayMetrics m=a.getResources().getDisplayMetrics();window.setLayout(Math.min(Ui.dp(a,620),m.widthPixels-Ui.dp(a,24)),Math.min(Ui.dp(a,820),(int)(m.heightPixels*.90f)));window.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);}
    }
}
