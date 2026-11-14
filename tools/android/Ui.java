/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;

import android.app.*;
import android.content.Context;
import android.content.res.ColorStateList;
import android.graphics.*;
import android.graphics.drawable.*;
import android.view.*;
import android.widget.*;

/** Small native UI vocabulary shared by the library and its settings. */
final class Ui {
    static final int BG=0xfff5f5f0, PAPER=0xffffffff, INK=0xff202d2a, MUTED=0xff77817b,
        LINE=0xffe4e8e1, ACCENT=0xff246f5d, TINT=0xffe6eee7, DANGER=0xffb35248;
    static int dp(Context c,float value){return Math.round(value*c.getResources().getDisplayMetrics().density);}
    static GradientDrawable shape(Context c,int color,int radius){
        GradientDrawable d=new GradientDrawable();d.setColor(color);d.setCornerRadius(dp(c,radius));return d;
    }
    static GradientDrawable border(Context c,int color,int radius){
        GradientDrawable d=shape(c,color,radius);d.setStroke(dp(c,1),LINE);return d;
    }
    static Drawable touch(Context c,int color,int radius){
        return new RippleDrawable(ColorStateList.valueOf(0x22246f5d),shape(c,color,radius),shape(c,PAPER,radius));
    }
    static TextView text(Context c,String value,int size,int color,boolean bold){
        TextView t=new TextView(c);t.setText(value);t.setTextSize(size);t.setTextColor(color);
        t.setFontFeatureSettings("kern");t.setIncludeFontPadding(false);
        t.setTypeface(Typeface.create(bold?"sans-serif-medium":"sans-serif",Typeface.NORMAL));
        t.setLineSpacing(dp(c,3),1);return t;
    }
    static LinearLayout column(Context c){LinearLayout l=new LinearLayout(c);l.setOrientation(LinearLayout.VERTICAL);return l;}
    static LinearLayout row(Context c){LinearLayout l=new LinearLayout(c);l.setGravity(Gravity.CENTER_VERTICAL);return l;}
    static void space(LinearLayout parent,int height){parent.addView(new View(parent.getContext()),new LinearLayout.LayoutParams(1,dp(parent.getContext(),height)));}
    static Button button(Context c,String label,boolean primary,Runnable action){
        Button b=new Button(c);b.setText(label);b.setTextSize(14);b.setAllCaps(false);b.setLetterSpacing(.01f);
        b.setTypeface(Typeface.create("sans-serif-medium",0));b.setTextColor(primary?PAPER:INK);
        b.setBackground(touch(c,primary?ACCENT:TINT,14));b.setMinHeight(dp(c,48));b.setMinimumHeight(dp(c,48));
        b.setMinWidth(0);b.setMinimumWidth(0);b.setPadding(dp(c,18),dp(c,8),dp(c,18),dp(c,8));
        b.setOnClickListener(v->action.run());return b;
    }
    static ImageView iconButton(Context c,String name,String description,Runnable action){
        ImageView v=new ImageView(c);v.setImageDrawable(new Icon(name,INK));v.setScaleType(ImageView.ScaleType.FIT_CENTER);
        int pad=dp(c,13);v.setPadding(pad,pad,pad,pad);v.setBackground(touch(c,0x00000000,14));
        v.setContentDescription(description);v.setFocusable(true);v.setOnClickListener(w->action.run());
        v.setLayoutParams(new LinearLayout.LayoutParams(dp(c,48),dp(c,48)));return v;
    }
    static void style(AlertDialog dialog){
        if(dialog.getWindow()!=null)dialog.getWindow().setBackgroundDrawable(shape(dialog.getContext(),PAPER,22));
        for(int which:new int[]{AlertDialog.BUTTON_POSITIVE,AlertDialog.BUTTON_NEGATIVE,AlertDialog.BUTTON_NEUTRAL}){
            Button b=dialog.getButton(which);if(b!=null){b.setTextColor(ACCENT);b.setAllCaps(false);}
        }
    }
    static TextView section(Context c,String label){
        TextView t=text(c,label,12,MUTED,true);t.setLetterSpacing(.07f);t.setPadding(0,dp(c,22),0,dp(c,10));return t;
    }
    static final class Icon extends Drawable {
        private final String name;private final Paint p=new Paint(3);private final int color;
        Icon(String name,int color){this.name=name;this.color=color;}
        public int getIntrinsicWidth(){return 24;}public int getIntrinsicHeight(){return 24;}
        public void draw(Canvas c){
            Rect b=getBounds();c.save();c.translate(b.left,b.top);c.scale(b.width()/24f,b.height()/24f);
            p.setColor(color);p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(1.65f);p.setStrokeCap(Paint.Cap.ROUND);p.setStrokeJoin(Paint.Join.ROUND);
            Path path=new Path();
            switch(name){
                case "plus":c.drawLine(12,5,12,19,p);c.drawLine(5,12,19,12,p);break;
                case "more":p.setStyle(Paint.Style.FILL);for(int y=5;y<21;y+=7)c.drawCircle(12,y,1.6f,p);break;
                case "search":c.drawCircle(10,10,6,p);c.drawLine(14.5f,14.5f,20,20,p);break;
                case "play":p.setStyle(Paint.Style.FILL);path.moveTo(8,5);path.lineTo(19,12);path.lineTo(8,19);path.close();c.drawPath(path,p);break;
                case "arrow":c.drawLine(5,12,19,12,p);c.drawLine(14,7,19,12,p);c.drawLine(14,17,19,12,p);break;
                case "down":c.drawLine(7,10,12,15,p);c.drawLine(12,15,17,10,p);break;
                case "back":c.drawLine(16,5,9,12,p);c.drawLine(9,12,16,19,p);break;
                case "library":c.drawRoundRect(4,4,9,20,1,1,p);c.drawRoundRect(12,4,17,20,1,1,p);c.drawLine(19,5,22,19,p);break;
                case "guide":c.drawRoundRect(4,3,20,21,3,3,p);c.drawLine(8,8,16,8,p);c.drawLine(8,12,16,12,p);c.drawLine(8,16,13,16,p);break;
                case "info":c.drawCircle(12,12,9,p);c.drawLine(12,11,12,17,p);p.setStyle(Paint.Style.FILL);c.drawCircle(12,7,1,p);break;
                case "folder":path.moveTo(3,7);path.lineTo(9,7);path.lineTo(11,9);path.lineTo(21,9);path.lineTo(21,20);path.lineTo(3,20);path.close();c.drawPath(path,p);break;
                case "download":c.drawLine(12,3,12,15,p);c.drawLine(7,10,12,15,p);c.drawLine(17,10,12,15,p);path.moveTo(4,16);path.lineTo(4,21);path.lineTo(20,21);path.lineTo(20,16);c.drawPath(path,p);break;
                default:c.drawRoundRect(4,4,20,20,4,4,p);c.drawLine(9,8,9,16,p);c.drawLine(13,8,17,12,p);c.drawLine(17,12,13,16,p);
            }
            c.restore();
        }
        public void setAlpha(int alpha){p.setAlpha(alpha);}public void setColorFilter(ColorFilter filter){p.setColorFilter(filter);}
        public int getOpacity(){return PixelFormat.TRANSLUCENT;}
    }
}
