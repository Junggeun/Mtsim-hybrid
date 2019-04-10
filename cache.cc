#include "cache.h"
#include "mtsim.h"
#include "memory.h"
#include "hybrid_mem.h"

#include <malloc.h>
#include <string.h>
#include <assert.h>

//* @junggeun: as I told above, all queues in the mtsim++ are in-order
//*						 basis; requests being in done staus must wait for order.

mem_queue_t*
init_mem_queue_structure(uint32_t capacity)
{
	mem_queue_t* new_queue = (mem_queue_t*)malloc(sizeof(mem_queue_t));
	new_queue->capacity = capacity;
	new_queue->n_req = 0;
	new_queue->_head = NULL;
	new_queue->_tail = NULL;
	return new_queue;
}

void
mem_queue_insert_request(mem_queue_t* _queue, req_t* _req)
{
	//if (_queue->n_req == _queue->capacity)	return;
	req_t* new_req = (req_t*)malloc(sizeof(req_t));
	memcpy(new_req, _req, sizeof(req_t));
	new_req->_next = NULL;
	if (_queue->n_req == 0)
	{
		_queue->_head = new_req;
		_queue->_tail = new_req;
	}
	else
	{
		_queue->_tail->_next = new_req;
		_queue->_tail = new_req;
	}
	_queue->n_req++;
}

void
mem_queue_remove_request(mem_queue_t* _queue, req_t* _req)
{
	req_t *it;
	req_t *prev, *next;

	it = _queue->_head;

	if (_queue->n_req == 1)
	{
		_queue->_head = NULL;
		_queue->_tail = NULL;
		_queue->n_req = 0;
		free(_req);
		return;
	}

	if (_req == _queue->_head)
	{
		_queue->_head = _req->_next;
		_queue->n_req--;
		free(_req);
		return;
	}

	for (uint32_t i=0; i<_queue->n_req; i++)
	{
		if (it->_next == _req)
		{
			prev = it;
			break;
		}
		if (it == _queue->_tail)
		{
			assert(0);
			break;
		}
		it = it->_next;
	}
	next = _req->_next;

	if (_req == _queue->_tail)
	{
		prev->_next = NULL;
		_queue->_tail = prev;
		_queue->n_req--;
		free(_req);
		return;
	}

	prev->_next = next;
	_queue->n_req--;
	free(_req);
}

bool
is_mem_queue_full(mem_queue_t* _queue)
{
	return (_queue->n_req == _queue->capacity);
}

bool
is_mem_queue_empty(mem_queue_t* _queue)
{
	return (_queue->n_req == 0);
}

//* @junggeun: MSHR handling functions.

bool
mshr_exist_entry(cache_t* _cache, uint64_t addr)
{
	req_t*	it = _cache->_mshr->_head;
	uint32_t n = _cache->_mshr->n_req;
	for (uint32_t i = 0; i < n; i++)
	{
		if ((uint64_t)((it->phys_addr)/(uint64_t)CACHE_LINE_SIZE) == (uint64_t)(addr/(uint64_t)CACHE_LINE_SIZE))
			return true;
		if (it->_next == NULL)
			break;
		it = it->_next;
	}
	return false;
}

//TODO: maybe the problem? (9.19.20:07)
void
mshr_remove_entry(cache_t* _cache, uint64_t addr, op_type type, uint64_t _clk)
{
	req_t*		it = _cache->_mshr->_head;
	uint32_t	n_entry = _cache->_mshr->n_req;
	for (uint32_t i=0; i<n_entry; i++)
	{
		it = _cache->_mshr->_head;
		//TODO:: @junggeun: Real sequence of merging MSHR entries differs from this implementation.
		//TODO::						LOAD-LOAD, LOAD-STORE, STORE-LOAD, STORE-STORE should be treated differently.
		while (1)
		{
			//* Delete all entries if MSHR_BLOCK_ADDRESS equals REQUEST_BLOCK_ADDRESS:
			if ((uint64_t)((it->phys_addr) / (uint64_t)CACHE_LINE_SIZE) == (uint64_t)(addr / (uint64_t)CACHE_LINE_SIZE))
			{
				//* Delete the entry.
				mem_queue_remove_request(_cache->_mshr, it);
				break;
			}
			if (it->_next == NULL) break;
			it = it->_next;
		}
	}
}

//* @junggeun: cache structure handling :::::

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
)
{
	cache_t* new_cache = (cache_t*)malloc(sizeof(cache_t));

	new_cache->n_size = n_size;
	new_cache->n_way = n_way;
	new_cache->n_set = n_set;
	new_cache->n_line = n_line;
	new_cache->lat_tag = lat_tag;
	new_cache->lat_data = lat_data;
	new_cache->repl_type = repl_type;
	new_cache->map_type = map_type;
	switch (map_type)
	{
		case directMapped:
			new_cache->n_set = n_size / n_line;
			new_cache->n_way = 1;
			break;
		case setAssociate:
			new_cache->n_set = n_size / n_way / n_line;
			break;
		case fulAssociate:
			new_cache->n_way = n_size / n_line;
			new_cache->n_set = 1;
			break;
	}

	new_cache->index_bit = u64_log2((uint64_t)(new_cache->n_set));
	new_cache->offset_bit = u64_log2((uint64_t)(new_cache->n_line));

	new_cache->size_mshr = size_mshr;
	new_cache->size_txQ = size_txQ;
	new_cache->size_pfQ = size_pfQ;
	new_cache->n_banks = n_banks;

	new_cache->bank_busy = (uint64_t*)malloc(sizeof(uint64_t) * n_banks);
	for (uint32_t i=0; i<n_banks; i++)
		new_cache->bank_busy[i] = 0;

	new_cache->tag = (tagstore_t**)malloc(sizeof(tagstore_t*) * new_cache->n_way);
	for (uint32_t i=0; i<new_cache->n_way; i++)
	{
		new_cache->tag[i] = (tagstore_t*)malloc(sizeof(tagstore_t) * new_cache->n_set);
		for (uint32_t j=0; j<new_cache->n_set; j++)
		{
			new_cache->tag[i][j].valid = false;
			new_cache->tag[i][j].dirty = false;
			new_cache->tag[i][j].tag = 0;
			new_cache->tag[i][j].repl = 0;

			new_cache->tag[i][j].pref = false;
			new_cache->tag[i][j].used = 0;
			new_cache->tag[i][j].dist = 0;
		}
	}

	new_cache->is_first = is_first;
	new_cache->is_last = is_last;
	new_cache->has_banks = has_banks;

	new_cache->is_perfect = is_perfect;

	new_cache->access_cnt = 0;
	new_cache->hit_cnt = 0;
	new_cache->miss_cnt = 0;
	new_cache->pf_cnt = 0;
	new_cache->pf_hit = 0;

	new_cache->_mshr = init_mem_queue_structure(size_mshr);
	new_cache->_txQ  = init_mem_queue_structure(size_txQ);
	new_cache->_pfQ  = init_mem_queue_structure(size_pfQ);

	new_cache->_rob = _rob;
	new_cache->_core = _core;
	new_cache->_proc = _proc;

	new_cache->_slave = NULL;
	new_cache->_master = NULL;

	new_cache->n_cores = n_cores;
	new_cache->_masters = (cache_t**)malloc(sizeof(cache_t*) * n_cores);
	new_cache->cache_level = (is_first)?CACHE_L1:((is_last)?CACHE_L3:CACHE_L2);

	new_cache->pref_timing_cnt = 0;

#ifdef L2_PREF_AVAIL
	if (!new_cache->is_first && !new_cache->is_last)
		init_l2_prefetcher(new_cache);
#endif 
	
	return new_cache;
}

bool
cache_check_hit(cache_t* _cache, uint64_t addr)
{
	uint64_t req_tag = (addr >> (uint64_t)(_cache->index_bit + _cache->offset_bit));
	uint64_t req_idx = (addr >> (uint64_t)(_cache->offset_bit)) % (uint64_t)_cache->n_set;
	for (uint64_t i=0; i<_cache->n_way; i++)
		if ((_cache->tag[i][req_idx].tag == req_tag) && _cache->tag[i][req_idx].valid)
			return true;
	return false;
}

bool
is_cache_busy(cache_t* _cache, uint64_t addr)
{
	//* @junggeun: 
	//* 63 ~  n  ~  3  2  1  0 (total 64 bits)
	//*  |... | bank | offset |
#if 0
	if (!_cache->has_banks)
		return (!(_cache->bank_busy[0] <= _cache->_core->_proc->global_clk));
	//TODO:: @junggeun: please fix this mapping scheme as re-configurable and reasonable
	uint32_t bank_addr = (addr >> u32_log2(CACHE_LINE_SIZE)) % _cache->n_banks;
	return (!(_cache->bank_busy[bank_addr] <= _cache->_core->_proc->global_clk));
#endif 
	uint32_t bank_addr = (addr >> u32_log2(CACHE_LINE_SIZE)) % _cache->n_banks;
	return (_cache->bank_busy[bank_addr] > _cache->_proc->global_clk);
}

bool
cache_access(cache_t* _cache, uint64_t addr, op_type type)
{
	//* as same as cache_check_hit
	//* & do_cache_replacement()
	uint64_t req_tag = (addr >> (uint64_t)(_cache->index_bit + _cache->offset_bit));
	uint64_t req_idx = (addr >> (uint64_t)(_cache->offset_bit)) % (uint64_t)_cache->n_set;
	
	_cache->access_cnt++;

	for (uint64_t i=0; i<_cache->n_way; i++)
	{
		if ((_cache->tag[i][req_idx].tag == req_tag) && _cache->tag[i][req_idx].valid)
		{
			//TODO: add refresh_replacement_info()
			_cache->hit_cnt++;
			return true;
		}
	}

	_cache->miss_cnt++;
	return false;
}

int is_there_victim(cache_t* _cache, uint64_t addr)
{
	//uint64_t req_tag = (addr >> (uint64_t)(_cache->index_bit + _cache->offset_bit));
	uint64_t req_idx = (addr >> (uint64_t)(_cache->offset_bit)) % (uint64_t)_cache->n_set;
	for (uint32_t w=0; w<_cache->n_way; w++)
		if (!(_cache->tag[w][req_idx].valid))
			return w;
	return -1;
}

bool cache_is_pf_hit(cache_t* _cache, uint64_t addr)
{
	uint64_t req_tag = (addr >> (uint64_t)(_cache->index_bit + _cache->offset_bit));
	uint64_t req_idx = (addr >> (uint64_t)(_cache->offset_bit)) % (uint64_t)_cache->n_set;

	for (uint64_t i=0; i<_cache->n_way; i++)
	{
		if ((_cache->tag[i][req_idx].tag == req_tag) && _cache->tag[i][req_idx].valid)
		{
			if ( _cache->tag[i][req_idx].pref )
			{
				_cache->tag[i][req_idx].pref = false;
				return true;
			}
			else
				return false;
		}
	}

}

//* @junggeun: I assumed that this is a non-inclusive cache.
//*						 (1) if there is room for inserting the cache line.
void cache_insert_line(cache_t* _cache, uint64_t addr, op_type type, uint32_t _way, req_t* _req, bool _pf)
{
	uint64_t req_tag = (addr >> (uint64_t)(_cache->index_bit + _cache->offset_bit));
	uint64_t req_idx = (addr >> (uint64_t)(_cache->offset_bit)) % (uint64_t)_cache->n_set;
	
	_cache->tag[_way][req_idx].valid = true;
	_cache->tag[_way][req_idx].tag = req_tag;
	//_cache->tag[_way][req_idx].dirty = (type==MEM_ST)?true:false;
	_cache->tag[_way][req_idx].dirty = false;	//TODO: 11-26 (should be fixed as "MEM_LD" type.)

	_cache->tag[_way][req_idx].dist = 0;
	if (_pf)	_cache->tag[_way][req_idx].pref = true;
	else			_cache->tag[_way][req_idx].pref = false;
	_cache->tag[_way][req_idx].used = 0;
}

uint64_t cache_evict_addr(cache_t* _cache, uint64_t addr)
{
	uint64_t req_idx = (addr >> (uint64_t)(_cache->offset_bit)) % (uint64_t)_cache->n_set;
	uint64_t evict_addr = (_cache->tag[0][req_idx].tag) << (uint64_t)(_cache->index_bit + _cache->offset_bit);

	//* req_tag | req_idx | byte_offset
	evict_addr += (req_idx << _cache->offset_bit);

	return evict_addr;
}

//* @junggeun: (2) if ther is not enough room for inserting cache line -> do cache eviction!
//TODO: handle the dirty eviction case!
void cache_evict_line(cache_t* _cache, uint64_t addr, op_type type, req_t* _req)
{
	//uint64_t req_tag = (addr >> (uint64_t)(_cache->index_bit + _cache->offset_bit));
	uint64_t req_idx = (addr >> (uint64_t)(_cache->offset_bit)) % (uint64_t)_cache->n_set;
	for (uint32_t w=0; w<_cache->n_way-1; w++)
		memcpy(&_cache->tag[w][req_idx], &_cache->tag[w+1][req_idx], sizeof(tagstore_t));
	_cache->tag[_cache->n_way-1][req_idx].valid = false;
	_cache->tag[_cache->n_way-1][req_idx].tag = 0;
	//TODO: (NEED TO CHECK) dirty needs to be set as false or it just depends on what the type of current req?
	_cache->tag[_cache->n_way-1][req_idx].dirty = (type==MEM_ST)? true: false;	//TODO: from (HPM).

	_cache->tag[_cache->n_way-1][req_idx].dist = 0;
	_cache->tag[_cache->n_way-1][req_idx].pref = false;
	_cache->tag[_cache->n_way-1][req_idx].used = 0;
}

void connect_caches(cache_t* _slave, cache_t* _master)
{
	_slave->_master = _master;
	_master->_slave = _slave;
}

void connect_llc(cache_t* _llc, uint32_t n_cores, cache_t** _l2_caches)
{
	for (uint32_t n=0; n<n_cores; n++)
	{
		_llc->_masters[n] = _l2_caches[n];
		_l2_caches[n]->_slave = _llc;
	}
}

void cache_operate(cache_t* _cache, uint64_t _clk)
{
	cache_handle_access(_cache, _clk);
	
	//* deleting old pf reqs
	/*
	if (!is_mem_queue_empty(_cache->_pfQ) && !_cache->is_last)
	{
		req_t* it = _cache->_pfQ->_head;
		req_t* next;
		for (uint32_t i=0; i<_cache->_pfQ->n_req; i++)
		{
			it = _cache->_pfQ->_head;
			if (it==NULL)	break;
			if (it->comp_time + _cache->lat_data * 3 <= _clk)
			//if (it->comp_time + _cache->_master->lat_data * 2 <= _clk)//_cache->lat_data <= _clk)
				mem_queue_remove_request(_cache->_pfQ, it);
			else
				break;
		}
	}
	*/
	//*
	
	if (_cache->is_last)
	{
		//if (_cache->_pfQ->n_req && (_cache->_proc->_mem_ctrl->_readQ->n_req < 5))
		if (_cache->_pfQ->n_req)
			cache_handle_prefetch(_cache, _clk);
	}
	else
	{
		//TODO: fix it!
		//if (_cache->_pfQ->n_req && _cache->_slave->_txQ->n_req < 5 && _cache->_txQ->n_req < 5)
		//if (!is_mem_queue_full(_cache->_slave->_mshr) && !is_mem_queue_full(_cache->_slave->_txQ) && !is_mem_queue_full(_cache->_txQ) && _cache->_pfQ->n_req)// && _cache->_mshr->n_req < 9 )// && _cache->_slave->_txQ->n_req < 9 && _cache->_txQ->n_req < 9) //is_mem_queue_empty(_cache->_slave->_txQ))
		if (_cache->_pfQ->n_req && !is_mem_queue_full(_cache->_slave->_txQ) && !is_mem_queue_full(_cache->_txQ) && _cache->_txQ->n_req < 17)
			cache_handle_prefetch(_cache, _clk);
	}


	if (_cache->is_last)
	{
		if (!_cache->_proc->hybrid)
		{
			if (_cache->_proc->_mem_ctrl->_readQ->n_req < 5)
				_cache->pref_timing_cnt++;
		}
	}
	else
	{
		//if (_cache->_slave->_txQ->n_req < 5 && _cache->_txQ->n_req < 5)	// if (_cache->_txQ->n_req == 0)
		//if (!is_mem_queue_full(_cache->_slave->_mshr) && !is_mem_queue_full(_cache->_slave->_txQ) && !is_mem_queue_full(_cache->_txQ) && _cache->_mshr->n_req < 12 && _cache->_slave->_txQ->n_req < 5 && _cache->_txQ->n_req < 12)
		//if (_cache->_pfQ->n_req && !is_mem_queue_full(_cache->_slave->_mshr) && !is_mem_queue_full(_cache->_slave->_txQ) && !is_mem_queue_full(_cache->_txQ) && is_mem_queue_empty(_cache->_slave->_txQ))
		//if (_cache->_txQ->n_req == 0)
		if (_cache->_pfQ->n_req && !is_mem_queue_full(_cache->_slave->_txQ) && !is_mem_queue_full(_cache->_txQ) && _cache->_txQ->n_req < 17)
			_cache->pref_timing_cnt++;
	}
/* ChampSim's case:
	if (PQ.occupancy && (RQ.occupancy == 0))
	{
		handle_prefetch();
	}
*/
}

void cache_handle_access(cache_t* _cache, uint64_t _clk)
{
	uint32_t	iter	= _cache->_txQ->n_req;
	req_t*		it		= _cache->_txQ->_head;
	//uint32_t	b_off = u32_log2(_cache->n_banks); //? bank offset

	uint32_t	r_limit = (_cache->is_first)? 2: 1;
	uint32_t	w_limit = 1;

	//bool was_pf = false;

	for (uint32_t i=0; i<iter; i++)
	{
		uint64_t bank_addr = (it->phys_addr / CACHE_LINE_SIZE) % _cache->n_banks;
			
		if (it->comp_time <= _clk) //&& !(it->is_pf && it->pf_target == _cache->cache_level) )// && (_cache->bank_busy[bank_addr] <= _clk))
		{
			switch (it->req_state)
			{
				case PENDING:
				{
					if (!_cache->is_last)
					{
						if (is_mem_queue_full(_cache->_slave->_txQ))	break;
					}
					else if (_cache->is_last)
					{
						if (!_cache->_proc->hybrid)
						{
							if (it->req_type == MEM_LD)
							{
								if (is_req_queue_full(_cache->_proc->_mem_ctrl->_readQ))	break;
							}
							else
							{
								if (is_req_queue_full(_cache->_proc->_mem_ctrl->_writeQ))	break;
							}
						}
						else
						{
							if (h_is_req_queue_full(_cache->_proc->_hmem_ctrl->_txQ))	break;
						}						
					}
					if (is_mem_queue_full(_cache->_mshr))	break;

					if (_cache->is_perfect)
					{
						it->comp_time = _clk + _cache->lat_tag;
						it->req_state = HIT;
						break;	//return;
					}

					bool hit = cache_check_hit(_cache, it->phys_addr);
					cache_access(_cache, it->phys_addr, it->req_type);
					
					//TODO: parallelized-tag-access!
					if (hit)
					{
						it->req_state = HIT;
						it->is_hit		= true;
						//if (it->is_pf)	_cache->pf_hit++;
						if (cache_is_pf_hit(_cache, it->phys_addr))
						{
							_cache->pf_hit++;
						}

						it->comp_time = _clk;

#ifdef L1_PREF_AVAIL
						if (_cache->is_first)
							l1_cache_prefetcher(_cache, it, _clk);
#endif 
#ifdef L2_PREF_AVAIL
						if (!_cache->is_first && !_cache->is_last)
							l2_cache_prefetcher(_cache, it, _clk);
#endif 
#ifdef L3_PREF_AVAIL
						//if (_cache->is_last)
							llc_cache_prefetcher(_cache, it, _clk);
#endif

					}
					else
					{
						it->req_state = MISS;
						it->is_hit		= false;

						//TODO: not-parallelized-tag-access!
						it->comp_time = _clk + _cache->lat_data;

						if 			(_cache->is_first)	_cache->_core->interval_l1_miss_cnt++;
						else if (_cache->is_last) 	_cache->_proc->interval_mpki++;
						else												_cache->_core->interval_l2_miss_cnt++;

#ifdef L1_PREF_AVAIL
						if (_cache->is_first)
							l1_cache_prefetcher(_cache, it, _clk);
#endif 
#ifdef L2_PREF_AVAIL
						if (!_cache->is_first && !_cache->is_last)
							l2_cache_prefetcher(_cache, it, _clk);
#endif 
#ifdef L3_PREF_AVAIL
						if (_cache->is_last)
							llc_cache_prefetcher(_cache, it, _clk);
#endif

					}
					break; //return;
				}
				case MISS:
				{
					//* if cache is not a llc
					if (!_cache->is_last)
					{
						if (!is_mem_queue_full(_cache->_mshr) && !is_mem_queue_full(_cache->_slave->_txQ))
						{ //* check whether it is available to add new MSHR entry at this level's MSHR.
							bool mshr_hit = mshr_exist_entry(_cache, it->phys_addr);
							mem_queue_insert_request(_cache->_mshr, it);
							it->req_state = MSHR_WAIT;
							it->comp_time = _clk + BIG_LATENCY;
							if (!mshr_hit)
							{
								req_t new_req;
								memcpy(&new_req, it, sizeof(req_t));
								new_req.is_pf = false;
								//TODO: 11-21:: fix it!
								new_req.pf_target = (it->is_pf)?it->pf_target:0;
								new_req.comp_time = _clk;
								new_req.req_state = PENDING;
								new_req.is_hit = false;
								new_req.req_type = MEM_LD;
								new_req.core_id = it->core_id;
								mem_queue_insert_request(_cache->_slave->_txQ, &new_req);
							}
							break; //return;
						}
						else
							break;
					}
					else	//* LLC case!
					{
						//* <-- if LLC's mshr is full or Memotry ctrl's txQs are full:
						if (is_mem_queue_full(_cache->_mshr))	break;
						if (!_cache->_proc->hybrid)
						{
							if (is_req_queue_full(_cache->_proc->_mem_ctrl->_readQ))	break;
						}
						else
						{
							if (h_is_req_queue_full(_cache->_proc->_hmem_ctrl->_txQ))	break;
						}
						
						//* waiting for the room.. -->

						//* check mshr entries & add new mshr entry.
						bool mshr_hit = mshr_exist_entry(_cache, it->phys_addr);
						mem_queue_insert_request(_cache->_mshr, it);
						
						it->req_state = MSHR_WAIT;
						it->comp_time = _clk + BIG_LATENCY;
						
						if (!mshr_hit)
						{
							if (!_cache->_proc->hybrid)	//TODO: <hybrid_test_version>
							{
								mem_req_t new_mem_req;
								new_mem_req.core_id = it->core_id;
								new_mem_req.comp_time = _clk;
								new_mem_req.phys_addr = it->phys_addr;
								new_mem_req.req_type = it->req_type;
								new_mem_req._c_req = it;
								new_mem_req.req_served = false;
								new_mem_req.next_command = NOP;
								new_mem_req.row_buffer_miss = false;
								new_mem_req._next = NULL;
								new_mem_req.arrival_time = 0;
								new_mem_req.dispatch_time = _clk;
								new_mem_req.req_type = MEM_LD;
								//TODO: don't have to wait for write requests..
								mem_txq_insert_req(_cache->_proc->_mem_ctrl->_readQ, &new_mem_req);
							}
							else
							{
								//* Hybrid main memory section::
								hmem_req_t new_hmem_req;
								new_hmem_req.core_id = it->core_id;
								new_hmem_req.comp_time = _clk;
								new_hmem_req.phys_addr = it->phys_addr;
								new_hmem_req.req_type = it->req_type;
								new_hmem_req.state = HMEM_TAG;
								new_hmem_req.is_hit = false;
								new_hmem_req._c_req = it;
								new_hmem_req._next = NULL;
								new_hmem_req.arrival_time = 0;
								new_hmem_req.dispatch_time = _clk;
								new_hmem_req.req_type = MEM_LD;
								h_mem_txq_insert_req(_cache->_proc->_hmem_ctrl->_txQ, &new_hmem_req);
							}
						}
						break; //return;
					}
					break;
				}
				case HIT:
				{
					/*if (_cache->is_last)	//TODO: pipeline-cache (set L3 busy latency as 1 --> other reqs which is trying to access another bank @ the same moment can be issued.. --> multi-core programs' cases)
					{
						if (is_cache_busy(_cache, it->phys_addr))	break;
						_cache->bank_busy[bank_addr] = _clk + _cache->lat_data;
					}*/
					//TODO: is_hit condition? (10-15)
					//it->is_hit = true;	//? because of MSHR response req, remove this line.
					it->req_state = DONE;
					it->comp_time = _clk + _cache->lat_data;
					break; //return;
				}
				case MSHR_WAIT:
				{
					break;
				}
				case FILL_WAIT:	//TODO: check this one!
				{
					//if (is_cache_busy(_cache, it->phys_addr))	break;
					it->req_state = DONE;
					it->comp_time = _clk + 1;
					//_cache->bank_busy[bank_addr] = _clk + 1;
					break;
				}
				case DONE:
				{
//TODO: is_hit has un-accurate information about whether this request packet was hit by this level or miss by upper level cache!!!!!!
//TODO: IMPORTANT!!!!!!!!!!!!
					if (it->req_type == MEM_ST)	if (w_limit == 0)	break;
					if (it->req_type == MEM_LD)	if (r_limit == 0)	break;

					if (_cache->is_first)
					{
						int w = is_there_victim(_cache, it->phys_addr);
						uint64_t req_idx = (it->phys_addr >> (uint64_t)(_cache->offset_bit)) % (uint64_t)_cache->n_set;
						if (w<0 && _cache->tag[0][req_idx].dirty)
							if (is_mem_queue_full(_cache->_slave->_txQ))
								break;

						if (!(it->is_pf && it->pf_target == _cache->cache_level))
						{
							//* respond to ROB -> COMMIT ready.
							it->rob_ptr->comp_time = _clk;
							it->rob_ptr->state		 = COMMIT;
							//* :::::::::::::::::::::::::::::::
						}

						//TODO: limit 2 reads/cycle or 1 read/cycle @L1/Ln $s.
						if (!(it->is_pf && it->pf_target == _cache->cache_level))
						{
							if (it->req_type == MEM_ST)	w_limit--;
							else												r_limit--;
						}

						if (_cache->is_perfect)
						{
							mem_queue_remove_request(_cache->_txQ, it);
							break; //return;
						}

						if (!it->is_hit)
						{
							//* cache eviction::
							if (w<0 && _cache->tag[0][req_idx].dirty)
							{
								uint64_t evict_addr = cache_evict_addr(_cache, it->phys_addr);
								cache_evict_line(_cache, it->phys_addr, it->req_type, it);
								//cache_do_replacement(_cache, it->phys_addr, 0);
								req_t evic_req;
								evic_req.comp_time = _clk;
								evic_req.core_id = it->core_id;
								evic_req.is_hit = false;
								evic_req.phys_addr = evict_addr;
								//evic_req.phys_addr = it->phys_addr;
								evic_req.req_type = MEM_ST;
								evic_req.rob_ptr = it->rob_ptr;
								evic_req.req_state = PENDING;
								evic_req.core_id = it->core_id;
								evic_req.is_pf = false;
								evic_req.pf_target = 0;
								mem_queue_insert_request(_cache->_slave->_txQ, &evic_req);
								bool insert_pf = (it->is_pf) && (it->pf_target == _cache->cache_level);
								cache_insert_line(_cache, it->phys_addr, it->req_type, _cache->n_way-1, &evic_req, insert_pf);
							}
							else
							{
								if (w<0)
								{
									cache_evict_line(_cache, it->phys_addr, it->req_type, it);
									w = _cache->n_way-1;
								}
								bool insert_pf = (it->is_pf) && (it->pf_target == _cache->cache_level);
								cache_insert_line(_cache, it->phys_addr, it->req_type, w, it, insert_pf);
							}
						}
						else
						{
							//TODO: do replacement here, or @ do_access() time?
							if (w<0)	cache_do_replacement(_cache, it->phys_addr, cache_get_target_way(_cache, it->phys_addr));
							else			cache_do_replacement(_cache, it->phys_addr, w);
						}
						//* [3] remove txQ entry for this request
						mem_queue_remove_request(_cache->_txQ, it);
						break; //return;
					}
					else if (_cache->is_last)
					{
						int w = is_there_victim(_cache, it->phys_addr);
						uint64_t req_idx = (it->phys_addr >> (uint64_t)(_cache->offset_bit)) % (uint64_t)_cache->n_set;

						//* condition for whether keep this job pending or not.
						bool is_load_req = (it->req_type==MEM_LD);
						if (w<0 && _cache->tag[0][req_idx].dirty)
						{
							if (!_cache->_proc->hybrid)
							{
								if (is_load_req)
								{
									if (is_req_queue_full(_cache->_proc->_mem_ctrl->_readQ))
										break;
								}
								else
								{
									if (is_req_queue_full(_cache->_proc->_mem_ctrl->_writeQ))
										break;
								}
							}
							else
							{
								if (h_is_req_queue_full(_cache->_proc->_hmem_ctrl->_txQ))
									break;
							}							
						}

						//TODO: limit 2 reads/cycle or 1 read/cycle @L1/Ln $s.
						if (!(it->is_pf && it->pf_target == _cache->cache_level) )
						{
							if (it->req_type == MEM_ST)	w_limit--;
							else												r_limit--;
						}

						cache_t* _mcache = _cache->_proc->core[it->core_id]->_l2;
						req_t*	 p_req = _mcache->_txQ->_head;
						uint32_t n_req = _mcache->_txQ->n_req;

						if (!_cache->is_perfect)
						{
							if (!it->is_hit)
							{
								//* [1] eviction and insertion for **this** level cache
								if (w<0 && _cache->tag[0][req_idx].dirty)
								{
									uint64_t evict_addr = cache_evict_addr(_cache, it->phys_addr);
									cache_evict_line(_cache, it->phys_addr, it->req_type, it);
									//* evicted requests goes to lower level cache's txQ!
									if (!_cache->_proc->hybrid)
									{
										mem_req_t new_mem_req;
										new_mem_req.core_id = it->core_id;
										new_mem_req.comp_time = _clk;
										//new_mem_req.phys_addr = it->phys_addr;
										new_mem_req.phys_addr = evict_addr;
										new_mem_req.req_type = it->req_type;	//TODO: should be a 'MEM_ST' instr?
										new_mem_req._c_req = NULL;	//TODO: may cause a problem..
										new_mem_req.req_served = false;
										new_mem_req.next_command = NOP;
										new_mem_req.row_buffer_miss = true;
										new_mem_req._next = NULL;
										new_mem_req.arrival_time = 0;
										new_mem_req.dispatch_time = _clk;
										mem_txq_insert_req(_cache->_proc->_mem_ctrl->_writeQ, &new_mem_req);
									}
									else
									{
										hmem_req_t new_hmem_req;
										new_hmem_req.core_id = it->core_id;
										new_hmem_req.comp_time = _clk;
										//new_hmem_req.phys_addr = it->phys_addr;
										new_hmem_req.phys_addr = evict_addr;
										new_hmem_req.state = HMEM_TAG;
										new_hmem_req.req_type = MEM_ST;	//TODO
										new_hmem_req.is_hit = false;
										new_hmem_req._c_req = NULL;	//TODO: ????
										new_hmem_req._next = NULL;
										new_hmem_req.arrival_time = 0;
										new_hmem_req.dispatch_time = _clk;
										h_mem_txq_insert_req(_cache->_proc->_hmem_ctrl->_txQ, &new_hmem_req);
									}

									bool insert_pf = (it->is_pf) && (it->pf_target == _cache->cache_level);
									cache_insert_line(_cache, it->phys_addr, it->req_type, _cache->n_way-1, it, insert_pf);
								}
								else
								{
									if (w<0)
									{
										cache_evict_line(_cache, it->phys_addr, it->req_type, it);
										w = _cache->n_way - 1;
									}
									bool insert_pf = (it->is_pf) && (it->pf_target == _cache->cache_level);
									cache_insert_line(_cache, it->phys_addr, it->req_type, w, it, insert_pf);
								}
								//* llc specific part:
								//*	[3-1] remove mshr(llc) entry for this request
								mshr_remove_entry(_cache, it->phys_addr, it->req_type, _clk);
							}
							else
							{
								if (w<0)
									cache_do_replacement(_cache, it->phys_addr, cache_get_target_way(_cache, it->phys_addr));
								else
									cache_do_replacement(_cache, it->phys_addr, w);
							}
						}

						//* [2] respond to the higher level cache (MSHR free)
						for (uint32_t i=0; i<n_req; i++)
						{
							if ((p_req->req_state == MSHR_WAIT) && ((uint64_t)(it->phys_addr / (uint64_t)CACHE_LINE_SIZE) == 
									(uint64_t)(p_req->phys_addr / (uint64_t)CACHE_LINE_SIZE)))
							{
								//p_req->comp_time = _clk;				//TODO: How can we **treat** cache fill operation??
								//p_req->req_state = FILL_WAIT;		//TODO: it is the most important thing in this stage.
								p_req->comp_time = _clk;
								p_req->req_state = DONE;
							}
							p_req = p_req->_next;
						}
						mshr_remove_entry(_mcache, it->phys_addr, it->req_type, _clk);
						
						mem_queue_remove_request(_cache->_txQ, it);

						break; //return;
					}
					else
					{
						int w = is_there_victim(_cache, it->phys_addr);
						uint64_t req_idx = (it->phys_addr >> (uint64_t)(_cache->offset_bit)) % (uint64_t)_cache->n_set;
						if (w<0 && _cache->tag[0][req_idx].dirty)
							if (is_mem_queue_full(_cache->_slave->_txQ))
								break;

						//TODO: limit 2 reads/cycle or 1 read/cycle @L1/Ln $s.
						if (!(it->is_pf && it->pf_target == _cache->cache_level))
						{
							if (it->req_type == MEM_ST)	w_limit--;
							else												r_limit--;
						}

						req_t*	 p_req = _cache->_master->_txQ->_head;
						uint32_t n_req = _cache->_master->_txQ->n_req;

						if (!_cache->is_perfect)
						{

							if (!it->is_hit)
							{
								//* [1] eviction and insertion for **this** level cache
								if (_cache->tag[0][req_idx].dirty && w<0)
								{
									uint64_t evict_addr = cache_evict_addr(_cache, it->phys_addr);
									cache_evict_line(_cache, it->phys_addr, it->req_type, it);
									//* evicted request goes to lower level cache's transaction queue!
									req_t evic_req;
									evic_req.comp_time = _clk;
									evic_req.comp_time = it->core_id;
									evic_req.is_hit = false;
									evic_req.phys_addr = evict_addr;
									//evic_req.phys_addr = it->phys_addr;
									evic_req.req_type = MEM_ST;
									evic_req.rob_ptr = it->rob_ptr;
									evic_req.req_state = PENDING;
									evic_req.core_id = it->core_id;
									evic_req.is_pf = false;
									evic_req.pf_target = 0;
									mem_queue_insert_request(_cache->_slave->_txQ, &evic_req);
									bool insert_pf = (it->is_pf) && (it->pf_target == _cache->cache_level);
									cache_insert_line(_cache, it->phys_addr, it->req_type, _cache->n_way-1, it, insert_pf);
								}
								else
								{
									if (w<0)
									{
										cache_evict_line(_cache, it->phys_addr, it->req_type, it);
										w = _cache->n_way - 1;
									}
									bool insert_pf = (it->is_pf) && (it->pf_target == _cache->cache_level);
									cache_insert_line(_cache, it->phys_addr, it->req_type, w, it, insert_pf);
								}
							}
							else
							{
								if (w<0)
									cache_do_replacement(_cache, it->phys_addr, cache_get_target_way(_cache, it->phys_addr));
								else
									cache_do_replacement(_cache, it->phys_addr, w);
							}


						}	//* perfect condition

						//* [2] respond to the higher level cache (MSHR free)
						for (uint32_t i=0; i<n_req; i++)
						{
							if ((p_req->req_state == MSHR_WAIT) && ((uint64_t)(it->phys_addr / (uint64_t)CACHE_LINE_SIZE) == 
									(uint64_t)(p_req->phys_addr / (uint64_t)CACHE_LINE_SIZE)))
							{
								//p_req->comp_time = _clk;				//TODO: How can we **treat** cache fill operation??
								//p_req->req_state = FILL_WAIT;		//TODO: it is the most important thing in this stage.
								p_req->comp_time = _clk;
								p_req->req_state = DONE;
							}
							p_req = p_req->_next;
						}
						mshr_remove_entry(_cache->_master, it->phys_addr, it->req_type, _clk);
						//* [3] remove txQ entry for this request
						mem_queue_remove_request(_cache->_txQ, it);

						break; //return;
					}
					break;
				}

			} //* end of the switch statement.
		} //* if the request is ready to be processed. [check the comp_time]
		it = it->_next;

		if (w_limit == 0 && r_limit == 0)	break;	//* L1 read queue is processed at a maximum rate of 2 reads/cycle, and L2&L3 read queues are processed at a maximum rate of 1 read/cycle.

	} //* iteration for each request in transaction queues.

}


uint32_t cache_get_target_way(cache_t* _cache, uint64_t _addr)
{
	uint64_t tag = (_addr >> (uint64_t)(_cache->index_bit + _cache->offset_bit));
	uint64_t idx = (_addr >> (uint64_t)(_cache->offset_bit)) % (uint64_t)_cache->n_set;
	for (uint32_t w=0; w < _cache->n_way; w++)
		if (_cache->tag[w][idx].tag == tag && _cache->tag[w][idx].valid)
			return w;
}

void cache_do_replacement(cache_t* _cache, uint64_t _addr, uint32_t _way)
{
	switch(_cache->repl_type)
	{
		case FIFO:	cache_FIFO_replacement(_cache, _addr, _way);	break;
		case LRU:		cache_LRU_replacement(_cache, _addr, _way);		break;
		case RAND:	cache_RAND_replacement(_cache, _addr, _way);	break;
	}
}

void cache_FIFO_replacement(cache_t* _cache, uint64_t _addr, uint32_t _way)
{
	//* do nothing.
}

void cache_LRU_replacement(cache_t* _cache, uint64_t _addr, uint32_t _way)	//TODO:
{
	uint64_t tag = (_addr >> (uint64_t)(_cache->index_bit + _cache->offset_bit));
	uint64_t idx = (_addr >> (uint64_t)(_cache->offset_bit)) % (uint64_t)_cache->n_set;
	for (uint32_t i=_way; i<_cache->n_way-1; i++)
	{
		memcpy(&_cache->tag[i][idx], &_cache->tag[i+1][idx], sizeof(tagstore_t));
		if (!_cache->tag[i+1][idx].valid)
			break;
	}
}

void cache_RAND_replacement(cache_t* _cache, uint64_t _addr, uint32_t _way)
{

}

void cache_handle_prefetch(cache_t* _cache, uint64_t _clk)
{

	req_t* it = _cache->_pfQ->_head;
	req_t* next;
	for (uint32_t i=0; i<_cache->_pfQ->n_req; i++)
	{
		next = it->_next;
		req_t* tx = _cache->_slave->_txQ->_head;
		for (uint32_t j=0; j<_cache->_slave->_txQ->n_req; ++j)
		{
			if ( (uint64_t)(it->phys_addr/CACHE_LINE_SIZE) == (uint64_t)(tx->phys_addr/CACHE_LINE_SIZE))
			{
				mem_queue_remove_request(_cache->_pfQ, it);
				break;
			}
			tx = tx->_next;
		}
		it = next;
		if (it==NULL)	break;
	}

	if (is_mem_queue_empty(_cache->_pfQ))	return;
	

	if (_cache->is_last)
	{
		if (!_cache->_proc->hybrid)
			if (is_req_queue_full(_cache->_proc->_mem_ctrl->_readQ))	return;
	}
	else
		if (is_mem_queue_full(_cache->_slave->_txQ))	return;

	
	//* [1] insert pf request into this level cache
	//* if not exist @ next level:
	//if (!_cache->_pfQ->_head->is_hit)
	//	mem_queue_insert_pfreq(_cache->_txQ, _cache->_pfQ->_head, 0);// _cache->_txQ->n_req);
	//else
	
	//mem_queue_insert_request(_cache->_txQ, _cache->_pfQ->_head);
	//mem_queue_insert_pfreq(_cache->_slave->_txQ, _cache->_pfQ->_head, 0);
	if (_cache->is_last)
	{
		if (is_mem_queue_full(_cache->_mshr) || is_mem_queue_full(_cache->_txQ))	return;
		req_t new_req;
		new_req.comp_time = _clk;
		new_req.core_id = _cache->_core->core_id;
		new_req.req_state = MSHR_WAIT;
		new_req.is_hit = false;
		new_req.is_pf = true;
		new_req.pf_target = _cache->cache_level;
		new_req.phys_addr = _cache->_pfQ->_head->phys_addr;
		new_req.req_type = MEM_LD;
		new_req.rob_ptr = NULL;
		mem_queue_insert_request(_cache->_txQ, &new_req);
		bool mshr_hit = mshr_exist_entry(_cache, new_req.phys_addr);
		mem_queue_insert_request(_cache->_mshr, &new_req);
		if (!mshr_hit)
		{
			mem_req_t	req;
			req.core_id = _cache->_pfQ->_head->core_id;
			req.comp_time = _clk;
			req.phys_addr = _cache->_pfQ->_head->phys_addr;
			req.req_type = _cache->_pfQ->_head->req_type;	//TODO: should be a 'MEM_ST' instr?
			req._c_req = &new_req;	//TODO: may cause a problem..
			req.req_served = false;
			req.next_command = NOP;
			req.row_buffer_miss = true;
			req._next = NULL;
			req.arrival_time = 0;
			req.dispatch_time = _clk;
			mem_txq_insert_req(_cache->_proc->_mem_ctrl->_readQ, &req);		
		}
	}
	else
	{
		if (is_mem_queue_full(_cache->_mshr) || is_mem_queue_full(_cache->_txQ))	return;
		req_t new_req;
		new_req.comp_time = _clk;
		new_req.core_id = _cache->_core->core_id;
		new_req.req_state = MSHR_WAIT;
		new_req.is_hit = false;
		new_req.is_pf = true;
		new_req.pf_target = _cache->cache_level;
		new_req.phys_addr = _cache->_pfQ->_head->phys_addr;
		new_req.req_type = MEM_LD;
		new_req.rob_ptr = NULL;
		mem_queue_insert_request(_cache->_txQ, &new_req);
		bool mshr_hit = mshr_exist_entry(_cache, _cache->_pfQ->_head->phys_addr);
		mem_queue_insert_request(_cache->_mshr, &new_req);
		if (!mshr_hit)
		{
			new_req.req_state = PENDING;
			mem_queue_insert_request(_cache->_slave->_txQ, &new_req);
		}
	}
	_cache->pf_cnt++;

	//* [2] delete pf request from pfQ
	mem_queue_remove_request(_cache->_pfQ, _cache->_pfQ->_head);
}

void mem_queue_insert_pfreq(mem_queue_t* _queue, req_t* _req, uint32_t idx)
{
	if (_queue->n_req == 0)
	{
		mem_queue_insert_request(_queue, _req);
		return;
	}
	if (idx > _queue->n_req)
	{
		idx = _queue->n_req;
	}
	req_t* cur_req = _queue->_head;
	req_t* new_req = (req_t*)malloc(sizeof(req_t));
	memcpy(new_req, _req, sizeof(req_t));
	new_req->_next = NULL;
	if (idx>0)
		for (uint32_t i=0; i<idx-1; i++)	cur_req = cur_req->_next;
	if (idx == 0)
	{
		new_req->_next = cur_req;
		_queue->_head = new_req;
		_queue->n_req++;
	}
	else if (idx == _queue->n_req)
	{
		mem_queue_insert_request(_queue, new_req);
		free(new_req);
	}
	else
	{
		req_t* next = cur_req->_next;
		cur_req->_next = new_req;
		new_req->_next = next;
		_queue->n_req++;
	}
	return;
}


int32_t
cache_get_pref_bit(cache_t* _cache, uint64_t _addr)
{

}

void
cache_set_pref_bit(cache_t* _cache, uint64_t _addr)
{

}

void
cache_unset_pref_bit(cache_t* _cache, uint64_t _addr)
{

}

int	 cache_get_level(cache_t* _cache)
{
	if (_cache->is_first)			return CACHE_L1;
	else if (_cache->is_last)	return CACHE_L3;
	else											return CACHE_L2;
}

