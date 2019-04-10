#include <stdio.h>
#include <inttypes.h>
#include <list>

#include "block.h"

//* Prefetch Buffer (PB) size in entries.
#define PREFETCH_BUFFER_SIZE		64

//* Sandbox size in entries.
#define L2_ACCESS_COUNT					1024

//* Latency averages in entries.
#define LATENCY_AVERAGES				32
#define LATENCY_AVERAGES_SHIFT	(unsigned short)log2(LATENCY_AVERAGES)

//* Number of prefetchers (total).
#define PREFETCHER_COUNT				9

//* Abstract generic prefetcher class definition. Any prefetchers must implement these functions
//* because they're pure virtual. It makes a clear interface if we have more than one type of prefetcher.
class BasePrefetcher
{
	public:
		BasePrefetcher() {}
		virtual ~BasePrefetcher() {}
		virtual unsigned long long int predict(unsigned long long int pc, unsigned long long int address) = 0;
		virtual int getOffset() = 0;
};

//* Sequential offset prefetcher class definition.
//* This prefetcher always predicts address access Ai + offset.
class SequentialPrefetcher : public BasePrefetcher
{
	public:
		SequentialPrefetcher(int offset) : BasePrefetcher(), offset(offset) {}
		virtual ~SequentialPrefetcher() {}
		int getOffset() {return this->offset;};
		void setOffset(int offset) {this->offset = offset;};
		unsigned long long int predict(unsigned long long int pc, unsigned long long int address)
		{
			if (offset > 0)	return (address + (offset * CACHE_LINE_SIZE));
			else						return (address - ((offset * -1) * CACHE_LINE_SIZE));
		}
	protected:
		int offset;
};



