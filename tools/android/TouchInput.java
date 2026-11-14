/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;

/** Touch gestures emit pointer primitives. No VM calls and no Android dependency. */
final class TouchInput {
    static final int MOVE=0, DOWN=1, UP=2, WHEEL=3, CANCEL=4;
    interface Sink { void pointer(int action,int button,float x,float y,int width,int height,float wheel); }
    interface Timer { void later(Runnable action,int ms); }
    private final Sink sink; private final Timer timer;
    private final boolean dragMode; private final float slop; private final int timeout;
    private boolean active,two,moved,longFired,pressed;
    private int id,width,height,generation; private float startX,startY,x,y,scrollY;
    TouchInput(Sink sink,Timer timer,boolean dragMode,float slop,int timeout) {
        this.sink=sink;this.timer=timer;this.dragMode=dragMode;this.slop=slop;this.timeout=timeout;
    }
    int pointerId(){return id;}
    boolean twoFingers(){return two;}
    private void emit(int action,int button,float px,float py,float wheel){sink.pointer(action,button,px,py,width,height,wheel);}
    void down(int pointer,float px,float py,int w,int h,boolean inside) {
        cancel();if(!inside || w<=0 || h<=0)return;
        active=true;id=pointer;width=w;height=h;x=startX=px;y=startY=scrollY=py;
        two=moved=longFired=pressed=false;emit(MOVE,0,x,y,0);
        final int token=++generation;
        timer.later(()->{
            if(active && token==generation && !two && !moved){
                longFired=true;emit(DOWN,1,x,y,0);emit(UP,1,x,y,0);
            }
        },timeout);
    }
    void secondPointer(){
        if(!active)return;
        ++generation;two=true;
        if(pressed){emit(UP,0,x,y,0);pressed=false;longFired=true;}
    }
    void move(float px,float py){
        if(!active)return;x=px;y=py;
        if(two || longFired)return;
        if(!moved && Math.hypot(x-startX,y-startY)>slop){moved=true;++generation;}
        if(dragMode){
            if(moved && !pressed){emit(DOWN,0,startX,startY,0);pressed=true;}
            emit(MOVE,0,x,y,0);
        }else if(moved){
            float step=height/12f;int delta=(int)((y-scrollY)/step);
            if(delta!=0){scrollY+=delta*step;emit(WHEEL,0,x,y,delta);}
        }
    }
    void up(float px,float py){
        if(!active)return;x=px;y=py;++generation;
        if(pressed){emit(MOVE,0,x,y,0);emit(UP,0,x,y,0);pressed=false;}
        else if(!longFired && (two || !moved)){int button=two?1:0;emit(MOVE,0,x,y,0);emit(DOWN,button,x,y,0);emit(UP,button,x,y,0);}
        active=false;
    }
    void cancel(){
        ++generation;
        if(pressed)emit(UP,0,x,y,0);
        active=pressed=false;
    }
}
