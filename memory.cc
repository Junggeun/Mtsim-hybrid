#include "memory.h"
#include "mtsim.h"

#include <malloc.h>
#include <assert.h>
#include <string.h>

mc_queue_t*
init_memory_request_queue(mem_ctrl_t* _mem_ctrl, uint32_t capacity)
{
	assert(capacity > 0);
	mc_queue_t*	new_queue = (mc_queue_t*)malloc(sizeof(mc_queue_t));
	new_queue->capacity = capacity;
	new_queue->_mem_ctrl = _mem_ctrl;
	new_queue->n_req = 0;
	new_queue->_head = NULL;
	new_queue->_tail = NULL;
	return new_queue;
}

void
mem_txq_insert_req(mc_queue_t* _queue, mem_req_t* _req)
{
	mem_req_t* new_req = (mem_req_t*)malloc(sizeof(mem_req_t));
	memcpy(new_req, _req, sizeof(mem_req_t));
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
	mem_address_scheme(_queue->_mem_ctrl, new_req);
	_queue->_mem_ctrl->total_mem_req_cnt++;
}

void
mem_txq_remove_req(mc_queue_t* _queue, mem_req_t* _req)
{
	mem_req_t *it;
	mem_req_t *prev, *next;

	_queue->_mem_ctrl->total_waiting_time += (_req->arrival_time - _req->dispatch_time);

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
is_req_queue_full(mc_queue_t* _queue)
{
	return (_queue->capacity == _queue->n_req);
}

bool
is_req_queue_empty(mc_queue_t* _queue)
{
	return (_queue->n_req == 0);
}

mem_ctrl_t*
init_memory_controller_structure
(
	proc_t* _proc,
	uint32_t n_channel,
	uint32_t n_rank,
	uint32_t n_bank,
	uint64_t n_row,
	uint32_t n_column,
	uint32_t size_readQ,
	uint32_t size_writeQ,
	uint32_t size_respQ,
	uint32_t tRP,
	uint32_t tRCD,
	uint32_t tCAS,
	uint32_t tBL,
	uint32_t tRAS,
	uint32_t tRC,
	uint32_t tCCD,
	uint32_t tRRD,
	uint32_t tFAW,
	uint32_t tWR,
	uint32_t tWTR,
	uint32_t tRTP,
	uint32_t tCWD,
	uint32_t tRRDact,
	uint32_t tRRDpre,
	addr_scheme_t scheme
)
{
	mem_ctrl_t* new_mc = (mem_ctrl_t*)malloc(sizeof(mem_ctrl_t));
	new_mc->_proc = _proc;

	new_mc->n_channel = n_channel;
	new_mc->n_rank = n_rank;
	new_mc->n_bank = n_bank;
	new_mc->n_row = n_row;
	new_mc->n_column = n_column;

	new_mc->n_size = n_channel * n_rank * n_bank * n_row * n_column * CACHE_LINE_SIZE;
	
	new_mc->size_readQ = size_readQ;
	new_mc->size_writeQ = size_writeQ;
	new_mc->size_respQ = size_respQ;
	
	new_mc->_readQ = init_memory_request_queue(new_mc, size_readQ);
	new_mc->_writeQ = init_memory_request_queue(new_mc, size_writeQ);
	new_mc->_respQ = init_memory_request_queue(new_mc, size_respQ);

	//?temp
	new_mc->_txQ = init_memory_request_queue(new_mc, size_readQ);

	new_mc->tRCD = tRCD;
	new_mc->tRP = tRP;
	new_mc->tCAS = tCAS;
	new_mc->tBL = tBL;
	new_mc->tRAS = tRAS;
	new_mc->tRC = tRC;
	new_mc->tCCD = tCCD;

	new_mc->tRRD = tRRD;
	new_mc->tFAW = tFAW;
	new_mc->tWR = tWR;
	new_mc->tWTR = tWTR;
	new_mc->tRTP = tRTP;
	new_mc->tCWD = tCWD;

	new_mc->tRRDact = tRRDact;
	new_mc->tRRDpre = tRRDpre;

	new_mc->scheme = scheme;

	for (uint32_t i=0; i<MAX_NUM_CHANNELS; i++)
	{
		new_mc->cmd_issued_cur_cycle[i] = false;

		for (uint32_t j=0; j<MAX_NUM_RANKS; j++)
		{
			new_mc->cmd_all_bank_precharge_issuable[i][j] = false;
			new_mc->cmd_refresh_issuable[i][j] = false;
			for (uint32_t k=0; k<MAX_NUM_BANKS; k++)
			{
				new_mc->cas_issued_cur_cycle[i][j][k]			= false;
				new_mc->cmd_precharge_issuable[i][j][k]		= false;
				new_mc->memory_state[i][j][k].state				= IDLE;
				new_mc->memory_state[i][j][k].active_row	= UINT64_MAX;
				new_mc->memory_state[i][j][k].next_pre		= 0;	//TODO: UINT64_MAX
				new_mc->memory_state[i][j][k].next_act		= 0;
				new_mc->memory_state[i][j][k].next_read		= 0;
				new_mc->memory_state[i][j][k].next_write	= 0;
				new_mc->memory_state[i][j][k].next_read		= 0;
			}
		}
	}
	
	new_mc->total_mem_req_cnt = 0;
	new_mc->row_buffer_miss_cnt = 0;
	new_mc->total_mem_req_cnt = 0;
	new_mc->avg_waiting_time = 0.0;

	new_mc->total_bandwidth = 0;
	new_mc->interval_bandwidth = 0;

	return new_mc;
}

void
mem_address_scheme(mem_ctrl_t* _ctrl, mem_req_t* _req)
{
	uint64_t phys_addr = _req->phys_addr;
	uint64_t temp_addr1, temp_addr2;
	uint64_t part_addr;

	uint32_t channel_bit_width	= u32_log2(_ctrl->n_channel);
	uint32_t rank_bit_width			= u32_log2(_ctrl->n_rank);
	uint32_t bank_bit_width			= u32_log2(_ctrl->n_bank);
	uint32_t row_bit_width			= (uint32_t)u64_log2(_ctrl->n_row);
	uint32_t col_bit_width			= u32_log2(_ctrl->n_column);
	uint32_t byteoffset_width		= u32_log2(CACHE_LINE_SIZE);

	part_addr = phys_addr;
	part_addr = phys_addr >> byteoffset_width;	//? strip out the cache_offset

	switch(_ctrl->scheme)
	{
		case ChRaRoBaCo:
		{
			temp_addr2		= part_addr;
			part_addr			= part_addr >> col_bit_width;
			temp_addr1		= part_addr << col_bit_width;
			_req->column	= temp_addr1 ^ temp_addr2;
			
			temp_addr2		= part_addr;
			part_addr			= part_addr >> bank_bit_width;
			temp_addr1		= part_addr << bank_bit_width;
			_req->bank		= temp_addr1 ^ temp_addr2;

			temp_addr2		= part_addr;
			part_addr			= part_addr >> row_bit_width;
			temp_addr1		= part_addr << row_bit_width;
			_req->row			= temp_addr1 ^ temp_addr2;

			temp_addr2		= part_addr;
			part_addr			= part_addr >> rank_bit_width;
			temp_addr1		= part_addr << rank_bit_width;
			_req->rank		= temp_addr1 ^ temp_addr2;

			temp_addr2		= part_addr;
			part_addr			= part_addr >> channel_bit_width;
			temp_addr1		= part_addr << channel_bit_width;
			_req->channel	= temp_addr1 ^ temp_addr2;

			break;
		}
		case RoBaRaCoCh:
		{
			temp_addr2		= part_addr;
			part_addr			= part_addr >> channel_bit_width;
			temp_addr1		= part_addr << channel_bit_width;
			_req->channel	= temp_addr1 ^ temp_addr2;

			temp_addr2		= part_addr;
			part_addr			= part_addr >> col_bit_width;
			temp_addr1		= part_addr << col_bit_width;
			_req->column	= temp_addr1 ^ temp_addr2;

			temp_addr2		= part_addr;
			part_addr			= part_addr >> rank_bit_width;
			temp_addr1		= part_addr << rank_bit_width;
			_req->rank		= temp_addr1 ^ temp_addr2;

			temp_addr2		= part_addr;
			part_addr			= part_addr >> bank_bit_width;
			temp_addr1		= part_addr << bank_bit_width;
			_req->bank		= temp_addr1 ^ temp_addr2;

			temp_addr2		= part_addr;
			part_addr			= part_addr >> row_bit_width;
			temp_addr1		= part_addr << row_bit_width;
			_req->row			= temp_addr1 ^ temp_addr2;

			break;
		}
		case ChRaBaRoCo:
		{
			temp_addr2		= part_addr;
			part_addr			= part_addr >> col_bit_width;
			temp_addr1		= part_addr << col_bit_width;
			_req->column	= temp_addr1 ^ temp_addr2;

			temp_addr2		= part_addr;
			part_addr			= part_addr >> row_bit_width;
			temp_addr1		= part_addr << row_bit_width;
			_req->row			= temp_addr1 ^ temp_addr2;

			temp_addr2		= part_addr;
			part_addr			= part_addr >> bank_bit_width;
			temp_addr1		= part_addr << bank_bit_width;
			_req->bank		= temp_addr1 ^ temp_addr2;			

			temp_addr2		= part_addr;
			part_addr			= part_addr >> rank_bit_width;
			temp_addr1		= part_addr << rank_bit_width;
			_req->rank		= temp_addr1 ^ temp_addr2;

			temp_addr2		= part_addr;
			part_addr			= part_addr >> channel_bit_width;
			temp_addr1		= part_addr << channel_bit_width;
			_req->channel	= temp_addr1 ^ temp_addr2;

			break;
		}
		case RoRaBaCoCh:
		{
			temp_addr2		= part_addr;
			part_addr			= part_addr >> channel_bit_width;
			temp_addr1		= part_addr << channel_bit_width;
			_req->channel	= temp_addr1 ^ temp_addr2;
			
			temp_addr2		= part_addr;
			part_addr			= part_addr >> col_bit_width;
			temp_addr1		= part_addr << col_bit_width;
			_req->column	= temp_addr1 ^ temp_addr2;

			temp_addr2		= part_addr;
			part_addr			= part_addr >> bank_bit_width;
			temp_addr1		= part_addr << bank_bit_width;
			_req->bank		= temp_addr1 ^ temp_addr2;	

			temp_addr2		= part_addr;
			part_addr			= part_addr >> rank_bit_width;
			temp_addr1		= part_addr << rank_bit_width;
			_req->rank		= temp_addr1 ^ temp_addr2;

			temp_addr2		= part_addr;
			part_addr			= part_addr >> row_bit_width;
			temp_addr1		= part_addr << row_bit_width;
			_req->row			= temp_addr1 ^ temp_addr2;

			break;
		}
		case RoCoRaBaCh:
		{
			temp_addr2		= part_addr;
			part_addr			= part_addr >> channel_bit_width;
			temp_addr1		= part_addr << channel_bit_width;
			_req->channel	= temp_addr1 ^ temp_addr2;

			temp_addr2		= part_addr;
			part_addr			= part_addr >> bank_bit_width;
			temp_addr1		= part_addr << bank_bit_width;
			_req->bank		= temp_addr1 ^ temp_addr2;	

			temp_addr2		= part_addr;
			part_addr			= part_addr >> rank_bit_width;
			temp_addr1		= part_addr << rank_bit_width;
			_req->rank		= temp_addr1 ^ temp_addr2;		

			temp_addr2		= part_addr;
			part_addr			= part_addr >> col_bit_width;
			temp_addr1		= part_addr << col_bit_width;
			_req->column	= temp_addr1 ^ temp_addr2;

			temp_addr2		= part_addr;
			part_addr			= part_addr >> row_bit_width;
			temp_addr1		= part_addr << row_bit_width;
			_req->row			= temp_addr1 ^ temp_addr2;

			break;
		}
		case ChRaRoCoBa:
		{
			temp_addr2		= part_addr;
			part_addr			= part_addr >> bank_bit_width;
			temp_addr1		= part_addr << bank_bit_width;
			_req->bank		= temp_addr1 ^ temp_addr2;
			
			temp_addr2		= part_addr;
			part_addr			= part_addr >> col_bit_width;
			temp_addr1		= part_addr << col_bit_width;
			_req->column	= temp_addr1 ^ temp_addr2;

			temp_addr2		= part_addr;
			part_addr			= part_addr >> row_bit_width;
			temp_addr1		= part_addr << row_bit_width;
			_req->row			= temp_addr1 ^ temp_addr2;

			temp_addr2		= part_addr;
			part_addr			= part_addr >> rank_bit_width;
			temp_addr1		= part_addr << rank_bit_width;
			_req->rank		= temp_addr1 ^ temp_addr2;

			temp_addr2		= part_addr;
			part_addr			= part_addr >> channel_bit_width;
			temp_addr1		= part_addr << channel_bit_width;
			_req->channel	= temp_addr1 ^ temp_addr2;

			break;
		}
	}
#if 0
	printf("[phys: %10lu] ch: %u, rank: %u, bank: %u, row: %lu, column: %u\n", _req->phys_addr, _req->channel, _req->rank, _req->bank, _req->row, _req->column);
#endif 
}

void
memory_operate(mem_ctrl_t* _ctrl, uint64_t _clk)
{
	memory_process(_ctrl, _clk);
	for (uint32_t ch=0; ch < _ctrl->n_channel; ch++)
		memory_schedule(_ctrl, _clk, ch);
}

void
memory_process(mem_ctrl_t* _ctrl, uint64_t _clk)
{
#if 0
	char* cmd_list[6] = {"ACT_CMD", "COL_READ_CMD", "COL_WRITE_CMD", "PRE_CMD", "REF_CMD", "NOP"};
	char* state_list[4] = {"IDLE", "PRECHARGING", "REFRESHING", "ROW_ACTIVE"};

	printf("\n");
	for (uint32_t ch=0; ch<_ctrl->n_channel; ch++)
	{
		for (uint32_t ra=0; ra<_ctrl->n_rank; ra++)
		{
			for (uint32_t ba=0; ba<_ctrl->n_bank; ba++)
			{
				printf("C[%u]R[%u]B[%u]: row:%10u / state:%s\n", ch, ra, ba, _ctrl->memory_state[ch][ra][ba].active_row, state_list[_ctrl->memory_state[ch][ra][ba].state]);
			}
		}
	}
	printf("\n");
#endif 

	for (uint32_t channel=0; channel<_ctrl->n_channel; channel++)
	{
		_ctrl->cmd_issued_cur_cycle[channel] = false;
		for (uint32_t rank=0; rank<_ctrl->n_rank; rank++)
		{
			for (uint32_t bank=0; bank<_ctrl->n_bank; bank++)
				_ctrl->cas_issued_cur_cycle[channel][rank][bank] = false;
		}

#if 0
		printf("[clk: %lu] ==RQ== [req: %u]\n", _clk, _ctrl->_readQ->n_req);
		mem_req_t* iter = _ctrl->_readQ->_head;
		for (uint32_t i=0; i<_ctrl->_readQ->n_req; i++)
		{
			printf("phys: %8lu [%u][%u][%u][%5lu] / next_cmd: %12s / comp_time: %lu / issuable: %d\n", iter->phys_addr, iter->channel, iter->rank, iter->bank, iter->row, cmd_list[iter->next_command], iter->comp_time, (iter->cmd_issuable==true)?1:0);
			iter = iter->_next;
		}
		printf("\n");

		printf("[clk: %lu] ==WQ== [req: %u]\n", _clk, _ctrl->_writeQ->n_req);
		iter = _ctrl->_writeQ->_head;
		for (uint32_t i=0; i<_ctrl->_writeQ->n_req; i++)
		{
			printf("phys: %8lu [%u][%u][%u][%5lu] / next_cmd: %12s / comp_time: %lu\n", iter->phys_addr, iter->channel, iter->rank, iter->bank, iter->row, cmd_list[iter->next_command], iter->comp_time);
			iter = iter->_next;
		}
		printf("\n");


#endif 

		//* update the variables corresponding to the non-queue variables
		update_issuable_commands(_ctrl, channel, _clk);

		//* update the request cmds in the queues
		if (_ctrl->_readQ->n_req)		update_read_queue_commands(_ctrl, channel, _clk);
		if (_ctrl->_writeQ->n_req)	update_write_queue_commands(_ctrl, channel, _clk);
		
		//* remove finished requests
		//? clean_queues(channel); <-------
		mem_req_t* it;
		mem_req_t* next;
		
		//TODO: make it as sub-line procedure..
		//* delete complete requests

		//TODO: this logic shows no problem; but exactly wrong path..
#if 0
		for (uint32_t i=0; i<_ctrl->_readQ->n_req; i++)
		{
			it = _ctrl->_readQ->_head;
			if (it->req_served && it->comp_time <= _clk)
			{
				assert(it->next_command == COL_READ_CMD);
				it->arrival_time = _clk;
				mem_txq_remove_req(_ctrl->_readQ, it);
			}
		}
		for (uint32_t i=0; i<_ctrl->_writeQ->n_req; i++)
		{
			it = _ctrl->_writeQ->_head;
			if (it->req_served && it->comp_time <= _clk)
			{
				assert(it->next_command == COL_WRITE_CMD);
				it->arrival_time = _clk;
				mem_txq_remove_req(_ctrl->_writeQ, it);
			}
		}
#endif 
		//? ----> clean_queues(channel);

		//TODO: even though, the right path shows error when meets assert() statements @ issue_command function.
		
		it = _ctrl->_readQ->_head;
		for (uint32_t i=0; i<_ctrl->_readQ->n_req; i++)
		{
			next = it->_next;
			if (it->req_served && it->comp_time <= _clk)
			{
				assert(it->next_command == COL_READ_CMD);
				assert(it->comp_time != 0);
				it->arrival_time = _clk;
				mem_txq_remove_req(_ctrl->_readQ, it);
			}
			it = next;
		}
		
		it = _ctrl->_writeQ->_head;
		for (uint32_t i=0; i<_ctrl->_writeQ->n_req; i++)
		{
			next = it->_next;
			if (it->req_served && it->comp_time <= _clk)
			{
				assert(it->next_command == COL_WRITE_CMD);
				it->arrival_time = _clk;
				mem_txq_remove_req(_ctrl->_writeQ, it);
			}
			it = next;			
		}
		
		//? ----> clean_queues(channel);
	}

}

void
memory_schedule(mem_ctrl_t* _ctrl, uint64_t _clk, uint32_t _channel)
{
	//* FCFS
	mem_req_t* rd_ptr = _ctrl->_readQ->_head;
	mem_req_t* wr_ptr = _ctrl->_writeQ->_head;
	
	bool drain_write = false;

	//TODO: ad-hoc condition for selecting write-drain mode or not:
	//TODO: In case of other simulators, switching to 'write-drain mode' when the number of entries in write-queue exceeds a threshold, like '60% of queue capacity'.
	if (_ctrl->_writeQ->n_req > _ctrl->_readQ->n_req)
	{
		drain_write = true;
	}

	//TODO: <----------------------------------------------------------
	//TODO: for supporting heterogeneous(hybrid) main memory systems, we should consider
	//TODO: "forced" write-drain mode until migrating line job being completed.
	

	//TODO: ---------------------------------------------------------->

	if (drain_write)
	{
		for (uint32_t n = 0; n < _ctrl->_writeQ->n_req; n++)
		{
			if (wr_ptr->cmd_issuable)
			{
				issue_request_command(_ctrl, wr_ptr, _clk);
				break;
			}
			wr_ptr = wr_ptr->_next;
		}
		return;
	}
	else
	{
		for (uint32_t n=0; n<_ctrl->_readQ->n_req; n++)
		{
			if (rd_ptr->cmd_issuable)
			{
				issue_request_command(_ctrl, rd_ptr, _clk);
				break;
			}
			rd_ptr = rd_ptr->_next;
		}
		return;
	}
}

void
update_issuable_commands(mem_ctrl_t* _ctrl, uint32_t _channel, uint64_t _clk)
{
	for (uint32_t rank = 0; rank < _ctrl->n_rank; rank++)
	{
		for (uint32_t bank = 0; bank < _ctrl->n_bank; bank++)
			_ctrl->cmd_precharge_issuable[_channel][rank][bank] = is_precharge_allowed(_ctrl, _channel, rank, bank, _clk);
		
		_ctrl->cmd_all_bank_precharge_issuable[_channel][rank] = is_all_bank_precharged_allowed(_ctrl, _channel, rank, _clk);
	}
}

//* function to see if the rank can be precharged or not
bool
is_precharge_allowed(mem_ctrl_t* _ctrl, uint32_t _channel, uint32_t _rank, uint32_t _bank, uint64_t _clk)
{
	if (_ctrl->cmd_issued_cur_cycle[_channel])
		return false;
	if ((_ctrl->memory_state[_channel][_rank][_bank].state == ROW_ACTIVE || 
			 _ctrl->memory_state[_channel][_rank][_bank].state == IDLE ||
			 _ctrl->memory_state[_channel][_rank][_bank].state == PRECHARGING || 
			 _ctrl->memory_state[_channel][_rank][_bank].state == REFRESHING) &&
			(_clk >= _ctrl->memory_state[_channel][_rank][_bank].next_pre))
	{
		return true;
	}
	else
		return false;
}

//* function to see if all banks can be precharged this cycle
bool
is_all_bank_precharged_allowed(mem_ctrl_t* _ctrl, uint32_t _channel, uint32_t _rank, uint64_t _clk)
{
	bool flag = false;
	if (_ctrl->cmd_issued_cur_cycle[_channel])
		return false;
	
	for (uint32_t i=0; i<_ctrl->n_bank; i++)
	{
		if ((_ctrl->memory_state[_channel][_rank][i].state == ROW_ACTIVE ||
				 _ctrl->memory_state[_channel][_rank][i].state == IDLE ||
				 _ctrl->memory_state[_channel][_rank][i].state == PRECHARGING) &&
				(_clk >= _ctrl->memory_state[_channel][_rank][i].next_pre))
		{
			flag = true;
		}
		else
			return false;
	}
	return flag;
}

//* function to see if the bank can be activated or not
bool
is_activate_allowed(mem_ctrl_t* _ctrl, uint32_t _channel, uint32_t _rank, uint32_t _bank, uint64_t _clk)
{
	if (_ctrl->cmd_issued_cur_cycle[_channel])
		return false;
	if ((_ctrl->memory_state[_channel][_rank][_bank].state == IDLE ||
			_ctrl->memory_state[_channel][_rank][_bank].state == PRECHARGING || 
			_ctrl->memory_state[_channel][_rank][_bank].state == REFRESHING) &&
			(_clk >= _ctrl->memory_state[_channel][_rank][_bank].next_act))
	{
		return true;
	}
	else
		return false;
}

void
update_read_queue_commands(mem_ctrl_t* _ctrl, uint32_t _channel, uint64_t _clk)
{
	mem_req_t* curr = _ctrl->_readQ->_head;
	for (uint32_t iter=0; iter < _ctrl->_readQ->n_req; iter++)
	{
		if (!curr->req_served)
		{
			uint32_t bank = curr->bank;
			uint32_t rank = curr->rank;
			uint64_t row	= curr->row;
			//printf("bank: %u, rank: %u, row: %lu\n", bank, rank, row);
			
			switch (_ctrl->memory_state[_channel][rank][bank].state)
			{
				//* 
				case IDLE:
				case PRECHARGING:
				case REFRESHING:
				{
					curr->next_command = ACT_CMD;
					if (_clk >= _ctrl->memory_state[_channel][rank][bank].next_act)
						curr->cmd_issuable = true;
					else
						curr->cmd_issuable = false;
					break;
				}
				case ROW_ACTIVE:
				{
					//* if row_buffer_hit occurs:
					//printf("active_row: %lu, row: %lu\n", _ctrl->memory_state[_channel][rank][bank].active_row, row);
					if (_ctrl->memory_state[_channel][rank][bank].active_row == row)
					{
						curr->next_command = COL_READ_CMD;
						if (_clk >= _ctrl->memory_state[_channel][rank][bank].next_read)
							curr->cmd_issuable = true;
						else
							curr->cmd_issuable = false;
					}
					//* if row_buffer_miss occurs:
					else
					{
						curr->next_command = PRE_CMD;
						curr->row_buffer_miss = true;
						if (_clk >= _ctrl->memory_state[_channel][rank][bank].next_pre)
							curr->cmd_issuable = true;
						else
							curr->cmd_issuable = false;
					}
					break;
				}
				default:	break;
			}
		}
		curr = curr->_next;
	}

}

void
update_write_queue_commands(mem_ctrl_t* _ctrl, uint32_t _channel, uint64_t _clk)
{
	mem_req_t* curr = _ctrl->_writeQ->_head;

	for (uint32_t iter=0; iter < _ctrl->_writeQ->n_req; iter++)
	{
		if (!curr->req_served)
		{
			uint32_t bank = curr->bank;
			uint32_t rank = curr->rank;
			uint64_t row = curr->row;

			switch (_ctrl->memory_state[_channel][rank][bank].state)
			{
				//* 
				case IDLE:
				case PRECHARGING:
				case REFRESHING:
				{
					curr->next_command = ACT_CMD;
					if (_clk >= _ctrl->memory_state[_channel][rank][bank].next_act)
						curr->cmd_issuable = true;
					else
						curr->cmd_issuable = false;
					break;
				}
				case ROW_ACTIVE:
				{
					//* if row_buffer_hit occurs:
					if (_ctrl->memory_state[_channel][rank][bank].active_row == row)
					{
						curr->next_command = COL_WRITE_CMD;
						if (_clk >= _ctrl->memory_state[_channel][rank][bank].next_write)
							curr->cmd_issuable = true;
						else
							curr->cmd_issuable = false;
					}
					//* if row_buffer_miss occurs:
					else
					{
						curr->next_command = PRE_CMD;
						curr->row_buffer_miss = true;
						if (_clk >= _ctrl->memory_state[_channel][rank][bank].next_pre)
							curr->cmd_issuable = true;
						else
							curr->cmd_issuable = false;
					}
					break;
				}
				default:	break;
			}
		}
		curr = curr->_next;
	}

}

bool
issue_request_command(mem_ctrl_t* _ctrl, mem_req_t* _req, uint64_t _clk)
{
	if (_req->cmd_issuable != true || _ctrl->cmd_issued_cur_cycle[_req->channel])
	{
		printf("(mtsim++) PANIC!! SCHEDULER_ERROR ~ Command for request selected cannot be issued in this cycle!\n");
		assert(0);
		return false;
	}

	//TODO: important!! why this statement does not exist in usimm?
	if (_req->req_served)	return false;
	//TODO:

	uint32_t	channel = _req->channel;
	uint32_t	rank 		= _req->rank;
	uint32_t	bank 		= _req->bank;
	uint64_t	row 		= _req->row;
	command_t	cmd 		= _req->next_command;

	switch(cmd)
	{
		case ACT_CMD:
		{
			assert(_ctrl->memory_state[channel][rank][bank].state == PRECHARGING || _ctrl->memory_state[channel][rank][bank].state == IDLE || _ctrl->memory_state[channel][rank][bank].state == REFRESHING);
			//* open row
			_ctrl->memory_state[channel][rank][bank].state 			= ROW_ACTIVE;
			_ctrl->memory_state[channel][rank][bank].active_row = row;
			_ctrl->memory_state[channel][rank][bank].next_pre 	= max((_clk + _ctrl->tRAS), _ctrl->memory_state[channel][rank][bank].next_pre);
			_ctrl->memory_state[channel][rank][bank].next_read 	= max((_clk + _ctrl->tRCD), _ctrl->memory_state[channel][rank][bank].next_read);
			_ctrl->memory_state[channel][rank][bank].next_write = max((_clk + _ctrl->tRCD), _ctrl->memory_state[channel][rank][bank].next_write);
			_ctrl->memory_state[channel][rank][bank].next_act 	= max((_clk + _ctrl->tRC),  _ctrl->memory_state[channel][rank][bank].next_act);

			for (uint32_t i=0; i<_ctrl->n_bank; i++)
			{
				if (i!=bank)
				{
					//_ctrl->memory_state[channel][rank][bank].next_act = max(_clk + _ctrl->tRRD, _ctrl->memory_state[channel][rank][i].next_act);
					_ctrl->memory_state[channel][rank][bank].next_act = max(_clk, _ctrl->memory_state[channel][rank][i].next_act);	//TODO: mem-test
				}
			}

			//printf("curr_clk: [%lu], act_row: %lu, next_pre: %lu, next_read: %lu, next_write: %lu, next_act: %lu\n", _clk, _ctrl->memory_state[channel][rank][bank].active_row, _ctrl->memory_state[channel][rank][bank].next_pre, _ctrl->memory_state[channel][rank][bank].next_read, _ctrl->memory_state[channel][rank][bank].next_write, _ctrl->memory_state[channel][rank][bank].next_act);
			_ctrl->cmd_issued_cur_cycle[channel] = true;
			break;
		}
		case COL_READ_CMD:
		{
			/*
			if (!(_ctrl->memory_state[channel][rank][bank].state == ROW_ACTIVE))
			{
				printf("%s\n", (_ctrl->is_NVM)?"is_NVM":"is_DRAM");
				if (_ctrl->_proc->hybrid)
				{
					printf("%u\n", _req->_h_req->state);
				}
				else
				{
					printf("state: %u\n", _req->_c_req->req_state );
				}
			}
			*/
			assert(_ctrl->memory_state[channel][rank][bank].state == ROW_ACTIVE);
			//tRTP is needed!
			if (_req->row_buffer_miss)
			{
				_ctrl->row_buffer_miss_cnt++;
				//printf("row_buffer_miss: [phys: %10lu][%u][%u][%u][%lu]\n", _req->phys_addr, _req->channel, _req->rank, _req->bank, _req->row);
			}

			_ctrl->memory_state[channel][rank][bank].next_pre = max(_clk, _ctrl->memory_state[channel][rank][bank].next_pre);

			for (uint32_t i=0; i<_ctrl->n_rank; i++)
			{
				for (uint32_t j=0; j<_ctrl->n_bank; j++)
				{
					if (i!=rank) 
						//_ctrl->memory_state[channel][i][j].next_read = max(_clk + _ctrl->tBL, _ctrl->memory_state[channel][i][j].next_read);
						_ctrl->memory_state[channel][i][j].next_read = max(_clk, _ctrl->memory_state[channel][i][j].next_read);	//TODO: mem-test
					else
						//_ctrl->memory_state[channel][i][j].next_read = max(_clk + max(_ctrl->tCCD, _ctrl->tBL), _ctrl->memory_state[channel][i][j].next_read);
						_ctrl->memory_state[channel][i][j].next_read = max(_clk, _ctrl->memory_state[channel][i][j].next_read);	//TODO: mem-test

					//_ctrl->memory_state[channel][i][j].next_write = max(_clk + _ctrl->tCAS + _ctrl->tBL, _ctrl->memory_state[channel][i][j].next_write);
					_ctrl->memory_state[channel][i][j].next_write = max(_clk + _ctrl->tCAS, _ctrl->memory_state[channel][i][j].next_write);	//TODO: mem-test
				}
			}
			//_req->comp_time = _clk + _ctrl->tCAS + _ctrl->tBL;
			_req->comp_time = _clk + _ctrl->tCAS;	//TODO: mem-test
			_req->req_served = true;

			//* update the higher level's txQ information with the completion time / state!
			if (!_ctrl->_proc->hybrid)
			{
				if (_req->_c_req != NULL)
				{
					//_req->_c_req->comp_time = _clk + _ctrl->tCAS + _ctrl->tBL;
					_req->_c_req->comp_time = _clk + _ctrl->tCAS;	//TODO: mem-test
					_req->_c_req->req_state = DONE;
				}
			}
			else
			{
				//_req->_h_req->comp_time = _clk + _ctrl->tCAS + _ctrl->tBL;
				_req->_h_req->comp_time = _clk + _ctrl->tCAS;	//TODO: mem-test
				_ctrl->_proc->_hmem_ctrl->total_bandwidth += 64;
				_ctrl->_proc->_hmem_ctrl->interval_bandwidth += 64;
			}

			_ctrl->cmd_issued_cur_cycle[channel] = true;
			_ctrl->cas_issued_cur_cycle[channel][rank][bank] = true;
			
			_ctrl->total_bandwidth += 64;
			_ctrl->interval_bandwidth += 64;

			break;
		}
		case COL_WRITE_CMD:
		{
			assert(_ctrl->memory_state[channel][rank][bank].state == ROW_ACTIVE);

			if (_req->row_buffer_miss)
				_ctrl->row_buffer_miss_cnt++;
			
			//_ctrl->memory_state[channel][rank][bank].next_pre = max(_clk + _ctrl->tBL + _ctrl->tWR, _ctrl->memory_state[channel][rank][bank].next_pre);
			_ctrl->memory_state[channel][rank][bank].next_pre = max(_clk, _ctrl->memory_state[channel][rank][bank].next_pre);	//TODO: mem-test

			for (uint32_t i=0; i<_ctrl->n_rank; i++)
			{
				for (uint32_t j=0; j<_ctrl->n_bank; j++)
				{
					if (i!=rank)
					{
						//_ctrl->memory_state[channel][i][j].next_write = max(_clk + _ctrl->tBL, _ctrl->memory_state[channel][i][j].next_write);
						//_ctrl->memory_state[channel][i][j].next_read = max(_clk + _ctrl->tBL - _ctrl->tCAS, _ctrl->memory_state[channel][i][j].next_read);
						_ctrl->memory_state[channel][i][j].next_write = max(_clk, _ctrl->memory_state[channel][i][j].next_write);	//TODO: mem-test
						_ctrl->memory_state[channel][i][j].next_read = max(_clk - _ctrl->tCAS, _ctrl->memory_state[channel][i][j].next_read);
					}
					else
					{
						//_ctrl->memory_state[channel][i][j].next_write = max(_clk + max(_ctrl->tCCD, _ctrl->tBL), _ctrl->memory_state[channel][rank][bank].next_write);
						//_ctrl->memory_state[channel][i][j].next_read = max(_clk + _ctrl->tBL, _ctrl->memory_state[channel][i][j].next_read);
						_ctrl->memory_state[channel][i][j].next_write = max(_clk, _ctrl->memory_state[channel][rank][bank].next_write);	//TODO: mem-test
						_ctrl->memory_state[channel][i][j].next_read = max(_clk, _ctrl->memory_state[channel][i][j].next_read);
					}
				}
			}
			//_req->comp_time = _clk + _ctrl->tBL + _ctrl->tWR;
			_req->comp_time = _clk;	//TODO: mem-test
			_req->req_served = true;

			if (!_ctrl->_proc->hybrid)
			{
				if (_req->_c_req != NULL)
				{
					//_req->_c_req->comp_time = _clk + _ctrl->tBL + _ctrl->tWR;
					_req->_c_req->comp_time = _clk;	//TODO: mem-test
					_req->_c_req->req_state = DONE;
				}
			}
			else
			{
				//_req->_h_req->comp_time = _clk + _ctrl->tBL + _ctrl->tWR;
				_req->_h_req->comp_time = _clk;	//TODO: mem-test
				_ctrl->_proc->_hmem_ctrl->total_bandwidth += 64;
				_ctrl->_proc->_hmem_ctrl->interval_bandwidth += 64;
			}

			_ctrl->cmd_issued_cur_cycle[channel] = true;
			_ctrl->cas_issued_cur_cycle[channel][rank][bank] = true;	//TODO: should be 2?

			_ctrl->total_bandwidth += 64;
			_ctrl->interval_bandwidth += 64;

			break;
		}
		case PRE_CMD:
		{
			assert(_ctrl->memory_state[channel][rank][bank].state == ROW_ACTIVE || _ctrl->memory_state[channel][rank][bank].state == PRECHARGING || _ctrl->memory_state[channel][rank][bank].state == IDLE);
			_ctrl->memory_state[channel][rank][bank].state = PRECHARGING;
			_ctrl->memory_state[channel][rank][bank].active_row = UINT64_MAX;
			_ctrl->memory_state[channel][rank][bank].next_act = max(_clk + _ctrl->tRP, _ctrl->memory_state[channel][rank][bank].next_act);
			_ctrl->memory_state[channel][rank][bank].next_pre = max(_clk + _ctrl->tRP, _ctrl->memory_state[channel][rank][bank].next_pre);
			_ctrl->cmd_issued_cur_cycle[channel] = true;
			break;
		}

		default:
			break;
	}

	return true;

}