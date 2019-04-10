#ifndef __CORE_H__
#define __CORE_H__

#include "block.h"
#include "cache.h"
#include "memory.h"
#include "hybrid_mem.h"

typedef struct hybrid_mem_controller hm_ctrl_t;

//* Execution Engine configuration::
#define MAX_RETIRE			6		//? 6-wide superscalar pipeline
//#define PIPELINE_LENGTH	22	//? 22-stage pipeline
#define PIPELINE_LENGTH	5		//TODO: fix it!
#define FETCH_WIDTH			4		//? instruction fetch width (4 ops per cycle)

//#define PROC_MEM_FREQ_RATIO	3	//? 2.4Ghz vs 800Mhz

//typedef struct core_structure				core_t;
//typedef struct processor_structure	proc_t;

struct core_structure
{
	uint32_t		freq;
	uint32_t		rob_capacity;
	rob_t*			_rob;
	proc_t*			_proc;
	uint32_t		core_id;

	cache_t*		_l1;
	cache_t*		_l2;
	cache_t*		_l3;

	//? stats::
	uint64_t		n_total_instr;
	uint64_t		n_non_mem_instr;
	uint64_t		interval_l1_miss_cnt;
	uint64_t		interval_l2_miss_cnt;

	//? CPI stats::
	uint64_t		cpi_l1c_exec_time;
	uint64_t		cpi_l2c_exec_time;

	uint64_t		cpi_ins_exec_time;
};

struct processor_structure
{
	uint32_t		freq;
	uint64_t		global_clk;
	uint32_t		n_cores;
	uint32_t		rob_capacity;
	core_t			**core;

	cache_t*		_llc;	//? shared last level cache (Shared-LLC)
	uint64_t		interval_mpki;
	uint64_t		dram_busy;

	//mem_ctrl_t	*_mem_ctrl;
	bool				hybrid;
	bool				is_dram;
	bool				is_nvm;
	struct memory_controller 			*_mem_ctrl;
	struct hybrid_mem_controller	*_hmem_ctrl;

	//? CPI stats::
	uint64_t		cpi_llc_exec_time;
	uint64_t		cpi_mem_exec_time;
};

rob_t*	init_rob_structure(uint32_t core_id, uint32_t capacity);
void		rob_enqueue(rob_t* _rob, op_t* _new_op);
void		rob_dequeue(rob_t* _rob);
bool		is_rob_empty(rob_t* _rob);
bool		is_rob_full(rob_t* _rob);

core_t*
init_core_structure
(
	proc_t*  _proc,
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
);

proc_t*
init_processor_structure
(
	uint32_t freq,
	uint32_t n_cores,
	uint32_t rob_capacity,
	
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
	uint32_t l2_perfect,

	uint64_t llc_size,
	uint32_t llc_way,
	uint32_t llc_set,
	uint32_t llc_line,
	uint32_t llc_tag_lat,
	uint32_t llc_dta_lat,
	uint32_t llc_type,
	uint32_t llc_repl_type,
	uint32_t llc_mshr_size,
	uint32_t llc_txQ_size,
	uint32_t llc_pfQ_size,
	uint32_t llc_banks,
	uint32_t llc_perfect,

	uint32_t m_hybrid,
	bool		 m_dram,
	bool		 m_nvm,

	uint32_t m_channel,
	uint32_t m_rank,
	uint32_t m_bank,
	uint64_t m_row,
	uint32_t m_column,
	uint32_t m_readQ_size,
	uint32_t m_writeQ_size,
	uint32_t m_respQ_size,
	uint32_t m_tRP,
	uint32_t m_tRCD,
	uint32_t m_tCAS,
	uint32_t m_tBL,
	uint32_t m_tRAS,
	uint32_t m_tRC,
	uint32_t m_tCCD,
	uint32_t m_tRRD,
	uint32_t m_tFAW,
	uint32_t m_tWR,
	uint32_t m_tWTR,
	uint32_t m_tRTP,
	uint32_t m_tCWD,
	uint32_t m_scheme,
	uint32_t m_tRRDact,
	uint32_t m_tRRDpre,

	uint32_t nv_channel,
	uint32_t nv_rank,
	uint32_t nv_bank,
	uint64_t nv_row,
	uint32_t nv_column,
	uint32_t nv_readQ_size,
	uint32_t nv_writeQ_size,
	uint32_t nv_respQ_size,
	uint32_t nv_tRP,
	uint32_t nv_tRCD,
	uint32_t nv_tCAS,
	uint32_t nv_tBL,
	uint32_t nv_tRAS,
	uint32_t nv_tRC,
	uint32_t nv_tCCD,
	uint32_t nv_tRRD,
	uint32_t nv_tFAW,
	uint32_t nv_tWR,
	uint32_t nv_tWTR,
	uint32_t nv_tRTP,
	uint32_t nv_tCWD,
	uint32_t nv_scheme,
	uint32_t nv_tRRDact,
	uint32_t nv_tRRDpre,

	uint64_t hc_size,
	uint32_t hc_way,
	uint64_t hc_set,
	uint32_t hc_line,
	uint32_t hc_type,
	uint32_t hc_repl_type
);

void free_core_structure(core_t* _core);
void free_processor_structure(proc_t* _proc);

#endif 