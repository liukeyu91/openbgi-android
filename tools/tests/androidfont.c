/*
 * This file is part of OpenBGI (https://openbgi.net).
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * androidfont.c - exercise Android's actual FreeType and CP932 back ends
 * on a host. BGI_TEST_CJK_FONT names a Noto CJK font; no Android device
 * or game resources are needed. The same code point must rasterise
 * identically in GBK, SJIS and UTF-8, without a change to its advance.
 */
#include "bgi/os.h"
#include "bgi/os_common.h"
#include "bgi/os_posix.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H
#include <stdio.h>

static int checks, failures;
#define CHECK(c)                                   \
	do                                             \
	{                                              \
		checks++;                                  \
		if(!(c))                                   \
		{                                          \
			printf("FAIL %d: %s\n", __LINE__, #c); \
			failures++;                            \
		}                                          \
	} while(0)
static void Bounds(const uint8_t* image, int* w, int* h)
{
	int l = 128, r = -1, t = 128, b = -1;
	for(int y = 0; y < 128; y++)
		for(int x = 0; x < 128; x++)
			if(image[y * 128 + x])
			{
				if(x < l)
					l = x;
				if(x > r)
					r = x;
				if(y < t)
					t = y;
				if(y > b)
					b = y;
			}
	*w = r >= l ? r - l + 1 : 0;
	*h = b >= t ? b - t + 1 : 0;
}
static void SaveGlyph(const char* prefix, const char* name, const uint8_t* image)
{
	if(!prefix)
		return;
	char path[1024];
	snprintf(path, sizeof path, "%s-%s.pgm", prefix, name);
	FILE* f = fopen(path, "wb");
	CHECK(f != NULL);
	if(f)
	{
		fprintf(f, "P5\n128 128\n255\n");
		CHECK(fwrite(image, 1, 128 * 128, f) == 128 * 128);
		fclose(f);
	}
}
int main(int argc, char** argv)
{
	const char* path = getenv("BGI_TEST_CJK_FONT");
	if(!path || !*path)
	{
		puts("SKIP Android font aspect: set BGI_TEST_CJK_FONT to a Noto Sans/Serif CJK font");
		return 0;
	}
	CHECK(setenv("BGI_FONT", path, 1) == 0);
	FT_Library library;
	FT_Face face;
	FT_Error error = FT_Init_FreeType(&library);
	CHECK(error == 0);
	if(error)
		return 1;
	error = FT_New_Face(library, path, 0, &face);
	CHECK(error == 0);
	if(error)
	{
		FT_Done_FreeType(library);
		return 1;
	}
	const TT_OS2* os2 = (const TT_OS2*)FT_Get_Sfnt_Table(face, FT_SFNT_OS2);
	CHECK(face->family_name && (strstr(face->family_name, "Noto Sans CJK") || strstr(face->family_name, "Noto Serif CJK")));
	CHECK(os2 != NULL);
	if(!os2)
		return 1;
	printf("Font %s: em=%u Win=%u+%u Typo=%d+%d\n", face->family_name, face->units_per_EM, os2->usWinAscent, os2->usWinDescent, os2->sTypoAscender, -os2->sTypoDescender);
	FT_Done_Face(face);
	FT_Done_FreeType(library);
	const int sizes[] = {16, 32, 48, 64}, widths[] = {80, 100, 120};
	const int encodings[] = {OS_TEXT_GBK, OS_TEXT_SJIS, OS_TEXT_UTF8};
	const char* names[] = {"gbk", "sjis", "utf8"};
	/* U+56FD, encoded by the actual production converters. */
	const char* text[] = {"\xb9\xfa", "\x8d\x91", "\xe5\x9b\xbd"};
	uint8_t reference[128 * 128], image[128 * 128], monoReference[16 * 128], mono[16 * 128];
	for(size_t si = 0; si < BGI_COUNTOF(sizes); si++)
		for(size_t wi = 0; wi < BGI_COUNTOF(widths); wi++)
		{
			int referenceAdvance = 0, referenceW = 0, referenceH = 0;
			for(int e = 0; e < 3; e++)
			{
				OS_SetTextEncoding(encodings[e]);
				CHECK(OsCommon_CodePoint(text[e], (int)strlen(text[e]), OsPosix_SjisChar) == 0x56fd);
				OsFont_t* font = OS_FontOpen("MS Gothic", sizes[si], sizes[si] * widths[wi] / 200, 0, 0);
				CHECK(font != NULL);
				if(!font)
					return 1;
				OS_FontRender(font, text[e], (int)strlen(text[e]), image, 128, 128, 128);
				OS_FontRenderMono(font, text[e], (int)strlen(text[e]), mono, 16, 128, 128);
				int w, h;
				Bounds(image, &w, &h);
				int advance = OS_FontCharAdvance(font, text[e], (int)strlen(text[e]));
				CHECK(w > 0 && h > 0);
				CHECK(h * 10 >= sizes[si] * 7);
				if(e == 0)
				{
					memcpy(reference, image, sizeof image);
					memcpy(monoReference, mono, sizeof mono);
					referenceAdvance = advance;
					referenceW = w;
					referenceH = h;
				}
				else
				{
					CHECK(memcmp(reference, image, sizeof image) == 0);
					CHECK(memcmp(monoReference, mono, sizeof mono) == 0);
					CHECK(advance == referenceAdvance);
					CHECK(w == referenceW && h == referenceH);
				}
				if(sizes[si] == 64 && widths[wi] == 100)
				{
					printf("%s ink=%dx%d advance=%d\n", names[e], w, h, advance);
					SaveGlyph(argc > 1 ? argv[1] : NULL, names[e], image);
				}
				OS_FontClose(font);
			}
		}
	OsPosix_FontShutdown();
	OS_SetTextEncoding(OS_TEXT_SJIS);
	printf("Android CJK font aspect: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
