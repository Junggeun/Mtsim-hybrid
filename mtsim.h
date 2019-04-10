#ifndef __MTSIM_H__
#define __MTSIM_H__

#include <inttypes.h>
#include <stdio.h>

#ifdef DEBUG_PRINT
#define DP(x) x
#else
#define DP(x)
#endif

#define MTSIM_VERSION		0.3

#define MAX_THREAD_NUM	256

#define KB	1024
#define MB	(1024 * KB)
#define GB	(1024 * (uint64_t)MB)

typedef struct simu_config
{
	uint32_t freq;
	uint32_t n_cores;
	uint32_t rob_capacity;
	
	uint64_t l1_size;
	uint32_t l1_way;
	uint32_t l1_set;
	uint32_t l1_line;
	uint32_t l1_tag_lat;
	uint32_t l1_dta_lat;
	uint32_t l1_type;
	uint32_t l1_repl_type;
	uint32_t l1_mshr_size;
	uint32_t l1_txQ_size;
	uint32_t l1_pfQ_size;
	uint32_t l1_banks;
	uint32_t l1_perfect;
	
	uint64_t l2_size;
	uint32_t l2_way;
	uint32_t l2_set;
	uint32_t l2_line;
	uint32_t l2_tag_lat;
	uint32_t l2_dta_lat;
	uint32_t l2_type;
	uint32_t l2_repl_type;
	uint32_t l2_mshr_size;
	uint32_t l2_txQ_size;
	uint32_t l2_pfQ_size;
	uint32_t l2_banks;
	uint32_t l2_perfect;

	uint64_t llc_size;
	uint32_t llc_way;
	uint32_t llc_set;
	uint32_t llc_line;
	uint32_t llc_tag_lat;
	uint32_t llc_dta_lat;
	uint32_t llc_type;
	uint32_t llc_repl_type;
	uint32_t llc_mshr_size;
	uint32_t llc_txQ_size;
	uint32_t llc_pfQ_size;
	uint32_t llc_banks;
	uint32_t llc_perfect;

	uint32_t m_hybrid;	//* 0: homogeneous(single), 1: heterogeneous
	bool		 m_dram;
	bool		 m_nvm;

	uint32_t m_freq;
	uint32_t m_channel;
	uint32_t m_rank;
	uint32_t m_bank;
	uint64_t m_row;
	uint32_t m_column;
	uint32_t m_readQ_size;
	uint32_t m_writeQ_size;
	uint32_t m_respQ_size;
	uint32_t m_tRP;
	uint32_t m_tRCD;
	uint32_t m_tCAS;
	uint32_t m_tBL;
	uint32_t m_tRAS;
	uint32_t m_tRC;
	uint32_t m_tCCD;
	uint32_t m_tRRD;
	uint32_t m_tFAW;
	uint32_t m_tWR;
	uint32_t m_tWTR;
	uint32_t m_tRTP;
	uint32_t m_tCWD;
	uint32_t m_tRRDact;
	uint32_t m_tRRDpre;
	uint32_t m_scheme;
	uint32_t m_page_open;

	uint32_t nv_freq;
	uint32_t nv_channel;
	uint32_t nv_rank;
	uint32_t nv_bank;
	uint64_t nv_row;
	uint32_t nv_column;
	uint32_t nv_readQ_size;
	uint32_t nv_writeQ_size;
	uint32_t nv_respQ_size;
	uint32_t nv_tRP;
	uint32_t nv_tRCD;
	uint32_t nv_tCAS;
	uint32_t nv_tBL;
	uint32_t nv_tRAS;
	uint32_t nv_tRC;
	uint32_t nv_tCCD;
	uint32_t nv_tRRD;
	uint32_t nv_tFAW;
	uint32_t nv_tWR;
	uint32_t nv_tWTR;
	uint32_t nv_tRTP;
	uint32_t nv_tCWD;
	uint32_t nv_tRRDact;
	uint32_t nv_tRRDpre;
	uint32_t nv_scheme;
	uint32_t nv_page_open;

	uint64_t hc_size;
	uint32_t hc_way;
	uint64_t hc_set;
	uint32_t hc_line;
	uint32_t hc_type;
	uint32_t hc_repl_type;

} simu_config_t;

uint32_t u32_log2(uint32_t n);
uint64_t u64_log2(uint64_t n);

void	 mtsim_read_config_file(FILE* _config, simu_config_t* _simu);

double mtsim_print_large_div(uint64_t _val1, uint64_t _val2);
void	 mtsim_print_simu_start();
void	 mtsim_print_mtsim_logo();

#endif 