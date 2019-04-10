#include "mtsim.h"
#include "hybrid_mem.h"
#include "cache.h"
#include <malloc.h>
#include <string.h>
#include <assert.h>

hm_queue_t*
h_init_memory_request_queue(hm_ctrl_t* _mem_ctrl, uint32_t capacity)
{
	hm_queue_t*	new_queue = (hm_queue_t*)malloc(sizeof(hm_queue_t));
	new_queue->capacity = capacity;
	new_queue->_mem_ctrl = _mem_ctrl;
	new_queue->n_req = 0;
	new_queue->_head = NULL;
	new_queue->_tail = NULL;
	return new_queue;
}

void
h_mem_txq_insert_req(hm_queue_t* _queue, hmem_req_t* _req)
{
	hmem_req_t* new_req = (hmem_req_t*)malloc(sizeof(hmem_req_t));
	memcpy(new_req, _req, sizeof(hmem_req_t));
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
h_mem_txq_remove_req(hm_queue_t* _queue, hmem_req_t* _req)
{
	hmem_req_t *it;
	hmem_req_t *prev, *next;

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
h_is_req_queue_full(hm_queue_t* _queue)
{
	return (_queue->capacity == _queue->n_req);
}

bool
h_is_req_queue_empty(hm_queue_t* _queue)
{
	return (_queue->n_req == 0);
}

hybrid_cache_t* init_hybrid_cache_structure(hm_ctrl_t* _ctrl, uint64_t n_size, uint32_t n_way, uint64_t n_set, uint32_t n_line, cache_map_t map_type, repl_t repl_type)
{
	hybrid_cache_t *new_hcache = (hybrid_cache_t*)malloc(sizeof(hybrid_cache_t));

	new_hcache->_ctrl = _ctrl;

	new_hcache->n_size = n_size;
	new_hcache->n_way = n_way;
	new_hcache->n_set = n_set;
	//new_hcache->n_line = n_line;
	new_hcache->n_line = CACHE_LINE_SIZE;	//TODO: what if 128/256-Byte?
	n_line = CACHE_LINE_SIZE;
	new_hcache->map_type = map_type;
	new_hcache->repl_type = repl_type;

	switch (map_type)
	{
		case directMapped:
			new_hcache->n_set = new_hcache->n_size / new_hcache->n_line;
			new_hcache->n_way = 1;
			break;
		case setAssociate:
			new_hcache->n_set = new_hcache->n_size / (uint64_t)new_hcache->n_way / (uint64_t)new_hcache->n_line;
			break;
		case fulAssociate:
			new_hcache->n_way = new_hcache->n_size / new_hcache->n_line;
			new_hcache->n_set = 1;
			break;
	}
	//printf("%llu, %llu, %llu\n", new_hcache->n_set, new_hcache->n_size, new_hcache->n_line);

	new_hcache->index_bit = u64_log2((uint64_t)(new_hcache->n_set));
	new_hcache->offset_bit = u64_log2((uint64_t)(new_hcache->n_line));

	new_hcache->tag = (tagstore**)malloc(sizeof(tagstore_t*) * new_hcache->n_way);
	for (uint32_t i=0; i<new_hcache->n_way; i++)
	{
		new_hcache->tag[i] = (tagstore*)malloc(sizeof(tagstore_t) * new_hcache->n_set);
		for (uint64_t j=0; j<new_hcache->n_set; j++)
		{
			new_hcache->tag[i][j].valid = false;
			new_hcache->tag[i][j].dirty = false;
			new_hcache->tag[i][j].tag = 0;
			new_hcache->tag[i][j].repl = 0;

			new_hcache->tag[i][j].pref = false;
			new_hcache->tag[i][j].used = 0;
			new_hcache->tag[i][j].dist = 0;
		}
	}

	new_hcache->hit_cnt = 0;
	new_hcache->miss_cnt = 0;
	new_hcache->pf_cnt = 0;
	new_hcache->pf_hit = 0;

	new_hcache->comp_time = 0;

	return new_hcache;
}

hm_ctrl_t* init_hybrid_memory_controller(uint32_t size_txQ, uint64_t hc_size, uint32_t hc_way, uint64_t hc_set, uint32_t hc_line, cache_map_t hc_type, repl_t hc_repl)
{
	hm_ctrl_t* new_hm_ctrl = (hm_ctrl_t*)malloc(sizeof(hm_ctrl_t));

	new_hm_ctrl->_hcache = init_hybrid_cache_structure(new_hm_ctrl, hc_size, hc_way, hc_set, hc_line, hc_type, hc_repl);

	new_hm_ctrl->hc_size = hc_size;
	new_hm_ctrl->hc_way = hc_way;
	new_hm_ctrl->hc_set = hc_set;
	new_hm_ctrl->hc_line = hc_line;
	new_hm_ctrl->hc_type = hc_type;
	new_hm_ctrl->hc_repl = hc_repl;

	new_hm_ctrl->size_txQ = size_txQ;

	new_hm_ctrl->_txQ = h_init_memory_request_queue(new_hm_ctrl, size_txQ);
	new_hm_ctrl->_hcache->_txQ = new_hm_ctrl->_txQ;

	new_hm_ctrl->total_hmem_req_cnt = 0;
	new_hm_ctrl->interval_bandwidth = 0;
	new_hm_ctrl->total_bandwidth = 0;

	return new_hm_ctrl;
}

void hybrid_memory_operate(hm_ctrl_t* hm_ctrl, uint64_t _clk)
{
	hcache_handle_access(hm_ctrl->_hcache, _clk);
	memory_operate(hm_ctrl->_dram_ctrl, _clk);
	memory_operate(hm_ctrl->_nvm_ctrl, _clk);
	
	//* delete complete requests
	hmem_req_t* it = hm_ctrl->_txQ->_head;
	hmem_req_t* next;
	for (uint32_t i=0; i<hm_ctrl->_txQ->n_req; i++)
	{
		next = it->_next;
		if (it->comp_time <= _clk && it->state == HMEM_COMMIT)
		{
			h_mem_txq_remove_req(hm_ctrl->_txQ, it);
		}
		it = next;
	}
	//* -----> clean_queues;

/*
	it = hm_ctrl->_hcache->_txQ->_head;
	for (uint32_t i=0; i<hm_ctrl->_txQ->n_req; i++)
	{
		printf("{%llu} ", _clk);
		printf("[%d]:%u:%llu:%llu\n", i, it->state, it->comp_time, it->phys_addr);
		it = it->_next;
	}
	printf("\n");
*/

}

uint64_t hybrid_address_translate(hybrid_cache_t* _hcache, uint64_t addr)
{
	uint64_t cache_addr = 0;
	uint64_t req_way;
	uint64_t req_tag = (addr >> (uint64_t)(_hcache->index_bit + _hcache->offset_bit));
	uint64_t req_idx = (addr >> (uint64_t)(_hcache->offset_bit)) % (uint64_t)_hcache->n_set;
	bool hit = false;
	for (req_way=0; req_way<_hcache->n_way; req_way++)
	{
		if ((_hcache->tag[req_way][req_idx].tag == req_tag) && _hcache->tag[req_way][req_idx].valid)
		{
			hit = true;
			break;
		}
	}

	if (!hit)	req_way = _hcache->n_way-1;

	//* req_way | req_idx | byte_offset (determined by cache_line_size) --> DRAM$ address
	cache_addr += (req_idx << _hcache->offset_bit);
	cache_addr += (req_way << (_hcache->offset_bit + _hcache->index_bit));

	return cache_addr;
}

void hcache_handle_access(hybrid_cache_t* _hcache, uint64_t _clk)
{
	uint32_t		iter	= _hcache->_txQ->n_req;
	hmem_req_t*	it		= _hcache->_txQ->_head;

	//? on-chip caches: r_limit and w_limit exist.
	//? off-chip heterogeneous cache tag: just 1 req per 1 cycle.
	for (uint32_t i=0; i<iter; i++)
	{
		if (it->comp_time <= _clk)
		{
			switch(it->state)	//TODO 22:20 / 03.26
			{
				//case HMEM_IDLE:
				case HMEM_TAG:
				{
					if (h_is_req_queue_full(_hcache->_txQ))	break;
					//TODO: (exception) rd/wrQ full - dram/nvm
					bool hit = hcache_check_hit(_hcache, it->phys_addr);
					hcache_access(_hcache, it->phys_addr);
					//TODO: IMPORTANT!! pipelining or not?
					//TODO: (1) for pipelining --> must care about tagstore-busy case
					//TODO: (2) non-pipeline   --> spin-lock is needed..
					if (hit)
					{
						it->state = HMEM_HIT;
						it->comp_time = _clk + 20;	//TODO: fixed latency of tag-accessing.
					}
					else
					{
						it->state = HMEM_MISS;
						it->comp_time = _clk + 20;	//TODO: fixed latency of tag-accessing.
					}
					_hcache->comp_time = _clk + 20;
					return;	//TODO: r_limit/w_limit
					//break;
				}
				//TODO: MSHR for eDRAM(HMEM) $ ?
				case HMEM_HIT:
				{
					if (h_is_req_queue_full(_hcache->_txQ))	break;

					it->state = HMEM_DONE;
					//* cannot determine hit-latency @ this module --> it depends on DRAM module's status.
					it->comp_time = _clk + BIG_LATENCY;
					it->is_hit = true;

					//TODO: new_mem_req.phys_addr must be translated!
					mem_req_t new_mem_req;
					new_mem_req.core_id = it->core_id;
					new_mem_req.comp_time = _clk;
					//if (hybrid_address_translate(_hcache, it->phys_addr) > _hcache->n_size)	printf("PANIC: %llu\n", hybrid_address_translate(_hcache, it->phys_addr));
					new_mem_req.phys_addr = hybrid_address_translate(_hcache, it->phys_addr);
					new_mem_req.req_type = it->req_type;
					new_mem_req._c_req = NULL;
					new_mem_req.req_served = false;
					new_mem_req.next_command = NOP;
					new_mem_req.row_buffer_miss = true;
					new_mem_req._next = NULL;
					new_mem_req.arrival_time = 0;
					new_mem_req.dispatch_time = _clk;
					new_mem_req._h_req = it;
					new_mem_req.migration = false;
					
					if (it->req_type == MEM_LD)	mem_txq_insert_req(_hcache->_ctrl->_dram_ctrl->_readQ, &new_mem_req);
					else												mem_txq_insert_req(_hcache->_ctrl->_dram_ctrl->_writeQ, &new_mem_req);

					//TODO: do_replacement() for hcache?
					return;
					//break;
				}
				case HMEM_MISS:
				{
					if (h_is_req_queue_full(_hcache->_txQ))	break;

					it->state = HMEM_MIGRATION;
					it->comp_time = _clk + BIG_LATENCY;
					it->is_hit = false;

					//TODO: write miss case--> do we really need to load that address from memory? or just overwrite it?
					mem_req_t new_mem_req;
					new_mem_req.core_id = it->core_id;
					new_mem_req.comp_time = _clk;
					new_mem_req.phys_addr = it->phys_addr;
					new_mem_req.req_type = it->req_type;
					new_mem_req._c_req = NULL;
					new_mem_req.req_served = false;
					new_mem_req.next_command = NOP;
					new_mem_req.row_buffer_miss = true;
					new_mem_req._next = NULL;
					new_mem_req.arrival_time = 0;
					new_mem_req.dispatch_time = _clk;
					new_mem_req._h_req = it;
					new_mem_req.migration = false;

					if (it->req_type == MEM_LD)	mem_txq_insert_req(_hcache->_ctrl->_nvm_ctrl->_readQ, &new_mem_req);
					else												mem_txq_insert_req(_hcache->_ctrl->_nvm_ctrl->_writeQ, &new_mem_req);
					return;
					//break;
				}
				case HMEM_MIGRATION:
				{
					if (is_req_queue_full(_hcache->_ctrl->_dram_ctrl->_writeQ))	break;

					//TODO: VERY VERY IMPORTANT!!
					//TODO: how can treat an evicted data from DRAM $???
					it->state = HMEM_DONE;
					it->comp_time = _clk + BIG_LATENCY;
					it->_c_req->comp_time = _clk;
					it->_c_req->req_state = DONE;
					
					//TODO: do we need to write twice for dram & pcm when miss occurs?
					//TODO: void memory_schedule() @ memory.cc::
					mem_req_t new_mem_req;
					new_mem_req.core_id = it->core_id;
					new_mem_req.comp_time = _clk;
					//if (hybrid_address_translate(_hcache, it->phys_addr) > _hcache->n_size)	printf("PANIC2: %llu\n", hybrid_address_translate(_hcache, it->phys_addr));
					new_mem_req.phys_addr = hybrid_address_translate(_hcache, it->phys_addr);
					new_mem_req.req_type = MEM_ST;
					//new_mem_req._c_req = it->_c_req;
					new_mem_req.req_served = false;
					new_mem_req.next_command = NOP;
					new_mem_req.row_buffer_miss = true;
					new_mem_req._next = NULL;
					new_mem_req.arrival_time = 0;
					new_mem_req.dispatch_time = _clk;
					new_mem_req._h_req = it;
					new_mem_req.migration = true;
					mem_txq_insert_req(_hcache->_ctrl->_dram_ctrl->_writeQ, &new_mem_req);
					return;
					//break;
				}
				case HMEM_DONE:
				{
					if (!it->is_hit)
					{
						uint32_t _way;
						hcache_evict_line(_hcache, it->phys_addr, it->req_type);
						_way = hcache_find_victim(_hcache, it->phys_addr);
						hcache_insert_line(_hcache, it->phys_addr, it->req_type, _way);
					}

					if (it->_c_req != NULL)
					{
						it->_c_req->comp_time = _clk;
						it->_c_req->req_state = DONE;
					}
					it->state = HMEM_COMMIT;	//TODO:
					return;
					//break;
				}
			}
		}
		it = it->_next;
	}
}

bool hcache_check_hit(hybrid_cache_t* _hcache, uint64_t addr)
{
	uint64_t req_tag = (addr >> (uint64_t)(_hcache->index_bit + _hcache->offset_bit));
	uint64_t req_idx = (addr >> (uint64_t)(_hcache->offset_bit)) % (uint64_t)_hcache->n_set;
	for (uint64_t i=0; i<_hcache->n_way; i++)
		if ((_hcache->tag[i][req_idx].tag == req_tag) && _hcache->tag[i][req_idx].valid)
			return true;
	return false;
}

bool hcache_access(hybrid_cache_t* _hcache, uint64_t addr)
{
	uint64_t req_tag = (addr >> (uint64_t)(_hcache->index_bit + _hcache->offset_bit));
	uint64_t req_idx = (addr >> (uint64_t)(_hcache->offset_bit)) % (uint64_t)_hcache->n_set;
	for (uint64_t i=0; i<_hcache->n_way; i++)
	{
		if ((_hcache->tag[i][req_idx].tag == req_tag) && _hcache->tag[i][req_idx].valid)
		{
			_hcache->hit_cnt++;
			return true;
		}
	}
	_hcache->miss_cnt++;
	return false;
}

int hcache_find_victim(hybrid_cache_t* _hcache, uint64_t addr)
{
	uint64_t req_idx = (addr >> (uint64_t)(_hcache->offset_bit)) % (uint64_t)_hcache->n_set;
	for (uint32_t w=0; w<_hcache->n_way; w++)
	{
		if (!(_hcache->tag[w][req_idx].valid))
			return w;
	}
	return -1;
}

uint32_t hcache_get_target_way(hybrid_cache_t* _hcache, uint64_t addr)
{
	uint64_t tag = (addr >> (uint64_t)(_hcache->index_bit + _hcache->offset_bit));
	uint64_t idx = (addr >> (uint64_t)(_hcache->offset_bit)) % (uint64_t)_hcache->n_set;
	for (uint32_t w=0; w < _hcache->n_way; w++)
	{
		if (_hcache->tag[w][idx].tag == tag && _hcache->tag[w][idx].valid)
			return w;
	}
	return UINT32_MAX;
}

void hcache_insert_line(hybrid_cache_t* _hcache, uint64_t addr, op_type type, uint32_t _way)
{
	uint64_t req_tag = (addr >> (uint64_t)(_hcache->index_bit + _hcache->offset_bit));
	uint64_t req_idx = (addr >> (uint64_t)(_hcache->offset_bit)) % (uint64_t)_hcache->n_set;

	_hcache->tag[_way][req_idx].valid = true;
	_hcache->tag[_way][req_idx].tag = req_tag;
	_hcache->tag[_way][req_idx].dirty = false;	//TODO:
	//TODO: are these member-variables useful for hetero/hybrid memory cache too?
	_hcache->tag[_way][req_idx].dist = 0;
	_hcache->tag[_way][req_idx].used = 0;
}

void hcache_evict_line(hybrid_cache_t* _hcache, uint64_t addr, op_type type)
{
	uint64_t req_idx = (addr >> (uint64_t)(_hcache->offset_bit)) % (uint64_t)_hcache->n_set;
	for (uint32_t w=0; w<_hcache->n_way-1; w++)
		memcpy(&_hcache->tag[w][req_idx], &_hcache->tag[w+1][req_idx], sizeof(tagstore_t));
	_hcache->tag[_hcache->n_way-1][req_idx].valid = false;
	_hcache->tag[_hcache->n_way-1][req_idx].tag = 0;
	_hcache->tag[_hcache->n_way-1][req_idx].dirty = false;
	_hcache->tag[_hcache->n_way-1][req_idx].dist = 0;
	_hcache->tag[_hcache->n_way-1][req_idx].pref = false;
	_hcache->tag[_hcache->n_way-1][req_idx].used = 0;
}

void hcache_do_replacement(hm_ctrl_t* hm_ctrl, uint64_t addr, uint32_t way)
{
	//TODO: LRU/RAND/etc.
}

