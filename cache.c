#include <stdio.h>
#include <stdlib.h>

#include "rv_emu.h"

void cache_init(struct cache *csp)
{
	if (csp->type == CACHE_NONE)
		return;

	csp->block_mask = (csp->block_size) - 1;
	csp->index_mask = ((csp->size / csp->block_size) / csp->ways) - 1;

	csp->index_bits = 0;
	while (csp->index_mask & (1 << csp->index_bits))
		csp->index_bits++;

	csp->block_bits = 0;
	while (csp->block_mask & (1 << csp->block_bits))
		csp->block_bits++;

	for (int i = 0; i < CACHE_MAX_SLOTS; i++) {
		csp->slots[i].valid = 0;
		csp->slots[i].tag = 0;
		for (int j = 0; j < CACHE_MAX_BLOCK_SIZE; j++)
			csp->slots[i].block[j] = 0;
		/* timestamp only used for SA cache */
		csp->slots[i].timestamp = 0;
	}

	csp->refs = 0;
	csp->hits = 0;
	csp->misses = 0;
	csp->misses_cold = 0;
	csp->misses_hot = 0;

	verbose("Cache initialized.\n");
}

void cache_print(struct cache *csp, char *name)
{
	int num_slots_used = 0;
	int i;

	for (i = 0; i < csp->size; i++)
		if (csp->slots[i].valid == 1)
			num_slots_used += 1;

	printf("=== Cache %s\n", name);
	printf("Type          = ");
	if (csp->type == CACHE_DM)
		printf("direct mapped\n");
	else if (csp->type == CACHE_SA)
		printf("set associative\n");

	printf("Size          = %d slots\n", csp->size);
	printf("Block size    = %d words\n", csp->block_size);
	printf("Ways          = %d\n", csp->ways);
	printf("References    = %d\n", csp->refs);
	printf("Hits          = %d (%.2f%% hit ratio)\n", csp->hits,
	       ((double)csp->hits / (double)csp->refs) * 100.00);
	printf("Misses        = %d (%.2f%% miss ratio)\n", csp->misses,
	       ((double)csp->misses / (double)csp->refs) * 100.00);
	printf("Misses (cold) = %d\n", csp->misses_cold);
	printf("Misses (hot)  = %d\n", csp->misses_hot);
	/* TODO: Funky format due to bad test cases. Remove later */
	printf("%% Used        = %.2f%%\n",
	       ((double)num_slots_used / (double)csp->size) * 100.0);
}

uint32_t evict_data_in_cache_block(struct cache *csp, struct cache_slot *slot,
				   uint64_t addr_word, uint64_t b_index)
{
		uint64_t block_base = addr_word - b_index;
		/* Convert block base to a pointer to the iw in memory */
		uint32_t *block_base_iw_ptr = (uint32_t *)(block_base << 2);

		/*
		 * Evict old data from the block and update with new data
		 * retrieved from the memory bus
		 */
                for (int i = 0; i < csp->block_size; i++) {
                        slot->block[i] = *block_base_iw_ptr;
                        block_base_iw_ptr++; /* Move pointer by 32 bits (next iw) */
                }

		/* iw is now in the updated cache block */
		uint32_t data = slot->block[b_index];
		return data;
}

/* Direct mapped lookup */
uint32_t cache_lookup_dm(struct cache *csp, uint64_t addr)
{
	uint64_t tag;
	uint64_t index;
	uint64_t b_index;
	uint64_t b_base;
	struct cache_slot *slot;
	uint32_t data = 0;

	uint64_t addr_word = addr >> 2;
	b_index = addr_word % csp->block_size; /* mask off according to the block size */

	index = (addr >> (csp->block_bits + 2)) & csp->index_mask;
	tag = addr >> (csp->index_bits + csp->block_bits + 2);

	slot = &csp->slots[index];

	csp->refs += 1;
	if (slot->valid && (slot->tag == tag)) {
		/* hit */
		csp->hits += 1;
		data = slot->block[b_index];

		verbose("  cache tag hit for index %d tag %X addr %lX\n",
			index, tag, addr);
	} else {
		/* miss */
		csp->misses += 1;
		if (slot->valid == 0) {
			csp->misses_cold += 1;
			verbose
			    ("  cache tag (%X) miss for index %d tag %X addr %X (cold)\n",
			     slot->tag, index, tag, addr);
		} else {
			csp->misses_hot += 1;
			verbose
			    ("  cache tag (%X) miss for index %d tag %X addr %X (hot)\n",
			     slot->tag, index, tag, addr);
		}
		slot->valid = 1;
		slot->tag = tag;

		data = evict_data_in_cache_block(csp, slot, addr_word, b_index);
	}

	return data;
}

struct cache_slot *find_lru_slot_in_set(struct cache *csp, uint32_t set_base)
{
	struct cache_slot *low = &csp->slots[set_base];
	for (int i = 0; i < csp->ways; i++) {
		struct cache_slot *slot = &csp->slots[set_base + i];
		if (slot->timestamp < low->timestamp)
			low = slot;
	}
	return low;
}

uint32_t cache_lookup_sa(struct cache *csp, uint64_t addr)
{
	bool hit = false;
	uint32_t value;

	/*
	 * Didn't allocate any cache. I guess that's a miss?
	 * This should not happen in our code as we don't simulate unless
	 * cache_size > 0
	 */

	if (csp->size == 0) {
		csp->misses += 1;
		return *((uint32_t *)addr);
	}

	csp->refs += 1;

	uint64_t tag = addr >> (csp->index_bits + csp->block_bits + 2);

	uint64_t addr_word = addr >> 2;
	uint64_t b_index = addr_word % csp->block_size; /* mask off according to the block size */

	int set_index = (addr >> (csp->block_bits + 2)) & csp->index_mask;
	int set_base = set_index * csp->ways;

	struct cache_slot *slot = NULL;
	struct cache_slot *slot_found = NULL;
	struct cache_slot *slot_invalid = NULL;

	/* Check each slot in the set */
	for (int i = 0; i < 4; i += 1) {
		slot = &csp->slots[set_base + i];
		if (slot->valid) {
			if (tag == slot->tag) {
				verbose
				    ("  cache tag hit for set %d way %d tag %X addr %lX\n",
				     set_index, i, tag, addr);
				hit = true;
				slot_found = slot;
				csp->hits++;
				break;
			}
		} else {
			/* Save invalid slot in case of miss */
			slot_invalid = slot;
		}
	}

	if (!slot_found) {
		if (slot_invalid) {
			slot = slot_invalid;
			verbose
			    ("  cache tag (%X) miss for set %d tag %X addr %X (fill invalid slot)\n",
			     slot->tag, set_index, tag, addr);
			csp->misses += 1;
			/* Miss due to tag collision is a "hot" miss */
			csp->misses_cold += 1;
		} else {
			/* hot miss */
			slot = find_lru_slot_in_set(csp, set_base);

			verbose
			    ("  cache tag (%X) miss for set %d tag %X addr %X (evict address %X)\n",
			     slot->tag, set_index, tag, addr,
			     ((slot->
			       tag << (csp->index_bits +
				       2)) | (set_index << 2)));

			csp->misses += 1;
			/* Miss due to tag collision is a "hot" miss */
			csp->misses_hot += 1;
		}
	}

	if (!hit) {
		value = evict_data_in_cache_block(csp, slot, addr_word, b_index);
		slot->tag = tag;
		slot->valid = true;
	} else {
		value = slot->block[b_index];
	}

	slot->timestamp = csp->refs;
	return value;
}

/* Cache lookup */
uint32_t cache_lookup(struct cache *csp, uint64_t addr)
{
	uint32_t data;
	switch (csp->type) {
	case CACHE_DM:
		data = cache_lookup_dm(csp, addr);
		break;
	case CACHE_SA:
		data = cache_lookup_sa(csp, addr);
		break;
	default:
		data = *((uint32_t *)addr);
		break;
	}
	return data;
}
