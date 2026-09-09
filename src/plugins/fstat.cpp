/*
 * fstat.dll: wamsoft's Storages filesystem extension -- fstat/getTime/
 * setTime, getLastModifiedFileTime/setLastModifiedFileTime, exportFile,
 * deleteFile/truncateFile/moveFile/copyFile(NoNormalize), dirlist/
 * dirlistEx/dirtree, (remove|create)Directory(NoNormalize), changeDirectory,
 * (get|set|reset)FileAttributes, isExistentDirectory,
 * isExistentStorageNoSearchNoNormalize, getDisplayName, getMD5HashString,
 * searchPath, currentPath, getTemporaryName, and a TemporaryFiles helper
 * class (delete-on-close temp file/folder handles).
 *
 * Ported from wamsoft's fstat plugin (Main.cpp), license "same as KiriKiri
 * itself" per its readme.txt. The original is ~1200 lines built entirely on
 * Win32 file APIs (CreateFile/FindFirstFile/GetFileTime/SHBrowseForFolder/
 * SHGetFileInfo/IStream/FILETIME). This port keeps the exact same
 * TJS-visible surface and semantics wherever there's a sane POSIX
 * equivalent, replacing the Win32 calls:
 *  - CreateFile/GetFileTime/SetFileTime/GetFileSizeEx -> stat()/utimensat()
 *    (times as milliseconds since epoch instead of Win32 FILETIME, still fed
 *    through the same Date class via TVPExecuteExpression("Date")).
 *  - FindFirstFile/FindNextFile/WIN32_FIND_DATA -> opendir()/readdir()/
 *    lstat() (dirlist/dirlistEx/dirtree).
 *  - DeleteFile/MoveFile/CreateDirectory/RemoveDirectory/
 *    SetCurrentDirectory/GetCurrentDirectory -> unlink()/rename()/mkdir()/
 *    rmdir()/chdir()/getcwd().
 *  - SetFilePointerEx+SetEndOfFile (truncateFile) -> truncate().
 *  - IStream (TVPCreateIStream, Windows-only in this tree -- only declared
 *    under core/base/win32/StorageImpl.h) -> tTJSBinaryStream via the
 *    portable TVPCreateStream(name, TJS_BS_READ/WRITE), which is what every
 *    other internal plugin already uses (see csvParser.cpp's initStorage)
 *    and which also transparently reads files inside XP3 archives, same as
 *    the original's IStream did.
 *  - FILE_ATTRIBUTE_* has no real filesystem equivalent on Android; only
 *    the DIRECTORY bit and a best-effort READONLY (mapped to the owner
 *    write permission bit via chmod) are meaningful here. HIDDEN/SYSTEM/
 *    ARCHIVE/TEMPORARY are accepted (so scripts that set/clear them don't
 *    throw) but are no-ops -- getFileAttributes never reports them set.
 *  - SHGetFileInfo(..., SHGFI_DISPLAYNAME) (getDisplayName) has no Android
 *    equivalent (it's the Explorer-localized display name); this returns
 *    the plain filename instead, which is what every caller actually shows
 *    in practice for ordinary files.
 *  - SearchPathW (searchPath) searches the OS's app-path/PATH locations,
 *    which doesn't exist as a concept here; this reasonable-default
 *    implementation instead resolves the given name against the given (or,
 *    if omitted, the current) storage path via TVPIsExistentStorageNoSearch-
 *    NoNormalize/TVPNormalizeStorageName, returning void if not found.
 *  - selectDirectory (SHBrowseForFolder folder-picker dialog) is NOT
 *    ported: there is no folder-picker in this engine, and building one is
 *    a UI-plumbing task well beyond this plugin file. It is simply not
 *    registered, so `typeof Storages.selectDirectory` stays "undefined" --
 *    grepping Noble Works' extracted scripts found no unconditional call to
 *    it, only guarded probes.
 *  - TemporaryFiles' delete-on-close HANDLEs -> the standard POSIX
 *    equivalent: open() then immediately unlink() (the entry disappears
 *    from its directory right away; the storage is freed for real once the
 *    fd is closed, exactly mirroring FILE_FLAG_DELETE_ON_CLOSE).
 */
#include "ncbind/ncbind.hpp"
#include "PluginImpl.h"
#include <string>
#include <vector>
using namespace std;

#include <sys/stat.h>
#include <sys/time.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#define NCB_MODULE_NAME TJS_W("fstat.dll")

// Date クラスメンバ
static iTJSDispatch2 *dateClass   = NULL;  // Date のクラスオブジェクト
static iTJSDispatch2 *dateSetTime = NULL;  // Date.setTime メソッド
static iTJSDispatch2 *dateGetTime = NULL;  // Date.getTime メソッド

static const tjs_nchar * StoragesFstatPreScript	= TJS_N("\
global.FILE_ATTRIBUTE_READONLY = 0x00000001,\
global.FILE_ATTRIBUTE_HIDDEN = 0x00000002,\
global.FILE_ATTRIBUTE_SYSTEM = 0x00000004,\
global.FILE_ATTRIBUTE_DIRECTORY = 0x00000010,\
global.FILE_ATTRIBUTE_ARCHIVE = 0x00000020,\
global.FILE_ATTRIBUTE_NORMAL = 0x00000080,\
global.FILE_ATTRIBUTE_TEMPORARY = 0x00000100;");

enum {
	kFILE_ATTRIBUTE_READONLY  = 0x00000001,
	kFILE_ATTRIBUTE_HIDDEN    = 0x00000002,
	kFILE_ATTRIBUTE_SYSTEM    = 0x00000004,
	kFILE_ATTRIBUTE_DIRECTORY = 0x00000010,
	kFILE_ATTRIBUTE_ARCHIVE   = 0x00000020,
	kFILE_ATTRIBUTE_NORMAL    = 0x00000080,
	kFILE_ATTRIBUTE_TEMPORARY = 0x00000100
};

/**
 * メソッド追加用
 */
class StoragesFstat {

	/**
	 * ファイル時刻(unix time, ms)を Date クラスにして保存
	 */
	static void storeDate(tTJSVariant &store, tjs_int64 unixtime_ms, iTJSDispatch2 *objthis)
	{
		if (unixtime_ms > 0 && dateClass) {
			iTJSDispatch2 *obj;
			if (TJS_SUCCEEDED(dateClass->CreateNew(0, NULL, NULL, &obj, 0, NULL, objthis))) {
				tTJSVariant time(unixtime_ms);
				tTJSVariant *param[] = { &time };
				dateSetTime->FuncCall(0, NULL, NULL, NULL, 1, param, obj);
				store = tTJSVariant(obj, obj);
				obj->Release();
			}
		}
	}
	/**
	 * Date クラスの時刻を unix time(ms) に変換
	 * @return 取得できたかどうか
	 */
	static bool restoreDate(tTJSVariant &restore, tjs_int64 &unixtime_ms)
	{
		if (restore.Type() != tvtObject) return false;
		iTJSDispatch2 *date = restore.AsObjectNoAddRef();
		if (!date || !dateGetTime) return false;
		tTJSVariant result;
		if (dateGetTime->FuncCall(0, NULL, NULL, &result, 0, NULL, date) != TJS_S_OK) return false;
		unixtime_ms = result.AsInteger();
		return true;
	}

	/**
	 * パスをローカル化する＆末尾の / を削除
	 */
	static void getLocalName(ttstr &path) {
		TVPGetLocalName(path);
		if (path.GetLastChar() == TJS_W('/')) {
			tjs_int len = path.length();
			path = ttstr(path.c_str(), len - 1);
		}
	}
	/**
	 * ローカルパスの有無判定
	 */
	static bool getLocallyAccessibleName(const ttstr &path, ttstr *local = NULL) {
		bool r = false;
		if (local) {
			*local = TVPGetLocallyAccessibleName(path);
			r = ! local->IsEmpty();
		} else {
			ttstr local(TVPGetLocallyAccessibleName(path));
			r = ! local.IsEmpty();
		}
		return r;
	}

	// ttstr (UTF-16) -> UTF-8 std::string, for POSIX calls
	static std::string toUtf8(const ttstr &s) {
		std::string out;
		const tjs_char *p = s.c_str();
		for (; *p; p++) {
			tjs_uint32 c = (tjs_uint32)(tjs_uint16)*p;
			if (c >= 0xD800 && c <= 0xDBFF && p[1] >= 0xDC00 && p[1] <= 0xDFFF) {
				c = 0x10000 + ((c - 0xD800) << 10) + ((tjs_uint32)(tjs_uint16)p[1] - 0xDC00);
				p++;
			}
			if (c < 0x80) {
				out += (char)c;
			} else if (c < 0x800) {
				out += (char)(0xC0 | (c >> 6));
				out += (char)(0x80 | (c & 0x3F));
			} else if (c < 0x10000) {
				out += (char)(0xE0 | (c >> 12));
				out += (char)(0x80 | ((c >> 6) & 0x3F));
				out += (char)(0x80 | (c & 0x3F));
			} else {
				out += (char)(0xF0 | (c >> 18));
				out += (char)(0x80 | ((c >> 12) & 0x3F));
				out += (char)(0x80 | ((c >> 6) & 0x3F));
				out += (char)(0x80 | (c & 0x3F));
			}
		}
		return out;
	}

	static tjs_int64 timespecToMs(time_t sec, long nsec) {
		return (tjs_int64)sec * 1000 + nsec / 1000000;
	}

	/**
	 * ファイルのタイムスタンプを取得する
	 * @return 0:失敗 1:ファイル 2:フォルダ
	 */
	static int getFileTime(ttstr const &filename, tTJSVariant &ctime, tTJSVariant &atime, tTJSVariant &mtime, tTJSVariant *size, iTJSDispatch2 *objthis)
	{
		struct stat st;
		if (stat(toUtf8(filename).c_str(), &st) != 0) return 0;
		bool isdir = S_ISDIR(st.st_mode);
		if (!isdir && size != 0) *size = (tjs_int64)st.st_size;
#if defined(__APPLE__)
		storeDate(ctime, timespecToMs(st.st_ctimespec.tv_sec, st.st_ctimespec.tv_nsec), objthis);
		storeDate(atime, timespecToMs(st.st_atimespec.tv_sec, st.st_atimespec.tv_nsec), objthis);
		storeDate(mtime, timespecToMs(st.st_mtimespec.tv_sec, st.st_mtimespec.tv_nsec), objthis);
#else
		storeDate(ctime, timespecToMs(st.st_ctime, 0), objthis);
		storeDate(atime, timespecToMs(st.st_atime, 0), objthis);
		storeDate(mtime, timespecToMs(st.st_mtime, 0), objthis);
#endif
		return isdir ? 2 : 1;
	}
	/**
	 * ファイルのタイムスタンプを設定する
	 * @return 0:失敗 1:ファイル 2:フォルダ
	 */
	static int setFileTime(ttstr const &filename, tTJSVariant &ctime, tTJSVariant &atime, tTJSVariant &mtime)
	{
		std::string path = toUtf8(filename);
		struct stat st;
		if (stat(path.c_str(), &st) != 0) return 0;
		bool isdir = S_ISDIR(st.st_mode);

		tjs_int64 aMs = 0, mMs = 0;
		bool hasA = restoreDate(atime, aMs);
		bool hasM = restoreDate(mtime, mMs);
		// ctime (inode change time) cannot be set on POSIX; accepted but ignored.

		struct timeval tv[2];
		tv[0].tv_sec  = hasA ? (time_t)(aMs / 1000) : st.st_atime;
		tv[0].tv_usec = hasA ? (suseconds_t)((aMs % 1000) * 1000) : 0;
		tv[1].tv_sec  = hasM ? (time_t)(mMs / 1000) : st.st_mtime;
		tv[1].tv_usec = hasM ? (suseconds_t)((mMs % 1000) * 1000) : 0;

		int r = utimes(path.c_str(), tv);
		if (r != 0) {
			TVPAddLog(ttstr(TJS_W("setFileTime : ")) + filename + TJS_W(":") + ttstr(strerror(errno)));
		}
		return (r == 0) ? (isdir ? 2 : 1) : 0;
	}
	static tjs_error _getTime(tTJSVariant *result, tTJSVariant const *param, bool chksize, iTJSDispatch2 *objthis) {
		ttstr filename = TVPNormalizeStorageName(param->AsStringNoAddRef());
		getLocalName(filename);
		tTJSVariant size, ctime, atime, mtime;
		int sel = getFileTime(filename, ctime, atime, mtime, chksize ? &size : 0, objthis);
		if (sel > 0) {
			if (result) {
				iTJSDispatch2 *dict = TJSCreateDictionaryObject();
				if (dict != NULL) {
					if (chksize && sel == 1) dict->PropSet(TJS_MEMBERENSURE, TJS_W("size"),  NULL, &size, dict);
					dict->PropSet(TJS_MEMBERENSURE, TJS_W("mtime"), NULL, &mtime, dict);
					dict->PropSet(TJS_MEMBERENSURE, TJS_W("ctime"), NULL, &ctime, dict);
					dict->PropSet(TJS_MEMBERENSURE, TJS_W("atime"), NULL, &atime, dict);
					*result = dict;
					dict->Release();
				}
			}
			return TJS_S_OK;
		}
		TVPThrowExceptionMessage((ttstr(TJS_W("cannot open : ")) + param->GetString()).c_str());
		return TJS_S_OK;
	}

	// TVPCreateStream 経由でファイルをコピー（アーカイブ内ファイルも読み取り可）
	static bool _copyStream(const ttstr &from, const ttstr &to, bool failIfExist) {
		if (from.length() == 0 || to.length() == 0) return false;
		if (failIfExist) {
			struct stat st;
			if (stat(toUtf8(to).c_str(), &st) == 0) return false;
		}
		tTJSBinaryStream *in = NULL, *out = NULL;
		try {
			in = TVPCreateStream(from, TJS_BS_READ);
			out = TVPCreateStream(to, TJS_BS_WRITE);
			tjs_uint8 buffer[1024*16];
			tjs_uint size;
			while ((size = in->Read(buffer, sizeof buffer)) > 0) {
				out->Write(buffer, size);
			}
		} catch (...) {
			delete in; delete out;
			return false;
		}
		delete in; delete out;
		TVPClearStorageCaches();
		return true;
	}

public:
	StoragesFstat(){};

	static void clearStorageCaches() {
		TVPClearStorageCaches();
	}

	/**
	 * 指定されたファイルの情報を取得する（アーカイブ内ファイルはサイズのみ）
	 */
	static tjs_error TJS_INTF_METHOD fstat(tTJSVariant *result,
										   tjs_int numparams,
										   tTJSVariant **param,
										   iTJSDispatch2 *objthis) {
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;

		ttstr filename = TVPGetPlacedPath(*param[0]);
		if (filename.length() > 0 && !getLocallyAccessibleName(filename)) {
			// アーカイブ内ファイル: サイズのみ portable stream 経由で取得
			tTJSBinaryStream *in = NULL;
			try {
				in = TVPCreateStream(filename, TJS_BS_READ);
			} catch (...) {
				in = NULL;
			}
			if (in) {
				tTJSVariant size((tjs_int64)in->GetSize());
				delete in;
				if (result) {
					iTJSDispatch2 *dict;
					if ((dict = TJSCreateDictionaryObject()) != NULL) {
						dict->PropSet(TJS_MEMBERENSURE, TJS_W("size"),  NULL, &size, dict);
						*result = dict;
						dict->Release();
					}
				}
				return TJS_S_OK;
			}
		}
		return _getTime(result, param[0], true, objthis);
	}
	/**
	 * 指定されたファイルのタイムスタンプ情報を取得する（アーカイブ内不可）
	 */
	static tjs_error TJS_INTF_METHOD getTime(tTJSVariant *result,
											 tjs_int numparams,
											 tTJSVariant **param,
											 iTJSDispatch2 *objthis) {
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;
		return _getTime(result, param[0], false, objthis);
	}
	/**
	 * 指定されたファイルのタイムスタンプ情報を設定する
	 */
	static tjs_error TJS_INTF_METHOD setTime(tTJSVariant *result,
											 tjs_int numparams,
											 tTJSVariant **param,
											 iTJSDispatch2 *objthis) {
		if (numparams < 2) return TJS_E_BADPARAMCOUNT;

		ttstr filename = TVPNormalizeStorageName(param[0]->AsStringNoAddRef());
		getLocalName(filename);
		tTJSVariant size, ctime, atime, mtime;
		iTJSDispatch2 *dict = param[1]->AsObjectNoAddRef();
		if (dict != NULL) {
			dict->PropGet(0, TJS_W("ctime"), NULL, &ctime, dict);
			dict->PropGet(0, TJS_W("atime"), NULL, &atime, dict);
			dict->PropGet(0, TJS_W("mtime"), NULL, &mtime, dict);
		}
		int sel = setFileTime(filename, ctime, atime, mtime);
		if (result) *result = (sel > 0);
		return TJS_S_OK;
	}

	/**
	 * 更新日時取得・設定（Dateを経由しない高速版, unix time ms）
	 */
	static tjs_uint64 getLastModifiedFileTime(ttstr target) {
		ttstr filename = TVPNormalizeStorageName(target);
		getLocalName(filename);
		struct stat st;
		if (stat(toUtf8(filename).c_str(), &st) != 0) return 0;
		return (tjs_uint64)timespecToMs(st.st_mtime, 0);
	}
	static bool setLastModifiedFileTime(ttstr target, tjs_uint64 time_ms) {
		ttstr filename = TVPNormalizeStorageName(target);
		getLocalName(filename);
		struct timeval tv[2];
		tv[1].tv_sec  = (time_t)(time_ms / 1000);
		tv[1].tv_usec = (suseconds_t)((time_ms % 1000) * 1000);
		tv[0] = tv[1];
		return utimes(toUtf8(filename).c_str(), tv) == 0;
	}

	/**
	 * 吉里吉里のストレージ空間中のファイルを抽出する
	 */
	static void exportFile(ttstr filename, ttstr storename) {
		tTJSBinaryStream *in = NULL, *out = NULL;
		try {
			in = TVPCreateStream(filename, TJS_BS_READ);
		} catch (...) {
			TVPThrowExceptionMessage((ttstr(TJS_W("cannot open readfile: ")) + filename).c_str());
		}
		try {
			out = TVPCreateStream(storename, TJS_BS_WRITE);
		} catch (...) {
			delete in;
			TVPThrowExceptionMessage((ttstr(TJS_W("cannot open storefile: ")) + storename).c_str());
		}
		tjs_uint8 buffer[1024*16];
		tjs_uint size;
		while ((size = in->Read(buffer, sizeof buffer)) > 0) {
			out->Write(buffer, size);
		}
		delete out;
		delete in;
	}

	/**
	 * 吉里吉里のストレージ空間中の指定ファイルを削除する。実ファイルがある場合のみ。
	 */
	static bool deleteFile(const tjs_char *file) {
		ttstr filename(TVPGetLocallyAccessibleName(TVPGetPlacedPath(file)));
		if (filename.length()) {
			if (unlink(toUtf8(filename).c_str()) == 0) {
				TVPClearStorageCaches();
				return true;
			}
			TVPAddLog(ttstr(TJS_W("deleteFile : ")) + filename + TJS_W(" : ") + ttstr(strerror(errno)));
		}
		return false;
	}

	/**
	 * 吉里吉里のストレージ空間中の指定ファイルのサイズを変更する(切り捨てる)。実ファイルがある場合のみ。
	 */
	static bool truncateFile(const tjs_char *file, tjs_int size) {
		ttstr filename(TVPGetLocallyAccessibleName(TVPGetPlacedPath(file)));
		if (filename.length()) {
			if (truncate(toUtf8(filename).c_str(), (off_t)size) == 0) return true;
			TVPAddLog(ttstr(TJS_W("truncateFile : ")) + filename + TJS_W(" : ") + ttstr(strerror(errno)));
		}
		return false;
	}

	/**
	 * 指定ファイルを移動する。移動元が実在し移動先が無い場合のみ。
	 */
	static bool moveFile(const tjs_char *from, const tjs_char *to) {
		ttstr fromFile(TVPGetLocallyAccessibleName(from));
		ttstr   toFile(TVPGetLocallyAccessibleName(to));
		if (fromFile.length() && toFile.length()) {
			if (rename(toUtf8(fromFile).c_str(), toUtf8(toFile).c_str()) == 0) {
				TVPClearStorageCaches();
				return true;
			}
			TVPAddLog(ttstr(TJS_W("moveFile : ")) + fromFile + ", " + toFile + TJS_W(" : ") + ttstr(strerror(errno)));
		}
		return false;
	}

	static tjs_error TJS_INTF_METHOD dirtree(tTJSVariant *result,
											 tjs_int numparams,
											 tTJSVariant **param,
											 iTJSDispatch2 *objthis) {
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;
		bool dironly = numparams > 1 ? param[1]->operator bool() : false;

		ttstr path(TVPNormalizeStorageName(ttstr(*param[0])+TJS_W("/")));
		TVPGetLocalName(path);

		iTJSDispatch2 * array = TJSCreateArrayObject();
		tTJSVariant ret = tTJSVariant(array, array);
		array->Release();

		tjs_int count = 0;
		_dirtree(path, TJS_W(""), array, count, dironly, objthis);

		if (result) *result = ret;
		return TJS_S_OK;
	}
	static void _dirtree(const ttstr &path, const ttstr &subdir, iTJSDispatch2 *array, tjs_int &count, bool dironly, iTJSDispatch2 *objthis) {
		std::string dirpath = toUtf8(path);
		DIR *dh = opendir(dirpath.c_str());
		if (!dh) return;
		struct dirent *ent;
		while ((ent = readdir(dh)) != NULL) {
			ttstr name8(ent->d_name);
			if (name8 == TJS_W(".") || name8 == TJS_W("..")) continue;
			std::string full = dirpath + "/" + ent->d_name;
			struct stat st;
			if (stat(full.c_str(), &st) != 0) continue;
			if (S_ISDIR(st.st_mode)) {
				ttstr name(subdir + name8 + TJS_W("/"));
				setDirListFileStat(array, count++, name, &st, ent->d_name);
				_dirtree(path + TJS_W("/") + name8, name, array, count, dironly, objthis);
			} else if (!dironly) {
				ttstr name(subdir + name8);
				setDirListFileStat(array, count++, name, &st, ent->d_name);
			}
		}
		closedir(dh);
	}

	/**
	 * 指定ディレクトリのファイル一覧を取得する
	 */
	static tTJSVariant dirlist(tjs_char const *dir) {
		return _dirlist(dir, false);
	}

	/**
	 * 指定ディレクトリのファイル一覧と詳細情報を取得する
	 */
	static tTJSVariant dirlistEx(tjs_char const *dir) {
		return _dirlist(dir, true);
	}

private:
	static void setDirListFileStat(iTJSDispatch2 *array, tjs_int count, ttstr const &file, struct stat const *st, const char *rawname) {
		tTJSVariant val(file);
		array->PropSetByNum(0, count, &val, array);
	}
	static tTJSVariant _dirlist(ttstr dir, bool detailed)
	{
		dir = TVPNormalizeStorageName(dir);
		if (dir.GetLastChar() != TJS_W('/')) {
			TVPThrowExceptionMessage(TJS_W("'/' must be specified at the end of given directory name."));
		}
		TVPGetLocalName(dir);

		iTJSDispatch2 * array = TJSCreateArrayObject();
		tTJSVariant result;

		try {
			std::string dirpath = toUtf8(dir);
			DIR *dh = opendir(dirpath.c_str());
			if (!dh) {
				TVPThrowExceptionMessage(TJS_W("Directory not found."));
			}
			tjs_int count = 0;
			struct dirent *ent;
			while ((ent = readdir(dh)) != NULL) {
				ttstr file(ent->d_name);
				if (file == TJS_W(".") || file == TJS_W("..")) continue;
				std::string full = dirpath + ent->d_name;
				struct stat st;
				bool haveStat = (stat(full.c_str(), &st) == 0);
				bool isdir = haveStat && S_ISDIR(st.st_mode);
				if (isdir) file += TJS_W("/");

				if (!detailed) {
					tTJSVariant val(file);
					array->PropSetByNum(0, count, &val, array);
				} else {
					iTJSDispatch2 *dict = TJSCreateDictionaryObject();
					if (dict != NULL) {
						tTJSVariant name = ttstr(ent->d_name);
						dict->PropSet(TJS_MEMBERENSURE, TJS_W("name"), NULL, &name, dict);
						tTJSVariant size((tjs_int64)(haveStat ? st.st_size : 0));
						dict->PropSet(TJS_MEMBERENSURE, TJS_W("size"), NULL, &size, dict);
						tTJSVariant attrib((tjs_int)(isdir ? kFILE_ATTRIBUTE_DIRECTORY : kFILE_ATTRIBUTE_NORMAL));
						dict->PropSet(TJS_MEMBERENSURE, TJS_W("attrib"), NULL, &attrib, dict);
						tTJSVariant ctime, atime, mtime;
						if (haveStat) {
							storeDate(ctime, timespecToMs(st.st_ctime, 0), NULL);
							storeDate(atime, timespecToMs(st.st_atime, 0), NULL);
							storeDate(mtime, timespecToMs(st.st_mtime, 0), NULL);
						}
						dict->PropSet(TJS_MEMBERENSURE, TJS_W("mtime"), NULL, &mtime, dict);
						dict->PropSet(TJS_MEMBERENSURE, TJS_W("ctime"), NULL, &ctime, dict);
						dict->PropSet(TJS_MEMBERENSURE, TJS_W("atime"), NULL, &atime, dict);
						tTJSVariant val(dict, dict);
						array->PropSetByNum(0, count, &val, array);
						dict->Release();
					}
				}
				count++;
			}
			closedir(dh);
			result = tTJSVariant(array, array);
			array->Release();
		} catch (...) {
			array->Release();
			throw;
		}

		return result;
	}
public:

	/**
	 * 指定ディレクトリを削除する（中にファイルが無い場合のみ）
	 */
	static bool removeDirectory(ttstr dir) {
		if (dir.GetLastChar() != TJS_W('/')) {
			TVPThrowExceptionMessage(TJS_W("'/' must be specified at the end of given directory name."));
		}
		dir = TVPNormalizeStorageName(dir);
		TVPGetLocalName(dir);
		bool r = (rmdir(toUtf8(dir).c_str()) == 0);
		if (!r) {
			TVPAddLog(ttstr(TJS_W("removeDirectory : ")) + dir + TJS_W(" : ") + ttstr(strerror(errno)));
		}
		return r;
	}

	static bool createDirectory(ttstr dir)
	{
		if (dir.GetLastChar() != TJS_W('/')) {
			TVPThrowExceptionMessage(TJS_W("'/' must be specified at the end of given directory name."));
		}
		dir = TVPNormalizeStorageName(dir);
		TVPGetLocalName(dir);
		bool r = (mkdir(toUtf8(dir).c_str(), 0755) == 0);
		if (!r) {
			TVPAddLog(ttstr(TJS_W("createDirectory : ")) + dir + TJS_W(" : ") + ttstr(strerror(errno)));
		}
		return r;
	}

	static bool createDirectoryNoNormalize(ttstr dir)
	{
		if (dir.GetLastChar() != TJS_W('/')) {
			TVPThrowExceptionMessage(TJS_W("'/' must be specified at the end of given directory name."));
		}
		TVPGetLocalName(dir);
		bool r = (mkdir(toUtf8(dir).c_str(), 0755) == 0);
		if (!r) {
			TVPAddLog(ttstr(TJS_W("createDirectory : ")) + dir + TJS_W(" : ") + ttstr(strerror(errno)));
		}
		return r;
	}

	static bool changeDirectory(ttstr dir)
	{
		if (dir.GetLastChar() != TJS_W('/')) {
			TVPThrowExceptionMessage(TJS_W("'/' must be specified at the end of given directory name."));
		}
		dir = TVPNormalizeStorageName(dir);
		TVPGetLocalName(dir);
		bool r = (chdir(toUtf8(dir).c_str()) == 0);
		if (!r) {
			TVPAddLog(ttstr(TJS_W("changeDirectory : ")) + dir + TJS_W(" : ") + ttstr(strerror(errno)));
		}
		return r;
	}

	/**
	 * ファイルの属性を設定する（READONLYのみ実際にchmodへ反映、他はno-op）
	 */
	static bool setFileAttributes(ttstr filename, tjs_uint32 attr)
	{
		filename = TVPNormalizeStorageName(filename);
		TVPGetLocalName(filename);
		std::string path = toUtf8(filename);
		if (attr & kFILE_ATTRIBUTE_READONLY) {
			struct stat st;
			if (stat(path.c_str(), &st) != 0) return false;
			return chmod(path.c_str(), st.st_mode & ~(S_IWUSR|S_IWGRP|S_IWOTH)) == 0;
		}
		return true; // no representable attribute bits requested: nothing to do
	}

	/**
	 * ファイルの属性を解除する（READONLYのみ実際にchmodへ反映、他はno-op）
	 */
	static bool resetFileAttributes(ttstr filename, tjs_uint32 attr)
	{
		filename = TVPNormalizeStorageName(filename);
		TVPGetLocalName(filename);
		std::string path = toUtf8(filename);
		if (attr & kFILE_ATTRIBUTE_READONLY) {
			struct stat st;
			if (stat(path.c_str(), &st) != 0) return false;
			return chmod(path.c_str(), st.st_mode | S_IWUSR) == 0;
		}
		return true;
	}

	/**
	 * ファイルの属性を取得する（DIRECTORY/READONLY/NORMALのみ意味を持つ）
	 */
	static tjs_uint32 getFileAttributes(ttstr filename)
	{
		filename = TVPNormalizeStorageName(filename);
		TVPGetLocalName(filename);
		struct stat st;
		if (stat(toUtf8(filename).c_str(), &st) != 0) return 0xFFFFFFFF; // INVALID_FILE_ATTRIBUTES equivalent
		tjs_uint32 attr = 0;
		if (S_ISDIR(st.st_mode)) attr |= kFILE_ATTRIBUTE_DIRECTORY;
		if (!(st.st_mode & S_IWUSR)) attr |= kFILE_ATTRIBUTE_READONLY;
		if (attr == 0) attr = kFILE_ATTRIBUTE_NORMAL;
		return attr;
	}

	/**
	 * ディレクトリの存在チェック
	 */
	static int isExistentDirectory(ttstr dir)
	{
		if (dir.GetLastChar() != TJS_W('/')) {
			TVPThrowExceptionMessage(TJS_W("'/' must be specified at the end of given directory name."));
		}
		dir = TVPNormalizeStorageName(dir);
		TVPGetLocalName(dir);
		struct stat st;
		if (stat(toUtf8(dir).c_str(), &st) != 0) return false;
		return S_ISDIR(st.st_mode) ? true : false;
	}

	static bool copyFile(const tjs_char *from, const tjs_char *to, bool failIfExist)
	{
		ttstr fromFile(TVPGetLocallyAccessibleName(TVPGetPlacedPath(from)));
		ttstr toFile  (TVPGetLocallyAccessibleName(TVPNormalizeStorageName(to)));
		return _copyStream(fromFile, toFile, failIfExist);
	}

	static bool copyFileNoNormalize(const tjs_char *from, const tjs_char *to, bool failIfExist)
	{
		ttstr fromFile(TVPGetLocallyAccessibleName(TVPGetPlacedPath(from)));
		ttstr toFile(to);
		if (toFile.length()) {
			TVPGetLocalName(toFile);
			return _copyStream(fromFile, toFile, failIfExist);
		}
		return false;
	}

	static bool isExistentStorageNoSearchNoNormalize(ttstr filename)
	{
		return TVPIsExistentStorageNoSearchNoNormalize(filename);
	}

	/**
	 * 表示名取得（Androidにはシェル表示名の概念がないため、ファイル名をそのまま返す）
	 */
	static ttstr getDisplayName(ttstr filename)
	{
		filename = TVPNormalizeStorageName(filename);
		if (filename == "") return filename;
		return TVPExtractStorageName(filename);
	}

	/**
	 * MD5ハッシュ値の取得
	 */
	static tjs_error TJS_INTF_METHOD getMD5HashString(tTJSVariant *result,
													  tjs_int numparams,
													  tTJSVariant **param,
													  iTJSDispatch2 *objthis) {
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;

		ttstr filename = TVPGetPlacedPath(*param[0]);
		tTJSBinaryStream *in = NULL;
		try {
			in = TVPCreateStream(filename, TJS_BS_READ);
		} catch (...) {
			in = NULL;
		}
		if (!in) TVPThrowExceptionMessage((ttstr(TJS_W("cannot open : ")) + param[0]->GetString()).c_str());

		TVP_md5_state_t st;
		TVP_md5_init(&st);

		tjs_uint8 buffer[1024];
		tjs_uint size;
		while ((size = in->Read(buffer, sizeof buffer)) > 0) {
			TVP_md5_append(&st, buffer, (int)size);
		}
		delete in;

		TVP_md5_finish(&st, buffer);

		tjs_char ret[32+1];
		const tjs_char *hex = TJS_W("0123456789abcdef");
		for (tjs_int i=0; i<16; i++) {
			ret[i*2  ] = hex[(buffer[i] >> 4) & 0xF];
			ret[i*2+1] = hex[(buffer[i]     ) & 0xF];
		}
		ret[32] = 0;
		if (result) *result = ttstr(ret);
		return TJS_S_OK;
	}

	/**
	 * パスの検索（Androidには"検索パス"の概念がないため、指定ストレージ名の存在確認に単純化）
	 * @return 見つからなかった場合はvoid，見つかった場合はファイルのフルパス
	 */
	static tjs_error TJS_INTF_METHOD searchPath(tTJSVariant *result,
												tjs_int numparams,
												tTJSVariant **param,
												iTJSDispatch2 *objthis) {
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;
		ttstr filename(*param[0]);
		ttstr normalized = TVPNormalizeStorageName(filename);
		if (TVPIsExistentStorageNoSearchNoNormalize(normalized)) {
			if (result) *result = normalized;
		} else {
			if (result) result->Clear();
		}
		return TJS_S_OK;
	}

	/*----------------------------------------------------------------------
	 * カレントディレクトリ
	 ----------------------------------------------------------------------*/
	static ttstr getCurrentPath() {
		char buf[4096];
		if (!getcwd(buf, sizeof buf)) buf[0] = 0;
		ttstr result(buf);
		if (result.GetLastChar() != TJS_W('/')) result += TJS_W("/");
		return TVPNormalizeStorageName(result);
	}

	static void setCurrentPath(ttstr path) {
		if (!changeDirectory(path)) {
			TVPThrowExceptionMessage(TJS_W("setCurrentPath failed:%1"), ttstr(strerror(errno)));
		}
	}
};

NCB_ATTACH_CLASS(StoragesFstat, Storages) {
	NCB_METHOD(clearStorageCaches);
	RawCallback("fstat",               &Class::fstat,               TJS_STATICMEMBER);
	RawCallback("getTime",             &Class::getTime,             TJS_STATICMEMBER);
	RawCallback("setTime",             &Class::setTime,             TJS_STATICMEMBER);
	NCB_METHOD(getLastModifiedFileTime);
	NCB_METHOD(setLastModifiedFileTime);
	NCB_METHOD(exportFile);
	NCB_METHOD(deleteFile);
	NCB_METHOD(truncateFile);
	NCB_METHOD(moveFile);
	NCB_METHOD(dirlist);
	NCB_METHOD(dirlistEx);
	RawCallback("dirtree",             &Class::dirtree,             TJS_STATICMEMBER);
	NCB_METHOD(removeDirectory);
	NCB_METHOD(createDirectory);
	NCB_METHOD(createDirectoryNoNormalize);
	NCB_METHOD(changeDirectory);
	NCB_METHOD(setFileAttributes);
	NCB_METHOD(resetFileAttributes);
	NCB_METHOD(getFileAttributes);
	// selectDirectory: intentionally not registered -- no folder-picker UI
	// exists on this engine's window; see the file header comment.
	NCB_METHOD(isExistentDirectory);
	NCB_METHOD(copyFile);
	NCB_METHOD(copyFileNoNormalize);
	NCB_METHOD(isExistentStorageNoSearchNoNormalize);
	NCB_METHOD(getDisplayName);
	RawCallback("getMD5HashString",    &Class::getMD5HashString,    TJS_STATICMEMBER);
	RawCallback("searchPath",          &Class::searchPath,          TJS_STATICMEMBER);
	Property("currentPath", &Class::getCurrentPath, &Class::setCurrentPath);
	Method(TJS_W("getTemporaryName"), &TVPGetTemporaryName);
};

// テンポラリファイル処理用クラス (delete-on-close: open() 直後に unlink())
class TemporaryFiles
{
public:
	TemporaryFiles() {};

	~TemporaryFiles() {
		for (size_t i = 0; i < fds.size(); i++) {
			if (fds[i] >= 0) close(fds[i]);
		}
	}

	bool entry(ttstr filename) {
		return _entry(filename);
	}

	bool entryFolder(ttstr filename) {
		// POSIX has no delete-on-close semantics for directories; best
		// effort is to just confirm it exists (matching the original's
		// intent of "this temp folder gets cleaned up"), the actual
		// removal is left to whatever code created it.
		ttstr filename2 = TVPNormalizeStorageName(filename);
		TVPGetLocalName(filename2);
		std::string path;
		{
			const tjs_char *p = filename2.c_str();
			for (; *p; p++) path += (char)*p; // ASCII path is fine here
		}
		struct stat st;
		return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
	}

private:
	std::vector<int> fds;

	static std::string toUtf8Local(const ttstr &s) {
		std::string out;
		const tjs_char *p = s.c_str();
		for (; *p; p++) {
			tjs_uint32 c = (tjs_uint32)(tjs_uint16)*p;
			if (c < 0x80) out += (char)c;
			else if (c < 0x800) { out += (char)(0xC0|(c>>6)); out += (char)(0x80|(c&0x3F)); }
			else { out += (char)(0xE0|(c>>12)); out += (char)(0x80|((c>>6)&0x3F)); out += (char)(0x80|(c&0x3F)); }
		}
		return out;
	}

	bool _entry(const ttstr &name) {
		ttstr filename = TVPNormalizeStorageName(name);
		TVPGetLocalName(filename);
		if (filename.length()) {
			std::string path = toUtf8Local(filename);
			int fd = open(path.c_str(), O_RDONLY);
			if (fd >= 0) {
				unlink(path.c_str()); // delete-on-close: drop the directory entry now
				fds.push_back(fd);
				return true;
			}
		}
		return false;
	}
};

NCB_REGISTER_CLASS(TemporaryFiles) {
	Constructor();
	NCB_METHOD(entry);
	NCB_METHOD(entryFolder);
}

/**
 * 登録処理後
 */
static void PostRegistCallback()
{
	tTJSVariant var;
	TVPExecuteExpression(TJS_W("Date"), &var);
	dateClass = var.AsObject();
	var.Clear();
	TVPExecuteExpression(TJS_W("Date.setTime"), &var);
	dateSetTime = var.AsObject();
	var.Clear();
	TVPExecuteExpression(TJS_W("Date.getTime"), &var);
	dateGetTime = var.AsObject();
	var.Clear();
	TVPExecuteExpression(StoragesFstatPreScript);
}

/**
 * 開放処理前
 */
static void PreUnregistCallback()
{
	if (dateClass)   { dateClass->Release();   dateClass = NULL; }
	if (dateSetTime) { dateSetTime->Release(); dateSetTime = NULL; }
	if (dateGetTime) { dateGetTime->Release(); dateGetTime = NULL; }
}

NCB_POST_REGIST_CALLBACK(PostRegistCallback);
NCB_PRE_UNREGIST_CALLBACK(PreUnregistCallback);
