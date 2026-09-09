#include "FontImpl.h"

// ## fix error: unknown type name 'FT_Library'
#include "freetype2/ft2build.h"
#include "freetype2/freetype.h"
#include "freetype2/ftsnames.h"
#include "freetype2/ttnameid.h"
// #include FT_TRUETYPE_IDS_H
// #include FT_SFNT_NAMES_H
// #include FT_FREETYPE_H

#include "StorageIntf.h"
#include "DebugIntf.h"
#include "MsgIntf.h"
#include <map>
#include <math.h>
#include "Application.h"
#include "Platform.h"
#include "ConfigManager/IndividualConfigManager.h"
#ifndef M_PI
#define M_PI       3.14159265358979323846
#endif

#ifdef _MSC_VER
#pragma comment(lib,"freetype.lib")
#endif
#include "platform/CCFileUtils.h"
#include "StorageImpl.h"
#include "BinaryStream.h"

tTJSHashTable<ttstr, TVPFontNamePathInfo, tTVPttstrHash>
    TVPFontNames;
static ttstr TVPDefaultFontName;
// The family name of face 0 of the last font file TVPInternalEnumFonts read.
// The default face used to be whatever key the hash happened to hand back last
// (TVPFontNames.GetLast()), which is an order nothing controls: a file that
// registers several names could leave the default pointing at a name from a
// file that was never the one chosen. Now the file the candidate list actually
// stopped on names the default.
static ttstr TVPPrimaryFontFamily;
const ttstr &TVPGetDefaultFontName() {
	return TVPDefaultFontName;
}
void TVPGetAllFontList(std::vector<ttstr>& list) {
	auto itend = TVPFontNames.GetLast();
	for (auto it = TVPFontNames.GetFirst(); it != itend; ++it) {
		list.push_back(it.GetKey());
	}
}
static FT_Library TVPFontLibrary;
FT_Library &TVPGetFontLibrary() {
	if (!TVPFontLibrary) {
		FT_Error error = FT_Init_FreeType(&TVPFontLibrary);
		if (error) TVPThrowExceptionMessage(
			(ttstr(TJS_W("Initialize FreeType failed, error = ")) + TJSIntegerToString((tjs_int)error)).c_str());
		TVPInitFontNames();
	}
	return TVPFontLibrary;
}
void TVPReleaseFontLibrary() {
	if (TVPFontLibrary) {
		FT_Done_FreeType(TVPFontLibrary);
	}
}
//---------------------------------------------------------------------------
static int TVPInternalEnumFonts(FT_Byte* pBuf, int buflen, const ttstr &FontPath, const std::function<tTJSBinaryStream*(TVPFontNamePathInfo*)>& getter) {
	unsigned int faceCount = 0;
	FT_Face fontface;
	FT_Error error = FT_New_Memory_Face(
		TVPGetFontLibrary(),
		pBuf,
		buflen,
		0,
		&fontface);
	if (error) {
		TVPAddLog(ttstr(TJS_W("Load Font \"") + FontPath + "\" failed (" + TJSIntegerToString((int)error) + ")"));
		return faceCount;
	}
	int nFaceNum = fontface->num_faces;
	for (int i = 0; i < nFaceNum; ++i) {
		if (i > 0) {
			if (FT_New_Memory_Face(
				TVPGetFontLibrary(),
				pBuf,
				buflen,
				i,
				&fontface)) {
				continue;
			}
		}
		if (FT_IS_SCALABLE(fontface)) {
			FT_UInt namecount = FT_Get_Sfnt_Name_Count(fontface);
			int addCount = 0;
			for (FT_UInt i = 0; i < namecount; ++i) {
				FT_SfntName name;
				if (FT_Get_Sfnt_Name(fontface, i, &name)) {
					continue;
				}
				if (name.name_id != TT_NAME_ID_FONT_FAMILY) {
					continue;
				}
				if (name.platform_id != TT_PLATFORM_MICROSOFT) {
					continue;
				}
				switch (name.language_id) { // for CJK names
				case TT_MS_LANGID_JAPANESE_JAPAN:
				case TT_MS_LANGID_CHINESE_GENERAL:
				case TT_MS_LANGID_CHINESE_TAIWAN:
				case TT_MS_LANGID_CHINESE_PRC:
				case TT_MS_LANGID_CHINESE_HONG_KONG:
				case TT_MS_LANGID_CHINESE_SINGAPORE:
				case TT_MS_LANGID_KOREAN_EXTENDED_WANSUNG_KOREA:
				case TT_MS_LANGID_KOREAN_JOHAB_KOREA:
					break;
				default:
					continue;
				}
				ttstr fontname;
				if (name.encoding_id == TT_MS_ID_UNICODE_CS) {
					std::vector<tjs_char> tmp;
					int namelen = name.string_len / 2;
					tmp.resize(namelen + 1);
					for (int j = 0; j < namelen; ++j) {
						tmp[j] = (name.string[j * 2] << 8) | (name.string[j * 2 + 1]);
					}
					fontname = &tmp.front();
				} else {
					continue;
				}
				TVPFontNamePathInfo info;
				info.Path = FontPath;
				info.Index = i;
				info.Getter = getter;
				TVPFontNames.Add(fontname, info);
				addCount = 1;
			}
			/*if (!addCount)*/ {
				ttstr fontname((tjs_nchar*)fontface->family_name);
				TVPFontNamePathInfo info;
				info.Path = FontPath;
				info.Index = i;
				info.Getter = getter;
				TVPFontNames.Add(fontname, info);
				if (i == 0) TVPPrimaryFontFamily = fontname;
			}
			++faceCount;
		}

		FT_Done_Face(fontface);
	}
	return faceCount;
}

int TVPEnumFontsProc(const ttstr &FontPath)
{
    if(!TVPIsExistentStorageNoSearch(FontPath)) {
        return 0;
    }

    tTJSBinaryStream * Stream = TVPCreateStream(FontPath, TJS_BS_READ);
    if(!Stream) {
        return 0;
    }
    int bufflen = Stream->GetSize();
	std::vector<FT_Byte> buf; buf.resize(bufflen);
    Stream->ReadBuffer(&buf.front(), bufflen);
    delete Stream;
	return TVPInternalEnumFonts(&buf.front(), bufflen, FontPath, nullptr);
}

tTJSBinaryStream* TVPCreateFontStream(const ttstr &fontname)
{
	TVPFontNamePathInfo *info = TVPFindFont(fontname);
	if (!info) {
		info = TVPFontNames.Find(TVPDefaultFontName);
		if (!info) return nullptr;
	}
	if (info->Getter) {
		return info->Getter(info);
	}
	return TVPCreateBinaryStreamForRead(info->Path, TJS_W(""));
}

//---------------------------------------------------------------------------
#ifdef __ANDROID__
extern std::vector<ttstr> Android_GetExternalStoragePath();
extern ttstr Android_GetInternalStoragePath();
extern ttstr Android_GetApkStoragePath();

// A font that ships inside the bundle's own assets. Nothing but cocos' FileUtils
// can see an asset, so the getter goes back through it every time FreeType
// reopens the face rather than holding the bytes for the life of the process.
static int TVPEnumFontsInAssets(const char *name)
{
	auto data = cocos2d::FileUtils::getInstance()->getDataFromFile(name);
	if (data.isNull()) return 0;   // the bundle was built without this asset
	return TVPInternalEnumFonts(data.getBytes(), (int)data.getSize(), name,
		[](TVPFontNamePathInfo* info)->tTJSBinaryStream* {
			auto data = cocos2d::FileUtils::getInstance()->getDataFromFile(info->Path.AsStdString());
			tTVPMemoryStream *ret = new tTVPMemoryStream();
			ret->WriteBuffer(data.getBytes(), data.getSize());
			ret->SetPosition(0);
			return ret;
		});
}
#endif

// The Windows font families a Japanese visual novel names, pointed at whatever
// face this build ended up with.
//
// These games are written for a Windows machine running in Japanese, and they
// say so: Noble Works asks for the four faces at the head of this list, and
// PreRenderFontEx.tjs uses the first of them as its FallbackFace -- the face
// every glyph its pre-rendered .tft files do not contain is drawn with, which
// on an English-patched copy is most of the text. None of them exists on a
// console, so every one of those requests used to fall through
// FontSystem::GetBeingFont to the default face anyway; the difference is that
// now the name a game asks for resolves to a face instead of quietly not
// existing, so a script that tests for it, or lists the fonts, gets an answer
// that matches what is drawn.
//
// Mincho is a serif family and the face we ship is a sans: a Mincho request is
// answered with the wrong shape rather than with nothing, which is the trade
// made rather than carry a second 16 MB font for two script lines.
static const tjs_char *const TVPWindowsFontAliases[] = {
	// Named by Noble Works' own scripts and by its English patch
	// (counted in /root/re/nobleworks/all: 37, 9, 2 and 1 uses).
	TJS_W("ＭＳ ゴシック"),      // MS Gothic
	TJS_W("ＭＳ Ｐゴシック"),    // MS PGothic
	TJS_W("ＭＳ Ｐ明朝"),        // MS PMincho
	TJS_W("Tahoma"),
	// The rest of the Japanese Windows set, so a patch or another KAG game
	// naming one of them is not left without a face either.
	TJS_W("ＭＳ 明朝"),          // MS Mincho
	TJS_W("ＭＳ ＵＩ ゴシック"), // MS UI Gothic
	TJS_W("メイリオ"),           // Meiryo (kana)
	TJS_W("游ゴシック"),         // Yu Gothic (kanji)
	TJS_W("MS Gothic"),
	TJS_W("MS PGothic"),
	TJS_W("MS Mincho"),
	TJS_W("MS PMincho"),
	TJS_W("MS UI Gothic"),
	TJS_W("Meiryo"),
	TJS_W("Yu Gothic"),
	TJS_W("Arial"),
	nullptr
};

static void TVPRegisterWindowsFontAliases()
{
	if (TVPDefaultFontName.IsEmpty()) return;
	TVPFontNamePathInfo *found = TVPFontNames.Find(TVPDefaultFontName);
	if (!found) return;
	// By value: adding to the table can rehash it and move what found points at.
	TVPFontNamePathInfo info = *found;
	for (const tjs_char *const *name = TVPWindowsFontAliases; *name; ++name) {
		ttstr alias(*name);
		if (TVPFontNames.Find(alias)) continue;   // a real one was found first
		TVPFontNames.Add(alias, info);
	}
}

void TVPInitFontNames()
{
    static bool TVPFontNamesInit = false;
    // enumlate all fonts
    if(TVPFontNamesInit) return;
	TVPFontNamesInit = true;
#ifdef __ANDROID__
	std::vector<ttstr> pathlist = Android_GetExternalStoragePath();
#endif
	do {
		ttstr userFont = IndividualConfigManager::GetInstance()->GetValue<std::string>("default_font", "");
		if (!userFont.IsEmpty() && TVPEnumFontsProc(userFont)) break;

		if (TVPEnumFontsProc(TVPGetAppPath() + "default.ttf")) break;
		if (TVPEnumFontsProc(TVPGetAppPath() + "default.ttc")) break;
		if (TVPEnumFontsProc(TVPGetAppPath() + "default.otf")) break;
		if (TVPEnumFontsProc(TVPGetAppPath() + "default.otc")) break;
#if defined(__ANDROID__)
		int fontCount = 0;
		for (const ttstr &path : pathlist) {
			fontCount += TVPEnumFontsProc(path + "/default.ttf");
			if (fontCount) break;
		}
		if (fontCount) break;
		
		if (TVPEnumFontsProc(Android_GetInternalStoragePath() + "/default.ttf")) break;

		// The face this bundle ships, and the first thing tried once the player
		// has not named one of their own. A Japanese game read on a console has
		// no Japanese font to fall back on: the two candidates that follow are
		// a Simplified-Chinese fallback face whose kanji are the wrong forms,
		// and a Latin-only one. Noto Sans CJK JP is the face Locale Emulator's
		// Japanese charset selection would have reached on Windows.
		if (TVPEnumFontsInAssets("NotoSansCJKjp-Regular.otf")) break;

		// Then the console's own CJK face, which is what a bundle built without
		// that asset gets. NotoSansCJK-Regular.ttc is where Android has kept
		// its CJK coverage since 7.0; face 0 of it is the JP one.
		if (TVPEnumFontsProc(TJS_W("file://./system/fonts/NotoSansCJK-Regular.ttc"))) break;
		if (TVPEnumFontsProc(TJS_W("file://./system/fonts/NotoSansJP-Regular.otf"))) break;

		if (TVPEnumFontsInAssets("DroidSansFallback.ttf")) break;
		if (TVPEnumFontsProc(TJS_W("file://./system/fonts/DroidSansFallback.ttf"))) break;
		if (TVPEnumFontsProc(TJS_W("file://./system/fonts/NotoSansHans-Regular.otf"))) break;
		if (TVPEnumFontsProc(TJS_W("file://./system/fonts/DroidSans.ttf"))) break;
#elif defined(WIN32)
		if (TVPEnumFontsProc(TJS_W("file://./c/windows/fonts/msyh.ttf"))) break;
		if (TVPEnumFontsProc(TJS_W("file://./c/windows/fonts/simhei.ttf"))) break;
#endif
        
        std::string fullPath = cocos2d::FileUtils::getInstance()->fullPathForFilename("DroidSansFallback.ttf");
        if (TVPEnumFontsProc(fullPath)) break;
	} while (false);
    if(!TVPPrimaryFontFamily.IsEmpty())
    {
        // the file the list above stopped on
        TVPDefaultFontName = TVPPrimaryFontFamily;
    }
    else if(TVPFontNames.GetCount() > 0)
    {
        TVPDefaultFontName = TVPFontNames.GetLast().GetKey();
    }

    // check exePath + "/fonts/*.ttf"
	{
		std::vector<ttstr> list;
		auto lister = [&](const ttstr &name, tTVPLocalFileInfo* s) {
			if (s->Mode & (S_IFREG | S_IFDIR)) {
				list.emplace_back(name);
			}
		};
#ifdef __ANDROID__
		TVPGetLocalFileListAt(Android_GetInternalStoragePath() + "/fonts", lister);
		for (const ttstr &path : pathlist) {
			TVPGetLocalFileListAt(path + "/fonts", lister);
		}
#endif
		TVPGetLocalFileListAt(TVPGetAppPath() + "/fonts", lister);
        auto itend = list.end();
        for (auto it = list.begin(); it != itend; ++it) {
            TVPEnumFontsProc(*it);
        }
    }

	// After the fonts/ scan, so a real MS Gothic dropped in there wins the name
	// over the alias.
	TVPRegisterWindowsFontAliases();

	if (TVPDefaultFontName.IsEmpty()) {
		TVPShowSimpleMessageBox(("Could not found any font.\nPlease ensure that at least \"default.ttf\" exists"), "Exception Occured");
    } else {
		// One line in krkr.console.log saying which face the text is drawn
		// with, because "the Japanese is boxes" and "the English is boxes" look
		// the same on screen and have different answers.
		TVPAddLog(ttstr(TJS_W("Default font face: ")) + TVPDefaultFontName);
	}
}
//---------------------------------------------------------------------------
TVPFontNamePathInfo* TVPFindFont(const ttstr &fontname)
{
    // check existence of font
    TVPInitFontNames();

	TVPFontNamePathInfo *info = nullptr;
	if (!fontname.IsEmpty() && fontname[0] == TJS_W('@')) { // vertical version
		info = TVPFontNames.Find(fontname.c_str() + 1);
	}
	if (!info) {
		info = TVPFontNames.Find(fontname);
	}
    return info;
}

tjs_uint32 tTVPttstrHash::Make( const ttstr &val )
{
    const tjs_char * ptr = val.c_str();
    if(*ptr == 0) return 0;
    tjs_uint32 v = 0;
    while(*ptr)
    {
        v += *ptr;
        v += (v << 10);
        v ^= (v >> 6);
        ptr++;
    }
    v += (v << 3);
    v ^= (v >> 11);
    v += (v << 15);
    if(!v) v = (tjs_uint32)-1;
    return v;
}
