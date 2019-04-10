#include <string.h>
#include "mtsim.h"
#include "cache.h"

//#ifdef NEXT_LINE_PREF_L1
void l1_cache_prefetcher(cache_t* _l1, req_t* _req, uint64_t _clk)
{
	//if (is_mem_queue_full(_l1->_pfQ))	return;
	cache_t* _l2 = _l1->_slave;
	//* Next line prefetcher
	
	//* [1] construct the prefetching request
	req_t pf_req;
	//? pick the next line
	memcpy(&pf_req, _req, sizeof(req_t));
	pf_req.phys_addr = ((_req->phys_addr >> u64_log2(CACHE_LINE_SIZE)) + 1) << u64_log2(CACHE_LINE_SIZE);
	pf_req.is_pf = true;
	pf_req.req_state = PENDING;
	pf_req._next = NULL;
	pf_req.comp_time = _clk;
	pf_req.pf_target = CACHE_L1;
	pf_req.core_id = _l1->_core->core_id;
	pf_req.req_type = MEM_LD;

	//* [2] check whether there is the same demand request address in txQ
	bool already_exist = false;
	req_t* it = _l1->_txQ->_head;
	for (uint32_t idx=0; idx < _l1->_txQ->n_req; ++idx)
	{
		if ( (it->phys_addr/(uint64_t)CACHE_LINE_SIZE) == (pf_req.phys_addr/(uint64_t)CACHE_LINE_SIZE) )
		{
			already_exist = true;
			break;
		}
		it = it->_next;
	}
	if (cache_check_hit(_l1, pf_req.phys_addr)) already_exist = true;

/*
	it = _l2->_txQ->_head;
	for (uint32_t idx=0; idx < _l2->_txQ->n_req; ++idx)
	{
		if ( (it->phys_addr/(uint64_t)CACHE_LINE_SIZE) == (pf_req.phys_addr/(uint64_t)CACHE_LINE_SIZE) )
		{
			already_exist = true;
			break;
		}
		it = it->_next;
	}
*/

	if (already_exist)	return;	//? don't have to add prefetch req into pfQ
	
	if (is_mem_queue_full(_l1->_pfQ))
	{
		mem_queue_remove_request(_l1->_pfQ, _l1->_pfQ->_head);
	}

	//* [3] if not, try to add pfReq to pfQ
	//pf_req.is_hit = cache_check_hit(_l2, pf_req.phys_addr);
	mem_queue_insert_request(_l1->_pfQ, &pf_req);
}
