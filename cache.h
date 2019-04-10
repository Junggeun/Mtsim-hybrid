#ifndef __CACHE_H__
#define __CACHE_H__

#include <inttypes.h>

#include "block.h"
#include "core.h"
#include "hybrid_mem.h"

//#define L1_PREF_AVAIL
//#define L2_PREF_AVAIL
//#define L3_PREF_AVAIL

typedef struct tagstore					tagstore_t;
//typedef struct cache_structure	cache_t;

//* @Junggeun: To follow the ctrl flow of ChampSim, I have chosen the same
//*						 structure as the ChampSim, using in-order memory queue for $ structure.
typedef struct mem_req_queue
{
	uint32_t		capacity;
	uint32_t		n_req;
	req_t				*_head;
	req_t				*_tail;
} mem_queue_t;

struct tagstore			//? a.k.a "cache block" or "block"
{
	bool			valid;	//? valide bit (0: invalid)
	bool			dirty;	//? dirty bit  (0: clean)
	uint64_t	tag;		//? tag data
	uint32_t	repl;		//? additional information for replacement
	
	//* @Junggeun: This variables are for cache management policies which need additional information
	//*						 to guess a current workload's characteristic.
	//*								ex) Don't prefetch when observing irregular access pattern to this $ level.
	//* Info: $-block meta-data for management policies (replacement, reuse-aware management, prefetching, etc.)
	bool			pref;		//? is this prefetched blk or not?
	uint8_t		used;		//? re-used time
	uint16_t	dist;		//? reuse distance
};

/*
typedef enum cache_type
{
	directMapped,
	setAssociate,
	fulAssociate,
} cache_map_t;

typedef enum repl_policy
{
	FIFO,
	RAND,
	LRU,
} repl_t;
*/

typedef enum cache_level
{
	LEVEL_NONE, CACHE_L1=1, CACHE_L2, CACHE_L3
} level_t;

//* @Junggeun: I've selected 'Write-back' as write-hit policy and
//*						 'Write-allocate' as write-miss policy.
//* @Junggeun: 

//* @Junggeun: **IMPORTANT** Mapping when the cache size is not a power of 2:
//*					 : Intel has definitely not released all the details of how they perform the cache mapping.
//*					 : There are two places where non-power-of-two sizes occur.
//*					 : 	1. Recent Xeon processors generally have 2.5 MiB LLC per 'slice', which is 128KiB of addresses
//*					 :     (2048 cache line addresses) with a 20-way associativity for each location.
//*					 :  2. Recent Xeon processors perform a hash on many high-order address bits to determine which LLC "slice" a line
//*					 :		 should map to. The detailed has has been reverse-engineered for XeonE5 v1, v2, v3 systems with 8 cores.
//*					 :		 In this case the hash can be expressed as an XOR. For non-power-of-two slice counts the hash is harder to
//*					 :		 understand, and I am not aware of any closed-form expressions that have been discovered. 
//*					 : Slide 8 of the presentation at
//*					 : (http://www.hotchips.org/wp-content/uploads/hc_archives/hc22/HC22.24.620-Hill-Intel-WSM-EP-print.pdf) shows
//*					 : on the XeonE56xx processors is built up from 6 slices, each 2MiB and 16-way associative (so 2048 cache-line-sized
//*					 : sets per L3 slice).
//*					 : (https://software.intel.com/en-us/forums/software-tuning-performance-optimization-platform-monitoring/topic/701635)
//*					 : (by McCalpin, John, "Dr. Bandwidth")

//* @Junggeun: This cache structure is based on "Fully-pipelined" cache architecture,
//*					 : which does process multiple-stages of cache functions (tag search, array access, mshr checks, etc.) at one cycle,
//*					 : hence, it can enhance entire cache performance without widening the size of transaction queues, or mshr size.

struct cache_structure
{
//? cache configuration
	uint64_t		n_size;
	uint32_t		n_way;
	uint32_t		n_set;
	uint32_t		n_line;
	uint32_t		lat_tag;
	uint32_t		lat_data;
	cache_map_t	map_type;
	repl_t			repl_type;
	uint32_t		size_mshr;
	uint32_t		size_txQ;
	uint32_t		size_pfQ;
//? cache organization: banks, etc.
	uint32_t		n_banks;
	uint64_t		*bank_busy;
	tagstore_t	**tag;
//? meta information:
	bool				is_first;
	bool				is_last;
	bool				is_perfect;
	bool				has_banks;
	uint64_t		index_bit;
	uint64_t		offset_bit;
//? cache statistics:
	uint64_t		access_cnt;
	uint64_t		hit_cnt;
	uint64_t		miss_cnt;
	uint32_t		pf_cnt;
	uint32_t		pf_hit;
//? cache queues:
	mem_queue_t	*_mshr;
	mem_queue_t	*_txQ;
	mem_queue_t	*_pfQ;
//? ownership:
	rob_t				*_rob;
	core_t			*_core;
	proc_t			*_proc;
//? interconnections:
	cache_t			*_master;
	cache_t			*_slave;
	level_t			cache_level;
//? only for llc
	cache_t			**_masters;
	uint32_t		n_cores;

	uint64_t		pref_timing_cnt;

//? prefetcher
};

cache_t*
init_cache_structure
(
	uint64_t n_size,
	uint32_t n_way,
	uint32_t n_set,
	uint32_t n_line,
	uint32_t lat_tag,
	uint32_t lat_data,
	cache_map_t map_type,
	repl_t repl_type,
	uint32_t size_mshr,
	uint32_t size_txQ,
	uint32_t size_pfQ,
	uint32_t n_banks,
	bool is_first,
	bool is_last,
	bool has_banks,
	bool is_perfect,
	rob_t *_rob,
	core_t *_core,
	proc_t *_proc,
	uint32_t n_cores
);

bool cache_check_hit(cache_t* _cache, uint64_t addr);
bool is_cache_busy(cache_t* _cache, uint64_t addr);
bool cache_access(cache_t* _cache, uint64_t addr, op_type type);

bool cache_is_pf_hit(cache_t* _cache, uint64_t addr);

int	 cache_get_level(cache_t* _cache);

int	 is_there_victim(cache_t* _cache, uint64_t addr);
void cache_insert_line(cache_t* _cache, uint64_t addr, op_type type, uint32_t _way, req_t* _req, bool _pf);
void cache_evict_line(cache_t* _cache, uint64_t addr, op_type type, req_t* _req);

//void cache_insert_line_directly(cache_t* _cache, uint64_t _addr, op_type type);

uint64_t cache_evict_addr(cache_t* _cache, uint64_t addr);

void cache_do_replacement(cache_t* _cache, uint64_t _addr, uint32_t _way);
void cache_FIFO_replacement(cache_t* _cache, uint64_t _addr, uint32_t _way);
void cache_LRU_replacement(cache_t* _cache, uint64_t _addr, uint32_t _way);
void cache_RAND_replacement(cache_t* _cache, uint64_t _addr, uint32_t _way);
uint32_t cache_get_target_way(cache_t* _cache, uint64_t _addr);

void connect_caches(cache_t* _slave, cache_t* _master);
void connect_llc(cache_t* _llc, uint32_t n_cores, cache_t** _l2_caches);

void cache_handle_access(cache_t* _cache, uint64_t _clk);
void cache_handle_access_2(cache_t* _cache, uint64_t _clk);

void cache_operate(cache_t* _cache, uint64_t _clk);
void cache_handle_prefetch(cache_t* _cache, uint64_t _clk);
//* @junggeun: as I told above, all queues in the mtsim++ are in-order
//*						 basis; requests being in done staus must wait for order.

mem_queue_t* init_mem_queue_structure(uint32_t capacity);

void mem_queue_insert_request(mem_queue_t* _queue, req_t* _req);
void mem_queue_remove_request(mem_queue_t* _queue, req_t* _req);
bool is_mem_queue_full(mem_queue_t* _queue);
bool is_mem_queue_empty(mem_queue_t* _queue);

//* @junggeun: MSHR (Miss Status Handling Register / ~ Holding Register) handling functions.
bool mshr_exist_entry(cache_t* _cache, uint64_t addr);
void mshr_remove_entry(cache_t* _cache, uint64_t addr, op_type type, uint64_t _clk);

//TODO: Prefetcher section::
void mem_queue_insert_pfreq(mem_queue_t* _queue, req_t* _req, uint32_t idx);
void init_l1_prefetcher(cache_t* _l1);
void l1_cache_prefetcher(cache_t* _l1, req_t* _req, uint64_t _clk);
void init_l2_prefetcher(cache_t* _l2);
void l2_cache_prefetcher(cache_t* _l2, req_t* _req, uint64_t _clk);
void init_llc_prefetcher(cache_t* _llc);
void llc_cache_prefetcher(cache_t* _llc, req_t* _req, uint64_t _clk);

int32_t cache_get_pref_bit(cache_t* _cache, uint64_t _addr);
void		cache_set_pref_bit(cache_t* _cache, uint64_t _addr);
void		cache_unset_pref_bit(cache_t* _cache, uint64_t _addr);

#endif