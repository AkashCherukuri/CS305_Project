#include "skewed_btb.h"

#if defined(SKEWED_BTB)

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

void get_skewed_set(uint64_t address, uint64_t LOG2_SET, int arr[L2BTB_WAY]) {
  uint64_t mask = ((1L << LOG2_SET) - 1);
  uint64_t A1 = address & mask;
  address >>= LOG2_SET;
  uint64_t A2 = address & mask;
  for (int i = 0; i < L2BTB_WAY; i++) {
    arr[i] = ((A1 ^ A2) % L2BTB_SET);
    uint64_t Low = (A1 >> (LOG2_SET - 1));
    A1 = ((2 * A1) + Low) & mask;
  }
}

uint64_t SKEWEDBTB::get_tag(uint64_t addr, uint8_t btb_type) {
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

uint64_t SKEWEDBTB::get_target(uint64_t addr, uint8_t btb_type) {
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

uint32_t SKEWEDBTB::find_victim(uint8_t btb_type, uint64_t addr) {
  if (btb_type == IS_L1BTB) {
    uint32_t set = ((addr >> 2) & ((1L << LOG2_L1BTB_SET) - 1));

    for (int way = 0; way < L1BTB_WAY; way++)
      if (L1BTB.block[set][way].valid == false)
        return way;

    for (int way = 0; way < L1BTB_WAY; way++)
      if (L1BTB.block[set][way].lru == L1BTB_WAY - 1)
        return way;

    cerr << "L1BTB No victim found! " << endl;
    assert(0);
  } else {
    int loc[L2BTB_WAY];
    get_skewed_set(addr >> 2, LOG2_L2BTB_SET, loc);
    for (int i = 0; i < L2BTB_WAY; i++) {
      assert(loc[i] < L2BTB_SET && loc[i] >= 0);
      if (!L2BTB.block[loc[i]][i].valid)
        return i;
    }

    int temp = rand() % 1000;
    for (int i = L2BTB_WAY - 1; i >= 0; i--) {
      if (temp >= (i * 1000.0 / L2BTB_WAY))
        return i;
    }
    assert(0);
  }
}

void SKEWEDBTB::update_replacement_state(uint8_t btb_type, uint32_t set,
                                     uint32_t way) {
  if (btb_type == IS_L1BTB) {
    for (int i = 0; i < L1BTB_WAY; i++) {
      if (L1BTB.block[set][i].lru < L1BTB.block[set][way].lru) {
        L1BTB.block[set][i].lru++;
      }
    }
    L1BTB.block[set][way].lru = 0;
  } else {
    L2BTB.block[set][way].lru = 0;
  }
}

uint32_t SKEWEDBTB::get_set(uint64_t address, uint8_t btb_type) {
  if (btb_type == IS_L1BTB) {
    return (uint32_t)((address >> 2) & ((1L << LOG2_L1BTB_SET) - 1));
  } else if (btb_type == IS_L2BTB) {
    int loc[L2BTB_WAY];
    get_skewed_set(address >> 2, LOG2_L2BTB_SET, loc);
    for (int i = 0; i < L2BTB_WAY; i++) {
      assert(loc[i] < L2BTB_SET && loc[i] >= 0);
      if (L2BTB.block[loc[i]][i].tag == get_tag(address, IS_L2BTB)) {
        return loc[i];
      }
    }
    return L2BTB_SET;
  }
}

uint32_t SKEWEDBTB::get_way(uint64_t address, uint32_t set, uint8_t btb_type) {
  if (btb_type == IS_L1BTB) {
    for (uint32_t way = 0; way < L1BTB_WAY; way++) {
      if (L1BTB.block[set][way].valid &&
          L1BTB.block[set][way].tag == get_tag(address, IS_L1BTB))
        return way;
    }
    return L1BTB_WAY;
  } else if (btb_type == IS_L2BTB) {
    if (set == L2BTB_SET)
      return L2BTB_WAY;

    for (int i = 0; i < L2BTB_WAY; i++) {
      if (L2BTB.block[set][i].tag == get_tag(address, IS_L2BTB))
        return i;
    }
    assert(0);
  }
}

uint32_t SKEWEDBTB::fill_btb(uint64_t trigger, uint64_t target, uint8_t branch_type,
                         uint8_t btb_type) {
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
      int loc[L2BTB_WAY];
      get_skewed_set(trigger >> 2, LOG2_L2BTB_SET, loc);
      set = loc[way];
    }

    update_replacement_state(IS_L2BTB, set, way);

    btb_unique_addr[set].insert(trigger);
    btb_fill_count[set]++;

    L2BTB.block[set][way].valid = true;
    L2BTB.block[set][way].tag = get_tag(trigger, IS_L2BTB);
    L2BTB.block[set][way].data = get_target(target, IS_L2BTB);
  }
}
#endif
