#include "baseline_btb.h"

#if defined(BASELINEBTB) || defined(PERFECT_BTB) || defined(DIV_CONQ)

map<uint64_t, set<uint64_t>> btb_unique_addr;
map<uint64_t, uint64_t> btb_fill_count;

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

uint64_t BASELINE_BTB::get_tag(uint64_t addr, uint8_t btb_type) {
  addr >>= 2;
  if (btb_type == IS_L1BTB) {
    addr >>= LOG2_L1BTB_SET;
    return addr;
  } else if (btb_type == IS_L2BTB) {
    addr = addr & ((1L << L2BTB_PARTIAL_TAG_BITS) - 1);
    return addr;
  } else
    assert(0);
}

uint64_t BASELINE_BTB::get_target(uint64_t addr, uint8_t btb_type) {
  if (btb_type == IS_L1BTB) {
    return addr;
  } else if (btb_type == IS_L2BTB) // Partial target
  {
    addr = addr & ((1L << L2BTB_PARTIAL_TARGET_BITS) - 1);
    return addr;
  } else {
    assert(0);
  }
}

uint32_t BASELINE_BTB::find_victim(uint8_t btb_type, uint64_t addr) {
  uint32_t set = get_set(addr, btb_type);
  CACHE &BTB = btb_type == IS_L1BTB ? L1BTB : L2BTB;

  for (int way = 0; way < BTB.NUM_WAY; way++)
    if (BTB.block[set][way].valid == false)
      return way;

  for (int way = 0; way < BTB.NUM_WAY; way++)
    if (BTB.block[set][way].lru == BTB.NUM_WAY - 1)
      return way;

  cerr << btb_type << " BTB No victim found! " << endl;
  assert(0);
}

void BASELINE_BTB::update_replacement_state(uint8_t btb_type, uint32_t set,
                                            uint32_t way) {
  CACHE &BTB = btb_type == IS_L1BTB ? L1BTB : L2BTB;

  for (int i = 0; i < BTB.NUM_WAY; i++) {
    if (BTB.block[set][i].lru < BTB.block[set][way].lru) {
      BTB.block[set][i].lru++;
    }
  }
  BTB.block[set][way].lru = 0;
}

uint32_t BASELINE_BTB::get_set(uint64_t address, uint8_t btb_type) {
  address >>= 2;
  if (btb_type == IS_L1BTB) {
    return (uint32_t)(address & ((1L << LOG2_L1BTB_SET) - 1));
  } else if (btb_type == IS_L2BTB) {
    uint32_t lower_14_bits = address & ((1L << 14) - 1);
    address >>= 14;
    uint32_t next_14_bits = address & ((1L << 14) - 1);
    next_14_bits ^= lower_14_bits;
    return (next_14_bits & ((1L << LOG2_L2BTB_SET) - 1));
  } else
    assert(0);
}

uint32_t BASELINE_BTB::get_way(uint64_t addr, uint32_t set, uint8_t btb_type) {
  CACHE &BTB = btb_type == IS_L1BTB ? L1BTB : L2BTB;

  for (uint32_t way = 0; way < BTB.NUM_WAY; way++) {
    if (BTB.block[set][way].valid &&
        BTB.block[set][way].tag == get_tag(addr, btb_type))
      return way;
  }

  if (btb_type == IS_L1BTB)
    return L1BTB_WAY;
  else
    return L2BTB_WAY;
}

uint32_t BASELINE_BTB::fill_btb(uint64_t trigger, uint64_t target,
                                uint8_t branch_type, uint8_t btb_type) {
  if (btb_type == IS_L1BTB || btb_type == IS_BTB_BOTH) {
    uint32_t set = get_set(trigger, IS_L1BTB);
    uint32_t way = get_way(trigger, set, IS_L1BTB);

    if (way == L1BTB_WAY) {
      way = find_victim(IS_L1BTB, trigger);
    }

    update_replacement_state(IS_L1BTB, set, way);

    L1BTB.block[set][way].valid = true;
    L1BTB.block[set][way].tag = get_tag(trigger, IS_L1BTB);
    L1BTB.block[set][way].data = get_target(target, IS_L1BTB);
  }
  if (btb_type == IS_L2BTB || btb_type == IS_BTB_BOTH) {
    uint32_t set = get_set(trigger, IS_L2BTB);
    uint32_t way = get_way(trigger, set, IS_L2BTB);

    if (way == L2BTB_WAY) {
      way = find_victim(IS_L2BTB, trigger);
    }

    btb_fill_count[set]++;
    btb_unique_addr[set].insert(trigger);

    update_replacement_state(IS_L2BTB, set, way);

    L2BTB.block[set][way].valid = true;
    L2BTB.block[set][way].tag = get_tag(trigger, IS_L2BTB);
    L2BTB.block[set][way].data = get_target(target, IS_L2BTB);
  }
}

#endif
