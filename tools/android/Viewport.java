/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;
/** Integer viewport shared by touch hit tests and the native editor; matches viewport.h. */
final class Viewport {
    final int x, y, w, h;
    Viewport(int screenW, int screenH, int canvasW, int canvasH, int mode) {
        int vw = Math.max(0,screenW), vh = Math.max(0,screenH);
        if(canvasW <= 0 || canvasH <= 0) { x = y = w = h = 0; return; }
        int k = Math.min(vw / canvasW, vh / canvasH);
        if(mode == 2 && k >= 1) { vw = canvasW * k; vh = canvasH * k; }
        else if(mode != 1) {
            if((long)vw * canvasH > (long)vh * canvasW) vw = (int)((long)vh * canvasW / canvasH);
            else vh = (int)((long)vw * canvasH / canvasW);
        }
        x = (screenW - vw) / 2; y = (screenH - vh) / 2; w = vw; h = vh;
    }
    boolean contains(float px, float py) { return w > 0 && h > 0 && px >= x && py >= y && px < x + w && py < y + h; }
}
