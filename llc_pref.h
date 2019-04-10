#include <stdio.h>
#include <assert.h>
#include <inttypes.h>
#include "cache.h"

#include <vector>
#include <list>


void init_llc_prefetcher();
void llc_cache_prefetcher(cache_t* _llc, req_t* _req, uint64_t _clk);
