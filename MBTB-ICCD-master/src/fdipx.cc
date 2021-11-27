#include "fdipx.h"

#if defined(FDIPX_BTB)

map<uint64_t, set<uint64_t>> btb_unique_addr;
map<uint64_t, uint64_t> btb_fill_count;

map<uint64_t, set<uint64_t>> ip_per_tag;

void RAS::push(uint64_t ret_addr, uint64_t call_addr, uint64_t target,
               uint64_t bb_size) {
  if (head >= MAX_RAS_DEPTH - 1) {
    max_depth_exceeded_cnt++;
    return;
  }
  head++;

  return_address[head] = ret_addr;
  target_address[head] = target;
  caller_trigger[head] = call_addr;
}

void RAS::pop() {
  if (head < 0) {
    return;
  }
  --head;
}

uint64_t find_absolute(uint64_t a, uint64_t b) {
  if (a > b)
    return a - b;
  else
    return b - a;
}

uint64_t FDIPX::get_tag(uint64_t addr, uint8_t btb_type) {
  uint64_t temp = addr;
  addr >>= 2;
  if (btb_type == IS_L1BTB) {
    addr >>= LOG2_L1BTB_SET;
    return addr;
  } else if (btb_type == IS_L2BTB) {
    uint64_t lower_8_bits = addr & ((1L << 8) - 1);
    addr >>= 8;
    uint64_t upper_8_bits = 0;
    while (addr > 0) {
      upper_8_bits ^= (addr & ((1L << 8) - 1));
      addr >>= 8;
    }
    upper_8_bits <<= 8;
    upper_8_bits |= lower_8_bits;

    return upper_8_bits;
  } else
    assert(0);
}

uint32_t FDIPX::find_victim(CACHE &cache, uint64_t addr, uint8_t btb_type) {
  uint32_t set = get_set(addr, btb_type, cache.NUM_SET);

  for (int way = 0; way < cache.NUM_WAY; way++)
    if (cache.block[set][way].valid == false)
      return way;

  for (int way = 0; way < cache.NUM_WAY; way++)
    if (cache.block[set][way].lru == cache.NUM_WAY - 1)
      return way;

  cerr << cache.NAME << " No victim found! " << endl;
  assert(0);
}

void FDIPX::update_replacement_state(CACHE &cache, uint32_t set, uint32_t way) {
  for (int i = 0; i < cache.NUM_WAY; i++) {
    if (cache.block[set][i].lru < cache.block[set][way].lru) {
      cache.block[set][i].lru++;
    }
  }
  cache.block[set][way].lru = 0;
}

uint32_t FDIPX::get_set(uint64_t address, uint8_t btb_type, uint32_t num_set) {
  if (btb_type == IS_L1BTB) {
    return (uint32_t)((address >> 2) & ((1L << LOG2_L1BTB_SET) - 1));
  } else if (btb_type == IS_L2BTB) {
    // return (address >> 2) % num_set;

    address >>= 2;

    uint32_t lower_14_bits = address & ((1L << 14) - 1);
    address >>= 14;
    uint32_t next_14_bits = address & ((1L << 14) - 1);
    next_14_bits ^= lower_14_bits;
    return (next_14_bits % num_set);
  }
}

uint32_t FDIPX::get_way(CACHE &cache, uint64_t address, uint32_t set,
                        uint8_t btb_type) {
  for (uint32_t way = 0; way < cache.NUM_WAY; way++) {
    if (cache.block[set][way].valid &&
        cache.block[set][way].tag == get_tag(address, btb_type))
      return way;
  }
  return cache.NUM_WAY;
}

uint32_t FDIPX::get_variant(uint64_t offset) {
  int num_bits_required = 0;
  while (offset > 0) {
    num_bits_required++;
    offset >>= 1;
  }
  if (num_bits_required <= L2BTB1_OFFSET_SIZE)
    return 0;
  else if (num_bits_required <= L2BTB2_OFFSET_SIZE)
    return 1;
  else if (num_bits_required <= L2BTB3_OFFSET_SIZE)
    return 2;
  else
    return 3;
}

uint32_t FDIPX::fill_btb(uint64_t trigger, uint64_t target, uint8_t branch_type,
                         uint8_t btb_type) {
  if (btb_type == IS_L1BTB || btb_type == IS_BTB_BOTH) {
    uint32_t set = get_set(trigger, IS_L1BTB, L1BTB.NUM_SET);
    uint32_t way = get_way(L1BTB, trigger, set, IS_L1BTB);

    if (way == L1BTB_WAY) {
      way = find_victim(L1BTB, trigger, IS_L1BTB);
    }
    update_replacement_state(L1BTB, set, way);

    L1BTB.block[set][way].valid = true;
    L1BTB.block[set][way].tag = get_tag(trigger, IS_L1BTB);
    L1BTB.block[set][way].data = target;
  }
  if (btb_type == IS_L2BTB || btb_type == IS_BTB_BOTH) {

    uint32_t set = 0, way = 0;
    int btb_type = -1;
    for (int i = 0; i < 4; i++) {
      set = get_set(trigger, IS_L2BTB, L2BTB[i]->NUM_SET);
      assert(set < L2BTB[i]->NUM_SET);
      way = get_way(*L2BTB[i], trigger, set, IS_L2BTB);
      if (way < L2BTB[i]->NUM_WAY) {
        btb_type = i;
        break;
      }
    }

    uint64_t offset = find_absolute(trigger, target);
    uint32_t curr_variant = get_variant(offset);

    if (btb_type != -1 && btb_type != curr_variant) {
      L2BTB[btb_type]->block[set][way].valid = false;
      btb_type = curr_variant;
      set = get_set(trigger, IS_L2BTB, L2BTB[btb_type]->NUM_SET);
      way = find_victim(*L2BTB[btb_type], trigger, IS_L2BTB);
    }

    if (btb_type == -1) {
      btb_type = curr_variant;
      set = get_set(trigger, IS_L2BTB, L2BTB[btb_type]->NUM_SET);
      way = find_victim(*L2BTB[btb_type], trigger, IS_L2BTB);
    }

    update_replacement_state(*L2BTB[btb_type], set, way);

    BLOCK &curr_block = L2BTB[btb_type]->block[set][way];

    curr_block.valid = true;
    curr_block.tag = get_tag(trigger, IS_L2BTB);
    curr_block.data = target - trigger;
  }
}
#endif
