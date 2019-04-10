#ifndef __BLOCK_H__
#define __BLOCK_H__

#include <inttypes.h>
#include <stdio.h>

#define CACHE_LINE_SIZE	64
#define	BIG_LATENCY			1000000

typedef struct request_packet	req_t;
typedef struct uop_struct			op_t;

typedef struct core_structure				core_t;
typedef struct processor_structure	proc_t;

typedef struct cache_structure			cache_t;

typedef enum op_type
{
	NOT_MEM,
	MEM_LD,
	MEM_ST,
	NUM_MEM_TYPES,
} op_type;

typedef enum req_state
{
	EMPTY,
	PENDING,
	HIT,
	MISS,
	MSHR_WAIT,
	FILL_WAIT,
	DONE,
} req_state_t;

struct request_packet
{
	op_t				*rob_ptr;		//? a pointer to rob entry which demanded this request
	op_type			req_type;		//? a request type
	req_state_t	req_state;	//? a request state
	uint64_t		phys_addr;	//? a physical address of the request
	uint64_t		comp_time;	//? a completion time of this request
	uint32_t		core_id;		//? a core id of this request
	bool				is_pf;			//? is demand request of prefetch request?
	bool				is_hit;			//? a bit stores whether this req was hit or miss.
	req_t*			_next;			//? a next ptr for memory queue logics
	uint8_t			pf_target;	//? a target cache level information when this request is for prefetching
};

enum rob_state
{
	FETCH,
	MEM_WAIT,
	COMMIT,
};

typedef enum cache_type
{
	directMapped,
	setAssociate,
	fulAssociate,
} cache_map_t;

typedef enum repl_policy
{
	FIFO,
	RAND,
	LRU,
} repl_t;

struct uop_struct
{
	op_type		type;
	uint64_t	phys_addr;
	uint64_t	inst_addr;
	uint32_t	core_id;
	uint64_t	comp_time;
	rob_state	state;
};

typedef struct rob_structure
{
	op_t			*_op;
	uint32_t	n_entry;
	uint32_t	capacity;
	uint32_t	_front;
	uint32_t	_rear;
	uint32_t	core_id;
} rob_t;

#endif