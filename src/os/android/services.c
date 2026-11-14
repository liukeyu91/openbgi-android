/*
 * This file is part of OpenBGI (https://openbgi.net).
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Android UI services. UI calls are posted to Java; shared edit text and
 * modal results are synchronized without calling the VM on the UI thread.
 */
#include "android_internal.h"
#include "bgi/os_common.h"
#include "bgi/strutil.h"
#include "bgi/os_posix.h"

void OsAndroid_EditCommand(int op, int a, int b, int c, int d, const char* text)
{
    AndroidHost_t* host = gAndroidHost;
    int attached, revision;
    JNIEnv* env = OsAndroid_GetEnv(&attached);
    jbyteArray bytes = NULL;
    if(!env) return;
    pthread_mutex_lock(&host->lock);
    revision = host->editRevision;
    pthread_mutex_unlock(&host->lock);
    if(text)
    {
        size_t n = strlen(text);
        bytes = (*env)->NewByteArray(env, (jsize)n);
        if(bytes) (*env)->SetByteArrayRegion(env, bytes, 0, (jsize)n, (const jbyte*)text);
    }
    if(!(*env)->ExceptionCheck(env))
        (*env)->CallVoidMethod(env, host->activity, host->editCommand,
            op, a, b, c, d, revision, gTextGbk ? OS_TEXT_GBK : gTextUtf8 ? OS_TEXT_UTF8 : OS_TEXT_SJIS, bytes);
    if(bytes) (*env)->DeleteLocalRef(env, bytes);
    if((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionDescribe(env); (*env)->ExceptionClear(env);
        OsAndroid_Log("Could not update the Android text input.");
    }
    OsAndroid_ReleaseEnv(attached);
}

void OS_EditShow(int show) {
    if(!show) {
        pthread_mutex_lock(&gAndroidHost->lock);gAndroidHost->editIme=0;pthread_mutex_unlock(&gAndroidHost->lock);
    }
    OsAndroid_EditCommand(ANDROID_EDIT_SHOW, show, 0, 0, 0, NULL);
}
void OS_EditMove(int x, int y, int w, int h) { OsAndroid_EditCommand(ANDROID_EDIT_MOVE, x, y, w, h, NULL); }
void OS_EditSetFont(const char* face, int h, int w) { BGI_UNUSED(face); OsAndroid_EditCommand(ANDROID_EDIT_FONT, h, w, 0, 0, NULL); }
void OS_EditSelectAll(void) { OsAndroid_EditCommand(ANDROID_EDIT_SELECT, 0, 0, 0, 0, NULL); }
void OS_EditFocus(int focus) {
    /* Scripts poll B0 23 immediately after setup. Reserve the editing session
     * before posting to Java: opening/animating an IME is asynchronous. */
    pthread_mutex_lock(&gAndroidHost->lock);gAndroidHost->editIme=focus!=0;pthread_mutex_unlock(&gAndroidHost->lock);
    OsAndroid_EditCommand(ANDROID_EDIT_FOCUS, focus, 0, 0, 0, NULL);
}
void OS_EditSetColour(uint32_t rgb) { OsAndroid_EditCommand(ANDROID_EDIT_COLOUR, (int)rgb, 0, 0, 0, NULL); }
void OS_EditRefresh(void) { OsAndroid_EditCommand(ANDROID_EDIT_REFRESH, 0, 0, 0, 0, NULL); }
void OS_EditRejectAscii(int reject) { OsAndroid_EditCommand(ANDROID_EDIT_REJECT, reject, 0, 0, 0, NULL); }
int OS_EditImeOpen(void)
{
    AndroidHost_t* host = gAndroidHost;
    int open;
    pthread_mutex_lock(&host->lock); open = host->editIme; pthread_mutex_unlock(&host->lock);
    return open;
}

static size_t CopyText(char* out, size_t cap, const char* text)
{
	size_t pos = 0;
	if(!cap)
		return 0;
	while(text[pos])
	{
		const char* p = text + pos;
		int len = 1;
		if(gTextGbk)
			len = OS_GbkCharLen(p);
		else if(gTextUtf8)
		{
			int utf8 = OsCommon_Utf8SeqLen((const uint8_t*)p);
			if(utf8)
				len = utf8;
		}
		else if(OsCommon_SjisIsLead((uint8_t)p[0]) && p[1])
			len = 2;
		if((size_t)len >= cap - pos)
			break;
		memcpy(out + pos, p, (size_t)len);
		pos += (size_t)len;
	}
	out[pos] = 0;
	return pos;
}

void OS_EditLimitText(int maxChars)
{
    AndroidHost_t* host = gAndroidHost;
    pthread_mutex_lock(&host->lock);
    host->editLimit = maxChars > 0 && maxChars < (int)sizeof host->editText ? maxChars : 0;
    pthread_mutex_unlock(&host->lock);
    OsAndroid_EditCommand(ANDROID_EDIT_LIMIT, maxChars, 0, 0, 0, NULL);
}

void OS_EditSetText(const char* text)
{
    AndroidHost_t* host = gAndroidHost;
    char copy[sizeof host->editText];
    size_t cap = sizeof copy;
    pthread_mutex_lock(&host->lock);
    if(host->editLimit > 0) cap = (size_t)host->editLimit + 1;
    CopyText(host->editText, cap, text ? text : "");
    strcpy(copy, host->editText);
    host->editRevision++;
    pthread_mutex_unlock(&host->lock);
    OsAndroid_EditCommand(ANDROID_EDIT_TEXT, 0, 0, 0, 0, copy);
}

int OS_EditGetText(char* out, size_t n)
{
    AndroidHost_t* host = gAndroidHost;
    int length;
    pthread_mutex_lock(&host->lock);
    length = (int)CopyText(out, n, host->editText);
    pthread_mutex_unlock(&host->lock);
    return length;
}

void OS_EditDestroy(void)
{
    AndroidHost_t* host = gAndroidHost;
    pthread_mutex_lock(&host->lock);
    host->editText[0] = 0; host->editLimit = host->editIme = 0; host->editRevision++;
    pthread_mutex_unlock(&host->lock);
    OsAndroid_EditCommand(ANDROID_EDIT_DESTROY, 0, 0, 0, 0, NULL);
}

OsChild_t* OS_ChildCreate(int i, const char* t, int x, int y, int w, int h)
{
	BGI_UNUSED(i); BGI_UNUSED(t); BGI_UNUSED(x); BGI_UNUSED(y); BGI_UNUSED(w); BGI_UNUSED(h);
	return NULL;
}

void OS_ChildClose(OsChild_t* c) { BGI_UNUSED(c); }
void OS_ChildShow(OsChild_t* c, int show) { BGI_UNUSED(c); BGI_UNUSED(show); }
void OS_ChildSetTitle(OsChild_t* c, const char* title) { BGI_UNUSED(c); BGI_UNUSED(title); }
void OS_ChildMove(OsChild_t* c, int x, int y, int w, int h)
{
	BGI_UNUSED(c); BGI_UNUSED(x); BGI_UNUSED(y); BGI_UNUSED(w); BGI_UNUSED(h);
}
void OS_ChildGetPos(OsChild_t* c, int* x, int* y) { BGI_UNUSED(c); *x = *y = 0; }
OsDc_t* OS_ChildGetDc(OsChild_t* c) { BGI_UNUSED(c); return NULL; }
void OS_ChildReleaseDc(OsChild_t* c, OsDc_t* dc) { BGI_UNUSED(c); BGI_UNUSED(dc); }

static jbyteArray Bytes(JNIEnv* env, const char* text)
{
    jsize n = (jsize)strlen(text ? text : "");
    jbyteArray result = (*env)->NewByteArray(env, n);
    if(result) (*env)->SetByteArrayRegion(env, result, 0, n, (const jbyte*)(text ? text : ""));
    return result;
}

static int InputDialog(const char* title, int count, const char* const labels[4], const char* const initial[4],
    const int limits[4], const int digits[4], char* const out[4], int32_t* month, int32_t* day, int compact)
{
    AndroidHost_t* host = gAndroidHost;
    int attached, i, serial, result = 0;
    jint maxima[4];
    JNIEnv* env = OsAndroid_GetEnv(&attached);
    jclass byteArrayClass = NULL;
    jobjectArray jlabels = NULL, jinitial = NULL;
    jbyteArray jtitle = NULL;
    jintArray jlimits = NULL, jdigits = NULL;
    if(!env) return 0;
    OsAndroid_Log("Android script input dialog");
    byteArrayClass = (*env)->FindClass(env, "[B");
    if(!byteArrayClass) goto done;
    jlabels = (*env)->NewObjectArray(env, count, byteArrayClass, NULL);
    if(!jlabels) goto done;
    jinitial = (*env)->NewObjectArray(env, count, byteArrayClass, NULL);
    if(!jinitial) goto done;
    jtitle = Bytes(env, title); if(!jtitle) goto done;
    jlimits = (*env)->NewIntArray(env, count); if(!jlimits) goto done;
    jdigits = (*env)->NewIntArray(env, count); if(!jdigits) goto done;
    for(i = 0; i < count; ++i)
    {
        jbyteArray label = Bytes(env, labels[i]), value;
        if(!label) goto done;
        value = Bytes(env, initial[i]);
        if(!value) { (*env)->DeleteLocalRef(env, label); goto done; }
        (*env)->SetObjectArrayElement(env, jlabels, i, label);
        (*env)->SetObjectArrayElement(env, jinitial, i, value);
        (*env)->DeleteLocalRef(env, label); (*env)->DeleteLocalRef(env, value);
        maxima[i] = limits[i] > 0 && limits[i] < 256 ? limits[i] : 255;
    }
    (*env)->SetIntArrayRegion(env, jlimits, 0, count, maxima);
    for(i = 0; i < count; ++i) { jint digit = digits[i]; (*env)->SetIntArrayRegion(env, jdigits, i, 1, &digit); }
    if((*env)->ExceptionCheck(env)) goto done;
    pthread_mutex_lock(&host->lock);
    serial = ++host->dialogSerial;
    host->dialogPending = 1; host->dialogResult = 0; host->formCount = count;
    for(i = 0; i < count; ++i) host->formLimits[i] = maxima[i];
    pthread_mutex_unlock(&host->lock);
    (*env)->CallVoidMethod(env, host->activity, host->inputDialog, serial,
        gTextGbk ? OS_TEXT_GBK : gTextUtf8 ? OS_TEXT_UTF8 : OS_TEXT_SJIS,
        jtitle, jlabels, jinitial, jlimits, jdigits, month ? *month : 0, day ? *day : 0, compact != 0);
    if((*env)->ExceptionCheck(env)) goto done;
    pthread_mutex_lock(&host->lock);
    while(host->dialogPending && !host->destroyed) pthread_cond_wait(&host->changed, &host->lock);
    result = host->destroyed ? 0 : host->dialogResult;
    host->dialogPending = 0;
    if(result)
    {
        for(i = 0; i < count; ++i) CopyText(out[i], (size_t)maxima[i] + 1, host->formText[i]);
        if(month) *month = host->formMonth;
        if(day) *day = host->formDay;
    }
    pthread_mutex_unlock(&host->lock);
done:
    if(jdigits) (*env)->DeleteLocalRef(env, jdigits);
    if(jlimits) (*env)->DeleteLocalRef(env, jlimits);
    if(jtitle) (*env)->DeleteLocalRef(env, jtitle);
    if(jinitial) (*env)->DeleteLocalRef(env, jinitial);
    if(jlabels) (*env)->DeleteLocalRef(env, jlabels);
    if(byteArrayClass) (*env)->DeleteLocalRef(env, byteArrayClass);
    if((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionDescribe(env); (*env)->ExceptionClear(env);
        pthread_mutex_lock(&host->lock); host->dialogPending = 0; pthread_mutex_unlock(&host->lock);
        OsAndroid_Log("Could not show the Android input dialog.");
    }
    OsAndroid_ReleaseEnv(attached);
    return result;
}

int OS_DialogInput1(const char* title, const char* initial, int maxLen, int digits, char* out)
{
    const char* labels[4] = {""}, *values[4] = {initial};
    int limits[4] = {maxLen}, numeric[4] = {digits};
    char* outputs[4] = {out};
    return InputDialog(title, 1, labels, values, limits, numeric, outputs, NULL, NULL, 0);
}

int OS_DialogInput2(const char* title, const char* label1, const char* initial1, int maxLen1, int digits1, char* out1,
    const char* label2, const char* initial2, int maxLen2, int digits2, char* out2, int compact)
{
    const char* labels[4] = {label1, label2}, *values[4] = {initial1, initial2};
    int limits[4] = {maxLen1, maxLen2}, numeric[4] = {digits1, digits2};
    char* outputs[4] = {out1, out2};
    return InputDialog(title, 2, labels, values, limits, numeric, outputs, NULL, NULL, compact);
}

int OS_DialogProfile(char* surname, char* givenName, char* nickname, char* pronoun, int32_t* month, int32_t* day)
{
    const char* labels[4] = {"", "", "", ""}, *values[4] = {surname, givenName, nickname, pronoun};
    int limits[4] = {10, 10, 10, 10}, numeric[4] = {0};
    char* outputs[4] = {surname, givenName, nickname, pronoun};
    return InputDialog("", 4, labels, values, limits, numeric, outputs, month, day, 0);
}

int OS_LauncherDialog(OsLauncher_t* l) { BGI_UNUSED(l); return -1; }
int OS_DebugWindowOpen(int w, int h, const char* title) { BGI_UNUSED(w); BGI_UNUSED(h); BGI_UNUSED(title); return 0; }
void OS_DebugWindowClose(void) {}
int OS_DebugWindowAlive(void) { return 0; }
void OS_DebugWindowPresent(const OsSurface_t* s) { BGI_UNUSED(s); }

int OS_MessageBox(const char* text, const char* caption, uint32_t flags)
{
    AndroidHost_t* host = gAndroidHost;
    char body[8192], title[1024];
    int attached, serial, result = (flags & 0xf) == OS_MB_YESNO ? OS_IDNO :
        (flags & 0xf) == OS_MB_OK ? OS_IDOK : OS_IDCANCEL;
    JNIEnv* env = OsAndroid_GetEnv(&attached);
    jbyteArray jbody = NULL, jtitle = NULL;
    OS_SjisToUtf8(caption ? caption : "", title, sizeof title);
    /* MsgBoxScript in core/error.c explicitly marks script-owned bodies.
     * Captions and unmarked engine messages retain their CP932 contract. */
    if(caption && strlen(caption)>=9 && !strcmp(caption+strlen(caption)-9," (script)"))
        OsAndroid_ScriptToUtf8(text ? text : "", body, sizeof body);
    else OS_SjisToUtf8(text ? text : "", body, sizeof body);
    OsAndroid_Log(title); OsAndroid_Log(body);
    if(!env) return result;
    jbody = (*env)->NewByteArray(env, (jsize)strlen(body));
    if(jbody) jtitle = (*env)->NewByteArray(env, (jsize)strlen(title));
    if(!jbody || !jtitle) goto failed;
    (*env)->SetByteArrayRegion(env, jbody, 0, (jsize)strlen(body), (const jbyte*)body);
    (*env)->SetByteArrayRegion(env, jtitle, 0, (jsize)strlen(title), (const jbyte*)title);
    pthread_mutex_lock(&host->lock);
    serial = ++host->dialogSerial;
    host->dialogResult = result;
    host->dialogPending = 1;
    pthread_mutex_unlock(&host->lock);
    (*env)->CallVoidMethod(env, host->activity, host->messageBox, jbody, jtitle, (jint)flags, serial);
    if((*env)->ExceptionCheck(env)) goto failed;
    pthread_mutex_lock(&host->lock);
    while(host->dialogPending && !host->destroyed)
        pthread_cond_wait(&host->changed, &host->lock);
    result = host->dialogResult;
    host->dialogPending = 0;
    pthread_mutex_unlock(&host->lock);
failed:
    if(jbody) (*env)->DeleteLocalRef(env, jbody);
    if(jtitle) (*env)->DeleteLocalRef(env, jtitle);
    if((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionDescribe(env); (*env)->ExceptionClear(env);
        pthread_mutex_lock(&host->lock); host->dialogPending = 0; pthread_mutex_unlock(&host->lock);
        OsAndroid_Log("Could not show the Android message dialog.");
    }
    OsAndroid_ReleaseEnv(attached);
    return result;
}

int OS_FileDialog(int save, char* path, size_t n, const char* filter, const char* ext, const char* initial, const char* title)
{
	BGI_UNUSED(save); BGI_UNUSED(path); BGI_UNUSED(n); BGI_UNUSED(filter); BGI_UNUSED(ext); BGI_UNUSED(initial); BGI_UNUSED(title);
	return 0;
}

int OS_FileDialogN(int save, char* path, size_t n, const char* const* descs, const char* const* pats,
	int count, const char* ext, const char* initial, const char* title)
{
	BGI_UNUSED(descs); BGI_UNUSED(pats); BGI_UNUSED(count);
	return OS_FileDialog(save, path, n, NULL, ext, initial, title);
}

int OS_BrowseFolder(char* path, size_t n, const char* title, const char* initial)
{
	BGI_UNUSED(path); BGI_UNUSED(n); BGI_UNUSED(title); BGI_UNUSED(initial);
	return 0;
}

int OS_ClipboardSetText(const char* text) { BGI_UNUSED(text); return 0; }
OsPipeProc_t* OS_PipeProcStart(const char* const* argv) { BGI_UNUSED(argv); return NULL; }
int OS_PipeProcRead(OsPipeProc_t* p, void* buf, int n) { BGI_UNUSED(p); BGI_UNUSED(buf); BGI_UNUSED(n); return -1; }
void OS_PipeProcStop(OsPipeProc_t* p) { BGI_UNUSED(p); }
void OS_PathNative(const char* path, char* out, size_t n) { OsPosix_Path(path, out, n); }
