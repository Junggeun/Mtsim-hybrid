#include <string.h>
#include "mtsim.h"
#include "cache.h"
#include "llc_pref.h"
#include <malloc.h>

#include <map>
#include <string>

void init_llc_prefetcher()
{
	
}

void llc_cache_prefetcher(cache_t* _llc, req_t* _req, uint64_t _clk)
{

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
	pf_req.pf_target = CACHE_L3;
	pf_req.core_id = _req->core_id;
	pf_req.req_type = MEM_LD;
	pf_req.pf_target = CACHE_L3;

	//* [2] check whether there is the same demand request address in txQ
	bool already_exist = false;
	req_t* it = _llc->_txQ->_head;
	for (uint32_t idx=0; idx < _llc->_txQ->n_req; ++idx)
	{
		if ( (it->phys_addr/(uint64_t)CACHE_LINE_SIZE) == (pf_req.phys_addr/(uint64_t)CACHE_LINE_SIZE) )
		{
			already_exist = true;
			break;
		}
		it = it->_next;
	}
	if (cache_check_hit(_llc, pf_req.phys_addr))	already_exist = true;

	if (already_exist)	return;	//? don't have to add prefetch req into pfQ
	
	if (is_mem_queue_full(_llc->_pfQ))
	{
		mem_queue_remove_request(_llc->_pfQ, _llc->_pfQ->_head);
	}

	//* [3] if not, try to add pfReq to pfQ
	mem_queue_insert_request(_llc->_pfQ, &pf_req);

}
