// See EnginehostProfile.h (Droidtop/tracker#853).
#include "EnginehostProfile.h"

#include <chrono>
#include <cstdio>
#include <mutex>
#ifdef __ANDROID__
#include <android/log.h>
#endif

namespace {

const int64_t kQuietNs = 500LL * 1000 * 1000; // this long without work ends a moment
const int64_t kSlowNs = 300LL * 1000 * 1000;  // a moment this long is logged

thread_local EhProfScope *tCurrent = nullptr;
thread_local int tDepth = 0;

struct Moment {
	int64_t firstNs = 0, lastNs = 0;
	int64_t ns[EH_PROF_KINDS] = {};
	uint64_t count[EH_PROF_KINDS] = {};
	uint64_t misses = 0, bytes = 0;
};

std::mutex gLock;
Moment gMoment;

int64_t NowNs()
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}

void Report(const Moment &m)
{
	int64_t wall = m.lastNs - m.firstNs;
	if (wall < kSlowNs) return;
	int64_t measured = 0;
	for (int i = 0; i < EH_PROF_KINDS; i++) measured += m.ns[i];
	int64_t other = wall > measured ? wall - measured : 0;
	auto ms = [](int64_t ns) { return (long long) (ns / 1000000); };
	char line[512];
	snprintf(line, sizeof line,
		"slow moment: %lld ms; lookups %llu (%llu missed) %lld ms, opens %llu %lld ms, "
		"reads %.1f MB %lld ms, images %llu %lld ms, scripts %llu %lld ms, other %lld ms",
		ms(wall),
		(unsigned long long) m.count[EH_PROF_LOOKUP], (unsigned long long) m.misses, ms(m.ns[EH_PROF_LOOKUP]),
		(unsigned long long) m.count[EH_PROF_OPEN], ms(m.ns[EH_PROF_OPEN]),
		m.bytes / 1048576.0, ms(m.ns[EH_PROF_READ]),
		(unsigned long long) m.count[EH_PROF_IMAGE], ms(m.ns[EH_PROF_IMAGE]),
		(unsigned long long) m.count[EH_PROF_SCRIPT], ms(m.ns[EH_PROF_SCRIPT]),
		ms(other));
#ifdef __ANDROID__
	__android_log_write(ANDROID_LOG_INFO, "EnginehostKiriKiri", line);
#else
	fprintf(stderr, "EnginehostKiriKiri: %s\n", line);
#endif
}

} // namespace

EhProfScope::EhProfScope(EhProfKind kind)
	: Kind(kind), StartNs(NowNs()), Parent(tCurrent)
{
	tCurrent = this;
	tDepth++;
	if (tDepth == 1) {
		// The first piece of work after a quiet spell starts a new moment; the one before it is
		// reported now, since nothing else would end it.
		Moment done;
		bool ended = false;
		{
			std::lock_guard<std::mutex> hold(gLock);
			if (gMoment.lastNs && StartNs - gMoment.lastNs > kQuietNs) {
				done = gMoment;
				gMoment = Moment();
				ended = true;
			}
			if (!gMoment.firstNs) gMoment.firstNs = StartNs;
		}
		if (ended) Report(done);
	}
}

EhProfScope::~EhProfScope()
{
	int64_t end = NowNs();
	int64_t total = end - StartNs;
	int64_t own = total > ChildNs ? total - ChildNs : 0;
	if (Parent) Parent->ChildNs += total;
	tCurrent = Parent;
	tDepth--;
	std::lock_guard<std::mutex> hold(gLock);
	gMoment.ns[Kind] += own;
	gMoment.count[Kind]++;
	if (Miss) gMoment.misses++;
	gMoment.bytes += ByteCount;
	if (!gMoment.firstNs) gMoment.firstNs = StartNs;
	if (end > gMoment.lastNs) gMoment.lastNs = end;
}
