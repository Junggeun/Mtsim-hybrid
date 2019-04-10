#include "pin.H"
#include <stdio.h>
#include <malloc.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "core.h"
#include "mtsim.h"
#include "cache.h"
#include <iostream>
#include <map>

using namespace std;

#define MIN_NUM_EVENTS 256
#define PAGE_SIZE	4096

KNOB<string> KnobConfig(KNOB_MODE_WRITEONCE, "pintool", "c", "ooo.cfg", "specify configuration file");
KNOB<string> KnobAppname(KNOB_MODE_WRITEONCE, "pintool", "a", "a.out", "specify target application name");
KNOB<string> KnobNumInstr(KNOB_MODE_WRITEONCE, "pintool", "i", "0", "specify total number of instructions to run.");

uint64_t	proc_total_instr;
uint32_t	intv_cnt;
uint64_t	limit_instrs;

//*
//TODO: set these variables as simu_instance
uint32_t	n_cores;
uint32_t	proc_mem_freq_ratio;
proc_t*		CPU;

uint32_t	optype[MAX_THREAD_NUM], thread_id[MAX_THREAD_NUM], num_instrs[MAX_THREAD_NUM];
uint64_t	addr[MAX_THREAD_NUM];

uint64_t	max_phys_addr;

uint64_t	interval_inst;
uint64_t	interval_cnt;

struct timeval tv1, tv2;
struct timeval simu_start, simu_end;

simu_config_t	simu;

FILE*	config;
FILE*	ipc;
FILE*	mpki;
FILE* f_bandwidth;

op_t new_op;
uint32_t num_fetch, num_retire;
bool mem_fetch[MAX_THREAD_NUM];
bool trace_end[MAX_THREAD_NUM];
//*
class PageMapper
{
	public:
		virtual ~PageMapper() {}
		virtual uint64_t translate(uint64_t virtual_address) = 0;
		uint64_t getNumPhysicalPage() { return m_page_table.size(); }
	protected:
		std::map<uint64_t, uint64_t> m_page_table;
		uint32_t m_page_size;
		uint64_t m_physical_tag;
		PageMapper(uint32_t page_size, uint64_t physical_tag)
			: m_page_size(page_size), m_physical_tag(physical_tag) {}
};

class FCFSPageMapper : public PageMapper
{
	public:
		FCFSPageMapper(uint32_t page_size = PAGE_SIZE);
		~FCFSPageMapper();
	public:
		uint64_t translate(uint64_t virtual_address);
};

FCFSPageMapper::FCFSPageMapper(uint32_t page_size)
	: PageMapper(page_size, 0)
{
}

FCFSPageMapper::~FCFSPageMapper()
{
}

uint32_t maplog2(uint32_t n)
{
	uint32_t power; for (power=0; n>>=1; ++power);	return power;
}

uint64_t FCFSPageMapper::translate(uint64_t virtual_address)
{
	uint64_t vpn = virtual_address >> (int)maplog2(m_page_size);
	uint64_t offset = virtual_address & (m_page_size - 1);

	std::map<uint64_t, uint64_t>::iterator it;
	it = m_page_table.find(vpn);

	if (it == m_page_table.end())
	{
		uint64_t physical_address = (m_physical_tag << (int)maplog2(m_page_size)) | offset;
		m_page_table[vpn] = m_physical_tag++;
		return physical_address;
	}
	else
	{
		return (it->second << (int)maplog2(m_page_size)) | offset;
	}	
}
//*

FCFSPageMapper	*pMapper;

typedef struct simu_event simu_event_t;
struct simu_event
{
	uint64_t			addr;
	uint64_t			instrs;
	op_type				type;
	simu_event_t	*_next;
	simu_event_t	*_prev;
};

typedef struct simu_event_queue	simu_event_queue_t;
struct simu_event_queue
{
	simu_event_t	*_head;
	simu_event_t	*_tail;
	uint32_t			num_events;
	uint64_t			nonmem_instrs;
};


simu_event_queue_t	*eventQ;

simu_event_queue_t* init_simu_event_queue()
{
	simu_event_queue_t* new_event_queue = (simu_event_queue_t*)malloc(sizeof(simu_event_queue_t));
	new_event_queue->_head = NULL;
	new_event_queue->_tail = NULL;
	new_event_queue->num_events = 0;
	new_event_queue->nonmem_instrs = 0;
	
	return new_event_queue;
}

void enq_event(simu_event_queue_t* event_queue, uint64_t addr, op_type type)
{
	simu_event_t*	new_simu_event = (simu_event_t*)malloc(sizeof(simu_event_t));
	new_simu_event->addr = addr;
	new_simu_event->type = type;
	new_simu_event->instrs = event_queue->nonmem_instrs;
	event_queue->nonmem_instrs = 0;
	new_simu_event->_next = NULL;
	new_simu_event->_prev = NULL;
	if (event_queue->num_events == 0)
	{
		event_queue->_head = new_simu_event;
	}
	else
	{
		event_queue->_tail->_next = new_simu_event;
		new_simu_event->_prev = event_queue->_tail;
	}
	event_queue->_tail = new_simu_event;
	event_queue->num_events++;
}

void deq_event(simu_event_queue_t* event_queue, simu_event_t* getter)
{
	simu_event_t* evicted_event;
	evicted_event = event_queue->_head;
	getter->addr = evicted_event->addr;
	getter->type = evicted_event->type;
	getter->instrs = evicted_event->instrs;

	if (event_queue->num_events == 1)
	{
		free(evicted_event);
		event_queue->_head = NULL;
		event_queue->_tail = NULL;
		event_queue->num_events = 0;
	}
	else
	{
		event_queue->_head = event_queue->_head->_next;
		free(evicted_event);
		event_queue->num_events--;
	}
}

double divide(uint64_t a, uint64_t b)
{
	while (a>10000 && b>10000)
	{
		a /= 10;
		b /= 10;
	}
	return (double)a/(double)b;
}

void run_cycle()//bool simu_fin)
{
	simu_event_t 			current_instr;
	simu_event_queue*	instr_queue;

	instr_queue = eventQ;

	for (;;)
	{
		if (eventQ->num_events < MIN_NUM_EVENTS)	return;
		//* (2) update status of caches and memory subsystems >>>>>>>>>>>>>
		if (CPU->global_clk % proc_mem_freq_ratio == 0)
		{
			if (!CPU->hybrid)
				memory_operate(CPU->_mem_ctrl, CPU->global_clk);
			else
				hybrid_memory_operate(CPU->_hmem_ctrl, CPU->global_clk);			
		}
		cache_operate(CPU->_llc, CPU->global_clk);		
		for (uint32_t core_id = 0; core_id < n_cores; core_id++)
		{
			core_t*	_core = CPU->core[core_id];
			//rob_t*	_rob	= CPU->core[core_id]->_rob;
			
			cache_operate(_core->_l2, CPU->global_clk);
			cache_operate(_core->_l1, CPU->global_clk);
		}
		//* >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
		for (uint32_t core_id = 0; core_id < n_cores; core_id++)	//TODO: EMPLOY CPU Scheduling policy!
		{
			core_t*	_core = CPU->core[core_id];
			rob_t*  _rob  = CPU->core[core_id]->_rob;
			//* (1) ROB consume --> RETIRE ==================================
			num_retire = 0;
			//TODO: while statement cond.
			while ( (num_retire < MAX_RETIRE) && (!is_rob_empty(_rob)) )
			//while ( (num_retire < MAX_RETIRE) && (!is_rob_empty(_rob)) && (!is_mem_queue_full(_core->_l1->_txQ)) && (!is_mem_queue_full(_core->_l1->_mshr)) )
			//while ( (num_retire < MAX_RETIRE) && (!is_rob_empty(_rob)) && (!is_mem_queue_full(_core->_l1->_txQ)) )
			{
				if ( (_rob->_op[_rob->_front].comp_time <= CPU->global_clk) && (_rob->_op[_rob->_front].state == COMMIT) )
				{
					if (_rob->_op[_rob->_front].type == NOT_MEM)
					//if (_rob->_op[_rob->_front].phys_addr == 0)
					{
						_core->n_non_mem_instr++;
						num_retire++;
						interval_inst++;
						_core->n_total_instr++;

						if (_core->n_total_instr == limit_instrs && (limit_instrs != 0))
							PIN_Detach();

					}
					//interval_inst++;
					//TODO: How can we treat interval_inst++; and _core->n_total_instr++; ?
					rob_dequeue(_rob);
					//_core->n_total_instr++;
					//TODO: could we put _core->n_total_instr++; at here?
				}
				else
					break;
			}
		}
		
		for (uint32_t core_id = 0; core_id < n_cores; core_id++)
		{
			core_t* _core = CPU->core[core_id];
			rob_t*	_rob	= CPU->core[core_id]->_rob;
			//* =============================================================
			//* (2) Cache request consuming
			uint32_t rob_id = _rob->_front;
			uint32_t req_limit = 2;
			for (uint32_t i=0; i<_rob->n_entry; i++)
			{
				if ( (_rob->_op[rob_id].comp_time <= CPU->global_clk) && (_rob->_op[rob_id].state == MEM_WAIT) )
				{
					//if ( (!is_mem_queue_full(_core->_l1->_txQ)) && (!is_mem_queue_full(_core->_l1->_mshr)) )
					if (!is_mem_queue_full(_core->_l1->_txQ))
					{
						if (_rob->_op[rob_id].type == MEM_ST && req_limit != 2)	break;
						req_t new_req;
						new_req.phys_addr = _rob->_op[rob_id].phys_addr;
						new_req.comp_time = CPU->global_clk;
						new_req.req_state = PENDING;
						new_req.req_type  = _rob->_op[rob_id].type;
						new_req.rob_ptr		= &(_rob->_op[rob_id]);
						new_req.core_id		= core_id;
						new_req.is_hit		= false;
						new_req.is_pf			= false;
						new_req.pf_target = 0;
						mem_queue_insert_request(_core->_l1->_txQ, &new_req);
						_rob->_op[rob_id].comp_time = CPU->global_clk + BIG_LATENCY;
						_rob->_op[rob_id].state			= COMMIT;
						//TODO::: IMPORTANT!
						if (max_phys_addr < new_req.phys_addr)	max_phys_addr = new_req.phys_addr;
						req_limit -= (new_req.req_type == MEM_ST)? 2: 1;
						//break;	//TODO: (HPM) does not have this one.
					}
				}
				rob_id++;
				if (rob_id == _rob->capacity)	rob_id = 0;
				if (req_limit <= 0)	break;
			}
		}
		for (uint32_t core_id = 0; core_id < n_cores; core_id++)
		{
			core_t* _core = CPU->core[core_id];
			rob_t*	_rob	= CPU->core[core_id]->_rob;
			//* (4) CPU trace ---> ROB enqueue ==============================
			num_fetch = 0;
			//TODO: core stall condition! [IMPORTANT!!]
			//while ( (num_fetch < FETCH_WIDTH) && (_rob->n_entry < _rob->capacity) && (!is_mem_queue_full(_core->_l1->_txQ)) && (!is_mem_queue_full(_core->_l1->_mshr)) )
			//while ( (num_fetch < FETCH_WIDTH) && (!is_rob_full(_rob)) )
			while ( (num_fetch < FETCH_WIDTH) && !is_rob_full(_rob) && (!is_mem_queue_full(_core->_l1->_txQ)))
			{
				if (instr_queue->num_events == 0)	break;
				//if (instr_queue->num_events < MIN_NUM_EVENTS)	return;

				//if (feof(_core->_trace))	trace_end[core_id] = true;
				//if (trace_end[core_id] && (num_instrs[core_id] == 0))	break;
				if ((num_instrs[core_id] == 0) && !mem_fetch[core_id])
				{
					deq_event(instr_queue, &current_instr);
					//fscanf(_core->_trace, "%u %u %u %lu\n", &optype[core_id], &thread_id[core_id], &num_instrs[core_id], &addr[core_id]);
					optype[core_id] = current_instr.type;
					thread_id[core_id] = core_id;	//TODO: fixit!
					num_instrs[core_id] = current_instr.instrs;
					addr[core_id] = current_instr.addr;

					mem_fetch[core_id] = true;
				}
				if (is_rob_full(_rob))	break;
				if (num_instrs[core_id])
				{
					new_op.comp_time = CPU->global_clk + PIPELINE_LENGTH;
					new_op.core_id	 = thread_id[core_id];
					new_op.inst_addr = 0;
					new_op.type			 = NOT_MEM;
					new_op.phys_addr = 0;
					new_op.state		 = COMMIT;
					rob_enqueue(_rob, &new_op);
					num_instrs[core_id]--;
					num_fetch++;
				}
				if (is_rob_full(_rob))	break;
				if (num_instrs[core_id] == 0 && mem_fetch[core_id])
				{
					new_op.comp_time	= CPU->global_clk + PIPELINE_LENGTH; //TODO:: HPM
					//new_op.comp_time	= CPU->global_clk;
					new_op.core_id		= thread_id[core_id];
					new_op.inst_addr	= 0;
					new_op.type				= (optype[core_id]==1)?MEM_ST:MEM_LD;
					new_op.phys_addr	= addr[core_id];
					new_op.state			= MEM_WAIT;
					rob_enqueue(_rob, &new_op);
					//num_fetch++;	//TODO: 2019-02-15: is this "the" problem?
					mem_fetch[core_id] = false;
				}
			}
			//* ==============================================================
		} //* per-core loop


//! debug -----------------------
#if 0
		printf("[clk:%llu]\n", CPU->global_clk);
		
		rob_t* d_rob = CPU->core[0]->_rob;
		uint32_t d_rob_ptr = d_rob->_front;
		printf(" {ROB}\n");
		const char* d_rob_txt[4] = {"FETCH", "MEM_WAIT", "COMMIT"};
		for (uint32_t i=0; i<d_rob->n_entry; i++)
		{
			printf("  %8s  comp:%12llu  addr:%12llu\n", d_rob_txt[d_rob->_op[d_rob_ptr].state], d_rob->_op[d_rob_ptr].comp_time, d_rob->_op[d_rob_ptr].phys_addr);
			d_rob_ptr++;
			if (d_rob_ptr >= d_rob->capacity)	d_rob_ptr = 0;
		}

		cache_t* d_cache = CPU->core[0]->_l1;
		req_t*	 d_req = d_cache->_txQ->_head;
		printf(" {L1 txQ}\n");
		const char* d_cache_txt[8] = {"EMPTY", "PENDING", "HIT", "MISS", "MSHR_WAIT", "FILL_WAIT", "DONE"};
		for (uint32_t i=0; i<d_cache->_txQ->n_req; i++)
		{
			printf("  %9s  comp:%12llu  addr:%12llu\n", d_cache_txt[d_req->req_state], d_req->comp_time, d_req->phys_addr);
			d_req = d_req->_next;
		}

		printf(" {L2 txQ}\n");
		d_cache = CPU->core[0]->_l2;
		d_req = d_cache->_txQ->_head;
		for (uint32_t i=0; i<d_cache->_txQ->n_req; i++)
		{
			printf("  %9s  comp:%12llu  addr:%12llu\n", d_cache_txt[d_req->req_state], d_req->comp_time, d_req->phys_addr);
			d_req = d_req->_next;
		}

		printf(" {L3 txQ}\n");
		d_cache = CPU->core[0]->_l3;
		d_req = d_cache->_txQ->_head;
		for (uint32_t i=0; i<d_cache->_txQ->n_req; i++)
		{
			printf("  %9s  comp:%12llu  addr:%12llu\n", d_cache_txt[d_req->req_state], d_req->comp_time, d_req->phys_addr);
			d_req = d_req->_next;
		}

		printf("  {MEM txQ}\n");
		mem_ctrl_t* d_ctrl = CPU->_mem_ctrl;
		mem_req_t*	d_mreq = d_ctrl->_readQ->_head;

		for (uint32_t i=0; i<d_ctrl->_readQ->n_req; i++)
		{
			printf("   comp:%12llu addr:%12llu\n", d_mreq->comp_time, d_mreq->phys_addr);
			d_mreq = d_mreq->_next;
		}
/*
		printf(" {Hybrid MEM}\n");
		hm_ctrl_t* d_hmem = CPU->_hmem_ctrl;
		hm_queue_t* d_hq = d_hmem->_txQ;
		hmem_req_t* d_hreq = d_hq->_head;
		const char* d_hreq_txt[8] = {"H_IDLE", "H_TAG", "H_HIT", "H_MISS", "H_MIG", "H_DONE"};
		
		for (uint32_t i=0; i<d_hq->n_req; i++)
		{
			printf("  %9s  comp:%12llu  addr:%12llu\n", d_hreq_txt[d_hreq->state], d_hreq->comp_time, d_hreq->phys_addr);
			d_hreq = d_hreq->_next;
		}
*/
		printf("\n\n");
#endif 
//! end of debug print ----------

		//* CPI stack:::::
		//TODO: per instructions analysis is strongly recommended!!
		if (is_mem_queue_empty(CPU->core[0]->_l1->_txQ))	CPU->core[0]->cpi_ins_exec_time++;
		if (!is_mem_queue_empty(CPU->core[0]->_l1->_txQ) && is_mem_queue_empty(CPU->core[0]->_l2->_txQ))
			CPU->core[0]->cpi_l1c_exec_time++;
		if (!is_mem_queue_empty(CPU->core[0]->_l1->_txQ) && !is_mem_queue_empty(CPU->core[0]->_l2->_txQ) &&
				is_mem_queue_empty(CPU->_llc->_txQ))
			CPU->core[0]->cpi_l2c_exec_time++;
		
		if (!CPU->hybrid)
		{
			if (!is_mem_queue_empty(CPU->core[0]->_l1->_txQ) && !is_mem_queue_empty(CPU->core[0]->_l2->_txQ) &&
					!is_mem_queue_empty(CPU->_llc->_txQ) && is_req_queue_empty(CPU->_mem_ctrl->_readQ) && is_req_queue_empty(CPU->_mem_ctrl->_writeQ))
				CPU->cpi_llc_exec_time++;		
			if (!is_req_queue_empty(CPU->_mem_ctrl->_readQ) || !is_req_queue_empty(CPU->_mem_ctrl->_writeQ))
				CPU->cpi_mem_exec_time++;
		}
		else
		{
			if (!is_mem_queue_empty(CPU->core[0]->_l1->_txQ) && !is_mem_queue_empty(CPU->core[0]->_l2->_txQ) &&
					!is_mem_queue_empty(CPU->_llc->_txQ) && h_is_req_queue_empty(CPU->_hmem_ctrl->_txQ))
				CPU->cpi_llc_exec_time++;		
			if (!h_is_req_queue_empty(CPU->_hmem_ctrl->_txQ))
				CPU->cpi_mem_exec_time++;
		}

		//* increase the global clk count
		CPU->global_clk++;
		interval_cnt++;

/*
		//* A condition for triggering the end of the simulation (legacy, for a trace-driven standalone simulator)
		bool flag_trace_end = true;
		bool flag_rob_empty = true;
		bool flag_txQ_empty = true;
		bool flag_num_instr = true;
		for (uint32_t core_id=0; core_id < n_cores; ++core_id)
		{
			flag_trace_end = flag_trace_end && trace_end[core_id];
			flag_rob_empty = flag_rob_empty && (CPU->core[core_id]->_rob->n_entry == 0);
			flag_txQ_empty = flag_txQ_empty && (CPU->core[core_id]->_l1->_txQ->n_req == 0);
			flag_num_instr = flag_num_instr && (num_instrs[core_id]==0); 
		}

		if (flag_trace_end && flag_rob_empty && flag_txQ_empty && flag_num_instr)
			break;
*/
		//TODO: weak-approach for detecting the variant of current working set size.
		if (interval_cnt % 1000 == 0)	//* check the variant of working set size..
			max_phys_addr = 0;

		if (interval_inst >= 1000000)
		//if (interval_cnt == 1000000)
		{
			intv_cnt++;
			gettimeofday(&tv2, NULL);
			double interval_ipc = mtsim_print_large_div(interval_inst, interval_cnt);
			double stats_mpki = mtsim_print_large_div(CPU->interval_mpki, interval_inst) * 1000;

			fprintf(ipc, "%lf\n", interval_ipc);
			fprintf(mpki, "%lf\n", stats_mpki);
			fflush(ipc);
			fflush(mpki);
			double mips = (double)(tv2.tv_usec - tv1.tv_usec) / 1000000 + (double)(tv2.tv_sec - tv1.tv_sec);

			double cur_bandwidth;

			if (!CPU->hybrid)
			{
				cur_bandwidth = mtsim_print_large_div(CPU->_mem_ctrl->interval_bandwidth * (CPU->freq / 1000), interval_cnt);
				//printf("%llu\n", CPU->_mem_ctrl->interval_bandwidth);
				//cur_bandwidth = mtsim_print_large_div(cur_bandwidth, 1000);
				fprintf(f_bandwidth, "%lf\n", cur_bandwidth);
				fflush(f_bandwidth);
			}

			/*
			mips = interval_inst / mips / 1000000;
			double l1_mpki, l2_mpki;
			l1_mpki = (double)CPU->core[0]->interval_l1_miss_cnt / (double)(interval_inst / 1000);
			l2_mpki = (double)CPU->core[0]->interval_l2_miss_cnt / (double)(interval_inst / 1000);

			double l1_rate, l2_rate, llc_rate;
			if (CPU->core[0]->_l1->miss_cnt + CPU->core[0]->_l1->hit_cnt != 0)
				l1_rate = (double)((CPU->core[0]->_l1->hit_cnt*10000) / (CPU->core[0]->_l1->miss_cnt + CPU->core[0]->_l1->hit_cnt));
			else
				l1_rate = .0;
			if (CPU->core[0]->_l2->miss_cnt + CPU->core[0]->_l2->hit_cnt != 0)
				l2_rate = (double)((CPU->core[0]->_l2->hit_cnt*10000) / (CPU->core[0]->_l2->miss_cnt + CPU->core[0]->_l2->hit_cnt));
			else
				l2_rate = .0;
			if (CPU->_llc->miss_cnt + CPU->_llc->hit_cnt != 0)
				llc_rate = (double)((CPU->_llc->hit_cnt*10000) / (CPU->_llc->miss_cnt + CPU->_llc->hit_cnt));
			else
				llc_rate = .0;

			*/
			//TODO:::::
			//* fixit!
			/*
			printf("\033[A\33[2K");
			printf("[clk: %12lu] [IPC: %.3lf] [PROC: %12llu] [INST: (%u)%12llu]", CPU->global_clk, interval_ipc, proc_total_instr, intv_cnt, interval_inst);
			printf(" [L1_MPKI: %6.2lf(%6.2lf%%)] [L2_MPKI: %6.2lf(%6.2lf%%)] [LLC_MPKI: %6.2lf(%6.2lf%%)] [B/W: %.2Lf GB/s] [MIPS: %5.2lf]\n", l1_mpki, l1_rate/100, l2_mpki, l2_rate/100, stats_mpki, llc_rate/100, cur_bandwidth, mips);
			*/
			memcpy(&tv1, &tv2, sizeof(struct timeval));

			//if (CPU->core[0]->_l1->miss_cnt + CPU->core[0]->_l1->hit_cnt > 0 && CPU->_llc->miss_cnt + CPU->_llc->hit_cnt > 0 && CPU->core[0]->_l2->miss_cnt + CPU->core[0]->_l2->hit_cnt > 0)
			//printf("[clk: %11lu] [IPC: %.3lf] [MPKI: %.2lf] [L1: %2.2lf] [L2: %2.2lf] [L3: %2.2lf]\n", CPU->global_clk, interval_ipc, stats_mpki, (double)(CPU->core[0]->_l1->hit_cnt) / (CPU->core[0]->_l1->miss_cnt + CPU->core[0]->_l1->hit_cnt)*100, (double)(CPU->core[0]->_l2->hit_cnt) / (CPU->core[0]->_l2->miss_cnt + CPU->core[0]->_l2->hit_cnt)*100, (double)(CPU->_llc->hit_cnt) / (CPU->_llc->miss_cnt + CPU->_llc->hit_cnt)*100);
			//fflush(ipc);
			//fflush(mpki);
			interval_inst = 0;
			CPU->core[0]->interval_l1_miss_cnt = 0;
			CPU->core[0]->interval_l2_miss_cnt = 0;
			CPU->interval_mpki= 0;
			interval_cnt = 0;
			if (!CPU->hybrid)
				CPU->_mem_ctrl->interval_bandwidth = 0;

		}

		if (instr_queue->num_events < MIN_NUM_EVENTS)	return;

	} //* simulation loop

}

VOID emit_instr(VOID *ip, uint32_t thread_id)
{
	//enq_event(eventQ, 0, NOT_MEM);
	//run_cycle(false);
	proc_total_instr++;
	eventQ->nonmem_instrs++;
	run_cycle();
}

VOID emit_load(VOID *addr, uint32_t thread_id)
{
	enq_event(eventQ, pMapper->translate((uint64_t)addr), MEM_LD);
	//enq_event(eventQ, ((uint64_t)addr), MEM_LD);
	//run_cycle();
}

VOID emit_store(VOID *addr, uint32_t thread_id)
{
	enq_event(eventQ, pMapper->translate((uint64_t)addr), MEM_ST);
	//enq_event(eventQ, ((uint64_t)addr), MEM_ST);
	//run_cycle();
}

VOID Instruction(INS ins, VOID *v)
{
	UINT32 memOperands = INS_MemoryOperandCount(ins);

	INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)emit_instr, IARG_INST_PTR, IARG_THREAD_ID, IARG_END);
	
	for (UINT32 memOp = 0; memOp < memOperands; memOp++)
	{
		if (INS_MemoryOperandIsRead(ins, memOp))
		{
			INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)emit_load, IARG_MEMORYOP_EA, memOp, IARG_THREAD_ID, IARG_END);
		}
		if (INS_MemoryOperandIsWritten(ins, memOp))
		{
			INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)emit_store, IARG_MEMORYOP_EA, memOp, IARG_THREAD_ID, IARG_END);
		}
	}
}

VOID Fini(INT32 code, VOID *v)
{
	//run_cycle(true);

	gettimeofday(&simu_end, NULL);
	//uint64_t avg_mips = (simu_end.tv_usec - simu_start.tv_usec) / 1000000 + (simu_end.tv_sec - simu_start.tv_sec);
	uint64_t avg_mips = (simu_end.tv_sec - simu_start.tv_sec) * 1000000 + (simu_end.tv_usec - simu_start.tv_usec);

	printf("(mtsim++) \n");
	printf("(mtsim++) simulation end.\n");
	printf("(mtsim++) \n");
	printf("(mtsim++) -------------------- Legend for Statistics ---------------------\n");
	printf("(mtsim++)   IPC: Instructions per cycle\n");
	printf("(mtsim++)   L1/L2/L3 MPKI: Cache misses per 1K instructions\n");
	printf("(mtsim++)   MIPS: Simluation speed in MIPS(Million instructions per second\n");
	printf("(mtsim++) ----------------------------------------------------------------\n");
	printf("(mtsim++) \n");
	printf("(mtsim++) Total execution cycle: %lu cycles\n", CPU->global_clk);
	
	uint64_t temp_time = CPU->global_clk;
	uint8_t  degree = 0;
	const char* time_degree[4] = {"ns", "us", "ms", "sec"};
	while (1)
	{
		if (degree==3)	break;
		if (temp_time >= 1000)
		{
			temp_time = temp_time / 1000;
			degree++;
		}
		else
			break;
		if (temp_time >= 1000 && temp_time < 1000000)
			break;
	}
	double elapsed_time = temp_time / (double)CPU->freq; //1000;
	degree++;
	printf("(mtsim++) Total execution time:  %.3lf %s\n", elapsed_time, time_degree[degree]);

	//printf("(mtsim++) Average IPC:           %.3Lf (Instr/cycle)\n", (long double) CPU->core[0]->n_non_mem_instr / CPU->global_clk);
	//TODO: multiple memory operations with one Macro-op
	//printf("(mtsim++) Average IPC:           %.3Lf (Instr/cycle)\n", (long double)CPU->core[0]->n_total_instr / CPU->global_clk);
	//TODO!!!!!!!!!!!!!! : n_total_instr non_mem_total_instr
	printf("(mtsim++) Average IPC:           %.3lf (Instr/cycle)\n", mtsim_print_large_div(CPU->core[0]->n_non_mem_instr, CPU->global_clk));
	//avg_mips = (uint64_t)CPU->core[0]->n_non_mem_instr / (avg_mips/1000);
	if (avg_mips > 1000)
		printf("(mtsim++) Average MIPS:          %.2lf (MInstr/sec)\n", mtsim_print_large_div(CPU->core[0]->n_non_mem_instr, avg_mips));
	fflush(stdout);
	printf("(mtsim++) \n");
	//printf("(%llu, %llu, %llu)\n", CPU->core[0]->_l1->hit_cnt, CPU->core[0]->_l1->miss_cnt, CPU->core[0]->_l1->access_cnt);
	//printf("(%llu, %llu)\n", CPU->core[0]->_l2->hit_cnt, CPU->core[0]->_l2->miss_cnt);
	for (uint32_t n=0; n<n_cores; ++n)
	{
		printf("(mtsim++) # core %d \n", n);
		//printf("(mtsim++) L1$ hit rate: %.2lf%% | MPKI: %.2lf\n", (double)(CPU->core[n]->_l1->hit_cnt) / (CPU->core[n]->_l1->miss_cnt + CPU->core[n]->_l1->hit_cnt) * 100, CPU->core[n]->_l1->miss_cnt / (double)(CPU->core[n]->n_total_instr / 1000) );
		//printf("(mtsim++) L2$ hit rate: %.2lf%% | MPKI: %.2lf\n", (double)(CPU->core[n]->_l2->hit_cnt) / (CPU->core[n]->_l2->hit_cnt + CPU->core[n]->_l2->miss_cnt) * 100, CPU->core[n]->_l2->miss_cnt / (double)(CPU->core[n]->n_total_instr / 1000) );
		printf("(mtsim++) L1$ hit rate: %.2lf%% | MPKI: %.2lf\n", mtsim_print_large_div(CPU->core[n]->_l1->hit_cnt * 100, CPU->core[n]->_l1->miss_cnt + CPU->core[n]->_l1->hit_cnt), mtsim_print_large_div(CPU->core[n]->_l1->miss_cnt * 1000, CPU->core[n]->n_non_mem_instr));
		printf("(mtsim++) L2$ hit rate: %.2lf%% | MPKI: %.2lf\n", mtsim_print_large_div(CPU->core[n]->_l2->hit_cnt * 100, CPU->core[n]->_l2->miss_cnt + CPU->core[n]->_l2->hit_cnt), mtsim_print_large_div(CPU->core[n]->_l2->miss_cnt * 1000, CPU->core[n]->n_non_mem_instr));
	}
	//TODO: llc--> fix it for multi-thread case!
	printf("(mtsim++) \n");
	printf("(mtsim++) # shared resources: \n");
	//printf("(mtsim++) L3$ hit rate: %.2lf%% | MPKI: %.2lf\n", (double)(CPU->_llc->hit_cnt) / (CPU->_llc->hit_cnt + CPU->_llc->miss_cnt) * 100, CPU->_llc->miss_cnt / (double)(CPU->core[0]->n_total_instr / 1000));
	printf("(mtsim++) L3$ hit rate: %.2lf%% | MPKI: %.2lf\n", mtsim_print_large_div(CPU->_llc->hit_cnt, CPU->_llc->hit_cnt + CPU->_llc->miss_cnt) * 100, mtsim_print_large_div(CPU->_llc->miss_cnt * 1000, CPU->core[0]->n_total_instr));
	printf("(mtsim++) \n");

	if (!CPU->hybrid)
	{
		printf("(mtsim++) Total main memory requests: %lu\n", CPU->_mem_ctrl->total_mem_req_cnt);
		uint64_t row_buffer_hit_cnt = CPU->_mem_ctrl->total_mem_req_cnt - CPU->_mem_ctrl->row_buffer_miss_cnt;
		if (row_buffer_hit_cnt == CPU->_mem_ctrl->total_mem_req_cnt)
			printf("(mtsim++) Memory row buffer hit rate: 100.00%% (%lu/%lu)\n", row_buffer_hit_cnt, CPU->_mem_ctrl->total_mem_req_cnt);
		else if (row_buffer_hit_cnt == 0)
			printf("(mtsim++) Memory row buffer hit rate: 0.00%% (%lu/%lu)\n", row_buffer_hit_cnt, CPU->_mem_ctrl->total_mem_req_cnt);
		else
			printf("(mtsim++) Memory row buffer hit rate: %.2Lf%% (%lu/%lu)\n", (long double)(row_buffer_hit_cnt*100*100 / CPU->_mem_ctrl->total_mem_req_cnt) / 100, row_buffer_hit_cnt, CPU->_mem_ctrl->total_mem_req_cnt);
		double avg_mem_lat = CPU->_mem_ctrl->total_waiting_time * 100 / CPU->_mem_ctrl->total_mem_req_cnt;

		printf("(mtsim++) Average main memory request latency: %.2lf (cycles), %.2lf (ns)\n", avg_mem_lat / 100, (avg_mem_lat / 100) / (CPU->freq / 1000) );

		//printf("(mtsim++) Average main memory bandwidth: %.3lf (MiB/sec)\n", mtsim_print_large_div(CPU->_mem_ctrl->total_bandwidth/1000, avg_mips/1000));
		printf("(mtsim++) Average Memory Bandwidth: %.3lf (GiB/sec)\n", 64 / ((avg_mem_lat/100) / (CPU->freq / 1000)) );
	}
	printf("(mtsim++) \n");



#if 0
	printf("(mtsim++) ======= CPI stack =======\n");
	
	uint64_t total_exec_time = CPU->core[0]->cpi_ins_exec_time + CPU->core[0]->cpi_l1c_exec_time + CPU->core[0]->cpi_l2c_exec_time\
														+ CPU->cpi_llc_exec_time + CPU->cpi_mem_exec_time;

	printf("(mtsim++)  Dispatch & execution: %5.2lf%%\n", mtsim_print_large_div(CPU->core[0]->cpi_ins_exec_time * 100, total_exec_time) );
	printf("(mtsim++)    L1 cache execution: %5.2lf%%\n", mtsim_print_large_div(CPU->core[0]->cpi_l1c_exec_time * 100, total_exec_time) );
	printf("(mtsim++)    L2 cache execution: %5.2lf%%\n", mtsim_print_large_div(CPU->core[0]->cpi_l2c_exec_time * 100, total_exec_time) );
	printf("(mtsim++)    L3 cache execution: %5.2lf%%\n", mtsim_print_large_div(CPU->cpi_llc_exec_time * 100, total_exec_time) );
	printf("(mtsim++) Main memory execution: %5.2lf%%\n", mtsim_print_large_div(CPU->cpi_mem_exec_time * 100, total_exec_time) );


	printf("(mtsim++) \n");
	printf("(mtsim++) [prefetch statistics]\n");
	printf("(mtsim++) == accuracy ==\n");
	printf("(mtsim++) L1 pref accuracy: %.3lf%% (%u/%u)\n", mtsim_print_large_div(CPU->core[0]->_l1->pf_hit * 100, CPU->core[0]->_l1->pf_cnt), CPU->core[0]->_l1->pf_hit, CPU->core[0]->_l1->pf_cnt);
	printf("(mtsim++) L2 pref accuracy: %.3lf%% (%u/%u)\n", mtsim_print_large_div(CPU->core[0]->_l2->pf_hit * 100, CPU->core[0]->_l2->pf_cnt), CPU->core[0]->_l2->pf_hit, CPU->core[0]->_l2->pf_cnt);
	printf("(mtsim++) L3 pref accuracy: %.3lf%% (%u/%u)\n", mtsim_print_large_div(CPU->_llc->pf_hit * 100, CPU->_llc->pf_cnt), CPU->_llc->pf_hit, CPU->_llc->pf_cnt);
	printf("(mtsim++) == coverage ==\n");
	printf("(mtsim++) (in progress)");
	printf("(mtsim++)\n");

	printf("(mtsim++)\n");
	printf("(mtsim++) [debug stats for constructing simulator]\n");
	//printf("(mtsim++) # of Non-memory instructions: %u (%.2Lf%%)\n", CPU->core[0]->n_non_mem_instr, (long double) ((((uint64_t)(CPU->core[0]->n_non_mem_instr)*1000) / CPU->core[0]->n_total_instr)) / 10 );
	printf("(mtsim++) # of Non-memory instructions: %u (%.2lf%%)\n", CPU->core[0]->n_non_mem_instr, mtsim_print_large_div(CPU->core[0]->n_non_mem_instr, CPU->core[0]->n_total_instr) * 100);
	printf("(mtsim++) L1 pref timing cnt: %lu (%.3Lf%%)\n", CPU->core[0]->_l1->pref_timing_cnt, CPU->core[0]->_l1->pref_timing_cnt / (long double)CPU->global_clk * 100);
	printf("(mtsim++) L2 pref timing cnt: %lu (%.3Lf%%)\n", CPU->core[0]->_l2->pref_timing_cnt, CPU->core[0]->_l2->pref_timing_cnt / (long double)CPU->global_clk * 100);
	printf("(mtsim++) LLC pref timing cnt: %lu (%.3Lf%%)\n", CPU->_llc->pref_timing_cnt, CPU->_llc->pref_timing_cnt / (long double)CPU->global_clk * 100);
#endif 
	printf("(mtsim++) \n");

	free_processor_structure(CPU);

}

INT32 Usage()
{
	PIN_ERROR("This Pintool does simulate a simple core simulation model.\n");
	return -1;
}

VOID DetachProgram(VOID *v)
{

	//run_cycle(true);

	gettimeofday(&simu_end, NULL);
	//uint64_t avg_mips = (simu_end.tv_usec - simu_start.tv_usec) / 1000000 + (simu_end.tv_sec - simu_start.tv_sec);
	uint64_t avg_mips = (simu_end.tv_sec - simu_start.tv_sec) * 1000000 + (simu_end.tv_usec - simu_start.tv_usec);

	printf("(mtsim++) \n");
	printf("(mtsim++) simulation end.\n");
	printf("(mtsim++) \n");
	printf("(mtsim++) -------------------- Legend for Statistics ---------------------\n");
	printf("(mtsim++)   IPC: Instructions per cycle\n");
	printf("(mtsim++)   L1/L2/L3 MPKI: Cache misses per 1K instructions\n");
	printf("(mtsim++)   MIPS: Simluation speed in MIPS(Million instructions per second\n");
	printf("(mtsim++) ----------------------------------------------------------------\n");
	printf("(mtsim++) \n");
	printf("(mtsim++) Total execution cycle: %lu cycles\n", CPU->global_clk);
	
	uint64_t temp_time = CPU->global_clk;
	uint8_t  degree = 0;
	const char* time_degree[4] = {"ns", "us", "ms", "sec"};
	while (1)
	{
		//if (degree==3)	break;
		if (temp_time >= 1000)
		{
			temp_time = temp_time / 1000;
			degree++;
		}
		else
			break;
		if (temp_time >= 1000 && temp_time < 1000000)
			break;
	}
	degree++;
	
	long double elapsed_time = CPU->global_clk / CPU->freq;
	printf("%Lf\n", elapsed_time);
	for (uint32_t i=0; i<degree-1; i++)	elapsed_time /= 1000;

	printf("(mtsim++) Total execution time:  %.3Lf %s\n", elapsed_time, time_degree[degree]);

	//printf("(mtsim++) Average IPC:           %.3Lf (Instr/cycle)\n", (long double) CPU->core[0]->n_non_mem_instr / CPU->global_clk);
	//TODO: multiple memory operations with one Macro-op
	//printf("(mtsim++) Average IPC:           %.3Lf (Instr/cycle)\n", (long double)CPU->core[0]->n_total_instr / CPU->global_clk);
	//TODO!!!!!!!!!!!!!! : n_total_instr non_mem_total_instr
	printf("(mtsim++) Average IPC:           %.3lf (Instr/cycle)\n", mtsim_print_large_div(CPU->core[0]->n_non_mem_instr, CPU->global_clk));
	//avg_mips = (uint64_t)CPU->core[0]->n_non_mem_instr / (avg_mips/1000);
	if (avg_mips > 1000)
		printf("(mtsim++) Average MIPS:          %.2lf (MInstr/sec)\n", mtsim_print_large_div(CPU->core[0]->n_non_mem_instr, avg_mips));
	fflush(stdout);
	printf("(mtsim++) \n");
	for (uint32_t n=0; n<n_cores; ++n)
	{
		printf("(mtsim++) # core %d \n", n);
		//printf("(mtsim++) L1$ hit rate: %.2lf%% | MPKI: %.2lf\n", (double)(CPU->core[n]->_l1->hit_cnt) / (CPU->core[n]->_l1->miss_cnt + CPU->core[n]->_l1->hit_cnt) * 100, CPU->core[n]->_l1->miss_cnt / (double)(CPU->core[n]->n_total_instr / 1000) );
		//printf("(mtsim++) L2$ hit rate: %.2lf%% | MPKI: %.2lf\n", (double)(CPU->core[n]->_l2->hit_cnt) / (CPU->core[n]->_l2->hit_cnt + CPU->core[n]->_l2->miss_cnt) * 100, CPU->core[n]->_l2->miss_cnt / (double)(CPU->core[n]->n_total_instr / 1000) );
		printf("(mtsim++) L1$ hit rate: %.2lf%% | MPKI: %.2lf\n", mtsim_print_large_div(CPU->core[n]->_l1->hit_cnt * 100, CPU->core[n]->_l1->miss_cnt + CPU->core[n]->_l1->hit_cnt), mtsim_print_large_div(CPU->core[n]->_l1->miss_cnt * 1000, CPU->core[n]->n_non_mem_instr));
		printf("(mtsim++) L2$ hit rate: %.2lf%% | MPKI: %.2lf\n", mtsim_print_large_div(CPU->core[n]->_l2->hit_cnt * 100, CPU->core[n]->_l2->miss_cnt + CPU->core[n]->_l2->hit_cnt), mtsim_print_large_div(CPU->core[n]->_l2->miss_cnt * 1000, CPU->core[n]->n_non_mem_instr));
	}
	//TODO: llc--> fix it for multi-thread case!
	printf("(mtsim++) \n");
	printf("(mtsim++) # shared resources: \n");
	//printf("(mtsim++) L3$ hit rate: %.2lf%% | MPKI: %.2lf\n", (double)(CPU->_llc->hit_cnt) / (CPU->_llc->hit_cnt + CPU->_llc->miss_cnt) * 100, CPU->_llc->miss_cnt / (double)(CPU->core[0]->n_total_instr / 1000));
	printf("(mtsim++) L3$ hit rate: %.2lf%% | MPKI: %.2lf\n", mtsim_print_large_div(CPU->_llc->hit_cnt, CPU->_llc->hit_cnt + CPU->_llc->miss_cnt) * 100, mtsim_print_large_div(CPU->_llc->miss_cnt * 1000, CPU->core[0]->n_total_instr));
	printf("(mtsim++) \n");

	if (!CPU->hybrid)
	{
		printf("(mtsim++) Total main memory requests: %lu\n", CPU->_mem_ctrl->total_mem_req_cnt);
		uint64_t row_buffer_hit_cnt = CPU->_mem_ctrl->total_mem_req_cnt - CPU->_mem_ctrl->row_buffer_miss_cnt;
		if (row_buffer_hit_cnt == CPU->_mem_ctrl->total_mem_req_cnt)
			printf("(mtsim++) Memory row buffer hit rate: 100.00%% (%lu/%lu)\n", row_buffer_hit_cnt, CPU->_mem_ctrl->total_mem_req_cnt);
		else if (row_buffer_hit_cnt == 0)
			printf("(mtsim++) Memory row buffer hit rate: 0.00%% (%lu/%lu)\n", row_buffer_hit_cnt, CPU->_mem_ctrl->total_mem_req_cnt);
		else
			printf("(mtsim++) Memory row buffer hit rate: %.2Lf%% (%lu/%lu)\n", (long double)(row_buffer_hit_cnt*100*100 / CPU->_mem_ctrl->total_mem_req_cnt) / 100, row_buffer_hit_cnt, CPU->_mem_ctrl->total_mem_req_cnt);
		double avg_mem_lat = CPU->_mem_ctrl->total_waiting_time * 100 / CPU->_mem_ctrl->total_mem_req_cnt;

		printf("(mtsim++) Average memory request latency: %.2lf (cycles), %.2lf (ns)\n", avg_mem_lat / 100, (avg_mem_lat / 100) / (CPU->freq / 1000) );

		//printf("(mtsim++) Average main memory bandwidth: %.3lf (MiB/sec)\n", mtsim_print_large_div(CPU->_mem_ctrl->total_bandwidth/1000, avg_mips/1000));
		printf("(mtsim++) Average Memory Bandwidth: %.3lf (GiB/sec)\n", 64 / ((avg_mem_lat/100) / (CPU->freq / 1000)) );
	}

	printf("(mtsim++) \n");
	printf("(mtsim++) ======= CPI stack =======\n");

		if (is_mem_queue_empty(CPU->core[0]->_l1->_txQ))	CPU->core[0]->cpi_ins_exec_time++;
		if (!is_mem_queue_empty(CPU->core[0]->_l1->_txQ) && is_mem_queue_empty(CPU->core[0]->_l2->_txQ))
			CPU->core[0]->cpi_l1c_exec_time++;
		if (!is_mem_queue_empty(CPU->core[0]->_l1->_txQ) && !is_mem_queue_empty(CPU->core[0]->_l2->_txQ) &&
				is_mem_queue_empty(CPU->_llc->_txQ))
			CPU->core[0]->cpi_l2c_exec_time++;
		if (!is_mem_queue_empty(CPU->core[0]->_l1->_txQ) && !is_mem_queue_empty(CPU->core[0]->_l2->_txQ) &&
				!is_mem_queue_empty(CPU->_llc->_txQ) && is_req_queue_empty(CPU->_mem_ctrl->_readQ) && is_req_queue_empty(CPU->_mem_ctrl->_writeQ))
			CPU->cpi_llc_exec_time++;		
		if (!is_req_queue_empty(CPU->_mem_ctrl->_readQ) || !is_req_queue_empty(CPU->_mem_ctrl->_writeQ))
			CPU->cpi_mem_exec_time++;

	uint64_t total_exec_time = CPU->core[0]->cpi_ins_exec_time + CPU->core[0]->cpi_l1c_exec_time + CPU->core[0]->cpi_l2c_exec_time\
													 + CPU->cpi_llc_exec_time + CPU->cpi_mem_exec_time;

	printf("(mtsim++)  Dispatch & execution: %5.2lf%%\n", mtsim_print_large_div(CPU->core[0]->cpi_ins_exec_time * 100, total_exec_time) );
	printf("(mtsim++)    L1 cache execution: %5.2lf%%\n", mtsim_print_large_div(CPU->core[0]->cpi_l1c_exec_time * 100, total_exec_time) );
	printf("(mtsim++)    L2 cache execution: %5.2lf%%\n", mtsim_print_large_div(CPU->core[0]->cpi_l2c_exec_time * 100, total_exec_time) );
	printf("(mtsim++)    L3 cache execution: %5.2lf%%\n", mtsim_print_large_div(CPU->cpi_llc_exec_time * 100, total_exec_time) );
	printf("(mtsim++) Main memory execution: %5.2lf%%\n", mtsim_print_large_div(CPU->cpi_mem_exec_time * 100, total_exec_time) );


	printf("(mtsim++) \n");
	printf("(mtsim++) [prefetch statistics]\n");
	printf("(mtsim++) == accuracy ==\n");
	printf("(mtsim++) L1 pref accuracy: %.3lf%% (%u/%u)\n", mtsim_print_large_div(CPU->core[0]->_l1->pf_hit * 100, CPU->core[0]->_l1->pf_cnt), CPU->core[0]->_l1->pf_hit, CPU->core[0]->_l1->pf_cnt);
	printf("(mtsim++) L2 pref accuracy: %.3lf%% (%u/%u)\n", mtsim_print_large_div(CPU->core[0]->_l2->pf_hit * 100, CPU->core[0]->_l2->pf_cnt), CPU->core[0]->_l2->pf_hit, CPU->core[0]->_l2->pf_cnt);
	printf("(mtsim++) L3 pref accuracy: %.3lf%% (%u/%u)\n", mtsim_print_large_div(CPU->_llc->pf_hit * 100, CPU->_llc->pf_cnt), CPU->_llc->pf_hit, CPU->_llc->pf_cnt);
	printf("(mtsim++) == coverage ==\n");
	printf("(mtsim++) (in progress)");
	printf("(mtsim++)\n");

	printf("(mtsim++)\n");
	printf("(mtsim++) [debug stats for constructing simulator]\n");
	//printf("(mtsim++) # of Non-memory instructions: %u (%.2Lf%%)\n", CPU->core[0]->n_non_mem_instr, (long double) ((((uint64_t)(CPU->core[0]->n_non_mem_instr)*1000) / CPU->core[0]->n_total_instr)) / 10 );
	printf("(mtsim++) # of Non-memory instructions: %u (%.2lf%%)\n", CPU->core[0]->n_non_mem_instr, mtsim_print_large_div(CPU->core[0]->n_non_mem_instr, CPU->core[0]->n_total_instr) * 100);
	printf("(mtsim++) L1 pref timing cnt: %lu (%.3Lf%%)\n", CPU->core[0]->_l1->pref_timing_cnt, CPU->core[0]->_l1->pref_timing_cnt / (long double)CPU->global_clk * 100);
	printf("(mtsim++) L2 pref timing cnt: %lu (%.3Lf%%)\n", CPU->core[0]->_l2->pref_timing_cnt, CPU->core[0]->_l2->pref_timing_cnt / (long double)CPU->global_clk * 100);
	printf("(mtsim++) LLC pref timing cnt: %lu (%.3Lf%%)\n", CPU->_llc->pref_timing_cnt, CPU->_llc->pref_timing_cnt / (long double)CPU->global_clk * 100);

	printf("(mtsim++) \n");

	free_processor_structure(CPU);

	PIN_ExitProcess(1);
}

int main(int argc, char** argv)
{
	if (PIN_Init(argc, argv)) return Usage();

	eventQ = init_simu_event_queue();

	config = fopen(KnobConfig.Value().c_str(), "r");
	mtsim_read_config_file(config, &simu);
	n_cores = simu.n_cores;

	char ipc_name[512], mpki_name[512], bw_name[512];
	char ipc_post[10]		= {'_','i','p','c','.','l','o','g','\0'};
	char mpki_post[10]	= {'_','m','p','k','i','.','l','o','g','\0'};
	char bw_post[10] = {'_','b','w','.','l','o','g','\0'};

	mtsim_print_mtsim_logo();

	strcpy(ipc_name, KnobAppname.Value().c_str());
	strcpy(mpki_name, KnobAppname.Value().c_str());
	strcpy(bw_name, KnobAppname.Value().c_str());

	strcpy(ipc_name + strlen(ipc_name), ipc_post);
	strcpy(mpki_name + strlen(mpki_name), mpki_post);
	strcpy(bw_name + strlen(bw_name), bw_post);

	limit_instrs = strtoull(KnobNumInstr.Value().c_str(), 0, 0);

	ipc = fopen(ipc_name, "w");
	mpki = fopen(mpki_name, "w");
	f_bandwidth = fopen(bw_name, "w");

	pMapper = new FCFSPageMapper();

	//TODO: the same issue as the line 390 @utility.cc
	if (simu.m_hybrid)
	{
		//TODO: assymetric case?
		proc_mem_freq_ratio = simu.freq / simu.m_freq;
	}
	else
	{
		if (simu.m_dram)
		{
			proc_mem_freq_ratio = simu.freq / simu.m_freq;
		}
		else
		{
			proc_mem_freq_ratio = simu.freq / simu.nv_freq;
		}
	}

	CPU = init_processor_structure(simu.freq, simu.n_cores, simu.rob_capacity,\
																 simu.l1_size, simu.l1_way, simu.l1_set, simu.l1_line, simu.l1_tag_lat, simu.l1_dta_lat,\
																 simu.l1_type, simu.l1_repl_type, simu.l1_mshr_size, simu.l1_txQ_size, simu.l1_pfQ_size,\
																 simu.l1_banks, simu.l1_perfect, simu.l2_size, simu.l2_way, simu.l2_set, simu.l2_line, simu.l2_tag_lat,\
																 simu.l2_dta_lat, simu.l2_type, simu.l2_repl_type, simu.l2_mshr_size, simu.l2_txQ_size,\
																 simu.l2_pfQ_size, simu.l2_banks, simu.l2_perfect, simu.llc_size, simu.llc_way, simu.llc_set,\
																 simu.llc_line, simu.llc_tag_lat, simu.llc_dta_lat, simu.llc_type, simu.llc_repl_type,\
																 simu.llc_mshr_size, simu.llc_txQ_size, simu.llc_pfQ_size, simu.llc_banks, simu.llc_perfect,\
																 simu.m_hybrid, simu.m_dram, simu.m_nvm,\
																 simu.m_channel, simu.m_rank, simu.m_bank, simu.m_row, simu.m_column,\
																 simu.m_readQ_size, simu.m_writeQ_size, simu.m_respQ_size, simu.m_tRP, simu.m_tRCD,\
																 simu.m_tCAS, simu.m_tBL, simu.m_tRAS, simu.m_tRC, simu.m_tCCD, simu.m_tRRD,\
																 simu.m_tFAW, simu.m_tWR, simu.m_tWTR, simu.m_tRTP, simu.m_tCWD, simu.m_scheme, simu.m_tRRDact, simu.m_tRRDpre,\
																 simu.nv_channel, simu.nv_rank, simu.nv_bank, simu.nv_row,\
																 simu.nv_column, simu.nv_readQ_size, simu.nv_writeQ_size, simu.nv_respQ_size, simu.nv_tRP, simu.nv_tRCD, simu.nv_tCAS, simu.nv_tBL, simu.nv_tRAS, simu.nv_tRC, simu.nv_tCCD, simu.nv_tRRD,\
																 simu.nv_tFAW, simu.nv_tWR, simu.nv_tWTR, simu.nv_tRTP, simu.nv_tCWD,
																 simu.nv_scheme, simu.nv_tRRDact, simu.nv_tRRDpre, simu.hc_size, simu.hc_way, simu.hc_set, simu.hc_line, simu.hc_type, simu.hc_repl_type
																 );

	
/*
	printf("\n");

	printf("%u %u %u\n", simu.m_hybrid, simu.m_dram, simu.m_nvm);

	printf("%u %u %u %6u %llu %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u\n", simu.m_channel, simu.m_rank, simu.m_bank, simu.m_row, simu.m_column, simu.m_readQ_size, simu.m_writeQ_size, simu.m_respQ_size, simu.m_tRP, simu.m_tRCD, simu.m_tCAS, simu.m_tBL, simu.m_tRAS, simu.m_tRC, simu.m_tCCD, simu.m_tRRD, simu.m_tFAW, simu.m_tWR, simu.m_tWTR, simu.m_tRTP, simu.m_tCWD, simu.m_scheme, simu.m_tRRDact, simu.m_tRRDpre);

	printf("%u %u %u %6u %llu %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u\n", simu.nv_channel, simu.nv_rank, simu.nv_bank, simu.nv_row, simu.nv_column, simu.nv_readQ_size, simu.nv_writeQ_size, simu.nv_respQ_size, simu.nv_tRP, simu.nv_tRCD, simu.nv_tCAS, simu.nv_tBL, simu.nv_tRAS, simu.nv_tRC, simu.nv_tCCD, simu.nv_tRRD, simu.nv_tFAW, simu.nv_tWR, simu.nv_tWTR, simu.nv_tRTP, simu.nv_tCWD, simu.nv_scheme, simu.nv_tRRDact, simu.nv_tRRDpre);
	
	printf("%llu %u %llu %u %u %u\n", simu.hc_size, simu.hc_way, simu.hc_set, simu.hc_line, simu.hc_type, simu.hc_repl_type);

	printf("\n");
*/
	for (uint32_t n=0; n<n_cores; n++)
	{
		mem_fetch[n]	= false;
		num_instrs[n] = 0;
		optype[n]			= 0;
		thread_id[n]	= 0;
		addr[n]				= 0;
	}	
	
	interval_inst = 0;
	interval_cnt = 0;

	INS_AddInstrumentFunction(Instruction, 0);
	PIN_AddFiniFunction(Fini, 0);
	PIN_AddDetachFunction(DetachProgram, 0);

	gettimeofday(&simu_start, NULL);
	gettimeofday(&tv1, NULL);
	printf("(mtsim++) Starting simulation... ");
	if (limit_instrs == 0)	printf("[MAX INSTR: ∞]\n");
	else										printf("[MAX INSTR: %llu]\n", limit_instrs);
	printf("(mtsim++) \n\n");
	
	PIN_StartProgram();

	return 0;
}
