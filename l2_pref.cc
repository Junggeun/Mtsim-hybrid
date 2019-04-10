#include <string.h>
#include "mtsim.h"
#include "cache.h"

/*
#define PREFETCH_BUFFER_SIZE	64
#define L2_ACCESS_COUNT				1024

#define LATENCY_AVERAGES			32
#define LATENCY_AVERAGES_SHIFT	(unsigned short)log2(LATENCY_AVERAGES)
*/

#define AMPM_PAGE_COUNT 64
#define PREFETCH_DEGREE	2


typedef struct ampm_page
{
	unsigned long long int	page;						//? page address
	
	//? The access map itself.
	//?	Each element is set when the corresponding cache lin is accessed.
	//? The whole structure is analyzed to make prefetching decisions.
	//? While this is coded as an integer array, it is used conceptually as a single 64-bit vector.
	int											access_map[64];
	
	//? This map represents cache lines in this page that have already been prefetched.
	//? We will only prefetch lines that haven't already been either demand accessed or prefetched.
	int 										pf_map[64];

	//? used for page replacement
	unsigned long long int	lru;
} ampm_page_t;

ampm_page_t	ampm_pages[AMPM_PAGE_COUNT];

void init_l2_prefetcher(cache_t* _l2)
{
	//printf("AMPM Lite Prefetcher\n");
#if 0
	int i;
	for (i=0; i<AMPM_PAGE_COUNT; ++i)
	{
		ampm_pages[i].page = 0;
		ampm_pages[i].lru = 0;
		int j;
		for (j=0; j<64; ++j)
		{
			ampm_pages[i].access_map[j] = 0;
			ampm_pages[i].pf_map[j] = 0;
		}
	}
#endif

	

}

#if 0
void l2_cache_prefetcher(cache_t* _l2, req_t* _req, uint64_t _clk)
{
	cache_t* _l3 = _l2->_slave;
	//* AMPM prefetcher:: lite ampm prefetcher

	//* [1] construct the prefetching request
	req_t pf_req;
	//? pick the next line
	memcpy(&pf_req, _req, sizeof(req_t));
	pf_req.is_pf = true;
	pf_req.req_state = PENDING;
	pf_req._next = NULL;
	pf_req.comp_time = _clk;
	pf_req.core_id = _l2->_core->core_id;
	pf_req.req_type = MEM_LD;
	pf_req.pf_target = CACHE_L2;
	
	//* [2] AMPM logics:: generating prefetching address.....

	unsigned long long int cl_address 	= _req->phys_addr >> 6;
	unsigned long long int page					= cl_address >> 6;
	unsigned long long int page_offset	=	cl_address & 63;

	//* check to see if we have a page hit
	int page_index = -1;
	int i;
	for (i=0; i<AMPM_PAGE_COUNT; ++i)
	{
		if (ampm_pages[i].page == page)
		{
			page_index = i;
			break;
		}
	}

	if (page_index == -1)
	{
		//* the page was not found, so we must replace an old page with this new page
		//* find the oldest page
		int lru_index = 0;
		unsigned long long int lru_cycle = ampm_pages[lru_index].lru;
		int i;
		for (i=0; i<AMPM_PAGE_COUNT; ++i)
		{
			if (ampm_pages[i].lru < lru_cycle)
			{
				lru_index = i;
				lru_cycle = ampm_pages[lru_index].lru;
			}
		}
		page_index = lru_index;

		//* reset the oldest page
		ampm_pages[page_index].page = page;
		for (i=0; i<64; ++i)
		{
			ampm_pages[page_index].access_map[i] = 0;
			ampm_pages[page_index].pf_map[i] = 0;
		}
	}

	//* update LRU
	ampm_pages[page_index].lru = _clk;
	
	//* mark the access map
	ampm_pages[page_index].access_map[page_offset] = 1;

	//* positive prefetching
	int count_prefetches = 0;
	for (i=1; i<=16; i++)
	{
		int check_index1 = page_offset - i;
		int check_index2 = page_offset - 2*i;
		int pf_index		 = page_offset + i;

		if (check_index2 < 0)											break;
		if (pf_index > 63)												break;
		if (count_prefetches >= PREFETCH_DEGREE)	break;
		
		//* don't prefetch something that's already been demand accessed.
		if (ampm_pages[page_index].access_map[pf_index] == 1)	continue;
		
		//* don't prefetch something that's already been prefetched.
		if (ampm_pages[page_index].pf_map[pf_index] == 1)			continue;

		if ((ampm_pages[page_index].access_map[check_index1]==1) &&
				(ampm_pages[page_index].access_map[check_index2]==1))
		{
			//* we found the stride repeated twice, so issue a prefetch
			unsigned long long int pf_address = (page << 12) + (pf_index << 6);
			pf_req.phys_addr = pf_address;
			//if (is_mem_queue_full(_l2->_pfQ)) return;
			bool already_exist = false;
			req_t* it = _l2->_txQ->_head;
			for (uint32_t idx=0; idx<_l2->_txQ->n_req; ++idx)
			{
				if ((it->phys_addr/(uint64_t)CACHE_LINE_SIZE) == (pf_req.phys_addr/(uint64_t)CACHE_LINE_SIZE))
				{
					already_exist = true;
					break;
				}
				it = it->_next;
			}
			if (cache_check_hit(_l2, pf_req.phys_addr)) already_exist = true;
			if (already_exist) continue;
			if (is_mem_queue_full(_l2->_pfQ))
			{
				mem_queue_remove_request(_l2->_pfQ, _l2->_pfQ->_head);
			}
			mem_queue_insert_request(_l2->_pfQ, &pf_req);
		}

		//* mark the prefetched line so we don't prefetch it again..
		ampm_pages[page_index].pf_map[pf_index] = 1;
		count_prefetches++;

	}
}
#endif

#if 0
void l2_cache_prefetcher(cache_t* _l2, req_t* _req, uint64_t _clk)
{
	//if (is_mem_queue_full(_l2->_pfQ))	return;

	cache_t* _l3 = _l2->_slave;
	//* Next line prefetcher
	
	//* [1] construct the prefetching request
	req_t pf_req;
	//? pick the next line
	memcpy(&pf_req, _req, sizeof(req_t));
	pf_req.phys_addr = ( (_req->phys_addr >> u64_log2(CACHE_LINE_SIZE)) + 1) << u64_log2(CACHE_LINE_SIZE);
	pf_req.is_pf = true;
	pf_req.req_state = PENDING;
	pf_req._next = NULL;
	pf_req.comp_time = _clk;
	pf_req.core_id = _l2->_core->core_id;
	pf_req.req_type = MEM_LD;
	pf_req.pf_target = CACHE_L2;
	
	//* [2] check whether there is the same demand request address in txQ
	bool already_exist = false;
	req_t* it = _l2->_txQ->_head;
	for (uint32_t idx=0; idx < _l2->_txQ->n_req; ++idx)
	{
		if ( (it->phys_addr/(uint64_t)CACHE_LINE_SIZE) == (pf_req.phys_addr/(uint64_t)CACHE_LINE_SIZE) )
		{
			already_exist = true;
			break;
		}
		it = it->_next;
	}
	if (cache_check_hit(_l2, pf_req.phys_addr))	already_exist = true;

/*
	it = _l3->_txQ->_head;
	for (uint32_t idx=0; idx < _l3->_txQ->n_req; ++idx)
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

	if (is_mem_queue_full(_l2->_pfQ))
	{
		mem_queue_remove_request(_l2->_pfQ, _l2->_pfQ->_head);
	}	

	//* [3] if not, try to add pfReq to pfQ
	//pf_req.is_hit = cache_check_hit(_l3, pf_req.phys_addr);
	mem_queue_insert_request(_l2->_pfQ, &pf_req);
}
#endif