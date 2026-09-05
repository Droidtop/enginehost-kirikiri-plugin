/*
 * sqlite3.dll: the Sqlite and SqliteStatement classes that KAG's scene
 * database is built on.
 *
 * Noble Works stops without this. KAGEnvSceneDB.tjs opens the scene database
 * on the first scene transition (`db = new Sqlite(storage, readonly)`), and
 * every line of narration, every choice and every evaluation expression is a
 * row in it.
 *
 * There is no wamsoft source for this plugin, so the surface below is read off
 * the game's own scripts (data/system/KAGEnvSceneDB.tjs and its patch2
 * revision), which use all of it:
 *
 *   Sqlite(storage, readonly = false)
 *     exec(sql, params = [], callback = void) -> bool   (false on any error)
 *     begin() / commit() / rollback()
 *     errorCode, errorMessage, lastInsertRowId
 *     Sqlite.SQLITE_OK / SQLITE_ROW / SQLITE_DONE / SQLITE_ERROR / SQLITE_BUSY
 *   SqliteStatement(db, sql)
 *     reset(), bind(array), step() -> bool, get(col = void), exec() -> int
 *
 * exec()'s callback is invoked once per row with the columns as positional
 * arguments (MasterData.resetFunc takes (id, name)), and it is passed as a TJS
 * closure -- checkDepend() hands in an anonymous function `incontextof` a
 * dictionary -- so it is called through tTJSVariantClosure and keeps its own
 * context. get() with no argument returns the whole row as an Array; get(n)
 * returns one column.
 *
 * Two things beyond plain bindings are needed, and they are why this file is
 * longer than the class list suggests:
 *
 * 1. The database lives inside an XP3 archive. KAGEnvPlayer decides read-only
 *    by whether the placed path contains ">", which is exactly the case where
 *    the file is archived; scene.sdb, scenedata.sdb and scenepatch.sdb are all
 *    inside patch3.xp3 for Noble Works. ATTACH resolves its argument inside
 *    SQLite, not in TJS, so it is not enough to hand SQLite a pre-opened
 *    stream: it needs a VFS that can open a KiriKiri storage name. That is
 *    KrkrVfs below -- read-only, over TVPCreateStream. The writable case (the
 *    scenario setup tool, which we do not run) keeps the default unix VFS on a
 *    locally accessible name.
 *
 * 2. SceneTextData prepares a statement containing `ncnt(text, ?)` at
 *    construction time, on the ordinary play path, so the connection must
 *    carry that function or every scene load fails at prepare. ncnt is
 *    wamsoft's "normalized containment" search predicate; see NormalizeForCnt
 *    for what this implementation folds and what it does not.
 */
#include "ncbind/ncbind.hpp"
#include "MsgIntf.h"
#include "StorageIntf.h"
#include "CharacterSet.h"

#include "sqlite3.h"

#include <string>
#include <vector>
#include <set>

#define NCB_MODULE_NAME TJS_W("sqlite3.dll")

//---------------------------------------------------------------------------
// UTF-8 <-> ttstr
//
// SQLite speaks UTF-8; tjs_char is char16_t here. The engine's own converters
// are used rather than the locale-dependent narrow tTJSVariant constructor.
//---------------------------------------------------------------------------

static std::string ToUtf8(const ttstr &s)
{
	if (s.IsEmpty()) return std::string();
	tjs_int len = TVPWideCharToUtf8String(s.c_str(), NULL);
	if (len <= 0) return std::string();
	std::string out;
	out.resize((size_t)len);
	TVPWideCharToUtf8String(s.c_str(), &out[0]);
	return out;
}

static ttstr FromUtf8(const char *u)
{
	if (!u || !*u) return ttstr();
	tjs_int len = TVPUtf8ToWideCharString(u, NULL);
	if (len <= 0) return ttstr();
	ttstr r;
	tjs_char *p = r.AllocBuffer((tjs_uint)len);
	TVPUtf8ToWideCharString(u, p);
	p[len] = 0;
	r.FixLen();
	return r;
}

//---------------------------------------------------------------------------
// The read-only KiriKiri VFS
//
// Only what a read-only connection actually uses is implemented. The file is
// reported as SQLITE_IOCAP_IMMUTABLE, which tells SQLite the database cannot
// change underneath it: it then skips locking and the hot-journal check
// entirely, so no journal file is ever asked for. Temporary b-trees stay in
// memory (SQLITE_TEMP_STORE=3, set in thirdparty/sqlite3/CMakeLists.txt), so
// no scratch file is asked for either. That leaves xOpen for the main database
// and nothing else.
//---------------------------------------------------------------------------

namespace {

struct KrkrFile {
	sqlite3_file base;
	tTJSBinaryStream *stream;
	sqlite3_int64 size;
};

int KrkrClose(sqlite3_file *file)
{
	KrkrFile *f = (KrkrFile *)file;
	if (f->stream) {
		delete f->stream;
		f->stream = NULL;
	}
	return SQLITE_OK;
}

int KrkrRead(sqlite3_file *file, void *buf, int amt, sqlite3_int64 ofst)
{
	KrkrFile *f = (KrkrFile *)file;
	if (!f->stream) return SQLITE_IOERR_READ;
	try {
		f->stream->SetPosition((tjs_uint64)ofst);
		tjs_uint got = f->stream->Read(buf, (tjs_uint)amt);
		if ((int)got < amt) {
			// SQLite requires the tail to be zeroed when it reads past the end.
			memset((char *)buf + got, 0, (size_t)(amt - (int)got));
			return SQLITE_IOERR_SHORT_READ;
		}
	} catch (...) {
		return SQLITE_IOERR_READ;
	}
	return SQLITE_OK;
}

int KrkrWrite(sqlite3_file *, const void *, int, sqlite3_int64) { return SQLITE_READONLY; }
int KrkrTruncate(sqlite3_file *, sqlite3_int64)                 { return SQLITE_READONLY; }
int KrkrSync(sqlite3_file *, int)                               { return SQLITE_OK; }

int KrkrFileSize(sqlite3_file *file, sqlite3_int64 *pSize)
{
	*pSize = ((KrkrFile *)file)->size;
	return SQLITE_OK;
}

int KrkrLock(sqlite3_file *, int)   { return SQLITE_OK; }
int KrkrUnlock(sqlite3_file *, int) { return SQLITE_OK; }

int KrkrCheckReservedLock(sqlite3_file *, int *pResOut)
{
	*pResOut = 0;
	return SQLITE_OK;
}

int KrkrFileControl(sqlite3_file *, int, void *) { return SQLITE_NOTFOUND; }
int KrkrSectorSize(sqlite3_file *)               { return 4096; }

int KrkrDeviceCharacteristics(sqlite3_file *)
{
	// The whole point: an immutable file needs no locking and can carry no
	// hot journal, so SQLite never looks for one beside it inside the archive.
	return SQLITE_IOCAP_IMMUTABLE;
}

const sqlite3_io_methods KrkrIoMethods = {
	1,                          /* iVersion */
	KrkrClose,
	KrkrRead,
	KrkrWrite,
	KrkrTruncate,
	KrkrSync,
	KrkrFileSize,
	KrkrLock,
	KrkrUnlock,
	KrkrCheckReservedLock,
	KrkrFileControl,
	KrkrSectorSize,
	KrkrDeviceCharacteristics
	/* the v2/v3 shared-memory and mmap methods stay null at iVersion 1 */
};

int KrkrOpen(sqlite3_vfs *, const char *zName, sqlite3_file *file, int flags, int *pOutFlags)
{
	KrkrFile *f = (KrkrFile *)file;
	memset(f, 0, sizeof(*f));

	// Only real databases -- the main one and anything ATTACHed -- come from
	// the archive. Anything else would be a file SQLite wants to write.
	if (!zName || !(flags & SQLITE_OPEN_MAIN_DB)) return SQLITE_CANTOPEN;

	ttstr name = FromUtf8(zName);
	tTJSBinaryStream *stream = NULL;
	try {
		stream = TVPCreateStream(name, TJS_BS_READ);
	} catch (...) {
		stream = NULL;
	}
	if (!stream) return SQLITE_CANTOPEN;

	f->base.pMethods = &KrkrIoMethods;
	f->stream = stream;
	try {
		f->size = (sqlite3_int64)stream->GetSize();
	} catch (...) {
		f->size = 0;
	}
	if (pOutFlags) *pOutFlags = SQLITE_OPEN_READONLY;
	return SQLITE_OK;
}

int KrkrDelete(sqlite3_vfs *, const char *, int) { return SQLITE_OK; }

int KrkrAccess(sqlite3_vfs *, const char *zName, int flags, int *pResOut)
{
	if (flags == SQLITE_ACCESS_READWRITE) {
		*pResOut = 0;
		return SQLITE_OK;
	}
	bool found = false;
	try {
		found = TVPIsExistentStorage(FromUtf8(zName));
	} catch (...) {
		found = false;
	}
	*pResOut = found ? 1 : 0;
	return SQLITE_OK;
}

int KrkrFullPathname(sqlite3_vfs *vfs, const char *zName, int nOut, char *zOut)
{
	// KiriKiri storage names are not filesystem paths; normalizing one would
	// break the "archive.xp3>member" form that ATTACH is given. Pass it
	// through unchanged.
	size_t len = strlen(zName);
	if (len >= (size_t)nOut) len = (size_t)nOut - 1;
	memcpy(zOut, zName, len);
	zOut[len] = 0;
	(void)vfs;
	return SQLITE_OK;
}

int KrkrRandomness(sqlite3_vfs *vfs, int nByte, char *zOut)
{
	sqlite3_vfs *base = (sqlite3_vfs *)vfs->pAppData;
	return base->xRandomness(base, nByte, zOut);
}

int KrkrSleep(sqlite3_vfs *vfs, int micro)
{
	sqlite3_vfs *base = (sqlite3_vfs *)vfs->pAppData;
	return base->xSleep(base, micro);
}

int KrkrCurrentTime(sqlite3_vfs *vfs, double *pTime)
{
	sqlite3_vfs *base = (sqlite3_vfs *)vfs->pAppData;
	return base->xCurrentTime(base, pTime);
}

int KrkrGetLastError(sqlite3_vfs *vfs, int n, char *z)
{
	sqlite3_vfs *base = (sqlite3_vfs *)vfs->pAppData;
	return base->xGetLastError ? base->xGetLastError(base, n, z) : 0;
}

const char *const KrkrVfsName = "krkr";

/** Registers the KiriKiri VFS once, and reports whether it is usable. */
bool EnsureKrkrVfs()
{
	static int state = 0; // 0 = untried, 1 = registered, -1 = unavailable
	if (state) return state > 0;

	sqlite3_vfs *base = sqlite3_vfs_find(NULL);
	if (!base) {
		state = -1;
		return false;
	}

	static sqlite3_vfs vfs;
	memset(&vfs, 0, sizeof(vfs));
	vfs.iVersion = 1;
	vfs.szOsFile = sizeof(KrkrFile);
	vfs.mxPathname = 2048;
	vfs.zName = KrkrVfsName;
	vfs.pAppData = base;
	vfs.xOpen = KrkrOpen;
	vfs.xDelete = KrkrDelete;
	vfs.xAccess = KrkrAccess;
	vfs.xFullPathname = KrkrFullPathname;
	vfs.xRandomness = KrkrRandomness;
	vfs.xSleep = KrkrSleep;
	vfs.xCurrentTime = KrkrCurrentTime;
	vfs.xGetLastError = KrkrGetLastError;

	state = (sqlite3_vfs_register(&vfs, 0) == SQLITE_OK) ? 1 : -1;
	return state > 0;
}

//---------------------------------------------------------------------------
// ncnt(text, keyword): wamsoft's normalized containment predicate
//
// Used by SceneTextData's keyword search, and -- because the statement is
// prepared in the constructor -- required for the scene database to open at
// all. No source for the original exists, so what it folds is inferred from
// what a Japanese scenario search has to cope with: a keyword typed with a
// different width, kana or letter case than the line it should match.
//
// Folded here: whitespace (ASCII and U+3000) is dropped, fullwidth ASCII
// (U+FF01..U+FF5E) becomes ASCII, ASCII letters become lowercase, halfwidth
// katakana becomes fullwidth (recomposing a following dakuten or handakuten),
// and katakana becomes hiragana. Not folded: anything needing real Unicode
// normalization tables (compatibility ideographs, circled forms), and
// characters outside the BMP.
//---------------------------------------------------------------------------

/** U+FF61..U+FF9D -> the fullwidth katakana (or punctuation) it stands for. */
static const tjs_char HalfwidthKatakana[] = {
	0x3002, 0x300C, 0x300D, 0x3001, 0x30FB, 0x30F2, 0x30A1, 0x30A3, /* FF61-FF68 */
	0x30A5, 0x30A7, 0x30A9, 0x30E3, 0x30E5, 0x30E7, 0x30C3, 0x30FC, /* FF69-FF70 */
	0x30A2, 0x30A4, 0x30A6, 0x30A8, 0x30AA, 0x30AB, 0x30AD, 0x30AF, /* FF71-FF78 */
	0x30B1, 0x30B3, 0x30B5, 0x30B7, 0x30B9, 0x30BB, 0x30BD, 0x30BF, /* FF79-FF80 */
	0x30C1, 0x30C4, 0x30C6, 0x30C8, 0x30CA, 0x30CB, 0x30CC, 0x30CD, /* FF81-FF88 */
	0x30CE, 0x30CF, 0x30D2, 0x30D5, 0x30D8, 0x30DB, 0x30DE, 0x30DF, /* FF89-FF90 */
	0x30E0, 0x30E1, 0x30E2, 0x30E4, 0x30E6, 0x30E8, 0x30E9, 0x30EA, /* FF91-FF98 */
	0x30EB, 0x30EC, 0x30ED, 0x30EF, 0x30F3                          /* FF99-FF9D */
};

/** Fullwidth katakana that a dakuten (voiced mark) can be composed onto. */
static bool ComposeDakuten(tjs_char base, tjs_char *out)
{
	if ((base >= 0x30AB && base <= 0x30C1) ||  /* ka..chi  */
	    (base >= 0x30C4 && base <= 0x30C8) ||  /* tsu..to  */
	    (base >= 0x30CF && base <= 0x30DB)) {  /* ha..ho   */
		// In these ranges the voiced form is always the next code point, and
		// the ha-row steps in threes (ha, ba, pa).
		if (base >= 0x30CF && ((base - 0x30CF) % 3) != 0) return false;
		if (base < 0x30CF && ((base - (base <= 0x30C1 ? 0x30AB : 0x30C4)) % 2) != 0) return false;
		*out = base + 1;
		return true;
	}
	if (base == 0x30A6) { *out = 0x30F4; return true; } /* u -> vu */
	return false;
}

/** Fullwidth katakana that a handakuten (semi-voiced mark) can compose onto. */
static bool ComposeHandakuten(tjs_char base, tjs_char *out)
{
	if (base >= 0x30CF && base <= 0x30DB && ((base - 0x30CF) % 3) == 0) {
		*out = base + 2;
		return true;
	}
	return false;
}

static void NormalizeForCnt(const ttstr &in, std::basic_string<tjs_char> &out)
{
	out.clear();
	const tjs_char *p = in.c_str();
	if (!p) return;
	for (; *p; ++p) {
		tjs_char c = *p;

		if (c >= 0xFF61 && c <= 0xFF9D) {
			c = HalfwidthKatakana[c - 0xFF61];
			// A halfwidth dakuten/handakuten follows the letter it marks.
			if (p[1] == 0xFF9E) {
				tjs_char v;
				if (ComposeDakuten(c, &v)) { c = v; ++p; }
			} else if (p[1] == 0xFF9F) {
				tjs_char v;
				if (ComposeHandakuten(c, &v)) { c = v; ++p; }
			}
		} else if (c == 0xFF9E || c == 0xFF9F) {
			// A mark with nothing to attach to.
			c = (c == 0xFF9E) ? 0x309B : 0x309C;
		} else if (c >= 0xFF01 && c <= 0xFF5E) {
			c -= 0xFEE0;
		}

		if (c == 0x3000 || c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
		if (c >= 'A' && c <= 'Z') c = (tjs_char)(c - 'A' + 'a');
		// Katakana to hiragana. U+30F7..U+30FA (va/vi/ve/vo) and U+30FC (the
		// long vowel mark) have no hiragana form and are left alone.
		if (c >= 0x30A1 && c <= 0x30F6) c -= 0x60;

		out.push_back(c);
	}
}

void NcntFunc(sqlite3_context *ctx, int argc, sqlite3_value **argv)
{
	if (argc < 2) {
		sqlite3_result_int(ctx, 0);
		return;
	}
	if (sqlite3_value_type(argv[0]) == SQLITE_NULL ||
	    sqlite3_value_type(argv[1]) == SQLITE_NULL) {
		sqlite3_result_int(ctx, 0);
		return;
	}
	// This runs inside sqlite3_step, i.e. between C frames that were not
	// compiled with exceptions, so nothing may escape.
	try {
		std::basic_string<tjs_char> hay, needle;
		NormalizeForCnt(FromUtf8((const char *)sqlite3_value_text(argv[0])), hay);
		NormalizeForCnt(FromUtf8((const char *)sqlite3_value_text(argv[1])), needle);
		sqlite3_result_int(ctx, hay.find(needle) != std::basic_string<tjs_char>::npos ? 1 : 0);
	} catch (...) {
		sqlite3_result_error(ctx, "ncnt failed", -1);
	}
}

} // anonymous namespace

//---------------------------------------------------------------------------
// The TJS classes
//---------------------------------------------------------------------------

class SqliteStatement;

class Sqlite {
public:
	Sqlite(const ttstr &storage, bool readonly);
	~Sqlite();

	sqlite3 *Handle() const { return db; }

	/** Records the outcome of the last operation, for errorCode/errorMessage. */
	void SetError(int code);
	void SetError(int code, const ttstr &message) { errorCode = code; errorMessage = message; }

	bool Exec(const ttstr &sql, const tTJSVariant *params, const tTJSVariant *callback);
	void Simple(const tjs_char *sql);

	tjs_int getErrorCode() const { return errorCode; }
	ttstr   getErrorMessage() const { return errorMessage; }
	tjs_int64 getLastInsertRowId() const {
		return db ? (tjs_int64)sqlite3_last_insert_rowid(db) : 0;
	}

	void Attach(SqliteStatement *s)   { statements.insert(s); }
	void Detach(SqliteStatement *s)   { statements.erase(s); }

private:
	sqlite3 *db;
	tjs_int errorCode;
	ttstr errorMessage;
	std::set<SqliteStatement *> statements;
};

class SqliteStatement {
public:
	SqliteStatement(Sqlite *owner, const ttstr &sql);
	~SqliteStatement();

	/** Called by Sqlite's destructor: the statement outlives its database
	 *  only if a script keeps it, and then it must not touch a closed handle. */
	void Orphan() { owner = NULL; stmt = NULL; }

	void Reset();
	void Bind(const tTJSVariant *params);
	bool Step();
	tjs_int Exec();
	void Get(tTJSVariant *result, const tTJSVariant *column);

private:
	Sqlite *owner;
	sqlite3_stmt *stmt;
	int lastStep;
};

//---------------------------------------------------------------------------
// value conversion
//---------------------------------------------------------------------------

static void ColumnToVariant(sqlite3_stmt *stmt, int col, tTJSVariant *out)
{
	switch (sqlite3_column_type(stmt, col)) {
	case SQLITE_INTEGER:
		*out = (tjs_int64)sqlite3_column_int64(stmt, col);
		break;
	case SQLITE_FLOAT:
		*out = (tjs_real)sqlite3_column_double(stmt, col);
		break;
	case SQLITE_TEXT:
		*out = FromUtf8((const char *)sqlite3_column_text(stmt, col));
		break;
	case SQLITE_BLOB: {
		const void *data = sqlite3_column_blob(stmt, col);
		int len = sqlite3_column_bytes(stmt, col);
		*out = tTJSVariant((const tjs_uint8 *)data, (tjs_uint)(len < 0 ? 0 : len));
		break;
	}
	default:
		out->Clear();
		break;
	}
}

/** Binds one TJS value to parameter `index` (1-based). */
static int BindVariant(sqlite3_stmt *stmt, int index, const tTJSVariant &v)
{
	switch (v.Type()) {
	case tvtVoid:
		return sqlite3_bind_null(stmt, index);
	case tvtInteger:
		return sqlite3_bind_int64(stmt, index, (sqlite3_int64)(tjs_int64)v);
	case tvtReal:
		return sqlite3_bind_double(stmt, index, (double)(tjs_real)v);
	case tvtString: {
		std::string u = ToUtf8(ttstr(v));
		return sqlite3_bind_text(stmt, index, u.c_str(), (int)u.size(), SQLITE_TRANSIENT);
	}
	case tvtOctet: {
		tTJSVariantOctet *o = v.AsOctetNoAddRef();
		if (!o || !o->GetLength()) return sqlite3_bind_zeroblob(stmt, index, 0);
		return sqlite3_bind_blob(stmt, index, o->GetData(), (int)o->GetLength(), SQLITE_TRANSIENT);
	}
	default:
		// An Array or Dictionary has no SQL representation. The scenario setup
		// tool serializes those to an octet before it gets here; say so rather
		// than storing something the reader cannot parse.
		TVPThrowExceptionMessage(TJS_W("sqlite3: cannot bind an object; pass an octet or a primitive"));
		return SQLITE_MISUSE;
	}
}

/** Binds every element of a TJS Array, in order. Missing/void means no binds. */
static void BindArray(sqlite3_stmt *stmt, const tTJSVariant *params)
{
	if (!params || params->Type() != tvtObject) return;
	iTJSDispatch2 *arr = params->AsObjectNoAddRef();
	if (!arr) return;

	tTJSVariant countVar;
	if (TJS_FAILED(arr->PropGet(0, TJS_W("count"), NULL, &countVar, arr))) return;
	tjs_int count = (tjs_int)countVar;

	for (tjs_int i = 0; i < count; i++) {
		tTJSVariant v;
		if (TJS_FAILED(arr->PropGetByNum(0, i, &v, arr))) continue;
		BindVariant(stmt, (int)(i + 1), v);
	}
}

//---------------------------------------------------------------------------
// Sqlite
//---------------------------------------------------------------------------

Sqlite::Sqlite(const ttstr &storage, bool readonly)
	: db(NULL), errorCode(SQLITE_OK)
{
	int r;
	if (readonly) {
		// Read-only means the path came out of an archive, so it has to go
		// through the KiriKiri VFS -- and so does anything the scripts ATTACH,
		// which SQLite resolves through the same VFS on its own.
		if (!EnsureKrkrVfs()) {
			SetError(SQLITE_CANTOPEN,
			         ttstr(TJS_W("sqlite3: the KiriKiri VFS could not be registered")));
			return;
		}
		std::string path = ToUtf8(storage);
		r = sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, KrkrVfsName);
	} else {
		// A writable database has to be a real file. TVPGetLocallyAccessibleName
		// turns a storage name into one, extracting it if it is archived.
		ttstr local;
		try {
			local = TVPGetLocallyAccessibleName(storage);
		} catch (...) {
			local.Clear();
		}
		if (local.IsEmpty()) local = storage;
		std::string path = ToUtf8(local);
		r = sqlite3_open_v2(path.c_str(), &db,
		                    SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
	}

	if (r != SQLITE_OK) {
		SetError(r);
		return;
	}

	// SceneTextData prepares a statement using ncnt() in its constructor, so
	// this has to exist before any script SQL is compiled.
	sqlite3_create_function(db, "ncnt", 2, SQLITE_UTF8 | SQLITE_DETERMINISTIC,
	                        NULL, NcntFunc, NULL, NULL);
	SetError(SQLITE_OK);
}

Sqlite::~Sqlite()
{
	for (std::set<SqliteStatement *>::iterator i = statements.begin();
	     i != statements.end(); ++i) {
		(*i)->Orphan();
	}
	statements.clear();
	if (db) {
		// Anything a script forgot to finalize would keep the handle open.
		sqlite3_stmt *s;
		while ((s = sqlite3_next_stmt(db, NULL)) != NULL) sqlite3_finalize(s);
		sqlite3_close(db);
		db = NULL;
	}
}

void Sqlite::SetError(int code)
{
	errorCode = code;
	if (code == SQLITE_OK) {
		errorMessage.Clear();
	} else {
		errorMessage = db ? FromUtf8(sqlite3_errmsg(db)) : FromUtf8(sqlite3_errstr(code));
	}
}

bool Sqlite::Exec(const ttstr &sql, const tTJSVariant *params, const tTJSVariant *callback)
{
	if (!db) {
		SetError(SQLITE_MISUSE, ttstr(TJS_W("sqlite3: the database is not open")));
		return false;
	}

	std::string text = ToUtf8(sql);
	const char *at = text.c_str();
	const char *end = at + text.size();
	bool bound = false;

	while (at < end) {
		sqlite3_stmt *stmt = NULL;
		const char *tail = NULL;
		int r = sqlite3_prepare_v2(db, at, (int)(end - at), &stmt, &tail);
		if (r != SQLITE_OK) {
			SetError(r);
			return false;
		}
		if (!stmt) {           // trailing whitespace or a comment
			at = tail ? tail : end;
			continue;
		}
		// The scripts only ever parameterize a single-statement exec (`attach ?
		// as datadb`), so the values go to the first statement that takes any.
		if (!bound && sqlite3_bind_parameter_count(stmt) > 0) {
			BindArray(stmt, params);
			bound = true;
		}

		const bool wantRows = callback && callback->Type() == tvtObject &&
		                      callback->AsObjectNoAddRef() != NULL;
		int columns = sqlite3_column_count(stmt);

		while ((r = sqlite3_step(stmt)) == SQLITE_ROW) {
			if (!wantRows) continue;
			// One argument per column, in order: MasterData.resetFunc is
			// declared as resetFunc(id, name) against "select id,name from ...".
			std::vector<tTJSVariant> values((size_t)columns);
			std::vector<tTJSVariant *> args((size_t)columns);
			for (int c = 0; c < columns; c++) {
				ColumnToVariant(stmt, c, &values[(size_t)c]);
				args[(size_t)c] = &values[(size_t)c];
			}
			tTJSVariantClosure closure = callback->AsObjectClosureNoAddRef();
			tjs_error e;
			try {
				e = closure.FuncCall(0, NULL, NULL, NULL, columns,
				                     columns ? &args[0] : NULL, NULL);
			} catch (...) {
				// The statement is ours to clean up before the script's
				// exception carries on out of exec().
				sqlite3_finalize(stmt);
				SetError(SQLITE_ABORT, ttstr(TJS_W("sqlite3: the exec callback threw")));
				throw;
			}
			if (TJS_FAILED(e)) {
				sqlite3_finalize(stmt);
				SetError(SQLITE_ABORT, ttstr(TJS_W("sqlite3: the exec callback failed")));
				return false;
			}
		}

		sqlite3_finalize(stmt);
		if (r != SQLITE_DONE) {
			SetError(r);
			return false;
		}
		at = tail ? tail : end;
	}

	SetError(SQLITE_OK);
	return true;
}

void Sqlite::Simple(const tjs_char *sql)
{
	Exec(ttstr(sql), NULL, NULL);
}

//---------------------------------------------------------------------------
// SqliteStatement
//---------------------------------------------------------------------------

SqliteStatement::SqliteStatement(Sqlite *database, const ttstr &sql)
	: owner(database), stmt(NULL), lastStep(SQLITE_OK)
{
	if (!database || !database->Handle()) {
		TVPThrowExceptionMessage(TJS_W("sqlite3: SqliteStatement needs an open Sqlite"));
	}
	std::string text = ToUtf8(sql);
	int r = sqlite3_prepare_v2(database->Handle(), text.c_str(), (int)text.size(), &stmt, NULL);
	if (r != SQLITE_OK || !stmt) {
		database->SetError(r);
		// The scripts never test a statement for success, so a silent failure
		// here would surface much later as a null-access; name the SQL instead.
		ttstr message = ttstr(TJS_W("sqlite3: cannot prepare \"")) + sql +
		                TJS_W("\": ") + FromUtf8(sqlite3_errmsg(database->Handle()));
		owner = NULL;
		TVPThrowExceptionMessage(message.c_str());
	}
	database->Attach(this);
}

SqliteStatement::~SqliteStatement()
{
	if (owner) owner->Detach(this);
	if (stmt) {
		sqlite3_finalize(stmt);
		stmt = NULL;
	}
}

void SqliteStatement::Reset()
{
	if (!stmt) return;
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);
	lastStep = SQLITE_OK;
}

void SqliteStatement::Bind(const tTJSVariant *params)
{
	if (!stmt) return;
	BindArray(stmt, params);
}

bool SqliteStatement::Step()
{
	if (!stmt) return false;
	lastStep = sqlite3_step(stmt);
	if (owner) owner->SetError(lastStep == SQLITE_ROW || lastStep == SQLITE_DONE
	                           ? SQLITE_OK : lastStep);
	return lastStep == SQLITE_ROW;
}

tjs_int SqliteStatement::Exec()
{
	if (!stmt) return SQLITE_MISUSE;
	lastStep = sqlite3_step(stmt);
	if (owner) owner->SetError(lastStep == SQLITE_ROW || lastStep == SQLITE_DONE
	                           ? SQLITE_OK : lastStep);
	return (tjs_int)lastStep;
}

void SqliteStatement::Get(tTJSVariant *result, const tTJSVariant *column)
{
	if (!result) return;
	result->Clear();
	if (!stmt || lastStep != SQLITE_ROW) return;

	int columns = sqlite3_column_count(stmt);
	if (column && column->Type() != tvtVoid) {
		int c = (int)(tjs_int)*column;
		if (c >= 0 && c < columns) ColumnToVariant(stmt, c, result);
		return;
	}

	// No column asked for: the whole row, as an Array. doSelect() relies on
	// this -- it reads .count and unwraps a single-column row itself.
	iTJSDispatch2 *arr = TJSCreateArrayObject();
	for (int c = 0; c < columns; c++) {
		tTJSVariant v;
		ColumnToVariant(stmt, c, &v);
		arr->PropSetByNum(TJS_MEMBERENSURE, c, &v, arr);
	}
	*result = tTJSVariant(arr, arr);
	arr->Release();
}

//---------------------------------------------------------------------------
// ncbind glue
//
// Both constructors and two of the methods take optional arguments, and
// ncbind's ordinary binding rejects a call with fewer arguments than the C++
// signature declares (doInvoke returns TJS_E_BADPARAMCOUNT). The scripts do
// call `new Sqlite(datastorage)` and `state.get()`, so these go through raw
// callbacks, which see the real argument count.
//---------------------------------------------------------------------------

static tjs_error TJS_INTF_METHOD CreateSqlite(Sqlite **result, tjs_int numparams,
                                              tTJSVariant **param, iTJSDispatch2 *)
{
	if (numparams < 1) return TJS_E_BADPARAMCOUNT;
	ttstr storage = ttstr(*param[0]);
	bool readonly = numparams > 1 && param[1]->Type() != tvtVoid && (tjs_int)*param[1] != 0;
	*result = new Sqlite(storage, readonly);
	return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD SqliteExec(tTJSVariant *result, tjs_int numparams,
                                            tTJSVariant **param, Sqlite *self)
{
	if (numparams < 1) return TJS_E_BADPARAMCOUNT;
	bool ok = self->Exec(ttstr(*param[0]),
	                     numparams > 1 ? param[1] : NULL,
	                     numparams > 2 ? param[2] : NULL);
	if (result) *result = ok;
	return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD SqliteBegin(tTJSVariant *, tjs_int, tTJSVariant **, Sqlite *self)
{
	self->Simple(TJS_W("BEGIN"));
	return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD SqliteCommit(tTJSVariant *, tjs_int, tTJSVariant **, Sqlite *self)
{
	self->Simple(TJS_W("COMMIT"));
	return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD SqliteRollback(tTJSVariant *, tjs_int, tTJSVariant **, Sqlite *self)
{
	self->Simple(TJS_W("ROLLBACK"));
	return TJS_S_OK;
}

NCB_REGISTER_CLASS(Sqlite) {
	RawCallback(&CreateSqlite);
	NCB_METHOD_RAW_CALLBACK(exec,     SqliteExec,     0);
	NCB_METHOD_RAW_CALLBACK(begin,    SqliteBegin,    0);
	NCB_METHOD_RAW_CALLBACK(commit,   SqliteCommit,   0);
	NCB_METHOD_RAW_CALLBACK(rollback, SqliteRollback, 0);
	NCB_PROPERTY_RO(errorCode,       getErrorCode);
	NCB_PROPERTY_RO(errorMessage,    getErrorMessage);
	NCB_PROPERTY_RO(lastInsertRowId, getLastInsertRowId);
}

static tjs_error TJS_INTF_METHOD CreateSqliteStatement(SqliteStatement **result, tjs_int numparams,
                                                       tTJSVariant **param, iTJSDispatch2 *)
{
	if (numparams < 2) return TJS_E_BADPARAMCOUNT;
	if (param[0]->Type() != tvtObject) return TJS_E_INVALIDPARAM;
	Sqlite *db = ncbInstanceAdaptor<Sqlite>::GetNativeInstance(param[0]->AsObjectNoAddRef(), true);
	if (!db) return TJS_E_INVALIDPARAM;
	*result = new SqliteStatement(db, ttstr(*param[1]));
	return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD StatementGet(tTJSVariant *result, tjs_int numparams,
                                              tTJSVariant **param, SqliteStatement *self)
{
	self->Get(result, numparams > 0 ? param[0] : NULL);
	return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD StatementBind(tTJSVariant *, tjs_int numparams,
                                               tTJSVariant **param, SqliteStatement *self)
{
	if (numparams < 1) return TJS_E_BADPARAMCOUNT;
	self->Bind(param[0]);
	return TJS_S_OK;
}

NCB_REGISTER_CLASS(SqliteStatement) {
	RawCallback(&CreateSqliteStatement);
	NCB_METHOD_DIFFER(reset, Reset);
	NCB_METHOD_DIFFER(step,  Step);
	NCB_METHOD_DIFFER(exec,  Exec);
	NCB_METHOD_RAW_CALLBACK(bind, StatementBind, 0);
	NCB_METHOD_RAW_CALLBACK(get,  StatementGet,  0);
}

/*
 * The result codes the scripts compare against. `insertState.exec() ==
 * Sqlite.SQLITE_DONE` is the common one; the rest are here because a script
 * that logs an error naturally reaches for them. ncbind has no macro for a
 * class constant, so they are assigned once the class object exists.
 */
static void RegisterResultCodes()
{
	static const tjs_char *assignments[] = {
		TJS_W("Sqlite.SQLITE_OK = 0"),
		TJS_W("Sqlite.SQLITE_ERROR = 1"),
		TJS_W("Sqlite.SQLITE_BUSY = 5"),
		TJS_W("Sqlite.SQLITE_LOCKED = 6"),
		TJS_W("Sqlite.SQLITE_READONLY = 8"),
		TJS_W("Sqlite.SQLITE_CONSTRAINT = 19"),
		TJS_W("Sqlite.SQLITE_MISUSE = 21"),
		TJS_W("Sqlite.SQLITE_ROW = 100"),
		TJS_W("Sqlite.SQLITE_DONE = 101"),
	};
	for (size_t i = 0; i < sizeof(assignments) / sizeof(assignments[0]); i++) {
		try {
			TVPExecuteExpression(ttstr(assignments[i]));
		} catch (...) {
			TVPAddLog(ttstr(TJS_W("sqlite3: could not set ")) + ttstr(assignments[i]));
		}
	}
}
NCB_POST_REGIST_CALLBACK(RegisterResultCodes);
