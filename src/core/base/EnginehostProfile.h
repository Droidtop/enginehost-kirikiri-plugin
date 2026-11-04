// Where the time of a slow moment goes, for Droidtop/tracker#853 ("Kirikiri takes several seconds
// to open the load menu"). Self-contained: std only, so the TJS compiler, the image loader and
// the storage layer can all include it.
//
// A scope times one piece of work of one kind. Time is exclusive: a scope nested in another (an
// image a script loads while it runs) is taken out of the outer one, so the kinds add up. When
// work stops for 500 ms, the moment that just ended is logged under EnginehostKiriKiri if it
// lasted 300 ms or more:
//
//   slow moment: 4210 ms; lookups 812 (37 missed) 1630 ms, opens 46 410 ms, reads 3.1 MB 220 ms,
//   images 23 1500 ms, scripts 2 300 ms, other 150 ms
//
// "other" is the rest of the moment's wall time: drawing, TJS running between the pieces
// measured, and waiting. Logging only; nothing the engine does changes.
#pragma once

#include <cstdint>

enum EhProfKind { EH_PROF_LOOKUP, EH_PROF_OPEN, EH_PROF_READ, EH_PROF_IMAGE, EH_PROF_SCRIPT, EH_PROF_KINDS };

class EhProfScope {
public:
	explicit EhProfScope(EhProfKind kind);
	~EhProfScope();
	// A lookup that found nothing, or how many bytes a read returned.
	void Missed() { Miss = true; }
	void Bytes(uint64_t n) { ByteCount = n; }

private:
	EhProfKind Kind;
	int64_t StartNs;
	int64_t ChildNs = 0;
	EhProfScope *Parent;
	bool Miss = false;
	uint64_t ByteCount = 0;
	EhProfScope(const EhProfScope &) = delete;
	EhProfScope &operator=(const EhProfScope &) = delete;
};
