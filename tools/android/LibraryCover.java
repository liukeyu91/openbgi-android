/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;

import android.content.Context;
import android.graphics.*;
import android.view.View;
import java.io.File;

/** User-owned cover, or a deterministic illustrated book jacket. No game assets shipped. */
final class LibraryCover extends View {
    private final Paint p=new Paint(3);
    private final String title;
    private final File file;
    private final String cacheKey;
    private boolean badImage;
    private static final android.util.LruCache<String,Bitmap> CACHE=new android.util.LruCache<String,Bitmap>(8*1024*1024){
        @Override protected int sizeOf(String key,Bitmap image){return image.getAllocationByteCount();}
    };
    private static final int[][] COLORS={{0xffdbe6dc,0xff588176,0xffabc9b7},{0xffeee0d5,0xffae7965,0xffd4b9a5},
        {0xffdce3ed,0xff637e9c,0xffa7b9d0},{0xffe9e3cd,0xff8c8656,0xffc6c098}};
    LibraryCover(Context c,File game,String title){
        super(c);this.title=title;
        file=new File(game,".openbgi/cover.jpg");cacheKey=file.toString()+":"+file.lastModified()+":"+file.length();
        setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);
    }
    private Bitmap image(){
        Bitmap image=CACHE.get(cacheKey);if(image!=null)return image;
        if(!badImage && file.isFile())try{
            BitmapFactory.Options o=new BitmapFactory.Options();o.inJustDecodeBounds=true;BitmapFactory.decodeFile(file.toString(),o);
            if(o.outWidth>0 && o.outHeight>0){o.inSampleSize=1;while(o.outWidth/o.inSampleSize>512 || o.outHeight/o.inSampleSize>512)o.inSampleSize*=2;
                o.inJustDecodeBounds=false;image=BitmapFactory.decodeFile(file.toString(),o);}
            if(image!=null)CACHE.put(cacheKey,image);else badImage=true;
        }catch(RuntimeException ignored){badImage=true;}
        return image;
    }
    @Override protected void onDraw(Canvas c){
        super.onDraw(c);float w=getWidth(),h=getHeight();if(w<=0 || h<=0)return;
        Bitmap image=image();if(image!=null){
            float scale=Math.max(w/image.getWidth(),h/image.getHeight());float bw=image.getWidth()*scale,bh=image.getHeight()*scale;
            c.drawBitmap(image,null,new RectF((w-bw)/2,(h-bh)/2,(w+bw)/2,(h+bh)/2),p);return;
        }
        int[] colors=COLORS[Math.floorMod(title.hashCode(),COLORS.length)];c.drawColor(colors[0]);
        p.setColor(colors[2]);p.setStyle(Paint.Style.FILL);c.drawCircle(w*.88f,h*.18f,h*.58f,p);
        p.setColor(colors[1]);p.setAlpha(45);c.drawOval(w*.15f,h*.32f,w*1.2f,h*1.7f,p);p.setAlpha(255);
        p.setColor(colors[1]);p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(Math.max(1,w*.003f));
        for(int i=0;i<7;i++)c.drawOval(w*(.38f+i*.06f),h*(-.6f+i*.06f),w*(1.4f+i*.06f),h*(1.3f+i*.06f),p);
        p.setStyle(Paint.Style.FILL);p.setColor(colors[1]);p.setTypeface(Typeface.create("serif",0));p.setTextSize(h*.46f);
        String mark=title.replaceFirst("(?i)^OPENBGI-","").replaceFirst("(?i)-TEST$","");
        String first=mark.isEmpty()?"O":mark.substring(0,mark.offsetByCodePoints(0,1));
        p.setAlpha(160);c.drawText(first,w*.08f,h*.72f,p);p.setAlpha(255);
        p.setTypeface(Typeface.create("sans-serif-medium",0));p.setTextSize(h*.065f);
        c.drawText("默认封面",w*.085f,h*.16f,p);
        p.setColor(colors[1]);c.drawRect(w*.085f,h*.84f,w*.24f,h*.85f,p);
    }
}
