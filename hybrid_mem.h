#ifndef __HYBRID_MEM_H__
#define __HYBRID_MEM_H__

#include <stdio.h>
#include <inttypes.h>
#include "block.h"
//#include "cache.h"
//#include "memory.h"

typedef struct memory_controller mem_ctrl_t;
typedef struct tagstore tagstore_t;

typedef struct hybrid_cache						hybrid_cache_t;
typedef struct hybrid_mem_controller	hm_ctrl_t;
typedef struct hmem_ctrl_queue				hm_queue_t;
typedef struct hmem_request_packet		hmem_req_t;

//? only for heterogeneous/hybrid memory request packet!
typedef enum hmem_state
{
	HMEM_IDLE,
	HMEM_TAG,
	HMEM_HIT,
	HMEM_MISS,
	HMEM_MIGRATION,
	HMEM_DONE,
	HMEM_COMMIT
} hmem_state_t;

struct hmem_request_packet
{
	//? general information of memory request packet
	op_type		req_type;
	uint64_t	phys_addr;	//? actual physical address.
	uint64_t	comp_time;
	uint32_t	core_id;
	//? statistics:
	bool			is_hit;
	uint64_t	dispatch_time;
	uint64_t	arrival_time;
	uint64_t	latency;	//? dispatch_time - arrival_time (for evaluating avg. queue waiting time.)
	//? misc.:
	req_t*			_c_req;	//? pointer for cache request packet
	hmem_req_t*	_next;
	//? status:
	hmem_state_t	state;
};

struct hmem_ctrl_queue
{
	uint32_t	capacity;
	uint32_t	n_req;
	hmem_req_t	*_head;
	hmem_req_t	*_tail;
	hm_ctrl_t	*_mem_ctrl;
};

struct hybrid_cache
{
	uint64_t		n_size;
	uint32_t		n_way;
	uint64_t		n_set;
	uint32_t		n_line;
	
	cache_map_t	map_type;
	repl_t			repl_type;

	tagstore_t	**tag;

	uint64_t		index_bit;
	uint64_t		offset_bit;

	uint64_t		hit_cnt;
	uint64_t		miss_cnt;
	uint64_t		pf_cnt;
	uint64_t		pf_hit;

	uint64_t		comp_time;

	hm_queue_t	*_txQ;
	hm_ctrl_t		*_ctrl;
};

struct hybrid_mem_controller
{
	uint64_t				hc_size;
	uint32_t				hc_way;
	uint64_t				hc_set;
	uint32_t				hc_line;
	cache_map_t			hc_type;
	repl_t					hc_repl;
	hybrid_cache_t	*_hcache;

	uint32_t				size_txQ;
	hm_queue_t			*_txQ;

	mem_ctrl_t			*_nvm_ctrl;
	mem_ctrl_t			*_dram_ctrl;

	uint64_t				total_hmem_req_cnt;
	uint64_t				interval_bandwidth;
	uint64_t				total_bandwidth;
};

hybrid_cache_t*	init_hybrid_cache_structure(hm_ctrl_t* _ctrl, uint64_t n_size, uint32_t n_way, uint64_t n_set, uint32_t n_line, cache_map_t map_type, repl_t repl_type);
hm_ctrl_t*			init_hybrid_memory_controller(uint32_t size_txQ, uint64_t hc_size, uint32_t hc_way, uint64_t hc_set, uint32_t hc_line, cache_map_t hc_type, repl_t hc_repl);

hm_queue_t*	h_init_memory_request_queue(hm_ctrl_t* _mem_ctrl, uint32_t capacity);
void				h_mem_txq_insert_req(hm_queue_t* _queue, hmem_req_t* _req);
void				h_mem_txq_remove_req(hm_queue_t* _queue, hmem_req_t* _req);
bool				h_is_req_queue_full(hm_queue_t*	_queue);
bool				h_is_req_queue_empty(hm_queue_t* _queue);

void				hybrid_memory_operate(hm_ctrl_t* hm_ctrl, uint64_t _clk);	//TODO

uint64_t		hybrid_address_translate(hybrid_cache_t* _hcache, uint64_t addr);
void				hcache_handle_access(hybrid_cache_t* _hcache, uint64_t _clk);
bool				hcache_check_hit(hybrid_cache_t* _hcache, uint64_t addr);
bool				hcache_access(hybrid_cache_t* _hcache, uint64_t addr);

int					hcache_find_victim(hybrid_cache_t* _hcache, uint64_t addr);
uint32_t		hcache_get_target_way(hybrid_cache_t* _hcache, uint64_t addr);
void				hcache_insert_line(hybrid_cache_t* _hcache, uint64_t addr, op_type type, uint32_t _way);
void				hcache_evict_line(hybrid_cache_t* _hcache, uint64_t addr, op_type type);

void				hcache_do_replacement(hm_ctrl_t* hm_ctrl, uint64_t addr, uint32_t way);
void				hybrid_memory_operate(hm_ctrl_t* hm_ctrl, uint64_t _clk);

void				hmem_dram_insert_req(hm_ctrl_t* hm_ctrl, hmem_req_t* _req);		//TODO
void				hmem_nvm_insert_req(hm_ctrl_t* hm_ctrl, hmem_req_t* _req);		//TODO



#endif