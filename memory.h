#ifndef __MEMORY_H__
#define __MEMORY_H__

#include <inttypes.h>
#include "block.h"
#include "core.h"

#define MAX_NUM_CHANNELS	4
#define MAX_NUM_RANKS			16
#define MAX_NUM_BANKS			64

#define max(a,b) (((a)>(b))?(a):(b))

//* DRAM Design Space:
//* INPUT:
//*		(1) Nature of Application (Locality, Rand Access)
//*		(2) Organization Parameters (Channel, Rank, Bank, etc.)
//*		(3) Address Mapping Polices (Favors locality or Random)
//*		(4) Txn Scheduling Polices (FCFS, Greedy, etc.)
//*		(5) Hardware Resources (Txn Queue, Bank Queue)
//*		(6) Timing Parameters (CAS, RAS, Pre-Charge Lat)
//* SYSTEM:
//*		DRAM System
//*	OUTPUT:
//*		(1) High Sustainable Bandwidth
//*		(2) Low Average Latency
//*		(3) Low Power Consumption
//*		(4) Fairness (Multiple Agents)

//*	DRAM Basics:
//*		(1) Organization:
//*			Channel >> Rank >> Bank >> Row >> Column
//*		(2) Memory Access Command:
//*			Row Activate <> Column Access <> Pre-Charge
//*		(3) DRAM Latency depends:
//*			Row Hit < Row Closed < Row Conflict

//*	Average Access Latency:
//*		DRAM Latency = A + B + C + D + E {E1, E2, E3} + F
//*		% A:  Delay in Processor Q
//*		% B:  Txn sent to MemCtrl
//*		% C:  Txn -> CMD sequence
//*		% D:  Cmd sent to DRAM
//*		% E1: Requires only CAS
//*		% E2: Requires RAS + CAS
//*		% E3: Needs Pre + RAS + CAS
//*		% F:  Txn sent back to CPU

typedef struct mem_request_packet	mem_req_t;
typedef struct bank_structure			bank_t;
typedef struct memory_ctrl_queue	mc_queue_t;
typedef struct memory_controller	mem_ctrl_t;

typedef struct hybrid_mem_controller	hm_ctrl_t;
typedef struct hmem_request_packet		hmem_req_t;

typedef enum command_t
{
	ACT_CMD,
	COL_READ_CMD,
	COL_WRITE_CMD,
	PRE_CMD,
	REF_CMD,
	NOP
} command_t;

typedef enum
{
	IDLE,
	PRECHARGING,
	REFRESHING,
	ROW_ACTIVE,
} bankstate_t;

typedef enum
{
	ChRaRoBaCo,
	RoBaRaCoCh,
	ChRaBaRoCo,
	RoRaBaCoCh,
	RoCoRaBaCh,
	ChRaRoCoBa,
} addr_scheme_t;

struct bank_structure
{
	bankstate_t		state;
	uint64_t			active_row;
	uint64_t			next_pre;
	uint64_t			next_act;
	uint64_t			next_read;
	uint64_t			next_write;
	uint64_t			next_refresh;
};

struct mem_request_packet
{
	//? general information of memory request packet
	op_type		req_type;
	uint64_t	phys_addr;	//? actual physical address.
	uint64_t	comp_time;
	uint32_t	core_id;
	command_t	next_command;
	bool			cmd_issuable;
	bool			req_served;
	bool			row_buffer_miss;
	//? memory device address::
	uint32_t	channel;
	uint32_t	rank;
	uint32_t	bank;
	uint64_t	row;
	uint32_t	column;
	//? statistics:
	uint64_t	dispatch_time;
	uint64_t	arrival_time;
	uint64_t	latency;	//? dispatch_time - arrival_time (for evaluating avg. queue waiting time.)
	//? misc.:
	req_t*			_c_req;	//? pointer for cache request packet
	mem_req_t*	_next;
	hmem_req_t*	_h_req;
	bool				migration;	//? migration flag (Is this request doing migration from DRAM to NVM?)
};

struct memory_ctrl_queue
{
	uint32_t	capacity;
	uint32_t	n_req;
	mem_req_t	*_head;
	mem_req_t	*_tail;
	mem_ctrl_t	*_mem_ctrl;
};

struct memory_controller
{
	//? general configuration of memory device and memory controller:
	uint32_t		n_channel;
	uint32_t		n_rank;
	uint32_t		n_bank;
	uint64_t		n_row;
	uint32_t		n_column;

	uint64_t		n_size;	//? total memory device's size.

	uint32_t		size_readQ;
	uint32_t		size_writeQ;
	uint32_t		size_respQ;

	addr_scheme_t	scheme;

	//? internal structures for physical memory device::
	bank_t			memory_state[MAX_NUM_CHANNELS][MAX_NUM_RANKS][MAX_NUM_BANKS];
	bool				cmd_issued_cur_cycle[MAX_NUM_CHANNELS];
	bool				cas_issued_cur_cycle[MAX_NUM_CHANNELS][MAX_NUM_RANKS][MAX_NUM_BANKS];
	////? issuables_for_different commands
	bool				cmd_precharge_issuable[MAX_NUM_CHANNELS][MAX_NUM_RANKS][MAX_NUM_BANKS];
	bool				cmd_all_bank_precharge_issuable[MAX_NUM_CHANNELS][MAX_NUM_RANKS];
	bool				cmd_refresh_issuable[MAX_NUM_CHANNELS][MAX_NUM_RANKS];
	//? -------------------------------------------------------------------------------

	//? memory queues:
	mc_queue_t	*_txQ;
	//TODO: should be per-channel basis, but not now.
	mc_queue_t	*_readQ;
	mc_queue_t	*_writeQ;
	mc_queue_t	*_respQ;
	
	//? it this memory module NVM? or DRAM?
	bool		 is_NVM;

	//? memory timing parameters:
	uint32_t tRP;
	uint32_t tRCD;
	uint32_t tCAS;
	uint32_t tBL;
	uint32_t tRAS;
	uint32_t tRC;
	uint32_t tCCD;

	uint32_t tRRD;
	uint32_t tFAW;
	uint32_t tWR;
	uint32_t tWTR;
	uint32_t tRTP;
	uint32_t tCWD;

	//? for NVM device
	uint32_t tRRDact;
	uint32_t tRRDpre;

	//? ownership information::
	proc_t	*_proc;

	//? Statistics::
	uint64_t total_mem_req_cnt;
	uint64_t row_buffer_miss_cnt;
	uint64_t total_waiting_time;

	uint64_t interval_bandwidth;
	uint64_t total_bandwidth;

	long double avg_waiting_time;
};

mc_queue_t*	init_memory_request_queue(mem_ctrl_t* _mem_ctrl, uint32_t capacity);
void				mem_txq_insert_req(mc_queue_t* _queue, mem_req_t* _req);
void				mem_txq_remove_req(mc_queue_t* _queue, mem_req_t* _req);
bool				is_req_queue_full(mc_queue_t*	_queue);
bool				is_req_queue_empty(mc_queue_t* _queue);

mem_ctrl_t*	init_memory_controller_structure(proc_t* _proc, uint32_t n_channel, uint32_t n_rank, uint32_t n_bank, uint64_t n_row, uint32_t n_column, uint32_t size_readQ, uint32_t size_writeQ, uint32_t size_respQ, uint32_t tRP, uint32_t tRCD, uint32_t tCAS, uint32_t tBL, uint32_t tRAS, uint32_t tRC, uint32_t tCCD, uint32_t tRRD, uint32_t tFAW, uint32_t tWR, uint32_t tWTR, uint32_t tRTP, uint32_t tCWD, uint32_t tRRDact, uint32_t tRRDpre, addr_scheme_t scheme);

void				mem_address_scheme(mem_ctrl_t* _ctrl, mem_req_t* _req);

void				memory_operate(mem_ctrl_t* _ctrl, uint64_t _clk);
void				memory_process(mem_ctrl_t* _ctrl, uint64_t _clk);
void				memory_schedule(mem_ctrl_t* _ctrl, uint64_t _clk, uint32_t _channel);

void				update_issuable_commands(mem_ctrl_t* _ctrl, uint32_t _channel, uint64_t _clk);

bool				is_precharge_allowed(mem_ctrl_t* _ctrl, uint32_t _channel, uint32_t _rank, uint32_t _bank, uint64_t _clk);
bool				is_all_bank_precharged_allowed(mem_ctrl_t* _ctrl, uint32_t _channel, uint32_t _rank, uint64_t _clk);
bool				is_activate_allowed(mem_ctrl_t* _ctrl, uint32_t _channel, uint32_t _rank, uint32_t _bank, uint64_t _clk);

void				update_read_queue_commands(mem_ctrl_t* _ctrl, uint32_t _channel, uint64_t _clk);
void				update_write_queue_commands(mem_ctrl_t* _ctrl, uint32_t _channel, uint64_t _clk);

bool				issue_request_command(mem_ctrl_t* _ctrl, mem_req_t* _req, uint64_t _clk);

#endif 