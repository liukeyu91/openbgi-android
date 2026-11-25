/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;
import android.app.*;
import android.widget.*;
import android.view.*;
import java.nio.charset.Charset;
import java.util.*;
import java.util.function.*;

/** Explicit replacement of a single field; the game's text encoding stays fixed. */
final class InitialTextRepairDialog {
    private static final Map<Activity,AlertDialog> open=new WeakHashMap<>();
    private static final int REPAIR_ITEM=0x6f624701;
    static void attach(EditText edit,BooleanSupplier available,Runnable show){
        /* Some vendor floating toolbars discard custom action-mode items.
         * Keep a deterministic long-press entry without changing field layout.
         * Standard editing actions delegate to TextView, preserving its filters. */
        final boolean[] nativeSelection={false};
        edit.setOnLongClickListener(view->{
            if(nativeSelection[0] || !available.getAsBoolean())return false;
            Activity activity=(Activity)edit.getContext();
            if(activity.isFinishing() || activity.isDestroyed())return false;
            dismissAll(activity);
            AlertDialog dialog=new AlertDialog.Builder(activity).setTitle(activity.getString(R.string.field_actions))
                .setItems(new String[]{activity.getString(R.string.initial_encoding),activity.getString(R.string.select_all),activity.getString(R.string.copy),activity.getString(R.string.cut),activity.getString(R.string.paste),activity.getString(R.string.system_selection)},(d,which)->{
                    edit.post(()->{
                        if(!available.getAsBoolean() || activity.isFinishing() || activity.isDestroyed())return;
                        if(which==0)show.run();
                        else if(which<=4){
                            int[] ids={android.R.id.selectAll,android.R.id.copy,android.R.id.cut,android.R.id.paste};
                            edit.onTextContextMenuItem(ids[which-1]);
                        } else {nativeSelection[0]=true;try{edit.performLongClick();}finally{nativeSelection[0]=false;}}
                    });
                }).setNegativeButton(activity.getString(R.string.cancel),null).create();
            dialog.setOnDismissListener(d->{if(open.get(activity)==dialog)open.remove(activity);});
            dialog.setCanceledOnTouchOutside(false);open.put(activity,dialog);dialog.show();
            return true;
        });
        ActionMode.Callback actions=new ActionMode.Callback(){
            private void update(Menu menu){
                if(!available.getAsBoolean()){menu.removeItem(REPAIR_ITEM);return;}
                if(menu.findItem(REPAIR_ITEM)==null)menu.add(0,REPAIR_ITEM,100,R.string.initial_encoding).setShowAsAction(MenuItem.SHOW_AS_ACTION_IF_ROOM);
            }
            public boolean onCreateActionMode(ActionMode mode,Menu menu){update(menu);return true;}
            public boolean onPrepareActionMode(ActionMode mode,Menu menu){update(menu);return true;}
            public boolean onActionItemClicked(ActionMode mode,MenuItem item){
                if(item.getItemId()!=REPAIR_ITEM)return false;
                mode.finish();edit.post(()->{if(available.getAsBoolean())show.run();});return true;
            }
            public void onDestroyActionMode(ActionMode mode){}
        };
        edit.setCustomSelectionActionModeCallback(actions);
        edit.setCustomInsertionActionModeCallback(actions);
    }
    static void dismissAll(Activity activity){AlertDialog dialog=open.remove(activity);if(dialog!=null)dialog.dismiss();}
    static void show(Activity activity,InitialTextRepair snapshot,Charset target,int limit,boolean rejectAscii,
            BooleanSupplier stillCurrent,Consumer<String> replace){
        if(activity.isFinishing() || activity.isDestroyed() || !stillCurrent.getAsBoolean())return;
        dismissAll(activity);
        InitialTextRepair.Candidate[] candidates=new InitialTextRepair.Candidate[InitialTextRepair.MODES.length];String[] labels=new String[candidates.length];
        for(int i=0;i<candidates.length;i++){candidates[i]=snapshot.candidate(i,target,limit,rejectAscii);labels[i]=candidates[i].valid()?candidates[i].label(i):InitialTextRepair.LABELS[i]+" · "+AppMessages.repairError(activity,candidates[i].error);}
        TextView title=new TextView(activity);title.setText(activity.getString(R.string.initial_encoding_help));
        int pad=(int)(16*activity.getResources().getDisplayMetrics().density);title.setPadding(pad,pad,pad,pad);title.setTextSize(14);
        AlertDialog dialog=new AlertDialog.Builder(activity).setCustomTitle(title).setItems(labels,null).setNegativeButton(activity.getString(R.string.cancel),null).create();
        dialog.setOnDismissListener(d->{if(open.get(activity)==dialog)open.remove(activity);});
        dialog.setCanceledOnTouchOutside(false);open.put(activity,dialog);dialog.show();
        dialog.getListView().setOnItemClickListener((parent,view,at,id)->{
            if(activity.isFinishing() || activity.isDestroyed() || !stillCurrent.getAsBoolean()){
                Toast.makeText(activity,activity.getString(R.string.field_changed),Toast.LENGTH_SHORT).show();dialog.dismiss();return;
            }
            InitialTextRepair.Candidate candidate=candidates[at];
            if(!candidate.valid()){Toast.makeText(activity,AppMessages.repairError(activity,candidate.error),Toast.LENGTH_SHORT).show();return;}
            replace.accept(candidate.text);dialog.dismiss();
        });
    }
}
