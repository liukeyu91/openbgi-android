/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;
import android.app.*;
import android.widget.*;
import android.view.*;
import java.io.*;
import java.util.*;

final class EncodingPreview {
    static void show(Activity a,File game,java.util.function.Consumer<String> selected){
        LinearLayout box=new LinearLayout(a);box.setOrientation(LinearLayout.VERTICAL);
        int pad=(int)(16*a.getResources().getDisplayMetrics().density);box.setPadding(pad,0,pad,0);
        ScrollView scroll=new ScrollView(a);scroll.addView(box);
        TextView status=new TextView(a);status.setText("正在读取脚本文字样本…");box.addView(status);
        final Thread[] worker=new Thread[1];
        AlertDialog dialog=new AlertDialog.Builder(a).setTitle("编码预览").setView(scroll).setNegativeButton("取消",null).create();
        dialog.setOnDismissListener(d->{if(worker[0]!=null)worker[0].interrupt();});dialog.show();Ui.style(dialog);
        File resource=game;
        if(game.getName().equals("external") && !GameImport.gamePresent(game)){
            File external=a.getExternalFilesDir(null);if(external!=null)resource=new File(external,"game");
        }
        final File input=resource;
        worker[0]=new Thread(()->{
            try{
                ArchiveTextSamples.Result samples=ArchiveTextSamples.scan(input);
                EncodingDetector.Result result=EncodingDetector.analyze(samples.samples);
                a.runOnUiThread(()->{
                    if(!dialog.isShowing() || a.isDestroyed())return;
                    status.setText(result.advice()+"\n"+samples.scenes+" 个场景 / "+samples.samples.size()+" 条样本"+(samples.limited?"（部分样本）":""));
                    android.util.Log.i("OpenBGI","EncodingProbe: "+result.summary()+"; archives="+samples.archives+" skipped="+samples.skipped);
                    List<ArchiveTextSamples.Sample> preview=EncodingDetector.previewSamples(samples.samples);
                    int show=preview.size();
                    if(show>0){
                        Spinner source=new Spinner(a);String[] names=new String[show];
                        for(int i=0;i<show;i++)names[i]="样本 "+(i+1)+" · "+preview.get(i).source;
                        ArrayAdapter<String> adapter=new ArrayAdapter<>(a,android.R.layout.simple_spinner_item,names);
                        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);source.setAdapter(adapter);box.addView(source);
                        LinearLayout row=new LinearLayout(a);TextView[] texts=new TextView[3];box.addView(row);
                        for(int enc=0;enc<3;enc++){
                            texts[enc]=new TextView(a);texts[enc].setTextSize(14);texts[enc].setTextIsSelectable(true);texts[enc].setPadding(0,pad/2,pad/2,pad/2);
                            row.addView(texts[enc],new LinearLayout.LayoutParams(0,-2,1));
                        }
                        source.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener(){
                            public void onNothingSelected(AdapterView<?> p){}
                            public void onItemSelected(AdapterView<?> p,View view,int at,long id){
                                for(int enc=0;enc<3;enc++)texts[enc].setText(EncodingDetector.ENCODINGS[enc].toUpperCase(Locale.ROOT)+"\n"+EncodingDetector.preview(preview.get(at).bytes,enc));
                            }
                        });
                        TextView note=new TextView(a);note.setText("预览来自脚本样本，不一定是游戏第一句。选择编码后需返回设置页保存；姓名和菜单的编码可能不同。");note.setTextSize(12);box.addView(note);
                        LinearLayout choices=new LinearLayout(a);box.addView(choices);
                        for(String encoding:EncodingDetector.ENCODINGS){
                            Button choose=new Button(a);choose.setTextSize(12);choose.setText("选择 "+encoding.toUpperCase(Locale.ROOT)+(encoding.equals(result.recommended)?"（建议）":""));
                            choose.setOnClickListener(v->{selected.accept(encoding);dialog.dismiss();});choices.addView(choose,new LinearLayout.LayoutParams(0,-2,1));
                        }
                    }
                });
            }catch(IOException error){a.runOnUiThread(()->{if(dialog.isShowing())status.setText("无法完成抽样："+error.getMessage()+"\n请手动选择编码。");});}
        },"OpenBGI-EncodingProbe");worker[0].start();
    }
}
