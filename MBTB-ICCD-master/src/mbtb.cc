#include "mbtb.h"

#if defined(MICRO_BTB)

map<uint64_t, set<uint64_t>> btb_unique_addr;
map<uint64_t, uint64_t> btb_fill_count;

uint64_t variantx_evicty[4][4] = {0};
uint64_t sizeofblock[9] = {0};

uint64_t correct_prediction[4] = {0}, incorrect_prediction[4] = {0};

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

#if defined(SKEWED)

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

#endif

uint64_t get_subtag(uint64_t addr, uint8_t variant) {
  addr >>= 2;
  if (variant == 0)
    return addr & ((1L << L2BTB_PARTIAL_TAG_BITS) - 1);
  else if (variant == 1)
    return addr & ((1L << 28) - 1);
  else if (variant == 2) {
    uint64_t lower_14 = addr & ((1L << 14) - 1);
    addr >>= 14;
    uint64_t upper_14 = addr & ((1L << 14) - 1);
    return upper_14 ^ lower_14;
  } else if (variant == 3) {
    uint64_t tag = 0;
    for (int i = 0; i < 4; i++) {
      tag ^= addr & ((1L << 7) - 1);
      addr >>= 7;
    }
    return tag;
  } else {
    assert(0);
  }
}

uint64_t find_absolute(uint64_t a, uint64_t b) {
  if (a > b)
    return a - b;
  else
    return b - a;
}

uint64_t MBTB::get_tag(uint64_t addr, uint8_t btb_type) {
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

uint64_t MBTB::get_target(uint64_t addr, uint8_t btb_type) {
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

uint64_t MBTB::get_target(uint64_t addr, uint64_t set, uint64_t way) {
  BLOCK &curr_block = L2BTB.block[set][way];
  for (auto it : curr_block.data_per_tag) {
    if (it.first == get_subtag(addr, curr_block.variant)) {
      if (curr_block.variant == 0)
        return it.second.data;
      else
        return addr + it.second.data;
    }
  }
  assert(0);
}

uint32_t MBTB::find_victim(uint8_t btb_type, uint64_t addr,
                           uint64_t curr_variant) {
  if (btb_type == IS_L1BTB) {
    uint32_t set = get_set(addr, IS_L1BTB);

    for (int way = 0; way < L1BTB_WAY; way++)
      if (L1BTB.block[set][way].valid == false)
        return way;

    for (int way = 0; way < L1BTB_WAY; way++)
      if (L1BTB.block[set][way].lru == L1BTB_WAY - 1)
        return way;

    cerr << "L1BTB No victim found! " << endl;
    assert(0);
  } else {

#if defined(SKEWED)
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
#else

    uint32_t set = get_set(addr, IS_L2BTB);

    for (int way = 0; way < L2BTB_WAY; way++)
      if (L2BTB.block[set][way].valid == false)
        return way;

    for (int way = 0; way < L2BTB_WAY; way++)
      if (L2BTB.block[set][way].lru == L2BTB_WAY - 1)
        return way;

    cerr << "L2BTB No victim found! " << endl;
    assert(0);
#endif
  }
}

void MBTB::update_replacement_state(uint8_t btb_type, uint32_t set,
                                    uint32_t way) {
  if (btb_type == IS_L1BTB) {
    for (int i = 0; i < L1BTB_WAY; i++) {
      if (L1BTB.block[set][i].lru < L1BTB.block[set][way].lru) {
        L1BTB.block[set][i].lru++;
      }
    }
    L1BTB.block[set][way].lru = 0;
  } else {
#if defined(SKEWED)
    L2BTB.block[set][way].lru = 0;
#else
    for (int i = 0; i < L2BTB_WAY; i++) {
      if (L2BTB.block[set][i].lru < L2BTB.block[set][way].lru) {
        L2BTB.block[set][i].lru++;
      }
    }
    L2BTB.block[set][way].lru = 0;
#endif
  }
}

uint32_t MBTB::get_set(uint64_t address, uint8_t btb_type) {
  if (btb_type == IS_L1BTB) {
    return (uint32_t)((address >> 2) & ((1L << LOG2_L1BTB_SET) - 1));
  } else if (btb_type == IS_L2BTB) {

#if defined(SKEWED)
    int loc[L2BTB_WAY];
    get_skewed_set(address >> 2, LOG2_L2BTB_SET, loc);
    for (int i = 0; i < L2BTB_WAY; i++) {
      assert(loc[i] < L2BTB_SET && loc[i] >= 0);

      if (L2BTB.block[loc[i]][i].valid) {
        for (auto it : L2BTB.block[loc[i]][i].data_per_tag)
          if (it.first == get_subtag(address, L2BTB.block[loc[i]][i].variant))
            return loc[i];
      }
    }
    return L2BTB_SET;
#else
    address >>= 2;
    uint32_t lower_14_bits = address & ((1L << 14) - 1);
    address >>= 14;
    uint32_t next_14_bits = address & ((1L << 14) - 1);
    next_14_bits ^= lower_14_bits;
    return (next_14_bits & ((1L << LOG2_L2BTB_SET) - 1));

#endif
  }
}

uint32_t MBTB::get_way(uint64_t address, uint32_t set, uint8_t btb_type) {
  if (btb_type == IS_L1BTB) {
    for (uint32_t way = 0; way < L1BTB_WAY; way++) {
      if (L1BTB.block[set][way].valid &&
          L1BTB.block[set][way].tag == get_tag(address, IS_L1BTB))
        return way;
    }
    return L1BTB_WAY;
  } else if (btb_type == IS_L2BTB) {
#if defined(SKEWED)
    if (set == L2BTB_SET)
      return L2BTB_WAY;

    int loc[L2BTB_WAY];
    get_skewed_set(address >> 2, LOG2_L2BTB_SET, loc);

    for (int i = 0; i < L2BTB_WAY; i++) {
      if (L2BTB.block[loc[i]][i].valid) {
        for (auto it : L2BTB.block[loc[i]][i].data_per_tag)
          if (it.first == get_subtag(address, L2BTB.block[loc[i]][i].variant)) {
            if (it.second.branch_ip == address) {
              correct_prediction[L2BTB.block[loc[i]][i].variant]++;
            } else {
              incorrect_prediction[L2BTB.block[loc[i]][i].variant]++;
            }
            return i;
          }
      }
    }
    assert(0);
#else
    for (uint32_t way = 0; way < L2BTB_WAY; way++) {
      if (L2BTB.block[set][way].valid) {
        for (auto it : L2BTB.block[set][way].data_per_tag)
          if (it.first == get_subtag(address, L2BTB.block[set][way].variant))
            return way;
      }
    }
    return L2BTB_WAY;
#endif
  }
}

uint32_t MBTB::get_variant(uint64_t offset) {
  int num_bits_required = 0;
  while (offset > 0) {
    num_bits_required++;
    offset >>= 1;
  }

#if defined(FOURVARIANTS)
  if (num_bits_required < 4)
    return 3;
  else if (num_bits_required < 8)
    return 2;
  else if (num_bits_required < 16)
    return 1;
  else
    return 0;
#elif defined(TWOVARIANTS)
  if (num_bits_required < 16)
    return 1;
  else
    return 0;
#else
  if (num_bits_required < 8)
    return 2;
  else if (num_bits_required < 16)
    return 1;
  else
    return 0;
#endif
}

uint32_t MBTB::find_location(uint64_t trigger, uint64_t offset,
                             uint64_t curr_variant) {
#if defined(SKEWED)
  int loc[L2BTB_WAY];
  get_skewed_set(trigger >> 2, LOG2_L2BTB_SET, loc);
  for (int i = 0; i < L2BTB_WAY; i++) {
    assert(loc[i] < L2BTB_SET && loc[i] >= 0);

    if (L2BTB.block[loc[i]][i].valid &&
        L2BTB.block[loc[i]][i].variant == curr_variant &&
        L2BTB.block[loc[i]][i].data_per_tag.size() <
            L2BTB.block[loc[i]][i].size) {
      return i;
    }
  }
#else
  uint32_t set = get_set(trigger, IS_L2BTB);

  if (curr_variant > 0) {
    for (int way = 0; way < L2BTB_WAY; way++) {
      if (L2BTB.block[set][way].valid &&
          L2BTB.block[set][way].variant == curr_variant &&
          L2BTB.block[set][way].data_per_tag.size() <
              L2BTB.block[set][way].size)
        return way;
    }
  }
#endif

  return find_victim(IS_L2BTB, trigger, curr_variant);
}

int get_variant_size(int variant) {
  int temp = 1;
  for (int i = 0; i < variant; i++)
    temp *= 2;
  return temp;
}

uint32_t MBTB::fill_btb(uint64_t trigger, uint64_t target, uint8_t branch_type,
                        uint8_t btb_type) {
  if (btb_type == IS_L1BTB || btb_type == IS_BTB_BOTH) {
    uint32_t set = get_set(trigger, IS_L1BTB);
    uint32_t way = get_way(trigger, set, IS_L1BTB);

    if (way == L1BTB_WAY) {
      way = find_victim(IS_L1BTB, trigger, 0);
    }
    update_replacement_state(IS_L1BTB, set, way);

    L1BTB.block[set][way].valid = true;
    L1BTB.block[set][way].tag = get_tag(trigger, IS_L1BTB);
    L1BTB.block[set][way].data = get_target(target, IS_L1BTB);
  }
  if (btb_type == IS_L2BTB || btb_type == IS_BTB_BOTH) {

    uint32_t set = get_set(trigger, IS_L2BTB);
    uint32_t way = get_way(trigger, set, IS_L2BTB);

    uint64_t offset = find_absolute(trigger, target);
    uint32_t curr_variant = get_variant(offset);

    if (branch_type == BRANCH_RETURN)
#if defined(FOURVARIANTS)
      curr_variant = 3;
#elif defined(TWOVARIANTS)
      curr_variant = 1;
#else
      curr_variant = 2;
#endif

    if (way == L2BTB_WAY) {

#if defined(SKEWED)
      way = find_location(trigger, offset, curr_variant);
      int loc[L2BTB_WAY];
      get_skewed_set(trigger >> 2, LOG2_L2BTB_SET, loc);
      set = loc[way];
#else
      way = find_location(trigger, offset, curr_variant);
#endif
    }

    update_replacement_state(IS_L2BTB, set, way);

    btb_unique_addr[set].insert(trigger);
    btb_fill_count[set]++;

    BLOCK &curr_block = L2BTB.block[set][way];

    curr_block.valid = true;
    if (curr_variant != 0) {
      if (curr_variant == curr_block.variant) {
        if (curr_block.data_per_tag.size() < curr_block.size) {
          mbtb_branch_track temp;
          temp.data = target - trigger;
          temp.branch_type = branch_type;
          temp.branch_ip = trigger;
          curr_block.data_per_tag[get_subtag(trigger, curr_variant)] = temp;
        } else {
          curr_block.data_per_tag.erase(curr_block.data_per_tag.begin());
          mbtb_branch_track temp;
          temp.data = target - trigger;
          temp.branch_type = branch_type;
          temp.branch_ip = trigger;
          curr_block.data_per_tag[get_subtag(trigger, curr_variant)] = temp;
        }
        curr_block.size = get_variant_size(curr_variant);
        // cout << curr_block.data_per_tag.size() << " " << curr_block.size <<
        // endl;
        assert(curr_block.data_per_tag.size() <= curr_block.size);
      } else {
        if (curr_block.variant <= 3) {
          variantx_evicty[curr_variant][curr_block.variant]++;
        }

        curr_block.data_per_tag.clear();
        curr_block.size = get_variant_size(curr_variant);
        mbtb_branch_track temp;
        temp.data = target - trigger;
        temp.branch_type = branch_type;
        temp.branch_ip = trigger;
        curr_block.data_per_tag[get_subtag(trigger, curr_variant)] = temp;
      }

      assert(curr_block.data_per_tag.size() <= 8);
      sizeofblock[curr_block.data_per_tag.size()]++;
    } else {
      if (curr_block.variant <= 3) {
        variantx_evicty[curr_variant][curr_block.variant]++;
      }
      curr_block.data_per_tag.clear();
      curr_block.size = 1;
      mbtb_branch_track temp;
      temp.data = target;
      temp.branch_type = branch_type;
      temp.branch_ip = trigger;
      curr_block.data_per_tag[get_subtag(trigger, curr_variant)] = temp;
    }
    curr_block.variant = curr_variant;
  }
}

#if defined(MBTB_PREDECODE)

void MBTB::operate_predecode() {
  for (int i = 0; i < PREDECODES_PER_CYCLE; ++i) {
    if (predecode_buffer.occupancy == 0)
      break;

    uint32_t set = predecode_buffer.entry[predecode_buffer.head].set;
    uint64_t branch_ip =
        predecode_buffer.entry[predecode_buffer.head].branch_ip;
    uint64_t blk = branch_ip >> LOG2_BLOCK_SIZE;

    to_predecode++;

    bool predecoded = false;
    for (auto ins : branches[blk]) {
      assert((ins.ip >> LOG2_BLOCK_SIZE) == blk);
      if (ins.branch_type == NOT_BRANCH)
        continue;

      if (ins.branch_type == BRANCH_DIRECT_JUMP ||
          ins.branch_type == BRANCH_DIRECT_CALL ||
          ins.branch_type == BRANCH_CONDITIONAL ||
          ins.branch_type == BRANCH_RETURN) {
        uint64_t offset = find_absolute(ins.ip, ins.branch_target);
        uint32_t curr_variant = get_variant(offset);
        if (curr_variant >= 2 || ins.branch_type == BRANCH_RETURN) {
          fill_btb(ins.ip, ins.branch_target, ins.branch_type, IS_L2BTB);
          predecoded = true;
        }
      }

      if (predecoded)
        predecoding_something++;
    }

    predecode_buffer.head = (predecode_buffer.head + 1) % PREDECODE_BUFFER_LEN;
    predecode_buffer.occupancy--;
  }
}

int MBTB::predecode(uint64_t branch_ip, uint32_t set) {

  if (predecode_buffer.occupancy == PREDECODE_BUFFER_LEN) {
    predecode_request_dropped++;
    return -1;
  }

  predecode_buffer.entry[predecode_buffer.tail].branch_ip = branch_ip;
  predecode_buffer.entry[predecode_buffer.tail].set = set;

  predecode_buffer.tail = (predecode_buffer.tail + 1) % PREDECODE_BUFFER_LEN;
  predecode_buffer.occupancy++;

  return 0;
}

#endif

#endif
