/* SPDX-License-Identifier: GPL-2.0-only
 * GLES2 owns a persistent RGBA canvas texture; only changed rows are uploaded.
 * The window reference is independent of the UI's Surface reference.
 */
#include "renderer.h"
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <android/native_window.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void OsAndroid_Log(const char* text);

static struct {
    EGLDisplay display;
    EGLContext context;
    EGLSurface surface;
    ANativeWindow* window;
    unsigned generation;
    GLuint program,texture;
    GLenum filter;
    int width,height;
    uint32_t* packed;
    size_t capacity;
} gRender;
static AndroidRenderStats_t gStats;
static GLenum gGlError;

AndroidRenderStats_t OsAndroid_RenderStats(void){return gStats;}

void OsAndroid_RenderClose(void)
{
    if(gRender.display!=EGL_NO_DISPLAY){
        eglMakeCurrent(gRender.display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
        if(gRender.surface!=EGL_NO_SURFACE)eglDestroySurface(gRender.display,gRender.surface);
        if(gRender.context!=EGL_NO_CONTEXT)eglDestroyContext(gRender.display,gRender.context);
        eglTerminate(gRender.display);
    }
    if(gRender.window)ANativeWindow_release(gRender.window);
    free(gRender.packed);
    memset(&gRender,0,sizeof gRender);
}

static GLuint Shader(GLenum type,const char* source)
{
    GLint ok=0;GLuint shader=glCreateShader(type);
    if(!shader)return 0;
    glShaderSource(shader,1,&source,NULL);glCompileShader(shader);
    glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if(!ok){char log[1024];glGetShaderInfoLog(shader,sizeof log,NULL,log);OsAndroid_Log(log);glDeleteShader(shader);return 0;}
    return shader;
}

static int Open(ANativeWindow* window,unsigned generation)
{
    const EGLint configAttrs[]={EGL_SURFACE_TYPE,EGL_WINDOW_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_DEPTH_SIZE,0,EGL_STENCIL_SIZE,0,EGL_NONE};
    const EGLint contextAttrs[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    const char* vertex="attribute vec2 position;attribute vec2 texcoord;varying vec2 uv;void main(){gl_Position=vec4(position,0.,1.);uv=texcoord;}";
    const char* fragment="#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n#else\nprecision mediump float;\n#endif\nvarying vec2 uv;uniform sampler2D canvas;void main(){gl_FragColor=vec4(texture2D(canvas,uv).rgb,1.);}";
    EGLConfig config;EGLint count,format;GLuint vs,fs;GLint ok=0;
    gRender.display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if(gRender.display==EGL_NO_DISPLAY || !eglInitialize(gRender.display,NULL,NULL) ||
        !eglBindAPI(EGL_OPENGL_ES_API) || !eglChooseConfig(gRender.display,configAttrs,&config,1,&count) || count!=1)return 0;
    if(!eglGetConfigAttrib(gRender.display,config,EGL_NATIVE_VISUAL_ID,&format) ||
        ANativeWindow_setBuffersGeometry(window,0,0,format)!=0)return 0;
    gRender.context=eglCreateContext(gRender.display,config,EGL_NO_CONTEXT,contextAttrs);
    if(gRender.context==EGL_NO_CONTEXT)return 0;
    gRender.surface=eglCreateWindowSurface(gRender.display,config,window,NULL);
    if(gRender.surface==EGL_NO_SURFACE || !eglMakeCurrent(gRender.display,gRender.surface,gRender.surface,gRender.context) ||
        !eglSwapInterval(gRender.display,1))return 0;
    ANativeWindow_acquire(window);gRender.window=window;gRender.generation=generation;
    vs=Shader(GL_VERTEX_SHADER,vertex);fs=Shader(GL_FRAGMENT_SHADER,fragment);
    if(!vs || !fs){if(vs)glDeleteShader(vs);if(fs)glDeleteShader(fs);return 0;}
    gRender.program=glCreateProgram();
    if(!gRender.program){glDeleteShader(vs);glDeleteShader(fs);return 0;}
    glAttachShader(gRender.program,vs);glAttachShader(gRender.program,fs);
    glBindAttribLocation(gRender.program,0,"position");glBindAttribLocation(gRender.program,1,"texcoord");
    glLinkProgram(gRender.program);glDeleteShader(vs);glDeleteShader(fs);
    glGetProgramiv(gRender.program,GL_LINK_STATUS,&ok);
    if(!ok){char log[1024];glGetProgramInfoLog(gRender.program,sizeof log,NULL,log);OsAndroid_Log(log);return 0;}
    glUseProgram(gRender.program);glUniform1i(glGetUniformLocation(gRender.program,"canvas"),0);
    glGenTextures(1,&gRender.texture);glBindTexture(GL_TEXTURE_2D,gRender.texture);
    gRender.filter=GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT,4);glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDisable(GL_DITHER);
    {char log[256];snprintf(log,sizeof log,"EGL GLES2 renderer: %s; swap interval 1",glGetString(GL_RENDERER));OsAndroid_Log(log);}
    gGlError=glGetError();return gGlError==GL_NO_ERROR;
}

int OsAndroid_Render(ANativeWindow* window,unsigned generation,const uint32_t* pixels,
    int w,int h,AndroidDamage_t damage,int mode,Viewport_t* presented)
{
    EGLint sw,sh;Viewport_t v;GLint maxSize;
    /* bottom-left vertex samples the last canvas row: canvas memory is top-down. */
    const GLfloat positions[]={-1,-1,1,-1,-1,1,1,1},uv[]={0,1,1,1,0,0,1,0};
    gGlError=GL_NO_ERROR;
    if(!window || !pixels || w<=0 || h<=0)return 0;
    if(gRender.window!=window || gRender.generation!=generation){
        OsAndroid_RenderClose();if(!Open(window,generation))goto failed;
    }
    if(!eglQuerySurface(gRender.display,gRender.surface,EGL_WIDTH,&sw) ||
        !eglQuerySurface(gRender.display,gRender.surface,EGL_HEIGHT,&sh) || sw<=0 || sh<=0)goto failed;
    if(gRender.width!=w || gRender.height!=h){
        glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maxSize);if(w>maxSize || h>maxSize)goto failed;
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
        gRender.width=w;gRender.height=h;++gStats.full;gStats.bytes+=(uint64_t)w*h*4;
    }else if(damage.l<damage.r && damage.t<damage.b){
        int y,dw=damage.r-damage.l,dh=damage.b-damage.t;
        const uint32_t* data=pixels+(size_t)damage.t*w;
        if(dw!=w){
            size_t size=(size_t)dw*dh;
            if(size>gRender.capacity){
                uint32_t* packed=realloc(gRender.packed,size*sizeof *packed);
                if(!packed)goto failed;gRender.packed=packed;gRender.capacity=size;
            }
            for(y=0;y<dh;y++)memcpy(gRender.packed+(size_t)y*dw,data+(size_t)y*w+damage.l,(size_t)dw*4);
            data=gRender.packed;
        }
        glTexSubImage2D(GL_TEXTURE_2D,0,damage.l,damage.t,dw,dh,GL_RGBA,GL_UNSIGNED_BYTE,data);
        ++gStats.partial;gStats.bytes+=(uint64_t)dw*dh*4;
    }
    v=AndroidViewport(sw,sh,w,h,mode);
    {
        GLenum filter=AndroidViewportNearest(v,w,h)?GL_NEAREST:GL_LINEAR;
        if(filter!=gRender.filter){
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,filter);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,filter);
            gRender.filter=filter;
        }
    }
    glViewport(0,0,sw,sh);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    glViewport(v.x,sh-v.y-v.h,v.w,v.h);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,positions);glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,0,uv);
    glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    gGlError=glGetError();
    if(gGlError!=GL_NO_ERROR || !eglSwapBuffers(gRender.display,gRender.surface))goto failed;
    if(presented)*presented=v;
    ++gStats.frames;return 1;
failed:
    {EGLint error=eglGetError();char log[160];if(gGlError==GL_NO_ERROR)gGlError=glGetError();
     snprintf(log,sizeof log,"EGL render failed: egl=0x%x gl=0x%x; recreating on next frame",error,gGlError);OsAndroid_Log(log);}
    ++gStats.errors;OsAndroid_RenderClose();return -1;
}
