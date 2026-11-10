/*
 * This file is part of OpenBGI (https://openbgi.net).
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Services outside the first Activity milestone. Keep the edit buffer
 * readable by the game while the native text control is still absent.
 */
#include "android_internal.h"
#include "bgi/os_common.h"
#include "bgi/strutil.h"
#include "bgi/os_posix.h"

static char gEditText[1024];
static int gEditLimit;

void OS_EditShow(int show) { BGI_UNUSED(show); }
void OS_EditMove(int x, int y, int w, int h) { BGI_UNUSED(x); BGI_UNUSED(y); BGI_UNUSED(w); BGI_UNUSED(h); }
void OS_EditSetFont(const char* face, int h, int w) { BGI_UNUSED(face); BGI_UNUSED(h); BGI_UNUSED(w); }
void OS_EditSelectAll(void) {}
void OS_EditFocus(int toEdit) { BGI_UNUSED(toEdit); }
void OS_EditSetColour(uint32_t rgb) { BGI_UNUSED(rgb); }
void OS_EditRefresh(void) {}
void OS_EditRejectAscii(int reject) { BGI_UNUSED(reject); }
int OS_EditImeOpen(void) { return 0; }

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
	gEditLimit = maxChars;
}

void OS_EditSetText(const char* text)
{
	size_t cap = sizeof gEditText;
	if(gEditLimit > 0 && (size_t)gEditLimit < cap - 1)
		cap = (size_t)gEditLimit + 1;
	CopyText(gEditText, cap, text ? text : "");
}

int OS_EditGetText(char* out, size_t n)
{
	return (int)CopyText(out, n, gEditText);
}

void OS_EditDestroy(void)
{
	gEditText[0] = 0;
	gEditLimit = 0;
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

int OS_DialogInput1(const char* title, const char* initial, int maxLen, int digits, char* out)
{
	BGI_UNUSED(title); BGI_UNUSED(initial); BGI_UNUSED(maxLen); BGI_UNUSED(digits); BGI_UNUSED(out);
	return 0;
}

int OS_DialogInput2(const char* title, const char* label1, const char* initial1, int maxLen1, int digits1, char* out1,
	const char* label2, const char* initial2, int maxLen2, int digits2, char* out2, int compact)
{
	BGI_UNUSED(title); BGI_UNUSED(label1); BGI_UNUSED(initial1); BGI_UNUSED(maxLen1); BGI_UNUSED(digits1); BGI_UNUSED(out1);
	BGI_UNUSED(label2); BGI_UNUSED(initial2); BGI_UNUSED(maxLen2); BGI_UNUSED(digits2); BGI_UNUSED(out2); BGI_UNUSED(compact);
	return 0;
}

int OS_DialogProfile(char* surname, char* givenName, char* nickname, char* pronoun, int32_t* month, int32_t* day)
{
	BGI_UNUSED(surname); BGI_UNUSED(givenName); BGI_UNUSED(nickname); BGI_UNUSED(pronoun); BGI_UNUSED(month); BGI_UNUSED(day);
	return 0;
}

int OS_LauncherDialog(OsLauncher_t* l) { BGI_UNUSED(l); return -1; }
int OS_DebugWindowOpen(int w, int h, const char* title) { BGI_UNUSED(w); BGI_UNUSED(h); BGI_UNUSED(title); return 0; }
void OS_DebugWindowClose(void) {}
int OS_DebugWindowAlive(void) { return 0; }
void OS_DebugWindowPresent(const OsSurface_t* s) { BGI_UNUSED(s); }

int OS_MessageBox(const char* text, const char* caption, uint32_t flags)
{
	char utf8[2048];
	OS_SjisToUtf8(caption ? caption : "", utf8, sizeof utf8);
	OsAndroid_Log(utf8);
	OS_SjisToUtf8(text ? text : "", utf8, sizeof utf8);
	OsAndroid_Log(utf8);
	switch(flags & 0xf)
	{
		case OS_MB_YESNO: return OS_IDNO;
		case OS_MB_OKCANCEL:
		case OS_MB_RETRYCANCEL: return OS_IDCANCEL;
		default: return OS_IDOK;
	}
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
