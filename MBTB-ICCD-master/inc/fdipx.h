/*
FDIPX BTB class methods declared here.
*/

#include "cache.h"
#include "champsim.h"
#include "instruction.h"
#include <bits/stdc++.h>
#include <iostream>
#include <iterator> // for iterators
#include <unordered_map>
#include <vector>
#if defined(FDIPX_BTB)

//#define FDIPX_4K

using namespace std;

#define L1BTB_SET 64
#define L1BTB_WAY 2
#define LOG2_L1BTB_SET (int)(ceil(log2(L1BTB_SET)))
#define LOG2_L1BTB_WAY (int)(ceil(log2(L1BTB_WAY)))

#if defined(FDIPX_4K)

#define L2BTB1_SET 768
#define L2BTB1_WAY 4
#define L2BTB1_OFFSET_SIZE 8

#define L2BTB2_SET 768
#define L2BTB2_WAY 4
#define L2BTB2_OFFSET_SIZE 13

#define L2BTB3_SET 768
#define L2BTB3_WAY 4
#define L2BTB3_OFFSET_SIZE 23

#define L2BTB4_SET 112
#define L2BTB4_WAY 4
#define L2BTB4_OFFSET_SIZE 64

#else

#define L2BTB1_SET 1536
#define L2BTB1_WAY 4
#define L2BTB1_OFFSET_SIZE 8

#define L2BTB2_SET 1536
#define L2BTB2_WAY 4
#define L2BTB2_OFFSET_SIZE 13

#define L2BTB3_SET 1536
#define L2BTB3_WAY 4
#define L2BTB3_OFFSET_SIZE 23

#define L2BTB4_SET 224
#define L2BTB4_WAY 4
#define L2BTB4_OFFSET_SIZE 64

#endif

#define LOG2_INSTR_SIZE 2

#define L2BTB_ACCESS_LATENCY 3

#define IS_L1BTB 1
#define IS_L2BTB 2
#define IS_BTB_BOTH 3

#define MAX_RAS_DEPTH 32

class RAS {
public:
  uint64_t return_address[MAX_RAS_DEPTH];
  uint64_t target_address[MAX_RAS_DEPTH];
  uint64_t caller_trigger[MAX_RAS_DEPTH];
  int head;
  uint64_t max_depth_exceeded_cnt = 0;

  RAS() { head = -1; };

  void push(uint64_t ret_addr, uint64_t call, uint64_t target,
            uint64_t bb_size);
  void pop();
};

class FDIPX {
public:
  string NAME = "FDIPX";
  uint32_t cpu;

  uint64_t total_miss_latency;

  uint64_t l2btb_access_latency = L2BTB_ACCESS_LATENCY;

  CACHE L1BTB{"L1BTB", L1BTB_SET, L1BTB_WAY, L1BTB_SET *L1BTB_WAY, 0, 0, 0, 0};
  CACHE L2BTB1{"L2BTB1", L2BTB1_SET, L2BTB1_WAY, L2BTB1_SET *L2BTB1_WAY,
               0,        0,          0,          0};
  CACHE L2BTB2{"L2BTB2", L2BTB2_SET, L2BTB2_WAY, L2BTB2_SET *L2BTB2_WAY,
               0,        0,          0,          0};
  CACHE L2BTB3{"L2BTB3", L2BTB3_SET, L2BTB3_WAY, L2BTB3_SET *L2BTB3_WAY,
               0,        0,          0,          0};
  CACHE L2BTB4{"L2BTB4", L2BTB4_SET, L2BTB4_WAY, L2BTB4_SET *L2BTB4_WAY,
               0,        0,          0,          0};
  CACHE *L2BTB[4] = {&L2BTB1, &L2BTB2, &L2BTB3, &L2BTB4};

  RAS ras;

  FDIPX() {
    L1BTB.cache_type = IS_BTB;
    L2BTB1.cache_type = IS_BTB;
    L2BTB2.cache_type = IS_BTB;
    L2BTB3.cache_type = IS_BTB;
    L2BTB4.cache_type = IS_BTB;
  }

  uint32_t find_victim(CACHE &cache, uint64_t addr, uint8_t btb_type);
  // btb_type: see IS_UBTB, IS_CBTB and IS_RIB
  void update_replacement_state(CACHE &cache, uint32_t set, uint32_t way);

  // send the full IP as address
  uint32_t get_set(uint64_t address, uint8_t btb_type, uint32_t num_set);
  uint32_t get_way(CACHE &cache, uint64_t address, uint32_t set,
                   uint8_t btb_type);
  uint64_t get_tag(uint64_t address, uint8_t btb_type);
  uint32_t get_variant(uint64_t offset);

  uint32_t fill_btb(uint64_t trigger, uint64_t target, uint8_t branch_type,
                    uint8_t btb_type);
};

#endif
