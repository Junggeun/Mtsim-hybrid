#include "core.h"
#include <malloc.h>
#include <string.h>
#include <stdlib.h>
#include "mtsim.h"
#include <assert.h>
#include "hybrid_mem.h"

core_t*
init_core_structure
(
	proc_t* _proc,
	uint32_t freq,
	uint32_t rob_capacity,
	uint32_t core_id,
	cache_t* _llc,
	uint32_t n_cores,
	uint64_t l1_size,
	uint32_t l1_way,
	uint32_t l1_set,
	uint32_t l1_line,
	uint32_t l1_tag_lat,
	uint32_t l1_dta_lat,
	uint32_t l1_type,
	uint32_t l1_repl_type,
	uint32_t l1_mshr_size,
	uint32_t l1_txQ_size,
	uint32_t l1_pfQ_size,
	uint32_t l1_banks,
	uint32_t l1_perfect,
	uint64_t l2_size,
	uint32_t l2_way,
	uint32_t l2_set,
	uint32_t l2_line,
	uint32_t l2_tag_lat,
	uint32_t l2_dta_lat,
	uint32_t l2_type,
	uint32_t l2_repl_type,
	uint32_t l2_mshr_size,
	uint32_t l2_txQ_size,
	uint32_t l2_pfQ_size,
	uint32_t l2_banks,
	uint32_t l2_perfect
)
{
	core_t* new_core = (core_t*)malloc(sizeof(core_t));

	new_core->freq = freq;
	new_core->rob_capacity = rob_capacity;
	new_core->_rob = init_rob_structure(core_id, rob_capacity);
	new_core->_proc = _proc;	
	new_core->core_id = core_id;

	new_core->_l1 = init_cache_structure(l1_size, l1_way, l1_set, l1_line, l1_tag_lat, l1_dta_lat, (cache_map_t)l1_type, (repl_t)l1_repl_type, l1_mshr_size, l1_txQ_size, l1_pfQ_size, l1_banks, true, false, (l1_banks>1), (l1_perfect==1), new_core->_rob, new_core, _proc, n_cores);
	new_core->_l2 = init_cache_structure(l2_size, l2_way, l2_set, l2_line, l2_tag_lat, l2_dta_lat, (cache_map_t)l2_type, (repl_t)l2_repl_type, l2_mshr_size, l2_txQ_size, l2_pfQ_size, l2_banks, false, false, (l2_banks>1), (l2_perfect==1), new_core->_rob, new_core, _proc, n_cores);
	new_core->_l3 = _llc;

	new_core->n_total_instr = 0;
	new_core->n_non_mem_instr = 0;

	connect_caches(new_core->_l2, new_core->_l1);
	connect_caches(new_core->_l3, new_core->_l2);

	//DP(
	printf("(mtsim++) ++ Re-Order-Buffer size: %3u (entries) \t\t ... [OK]\n", rob_capacity);
	//)
	char size_type[4][4] = {"B", "KiB", "MiB", "GiB"};
	char repl_type[3][5] = {"FIFO", "RAND", "LRU"};
	char ca_type[4][32] = {"directMapped", "setAssoc", "fullyAssoc"};
	char str_perfect[2][10] = {"REALISTIC", "PERFECT"};
	uint32_t trans_size = new_core->_l1->n_size;
	uint32_t degree = 0;
	while (trans_size >= 1024){trans_size /= 1024;degree++;}
	printf("(mtsim++) ++ L1-cache: \t\t\t\t\t\t ... [OK]\n");
	printf("(mtsim++)    + size:    %3u(%s), way: %u, set: %u\n", trans_size, size_type[degree], new_core->_l1->n_way, new_core->_l1->n_set);
	printf("(mtsim++)    + latency: tag - %u, array - %u (cycles)\n", new_core->_l1->lat_tag, new_core->_l1->lat_data);
	printf("(mtsim++)    + type:    %s | %s\n", ca_type[new_core->_l1->map_type], repl_type[new_core->_l1->repl_type]);
	printf("(mtsim++)    + txQ: %3u, pfQ: %3u, mshr: %3u\n", new_core->_l1->_txQ->capacity, new_core->_l1->_pfQ->capacity, new_core->_l1->_mshr->capacity);
	printf("(mtsim++)    + isFirst: %u | isLast: %u | hasBank: %u\n", new_core->_l1->is_first, new_core->_l1->is_last, new_core->_l1->has_banks);
	printf("(mtsim++)    + isPerfect: %s\n", str_perfect[(new_core->_l1->is_perfect)?1:0]);
	printf("(mtsim++) \n");
	
	trans_size = new_core->_l2->n_size;
	degree = 0;
	while (trans_size >= 1024){trans_size /= 1024;degree++;}
	printf("(mtsim++) ++ L2-cache: \t\t\t\t\t\t ... [OK]\n");
	printf("(mtsim++)    + size:    %3u(%s), way: %u, set: %u\n", trans_size, size_type[degree], new_core->_l2->n_way, new_core->_l2->n_set);
	printf("(mtsim++)    + latency: tag - %u, array - %u (cycles)\n", new_core->_l2->lat_tag, new_core->_l2->lat_data);
	printf("(mtsim++)    + type:    %s | %s\n", ca_type[new_core->_l2->map_type], repl_type[new_core->_l2->repl_type]);
	printf("(mtsim++)    + txQ: %3u, pfQ: %3u, mshr: %3u\n", new_core->_l2->_txQ->capacity, new_core->_l2->_pfQ->capacity, new_core->_l2->_mshr->capacity);
	printf("(mtsim++)    + isFirst: %u | isLast: %u | hasBank: %u\n", new_core->_l2->is_first, new_core->_l2->is_last, new_core->_l2->has_banks);
	printf("(mtsim++)    + isPerfect: %s\n", str_perfect[(new_core->_l2->is_perfect)?1:0]);
	printf("(mtsim++) \n");

	new_core->interval_l1_miss_cnt = 0;
	new_core->interval_l2_miss_cnt = 0;

	new_core->cpi_l1c_exec_time = 0;
	new_core->cpi_l2c_exec_time = 0;

	new_core->cpi_ins_exec_time = 0;

	return new_core;
}

proc_t*
init_processor_structure
(
	uint32_t freq, uint32_t n_cores, uint32_t rob_capacity,\
	uint64_t l1_size,	uint32_t l1_way, uint32_t l1_set, uint32_t l1_line,\
	uint32_t l1_tag_lat, uint32_t l1_dta_lat, uint32_t l1_type, uint32_t l1_repl_type,\
	uint32_t l1_mshr_size, uint32_t l1_txQ_size, uint32_t l1_pfQ_size, uint32_t l1_banks, uint32_t l1_perfect,\
	uint64_t l2_size, uint32_t l2_way, uint32_t l2_set, uint32_t l2_line,\
	uint32_t l2_tag_lat, uint32_t l2_dta_lat, uint32_t l2_type, uint32_t l2_repl_type,\
	uint32_t l2_mshr_size, uint32_t l2_txQ_size, uint32_t l2_pfQ_size, uint32_t l2_banks, uint32_t l2_perfect,\
	uint64_t llc_size, uint32_t llc_way, uint32_t llc_set, uint32_t llc_line,\
	uint32_t llc_tag_lat, uint32_t llc_dta_lat, uint32_t llc_type, uint32_t llc_repl_type,\
	uint32_t llc_mshr_size, uint32_t llc_txQ_size, uint32_t llc_pfQ_size, uint32_t llc_banks, uint32_t llc_perfect,\
	uint32_t m_hybrid, bool m_dram, bool m_nvm,
	uint32_t m_channel, uint32_t m_rank, uint32_t m_bank, uint64_t m_row,\
	uint32_t m_column, uint32_t m_readQ_size, uint32_t m_writeQ_size, uint32_t m_respQ_size,\
	uint32_t m_tRP, uint32_t m_tRCD, uint32_t m_tCAS, uint32_t m_tBL, uint32_t m_tRAS,\
	uint32_t m_tRC, uint32_t m_tCCD, uint32_t m_tRRD, uint32_t m_tFAW, uint32_t m_tWR, uint32_t m_tWTR, uint32_t m_tRTP, uint32_t m_tCWD, uint32_t m_scheme, uint32_t m_tRRDact, uint32_t m_tRRDpre, \
	uint32_t nv_channel, uint32_t nv_rank, uint32_t nv_bank, uint64_t nv_row, \
	uint32_t nv_column, uint32_t nv_readQ_size, uint32_t nv_writeQ_size, uint32_t nv_respQ_size,\
	uint32_t nv_tRP, uint32_t nv_tRCD, uint32_t nv_tCAS, uint32_t nv_tBL, uint32_t nv_tRAS,\
	uint32_t nv_tRC, uint32_t nv_tCCD, uint32_t nv_tRRD, uint32_t nv_tFAW, uint32_t nv_tWR, uint32_t nv_tWTR, uint32_t nv_tRTP, uint32_t nv_tCWD, uint32_t nv_scheme, uint32_t nv_tRRDact, uint32_t nv_tRRDpre,\
	uint64_t hc_size, uint32_t hc_way, uint64_t hc_set, uint32_t hc_line, uint32_t hc_type, uint32_t hc_repl_type
)
{
	proc_t*	new_proc = (proc_t*)malloc(sizeof(proc_t));

	DP(
		printf("(mtsim++) Processor is going to be initialized.\n");
	)

	new_proc->freq = freq;
	new_proc->n_cores = n_cores;
	new_proc->rob_capacity = rob_capacity;
	new_proc->global_clk = 0;

	printf("(mtsim++) \n");
	printf("(mtsim++) Initializing Processor...\n");
	new_proc->_llc = init_cache_structure(llc_size, llc_way, llc_set, llc_line, llc_tag_lat, llc_dta_lat, (cache_map_t)llc_type, (repl_t)llc_repl_type, llc_mshr_size, llc_txQ_size, llc_pfQ_size, llc_banks, false, true, (llc_banks>1), (llc_perfect==1), NULL, NULL, new_proc, n_cores);

	printf("(mtsim++) ++ # of cores: %3u\n", n_cores);
	printf("(mtsim++) ++ Processor frequency: %4.2lf (GHz)\n", (double)new_proc->freq/1000);
	printf("(mtsim++) ++ Rob size (common):   %4u (entries)\n", new_proc->rob_capacity);

	char size_type[4][4] = {"B", "KiB", "MiB", "GiB"};
	char repl_type[3][5] = {"FIFO", "RAND", "LRU"};
	char ca_type[4][32] = {"directMapped", "setAssoc", "fullyAssoc"};
	uint32_t trans_size = new_proc->_llc->n_size;
	uint32_t degree = 0;
	while (trans_size >= 1024){trans_size /= 1024;degree++;}
	printf("(mtsim++) ++ LLC-cache: \t\t\t\t\t ... [OK]\n");
	printf("(mtsim++)    + size:    %3u(%s), way: %u, set: %u\n", trans_size, size_type[degree], new_proc->_llc->n_way, new_proc->_llc->n_set);
	printf("(mtsim++)    + latency: tag - %u, array - %u (cycles)\n",new_proc->_llc->lat_tag, new_proc->_llc->lat_data);
	printf("(mtsim++)    + type:    %s | %s\n", ca_type[new_proc->_llc->map_type], repl_type[new_proc->_llc->repl_type]);
	printf("(mtsim++)    + txQ: %3u, pfQ: %3u, mshr: %3u\n", new_proc->_llc->_txQ->capacity, new_proc->_llc->_pfQ->capacity, new_proc->_llc->_mshr->capacity);
	printf("(mtsim++)    + isFirst: %u | isLast: %u | hasBank: %u\n", new_proc->_llc->is_first, new_proc->_llc->is_last, new_proc->_llc->has_banks);
	char str_perfect[2][10] = {"REALISTIC", "PERFECT"};
	printf("(mtsim++)    + isPerfect: %s\n", str_perfect[(new_proc->_llc->is_perfect)?1:0]);
	
	new_proc->core = (core_t**)malloc(sizeof(core_t*) * n_cores);
	for (uint32_t n = 0; n < n_cores; n++)
	{
		printf("(mtsim++) \n");
		printf("(mtsim++) Initializing core #%u: \n", n);
		new_proc->core[n] = init_core_structure(new_proc, freq, rob_capacity, n, new_proc->_llc, n_cores, l1_size, l1_way, l1_set, l1_line, l1_tag_lat, l1_dta_lat, l1_type, l1_repl_type, l1_mshr_size, l1_txQ_size, l1_pfQ_size, l1_banks, l1_perfect, l2_size, l2_way, l2_set, l2_line, l2_tag_lat, l2_dta_lat, l2_type, l2_repl_type, l2_mshr_size, l2_txQ_size, l2_pfQ_size, l2_banks, l2_perfect);
		printf("(mtsim++) core #%u was successfully initialized.\t\t\t ... [OK]\n", n);
		printf("(mtsim++) \n");
	}
	
	cache_t** l2_list = (cache_t**)malloc(sizeof(cache_t*) * n_cores);
	for (uint32_t n=0; n<n_cores; n++)
		l2_list[n] = new_proc->core[n]->_l2;
	connect_llc(new_proc->_llc, n_cores, l2_list);
	free(l2_list);

	new_proc->interval_mpki = 0;
	new_proc->dram_busy = 0;

	new_proc->hybrid = m_hybrid;
	new_proc->is_dram = m_dram;
	new_proc->is_nvm = m_nvm;

	printf("(mtsim++) Initializing Main memory system...\n");

	if (m_hybrid)
	{
		new_proc->_hmem_ctrl = init_hybrid_memory_controller(64, hc_size, hc_way, hc_set, hc_line, (cache_map_t)hc_type, (repl_t)hc_repl_type);
		new_proc->_hmem_ctrl->_dram_ctrl = init_memory_controller_structure(new_proc, m_channel, m_rank, m_bank, m_row, m_column, m_readQ_size, m_writeQ_size, m_respQ_size, m_tRP, m_tRCD, m_tCAS, m_tBL, m_tRAS, m_tRC, m_tCCD, m_tRRD, m_tFAW, m_tWR, m_tWTR, m_tRTP, m_tCWD, m_tRRDact, m_tRRDpre, (addr_scheme_t)m_scheme);
		new_proc->_hmem_ctrl->_nvm_ctrl = init_memory_controller_structure(new_proc, nv_channel, nv_rank, nv_bank, nv_row, nv_column, nv_readQ_size, nv_writeQ_size, nv_respQ_size, nv_tRP, nv_tRCD, nv_tCAS, nv_tBL, nv_tRAS, nv_tRC, nv_tCCD, nv_tRRD, nv_tFAW, nv_tWR, nv_tWTR, nv_tRTP, nv_tCWD, nv_tRRDact, nv_tRRDpre, (addr_scheme_t)nv_scheme);
		printf("(mtsim++) ++ Hybrid Main Memory module: DRAM + NVM\n");
	}
	else
	{
		if (m_dram)
			new_proc->_mem_ctrl = init_memory_controller_structure(new_proc, m_channel, m_rank, m_bank, m_row, m_column, m_readQ_size, m_writeQ_size, m_respQ_size, m_tRP, m_tRCD, m_tCAS, m_tBL, m_tRAS, m_tRC, m_tCCD, m_tRRD, m_tFAW, m_tWR, m_tWTR, m_tRTP, m_tCWD, m_tRRDact, m_tRRDpre, (addr_scheme_t)m_scheme);
		else
			new_proc->_mem_ctrl = init_memory_controller_structure(new_proc, nv_channel, nv_rank, nv_bank, nv_row, nv_column, nv_readQ_size, nv_writeQ_size, nv_respQ_size, nv_tRP, nv_tRCD, nv_tCAS, nv_tBL, nv_tRAS, nv_tRC, nv_tCCD, nv_tRRD, nv_tFAW, nv_tWR, nv_tWTR, nv_tRTP, nv_tCWD, nv_tRRDact, nv_tRRDpre, (addr_scheme_t)nv_scheme);
		printf("(mtsim++) ++ %s Main Memory module\n", (m_dram)?"DRAM":"NVM");
	}
	printf("(mtsim++)  \n");

	DP(
		printf("(mtsim++) Processor was successfully initialized with %u cores.\n", n_cores);
	)	

	if (n_cores > 1)
		printf("(mtsim++) Processor was successfully initialized with %u cores.\t ... [OK]\n", n_cores);
	else
		printf("(mtsim++) Processor was successfully initialized with %u core.\t ... [OK]\n", n_cores);
	printf("(mtsim++) \n");

	new_proc->cpi_llc_exec_time = 0;
	new_proc->cpi_mem_exec_time = 0;

	return new_proc;
}

//TODO: complete this func
void
free_core_structure(core_t* _core)
{
	free(_core->_l1);
	free(_core->_l2);
	free(_core->_rob->_op);
	free(_core->_rob);
	free(_core);
}

void
free_processor_structure(proc_t* _proc)
{
	for (uint32_t n = 0; n < _proc->n_cores; n++)
	{
		free_core_structure(_proc->core[n]);
	}
}

rob_t*
init_rob_structure(uint32_t core_id, uint32_t capacity)
{
	rob_t* new_rob = (rob_t*)malloc(sizeof(rob_t));
	
	new_rob->capacity = capacity;
	new_rob->core_id = core_id;
	new_rob->n_entry = 0;
	new_rob->_front = 0;
	new_rob->_rear = 0;
	new_rob->_op = (op_t*)malloc(sizeof(op_t) * capacity);

	return new_rob;
}

void
rob_enqueue(rob_t* _rob, op_t* _new_op)
{
	assert(!is_rob_full(_rob));
	memcpy(&(_rob->_op[_rob->_rear]), _new_op, sizeof(op_t));
	_rob->_rear++;
	_rob->n_entry++;
	if (_rob->_rear == _rob->capacity)
		_rob->_rear = 0;
}

void
rob_dequeue(rob_t* _rob)
{
	assert(!is_rob_empty(_rob));
	_rob->_front++;
	_rob->n_entry--;
	if (_rob->_front == _rob->capacity)
		_rob->_front = 0;
}

bool
is_rob_empty(rob_t* _rob)
{
	return (_rob->n_entry == 0);
}

bool
is_rob_full(rob_t* _rob)
{
	return (_rob->n_entry == _rob->capacity);
}