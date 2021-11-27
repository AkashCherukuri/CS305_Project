/*
MICRO BTB class methods declared here.
*/

#include "cache.h"
#include "champsim.h"
#include "instruction.h"
#include <bits/stdc++.h>
#include <iostream>
#include <iterator> // for iterators
#include <unordered_map>
#include <vector>
#if defined(MICRO_BTB)

using namespace std;

#define SKEWED

#define TWOVARIANTS
//#define FOURVARIANTS

//#define MBTB_PREDECODE

//#define MBTB_6K
//#define MBTB_8K

#define L1BTB_SET 64
#define L1BTB_WAY 2
#define LOG2_L1BTB_SET (int)(ceil(log2(L1BTB_SET)))
#define LOG2_L1BTB_WAY (int)(ceil(log2(L1BTB_WAY)))

#if defined(MBTB_6K)
#define L2BTB_SET 1536
#define L2BTB_WAY 4
#define LOG2_L2BTB_SET (int)(ceil(log2(L2BTB_SET)))
#define LOG2_L2BTB_WAY (int)(ceil(log2(L2BTB_WAY)))
#define L2BTB_ACCESS_LATENCY 3
#elif defined(MBTB_8K)
#define L2BTB_SET 2048
#define L2BTB_WAY 4
#define LOG2_L2BTB_SET (int)(ceil(log2(L2BTB_SET)))
#define LOG2_L2BTB_WAY (int)(ceil(log2(L2BTB_WAY)))
#define L2BTB_ACCESS_LATENCY 3
#else
#define L2BTB_SET 1024
#define L2BTB_WAY 4
#define LOG2_L2BTB_SET (int)(ceil(log2(L2BTB_SET)))
#define LOG2_L2BTB_WAY (int)(ceil(log2(L2BTB_WAY)))
#if defined(TWOVARIANTS)
#define L2BTB_ACCESS_LATENCY 2
#else
#define L2BTB_ACCESS_LATENCY 3
#endif
#endif

#define LOG2_INSTR_SIZE 2
//#define IP_SIZE 64
//#define TARGET (IP_SIZE - LOG2_INSTR_SIZE)

#define L2BTB_PARTIAL_TAG_BITS 30 // Last 2 bits are zero as 4 byte
                                  // instructions.
#define L2BTB_PARTIAL_TARGET_BITS 64

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

#if defined(MBTB_PREDECODE)

#define PREDECODE_BUFFER_LEN 50
#define PREDECODES_PER_CYCLE 1

class PREDEDECODE_ENTRY {
public:
  uint64_t branch_ip;
  uint32_t set;
};

class PREDECODE_BUFFER {
public:
  PREDEDECODE_ENTRY entry[PREDECODE_BUFFER_LEN];
  uint32_t head;
  uint32_t tail;
  uint32_t occupancy;
};

class Conds {
public:
  uint64_t ip, branch_target, branch_type;
};

#endif

class MBTB {
public:
  string NAME = "MBTB";
  uint32_t cpu;

#if defined(MBTB_PREDECODE)
  unordered_map<uint64_t, vector<Conds>> branches;
  PREDECODE_BUFFER predecode_buffer;
#endif

  std::string condfile;
  uint64_t to_predecode = 0, predecoding_something = 0,
           predecode_request_dropped = 0;

  uint64_t total_miss_latency;

  uint64_t l2btb_access_latency = L2BTB_ACCESS_LATENCY;

  CACHE L1BTB{"L1BTB", L1BTB_SET, L1BTB_WAY, L1BTB_SET *L1BTB_WAY, 0, 0, 0, 0};
  CACHE L2BTB{"L2BTB", L2BTB_SET, L2BTB_WAY, L2BTB_SET *L2BTB_WAY, 0, 0, 0, 0};

  RAS ras;

  MBTB() {
    L1BTB.cache_type = IS_BTB;
    L2BTB.cache_type = IS_BTB;
  }

  uint32_t find_victim(uint8_t btb_type, uint64_t addr, uint64_t variant);
  // btb_type: see IS_UBTB, IS_CBTB and IS_RIB
  void update_replacement_state(uint8_t btb_type, uint32_t set, uint32_t way);

  // send the full IP as address
  uint32_t get_set(uint64_t address, uint8_t btb_type);
  uint32_t get_way(uint64_t address, uint32_t set, uint8_t btb_type);
  uint64_t get_tag(uint64_t address, uint8_t btb_type);
  uint64_t get_target(uint64_t address, uint8_t btb_type);
  uint64_t get_target(uint64_t address, uint64_t set, uint64_t way);

  uint32_t get_variant(uint64_t offset);
  uint32_t find_location(uint64_t trigger, uint64_t offset,
                         uint64_t curr_variant);

  uint32_t fill_btb(uint64_t trigger, uint64_t target, uint8_t branch_type,
                    uint8_t btb_type);

#if defined(MBTB_PREDECODE)
  void operate_predecode();
  int predecode(uint64_t branch_ip, uint32_t set);
#endif
};

#endif
