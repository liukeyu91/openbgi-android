/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;
import java.util.*;

/** Physical Android key codes -> Windows VK, with shared-key reference counts. */
final class InputKeys {
    interface Sink { void key(int action,int vk,int repeat); }
    private final Sink sink;
    private final Map<Long,Integer> held=new HashMap<>(); private final int[] refs=new int[256];
    private int shortcutHeld;
    private static final int SHORTCUT_DEVICE=Integer.MIN_VALUE,SHORTCUT_CODE=Integer.MIN_VALUE;
    InputKeys(Sink sink){this.sink=sink;}
    static int map(int key,boolean swapAB){
        if(key>=29 && key<=54)return 0x41+key-29; // A..Z
        if(key>=7 && key<=16)return 0x30+key-7; // 0..9
        if(key>=131 && key<=142)return 0x70+key-131; // F1..F12
        switch(key){
            case 4:case 111:case 82:case 108:return 0x1b; // Back/Escape/Menu/Start
            case 66:case 23:return 0x0d;
            case 19:return 0x26;case 20:return 0x28;case 21:return 0x25;case 22:return 0x27;
            case 61:return 0x09;case 62:return 0x20;case 67:return 0x08;case 112:return 0x2e;
            case 92:return 0x21;case 93:return 0x22;case 122:return 0x24;case 123:return 0x23;
            case 113:case 114:case 104:case 105:return 0x11;
            case 59:case 60:return 0x10;case 57:case 58:return 0x12;
            case 96:return swapAB?0x1b:0x0d;case 97:return swapAB?0x0d:0x1b;
            case 99:return 0x20;case 100:case 102:return 0x21;case 103:return 0x22;
            default:return 0;
        }
    }
    void physical(int device,int code,int vk,boolean down,int repeat){
        if(vk<=0 || vk>=256)return;
        long token=((long)device<<32)|(code&0xffffffffL);
        Integer old=held.get(token);
        if(down){
            if(old!=null){if(repeat>0)sink.key(0,old,1);return;}
            held.put(token,vk);if(refs[vk]++==0)sink.key(0,vk,0);
        }else if(old!=null){held.remove(token);if(--refs[old]==0)sink.key(1,old,0);}
    }
    void axes(int device,float x,float y,float hatX,float hatY,float leftTrigger,float rightTrigger){
        float dx=Math.abs(hatX)>.5f?hatX:x,dy=Math.abs(hatY)>.5f?hatY:y;
        physical(device,-1,0x25,dx<-.5f,0);physical(device,-2,0x27,dx>.5f,0);
        physical(device,-3,0x26,dy<-.5f,0);physical(device,-4,0x28,dy>.5f,0);
        physical(device,-5,0x11,leftTrigger>.5f,0);physical(device,-6,0x11,rightTrigger>.5f,0);
    }
    void tap(int vk){if(vk<=0 || vk>=256)return;sink.key(0,vk,refs[vk]>0?1:0);if(refs[vk]==0)sink.key(1,vk,0);}
    boolean toggleShortcut(int vk){
        if(vk<=0 || vk>=256)return false;
        boolean enable=shortcutHeld!=vk;releaseShortcut();
        if(enable){physical(SHORTCUT_DEVICE,SHORTCUT_CODE,vk,true,0);shortcutHeld=vk;}
        return enable;
    }
    boolean shortcutHeld(){return shortcutHeld!=0;}
    void releaseShortcut(){if(shortcutHeld!=0){physical(SHORTCUT_DEVICE,SHORTCUT_CODE,shortcutHeld,false,0);shortcutHeld=0;}}
    void releaseDevice(int device){
        Iterator<Map.Entry<Long,Integer>> it=held.entrySet().iterator();
        while(it.hasNext()){
            Map.Entry<Long,Integer> e=it.next();
            if((int)(e.getKey()>>32)==device){int vk=e.getValue();it.remove();if(--refs[vk]==0)sink.key(1,vk,0);}
        }
    }
    void clear(){held.clear();shortcutHeld=0;for(int vk=1;vk<refs.length;vk++)if(refs[vk]>0){refs[vk]=0;sink.key(1,vk,0);}}
}
