#include "champsim.h"
#include "instruction.h"
#include "set.h"

#define FTQ_SIZE 24
#define MAX_INSTR_PER_FTQ_ENTRY 8

class FETCH_TARGET_QUEUE {
public:
  uint32_t occupancy, head, tail;
  const string NAME;
  const uint32_t SIZE;

  vector<vector<ooo_model_instr>> entry;

  FETCH_TARGET_QUEUE(string v1, uint32_t v2) : NAME(v1), SIZE(v2) {
    head = 0;
    tail = 0;
    occupancy = 0;
    entry.resize(SIZE);
  }

  bool check_occupancy(uint64_t ip) {
    if (occupancy == SIZE) {
      assert(entry[tail].size() > 0);

      if ((entry[tail][0].ip >> 5) == (ip >> 5) &&
          entry[tail].size() < MAX_INSTR_PER_FTQ_ENTRY)
        return true;
      else
        return false;
    } else
      return true;
  }
};
