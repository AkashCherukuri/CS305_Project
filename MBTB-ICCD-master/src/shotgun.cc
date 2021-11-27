#include "ooo_cpu.h"
#include "set.h"
#include "uncore.h"

#if defined(SHOTGUN_BTB)

#include <vector>

#include <iterator>

// SHOTGUN Class methods

uint64_t incoming_addr = 0;

set<uint64_t> ubtb_set_used, cbtb_set_used, rib_set_used;

set<uint64_t> ubtb_unique_addr[UBTB_SET], cbtb_unique_addr[CBTB_SET],
    rib_unique_addr[RIB_SET];
uint64_t ubtb_fill_count[UBTB_SET], cbtb_fill_count[CBTB_SET],
    rib_fill_count[RIB_SET];

void O3_CPU::btb_update() {
  if (ROB.occupancy == 0)
    return;

#if defined(BASELINE_SHOTGUN_BTB)

  // record cache accesses in footprints
  uint64_t accessed_blk = ROB.entry[ROB.head].ip >> LOG2_BLOCK_SIZE;
  int ubtb_set = BTB.active_ubtb_set;
  int ubtb_way = BTB.active_ubtb_way;
  int index;
  uint64_t trigger_blk;

  if (BTB.active_footprint == CALLER_FOOTPRINT) {
    trigger_blk =
        (BTB.ubtb[ubtb_set][ubtb_way].target.to_ulong() << LOG2_INSTR_SIZE) >>
        LOG2_BLOCK_SIZE;

    if (accessed_blk < trigger_blk &&
        (trigger_blk - accessed_blk) <= SPACIAL_FOOTPRINT_PREV) {
      index = SPACIAL_FOOTPRINT_PREV - (trigger_blk - accessed_blk);
    } else if (trigger_blk < accessed_blk &&
               (accessed_blk - trigger_blk) <= SPACIAL_FOOTPRINT_FWD) {
      index = (SPACIAL_FOOTPRINT_PREV - 1) + (accessed_blk - trigger_blk);
    }

    BTB.ubtb[ubtb_set][ubtb_way].call_footprint[index] = 1;

  } else {
    trigger_blk =
        (BTB.ubtb[ubtb_set][ubtb_way].tag.to_ulong() << LOG2_UBTB_SET);
    trigger_blk =
        ((trigger_blk | ubtb_set) << LOG2_INSTR_SIZE) >> LOG2_BLOCK_SIZE;

    if (accessed_blk < trigger_blk &&
        (trigger_blk - accessed_blk) <= SPACIAL_FOOTPRINT_PREV) {
      index = SPACIAL_FOOTPRINT_PREV - (trigger_blk - accessed_blk);
    } else if (trigger_blk < accessed_blk &&
               (accessed_blk - trigger_blk) <= SPACIAL_FOOTPRINT_FWD) {
      index = (SPACIAL_FOOTPRINT_PREV - 1) + (accessed_blk - trigger_blk);
    }

    BTB.ubtb[ubtb_set][ubtb_way].return_footprint[index] = 1;
  }
#endif
}

void get_skewed_set(uint64_t address, uint64_t LOG2_SET, int arr[4]) {
  uint64_t mask = ((1L << LOG2_SET) - 1);
  uint64_t A1 = address & mask;
  address >>= LOG2_SET;
  uint64_t A2 = address & mask;
  for (int i = 0; i < 4; i++) {
    arr[i] = (A1 ^ A2);
    uint64_t Low = (A1 >> (LOG2_SET - 1));
    A1 = ((2 * A1) + Low) & mask;
  }
}

// SHOTGUN Class Methods

uint32_t SHOTGUN::get_set(uint64_t address, uint8_t btb_type) {
  address >>= LOG2_INSTR_SIZE;
  int loc[4];
  if (btb_type == IS_UBTB) {

#if defined(SKEWED_SHOTGUN_BTB)
    get_skewed_set(address, LOG2_UBTB_SET, loc);
    assert(UBTB_WAY == 1);
    for (int i = 0; i < 4; i++) {
      assert(loc[i] < UBTB_SET && loc[i] >= 0);
      if (ubtb[loc[i]][0].tag == bitset<UBTB_TAG>(address))
        return loc[i];
    }

    return UBTB_SET;
#else

    return (uint32_t)(address & ((1 << LOG2_UBTB_SET) - 1));
#endif
  }

  if (btb_type == IS_CBTB) {

#if defined(SKEWED_SHOTGUN_BTB)
    get_skewed_set(address, LOG2_CBTB_SET, loc);
    assert(CBTB_WAY == 1);
    for (int i = 0; i < 4; i++) {
      assert(loc[i] < CBTB_SET && loc[i] >= 0);
      if (cbtb[loc[i]][0].tag == bitset<CBTB_TAG>(address))
        return loc[i];
    }

    return CBTB_SET;
#else
    return (uint32_t)(address & ((1 << LOG2_CBTB_SET) - 1));
#endif
  }

  if (btb_type == IS_RIB) {
#if defined(SKEWED_SHOTGUN_BTB)
    get_skewed_set(address, LOG2_RIB_SET, loc);
    assert(RIB_WAY == 1);
    for (int i = 0; i < 4; i++) {
      assert(loc[i] < RIB_SET && loc[i] >= 0);
      if (rib[loc[i]][0].tag == bitset<RIB_TAG>(address))
        return loc[i];
    }

    return RIB_SET;
#else

    return (uint32_t)(address & ((1 << LOG2_RIB_SET) - 1));
#endif
  }
}

uint32_t SHOTGUN::get_way(uint64_t address, uint32_t set, uint8_t btb_type) {
  if (btb_type == IS_UBTB) // && set < UBTB_SET)
  {
#if defined(SKEWED_SHOTGUN_BTB)
    address >>= LOG2_INSTR_SIZE;
    assert(UBTB_WAY == 1);

    if (set == UBTB_SET)
      return UBTB_WAY;

    return 0;

#else
    address >>= (LOG2_INSTR_SIZE + LOG2_UBTB_SET);
    for (uint32_t way = 0; way < UBTB_WAY; way++)
      if (ubtb[set][way].valid &&
          ubtb[set][way].tag == bitset<UBTB_TAG>(address))
        return way;

    return UBTB_WAY;
#endif
  } else if (btb_type == IS_CBTB) // && set < CBTB_SET)
  {
#if defined(SKEWED_SHOTGUN_BTB)
    address >>= LOG2_INSTR_SIZE;
    assert(CBTB_WAY == 1);

    if (set == CBTB_SET)
      return CBTB_WAY;

    return 0;
#else
    address >>= (LOG2_INSTR_SIZE + LOG2_CBTB_SET);
    for (uint32_t way = 0; way < CBTB_WAY; way++)
      if (cbtb[set][way].valid &&
          cbtb[set][way].tag == bitset<CBTB_TAG>(address))
        return way;

    return CBTB_WAY;
#endif
  } else if (btb_type == IS_RIB) // && set < RIB_SET)
  {
#if defined(SKEWED_SHOTGUN_BTB)
    address >>= LOG2_INSTR_SIZE;
    assert(RIB_WAY == 1);

    if (set == RIB_SET)
      return RIB_WAY;

    return 0;
#else
    address >>= (LOG2_INSTR_SIZE + LOG2_RIB_SET);
    for (uint32_t way = 0; way < RIB_WAY; way++)
      if (rib[set][way].valid && rib[set][way].tag == bitset<RIB_TAG>(address))
        return way;

    return RIB_WAY;
#endif
  } else {
    cerr << "\n SHOTGUN::get_way something wrong with args\n";
    cerr << "btb_type: " << btb_type << ", address: " << address
         << ", set: " << set << "\n";
    assert(0);
  }
}

uint32_t SHOTGUN::fill_btb(uint64_t trigger, uint64_t target, uint8_t btb_type,
                           bool branch_type) {
  uint32_t set = get_set(trigger, btb_type);
  uint32_t way = get_way(trigger, set, btb_type);

  uint32_t max_ways;
  if (btb_type == IS_UBTB)
    max_ways = UBTB_WAY;
  else if (btb_type == IS_CBTB)
    max_ways = CBTB_WAY;
  else if (btb_type == IS_RIB)
    max_ways = RIB_WAY;
  else {
    cerr << "SHOTGUN fill_btb type is " << btb_type << endl;
    assert(0);
  }

  if (way == max_ways) {
    incoming_addr = trigger >> LOG2_INSTR_SIZE;
    way = find_victim(btb_type, set);
#if defined(SKEWED_SHOTGUN_BTB)
    if (btb_type == IS_CBTB || btb_type == IS_RIB || btb_type == IS_UBTB) {
      set = way;
      way = 0;
    }
#endif

    update_replacement_state(btb_type, set, way);

    if (btb_type == IS_UBTB) {
      ubtb_set_used.insert(set);
      ubtb_unique_addr[set].insert(trigger >> LOG2_INSTR_SIZE);
      ubtb_fill_count[set]++;
      ubtb[set][way].valid = true;
      ubtb[set][way].type = branch_type;
#if defined(SKEWED_SHOTGUN_BTB)
      ubtb[set][way].tag = bitset<UBTB_TAG>(trigger >> (LOG2_INSTR_SIZE));
#else
      ubtb[set][way].tag =
          bitset<UBTB_TAG>(trigger >> (LOG2_INSTR_SIZE + LOG2_UBTB_SET));
#endif
      ubtb[set][way].target = bitset<TARGET>(target >> LOG2_INSTR_SIZE);
      uint64_t bb_size = 0;
      auto it = basic_block_size_per_ip.find(target);
      if (it != basic_block_size_per_ip.end())
        bb_size = it->second;
      bb_size /= 4;
      if (bb_size >= 32)
        bb_size = 31;
      // cout << "UBTB: " << hex << trigger << dec <<  " " << bb_size << endl;
      ubtb[set][way].basic_block_size = bitset<BB_SIZE>(bb_size);
      ubtb[set][way].call_footprint = bitset<SPACIAL_FOOTPRINT>(0);
      ubtb[set][way].return_footprint = bitset<SPACIAL_FOOTPRINT>(0);
    } else if (btb_type == IS_CBTB) {
      cbtb_set_used.insert(set);
      cbtb_unique_addr[set].insert(trigger >> LOG2_INSTR_SIZE);
      cbtb_fill_count[set]++;
      cbtb[set][way].valid = true;
#if defined(SKEWED_SHOTGUN_BTB)
      cbtb[set][way].tag = bitset<CBTB_TAG>(trigger >> (LOG2_INSTR_SIZE));
#else
      cbtb[set][way].tag =
          bitset<CBTB_TAG>(trigger >> (LOG2_INSTR_SIZE + LOG2_CBTB_SET));
#endif

      cbtb[set][way].target = bitset<TARGET>(target >> LOG2_INSTR_SIZE);
      uint64_t bb_size = 0;
      auto it = basic_block_size_per_ip.find(target);
      if (it != basic_block_size_per_ip.end())
        bb_size = it->second;
      bb_size /= 4;
      if (bb_size >= 32)
        bb_size = 31;
      cbtb[set][way].basic_block_size = bitset<BB_SIZE>(bb_size);
    } else if (btb_type == IS_RIB) {
      rib_set_used.insert(set);
      rib_unique_addr[set].insert(trigger >> LOG2_INSTR_SIZE);
      rib_fill_count[set]++;
      rib[set][way].valid = true;
#if defined(SKEWED_SHOTGUN_BTB)
      rib[set][way].tag = bitset<RIB_TAG>(trigger >> (LOG2_INSTR_SIZE));
#else
      rib[set][way].tag =
          bitset<RIB_TAG>(trigger >> (LOG2_INSTR_SIZE + LOG2_RIB_SET));
#endif
      uint64_t bb_size = 0;
      auto it = basic_block_size_per_ip.find(target);
      if (it != basic_block_size_per_ip.end())
        bb_size = it->second;
      bb_size /= 4;
      if (bb_size >= 32)
        bb_size = 31;
      rib[set][way].basic_block_size = bitset<BB_SIZE>(bb_size);
    }
  } else {
    // valid branch already present, update it if needed
    if (btb_type == IS_UBTB && ubtb[set][way].target != target) {
      ubtb[set][way].target = bitset<TARGET>(target >> LOG2_INSTR_SIZE);
      ubtb[set][way].type = branch_type;
      uint64_t bb_size = 0;
      auto it = basic_block_size_per_ip.find(target);
      if (it != basic_block_size_per_ip.end())
        bb_size = it->second;
      bb_size /= 4;
      if (bb_size >= 32)
        bb_size = 31;
      ubtb[set][way].basic_block_size = bitset<BB_SIZE>(bb_size);
      ubtb[set][way].call_footprint = bitset<SPACIAL_FOOTPRINT>(0);
      ubtb[set][way].return_footprint = bitset<SPACIAL_FOOTPRINT>(0);
    } else if (btb_type == IS_CBTB && cbtb[set][way].target != target) {
      cbtb[set][way].target = bitset<TARGET>(target >> LOG2_INSTR_SIZE);
      uint64_t bb_size = 0;
      auto it = basic_block_size_per_ip.find(target);
      if (it != basic_block_size_per_ip.end())
        bb_size = it->second;
      bb_size /= 4;
      if (bb_size >= 32)
        bb_size = 31;
      cbtb[set][way].basic_block_size = bitset<BB_SIZE>(bb_size);
    } else if (btb_type == IS_RIB) {
      uint64_t bb_size = 0;
      auto it = basic_block_size_per_ip.find(target);
      if (it != basic_block_size_per_ip.end())
        bb_size = it->second;
      bb_size /= 4;
      if (bb_size >= 32)
        bb_size = 31;
      rib[set][way].basic_block_size = bitset<BB_SIZE>(bb_size);
    }
  }

  assert(way != max_ways);
  return way;
}
uint64_t to_predecode = 0;
uint64_t predecoding_something = 0, conditional_predecode = 0,
         direct_call_predecode = 0, direct_branch_predecode = 0;
void SHOTGUN::operate_predecode() {
  uint64_t blk_addr, missed_ip;
  uint32_t l1iset;

  for (int i = 0; i < PREDECODE_WIDTH; ++i) {
    if (predecode_buffer_occupancy == 0)
      return;

    blk_addr = predecode_buffer[predecode_buffer_head].blk_addr;
    missed_ip = predecode_buffer[predecode_buffer_head].missed_ip;
    l1iset = (uint32_t)((blk_addr >> 6) & ((1 << lg2(L1I_SET)) - 1));

    to_predecode++;

    for (int x = 0; x < conditionals[l1iset].size(); ++x) {
      Conds instr = conditionals[l1iset][x];

      if ((instr.ip >> 6) != (blk_addr >> 6))
        continue;

      predecoding_something++;

      if (instr.branch_type == BRANCH_CONDITIONAL) {
        fill_btb(instr.ip, instr.branch_target, IS_CBTB, 0); // ignore retval
        conditional_predecode++;
      }

      else if (instr.branch_type == BRANCH_DIRECT_JUMP) {
#if !defined(BASELINE_SHOTGUN_BTB)
        fill_btb(instr.ip, instr.branch_target, IS_UBTB, UNCONDITIONAL);
#else
        if (instr.ip == missed_ip)
          fill_btb(instr.ip, instr.branch_target, IS_UBTB, UNCONDITIONAL);
        else
          add_pf(instr.ip, instr.branch_target, IS_UBTB, UNCONDITIONAL);
#endif
        direct_branch_predecode++;

      } else if (instr.branch_type == BRANCH_DIRECT_CALL) {
#if !defined(BASELINE_SHOTGUN_BTB)
        fill_btb(instr.ip, instr.branch_target, IS_UBTB, CALL);
#else
        if (instr.ip == missed_ip)
          fill_btb(instr.ip, instr.branch_target, IS_UBTB, CALL);
        else
          add_pf(instr.ip, instr.branch_target, IS_UBTB, CALL);
#endif
        direct_call_predecode++;
      }
    }

    predecode_buffer[predecode_buffer_head].blk_addr = 0;
    predecode_buffer[predecode_buffer_head].missed_ip = 0;

    predecode_buffer_occupancy--;
    predecode_buffer_head = (predecode_buffer_head + 1) % PREDECODE_BUFFER_LEN;
  }
}

uint32_t SHOTGUN::predecode(uint64_t blk_addr, uint64_t missed_ip) {

#if !defined(SHOTGUN_PREDECODING)
  return PREDECODE_BUFFER_LEN;
#endif

  if (predecode_buffer_occupancy == PREDECODE_BUFFER_LEN)
    return PREDECODE_BUFFER_LEN;

  predecode_buffer_tail = (predecode_buffer_tail + 1) % PREDECODE_BUFFER_LEN;
  predecode_buffer[predecode_buffer_tail].blk_addr = blk_addr;
  predecode_buffer[predecode_buffer_tail].missed_ip = missed_ip;
  predecode_buffer_occupancy++;

  return predecode_buffer_tail;
}

void SHOTGUN::add_pf(uint64_t ip, uint64_t target, uint8_t btb_type,
                     bool uncond_branch_type) {
  uint32_t way = 0;

  way = get_way(ip, get_set(ip, btb_type), btb_type);

  // no need to add if already present in BTB
  if (way == MAX_WAYS(btb_type))
    return;

  // invalidate all entries that are already present in BTB
  for (uint32_t i = 0; i < PREFETCH_BUFFER_LEN; i++) {
    if (pbuf[i].valid == false)
      continue;

    way = get_way(pbuf[i].ip, get_set(pbuf[i].ip, pbuf[i].btb_type),
                  pbuf[i].btb_type);
    if (way != MAX_WAYS(btb_type))
      pbuf[i].valid = false;
  }

  // find lru victim
  for (way = 0; way < PREFETCH_BUFFER_LEN; way++)
    if (pbuf[way].valid == false)
      break;

  if (way == PREFETCH_BUFFER_LEN)
    for (way = 0; way < PREFETCH_BUFFER_LEN; way++)
      if (pbuf[way].lru == PREFETCH_BUFFER_LEN - 1)
        break;

  assert(way < PREFETCH_BUFFER_LEN);

  // update replacement state
  for (uint32_t i = 0; i < PREFETCH_BUFFER_LEN; ++i)
    if (pbuf[i].lru < pbuf[way].lru)
      pbuf[i].lru++;

  pbuf[way].lru = 0;

  // insert data
  pbuf[way].valid = true;
  pbuf[way].ip = ip;
  pbuf[way].target = target;
  pbuf[way].btb_type = btb_type;
  pbuf[way].uncond_branch_type = uncond_branch_type;
}

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
  basic_block_size[head] = bitset<BB_SIZE>(bb_size);
}

void RAS::pop() {
  if (head < 0) {
    return;
  }
  --head;
}

// SHOTGUN Class Methods

uint32_t SHOTGUN::find_victim(uint8_t btb_type, uint32_t set) {
  uint32_t way = 0;

  int loc[4] = {0};

  if (btb_type == IS_UBTB) {
#if defined(SKEWED_SHOTGUN_BTB)
    get_skewed_set(incoming_addr, LOG2_UBTB_SET, loc);
    assert(UBTB_WAY == 1);
    for (int i = 0; i < 4; i++)
      assert(loc[i] < UBTB_SET && loc[i] >= 0);

    int temp = rand() % 1000;

    if (temp < 250)
      return loc[0];
    if (temp < 500)
      return loc[1];
    if (temp < 750)
      return loc[2];

    return loc[3];
#else
    // fill invalid line first
    for (way = 0; way < UBTB_WAY; way++)
      if (ubtb[set][way].valid == false)
        break;

    // LRU victim
    if (way == UBTB_WAY)
      for (way = 0; way < UBTB_WAY; way++)
        if ((int)(ubtb[set][way].lru.to_ulong()) == UBTB_WAY - 1)
          break;

    if (way == UBTB_WAY) {
      cerr << "[SHOTGUN UBTB] " << __func__ << " no victim! set: " << set
           << endl;
      assert(0);
    }
#endif
  } else if (btb_type == IS_CBTB) {
#if defined(SKEWED_SHOTGUN_BTB)
    get_skewed_set(incoming_addr, LOG2_CBTB_SET, loc);
    assert(CBTB_WAY == 1);
    for (int i = 0; i < 4; i++)
      assert(loc[i] < CBTB_SET && loc[i] >= 0);

    int temp = rand() % 1000;

    if (temp < 250)
      return loc[0];
    if (temp < 500)
      return loc[1];
    if (temp < 750)
      return loc[2];

    return loc[3];
#else
    // fill invalid line first
    for (way = 0; way < CBTB_WAY; way++)
      if (cbtb[set][way].valid == false)
        break;

    // LRU victim
    if (way == CBTB_WAY)
      for (way = 0; way < CBTB_WAY; way++)
        if ((int)(cbtb[set][way].lru.to_ulong()) == CBTB_WAY - 1)
          break;

    if (way == CBTB_WAY) {
      cerr << "[SHOTGUN CBTB] " << __func__ << " no victim! set: " << set
           << endl;
      for (way = 0; way < CBTB_WAY; way++)
        cerr << way << " " << (int)(cbtb[set][way].lru.to_ulong()) << endl;
      assert(0);
    }
#endif
  } else if (btb_type == IS_RIB) {
#if defined(SKEWED_SHOTGUN_BTB)
    get_skewed_set(incoming_addr, LOG2_RIB_SET, loc);
    assert(UBTB_WAY == 1);
    for (int i = 0; i < 4; i++)
      assert(loc[i] < RIB_SET && loc[i] >= 0);

    int temp = rand() % 1000;

    if (temp < 250)
      return loc[0];
    if (temp < 500)
      return loc[1];
    if (temp < 750)
      return loc[2];

    return loc[3];

#else

    // fill invalid line first
    for (way = 0; way < RIB_WAY; way++)
      if (rib[set][way].valid == false)
        break;

    // LRU victim
    if (way == RIB_WAY)
      for (way = 0; way < RIB_WAY; way++)
        if ((int)(rib[set][way].lru.to_ulong()) == RIB_WAY - 1)
          break;

    if (way == RIB_WAY) {
      cerr << "[SHOTGUN RIB] " << __func__ << " no victim! set: " << set
           << endl;
      assert(0);
    }
#endif
  } else {
    cerr << btb_type << "is no BTB type. SHOTGUN::find_victim" << endl;
    assert(0);
  }

  return way;
}

void SHOTGUN::update_replacement_state(uint8_t btb_type, uint32_t set,
                                       uint32_t way) {
  int temp;

  if (btb_type == IS_UBTB) {
#if defined(SKEWED_SHOTGUN_BTB)
    ubtb[set][way].lru = 0;
#else
    // update lru replacement state
    for (uint32_t i = 0; i < UBTB_WAY; i++)
      if ((ubtb[set][i].lru.to_ulong()) < (ubtb[set][way].lru.to_ulong())) {
        temp = (int)(ubtb[set][i].lru.to_ulong());
        ++temp;
        ubtb[set][i].lru = bitset<LOG2_UBTB_WAY>(temp);
      }

    ubtb[set][way].lru =
        bitset<LOG2_UBTB_WAY>(0); // promote to the MRU position
#endif
  } else if (btb_type == IS_CBTB) {
#if defined(SKEWED_SHOTGUN_BTB)
    cbtb[set][way].lru = 0;
#else
    // update lru replacement state
    for (uint32_t i = 0; i < CBTB_WAY; i++)
      if ((cbtb[set][i].lru.to_ulong()) < (cbtb[set][way].lru.to_ulong())) {
        temp = (int)(cbtb[set][i].lru.to_ulong());
        ++temp;
        cbtb[set][i].lru = bitset<LOG2_CBTB_WAY>(temp);
      }

    cbtb[set][way].lru =
        bitset<LOG2_CBTB_WAY>(0); // promote to the MRU position

#endif
  } else if (btb_type == IS_RIB) {
#if defined(SKEWED_SHOTGUN_BTB)
    rib[set][way].lru = 0;
#else
    // update lru replacement state
    for (uint32_t i = 0; i < RIB_WAY; i++)
      if ((rib[set][i].lru.to_ulong()) < (rib[set][way].lru.to_ulong())) {
        temp = (int)(rib[set][i].lru.to_ulong());
        ++temp;
        rib[set][i].lru = bitset<LOG2_RIB_WAY>(temp);
      }

    rib[set][way].lru = bitset<LOG2_RIB_WAY>(0); // promote to the MRU position
#endif
  } else {
    cerr << btb_type << "is no BTB type. SHOTGUN::update_replacement_state"
         << endl;
    assert(0);
  }
}

#endif
