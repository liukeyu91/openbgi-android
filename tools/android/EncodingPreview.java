/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;
import android.app.*;
import android.widget.*;
import android.view.*;
import java.io.*;
import java.util.*;

final class EncodingPreview {
    static void show(Activity a,File game,File resources,ArchiveLanguage language,java.util.function.Consumer<String> selected){
        LinearLayout box=new LinearLayout(a);box.setOrientation(LinearLayout.VERTICAL);
        int pad=(int)(16*a.getResources().getDisplayMetrics().density);box.setPadding(pad,0,pad,0);
        ScrollView scroll=new ScrollView(a);scroll.addView(box);
        TextView status=new TextView(a);status.setText(a.getString(R.string.encoding_sampling));box.addView(status);
        final Thread[] worker=new Thread[1];
        AlertDialog dialog=new AlertDialog.Builder(a).setTitle(a.getString(R.string.encoding_preview)).setView(scroll).setNegativeButton(a.getString(R.string.cancel),null).create();
        dialog.setOnDismissListener(d->{if(worker[0]!=null)worker[0].interrupt();});dialog.show();Ui.style(dialog);
        worker[0]=new Thread(()->{
            try(ResourceLayers layers=ResourceLayers.of(game,resources)){
                ArchiveTextSamples.Result samples=ArchiveTextSamples.scan(layers,language);
                EncodingDetector.Result result=EncodingDetector.analyze(samples.samples);
                a.runOnUiThread(()->{
                    if(!dialog.isShowing() || a.isDestroyed())return;
                    String recommended=language==null?result.recommended:language.encoding;
                    status.setText(language==null?AppMessages.encodingAdvice(a,result):
                        a.getString(R.string.encoding_detected,AppMessages.encodingName(recommended)));
                    android.util.Log.i("OpenBGI","EncodingProbe: "+result.summary()+"; archives="+samples.archives+" skipped="+samples.skipped);
                    List<ArchiveTextSamples.Sample> preview=EncodingDetector.previewSamples(samples.samples);
                    int show=preview.size();
                    if(show>0){
                        Spinner source=new Spinner(a);String[] names=new String[show];
                        for(int i=0;i<show;i++)names[i]=a.getString(R.string.encoding_sample,i+1,preview.get(i).source);
                        ArrayAdapter<String> adapter=new ArrayAdapter<>(a,android.R.layout.simple_spinner_item,names);
                        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);source.setAdapter(adapter);box.addView(source);
                        String[] encodings={"sjis","gbk","utf8","big5"};
                        LinearLayout row=new LinearLayout(a);TextView[] texts=new TextView[encodings.length];box.addView(row);
                        for(int enc=0;enc<texts.length;enc++){
                            texts[enc]=new TextView(a);texts[enc].setTextSize(14);texts[enc].setTextIsSelectable(true);texts[enc].setPadding(0,pad/2,pad/2,pad/2);
                            row.addView(texts[enc],new LinearLayout.LayoutParams(0,-2,1));
                        }
                        source.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener(){
                            public void onNothingSelected(AdapterView<?> p){}
                            public void onItemSelected(AdapterView<?> p,View view,int at,long id){
                                for(int enc=0;enc<texts.length;enc++)texts[enc].setText(AppMessages.encodingName(encodings[enc])+"\n"+AppMessages.encodingPreview(a,preview.get(at).bytes,enc));
                            }
                        });
                        LinearLayout choices=new LinearLayout(a);box.addView(choices);
                        for(String encoding:encodings){
                            Button choose=new Button(a);choose.setTextSize(12);choose.setText(a.getString(encoding.equals(recommended)?R.string.encoding_select_recommended:R.string.encoding_select,AppMessages.encodingName(encoding)));
                            choose.setOnClickListener(v->{selected.accept(encoding);dialog.dismiss();});choices.addView(choose,new LinearLayout.LayoutParams(0,-2,1));
                        }
                    }
                });
            }catch(IOException error){a.runOnUiThread(()->{if(dialog.isShowing())status.setText(a.getString(R.string.encoding_sampling_failed,error.getMessage()));});}
        },"OpenBGI-EncodingProbe");worker[0].start();
    }
}
