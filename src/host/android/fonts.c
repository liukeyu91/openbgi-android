/* SPDX-License-Identifier: GPL-2.0-only */
#include "fonts.h"
#include "bgi/os.h"
#include "bgi/os_posix.h"

static void* OpenFont(void* user, const char* name, int height, int width, int bold, int italic)
{
    (void)user;
    return OS_FontOpen(name, height, width, bold, italic);
}

static void* OpenMono(void* user, const char* name, int size, int bold)
{
    (void)user;
    return OS_FontOpenMono(name, size, bold);
}

static void CloseFont(void* user, void* font)
{
    (void)user;
    OS_FontClose(font);
}

static void Render(void* user, void* font, const char* ch, int len, uint8_t* buf, int pitch, int w, int h)
{
    (void)user;
    OS_FontRender(font, ch, len, buf, pitch, w, h);
}

static void RenderMono(void* user, void* font, const char* ch, int len, uint8_t* buf, int pitch, int w, int h)
{
    (void)user;
    OS_FontRenderMono(font, ch, len, buf, pitch, w, h);
}

static int Advance(void* user, void* font, const char* ch, int len)
{
    (void)user;
    return OS_FontCharAdvance(font, ch, len);
}

/* Host_FileAttrs also sees fonts inside ISO and ZIP sources. Read those via
 * the same storage layer when FreeType cannot open a loose file. */
static int AddFile(void* user, const char* path)
{
    (void)user;
    if(OS_FontAddFile(path)) return 1;
    OsFile_t* file = OS_FileOpenRead(path);
    if(!file) return 0;
    uint32_t size = OS_FileSize(file);
    uint8_t* data = size ? malloc(size) : NULL;
    int ok = data && OS_FileRead(file, data, size) == size && OS_FontAddMemory(data, size);
    free(data);
    OS_FileClose(file);
    return ok;
}

static int AddMemory(void* user, const void* data, uint32_t size)
{
    (void)user;
    return OS_FontAddMemory(data, size);
}

static int FaceExists(void* user, const char* face)
{
    (void)user;
    return OS_FontFaceExists(face);
}

static int EnumFaces(void* user, char* buf, int charset, int* bytes)
{
    (void)user;
    return OS_FontEnumFaces(buf, charset, bytes);
}

void AndroidHostFontsClose(void)
{
    OsPosix_FontShutdown();
}

const OpenBGI_FontRenderer_t AndroidHostFonts = {
    .size = sizeof(OpenBGI_FontRenderer_t),
    .open = OpenFont, .openMono = OpenMono, .close = CloseFont,
    .render = Render, .renderMono = RenderMono, .advance = Advance,
    .addFile = AddFile, .addMemory = AddMemory, .faceExists = FaceExists, .enumFaces = EnumFaces,
};
