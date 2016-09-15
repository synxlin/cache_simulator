#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include "cache.h"
#include "op.h"
#include "rbtree.h"

void Cache_Initial(uint32_t *size, uint32_t *assoc, uint32_t *repl_policy, uint32_t *inclusion)
{
	uint32_t j;
	CACHE = (cache *)malloc(sizeof(cache)*NUM_LEVEL);
	if (CACHE == NULL)
		_error_exit("malloc")
	BLOCK_OFFSET_WIDTH = log_2(BLOCKSIZE);
	for (j = 0; j < NUM_LEVEL; j++)
	{
		if (size[j] == 0)
			break;
		CACHE[j].SIZE = size[j];
		CACHE[j].ASSOC = assoc[j];
		CACHE[j].REPL_POLICY = repl_policy[j];
		CACHE[j].INCLUSION = inclusion[j];
		CACHE[j].SET_NUM = CACHE[j].SIZE / (CACHE[j].ASSOC * BLOCKSIZE);
		
		CACHE[j].INDEX_WIDTH = log_2(CACHE[j].SET_NUM);
		CACHE[j].TAG_WIDTH = 64 - CACHE[j].INDEX_WIDTH - BLOCK_OFFSET_WIDTH;

		CACHE[j].CACHE_IC = (set *)malloc(sizeof(set) * CACHE[j].SET_NUM);
		if (CACHE[j].CACHE_IC == NULL)
			_error_exit("malloc")
		uint32_t i;
		for (i = 0; i < CACHE[j].SET_NUM; i++)
		{
			CACHE[j].CACHE_IC[i].BLOCK = (block *)malloc(sizeof(block) * CACHE[j].ASSOC);
			if (CACHE[j].CACHE_IC[i].BLOCK == NULL)
				_error_exit("malloc")
			memset(CACHE[j].CACHE_IC[i].BLOCK, 0, sizeof(block) * CACHE[j].ASSOC);
			CACHE[j].CACHE_IC[i].RANK = (uint64_t *)malloc(sizeof(uint64_t) * CACHE[j].ASSOC);
			if (CACHE[j].CACHE_IC[i].RANK == NULL)
				_error_exit("malloc")
			memset(CACHE[j].CACHE_IC[i].RANK, 0, sizeof(uint64_t) * CACHE[j].ASSOC);
		}
		memset(&(CACHE[j].CACHE_STAT), 0, sizeof(cache_stat));
	}
}

void OPTIMIZATION_TRACE_Initial()
{
	FILE *trace_file_fp = fopen(TRACE_FILE, "r");
	if (trace_file_fp == NULL)
		_error_exit("fopen()")
	uint64_t trace_len = 1000, i = 0;
	uint64_t *trace = (uint64_t *)malloc(sizeof(uint64_t) * trace_len);
	if (trace == NULL)
		_error_exit("malloc")
	while (1)
	{
		int result;
		uint8_t OP;
		uint64_t ADDR;
		result = fscanf(trace_file_fp, "%c %llx", &OP, &ADDR);
		if (result == EOF)
			break;
		if (i > trace_len)
		{
			trace_len *= 2;
			trace = (uint64_t *)realloc(OPTIMIZATION_TRACE, trace_len);
			if (trace == NULL)
				_error_exit("realloc")
		}
		trace[i] = ADDR;
		i++;
	}
	fclose(trace_file_fp);

	OPTIMIZATION_TRACE = (uint64_t *)malloc(sizeof(uint64_t) * i);
	if (OPTIMIZATION_TRACE == NULL)
		_error_exit("malloc")

	_rb_tree_ptr T = (_rb_tree_ptr)malloc(sizeof(struct _rb_tree));
	if (T == NULL)
		_error_exit("malloc")
	T->_root = NULL;
	uint64_t j;
	for (j = i - 1; j >= 0; j--)
	{
		_rb_tree_node_ptr _result = _rb_tree_find(T, trace[j]);
		if (_result == NULL)
		{
			_rb_tree_insert(T, trace[j], j);
			OPTIMIZATION_TRACE[j] = i + 1;
		}
		else
		{
			OPTIMIZATION_TRACE[j] = _result->value - j;
			_result->value = j;
		}
	}
	free(trace);
	_rb_tree_clear(T);
}

void Interpret_Address(uint32_t level, uint64_t ADDR, uint64_t *tag, uint64_t *index)
{
	uint32_t tag_width = CACHE[level].TAG_WIDTH;
	*tag = ADDR >> (64 - tag_width);
	*index = (ADDR << tag_width) >> (tag_width + BLOCK_OFFSET_WIDTH);
}

uint64_t Rebuild_Address(uint32_t level, uint64_t tag, uint64_t index)
{
	uint64_t ADDR = 0;
	ADDR |= (tag << (CACHE[level].INDEX_WIDTH + BLOCK_OFFSET_WIDTH));
	ADDR |= (index << BLOCK_OFFSET_WIDTH);
	return ADDR;
}

uint8_t Cache_Search(uint32_t level, uint64_t tag, uint64_t index, uint32_t *way_num)
{
	uint32_t i;
	int k = -1;
	for (i = 0; i < CACHE[level].ASSOC; i++)
		if (CACHE[level].CACHE_IC[index].BLOCK[i].TAG == tag &&  CACHE[level].CACHE_IC[index].BLOCK[i].VALID_BIT == VALID)
		{
			k = i;
			break;
		}
	if (k == -1)
		return MISS;
	else
	{
		*way_num = k;
		return HIT;
	}
}

void Rank_Maintain(uint32_t level, uint64_t index, uint32_t way_num, uint8_t result)
{
	uint64_t *rank = CACHE[level].CACHE_IC[index].RANK;
	switch (CACHE[level].REPL_POLICY)
	{
	case FIFO:
		if (result == MISS)
			rank[way_num] = trace_count;
		return; 
	case PLRU:
	{
		if (CACHE[level].ASSOC == 1)
			return;
		uint32_t idx = CACHE[level].ASSOC - 1 + way_num;
		uint8_t l_or_r = LEFT;
		l_or_r = (idx & 1) ? LEFT : RIGHT;
		idx = ((idx + 1) >> 1) - 1;
		while (idx > 0)
		{
			rank[idx] = 1 - l_or_r;
			l_or_r = (idx & 1) ? LEFT : RIGHT;
			idx = ((idx + 1) >> 1) - 1;
		}
		rank[idx] = 1 - l_or_r;
		return;
	}
	case OPTIMIZATION:
		rank[way_num] = OPTIMIZATION_TRACE[trace_count];
		return;
	case LRU:
		rank[way_num] = trace_count;
		return;
	}
}

uint32_t Rank_Top(uint32_t level, const uint64_t *rank)
{
	switch (CACHE[level].REPL_POLICY)
	{
	default:
	case FIFO:
	case LRU:
	{
		uint32_t i, k = 0;
		for (i = 0; i < CACHE[level].ASSOC; i++)
			if (rank[i] < rank[k])
				k = i;
		return k;
	}
	case PLRU:
	{
		uint32_t assoc = CACHE[level].ASSOC;
		if (assoc == 1)
			return 0;
		uint32_t idx = 0;
		while (idx < assoc - 1)
		{
			if (rank[idx] == LEFT)
				idx = (idx << 1) + 1;
			else
				idx = (idx << 1) + 2;
		}
		idx = idx - assoc + 1;
		return idx;
	}
	case OPTIMIZATION:
	{
		uint32_t i, k = 0;
		for (i = 0; i < CACHE[level].ASSOC; i++)
			if (rank[i] > rank[k])
				k = i;
		return k;
	}
	}
}

uint32_t Cache_Replacement(uint32_t level, uint64_t index, block blk)
{
	uint32_t way_num = Rank_Top(level, CACHE[level].CACHE_IC[index].RANK);
	block *tmp = &(CACHE[level].CACHE_IC[index].BLOCK[way_num]);
	if (tmp->VALID_BIT == INVALID)
	{
#ifdef DBG
		printf("create new in L%u\n", level+1);
#endif // DBG
		tmp->VALID_BIT = VALID;
		tmp->TAG = blk.TAG;
		tmp->DIRTY_BIT = blk.DIRTY_BIT;
	}
	else
	{
		uint64_t ADDR = Rebuild_Address(level, tmp->TAG, index);
#ifdef DBG
		printf("replace old %llx in L%u - write back\n", ADDR, level+1);
		printf("replace new\n");
#endif // DBG	
		if (tmp->DIRTY_BIT == DIRTY || CACHE[level].REPL_POLICY == EXCLUSIVE)
		{
			CACHE[level].CACHE_STAT.num_write_backs++;
			Write_Back(level, ADDR, tmp->DIRTY_BIT);
		}
		tmp->VALID_BIT = VALID;
		tmp->DIRTY_BIT = blk.DIRTY_BIT;
		tmp->TAG = blk.TAG;
	}
	return way_num;
}

void Write_Back(uint32_t level, uint64_t ADDR, uint8_t dirty_bit)
{
	uint32_t nxt_level = level + 1;
	if (nxt_level >= NUM_LEVEL)
	{
		if(dirty_bit == DIRTY)
			Write(NUM_LEVEL, ADDR, DIRTY);
	}
	else
	{
		switch (CACHE[level].INCLUSION)
		{
		case INCLUSIVE:
		{
			uint64_t tag, index;
			uint32_t way_num;
			Interpret_Address(nxt_level, ADDR, &tag, &index);
			uint8_t result = Cache_Search(nxt_level, tag, index, &way_num);
			if (result == HIT) // HIT说明新的没有和旧的重复, MISS说明已经被新的替换了, 需要从下级写回下下级
			{
				CACHE[nxt_level].CACHE_IC[index].BLOCK[way_num].DIRTY_BIT = DIRTY;
				CACHE[nxt_level].CACHE_STAT.num_writes++;
			}
			else
			{
				Write_Back(nxt_level, ADDR, dirty_bit);
				//CACHE[nxt_level].CACHE_STAT.num_write_misses++;
			}
			break;
		}
		case NON_INCLUSIVE:
		case EXCLUSIVE:
			Write(nxt_level, ADDR, dirty_bit);
			return;
		}
	}
	if (level > L1)
		Invalidation(level - 1, ADDR);
}

void Invalidation(uint32_t level, uint64_t ADDR)
{
	switch (CACHE[level].INCLUSION)
	{
	case INCLUSIVE:
	{
		uint64_t tag, index;
		uint32_t way_num;
		Interpret_Address(level, ADDR, &tag, &index);
		uint8_t result = Cache_Search(level, tag, index, &way_num);
		if (result == HIT)
			CACHE[level].CACHE_IC[index].BLOCK[way_num].VALID_BIT = INVALID;
		if (level > L1)
			Invalidation(level - 1, ADDR);
		else
			return;
	}
	default:
		return;
	}
}

uint32_t Read(uint32_t level, uint64_t ADDR, block *blk, uint32_t access_count)
{
	if (level >= NUM_LEVEL)
	{
#ifdef DBG
		printf("read %llx : Main Memory HIT\n", ADDR);
#endif // DBG
		CACHE[NUM_LEVEL - 1].CACHE_STAT.num_blocks_transferred++;
		blk->DIRTY_BIT = CLEAN;
		blk->VALID_BIT = VALID;
		return 0;
	}
	CACHE[level].CACHE_STAT.num_reads++;
	uint64_t tag, index;
	uint32_t way_num;
	Interpret_Address(level, ADDR, &tag, &index);
	uint8_t result = Cache_Search(level, tag, index, &way_num);
	if (result == HIT)
	{
#ifdef DBG
		printf("read %llx : L%u HIT\n", ADDR, level+1);
#endif // DBG
		if (level > L1 && CACHE[level - 1].INCLUSION == EXCLUSIVE)
		{
			CACHE[level].CACHE_IC[index].BLOCK[way_num].VALID_BIT = INVALID;
			*blk = CACHE[level].CACHE_IC[index].BLOCK[way_num];
			return way_num;
		}
		*blk = CACHE[level].CACHE_IC[index].BLOCK[way_num];
		Rank_Maintain(level, index, way_num, HIT);
		return way_num;
	}
	else
	{
#ifdef DBG
		printf("read %llx : L%u MISS\n", ADDR, level + 1);
#endif // DBG
		CACHE[level].CACHE_STAT.num_read_misses++;
		Read(level + 1, ADDR, blk, access_count + 1);
		blk->TAG = tag;
		if ((level > L1 && CACHE[level - 1].INCLUSION != EXCLUSIVE) || (access_count == 0))
		{
			way_num = Cache_Replacement(level, index, *blk);
			Rank_Maintain(level, index, way_num, MISS);
		}
		return way_num;
	}
}

void Write(uint32_t level, uint64_t ADDR, uint8_t dirty_bit)
{
	if (level >= NUM_LEVEL)
	{
#ifdef DBG
		printf("write %llx : Main Memory HIT\n", ADDR);
#endif // DBG
		CACHE[NUM_LEVEL - 1].CACHE_STAT.num_blocks_transferred++;
		return;
	}
	CACHE[level].CACHE_STAT.num_writes++;
	uint64_t tag, index;
	uint32_t way_num;
	Interpret_Address(level, ADDR, &tag, &index);
	uint8_t result = Cache_Search(level, tag, index, &way_num);
	if (result == HIT)
	{
#ifdef DBG
		printf("write %llx : L%u HIT\n", ADDR, level + 1);
#endif // DBG
		Rank_Maintain(level, index, way_num, HIT);
	}
	else
	{
#ifdef DBG
		printf("write %llx : L%u MISS\n", ADDR, level + 1);
#endif // DBG
		block *blk = (block*)malloc(sizeof(block));
		way_num = Read(level, ADDR, blk, 0);
		CACHE[level].CACHE_STAT.num_reads--;
		CACHE[level].CACHE_STAT.num_read_misses--;
		CACHE[level].CACHE_STAT.num_write_misses++;
		free(blk);
	}
	CACHE[level].CACHE_IC[index].BLOCK[way_num].DIRTY_BIT = dirty_bit;
}

void Cache_free()
{
	uint32_t i;
	for (i = 0; i < NUM_LEVEL; i++)
	{
		uint32_t j;
		for (j = 0; j < CACHE[i].SET_NUM; j++)
		{
			free(CACHE[i].CACHE_IC[j].BLOCK);
			free(CACHE[i].CACHE_IC[j].RANK);
		}
		free(CACHE[i].CACHE_IC);
	}
	free(CACHE);
}
