/*
 * psbfile.dll: the PSBFile class, which turns a PSB octet back into a TJS
 * value tree.
 *
 * This sits immediately behind the scene database. KAGEnvPlayer.loadScene()
 * reads a scene row and then does `scenario = new PSBFile(data[2])`
 * unconditionally, and every line of the scene is read out of `scenario.root`;
 * restore() does the same for a saved screen state, and KAGEnvSceneDB's
 * convertVoice() does it for the voice column. So the scene database opening
 * is only half of getting past the title screen -- without this the very next
 * call fails.
 *
 * PSB ("Packaged Struct Binary") is M2's container. Its use here is as a plain
 * serialization format: the scenario tool wrote a TJS array of line
 * dictionaries into a blob, and `.root` is that array again. The layout below
 * -- header offsets, the variable-width integer arrays, the name trie, and the
 * value type byte -- was read off FreeMote, the public reference for the
 * format (/root/re/psb in the work container). No FreeMote code is copied; the
 * mapping onto TJS values is the part that matters here and has no counterpart
 * there.
 *
 * What is not implemented: the `mdf` compressed container and header/body
 * encryption. Those are how PSBs are shipped as game assets, not how a blob
 * inside the game's own database is written, and a file that is one says so
 * in the log rather than being half-parsed.
 */
#include "ncbind/ncbind.hpp"
#include "MsgIntf.h"
#include "CharacterSet.h"

#include <string>
#include <vector>
#include <cstring>

#define NCB_MODULE_NAME TJS_W("psbfile.dll")

namespace {

/** PSB value type bytes. */
enum PsbType {
	PSB_NONE      = 0x00,
	PSB_NULL      = 0x01,
	PSB_FALSE     = 0x02,
	PSB_TRUE      = 0x03,
	PSB_INT0      = 0x04,   // 0x04..0x0C: an integer in 0..8 little-endian bytes
	PSB_INT8      = 0x0C,
	PSB_ARRAY1    = 0x0D,   // 0x0D..0x14: an integer array, N = byte width of the count
	PSB_ARRAY8    = 0x14,
	PSB_STRING1   = 0x15,   // 0x15..0x18: index into the string table
	PSB_STRING4   = 0x18,
	PSB_RESOURCE1 = 0x19,   // 0x19..0x1C: index into the chunk table
	PSB_RESOURCE4 = 0x1C,
	PSB_FLOAT0    = 0x1D,
	PSB_FLOAT     = 0x1E,
	PSB_DOUBLE    = 0x1F,
	PSB_LIST      = 0x20,
	PSB_OBJECTS   = 0x21,
	PSB_EXTRA1    = 0x22,   // 0x22..0x25: index into the v4 extra chunk table
	PSB_EXTRA4    = 0x25
};

/** Deep enough for any real scene tree; a bound so a damaged blob cannot
 *  recurse until the stack runs out. */
const int MaxDepth = 128;

void Fail(const tjs_char *why)
{
	TVPThrowExceptionMessage(why);
}

/** A bounds-checked cursor over the octet. Every read is validated: this
 *  parses data that came out of a file on the card. */
class Reader {
public:
	Reader(const tjs_uint8 *base, size_t size) : base(base), size(size), pos(0) {}

	void Seek(tjs_uint64 to) {
		if (to > size) Fail(TJS_W("psbfile: an offset points past the end of the data"));
		pos = (size_t)to;
	}
	size_t Tell() const { return pos; }

	tjs_uint8 U8() {
		if (pos >= size) Fail(TJS_W("psbfile: the data ends in the middle of a value"));
		return base[pos++];
	}

	/** n little-endian bytes, zero extended. */
	tjs_uint64 UN(int n) {
		if (n < 0 || n > 8) Fail(TJS_W("psbfile: an integer claims an impossible width"));
		if (pos + (size_t)n > size) Fail(TJS_W("psbfile: the data ends in the middle of an integer"));
		tjs_uint64 v = 0;
		for (int i = 0; i < n; i++) v |= (tjs_uint64)base[pos + (size_t)i] << (8 * i);
		pos += (size_t)n;
		return v;
	}

	tjs_uint32 U32At(size_t off) const {
		if (off + 4 > size) Fail(TJS_W("psbfile: the header is shorter than a PSB header"));
		return (tjs_uint32)base[off] | ((tjs_uint32)base[off + 1] << 8) |
		       ((tjs_uint32)base[off + 2] << 16) | ((tjs_uint32)base[off + 3] << 24);
	}

	tjs_uint16 U16At(size_t off) const {
		if (off + 2 > size) Fail(TJS_W("psbfile: the header is shorter than a PSB header"));
		return (tjs_uint16)(base[off] | (base[off + 1] << 8));
	}

	/** A NUL-terminated UTF-8 string starting at `off`. */
	ttstr StringAt(tjs_uint64 off) const {
		if (off >= size) Fail(TJS_W("psbfile: a string starts past the end of the data"));
		const char *s = (const char *)base + off;
		size_t max = size - (size_t)off;
		size_t len = 0;
		while (len < max && s[len]) len++;
		if (len == max) Fail(TJS_W("psbfile: a string is not terminated"));
		if (!len) return ttstr();
		tjs_int wide = TVPUtf8ToWideCharString(s, NULL);
		if (wide <= 0) return ttstr();
		ttstr r;
		tjs_char *p = r.AllocBuffer((tjs_uint)wide);
		TVPUtf8ToWideCharString(s, p);
		p[wide] = 0;
		r.FixLen();
		return r;
	}

	const tjs_uint8 *Base() const { return base; }
	size_t Size() const { return size; }

private:
	const tjs_uint8 *base;
	size_t size;
	size_t pos;
};

typedef std::vector<tjs_uint64> UIntArray;

/**
 * One of PSB's packed integer arrays: a type byte whose low nibble gives the
 * byte width of the element count, the count itself, then a second type byte
 * giving the byte width of each element, then the elements.
 */
UIntArray ReadArray(Reader &r)
{
	tjs_uint8 t = r.U8();
	if (t < PSB_ARRAY1 || t > PSB_ARRAY8) Fail(TJS_W("psbfile: expected an integer array"));
	tjs_uint64 count = r.UN(t - PSB_ARRAY1 + 1);
	tjs_uint8 widthByte = r.U8();
	// The element width is encoded against the integer types, so the byte is
	// one past PSB_INT8 for a width of one.
	if (widthByte <= PSB_INT8 || widthByte > PSB_INT8 + 8) {
		Fail(TJS_W("psbfile: an array's element width is not a valid integer width"));
	}
	int width = widthByte - PSB_INT8;
	if (count > (tjs_uint64)(r.Size() / (size_t)width) + 1) {
		Fail(TJS_W("psbfile: an array is longer than the data that holds it"));
	}
	UIntArray out;
	out.reserve((size_t)count);
	for (tjs_uint64 i = 0; i < count; i++) out.push_back(r.UN(width));
	return out;
}

/** The parse of one PSB octet: the tables, then Unpack() over the entry. */
class PsbParser {
public:
	PsbParser(const tjs_uint8 *data, size_t size) : r(data, size), version(0) {}

	tTJSVariant Parse();

private:
	tTJSVariant Unpack(int depth);
	void LoadNames();

	Reader r;
	tjs_uint16 version;
	tjs_uint32 offsetNames, offsetStrings, offsetStringsData;
	tjs_uint32 offsetChunkOffsets, offsetChunkLengths, offsetChunkData;
	tjs_uint32 offsetEntries, headerLength;
	std::vector<ttstr> names;
	UIntArray stringOffsets, chunkOffsets, chunkLengths;
};

void PsbParser::LoadNames()
{
	// v2+ stores the names as a trie walked backwards from each leaf: charset
	// gives the per-node offset, namesData the parent link, nameIndexes the
	// leaf of each name. Walking a leaf to the root spells the name reversed.
	r.Seek(offsetNames);
	UIntArray charset = ReadArray(r);
	UIntArray namesData = ReadArray(r);
	UIntArray nameIndexes = ReadArray(r);

	names.reserve(nameIndexes.size());
	for (size_t i = 0; i < nameIndexes.size(); i++) {
		std::string bytes;
		tjs_uint64 chr = nameIndexes[i] < namesData.size() ? namesData[(size_t)nameIndexes[i]] : 0;
		size_t guard = 0;
		while (chr != 0) {
			if (chr >= namesData.size()) Fail(TJS_W("psbfile: a name walks off the trie"));
			tjs_uint64 code = namesData[(size_t)chr];
			if (code >= charset.size()) Fail(TJS_W("psbfile: a name uses an unknown charset entry"));
			bytes.push_back((char)(tjs_uint8)(chr - charset[(size_t)code]));
			chr = code;
			if (++guard > namesData.size()) Fail(TJS_W("psbfile: a name trie walk does not terminate"));
		}
		std::string forward(bytes.rbegin(), bytes.rend());
		tjs_int wide = forward.empty() ? 0 : TVPUtf8ToWideCharString(forward.c_str(), NULL);
		ttstr name;
		if (wide > 0) {
			tjs_char *p = name.AllocBuffer((tjs_uint)wide);
			TVPUtf8ToWideCharString(forward.c_str(), p);
			p[wide] = 0;
			name.FixLen();
		}
		names.push_back(name);
	}
}

tTJSVariant PsbParser::Parse()
{
	if (r.Size() < 40) Fail(TJS_W("psbfile: too short to be a PSB"));
	const tjs_uint8 *b = r.Base();
	if (!(b[0] == 'P' && b[1] == 'S' && b[2] == 'B' && b[3] == 0)) {
		if (b[0] == 'm' && b[1] == 'd' && b[2] == 'f' && b[3] == 0) {
			Fail(TJS_W("psbfile: this is an mdf (compressed) PSB, which is not supported"));
		}
		Fail(TJS_W("psbfile: not a PSB (no \"PSB\\0\" signature)"));
	}

	version = r.U16At(4);
	tjs_uint16 headerEncrypt = r.U16At(6);
	if (headerEncrypt != 0) Fail(TJS_W("psbfile: this PSB has an encrypted header, which is not supported"));

	headerLength       = r.U32At(8);
	offsetNames        = r.U32At(12);
	offsetStrings      = r.U32At(16);
	offsetStringsData  = r.U32At(20);
	offsetChunkOffsets = r.U32At(24);
	offsetChunkLengths = r.U32At(28);
	offsetChunkData    = r.U32At(32);
	offsetEntries      = r.U32At(36);

	if (version == 1) {
		// v1 keeps the names as plain NUL-terminated strings at offsetNames,
		// indexed by a table that sits at the end of the header.
		r.Seek(headerLength);
		UIntArray nameIndexes = ReadArray(r);
		names.reserve(nameIndexes.size());
		for (size_t i = 0; i < nameIndexes.size(); i++) {
			names.push_back(r.StringAt((tjs_uint64)offsetNames + nameIndexes[i]));
		}
	} else {
		LoadNames();
	}

	r.Seek(offsetStrings);
	stringOffsets = ReadArray(r);
	r.Seek(offsetChunkOffsets);
	chunkOffsets = ReadArray(r);
	r.Seek(offsetChunkLengths);
	chunkLengths = ReadArray(r);

	r.Seek(offsetEntries);
	return Unpack(0);
}

tTJSVariant PsbParser::Unpack(int depth)
{
	if (depth > MaxDepth) Fail(TJS_W("psbfile: the value tree is nested too deeply"));

	tjs_uint8 t = r.U8();

	if (t == PSB_NONE || t == PSB_NULL) return tTJSVariant();
	if (t == PSB_FALSE) return tTJSVariant((tjs_int)0);
	if (t == PSB_TRUE)  return tTJSVariant((tjs_int)1);

	if (t >= PSB_INT0 && t <= PSB_INT8) {
		int n = t - PSB_INT0;
		tjs_uint64 raw = r.UN(n);
		// Up to four bytes the value is a 32-bit int; wider, a 64-bit one.
		// Zero-extended bytes are reinterpreted as signed, so a four-byte
		// 0xFFFFFFFF is -1 and a one-byte 0xFF is 255.
		if (n <= 4) return tTJSVariant((tjs_int64)(tjs_int32)(tjs_uint32)raw);
		return tTJSVariant((tjs_int64)raw);
	}

	if (t >= PSB_ARRAY1 && t <= PSB_ARRAY8) {
		// A bare integer array as a value. Rewind over the type byte so
		// ReadArray sees the whole thing.
		r.Seek(r.Tell() - 1);
		UIntArray values = ReadArray(r);
		iTJSDispatch2 *arr = TJSCreateArrayObject();
		for (size_t i = 0; i < values.size(); i++) {
			tTJSVariant v((tjs_int64)values[i]);
			arr->PropSetByNum(TJS_MEMBERENSURE, (tjs_int)i, &v, arr);
		}
		tTJSVariant result(arr, arr);
		arr->Release();
		return result;
	}

	if (t >= PSB_STRING1 && t <= PSB_STRING4) {
		tjs_uint64 index = r.UN(t - PSB_STRING1 + 1);
		if (index >= stringOffsets.size()) Fail(TJS_W("psbfile: a string index is out of range"));
		return tTJSVariant(r.StringAt((tjs_uint64)offsetStringsData + stringOffsets[(size_t)index]));
	}

	if (t >= PSB_RESOURCE1 && t <= PSB_RESOURCE4) {
		tjs_uint64 index = r.UN(t - PSB_RESOURCE1 + 1);
		if (index >= chunkOffsets.size() || index >= chunkLengths.size()) {
			Fail(TJS_W("psbfile: a resource index is out of range"));
		}
		tjs_uint64 at = (tjs_uint64)offsetChunkData + chunkOffsets[(size_t)index];
		tjs_uint64 len = chunkLengths[(size_t)index];
		if (at + len > r.Size()) Fail(TJS_W("psbfile: a resource runs past the end of the data"));
		return tTJSVariant(r.Base() + at, (tjs_uint)len);
	}

	if (t >= PSB_EXTRA1 && t <= PSB_EXTRA4) {
		// v4 extra chunks live in tables this reader does not carry; nothing
		// the scene database stores uses them.
		r.UN(t - PSB_EXTRA1 + 1);
		return tTJSVariant();
	}

	if (t == PSB_FLOAT0) return tTJSVariant((tjs_real)0.0);
	if (t == PSB_FLOAT) {
		tjs_uint32 bits = (tjs_uint32)r.UN(4);
		float f;
		memcpy(&f, &bits, sizeof(f));
		return tTJSVariant((tjs_real)f);
	}
	if (t == PSB_DOUBLE) {
		tjs_uint64 bits = r.UN(8);
		double d;
		memcpy(&d, &bits, sizeof(d));
		return tTJSVariant((tjs_real)d);
	}

	if (t == PSB_LIST) {
		UIntArray offsets = ReadArray(r);
		size_t base = r.Tell();
		iTJSDispatch2 *arr = TJSCreateArrayObject();
		try {
			for (size_t i = 0; i < offsets.size(); i++) {
				r.Seek((tjs_uint64)base + offsets[i]);
				tTJSVariant v = Unpack(depth + 1);
				arr->PropSetByNum(TJS_MEMBERENSURE, (tjs_int)i, &v, arr);
			}
		} catch (...) {
			arr->Release();
			throw;
		}
		tTJSVariant result(arr, arr);
		arr->Release();
		return result;
	}

	if (t == PSB_OBJECTS) {
		UIntArray keys = ReadArray(r);
		UIntArray offsets = ReadArray(r);
		size_t base = r.Tell();
		if (keys.size() != offsets.size()) Fail(TJS_W("psbfile: an object has more keys than values"));
		iTJSDispatch2 *dic = TJSCreateDictionaryObject();
		try {
			for (size_t i = 0; i < keys.size(); i++) {
				if (keys[i] >= names.size()) Fail(TJS_W("psbfile: an object key is not in the name table"));
				r.Seek((tjs_uint64)base + offsets[i]);
				tTJSVariant v = Unpack(depth + 1);
				dic->PropSet(TJS_MEMBERENSURE, names[(size_t)keys[i]].c_str(), NULL, &v, dic);
			}
		} catch (...) {
			dic->Release();
			throw;
		}
		tTJSVariant result(dic, dic);
		dic->Release();
		return result;
	}

	Fail(TJS_W("psbfile: unknown value type in the PSB"));
	return tTJSVariant();
}

} // anonymous namespace

/**
 * PSBFile(octet): the parse happens in the constructor, and `root` is the
 * value tree. That is the whole surface the scripts use -- KAGEnvPlayer reads
 * `scenario.root` for every line, KAGEnvSceneDB reads `psb.root` for voices,
 * and restore() reads it for a saved screen state.
 */
class PSBFile {
public:
	PSBFile(tTJSVariant source) {
		if (source.Type() != tvtOctet) {
			TVPThrowExceptionMessage(TJS_W("psbfile: PSBFile takes an octet"));
		}
		tTJSVariantOctet *o = source.AsOctetNoAddRef();
		if (!o || !o->GetLength()) {
			TVPThrowExceptionMessage(TJS_W("psbfile: PSBFile was given an empty octet"));
		}
		PsbParser parser(o->GetData(), (size_t)o->GetLength());
		root = parser.Parse();
	}

	tTJSVariant getRoot() const { return root; }

private:
	tTJSVariant root;
};

NCB_REGISTER_CLASS(PSBFile) {
	NCB_CONSTRUCTOR((tTJSVariant));
	NCB_PROPERTY_RO(root, getRoot);
}
