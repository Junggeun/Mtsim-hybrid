#include "mtsim.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

uint32_t u32_log2(uint32_t n)
{
	uint32_t power;
	for (power=0; n>>=1; ++power);
	return power;
}

uint64_t u64_log2(uint64_t n)
{
	uint64_t power;
	for (power=0; n>>=1; ++power);
	return power;
}

double mtsim_print_large_div(uint64_t _val1, uint64_t _val2)
{
	while(_val1>=1000 && _val2>=1000){_val1 /= 10; _val2 /= 10;}
	return (double)_val1 / (double)_val2;
}


typedef enum config_state {IDLE, SYS, CACHE, L1, L1_A, L2, L2_A, L3, L3_A, CORES, CO_CORES, MEM, SIMU, DRAM, NVM, DRAM_A} config_state_t;
typedef enum word_state		{W_NONE, W_SIM, W_SYS, W_CACHES, W_L1, W_L2, W_L3, W_ARRAY, W_TYPE, W_WAYS, W_TAG, W_ARLAT, W_SIZE, W_BANKS, W_CORES, W_FREQ, W_LINE, W_MEM, W_ADDR, W_PAGE, W_ROBSIZE, W_PREFETCHER, W_REPL, W_MSHR, W_TXQ, W_PFQ, W_CHAN, W_RANK, W_BANK, W_ROW, W_COL, W_RDQ, W_WRQ, W_TRP, W_TRCD, W_TCAS, W_TBL, W_TRAS, W_TRC, W_TCCD, W_TRRD, W_TFAW, W_TWR, W_TWTR, W_TRTP, W_TCWD, W_RESPQ, W_PERFECT, W_TRRD_ACT, W_TRRD_PRE, W_HYBRID, W_DRAM, W_NVM, W_ST_SIZE} word_state_t;
typedef enum syntax_state {LEFT_RESV, RIGHT_VALE, RIGHT_STMT, END_STMT} syntax_state_t;

const char word_list[64][16] = {"none", "sim", "sys", "caches", "l1", "l2", "l3", "array", "type", "ways", "tagLatency", "arrayLatency", "size", "banks", "cores", "frequency", "lineSize", "mem", "addrMapping", "closedPage", "robSize", "prefetcher", "replPolicy", "mshr", "txQsize", "pfQsize", "channel", "rank", "bank", "row", "column", "rdQsize", "wrQsize", "tRP", "tRCD", "tCAS", "tBL", "tRAS", "tRC", "tCCD", "tRRD", "tFAW", "tWR", "tWTR", "tRTP", "tCWD", "respQsize", "perfect", "tRRDact", "tRRDpre", "hybrid", "dram", "nvm"};

const char val_list[64][32] = {"False", "True", "DirectMap", "SetAssoc", "FulAssoc",
															 "ChRaRoBaCo", "RoBaRaCoCh", "ChRaBaRoCo", "RoRaBaCoCh",
															 "RoCoRaBaCh", "ChRaRoCoBa", "FIFO", "RAND", "LRU", "NextLine"};

void mtsim_read_config_file(FILE* _config, simu_config_t* _simu)
{
	uint32_t t;
	char cfg_buf[512];
	char str_buf[512];
	uint32_t cnt = 0;
	str_buf[0] = '\0';

	uint32_t line_size;

	bool is_dram, is_nvm;
	is_dram = false; is_nvm = false;

	_simu->l1_banks = 1;	_simu->l2_banks = 1;	_simu->llc_banks = 1;
	_simu->l1_set = 1;	_simu->l2_set = 1;	_simu->llc_set = 1;
	_simu->m_respQ_size = 1;
	
	uint32_t l1_pref, l2_pref, l3_pref;

	config_state_t	cfg_st;
	syntax_state_t	syn_st;
	word_state_t		word_st;

	char prev_char;

	word_st = W_NONE;

	cfg_st = IDLE;
	syn_st = LEFT_RESV;

	bool is_char = false;
	bool comment_flag = false;

	uint64_t val=0;
	
	while(fgets(cfg_buf, 512, _config))
	{
		cnt = 0;
		comment_flag = false;

		for (t=0; t<strlen(cfg_buf); t++)
		{
			if (cfg_buf[t] == '=')
			{
				t++;
				break;
			}
			if (cfg_buf[t] == '}')
			{
				syn_st = END_STMT;
				word_st = W_NONE;
				break;
			}
			else if (cfg_buf[t] == '/' || cfg_buf[t] == '#')
			{
				comment_flag = true;
				word_st = W_NONE;
				break;
			}
			if (cfg_buf[t] != ' ' && cfg_buf[t] != '\t')
				str_buf[cnt++] = cfg_buf[t];
		}
		str_buf[cnt] = '\0';

		if (!comment_flag)
		{
			if (syn_st != END_STMT)
			{
				for (uint32_t l=0; l<W_ST_SIZE; l++)
				{
					if (strcmp(word_list[l], str_buf) == 0)
					{
						word_st = (word_state_t)l;
						break;
					}
				}
			}	

			syn_st = RIGHT_VALE;
			for (;t<strlen(cfg_buf); t++)
			{
				if (cfg_buf[t] == '{')
				{
					syn_st = RIGHT_STMT;
					t++;
					break;
				}
				else if (cfg_buf[t] != ' ')
					break;
			}

			is_char = false;
			cnt = 0;
			if (syn_st == RIGHT_VALE)
			{
				for (;t<strlen(cfg_buf); t++)
				{
					if (cfg_buf[t] == '}')
					{
						syn_st = END_STMT;
						break;
					}
					if (cfg_buf[t] == '"')			is_char = true;
					else if (cfg_buf[t] == ';')	break;
					else if (cfg_buf[t] != ' ')	str_buf[cnt++] = cfg_buf[t];
				}
				str_buf[cnt] = '\0';

				if (is_char)
				{
					//TODO: i < max(W_ST_SIZE, val_list_num)
					for (uint32_t i=0; i<W_ST_SIZE; i++)
					{
						if (strcmp(str_buf, val_list[i]) == 0)
						{
							val = i;
							break;
						}
					}
				}
				else
				{
					char* stopstr;
					val = strtoull(str_buf, &stopstr, 10);
					//printf("%llu\n", val);
					//val = atoi(str_buf);
				}
			}
			
			if (syn_st != END_STMT)
			{
				switch (word_st)
				{
					case W_SIM:	break;
					case W_SYS:			cfg_st = SYS;	syn_st = LEFT_RESV;	break;
					case W_CACHES:	cfg_st = CACHE;	syn_st = LEFT_RESV;	break;
					case W_L1:			cfg_st = L1;	break;
					case W_L2:			cfg_st = L2;	break;
					case W_L3:			cfg_st = L3;	break;
					case W_ARRAY:
					{
						switch (cfg_st)
						{
							case L1:		cfg_st = L1_A;		break;
							case L2:		cfg_st = L2_A;		break;
							case L3:		cfg_st = L3_A;		break;
							case DRAM:	cfg_st = DRAM_A;	break;
						}
						break;
					}
					case W_TYPE:
					{
						switch (cfg_st)
						{
							case L1_A:		_simu->l1_type = val - 2;		break;
							case L2_A:		_simu->l2_type = val - 2;		break;
							case L3_A:		_simu->llc_type = val - 2;	break;
							case DRAM_A:	_simu->hc_type = val - 2;		break;
						}
						break;
					}
					case W_WAYS:
					{
						switch (cfg_st)
						{
							case L1_A:	_simu->l1_way = val;	break;
							case L2_A:	_simu->l2_way = val;	break;
							case L3_A:	_simu->llc_way = val;	break;
							case DRAM_A:	_simu->hc_way = val;	break;
						}
						break;
					}
					case W_TAG:
					{
						switch (cfg_st)
						{
							case L1:	_simu->l1_tag_lat = val; break;
							case L2:	_simu->l2_tag_lat = val; break;
							case L3:	_simu->llc_tag_lat = val;	break;
						}
						break;
					}
					case W_ARLAT:
					{
						switch (cfg_st)
						{
							case L1:	_simu->l1_dta_lat = val; break;
							case L2:	_simu->l2_dta_lat = val; break;
							case L3:	_simu->llc_dta_lat = val; break;
						}
						break;
					}
					case W_SIZE:
					{
						switch (cfg_st)
						{
							case L1:	_simu->l1_size = val; break;
							case L2:	_simu->l2_size	= val; break;
							case L3:	_simu->llc_size = val; break;
							case DRAM: if (!_simu->hc_size > 0) _simu->hc_size = val; break;
						}
						break;
					}
					case W_BANKS:
					{
						switch (cfg_st)
						{
							case L1:	_simu->l1_banks = val; break;
							case L2:	_simu->l2_banks = val; break;
							case L3:	_simu->llc_banks = val; break;
						}
						break;
					}
					case W_CORES:
					{
						switch (cfg_st)
						{
							case SYS:		cfg_st = CORES;	break;
							case CORES:	_simu->n_cores = val;	break;
						}
						break;
					}
					case W_FREQ:
					{
						switch (cfg_st)
						{
							case CORES:	_simu->freq = val;		break;
							case DRAM:	_simu->m_freq = val;	break;
							case NVM:		_simu->nv_freq = val;	break;
						}
						break;
					}
					case W_LINE:
						if (cfg_st == CORES)	line_size = val;
						break;
					case W_MEM:			cfg_st = MEM;											break;
					case W_DRAM:		{cfg_st = DRAM; is_dram = true;}	break;
					case W_NVM:			{cfg_st = NVM;	is_nvm = true;}		break;

					case W_ROBSIZE:	_simu->rob_capacity = val;	break;
					case W_PREFETCHER:
					{
						switch(cfg_st)
						{
							case L1:	l1_pref = val - 14;	break;
							case L2:	l2_pref = val - 14; break;
							case L3:	l3_pref = val - 14; break;
						}
						break;
					}
					case W_REPL:
					{
						switch(cfg_st)
						{
							case L1:	_simu->l1_repl_type = val - 11;		break;
							case L2:	_simu->l2_repl_type = val - 11;		break;
							case L3:	_simu->llc_repl_type = val - 11;	break;
							case DRAM:	_simu->hc_repl_type = val - 11;	break;
						}
						break;
					}
					case W_MSHR:
					{
						switch(cfg_st)
						{
							case L1:	_simu->l1_mshr_size = val;	break;
							case L2:	_simu->l2_mshr_size = val;	break;
							case L3:	_simu->llc_mshr_size = val;	break;
						}
						break;
					}
					case W_TXQ:
					{
						switch(cfg_st)
						{
							case L1:	_simu->l1_txQ_size = val;		break;
							case L2:	_simu->l2_txQ_size = val; 	break;
							case L3:	_simu->llc_txQ_size = val;	break;
						}
						break;
					}
					case W_PFQ:
					{
						switch(cfg_st)
						{
							case L1:	_simu->l1_pfQ_size = val;		break;
							case L2:	_simu->l2_pfQ_size = val;		break;
							case L3:	_simu->llc_pfQ_size = val;	break;
						}
						break;
					}

					case W_HYBRID:	_simu->m_hybrid = val;		break;

					case W_ADDR:	if (cfg_st==DRAM){_simu->m_scheme = val - 5;}	else {_simu->nv_scheme = val - 5;}	break;
					case W_PAGE:	if (cfg_st==DRAM){_simu->m_page_open = val;}	else {_simu->nv_page_open = val;}		break;
					case W_CHAN:	if (cfg_st==DRAM){_simu->m_channel = val;}		else {_simu->nv_channel = val;}			break;
					case W_RANK:	if (cfg_st==DRAM){_simu->m_rank = val;}				else {_simu->nv_rank = val;}				break;
					case W_BANK:	if (cfg_st==DRAM){_simu->m_bank = val;}				else {_simu->nv_bank = val;}				break;
					case W_ROW:		if (cfg_st==DRAM){_simu->m_row = val;}				else {_simu->nv_row = val;}					break;
					case W_COL:		if (cfg_st==DRAM){_simu->m_column = val;}			else {_simu->nv_column = val;}			break;
					case W_RDQ:		if (cfg_st==DRAM){_simu->m_readQ_size = val;}	else {_simu->nv_readQ_size = val;}	break;
					case W_WRQ:		if (cfg_st==DRAM){_simu->m_writeQ_size = val;}	else {_simu->nv_writeQ_size = val;} break;
					case W_RESPQ:	if (cfg_st==DRAM){_simu->m_respQ_size = val;}	else {_simu->nv_respQ_size = val;}	break;
					case W_TRP:		if (cfg_st==DRAM){_simu->m_tRP = val;}				else {_simu->nv_tRP = val;}					break;
					case W_TRCD:	if (cfg_st==DRAM){_simu->m_tRCD = val;}				else {_simu->nv_tRCD = val;}				break;
					case W_TCAS:	if (cfg_st==DRAM){_simu->m_tCAS = val;}				else {_simu->nv_tCAS = val;}				break;
					case W_TBL:		if (cfg_st==DRAM){_simu->m_tBL = val;}				else {_simu->nv_tBL = val;}					break;
					case W_TRAS:	if (cfg_st==DRAM){_simu->m_tRAS = val;}				else {_simu->nv_tRAS = val;}				break;
					case W_TRC:		if (cfg_st==DRAM){_simu->m_tRC = val;}				else {_simu->nv_tRC = val;}					break;
					case W_TCCD:	if (cfg_st==DRAM){_simu->m_tCCD = val;}				else {_simu->nv_tCCD = val;}				break;
					case W_TRRD:	if (cfg_st==DRAM){_simu->m_tRRD = val;}				else {_simu->nv_tRRD = val;}				break;
					case W_TFAW:	if (cfg_st==DRAM){_simu->m_tFAW = val;}				else {_simu->nv_tFAW = val;}				break;
					case W_TWR:		if (cfg_st==DRAM){_simu->m_tWR = val;}				else {_simu->nv_tWR = val;}					break;
					case W_TWTR:	if (cfg_st==DRAM){_simu->m_tWTR = val;}				else {_simu->nv_tWTR = val;}				break;
					case W_TRTP:	if (cfg_st==DRAM){_simu->m_tRTP = val;}				else {_simu->nv_tRTP = val;}				break;
					case W_TCWD:	if (cfg_st==DRAM){_simu->m_tCWD = val;}				else {_simu->nv_tCWD = val;}				break;
					case W_TRRD_ACT:	if (cfg_st==DRAM){_simu->m_tRRDact = val;}	else {_simu->nv_tRRDact = val;}		break;
					case W_TRRD_PRE:	if (cfg_st==DRAM){_simu->m_tRRDpre = val;}	else {_simu->nv_tRRDpre = val;}		break;

					case W_PERFECT:
					{
						switch(cfg_st)
						{
							case L1:	_simu->l1_perfect = val;	break;
							case L2:	_simu->l2_perfect = val;	break;
							case L3:	_simu->llc_perfect = val;	break;
						}
						break;
					}
					default:	break;
				}
				syn_st = LEFT_RESV;
			}
			else
			{
				switch (cfg_st)
				{
					case SYS:
					case SIMU:
						cfg_st = IDLE;
						break;
					case CACHE: case CORES: case MEM:	cfg_st = SYS;	break;
					case L1: case L2: case L3:
						cfg_st = CACHE;
						break;
					case L1_A:			cfg_st = L1;		break;
					case L2_A:			cfg_st = L2;		break;
					case L3_A:			cfg_st = L3;		break;
					case DRAM_A:		cfg_st = DRAM;	break;
					case DRAM:			cfg_st = MEM;		break;
					case NVM:				cfg_st = MEM;		break;
					case CO_CORES:	cfg_st = CORES; break;
				}
				syn_st = LEFT_RESV;
			}
		}
	}
	_simu->l1_line = line_size;
	_simu->l2_line = line_size;
	_simu->llc_line = line_size;

	//TODO: unifying dram/nvm frequencies as the same one?
	uint32_t proc_mem_freq_ratio;
	if (is_dram)
		proc_mem_freq_ratio = _simu->freq / _simu->m_freq;
	else
		proc_mem_freq_ratio = _simu->freq / _simu->nv_freq;	

	_simu->m_tRP *= proc_mem_freq_ratio;
	_simu->m_tRCD *= proc_mem_freq_ratio;
	_simu->m_tCAS *= proc_mem_freq_ratio;
	_simu->m_tBL *= proc_mem_freq_ratio;
	_simu->m_tRAS *= proc_mem_freq_ratio;
	_simu->m_tRC *= proc_mem_freq_ratio;
	_simu->m_tCCD *= proc_mem_freq_ratio;
	_simu->m_tWR *= proc_mem_freq_ratio;
	_simu->m_tRRD *= proc_mem_freq_ratio;
	_simu->m_tRRDact = 0;
	_simu->m_tRRDpre = 0;

	if (_simu->m_hybrid)
	{
		_simu->nv_tRP *= proc_mem_freq_ratio;
		_simu->nv_tRCD *= proc_mem_freq_ratio;
		_simu->nv_tCAS *= proc_mem_freq_ratio;
		_simu->nv_tBL *= proc_mem_freq_ratio;
		_simu->nv_tRAS *= proc_mem_freq_ratio;
		_simu->nv_tRC *= proc_mem_freq_ratio;
		_simu->nv_tCCD *= proc_mem_freq_ratio;
		_simu->nv_tWR *= proc_mem_freq_ratio;
		_simu->nv_tRRD *= proc_mem_freq_ratio;
		_simu->nv_tRRDact *= proc_mem_freq_ratio;
		_simu->nv_tRRDpre *= proc_mem_freq_ratio;
	}

	if (is_dram && !_simu->m_hybrid)
	{
		_simu->nv_tRP = 0;
		_simu->nv_tRCD = 0;
		_simu->nv_tCAS = 0;
		_simu->nv_tBL = 0;
		_simu->nv_tRAS = 0;
		_simu->nv_tRC = 0;
		_simu->nv_tCCD = 0;
		_simu->nv_tWR = 0;
		_simu->nv_tRRD = 0;
		_simu->nv_tRRDact = 0;
		_simu->nv_tRRDpre = 0;
	}
	
	//printf("line_size: %u\n", line_size);
	//printf("freq: %u, rob_capacity: %u, num_cores: %u\n", _simu->freq, _simu->rob_capacity, _simu->n_cores);

	_simu->m_dram = is_dram;
	_simu->m_nvm = is_nvm;

	fclose(_config);
}

void mtsim_print_mtsim_logo()
{
	printf(" \n");
	printf("======================================================================================\n");
	/*printf("                               _       _                       \n");
	printf("                     _ __ ___ | |_ ___(_)_ __ ___    _     _   \n");
	printf("                    | '_ ` _ \\| __/ __| | '_ ` _ \\ _| |_ _| |_ \n");
	printf("                    | | | | | | |_\\__ \\ | | | | | |_   _|_   _|\n");
	printf("                    |_| |_| |_|\\__|___/_|_| |_| |_| |_|   |_|  \n");
	*/
	printf("             ███╗   ███╗████████╗███████╗██╗███╗   ███╗   ██╗      ██╗\n");
	printf("             ████╗ ████║╚══██╔══╝██╔════╝██║████╗ ████║   ██║      ██║\n");
	printf("             ██╔████╔██║   ██║   ███████╗██║██╔████╔██║████████╗████████╗\n");
	printf("             ██║╚██╔╝██║   ██║   ╚════██║██║██║╚██╔╝██║╚══██╔══╝╚══██╔══╝\n");
	printf("             ██║ ╚═╝ ██║   ██║   ███████║██║██║ ╚═╝ ██║   ██║      ██║\n");
	printf("             ╚═╝     ╚═╝   ╚═╝   ╚══════╝╚═╝╚═╝     ╚═╝   ╚═╝      ╚═╝\n");
	printf("======================================================================================\n");
	printf("(mtsim++) mtsim++ v0.4, developed by Junggeun Kim (junggeun@yonsei.ac.kr)\n");
	printf("(mtsim++) since: 2018.09.01. \n");
	printf("(mtsim++) mtsim++: a cycle accurate cache/main memory simulator with Pin tool.\n");
	printf("(mtsim++) ** MULTI-THREAD SIMULATOR WITH DETAILED CACHE AND MEMORY MODELS **\n");
	printf("(mtsim++) \n");
}
