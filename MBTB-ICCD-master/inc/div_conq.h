#include "ooo_cpu.h"
#include <bits/stdc++.h>
#include <iostream>
#include <vector>
// #include <iterator>

#if defined(DIV_CONQ)

#define SEQTABLE_SIZE (1 << 14)
#define DISTABLE_SIZE (1 << 12)

#define DIS_PART_TAG 4
#define RLUQ_LEN 8
#define TERMINATING_DEPTH 4 // terminate recursive prefetches at this depth
#define PENDING_BRANCHES_QUEUE_LEN 32
#define PREFETCH_QUEUE_LEN 32
#define ISSUED_PREFETCH_QUEUE_LEN 32

#define PREDECODE_BUFFER_LEN 24
#define PREDECODES_PER_CYCLE 6

class Conds {
public:
  uint64_t ip, branch_target, branch_type;
};

class DisTableEntry {
public:
  bool valid;
  uint32_t partial_tag; // only least sig DIS_PART_TAG bits of cacheline tag
  uint8_t offset;       // only 4 bits (since 16 ins per cacheline)
};

class RLU_ENTRY {
public:
  bool valid;
  uint8_t lru;
  uint64_t ip;
  uint8_t prefetched;
};

class PREFETCH_QUEUE_ENTRY {
public:
  uint64_t ip;
  uint8_t depth;
  bool prefetched; // only for issued_prefetches
};

class PREFETCH_QUEUE {
public:
  PREFETCH_QUEUE_ENTRY *entry;
  const uint32_t size;
  uint32_t head;
  uint32_t tail;
  uint32_t occupancy;

  PREFETCH_QUEUE(uint32_t length) : size(length) {
    entry = new PREFETCH_QUEUE_ENTRY[length];
    head = 0;
    tail = 0;
    occupancy = 0;
  }
};

class PREDEDECODE_ENTRY {
public:
  uint64_t branch_ip;
  uint32_t set;
  uint32_t depth;
};

class PREDECODE_BUFFER {
public:
  PREDEDECODE_ENTRY entry[PREDECODE_BUFFER_LEN];
  uint32_t head;
  uint32_t tail;
  uint32_t occupancy;
};

class DNC {
public:
  vector<Conds> branches[64]; // Changed from L1I_SET

  // PENDING_BRANCHES pending_branch_queue;

  RLU_ENTRY RLUQ[RLUQ_LEN];

  bool SeqTable[SEQTABLE_SIZE];
  DisTableEntry DisTable[DISTABLE_SIZE];

  PREFETCH_QUEUE SeqQ{PREFETCH_QUEUE_LEN};
  PREFETCH_QUEUE DisQ{PREFETCH_QUEUE_LEN};
  PREFETCH_QUEUE issued{ISSUED_PREFETCH_QUEUE_LEN};

  PREDECODE_BUFFER predecode_buffer;

  RAS ras;

  int predecode(uint64_t branch_ip, uint32_t set, uint32_t depth);

  // 0 return means the entry is already in RLUQ. 1 indicates otherwise.
  int add_rluq(uint64_t v_addr, uint8_t prefetch);
  void operate_predecode(uint32_t cpu);

  void add_disq(uint64_t prefetch_target, uint32_t depth);
  void add_seqq(uint64_t prefetch_target, uint32_t depth);

  uint32_t get_SeqTable_set(uint64_t ip);
  uint32_t get_DisTable_set(uint64_t ip);
  uint32_t get_DisTable_partial_tag(uint64_t ip);
};

// DNC L1IPrefetcher;
#endif
