/*
BASELINE BTB class methods declared here.
*/

#include "cache.h"
#include "champsim.h"
#include "instruction.h"
#include <bits/stdc++.h>
#include <iostream>
#include <iterator> // for iterators
#include <unordered_map>
#include <vector>

#if defined(BASELINEBTB) || defined(PERFECT_BTB) || defined(DIV_CONQ)

using namespace std;

#define L1BTB_SET 64
#define L1BTB_WAY 2
#define LOG2_L1BTB_SET (int)(ceil(log2(L1BTB_SET)))
#define LOG2_L1BTB_WAY (int)(ceil(log2(L1BTB_WAY)))

#define L2BTB_SET 2048
#define L2BTB_WAY 4
#define LOG2_L2BTB_SET (int)(ceil(log2(L2BTB_SET)))
#define LOG2_L2BTB_WAY (int)(ceil(log2(L2BTB_WAY)))

#define LOG2_INSTR_SIZE 2
//#define IP_SIZE 64
//#define TARGET (IP_SIZE - LOG2_INSTR_SIZE)

#define L2BTB_ACCESS_LATENCY 2
#define L2BTB_PARTIAL_TAG_BITS 30 // Last 2 bits are zero as 4 byte
                                  // instructions.
#define L2BTB_PARTIAL_TARGET_BITS 64 // 32

#define IS_L1BTB 1
#define IS_L2BTB 2
#define IS_BTB_BOTH 3

#define L2BTB_TAG ((IP_SIZE - LOG2_INSTR_SIZE) - LOG2_L2BTB_SET)
#define L1BTB_TAG ((IP_SIZE - LOG2_INSTR_SIZE) - LOG2_L1BTB_SET)

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

class BASELINE_BTB {
public:
  string NAME = "BASELINE_BTB";
  uint32_t cpu;

  uint64_t total_miss_latency;

  CACHE L1BTB{"L1BTB", L1BTB_SET, L1BTB_WAY, L1BTB_SET *L1BTB_WAY, 0, 0, 0, 0};
  CACHE L2BTB{"L2BTB", L2BTB_SET, L2BTB_WAY, L2BTB_SET *L2BTB_WAY, 0, 0, 0, 0};

  uint64_t l2btb_access_latency = L2BTB_ACCESS_LATENCY;

  RAS ras;

  BASELINE_BTB() {
    L1BTB.cache_type = IS_BTB;
    L2BTB.cache_type = IS_BTB;
  }

  uint32_t find_victim(uint8_t btb_type, uint64_t addr);
  // btb_type: see IS_UBTB, IS_CBTB and IS_RIB
  void update_replacement_state(uint8_t btb_type, uint32_t set, uint32_t way);

  // send the full IP as address
  uint32_t get_set(uint64_t address, uint8_t btb_type);
  uint32_t get_way(uint64_t address, uint32_t set, uint8_t btb_type);
  uint64_t get_tag(uint64_t address, uint8_t btb_type);
  uint64_t get_target(uint64_t address, uint8_t btb_type);
  uint32_t fill_btb(uint64_t trigger, uint64_t target, uint8_t branch_type,
                    uint8_t btb_type);
};

#endif
