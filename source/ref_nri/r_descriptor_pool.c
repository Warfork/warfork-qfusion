#include "r_descriptor_pool.h"
#include "r_nri.h"
#include "stb_ds.h"

static struct descriptor_set_slot_s *ReserveDescriptorSetSlot( struct descriptor_set_allloc_s *alloc )
{
	if( alloc->blocks == NULL || alloc->blockIndex == RESERVE_BLOCK_SIZE ) {
		struct descriptor_set_slot_s *block = calloc( RESERVE_BLOCK_SIZE, sizeof( struct descriptor_set_slot_s ) );
		alloc->blockIndex = 0;
		arrpush( alloc->blocks, block );
		return block + ( alloc->blockIndex++ );
	}
	return alloc->blocks[arrlen( alloc->blocks ) - 1] + ( alloc->blockIndex++ );
}

static void AttachDescriptorSlot( struct descriptor_set_allloc_s *alloc, struct descriptor_set_slot_s *slot )
{
	assert( slot );
	{
		slot->quNext = NULL;
		slot->quPrev = alloc->queueEnd;
		if( alloc->queueEnd ) {
			alloc->queueEnd->quNext = slot;
		}
		alloc->queueEnd = slot;
		if(!alloc->queueBegin) {
			alloc->queueBegin = slot;
		}
	}
	{
		const size_t hashIndex = slot->hash % ALLOC_HASH_RESERVE;
		slot->hPrev = NULL;
		slot->hNext = NULL;
		if( alloc->hashSlots[hashIndex] ) {
			alloc->hashSlots[hashIndex]->hPrev = slot;
			slot->hNext = alloc->hashSlots[hashIndex];
		}
		alloc->hashSlots[hashIndex] = slot;
	}
}


static void DetachDescriptorSlot( struct descriptor_set_allloc_s *alloc, struct descriptor_set_slot_s *slot )
{
	assert( slot );
	// remove from queue
	{
		if( alloc->queueBegin == slot ) {
			alloc->queueBegin = slot->quNext;
			if( slot->quNext ) {
				slot->quNext->quPrev = NULL;
			}
		} else if( alloc->queueEnd == slot ) {
			alloc->queueEnd = slot->quPrev;
			if( slot->quPrev ) {
				slot->quPrev->quNext = NULL;
			}
		} else {
			if( slot->quPrev ) {
				slot->quPrev->quNext = slot->quNext;
			}
			if( slot->quNext ) {
				slot->quNext->quPrev = slot->quPrev;
			}
		}
	}
	// free from hashTable
	{
		const size_t hashIndex = slot->hash % ALLOC_HASH_RESERVE;
		if( alloc->hashSlots[hashIndex] == slot ) {
			alloc->hashSlots[hashIndex] = slot->hNext;
			if( slot->hNext ) {
				slot->hPrev = NULL;
			}
		} else {
			if( slot->hPrev ) {
				slot->hPrev->hNext = slot->hNext;
			}
			if( slot->hNext ) {
				slot->hNext->hPrev = slot->hPrev;
			}
		}
	}
}

struct descriptor_set_result_s ResolveDescriptorSet( struct nri_backend_s *backend, struct frame_cmd_buffer_s *cmd, NriPipelineLayout* layout, uint32_t setIndex, struct descriptor_set_allloc_s *alloc, uint32_t hash )
{
	NriDescriptorPool *descriptorPool = NULL;
	struct descriptor_set_result_s result = { 0 };
	const size_t hashIndex = hash % ALLOC_HASH_RESERVE;
	for( struct descriptor_set_slot_s *c = alloc->hashSlots[hashIndex]; c; c = c->hNext ) {
		if( c->hash == hash ) {
			if( alloc->queueEnd == c ) {
				// already at the end of the queue
			} else if (alloc->queueBegin == c) {
				alloc->queueBegin = c->quNext;
				if( c->quNext ) {
					c->quNext->quPrev = NULL;
				}
			} else {
				if( c->quPrev ) {
					c->quPrev->quNext = c->quNext;
				}
				if( c->quNext ) {
					c->quNext->quPrev = c->quPrev;
				}
			}
			c->quNext = NULL;
			c->quPrev = alloc->queueEnd;
			if( alloc->queueEnd ) {
				alloc->queueEnd->quNext = c;
			}
			alloc->queueEnd = c;

			c->frameCount = cmd->frameCount;
			result.set = c->descriptorSet;
			result.found = true;
			assert(result.set);
			return result;
		}
	}

	if( alloc->queueBegin && cmd->frameCount > alloc->queueBegin->frameCount + NUMBER_FRAMES_FLIGHT ) {
		struct descriptor_set_slot_s *slot = alloc->queueBegin;
		DetachDescriptorSlot( alloc, slot );
		slot->frameCount = cmd->frameCount;
		slot->hash = hash;
		AttachDescriptorSlot( alloc, slot );
		result.set = slot->descriptorSet;
		result.found = false;
		assert(result.set);
		return result;
	}

	if( arrlen( alloc->reservedSlots ) == 0 ) {
		const NriDescriptorPoolDesc poolDesc = { .descriptorSetMaxNum = DESCRIPTOR_MAX_SIZE,
												 .samplerMaxNum = alloc->config.samplerMaxNum * DESCRIPTOR_MAX_SIZE,
												 .constantBufferMaxNum = alloc->config.constantBufferMaxNum * DESCRIPTOR_MAX_SIZE,
												 .dynamicConstantBufferMaxNum = alloc->config.dynamicConstantBufferMaxNum * DESCRIPTOR_MAX_SIZE,
												 .textureMaxNum = alloc->config.textureMaxNum * DESCRIPTOR_MAX_SIZE,
												 .storageTextureMaxNum = alloc->config.storageTextureMaxNum * DESCRIPTOR_MAX_SIZE,
												 .bufferMaxNum = alloc->config.bufferMaxNum * DESCRIPTOR_MAX_SIZE,
												 .storageBufferMaxNum = alloc->config.storageBufferMaxNum * DESCRIPTOR_MAX_SIZE,
												 .structuredBufferMaxNum = alloc->config.structuredBufferMaxNum * DESCRIPTOR_MAX_SIZE,
												 .storageStructuredBufferMaxNum = alloc->config.storageStructuredBufferMaxNum * DESCRIPTOR_MAX_SIZE,
												 .accelerationStructureMaxNum = alloc->config.accelerationStructureMaxNum * DESCRIPTOR_MAX_SIZE };
		NRI_ABORT_ON_FAILURE( backend->coreI.CreateDescriptorPool( backend->device, &poolDesc, &descriptorPool ) );
		NriDescriptorSet* sets[DESCRIPTOR_MAX_SIZE];
		backend->coreI.AllocateDescriptorSets( descriptorPool, layout, setIndex, sets, DESCRIPTOR_MAX_SIZE, 0 );
		arrpush(alloc->pools, descriptorPool);
		for( size_t i = 0; i < DESCRIPTOR_MAX_SIZE; i++ ) {
			struct descriptor_set_slot_s *slot = ReserveDescriptorSetSlot( alloc );
			assert(sets[i]);
			slot->descriptorSet = sets[i];
			arrpush( alloc->reservedSlots, slot );
		}
	}
	struct descriptor_set_slot_s *slot = arrpop( alloc->reservedSlots );
	slot->hash = hash;
	slot->frameCount = cmd->frameCount;

	AttachDescriptorSlot( alloc, slot );
	result.set = slot->descriptorSet;
	backend->coreI.SetDescriptorSetDebugName(slot->descriptorSet, alloc->debugName);
	result.found = false;
	assert(result.set);
	return result;
}

void FreeDescriptorSetAlloc( struct nri_backend_s *backend, struct descriptor_set_allloc_s *alloc )
{
	for( size_t i = 0; i < arrlen( alloc->blocks ); i++ ) {
		free( alloc->blocks[i] );
	}
	arrfree( alloc->blocks );
	for( size_t i = 0; i < arrlen( alloc->pools ); i++ ) {
		backend->coreI.DestroyDescriptorPool( alloc->pools[i] );
	}
	arrfree( alloc->pools );
}
