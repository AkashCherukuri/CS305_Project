#include "ooo_cpu.h"
#include "set.h"
#include <iterator>
#include <map>
#include <set>
#include <unordered_map>

// out-of-order core
O3_CPU ooo_cpu[NUM_CPUS];
uint64_t current_core_cycle[NUM_CPUS], stall_cycle[NUM_CPUS];
uint32_t SCHEDULING_LATENCY = 0, EXEC_LATENCY = 0, DECODE_LATENCY = 0;
uint8_t TRACE_ENDS_STOP = 0;
uint8_t UNIQUE_ASID[5];
int asid_index = 0;

uint64_t total_stall_cycle_per_branch[NUM_TYPES] = {0};
uint64_t total_fetch_cycle_per_branch[NUM_TYPES] = {0};
uint64_t total_fetch_queue_cycle_per_branch[NUM_TYPES] = {0};
uint64_t total_decode_cycle_per_branch[NUM_TYPES] = {0};
uint64_t total_execute_cycle_per_branch[NUM_TYPES] = {0};
uint64_t total_stall_count[NUM_TYPES] = {0};

map<uint64_t, set<uint64_t>> unique_branch_target;

map<uint64_t, set<uint64_t>> unique_trigger_per_target;

map<uint64_t, set<uint64_t>> offset_bits_required;

map<uint64_t, map<uint64_t, set<uint64_t>>> offsets_per_branch_type;

unordered_map<uint64_t, unordered_set<uint64_t>> variant1, variant2, variant3;
#if defined(MICRO_BTB)
unordered_map<uint64_t, unordered_set<uint64_t>> variant1_per_set[L2BTB_SET],
    variant2_per_set[L2BTB_SET], variant3_per_set[L2BTB_SET];
#endif
map<uint64_t, uint64_t> dispatches_per_cycle;
uint64_t rob_full_cycles = 0;

uint64_t ftq_in = 0, decode_in = 0, rob_in = 0, rob_out = 0;
uint64_t ftq_total_occupancy = 0, decode_total_occupancy = 0,
         rob_total_occupancy = 0, denom_cycle = 0;

#if defined(PERFECT_BTB)
map<uint64_t, uint64_t> perfect_btb;
#endif

int reg_instruction_pointer = REG_INSTRUCTION_POINTER, reg_flags = REG_FLAGS,
    reg_stack_pointer = REG_STACK_POINTER;

unordered_map<uint64_t, uint64_t> branch_type_per_ip;
set<uint64_t> instruction_pages;
set<uint64_t> data_pages;

set<uint64_t> upper_32_bits, upper_36_bits, upper_40_bits, upper_44_bits,
    upper_48_bits;

unordered_map<uint64_t, pair<uint64_t, uint64_t>> reuse_distance_per_branch;
unordered_map<uint64_t, uint64_t> prev_access_per_branch;
uint64_t branch_counter = 0;

unordered_map<uint64_t, unordered_set<uint64_t>> branch_basic_block_size;
uint64_t prev_branch_ip = 0;

set<uint64_t> branch_instructions;

void O3_CPU::initialize_core() {}

void calculate_stall_cycle(ooo_model_instr &entry) {
  //	cout << entry.instr_id << " " << entry.stall_begin_cycle << " " <<
  //entry.fetch_start_cycle << " " << entry.fetch_end_cycle << " " <<
  //entry.decode_start_cycle << " " << entry.decode_end_cycle << " " <<
  //entry.execute_start_cycle << " " << entry.execute_end_cycle << endl;

  // Works for single core
  total_stall_cycle_per_branch[entry.branch_type] +=
      current_core_cycle[0] - entry.stall_begin_cycle;
  total_fetch_cycle_per_branch[entry.branch_type] +=
      entry.fetch_end_cycle - entry.fetch_start_cycle;
  total_fetch_queue_cycle_per_branch[entry.branch_type] +=
      entry.decode_start_cycle - entry.fetch_end_cycle;
  total_decode_cycle_per_branch[entry.branch_type] +=
      entry.decode_end_cycle - entry.decode_start_cycle;
  total_execute_cycle_per_branch[entry.branch_type] +=
      entry.execute_end_cycle - entry.decode_end_cycle;
  total_stall_count[entry.branch_type]++;
}

uint64_t find_abs(uint64_t a, uint64_t b) {
  if (a > b)
    return a - b;
  else
    return b - a;
}

void O3_CPU::read_from_trace() {
  // actual processors do not work like this but for easier implementation,
  // we read instruction traces and virtually add them in the ROB
  // note that these traces are not yet translated and fetched

  uint8_t continue_reading = 1;
  uint32_t num_reads = 0;
  instrs_to_read_this_cycle = FETCH_WIDTH;

  // first, read PIN trace
  while (continue_reading) {

    size_t instr_size =
        knob_cloudsuite ? sizeof(cloudsuite_instr) : sizeof(input_instr);

    if (knob_cloudsuite) {

      if (!fread(&current_cloudsuite_instr, instr_size, 1, trace_file)) {
        // reached end of file for this trace
        trace_file = popen(gunzip_command, "r");
        if (trace_file == NULL) {
          cerr << endl
               << "*** CANNOT REOPEN TRACE FILE: " << trace_string << " ***"
               << endl;
          assert(0);
        }
      } else { // successfully read the trace

        // copy the instruction into the performance model's instruction format
        ooo_model_instr arch_instr;
        int num_reg_ops = 0, num_mem_ops = 0;

        arch_instr.instr_id = instr_unique_id;
        arch_instr.ip = current_cloudsuite_instr.ip;
        arch_instr.is_branch = current_cloudsuite_instr.is_branch;
        arch_instr.branch_taken = current_cloudsuite_instr.branch_taken;

        arch_instr.asid[0] = current_cloudsuite_instr.asid[0];
        arch_instr.asid[1] = current_cloudsuite_instr.asid[1];

        /*@Vasudha - find unique asid*/
        int flag = 0;
        for (int i = 0; i < asid_index; i++) {
          if (UNIQUE_ASID[i] == arch_instr.asid[0]) {
            flag = 1;
            break;
          }
        }
        if (flag == 0) {
          UNIQUE_ASID[asid_index] = arch_instr.asid[0];
          cout << "UNIQUE_ASID[" << asid_index
               << "] = " << UNIQUE_ASID[asid_index] << endl;
          asid_index++;
        }
        for (uint32_t i = 0; i < MAX_INSTR_DESTINATIONS; i++) {
          arch_instr.destination_registers[i] =
              current_cloudsuite_instr.destination_registers[i];
          arch_instr.destination_memory[i] =
              current_cloudsuite_instr.destination_memory[i];
          arch_instr.destination_virtual_address[i] =
              current_cloudsuite_instr.destination_memory[i];

          if (arch_instr.destination_registers[i])
            num_reg_ops++;
          if (arch_instr.destination_memory[i]) {
            num_mem_ops++;

            // update STA, this structure is required to execute store
            // instructios properly without deadlock
            if (num_mem_ops > 0) {
#ifdef SANITY_CHECK
              if (STA[STA_tail] < UINT64_MAX) {
                if (STA_head != STA_tail)
                  assert(0);
              }
#endif
              STA[STA_tail] = instr_unique_id;
              STA_tail++;

              if (STA_tail == STA_SIZE)
                STA_tail = 0;
            }
          }
        }

        for (int i = 0; i < NUM_INSTR_SOURCES; i++) {
          arch_instr.source_registers[i] =
              current_cloudsuite_instr.source_registers[i];
          arch_instr.source_memory[i] =
              current_cloudsuite_instr.source_memory[i];
          arch_instr.source_virtual_address[i] =
              current_cloudsuite_instr.source_memory[i];

          if (arch_instr.source_registers[i])
            num_reg_ops++;
          if (arch_instr.source_memory[i])
            num_mem_ops++;
        }

        arch_instr.num_reg_ops = num_reg_ops;
        arch_instr.num_mem_ops = num_mem_ops;
        if (num_mem_ops > 0)
          arch_instr.is_memory = 1;

        // Neelu: Addition begin

        // add this instruction to the FTQ
        if (FTQ.check_occupancy(arch_instr.ip)) {
          ooo_model_instr *curr_ftq_instr = add_to_ftq(&arch_instr);

          num_reads++;
          // handle branch prediction

          if (curr_ftq_instr->is_branch) {
            DP(if (warmup_complete[cpu]) {
              cout << "[BRANCH] instr_id: " << instr_unique_id << " ip: " << hex
                   << arch_instr.ip << dec
                   << " taken: " << +arch_instr.branch_taken << endl;
            });

            num_branch++;
            // handle branch prediction & branch predictor update

            uint8_t branch_prediction = predict_branch(curr_ftq_instr->ip);
            if (curr_ftq_instr->branch_taken != branch_prediction) {
              branch_mispredictions++;
              total_rob_occupancy_at_branch_mispredict += ROB.occupancy;
              if (warmup_complete[cpu]) {
                fetch_stall = 1;
                instrs_to_read_this_cycle = 0;
                curr_ftq_instr->branch_mispredicted = 1;
              }
            } else {
              // correct prediction
              if (branch_prediction == 1) {
                // if correctly predicted taken, then we can't fetch anymore
                // instructions this cycle
                instrs_to_read_this_cycle = 0;
              }
            }

            last_branch_result(curr_ftq_instr->ip,
                               curr_ftq_instr->branch_taken);
          }

          if ((num_reads >= instrs_to_read_this_cycle) ||
              !(FTQ.check_occupancy(next_instr.ip)))
            continue_reading = 0;
        } else {
          assert(0);
        }
        instr_unique_id++;
      }
    } else {
      input_instr trace_read_instr;
      if (!fread(&trace_read_instr, instr_size, 1, trace_file)) {
        // reached end of file for this trace
        cout << "*** Reached end of trace for Core: " << cpu
             << " Repeating trace: " << trace_string << endl;

        // close the trace file and re-open it
        pclose(trace_file);
        trace_file = popen(gunzip_command, "r");
        if (trace_file == NULL) {
          cerr << endl
               << "*** CANNOT REOPEN TRACE FILE: " << trace_string << " ***"
               << endl;
          assert(0);
        }
      } else { // successfully read the trace

        if (instr_unique_id == 0) {
          current_instr = next_instr = trace_read_instr;
        } else {
          current_instr = next_instr;
          next_instr = trace_read_instr;
        }

        // copy the instruction into the performance model's instruction format
        ooo_model_instr arch_instr;
        int num_reg_ops = 0, num_mem_ops = 0;

        arch_instr.instr_id = instr_unique_id;
        arch_instr.ip = current_instr.ip;
        arch_instr.is_branch = current_instr.is_branch;
        arch_instr.branch_taken = current_instr.branch_taken;

        arch_instr.asid[0] = cpu;
        arch_instr.asid[1] = cpu;

        bool reads_sp = false;
        bool writes_sp = false;
        bool reads_flags = false;
        bool reads_ip = false;
        bool writes_ip = false;
        bool reads_other = false;

        for (uint32_t i = 0; i < MAX_INSTR_DESTINATIONS; i++) {
          arch_instr.destination_registers[i] =
              current_instr.destination_registers[i];
          arch_instr.destination_memory[i] =
              current_instr.destination_memory[i];
          arch_instr.destination_virtual_address[i] =
              current_instr.destination_memory[i];

          if (arch_instr.destination_memory[i] != 0)
            data_pages.insert(arch_instr.destination_memory[i] >> 12);

          if (arch_instr.destination_registers[i] == reg_stack_pointer)
            writes_sp = true;
          else if (arch_instr.destination_registers[i] ==
                   reg_instruction_pointer)
            writes_ip = true;

          if (arch_instr.destination_registers[i])
            num_reg_ops++;
          if (arch_instr.destination_memory[i]) {
            num_mem_ops++;
            // update STA, this structure is required to execute store
            // instructios properly without deadlock
            if (num_mem_ops > 0) {
#ifdef SANITY_CHECK
              if (STA[STA_tail] < UINT64_MAX) {
                if (STA_head != STA_tail)
                  assert(0);
              }
#endif
              STA[STA_tail] = instr_unique_id;
              STA_tail++;

              if (STA_tail == STA_SIZE)
                STA_tail = 0;
            }
          }
        }

        for (int i = 0; i < NUM_INSTR_SOURCES; i++) {
          arch_instr.source_registers[i] = current_instr.source_registers[i];
          arch_instr.source_memory[i] = current_instr.source_memory[i];
          arch_instr.source_virtual_address[i] = current_instr.source_memory[i];

          if (arch_instr.source_memory[i] != 0)
            data_pages.insert(arch_instr.source_memory[i] >> 12);

          if (arch_instr.source_registers[i] == reg_stack_pointer)
            reads_sp = true;
          else if (arch_instr.source_registers[i] == reg_flags)
            reads_flags = true;
          else if (arch_instr.source_registers[i] == reg_instruction_pointer)
            reads_ip = true;
          else if (arch_instr.source_registers[i] != 0)
            reads_other = true;

          if (arch_instr.source_registers[i])
            num_reg_ops++;
          if (arch_instr.source_memory[i])
            num_mem_ops++;
        }

        arch_instr.num_reg_ops = num_reg_ops;
        arch_instr.num_mem_ops = num_mem_ops;
        if (num_mem_ops > 0)
          arch_instr.is_memory = 1;

        // determine what kind of branch this is, if any
        if (!reads_sp && !reads_flags && writes_ip && !reads_other) {
          // direct jump
          arch_instr.is_branch = 1;
          arch_instr.branch_taken = 1;
          arch_instr.branch_type = BRANCH_DIRECT_JUMP;
        } else if (!reads_sp && !reads_flags && writes_ip && reads_other) {
          // indirect branch
          arch_instr.is_branch = 1;
          arch_instr.branch_taken = 1;
          arch_instr.branch_type = BRANCH_INDIRECT;
        } else if (!reads_sp && reads_ip && !writes_sp && writes_ip &&
                   reads_flags && !reads_other) {
          // conditional branch
          arch_instr.is_branch = 1;
          arch_instr.branch_taken =
              arch_instr.branch_taken; // don't change this
          arch_instr.branch_type = BRANCH_CONDITIONAL;
        } else if (reads_sp && reads_ip && writes_sp && writes_ip &&
                   !reads_flags && !reads_other) {
          // direct call
          arch_instr.is_branch = 1;
          arch_instr.branch_taken = 1;
          arch_instr.branch_type = BRANCH_DIRECT_CALL;

        } else if (reads_sp && reads_ip && writes_sp && writes_ip &&
                   !reads_flags && reads_other) {
          // indirect call
          arch_instr.is_branch = 1;
          arch_instr.branch_taken = 1;
          arch_instr.branch_type = BRANCH_INDIRECT_CALL;
        } else if (reads_sp && !reads_ip && writes_sp && writes_ip) {
          // return
          arch_instr.is_branch = 1;
          arch_instr.branch_taken = 1;
          arch_instr.branch_type = BRANCH_RETURN;
        } else if (writes_ip) {
          // some other branch type that doesn't fit the above categories
          arch_instr.is_branch = 1;
          arch_instr.branch_taken =
              arch_instr.branch_taken; // don't change this
          arch_instr.branch_type = BRANCH_OTHER;
        }

        total_branch_types[arch_instr.branch_type]++;

        if ((arch_instr.is_branch == 1)) //&& (arch_instr.branch_taken == 1))
        {
          arch_instr.branch_target = next_instr.ip;
        }

        if (arch_instr.is_branch) {
          uint64_t offset = find_abs(next_instr.ip, arch_instr.ip);
          int bits_required = 0;
          while (offset > 0) {
            bits_required++;
            offset >>= 1;
          }
          assert(bits_required <= 64);
          offset_bits_required[bits_required].insert(arch_instr.ip);

          offsets_per_branch_type[arch_instr.branch_type][bits_required].insert(
              arch_instr.ip);

#if defined(BASELINEBTB)
          unique_trigger_per_target[next_instr.ip].insert(
              BTB.get_tag(arch_instr.ip, IS_L2BTB));
#endif
        }

        upper_32_bits.insert(arch_instr.ip >> 32);
        upper_36_bits.insert(arch_instr.ip >> 28);
        upper_40_bits.insert(arch_instr.ip >> 24);
        upper_44_bits.insert(arch_instr.ip >> 20);
        upper_48_bits.insert(arch_instr.ip >> 16);

        if (arch_instr.is_branch) {
          branch_type_per_ip[arch_instr.ip] = arch_instr.branch_type;
          branch_counter++;
          if (prev_access_per_branch.find(arch_instr.ip) !=
              prev_access_per_branch.end()) {
            reuse_distance_per_branch[arch_instr.ip].first =
                branch_counter - prev_access_per_branch[arch_instr.ip];
            reuse_distance_per_branch[arch_instr.ip].second++;
          }
          prev_access_per_branch[arch_instr.ip] = branch_counter;
        }

        instruction_pages.insert(arch_instr.ip >> 12);

        // Neelu: Inserting instruction to FTQ

        // add this instruction to the FTQ
        if (FTQ.check_occupancy(arch_instr.ip)) {
          ooo_model_instr *curr_ftq_instr = add_to_ftq(&arch_instr);

          curr_ftq_instr->cycle_enqueued = current_core_cycle[cpu];
          num_reads++;

          // handle branch prediction

          if (curr_ftq_instr->is_branch) {

            DP(if (warmup_complete[cpu]) {
              cout << "[BRANCH] instr_id: " << instr_unique_id << " ip: " << hex
                   << arch_instr.ip << dec
                   << " taken: " << +arch_instr.branch_taken << endl;
            });
            num_branch++;
            // handle branch prediction & branch predictor update
            uint8_t branch_prediction = predict_branch(curr_ftq_instr->ip);
            uint64_t predicted_branch_target = curr_ftq_instr->branch_target;
            if (branch_prediction == 0) {
              predicted_branch_target = 0;
            }

            branch_basic_block_size[prev_branch_ip].insert(curr_ftq_instr->ip -
                                                           prev_branch_ip);
            prev_branch_ip = curr_ftq_instr->branch_target;

            if (arch_instr.branch_type == BRANCH_INDIRECT ||
                arch_instr.branch_type == BRANCH_INDIRECT_CALL)
              unique_branch_target[arch_instr.ip].insert(
                  arch_instr.branch_target);

#if defined(PERFECT_BTB)
            if (arch_instr.branch_taken) {
              if (perfect_btb.find(arch_instr.ip) == perfect_btb.end()) {
                perfect_btb[arch_instr.ip] = arch_instr.branch_target;
                BTB.L1BTB.sim_hit[cpu][arch_instr.branch_type - 1]++;
                BTB.L1BTB.sim_access[cpu][arch_instr.branch_type - 1]++;
              } else if (perfect_btb[arch_instr.ip] !=
                             arch_instr.branch_target &&
                         (arch_instr.branch_type == BRANCH_INDIRECT ||
                          arch_instr.branch_type == BRANCH_INDIRECT_CALL)) {
                BTB.L1BTB.sim_miss[cpu][arch_instr.branch_type - 1]++;
                BTB.L1BTB.sim_access[cpu][arch_instr.branch_type - 1]++;

                if (warmup_complete[cpu]) {
                  fetch_stall = 1;
                  curr_ftq_instr->stall_begin_cycle = current_core_cycle[cpu];
                  instrs_to_read_this_cycle = 0;
                  curr_ftq_instr->btb_miss = 1;
                  total_rob_occupancy_at_btb_mispredict += ROB.occupancy;
                }
                perfect_btb[arch_instr.ip] = arch_instr.branch_target;
              } else {
                perfect_btb[arch_instr.ip] = arch_instr.branch_target;
                BTB.L1BTB.sim_hit[cpu][arch_instr.branch_type - 1]++;
                BTB.L1BTB.sim_access[cpu][arch_instr.branch_type - 1]++;
              }
            }

#endif

#if defined(MICRO_BTB)
            if (arch_instr.branch_taken) {
              uint64_t variant1_tag = arch_instr.ip & ((1L << 28) - 1);
              uint64_t addr = arch_instr.ip;
              uint64_t lower_14 = addr & ((1L << 14) - 1);
              addr >>= 14;
              uint64_t variant2_tag = (addr & ((1L << 14) - 1)) ^ lower_14;

              uint64_t variant3_tag = 0;
              addr = arch_instr.ip;
              for (int i = 0; i < 4; i++) {
                variant3_tag ^= addr & ((1L << 7) - 1);
                addr >>= 7;
              }
              variant1[variant1_tag].insert(arch_instr.ip);
              variant2[variant2_tag].insert(arch_instr.ip);
              variant3[variant3_tag].insert(arch_instr.ip);
              uint64_t set_number = BTB.get_set(arch_instr.ip, IS_L2BTB);
              variant1_per_set[set_number][variant1_tag].insert(arch_instr.ip);
              variant2_per_set[set_number][variant2_tag].insert(arch_instr.ip);
              variant3_per_set[set_number][variant3_tag].insert(arch_instr.ip);
            }
#endif

#if (defined(SKEWED_BTB) || defined(BASELINEBTB) ||                 \
     defined(DIV_CONQ) || defined(MICRO_BTB)) &&                               \
    !defined(PERFECT_BTB)
            if (arch_instr.branch_taken) {
              uint32_t btb_set = BTB.get_set(arch_instr.ip, IS_L1BTB);
              int btb_way = BTB.get_way(arch_instr.ip, btb_set, IS_L1BTB);

              if (btb_way == L1BTB_WAY) {
                BTB.L1BTB.sim_miss[cpu][arch_instr.branch_type - 1]++;
                BTB.L1BTB.sim_access[cpu][arch_instr.branch_type - 1]++;

                btb_set = BTB.get_set(arch_instr.ip, IS_L2BTB);
                btb_way = BTB.get_way(arch_instr.ip, btb_set, IS_L2BTB);

#if defined(DIV_CONQ)
                if (btb_way == L2BTB_WAY)
                  btb_way = btb_prefetch_buffer.check_hit(
                      cpu, arch_instr.ip, arch_instr.branch_type);
#endif

                if (btb_way == L2BTB_WAY) {
                  BTB.L2BTB.sim_miss[cpu][arch_instr.branch_type - 1]++;
                  BTB.L2BTB.sim_access[cpu][arch_instr.branch_type - 1]++;

                  if (warmup_complete[cpu]) {
                    fetch_stall = 1;
                    curr_ftq_instr->stall_begin_cycle = current_core_cycle[cpu];
                    instrs_to_read_this_cycle = 0;
                    if (arch_instr.branch_type == BRANCH_RETURN &&
                        BTB.ras.return_address[BTB.ras.head] ==
                            curr_ftq_instr->branch_target)
                      curr_ftq_instr->btb_miss = 2;
                    else
                      curr_ftq_instr->btb_miss = 1;
                    total_rob_occupancy_at_btb_mispredict += ROB.occupancy;
                  } else {
                    BTB.fill_btb(arch_instr.ip, arch_instr.branch_target,
                                 arch_instr.branch_type, IS_BTB_BOTH);
                  }
                } else {
                  // Partial target
                  uint64_t target = 0;
#if defined(MICRO_BTB)
                  target = BTB.get_target(arch_instr.ip, btb_set, btb_way);
#else
                  target = (BTB.L2BTB.block[btb_set][btb_way].data) |
                           ((arch_instr.ip >> L2BTB_PARTIAL_TARGET_BITS)
                            << L2BTB_PARTIAL_TARGET_BITS);
#endif

                  if (arch_instr.branch_type == BRANCH_RETURN)
                    target = BTB.ras.return_address[BTB.ras.head];

                  if (target == curr_ftq_instr->branch_target) {

                    if (warmup_complete[cpu]) {
                      fetch_stall = 1;
                      instrs_to_read_this_cycle = 0;
                      fetch_resume_cycle = current_core_cycle[cpu] +
                                           BTB.l2btb_access_latency + 1;
                    }

                    BTB.L2BTB.sim_hit[cpu][arch_instr.branch_type - 1]++;
                    BTB.L2BTB.sim_access[cpu][arch_instr.branch_type - 1]++;
                    BTB.update_replacement_state(IS_L2BTB, btb_set, btb_way);
                    BTB.fill_btb(arch_instr.ip, arch_instr.branch_target,
                                 arch_instr.branch_type, IS_L1BTB);
                  } else {
                    BTB.L2BTB.sim_miss[cpu][arch_instr.branch_type - 1]++;
                    BTB.L2BTB.sim_access[cpu][arch_instr.branch_type - 1]++;
                    if (warmup_complete[cpu]) {
                      fetch_stall = 1;
                      curr_ftq_instr->stall_begin_cycle =
                          current_core_cycle[cpu];
                      instrs_to_read_this_cycle = 0;
                      curr_ftq_instr->btb_miss = 1;
                      total_rob_occupancy_at_btb_mispredict += ROB.occupancy;
                    } else {
                      BTB.fill_btb(arch_instr.ip, arch_instr.branch_target,
                                   arch_instr.branch_type, IS_BTB_BOTH);
                    }
                  }
                }

              } else {
                uint64_t target = BTB.L1BTB.block[btb_set][btb_way].data;
                if (arch_instr.branch_type == BRANCH_RETURN)
                  target = BTB.ras.return_address[BTB.ras.head];

                if (target == curr_ftq_instr->branch_target) {
                  BTB.L1BTB.sim_hit[cpu][arch_instr.branch_type - 1]++;
                  BTB.L1BTB.sim_access[cpu][arch_instr.branch_type - 1]++;
                  BTB.update_replacement_state(IS_L1BTB, btb_set, btb_way);
                } else {
                  BTB.L1BTB.sim_miss[cpu][arch_instr.branch_type - 1]++;
                  BTB.L1BTB.sim_access[cpu][arch_instr.branch_type - 1]++;
                  if (warmup_complete[cpu]) {
                    fetch_stall = 1;
                    curr_ftq_instr->stall_begin_cycle = current_core_cycle[cpu];
                    instrs_to_read_this_cycle = 0;
                    if (arch_instr.branch_type == BRANCH_RETURN &&
                        BTB.ras.return_address[BTB.ras.head] ==
                            curr_ftq_instr->branch_target)
                      curr_ftq_instr->btb_miss = 2;
                    else
                      curr_ftq_instr->btb_miss = 1;
                    total_rob_occupancy_at_btb_mispredict += ROB.occupancy;
                  } else {
                    BTB.fill_btb(arch_instr.ip, arch_instr.branch_target,
                                 arch_instr.branch_type, IS_BTB_BOTH);
                  }
                }
              }
            } else {
              uint32_t btb_set = BTB.get_set(arch_instr.ip, IS_L1BTB);
              int btb_way = BTB.get_way(arch_instr.ip, btb_set, IS_L1BTB);

              if (btb_way != L1BTB_WAY) {
                BTB.L1BTB.sim_hit[cpu][arch_instr.branch_type - 1]++;
                BTB.L1BTB.sim_access[cpu][arch_instr.branch_type - 1]++;
                BTB.update_replacement_state(IS_L1BTB, btb_set, btb_way);
              }
            }

            if (arch_instr.branch_type == BRANCH_DIRECT_CALL ||
                arch_instr.branch_type == BRANCH_INDIRECT_CALL) {
              BTB.ras.push(arch_instr.ip + 4, arch_instr.ip,
                           curr_ftq_instr->branch_target, 0);
            }

            if (arch_instr.branch_type == BRANCH_RETURN)
              BTB.ras.pop();
#endif

#if (defined(FDIPX_BTB)) && !defined(PERFECT_BTB)
            if (arch_instr.branch_taken) {
              uint32_t btb_set =
                  BTB.get_set(arch_instr.ip, IS_L1BTB, BTB.L1BTB.NUM_SET);
              int btb_way =
                  BTB.get_way(BTB.L1BTB, arch_instr.ip, btb_set, IS_L1BTB);

              if (btb_way == L1BTB_WAY) {
                BTB.L1BTB.sim_miss[cpu][arch_instr.branch_type - 1]++;
                BTB.L1BTB.sim_access[cpu][arch_instr.branch_type - 1]++;

                int btb_type = -1;

                for (int i = 0; i < 4; i++) {
                  btb_set = BTB.get_set(arch_instr.ip, IS_L2BTB,
                                        BTB.L2BTB[i]->NUM_SET);
                  btb_way = BTB.get_way(*(BTB.L2BTB[i]), arch_instr.ip, btb_set,
                                        IS_L2BTB);
                  if (btb_way < BTB.L2BTB[i]->NUM_WAY) {
                    btb_type = i;
                    break;
                  }
                }

                if (btb_type == -1) {
                  BTB.L2BTB[0]->sim_miss[cpu][arch_instr.branch_type - 1]++;
                  BTB.L2BTB[0]->sim_access[cpu][arch_instr.branch_type - 1]++;

                  if (warmup_complete[cpu]) {
                    fetch_stall = 1;
                    curr_ftq_instr->stall_begin_cycle = current_core_cycle[cpu];
                    instrs_to_read_this_cycle = 0;
                    if (arch_instr.branch_type == BRANCH_RETURN &&
                        BTB.ras.return_address[BTB.ras.head] ==
                            curr_ftq_instr->branch_target)
                      curr_ftq_instr->btb_miss = 2;
                    else
                      curr_ftq_instr->btb_miss = 1;
                    total_rob_occupancy_at_btb_mispredict += ROB.occupancy;
                  } else {
                    BTB.fill_btb(arch_instr.ip, arch_instr.branch_target,
                                 arch_instr.branch_type, IS_BTB_BOTH);
                  }
                } else {
                  uint64_t target =
                      BTB.L2BTB[btb_type]->block[btb_set][btb_way].data +
                      arch_instr.ip;

                  if (arch_instr.branch_type == BRANCH_RETURN)
                    target = BTB.ras.return_address[BTB.ras.head];

                  if (target == curr_ftq_instr->branch_target) {
                    if (warmup_complete[cpu]) {
                      fetch_stall = 1;
                      instrs_to_read_this_cycle = 0;
                      fetch_resume_cycle = current_core_cycle[cpu] +
                                           BTB.l2btb_access_latency + 1;
                    }

                    BTB.L2BTB[0]->sim_hit[cpu][arch_instr.branch_type - 1]++;
                    BTB.L2BTB[0]->sim_access[cpu][arch_instr.branch_type - 1]++;
                    BTB.update_replacement_state(*BTB.L2BTB[btb_type], btb_set,
                                                 btb_way);
                    BTB.fill_btb(arch_instr.ip, arch_instr.branch_target,
                                 arch_instr.branch_type, IS_L1BTB);
                  } else {

                    BTB.L2BTB[0]->sim_miss[cpu][arch_instr.branch_type - 1]++;
                    BTB.L2BTB[0]->sim_access[cpu][arch_instr.branch_type - 1]++;
                    if (warmup_complete[cpu]) {
                      fetch_stall = 1;
                      curr_ftq_instr->stall_begin_cycle =
                          current_core_cycle[cpu];
                      instrs_to_read_this_cycle = 0;
                      curr_ftq_instr->btb_miss = 1;
                      total_rob_occupancy_at_btb_mispredict += ROB.occupancy;
                    } else {
                      BTB.fill_btb(arch_instr.ip, arch_instr.branch_target,
                                   arch_instr.branch_type, IS_BTB_BOTH);
                    }
                  }
                }

              } else {
                uint64_t target = BTB.L1BTB.block[btb_set][btb_way].data;
                if (arch_instr.branch_type == BRANCH_RETURN)
                  target = BTB.ras.return_address[BTB.ras.head];

                if (target == curr_ftq_instr->branch_target) {
                  BTB.L1BTB.sim_hit[cpu][arch_instr.branch_type - 1]++;
                  BTB.L1BTB.sim_access[cpu][arch_instr.branch_type - 1]++;
                  BTB.update_replacement_state(BTB.L1BTB, btb_set, btb_way);
                } else {
                  BTB.L1BTB.sim_miss[cpu][arch_instr.branch_type - 1]++;
                  BTB.L1BTB.sim_access[cpu][arch_instr.branch_type - 1]++;
                  if (warmup_complete[cpu]) {
                    fetch_stall = 1;
                    curr_ftq_instr->stall_begin_cycle = current_core_cycle[cpu];
                    instrs_to_read_this_cycle = 0;
                    if (arch_instr.branch_type == BRANCH_RETURN &&
                        BTB.ras.return_address[BTB.ras.head] ==
                            curr_ftq_instr->branch_target)
                      curr_ftq_instr->btb_miss = 2;
                    else
                      curr_ftq_instr->btb_miss = 1;
                    total_rob_occupancy_at_btb_mispredict += ROB.occupancy;
                  } else {
                    BTB.fill_btb(arch_instr.ip, arch_instr.branch_target,
                                 arch_instr.branch_type, IS_BTB_BOTH);
                  }
                }
              }
            } else {
              uint32_t btb_set =
                  BTB.get_set(arch_instr.ip, IS_L1BTB, BTB.L1BTB.NUM_SET);
              int btb_way =
                  BTB.get_way(BTB.L1BTB, arch_instr.ip, btb_set, IS_L1BTB);

              if (btb_way != L1BTB_WAY) {
                BTB.L1BTB.sim_hit[cpu][arch_instr.branch_type - 1]++;
                BTB.L1BTB.sim_access[cpu][arch_instr.branch_type - 1]++;
                BTB.update_replacement_state(BTB.L1BTB, btb_set, btb_way);
              }
            }

            if (arch_instr.branch_type == BRANCH_DIRECT_CALL ||
                arch_instr.branch_type == BRANCH_INDIRECT_CALL) {
              BTB.ras.push(arch_instr.ip + 4, arch_instr.ip,
                           curr_ftq_instr->branch_target, 0);
            }

            if (arch_instr.branch_type == BRANCH_RETURN)
              BTB.ras.pop();
#endif

#if defined(SHOTGUN_BTB) && !defined(PERFECT_BTB)

            uint8_t btb_type;
            int max_ways = -1;

            if (curr_ftq_instr->branch_type == BRANCH_DIRECT_JUMP ||
                curr_ftq_instr->branch_type == BRANCH_INDIRECT ||
                curr_ftq_instr->branch_type == BRANCH_DIRECT_CALL ||
                curr_ftq_instr->branch_type == BRANCH_INDIRECT_CALL) {
              btb_type = IS_UBTB;
              max_ways = UBTB_WAY;
            }

            else if (curr_ftq_instr->branch_type == BRANCH_CONDITIONAL) {
              btb_type = IS_CBTB;
              max_ways = CBTB_WAY;
            }

            else if (curr_ftq_instr->branch_type == BRANCH_RETURN) {
              btb_type = IS_RIB;
              max_ways = RIB_WAY;
            }

            if (arch_instr.branch_taken) {
              // ++BTB.cycles;
              int a = 0;
              uint32_t btb_set = BTB.get_set(arch_instr.ip, btb_type);
              int btb_way = BTB.get_way(arch_instr.ip, btb_set, btb_type);
              if (btb_way == max_ways) {
                BTB.sim_miss[cpu][arch_instr.branch_type - 1]++;
                BTB.sim_access[cpu][arch_instr.branch_type - 1]++;

                // missed in all BTBs so predecode the block
                curr_ftq_instr->predecode = 1;
                if (warmup_complete[cpu]) {
                  fetch_stall = 1;
                  curr_ftq_instr->stall_begin_cycle = current_core_cycle[cpu];
                  instrs_to_read_this_cycle = 0;
                  if (arch_instr.branch_type == BRANCH_RETURN &&
                      BTB.ras.return_address[BTB.ras.head] ==
                          curr_ftq_instr->branch_target)
                    curr_ftq_instr->btb_miss = 2;
                  else
                    curr_ftq_instr->btb_miss = 1;
                } else {
                  fill_btb(arch_instr.ip, arch_instr.branch_target,
                           arch_instr.branch_type);
                }
              } else {
                // found the requested entry.
                // if target matches, record hit, else record miss

                uint64_t target;
                if (btb_type == IS_UBTB)
                  target = (BTB.ubtb[btb_set][btb_way].target.to_ulong()
                            << LOG2_INSTR_SIZE);
                else if (btb_type == IS_CBTB)
                  target = (BTB.cbtb[btb_set][btb_way].target.to_ulong()
                            << LOG2_INSTR_SIZE);
                else if (btb_type == IS_RIB)
                  target = BTB.ras.return_address[BTB.ras.head];

                if (target == curr_ftq_instr->branch_target) {
                  if (warmup_complete[cpu]) {
                    fetch_stall = 1;
                    instrs_to_read_this_cycle = 0;
                    fetch_resume_cycle = current_core_cycle[cpu] +
                                         BTB.access_latency +
                                         1; // Shotgun Access latency
                  }

                  BTB.sim_hit[cpu][arch_instr.branch_type - 1]++;
                  BTB.sim_access[cpu][arch_instr.branch_type - 1]++;
                  BTB.update_replacement_state(btb_type, btb_set, btb_way);
                } else {

                  BTB.sim_miss_target[cpu][arch_instr.branch_type - 1]++;
                  BTB.sim_access[cpu][arch_instr.branch_type - 1]++;
                  // missed in all BTBs so predecode the block
                  // IFETCH_BUFFER.entry[ifetch_buffer_index].predecode = 1;
                  if (warmup_complete[cpu]) {

                    fetch_stall = 1;
                    curr_ftq_instr->stall_begin_cycle = current_core_cycle[cpu];
                    instrs_to_read_this_cycle = 0;
                    curr_ftq_instr->btb_miss = 1;
                  } else {
                    fill_btb(arch_instr.ip, arch_instr.branch_target,
                             arch_instr.branch_type);
                  }
                }
              }
            } else {
              uint32_t btb_set = BTB.get_set(arch_instr.ip, btb_type);
              int btb_way = BTB.get_way(arch_instr.ip, btb_set, btb_type);

              if (btb_way != max_ways) {
                BTB.sim_hit[cpu][arch_instr.branch_type - 1]++;
                BTB.sim_access[cpu][arch_instr.branch_type - 1]++;
                BTB.update_replacement_state(btb_type, btb_set, btb_way);
              }
            }

            if (curr_ftq_instr->branch_type == BRANCH_DIRECT_CALL ||
                curr_ftq_instr->branch_type == BRANCH_INDIRECT_CALL) {

              uint64_t bb_size = 0;
              auto it =
                  BTB.basic_block_size_per_ip.find(curr_ftq_instr->ip + 4);
              if (it != BTB.basic_block_size_per_ip.end())
                bb_size = it->second;
              bb_size /= 4;
              if (bb_size >= 32)
                bb_size = 31;
              BTB.ras.push(curr_ftq_instr->ip + 4, curr_ftq_instr->ip,
                           curr_ftq_instr->branch_target, bb_size);
            }

            if (curr_ftq_instr->branch_type == BRANCH_RETURN)
              BTB.ras.pop();
#endif

            // call code prefetcher every time the branch predictor is used
            l1i_prefetcher_branch_operate(arch_instr.ip, arch_instr.branch_type,
                                          predicted_branch_target);

            if (curr_ftq_instr->branch_taken != branch_prediction) {
              branch_mispredictions++;
              total_rob_occupancy_at_branch_mispredict += ROB.occupancy;
              if (warmup_complete[cpu]) {
                fetch_stall = 1;
                instrs_to_read_this_cycle = 0;
                curr_ftq_instr->branch_mispredicted = 1;
              }
            } else {
              // correct prediction

              if (branch_prediction == 1) {
                // if correctly predicted taken, then we can't fetch anymore
                // instructions this cycle
                instrs_to_read_this_cycle = 0;
              }
            }

            last_branch_result(arch_instr.ip, curr_ftq_instr->branch_taken);
          }

          if ((num_reads >= instrs_to_read_this_cycle) ||
              !(FTQ.check_occupancy(next_instr.ip)))
            continue_reading = 0;
        } else {
          assert(0);
        }
        instr_unique_id++;
      }
    }
  }
}

uint32_t O3_CPU::add_to_rob(ooo_model_instr *arch_instr) {

  uint32_t index = ROB.tail;

  // sanity check
  if (ROB.entry[index].instr_id != 0) {
    cerr << "[ROB_ERROR] " << __func__ << " is not empty index: " << index;
    cerr << " instr_id: " << ROB.entry[index].instr_id << endl;
    assert(0);
  }

  ROB.entry[index] = *arch_instr;
  ROB.entry[index].event_cycle = current_core_cycle[cpu];

  rob_in++;

  ROB.occupancy++;
  ROB.tail++;
  if (ROB.tail >= ROB.SIZE)
    ROB.tail = 0;

    // DP ( if (warmup_complete[cpu]) {
    // cout << "[ROB] " <<  __func__ << " instr_id: " <<
    // ROB.entry[index].instr_id; cout << " ip: " << hex << ROB.entry[index].ip
    // << dec; cout << " head: " << ROB.head << " tail: " << ROB.tail << "
    // occupancy: " << ROB.occupancy; cout << " event: " <<
    // ROB.entry[index].event_cycle << " current: " << current_core_cycle[cpu] <<
    // endl; });

#ifdef SANITY_CHECK
  if (ROB.entry[index].ip == 0) {
    cerr << "[ROB_ERROR] " << __func__ << " ip is zero index: " << index;
    cerr << " instr_id: " << ROB.entry[index].instr_id
         << " ip: " << ROB.entry[index].ip << endl;
    assert(0);
  }
#endif

  return index;
}

ooo_model_instr *O3_CPU::add_to_ftq(ooo_model_instr *arch_instr) {
  uint32_t index = FTQ.tail;

  uint64_t prev_instr_pa = 0;
  uint64_t prev_translated = 0;
  uint64_t prev_fetched = 0;

  if (FTQ.entry[index].size() == 0) {
    FTQ.entry[index].push_back(*arch_instr);
    FTQ.occupancy++;
  } else if (((FTQ.entry[index][0].ip >> 5) == (arch_instr->ip >> 5)) &&
             (FTQ.entry[index].size() < MAX_INSTR_PER_FTQ_ENTRY)) {
    FTQ.entry[index].push_back(*arch_instr);
    prev_instr_pa = FTQ.entry[index][0].instruction_pa;
    prev_translated = FTQ.entry[index][0].translated;
    prev_fetched = FTQ.entry[index][0].fetched;
  } else {
    FTQ.tail++;
    FTQ.occupancy++;
    if (FTQ.tail >= FTQ.SIZE)
      FTQ.tail = 0;

    index = FTQ.tail;

    assert(FTQ.entry[index].size() == 0);
    FTQ.entry[index].push_back(*arch_instr);
  }

  assert(FTQ.occupancy <= FTQ.SIZE);

  ooo_model_instr *curr_ftq_instr =
      &(FTQ.entry[index][FTQ.entry[index].size() - 1]);

  ftq_in++;

  curr_ftq_instr->event_cycle = current_core_cycle[cpu];

  if (prev_fetched == INFLIGHT)
    curr_ftq_instr->fetch_start_cycle = current_core_cycle[cpu];

  if (prev_fetched == COMPLETED) {
    curr_ftq_instr->fetch_start_cycle = current_core_cycle[cpu];
    curr_ftq_instr->fetch_end_cycle = current_core_cycle[cpu];
  }

  // Neelu: Instead of translating instructions magically, translating them as
  // usual to pay ITLB access penalty.
  curr_ftq_instr->instruction_pa = prev_instr_pa;
  curr_ftq_instr->translated = prev_translated;
  curr_ftq_instr->fetched = prev_fetched;

  return curr_ftq_instr;
}

uint32_t O3_CPU::add_to_decode_buffer(ooo_model_instr *arch_instr) {
  uint32_t index = DECODE_BUFFER.tail;

  if (DECODE_BUFFER.entry[index].instr_id != 0) {
    cerr << "[DECODE_BUFFER_ERROR] " << __func__
         << " is not empty index: " << index;
    cerr << " instr_id: " << DECODE_BUFFER.entry[index].instr_id << endl;
    assert(0);
  }

  DECODE_BUFFER.entry[index] = *arch_instr;
  DECODE_BUFFER.entry[index].event_cycle = current_core_cycle[cpu];

  decode_in++;

  DECODE_BUFFER.occupancy++;
  DECODE_BUFFER.tail++;
  if (DECODE_BUFFER.tail >= DECODE_BUFFER.SIZE) {
    DECODE_BUFFER.tail = 0;
  }

  return index;
}

uint32_t O3_CPU::check_rob(uint64_t instr_id) {
  if ((ROB.head == ROB.tail) && ROB.occupancy == 0)
    return ROB.SIZE;

  if (ROB.head < ROB.tail) {
    for (uint32_t i = ROB.head; i < ROB.tail; i++) {
      if (ROB.entry[i].instr_id == instr_id) {
        // DP ( if (warmup_complete[ROB.cpu]) {
        // cout << "[ROB] " << __func__ << " same instr_id: " <<
        // ROB.entry[i].instr_id; cout << " rob_index: " << i << endl; });
        return i;
      }
    }
  } else {
    for (uint32_t i = ROB.head; i < ROB.SIZE; i++) {
      if (ROB.entry[i].instr_id == instr_id) {
        // DP ( if (warmup_complete[cpu]) {
        // cout << "[ROB] " << __func__ << " same instr_id: " <<
        // ROB.entry[i].instr_id; cout << " rob_index: " << i << endl; });
        return i;
      }
    }
    for (uint32_t i = 0; i < ROB.tail; i++) {
      if (ROB.entry[i].instr_id == instr_id) {
        // DP ( if (warmup_complete[cpu]) {
        // cout << "[ROB] " << __func__ << " same instr_id: " <<
        // ROB.entry[i].instr_id; cout << " rob_index: " << i << endl; });
        return i;
      }
    }
  }

  cerr << "[ROB_ERROR] " << __func__ << " does not have any matching index! "
       << " head=" << ROB.head << " tail=" << ROB.tail << endl;
  cerr << " instr_id: " << instr_id << endl;
  assert(0);

  return ROB.SIZE;
}

void O3_CPU::fetch_instruction() {

  // if we had a branch mispredict, turn fetching back on after the branch
  // mispredict penalty
  if ((fetch_stall == 1) && (current_core_cycle[cpu] >= fetch_resume_cycle) &&
      (fetch_resume_cycle != 0)) {
    fetch_stall = 0;
    fetch_resume_cycle = 0;
  }

  if (FTQ.occupancy == 0) {
    return;
  }

  // Neelu: From FTQ, requests will be sent to L1-I and it will take care of
  // sending the translation request to ITLB as L1 caches are VIPT in this
  // version.

  int ftq_to_l1i = 0;

  for (auto ftq_it : FTQ.entry) {
    if (ftq_it.size() == 0) {
      continue;
    }

    if ((ftq_it[0].fetched == 0)) {
      // add it to the L1-I's read queue
      PACKET fetch_packet;
      fetch_packet.instruction = 1;
      fetch_packet.is_data = 0;
      fetch_packet.fill_level = FILL_L1;
      fetch_packet.fill_l1i = 1;
      fetch_packet.cpu = cpu;
      fetch_packet.address = ftq_it[0].ip >> 6;
      fetch_packet.full_addr = ftq_it[0].ip;
      fetch_packet.full_virtual_address = ftq_it[0].ip;
      fetch_packet.instr_id = ftq_it[0].instr_id;
      fetch_packet.rob_index = 0;
      fetch_packet.producer = 0;
      fetch_packet.ip = ftq_it[0].ip;
      fetch_packet.type = LOAD;
      fetch_packet.asid[0] = 0;
      fetch_packet.asid[1] = 0;
      fetch_packet.event_cycle = current_core_cycle[cpu];

      int rq_index = L1I.add_rq(&fetch_packet);

      if (rq_index != -2) {
        ftq_to_l1i++;
        for (auto &ftq_jt : FTQ.entry) {
          for (auto &ftq_jt_entry : ftq_jt)
            if ((ftq_jt_entry.ip >> 6) == (ftq_it[0].ip >> 6) &&
                ftq_jt_entry.fetched == 0) {
              ftq_jt_entry.translated = INFLIGHT;
              ftq_jt_entry.fetched = INFLIGHT;
              ftq_jt_entry.fetch_start_cycle = current_core_cycle[cpu];
            }
        }
      } else
        break;
    }

  } // Neelu: For loop ending.

  // send to decode stage

  bool decode_full = false;
  auto ftq_it = &(FTQ.entry[FTQ.head]);
  uint64_t decode_width = 60; // DECODE_WIDTH;
  for (uint32_t i = 0; i < decode_width;) {
    if (DECODE_BUFFER.occupancy >= DECODE_BUFFER.SIZE) {
      decode_full = true;
      break;
    }

    if (ftq_it->size() == 0) {
      no_decode_cycle++;
      break;
    }

    if ((ftq_it->begin()->translated == COMPLETED) &&
        (ftq_it->begin()->fetched == COMPLETED)) {
      while (DECODE_BUFFER.occupancy < DECODE_BUFFER.SIZE &&
             ftq_it->size() > 0 && i < decode_width) {
        assert(ftq_it->begin()->fetched == COMPLETED);
        ftq_it->begin()->decode_start_cycle = current_core_cycle[cpu];
        uint32_t decode_index = add_to_decode_buffer(&(*(ftq_it->begin())));
        DECODE_BUFFER.entry[decode_index].event_cycle = 0;

        ftq_it->erase(ftq_it->begin());
        i++;
      }

      if (ftq_it->size() == 0) {
        FTQ.occupancy--;
        assert(FTQ.occupancy >= 0);
        FTQ.head++;
        if (FTQ.head >= FTQ.SIZE)
          FTQ.head = 0;
        if (FTQ.occupancy == 0) {
          FTQ.head = FTQ.tail = 0;
        }
        ftq_it = &(FTQ.entry[FTQ.head]);
      }
    } else {
      no_decode_cycle++;
      break;
    }
  }

} // Ending of fetch_instruction()

#if defined(SHOTGUN_BTB)
void O3_CPU::fill_btb(uint64_t bb_addr, uint64_t target, uint32_t btype) {
  // update SHOTGUN metadata and fill BTB accordingly.

  if (btype == BRANCH_DIRECT_JUMP || btype == BRANCH_INDIRECT) {
    int set = BTB.get_set(bb_addr, IS_UBTB);
    int way = BTB.fill_btb(bb_addr, target, IS_UBTB, UNCONDITIONAL);
  } else if (btype == BRANCH_DIRECT_CALL || btype == BRANCH_INDIRECT_CALL) {
    int set = BTB.get_set(bb_addr, IS_UBTB);
    int way = BTB.fill_btb(bb_addr, target, IS_UBTB, CALL);
  } else if (btype == BRANCH_CONDITIONAL) {
    int set = BTB.get_set(bb_addr, IS_CBTB);
    int way = BTB.get_way(bb_addr, set, IS_CBTB);

    if (way == CBTB_WAY)
      BTB.fill_btb(bb_addr, target, IS_CBTB, 0);
  } else if (btype == BRANCH_RETURN) {

    uint64_t trigger = BTB.ras.caller_trigger[BTB.ras.head];
    uint64_t target_addr = BTB.ras.target_address[BTB.ras.head];

    int set = BTB.get_set(trigger, IS_UBTB);

    int way = BTB.fill_btb(trigger, target_addr, IS_UBTB, CALL);

    // add to RIB
    way = BTB.fill_btb(bb_addr, 0, IS_RIB, 0);
  }
}
#endif

void O3_CPU::decode_and_dispatch() {
  // dispatch DECODE_WIDTH instructions that have decoded into the ROB
  uint32_t count_dispatches = 0;
  for (uint32_t i = 0; i < DECODE_BUFFER.SIZE; i++) {
    ooo_model_instr &entry = DECODE_BUFFER.entry[DECODE_BUFFER.head];

    if (entry.ip == 0)
      break;

    if (((!warmup_complete[cpu]) && (ROB.occupancy < ROB.SIZE)) ||
        ((entry.event_cycle != 0) &&
         (entry.event_cycle < current_core_cycle[cpu]) &&
         (ROB.occupancy < ROB.SIZE))) {
      entry.decode_end_cycle = current_core_cycle[cpu];

      if (entry.btb_miss == 1 && entry.branch_mispredicted == 0) {
        uint8_t branch_type = entry.branch_type;
        if (branch_type == BRANCH_DIRECT_JUMP ||
            branch_type == BRANCH_DIRECT_CALL ||
            (branch_type == BRANCH_CONDITIONAL)) {
          if (warmup_complete[cpu]) {
            fetch_resume_cycle =
                current_core_cycle[cpu] + 1; // Resume fetch from next cycle.
            assert(entry.stall_begin_cycle != UINT64_MAX);
            calculate_stall_cycle(entry);
          }
          entry.btb_miss = 0;
#if defined(SHOTGUN_BTB)
          fill_btb(entry.ip, entry.branch_target, entry.branch_type);

#elif defined(SKEWED_BTB) || defined(BASELINEBTB) ||                \
    defined(MICRO_BTB) || defined(FDIPX_BTB) || defined(DIV_CONQ)
          BTB.fill_btb(entry.ip, entry.branch_target, entry.branch_type,
                       IS_BTB_BOTH);
#endif
          BTB.total_miss_latency +=
              (current_core_cycle[cpu] - entry.cycle_enqueued);
        }
      } else if (entry.btb_miss == 2 && entry.branch_mispredicted == 0) {
        uint8_t branch_type = entry.branch_type;
        assert(branch_type == BRANCH_RETURN);
        {
          if (warmup_complete[cpu]) {
            fetch_resume_cycle =
                current_core_cycle[cpu] + 1; // Resume fetch from next cycle.
            assert(entry.stall_begin_cycle != UINT64_MAX);
            calculate_stall_cycle(entry);
          }
          entry.btb_miss = 0;
#if defined(SHOTGUN_BTB)
          fill_btb(entry.ip, entry.branch_target, entry.branch_type);

#elif defined(SKEWED_BTB) || defined(BASELINEBTB) ||                \
    defined(MICRO_BTB) || defined(FDIPX_BTB) || defined(DIV_CONQ)
          BTB.fill_btb(entry.ip, entry.branch_target, entry.branch_type,
                       IS_BTB_BOTH);
#endif
          BTB.total_miss_latency +=
              (current_core_cycle[cpu] - entry.cycle_enqueued);
        }
      }

      // move this instruction to the ROB if there's space
      uint32_t rob_index = add_to_rob(&entry);
      ROB.entry[rob_index].event_cycle = current_core_cycle[cpu];

      ooo_model_instr empty_entry;
      entry = empty_entry;

      DECODE_BUFFER.head++;
      if (DECODE_BUFFER.head >= DECODE_BUFFER.SIZE) {
        DECODE_BUFFER.head = 0;
      }
      DECODE_BUFFER.occupancy--;

      count_dispatches++;
      if (count_dispatches >= DECODE_WIDTH) {
        break;
      }
    } else {
      break;
    }
  }

  dispatches_per_cycle[count_dispatches]++;
  if (count_dispatches == 0 && ROB.occupancy == ROB.SIZE)
    rob_full_cycles++;

  // make new instructions pay decode penalty if they miss in the decoded
  // instruction cache
  uint32_t decode_index = DECODE_BUFFER.head;
  uint32_t count_decodes = 0;
  for (uint32_t i = 0; i < DECODE_BUFFER.SIZE; i++) {
    if (DECODE_BUFFER.entry[decode_index].ip == 0) {
      break;
    }

    if (DECODE_BUFFER.entry[decode_index].event_cycle == 0) {
      // apply decode latency
      DECODE_BUFFER.entry[decode_index].event_cycle =
          current_core_cycle[cpu] + DECODE_LATENCY;
      count_decodes++;
    }

    if (decode_index == DECODE_BUFFER.tail) {
      break;
    }
    decode_index++;
    if (decode_index >= DECODE_BUFFER.SIZE) {
      decode_index = 0;
    }

    if (count_decodes >= DECODE_WIDTH) {
      break;
    }
  }
} // Ending of decode_and_dispatch()

int O3_CPU::prefetch_code_line(uint64_t pf_v_addr) {
  if (pf_v_addr == 0) {
    cerr << "Cannot prefetch code line 0x0 !!!" << endl;
    assert(0);
  }

  L1I.pf_requested++;

  if (L1I.PQ.occupancy < L1I.PQ.SIZE) {
    // Neelu: Cannot magically translate prefetches, need to get realistic and
    // access the TLBs.
    // magically translate prefetches

    PACKET pf_packet;
    pf_packet.instruction = 1; // this is a code prefetch
    pf_packet.is_data = 0;
    pf_packet.fill_level = FILL_L1;
    pf_packet.fill_l1i = 1;
    pf_packet.pf_origin_level = FILL_L1;
    pf_packet.cpu = cpu;

    pf_packet.address = pf_v_addr >> LOG2_BLOCK_SIZE;
    pf_packet.full_addr = pf_v_addr;

    // Neelu: Marking translated = 0
    pf_packet.translated = 0;
    pf_packet.full_physical_address = 0;

    pf_packet.ip = pf_v_addr;
    pf_packet.type = PREFETCH;
    pf_packet.event_cycle = current_core_cycle[cpu];

    L1I.add_pq(&pf_packet);
    L1I.pf_issued++;

    return 1;
  }

  return 0;
}

// TODO: When should we update ROB.schedule_event_cycle?
// I. Instruction is fetched
// II. Instruction is completed
// III. Instruction is retired
void O3_CPU::schedule_instruction() {
  if ((ROB.head == ROB.tail) && ROB.occupancy == 0)
    return;

  // execution is out-of-order but we have an in-order scheduling algorithm to
  // detect all RAW dependencies
  uint32_t limit = ROB.next_fetch[1];
  num_searched = 0;
  if (ROB.head < limit) {
    for (uint32_t i = ROB.head; i < limit; i++) {
      if ((ROB.entry[i].fetched != COMPLETED) ||
          (ROB.entry[i].event_cycle > current_core_cycle[cpu]) ||
          (num_searched >= SCHEDULER_SIZE))
        return;

      if (ROB.entry[i].scheduled == 0)
        do_scheduling(i);

      num_searched++;
    }
  } else {
    for (uint32_t i = ROB.head; i < ROB.SIZE; i++) {
      if ((ROB.entry[i].fetched != COMPLETED) ||
          (ROB.entry[i].event_cycle > current_core_cycle[cpu]) ||
          (num_searched >= SCHEDULER_SIZE))
        return;

      if (ROB.entry[i].scheduled == 0)
        do_scheduling(i);

      num_searched++;
    }
    for (uint32_t i = 0; i < limit; i++) {
      if ((ROB.entry[i].fetched != COMPLETED) ||
          (ROB.entry[i].event_cycle > current_core_cycle[cpu]) ||
          (num_searched >= SCHEDULER_SIZE))
        return;

      if (ROB.entry[i].scheduled == 0)
        do_scheduling(i);

      num_searched++;
    }
  }
}

void O3_CPU::do_scheduling(uint32_t rob_index) {
  ROB.entry[rob_index].reg_ready =
      1; // reg_ready will be reset to 0 if there is RAW dependency

  reg_dependency(rob_index);
  ROB.next_schedule = (rob_index == (ROB.SIZE - 1)) ? 0 : (rob_index + 1);

  if (ROB.entry[rob_index].is_memory)
    ROB.entry[rob_index].scheduled = INFLIGHT;
  else {
    ROB.entry[rob_index].scheduled = COMPLETED;

    // ADD LATENCY
    if (ROB.entry[rob_index].event_cycle < current_core_cycle[cpu])
      ROB.entry[rob_index].event_cycle =
          current_core_cycle[cpu] + SCHEDULING_LATENCY;
    else
      ROB.entry[rob_index].event_cycle += SCHEDULING_LATENCY;

    if (ROB.entry[rob_index].reg_ready) {

#ifdef SANITY_CHECK
      if (RTE1[RTE1_tail] < ROB_SIZE)
        assert(0);
#endif
      // remember this rob_index in the Ready-To-Execute array 1
      RTE1[RTE1_tail] = rob_index;

      RTE1_tail++;
      if (RTE1_tail == ROB_SIZE)
        RTE1_tail = 0;
    }
  }
}

void O3_CPU::reg_dependency(uint32_t rob_index) {
  // check RAW dependency
  int prior = rob_index - 1;
  if (prior < 0)
    prior = ROB.SIZE - 1;

  if (rob_index != ROB.head) {
    if ((int)ROB.head <= prior) {
      for (int i = prior; i >= (int)ROB.head; i--)
        if (ROB.entry[i].executed != COMPLETED) {
          for (uint32_t j = 0; j < NUM_INSTR_SOURCES; j++) {
            if (ROB.entry[rob_index].source_registers[j] &&
                (ROB.entry[rob_index].reg_RAW_checked[j] == 0))
              reg_RAW_dependency(i, rob_index, j);
          }
        }
    } else {
      for (int i = prior; i >= 0; i--)
        if (ROB.entry[i].executed != COMPLETED) {
          for (uint32_t j = 0; j < NUM_INSTR_SOURCES; j++) {
            if (ROB.entry[rob_index].source_registers[j] &&
                (ROB.entry[rob_index].reg_RAW_checked[j] == 0))
              reg_RAW_dependency(i, rob_index, j);
          }
        }
      for (int i = ROB.SIZE - 1; i >= (int)ROB.head; i--)
        if (ROB.entry[i].executed != COMPLETED) {
          for (uint32_t j = 0; j < NUM_INSTR_SOURCES; j++) {
            if (ROB.entry[rob_index].source_registers[j] &&
                (ROB.entry[rob_index].reg_RAW_checked[j] == 0))
              reg_RAW_dependency(i, rob_index, j);
          }
        }
    }
  }
}

void O3_CPU::reg_RAW_dependency(uint32_t prior, uint32_t current,
                                uint32_t source_index) {
  for (uint32_t i = 0; i < MAX_INSTR_DESTINATIONS; i++) {
    if (ROB.entry[prior].destination_registers[i] == 0)
      continue;

    if (ROB.entry[prior].destination_registers[i] ==
        ROB.entry[current].source_registers[source_index]) {

      // we need to mark this dependency in the ROB since the producer might not
      // be added in the store queue yet
      ROB.entry[prior].registers_instrs_depend_on_me.insert(
          current); // this load cannot be executed until the prior store gets
                    // executed
      ROB.entry[prior].registers_index_depend_on_me[source_index].insert(
          current); // this load cannot be executed until the prior store gets
                    // executed
      ROB.entry[prior].reg_RAW_producer = 1;

      ROB.entry[current].reg_ready = 0;
      ROB.entry[current].producer_id = ROB.entry[prior].instr_id;
      ROB.entry[current].num_reg_dependent++;
      ROB.entry[current].reg_RAW_checked[source_index] = 1;

      return;
    }
  }
}

void O3_CPU::execute_instruction() {
  if ((ROB.head == ROB.tail) && ROB.occupancy == 0)
    return;

  // out-of-order execution for non-memory instructions
  // memory instructions are handled by memory_instruction()
  uint32_t exec_issued = 0, num_iteration = 0;

  while (exec_issued < EXEC_WIDTH) {
    if (RTE0[RTE0_head] < ROB_SIZE) {
      uint32_t exec_index = RTE0[RTE0_head];
      if (ROB.entry[exec_index].event_cycle <= current_core_cycle[cpu]) {
        do_execution(exec_index);

        RTE0[RTE0_head] = ROB_SIZE;
        RTE0_head++;
        if (RTE0_head == ROB_SIZE)
          RTE0_head = 0;
        exec_issued++;
      }
    } else {
      break;
    }

    num_iteration++;
    if (num_iteration == (ROB_SIZE - 1))
      break;
  }

  num_iteration = 0;
  while (exec_issued < EXEC_WIDTH) {
    if (RTE1[RTE1_head] < ROB_SIZE) {
      uint32_t exec_index = RTE1[RTE1_head];
      if (ROB.entry[exec_index].event_cycle <= current_core_cycle[cpu]) {
        do_execution(exec_index);

        RTE1[RTE1_head] = ROB_SIZE;
        RTE1_head++;
        if (RTE1_head == ROB_SIZE)
          RTE1_head = 0;
        exec_issued++;
      }
    } else {
      break;
    }

    num_iteration++;
    if (num_iteration == (ROB_SIZE - 1))
      break;
  }
}

void O3_CPU::do_execution(uint32_t rob_index) {
  ROB.entry[rob_index].executed = INFLIGHT;

  ROB.entry[rob_index].execute_start_cycle = current_core_cycle[cpu];

  // ADD LATENCY
  if (ROB.entry[rob_index].event_cycle < current_core_cycle[cpu])
    ROB.entry[rob_index].event_cycle = current_core_cycle[cpu] + EXEC_LATENCY;
  else
    ROB.entry[rob_index].event_cycle += EXEC_LATENCY;

  inflight_reg_executions++;
}

void O3_CPU::schedule_memory_instruction() {
  if ((ROB.head == ROB.tail) && ROB.occupancy == 0)
    return;

  // execution is out-of-order but we have an in-order scheduling algorithm to
  // detect all RAW dependencies
  uint32_t limit = ROB.next_schedule;
  num_searched = 0;
  if (ROB.head < limit) {
    for (uint32_t i = ROB.head; i < limit; i++) {

      if (ROB.entry[i].is_memory == 0)
        continue;

      if ((ROB.entry[i].fetched != COMPLETED) ||
          (ROB.entry[i].event_cycle > current_core_cycle[cpu]) ||
          (num_searched >= SCHEDULER_SIZE))
        break;

      if (ROB.entry[i].is_memory && ROB.entry[i].reg_ready &&
          (ROB.entry[i].scheduled == INFLIGHT))
        do_memory_scheduling(i);
    }
  } else {
    for (uint32_t i = ROB.head; i < ROB.SIZE; i++) {

      if (ROB.entry[i].is_memory == 0)
        continue;

      if ((ROB.entry[i].fetched != COMPLETED) ||
          (ROB.entry[i].event_cycle > current_core_cycle[cpu]) ||
          (num_searched >= SCHEDULER_SIZE))
        break;

      if (ROB.entry[i].is_memory && ROB.entry[i].reg_ready &&
          (ROB.entry[i].scheduled == INFLIGHT))
        do_memory_scheduling(i);
    }
    for (uint32_t i = 0; i < limit; i++) {

      if (ROB.entry[i].is_memory == 0)
        continue;

      if ((ROB.entry[i].fetched != COMPLETED) ||
          (ROB.entry[i].event_cycle > current_core_cycle[cpu]) ||
          (num_searched >= SCHEDULER_SIZE))
        break;

      if (ROB.entry[i].is_memory && ROB.entry[i].reg_ready &&
          (ROB.entry[i].scheduled == INFLIGHT))
        do_memory_scheduling(i);
    }
  }
}

void O3_CPU::execute_memory_instruction() {
  operate_lsq();
  operate_cache();
}

void O3_CPU::do_memory_scheduling(uint32_t rob_index) {
  uint32_t not_available = check_and_add_lsq(rob_index);
  if (not_available == 0) {
    ROB.entry[rob_index].scheduled = COMPLETED;
    if (ROB.entry[rob_index].executed ==
        0) // it could be already set to COMPLETED due to store-to-load
           // forwarding
      ROB.entry[rob_index].executed = INFLIGHT;
    ROB.entry[rob_index].execute_start_cycle = current_core_cycle[cpu];
  }

  num_searched++;
}

uint32_t O3_CPU::check_and_add_lsq(uint32_t rob_index) {
  uint32_t num_mem_ops = 0, num_added = 0;

  // load
  for (uint32_t i = 0; i < NUM_INSTR_SOURCES; i++) {
    if (ROB.entry[rob_index].source_memory[i]) {
      num_mem_ops++;
      if (ROB.entry[rob_index].source_added[i])
        num_added++;
      else if (LQ.occupancy < LQ.SIZE) {
        add_load_queue(rob_index, i);
        num_added++;
      } else {
      }
    }
  }

  // store
  for (uint32_t i = 0; i < MAX_INSTR_DESTINATIONS; i++) {
    if (ROB.entry[rob_index].destination_memory[i]) {
      num_mem_ops++;
      if (ROB.entry[rob_index].destination_added[i])
        num_added++;
      else if (SQ.occupancy < SQ.SIZE) {
        if (STA[STA_head] == ROB.entry[rob_index].instr_id) {
          add_store_queue(rob_index, i);
          num_added++;
        }
        // add_store_queue(rob_index, i);
        // num_added++;
      } else {
      }
    }
  }

  if (num_added == num_mem_ops)
    return 0;

  uint32_t not_available = num_mem_ops - num_added;
  if (not_available > num_mem_ops) {
    cerr << "instr_id: " << ROB.entry[rob_index].instr_id << endl;
    assert(0);
  }

  return not_available;
}

void O3_CPU::add_load_queue(uint32_t rob_index, uint32_t data_index) {
  // search for an empty slot
  uint32_t lq_index = LQ.SIZE;
  for (uint32_t i = 0; i < LQ.SIZE; i++) {
    if (LQ.entry[i].virtual_address == 0) {
      lq_index = i;
      break;
    }
  }

  // sanity check
  if (lq_index == LQ.SIZE) {
    cerr << "instr_id: " << ROB.entry[rob_index].instr_id
         << " no empty slot in the load queue!!!" << endl;
    assert(0);
  }

  sim_load_gen++;

  // add it to the load queue
  ROB.entry[rob_index].lq_index[data_index] = lq_index;
  LQ.entry[lq_index].instr_id = ROB.entry[rob_index].instr_id;
  LQ.entry[lq_index].virtual_address =
      ROB.entry[rob_index].source_memory[data_index];
  LQ.entry[lq_index].ip = ROB.entry[rob_index].ip;
  LQ.entry[lq_index].data_index = data_index;
  LQ.entry[lq_index].rob_index = rob_index;
  LQ.entry[lq_index].asid[0] = ROB.entry[rob_index].asid[0];
  LQ.entry[lq_index].asid[1] = ROB.entry[rob_index].asid[1];
  LQ.entry[lq_index].event_cycle = current_core_cycle[cpu] + SCHEDULING_LATENCY;
  LQ.occupancy++;

  // check RAW dependency
  int prior = rob_index - 1;
  if (prior < 0)
    prior = ROB.SIZE - 1;

  if (rob_index != ROB.head) {
    if ((int)ROB.head <= prior) {
      for (int i = prior; i >= (int)ROB.head; i--) {
        if (LQ.entry[lq_index].producer_id != UINT64_MAX)
          break;

        mem_RAW_dependency(i, rob_index, data_index, lq_index);
      }
    } else {
      for (int i = prior; i >= 0; i--) {
        if (LQ.entry[lq_index].producer_id != UINT64_MAX)
          break;

        mem_RAW_dependency(i, rob_index, data_index, lq_index);
      }
      for (int i = ROB.SIZE - 1; i >= (int)ROB.head; i--) {
        if (LQ.entry[lq_index].producer_id != UINT64_MAX)
          break;

        mem_RAW_dependency(i, rob_index, data_index, lq_index);
      }
    }
  }

  // check
  // 1) if store-to-load forwarding is possible
  // 2) if there is WAR that are not correctly executed
  uint32_t forwarding_index = SQ.SIZE;
  for (uint32_t i = 0; i < SQ.SIZE; i++) {

    // skip empty slot
    if (SQ.entry[i].virtual_address == 0)
      continue;

    // forwarding should be done by the SQ entry that holds the same producer_id
    // from RAW dependency check
    if (SQ.entry[i].virtual_address ==
        LQ.entry[lq_index].virtual_address) { // store-to-load forwarding check

      // forwarding store is in the SQ
      if ((rob_index != ROB.head) &&
          (LQ.entry[lq_index].producer_id == SQ.entry[i].instr_id)) { // RAW
        forwarding_index = i;
        break; // should be break
      }

      if ((LQ.entry[lq_index].producer_id == UINT64_MAX) &&
          (LQ.entry[lq_index].instr_id <= SQ.entry[i].instr_id)) { // WAR
        // a load is about to be added in the load queue and we found a store
        // that is "logically later in the program order but already executed"
        // => this is not correctly executed WAR due to out-of-order execution,
        // this case is possible, for example 1) application is load intensive
        // and load queue is full 2) we have loads that can't be added in the
        // load queue 3) subsequent stores logically behind in the program order
        // are added in the store queue first

        // thanks to the store buffer, data is not written back to the memory
        // system until retirement also due to in-order retirement, this
        // "already executed store" cannot be retired until we finish the prior
        // load instruction if we detect WAR when a load is added in the load
        // queue, just let the load instruction to access the memory system no
        // need to mark any dependency because this is actually WAR not RAW

        // do not forward data from the store queue since this is WAR
        // just read correct data from data cache

        LQ.entry[lq_index].physical_address = 0;
        LQ.entry[lq_index].translated = 0;
        LQ.entry[lq_index].fetched = 0;

        // DP(if(warmup_complete[cpu]) {
        // cout << "[LQ] " << __func__ << " instr_id: " <<
        // LQ.entry[lq_index].instr_id << " reset fetched: " <<
        // +LQ.entry[lq_index].fetched; cout << " to obey WAR store instr_id: "
        // << SQ.entry[i].instr_id << " cycle: " << current_core_cycle[cpu] <<
        // endl; });
      }
    }
  }

  if (forwarding_index != SQ.SIZE) { // we have a store-to-load forwarding

    if ((SQ.entry[forwarding_index].fetched == COMPLETED) &&
        (SQ.entry[forwarding_index].event_cycle <= current_core_cycle[cpu])) {

      //@Vishal: count RAW forwarding
      sim_RAW_hits++;

      LQ.entry[lq_index].translated = COMPLETED;
      LQ.entry[lq_index].fetched = COMPLETED;

      uint32_t fwr_rob_index = LQ.entry[lq_index].rob_index;
      ROB.entry[fwr_rob_index].num_mem_ops--;
      ROB.entry[fwr_rob_index].event_cycle = current_core_cycle[cpu];
      if (ROB.entry[fwr_rob_index].num_mem_ops < 0) {
        cerr << "instr_id: " << ROB.entry[fwr_rob_index].instr_id << endl;
        assert(0);
      }
      if (ROB.entry[fwr_rob_index].num_mem_ops == 0)
        inflight_mem_executions++;

      // DP(if(warmup_complete[cpu]) {
      // cout << "[LQ] " << __func__ << " instr_id: " <<
      // LQ.entry[lq_index].instr_id << hex; cout << " full_addr: " <<
      // LQ.entry[lq_index].physical_address << dec << " is forwarded by store
      // instr_id: "; cout << SQ.entry[forwarding_index].instr_id << "
      // remain_num_ops: " << ROB.entry[fwr_rob_index].num_mem_ops << " cycle: "
      // << current_core_cycle[cpu] << endl; });

      release_load_queue(lq_index);
    } else
      ; // store is not executed yet, forwarding will be handled by
        // execute_store()
  }

  // succesfully added to the load queue
  ROB.entry[rob_index].source_added[data_index] = 1;

  if (LQ.entry[lq_index].virtual_address &&
      (LQ.entry[lq_index].producer_id ==
       UINT64_MAX)) { // not released and no forwarding
    RTL0[RTL0_tail] = lq_index;
    RTL0_tail++;
    if (RTL0_tail == LQ_SIZE)
      RTL0_tail = 0;

    // DP (if (warmup_complete[cpu]) {
    // cout << "[RTL0] " << __func__ << " instr_id: " <<
    // LQ.entry[lq_index].instr_id << " rob_index: " <<
    // LQ.entry[lq_index].rob_index << " is added to RTL0"; cout << " head: " <<
    // RTL0_head << " tail: " << RTL0_tail << endl; });
  }

  // DP(if(warmup_complete[cpu]) {
  // cout << "[LQ] " << __func__ << " instr_id: " <<
  // LQ.entry[lq_index].instr_id; cout << " is added in the LQ address: " << hex
  // << LQ.entry[lq_index].virtual_address << dec << " translated: " <<
  // +LQ.entry[lq_index].translated; cout << " fetched: " <<
  // +LQ.entry[lq_index].fetched << " index: " << lq_index << " occupancy: " <<
  // LQ.occupancy << " cycle: " << current_core_cycle[cpu] << endl; });
}

void O3_CPU::mem_RAW_dependency(uint32_t prior, uint32_t current,
                                uint32_t data_index, uint32_t lq_index) {
  for (uint32_t i = 0; i < MAX_INSTR_DESTINATIONS; i++) {
    if (ROB.entry[prior].destination_memory[i] == 0)
      continue;

    if (ROB.entry[prior].destination_memory[i] ==
        ROB.entry[current]
            .source_memory[data_index]) { //  store-to-load forwarding check

      // we need to mark this dependency in the ROB since the producer might not
      // be added in the store queue yet
      ROB.entry[prior].memory_instrs_depend_on_me.insert(
          current); // this load cannot be executed until the prior store gets
                    // executed
      ROB.entry[prior].is_producer = 1;
      LQ.entry[lq_index].producer_id = ROB.entry[prior].instr_id;
      LQ.entry[lq_index].translated = INFLIGHT;

      // DP (if(warmup_complete[cpu]) {
      // cout << "[LQ] " << __func__ << " RAW producer instr_id: " <<
      // ROB.entry[prior].instr_id << " consumer_id: " <<
      // ROB.entry[current].instr_id << " lq_index: " << lq_index; cout << hex <<
      // " address: " << ROB.entry[prior].destination_memory[i] << dec << endl;
      // });

      return;
    }
  }
}

void O3_CPU::add_store_queue(uint32_t rob_index, uint32_t data_index) {
  uint32_t sq_index = SQ.tail;
#ifdef SANITY_CHECK
  if (SQ.entry[sq_index].virtual_address)
    assert(0);
#endif

  sim_store_gen++;

  // add it to the store queue
  ROB.entry[rob_index].sq_index[data_index] = sq_index;
  SQ.entry[sq_index].instr_id = ROB.entry[rob_index].instr_id;
  SQ.entry[sq_index].virtual_address =
      ROB.entry[rob_index].destination_memory[data_index];
  SQ.entry[sq_index].ip = ROB.entry[rob_index].ip;
  SQ.entry[sq_index].data_index = data_index;
  SQ.entry[sq_index].rob_index = rob_index;
  SQ.entry[sq_index].asid[0] = ROB.entry[rob_index].asid[0];
  SQ.entry[sq_index].asid[1] = ROB.entry[rob_index].asid[1];
  SQ.entry[sq_index].event_cycle = current_core_cycle[cpu] + SCHEDULING_LATENCY;

  SQ.occupancy++;
  SQ.tail++;
  if (SQ.tail == SQ.SIZE)
    SQ.tail = 0;

  // succesfully added to the store queue
  ROB.entry[rob_index].destination_added[data_index] = 1;

  STA[STA_head] = UINT64_MAX;
  STA_head++;
  if (STA_head == STA_SIZE)
    STA_head = 0;

  RTS0[RTS0_tail] = sq_index;
  RTS0_tail++;
  if (RTS0_tail == SQ_SIZE)
    RTS0_tail = 0;
}

void O3_CPU::operate_lsq() {
  // handle store
  uint32_t store_issued = 0, num_iteration = 0;

  //@Vishal: VIPT Execute store without sending translation request to DTLB.
  while (store_issued < SQ_WIDTH) {
    if (RTS0[RTS0_head] < SQ_SIZE) {
      uint32_t sq_index = RTS0[RTS0_head];
      if (SQ.entry[sq_index].event_cycle <= current_core_cycle[cpu]) {
        execute_store(SQ.entry[sq_index].rob_index, sq_index,
                      SQ.entry[sq_index].data_index);

        RTS0[RTS0_head] = SQ_SIZE;
        RTS0_head++;
        if (RTS0_head == SQ_SIZE)
          RTS0_head = 0;

        store_issued++;
      }
    } else {
      ////DP (if (warmup_complete[cpu]) {
      ////cout << "[RTS0] is empty head: " << RTS0_head << " tail: " <<
      ///RTS0_tail << endl; });
      break;
    }

    num_iteration++;
    if (num_iteration == (SQ_SIZE - 1))
      break;
  }

  unsigned load_issued = 0;
  num_iteration = 0;

  //@Vishal: VIPT. Send request to L1D.

  while (load_issued < LQ_WIDTH) {
    if (RTL0[RTL0_head] < LQ_SIZE) {
      uint32_t lq_index = RTL0[RTL0_head];
      if (LQ.entry[lq_index].event_cycle <= current_core_cycle[cpu]) {

        int rq_index = execute_load(LQ.entry[lq_index].rob_index, lq_index,
                                    LQ.entry[lq_index].data_index);

        if (rq_index != -2) {
          RTL0[RTL0_head] = LQ_SIZE;
          RTL0_head++;
          if (RTL0_head == LQ_SIZE)
            RTL0_head = 0;

          load_issued++;
        }
      }
    } else {
      ////DP (if (warmup_complete[cpu]) {
      ////cout << "[RTL1] is empty head: " << RTL1_head << " tail: " <<
      ///RTL1_tail << endl; });
      break;
    }

    num_iteration++;
    if (num_iteration == (LQ_SIZE - 1))
      break;
  }
}

void O3_CPU::execute_store(uint32_t rob_index, uint32_t sq_index,
                           uint32_t data_index) {
  SQ.entry[sq_index].fetched = COMPLETED;
  SQ.entry[sq_index].event_cycle = current_core_cycle[cpu];

  ROB.entry[rob_index].num_mem_ops--;
  ROB.entry[rob_index].event_cycle = current_core_cycle[cpu];
  if (ROB.entry[rob_index].num_mem_ops < 0) {
    cerr << "instr_id: " << ROB.entry[rob_index].instr_id << endl;
    assert(0);
  }

  if (ROB.entry[rob_index].num_mem_ops == 0)
    inflight_mem_executions++;

  // DP (if (warmup_complete[cpu]) {
  // cout << "[SQ1] " << __func__ << " instr_id: " <<
  // SQ.entry[sq_index].instr_id << hex; cout << " full_address: " <<
  // SQ.entry[sq_index].physical_address << dec << " remain_mem_ops: " <<
  // ROB.entry[rob_index].num_mem_ops; cout << " event_cycle: " <<
  // SQ.entry[sq_index].event_cycle << endl; });

  // resolve RAW dependency after DTLB access
  // check if this store has dependent loads
  if (ROB.entry[rob_index].is_producer) {
    ITERATE_SET(dependent, ROB.entry[rob_index].memory_instrs_depend_on_me,
                ROB_SIZE) {
      // check if dependent loads are already added in the load queue
      for (uint32_t j = 0; j < NUM_INSTR_SOURCES;
           j++) { // which one is dependent?
        if (ROB.entry[dependent].source_memory[j] &&
            ROB.entry[dependent].source_added[j]) {
          if (ROB.entry[dependent].source_memory[j] ==
              SQ.entry[sq_index]
                  .virtual_address) { // this is required since a single
                                      // instruction can issue multiple loads

            //@Vishal: count RAW forwarding
            sim_RAW_hits++;

            // now we can resolve RAW dependency
            uint32_t lq_index = ROB.entry[dependent].lq_index[j];
#ifdef SANITY_CHECK
            if (lq_index >= LQ.SIZE)
              assert(0);
            if (LQ.entry[lq_index].producer_id != SQ.entry[sq_index].instr_id) {
              cerr << "[SQ2] " << __func__ << " lq_index: " << lq_index
                   << " producer_id: " << LQ.entry[lq_index].producer_id;
              cerr << " does not match to the store instr_id: "
                   << SQ.entry[sq_index].instr_id << endl;
              assert(0);
            }
#endif
            // update correspodning LQ entry
            // @Vishal: Dependent load can now get the data, translation is not
            // required
            // LQ.entry[lq_index].physical_address =
            // (SQ.entry[sq_index].physical_address & ~(uint64_t) ((1 <<
            // LOG2_BLOCK_SIZE) - 1)) | (LQ.entry[lq_index].virtual_address & ((1
            // << LOG2_BLOCK_SIZE) - 1));
            LQ.entry[lq_index].translated = COMPLETED;
            LQ.entry[lq_index].fetched = COMPLETED;
            LQ.entry[lq_index].event_cycle = current_core_cycle[cpu];

            uint32_t fwr_rob_index = LQ.entry[lq_index].rob_index;
            ROB.entry[fwr_rob_index].num_mem_ops--;
            ROB.entry[fwr_rob_index].event_cycle = current_core_cycle[cpu];
#ifdef SANITY_CHECK
            if (ROB.entry[fwr_rob_index].num_mem_ops < 0) {
              cerr << "instr_id: " << ROB.entry[fwr_rob_index].instr_id << endl;
              assert(0);
            }
#endif
            if (ROB.entry[fwr_rob_index].num_mem_ops == 0)
              inflight_mem_executions++;

            // DP(if(warmup_complete[cpu]) {
            // cout << "[LQ3] " << __func__ << " instr_id: " <<
            // LQ.entry[lq_index].instr_id << hex; cout << " full_addr: " <<
            // LQ.entry[lq_index].physical_address << dec << " is forwarded by
            // store instr_id: "; cout << SQ.entry[sq_index].instr_id << "
            // remain_num_ops: " << ROB.entry[fwr_rob_index].num_mem_ops << "
            // cycle: " << current_core_cycle[cpu] << endl; });

            release_load_queue(lq_index);

            // clear dependency bit
            if (j == (NUM_INSTR_SOURCES - 1))
              ROB.entry[rob_index].memory_instrs_depend_on_me.insert(dependent);
          }
        }
      }
    }
  }
}

int O3_CPU::execute_load(uint32_t rob_index, uint32_t lq_index,
                         uint32_t data_index) {
  // add it to L1D
  PACKET data_packet;
  data_packet.fill_level = FILL_L1;
  data_packet.fill_l1d = 1;
  data_packet.cpu = cpu;
  data_packet.data_index = LQ.entry[lq_index].data_index;
  data_packet.lq_index = lq_index;

  //@Vishal: VIPT send virtual address instead of physical address
  // data_packet.address = LQ.entry[lq_index].physical_address >>
  // LOG2_BLOCK_SIZE; data_packet.full_addr =
  // LQ.entry[lq_index].physical_address;
  data_packet.address = LQ.entry[lq_index].virtual_address >> LOG2_BLOCK_SIZE;
  data_packet.full_addr = LQ.entry[lq_index].virtual_address;
  data_packet.full_virtual_address = LQ.entry[lq_index].virtual_address;

  data_packet.instr_id = LQ.entry[lq_index].instr_id;
  data_packet.rob_index = LQ.entry[lq_index].rob_index;
  data_packet.ip = LQ.entry[lq_index].ip;

  // Neelu: Setting critical ip flag in packet if ip is identified as critical
#ifdef DETECT_CRITICAL_IPS
  if (critical_ips[cpu].find(LQ.entry[lq_index].ip) != critical_ips[cpu].end())
    data_packet.critical_ip_flag = 1;
#endif

  data_packet.type = LOAD;
  data_packet.asid[0] = LQ.entry[lq_index].asid[0];
  data_packet.asid[1] = LQ.entry[lq_index].asid[1];
  data_packet.event_cycle = LQ.entry[lq_index].event_cycle;

  int rq_index = L1D.add_rq(&data_packet);

  if (rq_index == -2)
    return rq_index;
  else {
    LQ.entry[lq_index].fetched = INFLIGHT;

    sim_load_sent++;
  }

  return rq_index;
}

void O3_CPU::complete_execution(uint32_t rob_index) {
  if (ROB.entry[rob_index].is_memory == 0) {
    if ((ROB.entry[rob_index].executed == INFLIGHT) &&
        (ROB.entry[rob_index].event_cycle <= current_core_cycle[cpu])) {

      ROB.entry[rob_index].executed = COMPLETED;
      ROB.entry[rob_index].execute_end_cycle = current_core_cycle[cpu];
      inflight_reg_executions--;
      completed_executions++;

      if (ROB.entry[rob_index].reg_RAW_producer)
        reg_RAW_release(rob_index);

      if (ROB.entry[rob_index].branch_mispredicted) {
        if (warmup_complete[cpu]) {
          ooo_model_instr &entry = ROB.entry[rob_index];

          fetch_resume_cycle = current_core_cycle[cpu] + 1;
          // cout << entry.instr_id << " " << entry.stall_begin_cycle << " " <<
          // entry.fetch_start_cycle << " " << entry.fetch_end_cycle << " " <<
          // entry.decode_start_cycle << " " << entry.decode_end_cycle << " " <<
          // entry.execute_start_cycle << " " << entry.execute_end_cycle << endl;
        }

        if (ROB.entry[rob_index].branch_taken) {
#if defined(SHOTGUN_BTB)
          fill_btb(ROB.entry[rob_index].ip, ROB.entry[rob_index].branch_target,
                   ROB.entry[rob_index].branch_type);
#elif defined(SKEWED_BTB) || defined(BASELINEBTB) ||                \
    defined(MICRO_BTB) || defined(FDIPX_BTB) || defined(DIV_CONQ)
          BTB.fill_btb(ROB.entry[rob_index].ip,
                       ROB.entry[rob_index].branch_target,
                       ROB.entry[rob_index].branch_type, IS_BTB_BOTH);
#endif
        }
      }

      if (ROB.entry[rob_index].btb_miss &&
          ROB.entry[rob_index].branch_mispredicted == 0) {
        uint8_t branch_type = ROB.entry[rob_index].branch_type;
        assert(branch_type != BRANCH_DIRECT_JUMP &&
               branch_type != BRANCH_DIRECT_CALL &&
               branch_type != BRANCH_CONDITIONAL);
        if (warmup_complete[cpu]) {
          ooo_model_instr &entry = ROB.entry[rob_index];

          fetch_resume_cycle =
              current_core_cycle[cpu] + 1; // Resume fetch from next cycle.
          assert(ROB.entry[rob_index].stall_begin_cycle != UINT64_MAX);
          calculate_stall_cycle(entry);
        }

#if defined(SHOTGUN_BTB)
        fill_btb(ROB.entry[rob_index].ip, ROB.entry[rob_index].branch_target,
                 ROB.entry[rob_index].branch_type);
#elif defined(SKEWED_BTB) || defined(BASELINEBTB) ||                \
    defined(MICRO_BTB) || defined(FDIPX_BTB) || defined(DIV_CONQ)
        BTB.fill_btb(ROB.entry[rob_index].ip,
                     ROB.entry[rob_index].branch_target,
                     ROB.entry[rob_index].branch_type, IS_BTB_BOTH);
#endif
        BTB.total_miss_latency +=
            (current_core_cycle[cpu] - ROB.entry[rob_index].cycle_enqueued);

        DP(if (warmup_complete[cpu]) {
          cout << "[ROB] " << __func__
               << " instr_id: " << ROB.entry[rob_index].instr_id;
          cout << " branch_mispredicted: "
               << +ROB.entry[rob_index].branch_mispredicted
               << " fetch_stall: " << +fetch_stall;
          cout << " event: " << ROB.entry[rob_index].event_cycle << endl;
        });
      }
    }
  } else {
    if (ROB.entry[rob_index].num_mem_ops == 0) {
      if ((ROB.entry[rob_index].executed == INFLIGHT) &&
          (ROB.entry[rob_index].event_cycle <= current_core_cycle[cpu])) {
        ROB.entry[rob_index].executed = COMPLETED;
        ROB.entry[rob_index].execute_end_cycle = current_core_cycle[cpu];
        inflight_mem_executions--;
        completed_executions++;

        if (ROB.entry[rob_index].reg_RAW_producer)
          reg_RAW_release(rob_index);

        if (ROB.entry[rob_index].branch_mispredicted) {
          if (warmup_complete[cpu]) {
            ooo_model_instr &entry = ROB.entry[rob_index];
            cout << entry.instr_id << " " << entry.stall_begin_cycle << " "
                 << entry.fetch_start_cycle << " " << entry.fetch_end_cycle
                 << " " << entry.decode_start_cycle << " "
                 << entry.decode_end_cycle << " " << entry.execute_start_cycle
                 << " " << entry.execute_end_cycle << endl;

            fetch_resume_cycle = current_core_cycle[cpu] + 1;
          }

          if (ROB.entry[rob_index].branch_taken) {

#if defined(SHOTGUN_BTB)
            fill_btb(ROB.entry[rob_index].ip,
                     ROB.entry[rob_index].branch_target,
                     ROB.entry[rob_index].branch_type);
#elif defined(SKEWED_BTB) || defined(BASELINEBTB) ||                \
    defined(MICRO_BTB) || defined(FDIPX_BTB) || defined(DIV_CONQ)
            BTB.fill_btb(ROB.entry[rob_index].ip,
                         ROB.entry[rob_index].branch_target,
                         ROB.entry[rob_index].branch_type, IS_BTB_BOTH);
#endif
          }
        }

        if (ROB.entry[rob_index].btb_miss &&
            ROB.entry[rob_index].branch_mispredicted == 0) {
          uint8_t branch_type = ROB.entry[rob_index].branch_type;
          assert(branch_type != BRANCH_DIRECT_JUMP &&
                 branch_type != BRANCH_DIRECT_CALL &&
                 branch_type != BRANCH_CONDITIONAL);
          if (warmup_complete[cpu]) {
            ooo_model_instr &entry = ROB.entry[rob_index];
            fetch_resume_cycle =
                current_core_cycle[cpu] + 1; // Resume fetch from next cycle.
            assert(ROB.entry[rob_index].stall_begin_cycle != UINT64_MAX);
            calculate_stall_cycle(entry);
          }

#if defined(SHOTGUN_BTB)
          fill_btb(ROB.entry[rob_index].ip, ROB.entry[rob_index].branch_target,
                   ROB.entry[rob_index].branch_type);
#elif defined(SKEWED_BTB) || defined(BASELINEBTB) ||                \
    defined(MICRO_BTB) || defined(FDIPX_BTB) || defined(DIV_CONQ)
          BTB.fill_btb(ROB.entry[rob_index].ip,
                       ROB.entry[rob_index].branch_target,
                       ROB.entry[rob_index].branch_type, IS_BTB_BOTH);
#endif
          BTB.total_miss_latency +=
              (current_core_cycle[cpu] - ROB.entry[rob_index].cycle_enqueued);
        }

        DP(if (warmup_complete[cpu]) {
          cout << "[ROB] " << __func__
               << " instr_id: " << ROB.entry[rob_index].instr_id;
          cout << " is_memory: " << +ROB.entry[rob_index].is_memory
               << " branch_mispredicted: "
               << +ROB.entry[rob_index].branch_mispredicted;
          cout << " fetch_stall: " << +fetch_stall
               << " event: " << ROB.entry[rob_index].event_cycle
               << " current: " << current_core_cycle[cpu] << endl;
        });
      }
    }
  }
}

void O3_CPU::reg_RAW_release(uint32_t rob_index) {
  // if (!ROB.entry[rob_index].registers_instrs_depend_on_me.empty())

  ITERATE_SET(i, ROB.entry[rob_index].registers_instrs_depend_on_me, ROB_SIZE) {
    for (uint32_t j = 0; j < NUM_INSTR_SOURCES; j++) {
      if (ROB.entry[rob_index].registers_index_depend_on_me[j].search(i)) {
        ROB.entry[i].num_reg_dependent--;

        if (ROB.entry[i].num_reg_dependent == 0) {
          ROB.entry[i].reg_ready = 1;
          if (ROB.entry[i].is_memory)
            ROB.entry[i].scheduled = INFLIGHT;
          else {
            ROB.entry[i].scheduled = COMPLETED;

#ifdef SANITY_CHECK
            if (RTE0[RTE0_tail] < ROB_SIZE)
              assert(0);
#endif
            // remember this rob_index in the Ready-To-Execute array 0
            RTE0[RTE0_tail] = i;

            // DP (if (warmup_complete[cpu]) {
            // cout << "[RTE0] " << __func__ << " instr_id: " <<
            // ROB.entry[i].instr_id << " rob_index: " << i << " is added to
            // RTE0"; cout << " head: " << RTE0_head << " tail: " << RTE0_tail <<
            // endl; });

            RTE0_tail++;
            if (RTE0_tail == ROB_SIZE)
              RTE0_tail = 0;
          }
        }

        // DP (if (warmup_complete[cpu]) {
        // cout << "[ROB] " << __func__ << " instr_id: " <<
        // ROB.entry[rob_index].instr_id << " releases instr_id: "; cout <<
        // ROB.entry[i].instr_id << " reg_index: " <<
        // +ROB.entry[i].source_registers[j] << " num_reg_dependent: " <<
        // ROB.entry[i].num_reg_dependent << " cycle: " <<
        // current_core_cycle[cpu] << endl; });
      }
    }
  }
}

void O3_CPU::operate_cache() {
  if (warmup_complete[0] && current_core_cycle[0] % 10000000 == 0) {
    cout << "Cycle: " << current_core_cycle[0] << " FTQ IN: " << ftq_in
         << " FTQ occupancy: " << ftq_total_occupancy * 1.0 / denom_cycle
         << " DECODE IN: " << decode_in
         << " DECODE occupancy: " << decode_total_occupancy * 1.0 / denom_cycle
         << " ROB IN: " << rob_in
         << " ROB occupancy: " << rob_total_occupancy * 1.0 / denom_cycle
         << " ROB OUT: " << rob_out << endl;
    ftq_in = decode_in = rob_in = rob_out = 0;
    ftq_total_occupancy = decode_total_occupancy = rob_total_occupancy =
        denom_cycle = 0;
  }

  ftq_total_occupancy += FTQ.occupancy;
  decode_total_occupancy += DECODE_BUFFER.occupancy;
  rob_total_occupancy += ROB.occupancy;
  denom_cycle++;

  ITLB.operate();
  DTLB.operate();
  STLB.operate();
#ifdef INS_PAGE_TABLE_WALKER
  PTW.operate();
#endif
  L1I.operate();
  L1D.operate();
  L2C.operate();

  // also handle per-cycle prefetcher operation
  l1i_prefetcher_cycle_operate();
}

void O3_CPU::update_rob() {
  //@Vishal: VIPT ITLB processed entries will be handled by L1I cache.
  // if (ITLB.PROCESSED.occupancy &&
  // (ITLB.PROCESSED.entry[ITLB.PROCESSED.head].event_cycle <=
  // current_core_cycle[cpu]))
  //    complete_instr_fetch(&ITLB.PROCESSED, 1);

  if (L1I.PROCESSED.occupancy &&
      (L1I.PROCESSED.entry[L1I.PROCESSED.head].event_cycle <=
       current_core_cycle[cpu]))
    complete_instr_fetch(&L1I.PROCESSED, 0);

  //@Vishal: VIPT DTLB processed entries will be handled by L1D cache
  // if (DTLB.PROCESSED.occupancy &&
  // (DTLB.PROCESSED.entry[DTLB.PROCESSED.head].event_cycle <=
  // current_core_cycle[cpu]))
  //    complete_data_fetch(&DTLB.PROCESSED, 1);

  if (L1D.PROCESSED.occupancy &&
      (L1D.PROCESSED.entry[L1D.PROCESSED.head].event_cycle <=
       current_core_cycle[cpu]))
    complete_data_fetch(&L1D.PROCESSED, 0);

  // update ROB entries with completed executions
  if ((inflight_reg_executions > 0) || (inflight_mem_executions > 0)) {
    if (ROB.head < ROB.tail) {
      for (uint32_t i = ROB.head; i < ROB.tail; i++)
        complete_execution(i);
    } else {
      for (uint32_t i = ROB.head; i < ROB.SIZE; i++)
        complete_execution(i);
      for (uint32_t i = 0; i < ROB.tail; i++)
        complete_execution(i);
    }
  }
}

void O3_CPU::complete_instr_fetch(PACKET_QUEUE *queue, uint8_t is_it_tlb) {
  //@Vishal: VIPT, TLB request should not be handled here
  assert(is_it_tlb == 0);

  uint32_t index = queue->head, rob_index = queue->entry[index].rob_index,
           num_fetched = 0;

  uint64_t complete_ip = queue->entry[index].ip;

  // Neelu: Marking FTQ entries translated and fetched and then returning.

  for (auto &ftq_it : FTQ.entry) {
    if (ftq_it.size() == 0)
      continue;

    for (auto &ftq_it_entry : ftq_it) {
      if ((ftq_it_entry.ip >> 6) == (complete_ip >> 6) &&
          (ftq_it_entry.fetched == INFLIGHT)) {
        ftq_it_entry.translated = COMPLETED;
        ftq_it_entry.fetched = COMPLETED;
        ftq_it_entry.fetch_end_cycle = current_core_cycle[cpu];
      }
    }
  }

  // remove this entry
  queue->remove_queue(&queue->entry[index]);

  return;

  // old function

  assert(0);

#ifdef SANITY_CHECK
  // DP (if (warmup_complete[cpu]) {
  // cout << "**(complete_instr_fetch)queue->entry[index].full_addr = "<< hex <<
  // queue->entry[index].full_addr << "  instr_id = "<<
  // queue->entry[index].instr_id << " index="<<index<< "
  // rob_index="<<queue->entry[index].rob_index<< endl; });
  int a;
  if (rob_index != (a = check_rob(queue->entry[index].instr_id))) {
    // cout << "complete_instr_fetch, rob_index ="<<rob_index<< " a = " << a <<
    // endl;
    assert(0);
  }
#endif

  // update ROB entry
  if (is_it_tlb) {
    ROB.entry[rob_index].translated = COMPLETED;
    ROB.entry[rob_index].instruction_pa =
        (queue->entry[index].instruction_pa << LOG2_PAGE_SIZE) |
        (ROB.entry[rob_index].ip &
         ((1 << LOG2_PAGE_SIZE) - 1)); // translated address
  } else
    ROB.entry[rob_index].fetched = COMPLETED;
  ROB.entry[rob_index].event_cycle = current_core_cycle[cpu];
  num_fetched++;

  // DP ( if (warmup_complete[cpu]) {
  // cout << "[" << queue->NAME << "] " << __func__ << " cpu: " << cpu <<  "
  // instr_id: " << ROB.entry[rob_index].instr_id; cout << " ip: " << hex <<
  // ROB.entry[rob_index].ip << " address: " <<
  // ROB.entry[rob_index].instruction_pa << dec; cout << " translated: " <<
  // +ROB.entry[rob_index].translated << " fetched: " <<
  // +ROB.entry[rob_index].fetched; cout << " event_cycle: " <<
  // ROB.entry[rob_index].event_cycle << endl; });

  // check if other instructions were merged
  if (queue->entry[index].instr_merged) {
    ITERATE_SET(i, queue->entry[index].rob_index_depend_on_me, ROB_SIZE) {
      // update ROB entry
      if (is_it_tlb) {
        ROB.entry[i].translated = COMPLETED;
        ROB.entry[i].instruction_pa =
            (queue->entry[index].instruction_pa << LOG2_PAGE_SIZE) |
            (ROB.entry[i].ip &
             ((1 << LOG2_PAGE_SIZE) - 1)); // translated address
      } else
        ROB.entry[i].fetched = COMPLETED;
      ROB.entry[i].event_cycle =
          current_core_cycle[cpu] + (num_fetched / FETCH_WIDTH);
      num_fetched++;

      // DP ( if (warmup_complete[cpu]) {
      // cout << "[" << queue->NAME << "] " << __func__ << " cpu: " << cpu <<  "
      // instr_id: " << ROB.entry[i].instr_id; cout << " ip: " << hex <<
      // ROB.entry[i].ip << " address: " << ROB.entry[i].instruction_pa << dec;
      // cout << " translated: " << +ROB.entry[i].translated << " fetched: " <<
      // +ROB.entry[i].fetched << " provider: " << ROB.entry[rob_index].instr_id;
      // cout << " event_cycle: " << ROB.entry[i].event_cycle << endl; });
    }
  }

  // remove this entry
  queue->remove_queue(&queue->entry[index]);
}

void O3_CPU::complete_data_fetch(PACKET_QUEUE *queue, uint8_t is_it_tlb) {

  //@Vishal: VIPT, TLB request should not be handled here
  assert(is_it_tlb == 0);

  uint32_t index = queue->head, rob_index = queue->entry[index].rob_index,
           sq_index = queue->entry[index].sq_index,
           lq_index = queue->entry[index].lq_index;

#ifdef SANITY_CHECK
  if (queue->entry[index].type != RFO) {
    // DP (if (warmup_complete[cpu]) {
    // cout << "queue->entry[index].full_addr = "<<
    // queue->entry[index].full_addr << endl; });
    if (rob_index != check_rob(queue->entry[index].instr_id)) {
      assert(0);
    }
  }
#endif

  // update ROB entry
  if (is_it_tlb) { // DTLB

    if (queue->entry[index].type == RFO) {
      SQ.entry[sq_index].physical_address =
          (queue->entry[index].data_pa << LOG2_PAGE_SIZE) |
          (SQ.entry[sq_index].virtual_address &
           ((1 << LOG2_PAGE_SIZE) - 1)); // translated address
      SQ.entry[sq_index].translated = COMPLETED;
      SQ.entry[sq_index].event_cycle = current_core_cycle[cpu];

      RTS1[RTS1_tail] = sq_index;
      RTS1_tail++;
      if (RTS1_tail == SQ_SIZE)
        RTS1_tail = 0;

      // DP (if (warmup_complete[cpu]) {
      // cout << "[ROB] " << __func__ << " RFO instr_id: " <<
      // SQ.entry[sq_index].instr_id; cout << " DTLB_FETCH_DONE translation: " <<
      // +SQ.entry[sq_index].translated << hex << " page: " <<
      // (SQ.entry[sq_index].physical_address>>LOG2_PAGE_SIZE); cout << "
      // full_addr: " << SQ.entry[sq_index].physical_address << dec << "
      // store_merged: " << +queue->entry[index].store_merged; cout << "
      // load_merged: " << +queue->entry[index].load_merged << endl; });

      handle_merged_translation(&queue->entry[index]);
    } else {
      LQ.entry[lq_index].physical_address =
          (queue->entry[index].data_pa << LOG2_PAGE_SIZE) |
          (LQ.entry[lq_index].virtual_address &
           ((1 << LOG2_PAGE_SIZE) - 1)); // translated address
      LQ.entry[lq_index].translated = COMPLETED;
      LQ.entry[lq_index].event_cycle = current_core_cycle[cpu];

      RTL1[RTL1_tail] = lq_index;
      RTL1_tail++;
      if (RTL1_tail == LQ_SIZE)
        RTL1_tail = 0;

      // DP (if (warmup_complete[cpu]) {
      // cout << "[RTL1] " << __func__ << " instr_id: " <<
      // LQ.entry[lq_index].instr_id << " rob_index: " <<
      // LQ.entry[lq_index].rob_index << " is added to RTL1"; cout << " head: "
      // << RTL1_head << " tail: " << RTL1_tail << endl; });

      // DP (if (warmup_complete[cpu]) {
      // cout << "[ROB] " << __func__ << " load instr_id: " <<
      // LQ.entry[lq_index].instr_id; cout << " DTLB_FETCH_DONE translation: " <<
      // +LQ.entry[lq_index].translated << hex << " page: " <<
      // (LQ.entry[lq_index].physical_address>>LOG2_PAGE_SIZE); cout << "
      // full_addr: " << LQ.entry[lq_index].physical_address << dec << "
      // store_merged: " << +queue->entry[index].store_merged; cout << "
      // load_merged: " << +queue->entry[index].load_merged << endl; });

      handle_merged_translation(&queue->entry[index]);
    }

    ROB.entry[rob_index].event_cycle = queue->entry[index].event_cycle;
  } else { // L1D

    if (queue->entry[index].type == RFO)
      handle_merged_load(&queue->entry[index]);
    else {
#ifdef SANITY_CHECK
      if (queue->entry[index].store_merged)
        assert(0);
#endif
      LQ.entry[lq_index].fetched = COMPLETED;
      LQ.entry[lq_index].event_cycle = current_core_cycle[cpu];
      ROB.entry[rob_index].num_mem_ops--;
      ROB.entry[rob_index].event_cycle = queue->entry[index].event_cycle;

#ifdef SANITY_CHECK
      if (ROB.entry[rob_index].num_mem_ops < 0) {
        cerr << "instr_id: " << ROB.entry[rob_index].instr_id << endl;
        assert(0);
      }
#endif
      if (ROB.entry[rob_index].num_mem_ops == 0)
        inflight_mem_executions++;

      // DP (if (warmup_complete[cpu]) {
      // cout << "[ROB] " << __func__ << " load instr_id: " <<
      // LQ.entry[lq_index].instr_id; cout << " L1D_FETCH_DONE fetched: " <<
      // +LQ.entry[lq_index].fetched << hex << " address: " <<
      // (LQ.entry[lq_index].physical_address>>LOG2_BLOCK_SIZE); cout << "
      // full_addr: " << LQ.entry[lq_index].physical_address << dec << "
      // remain_mem_ops: " << ROB.entry[rob_index].num_mem_ops; cout << "
      // load_merged: " << +queue->entry[index].load_merged << " inflight_mem: "
      // << inflight_mem_executions << endl; });

      release_load_queue(lq_index);
      handle_merged_load(&queue->entry[index]);
    }
  }

  // remove this entry
  queue->remove_queue(&queue->entry[index]);
}

void O3_CPU::handle_merged_translation(PACKET *provider) {

  //@Vishal: VIPT, Translation are not sent from processor, so this code should
  //not be executed.
  assert(0);

  if (provider->store_merged) {
    ITERATE_SET(merged, provider->sq_index_depend_on_me, SQ.SIZE) {
      SQ.entry[merged].translated = COMPLETED;
      SQ.entry[merged].physical_address =
          (provider->data_pa << LOG2_PAGE_SIZE) |
          (SQ.entry[merged].virtual_address &
           ((1 << LOG2_PAGE_SIZE) - 1)); // translated address
      SQ.entry[merged].event_cycle = current_core_cycle[cpu];

      RTS1[RTS1_tail] = merged;
      RTS1_tail++;
      if (RTS1_tail == SQ_SIZE)
        RTS1_tail = 0;

      // DP (if (warmup_complete[cpu]) {
      // cout << "[ROB] " << __func__ << " store instr_id: " <<
      // SQ.entry[merged].instr_id; cout << " DTLB_FETCH_DONE translation: " <<
      // +SQ.entry[merged].translated << hex << " page: " <<
      // (SQ.entry[merged].physical_address>>LOG2_PAGE_SIZE); cout << "
      // full_addr: " << SQ.entry[merged].physical_address << dec << " by
      // instr_id: " << +provider->instr_id << endl; });
    }
  }
  if (provider->load_merged) {
    ITERATE_SET(merged, provider->lq_index_depend_on_me, LQ.SIZE) {
      LQ.entry[merged].translated = COMPLETED;
      LQ.entry[merged].physical_address =
          (provider->data_pa << LOG2_PAGE_SIZE) |
          (LQ.entry[merged].virtual_address &
           ((1 << LOG2_PAGE_SIZE) - 1)); // translated address
      LQ.entry[merged].event_cycle = current_core_cycle[cpu];

      RTL1[RTL1_tail] = merged;
      RTL1_tail++;
      if (RTL1_tail == LQ_SIZE)
        RTL1_tail = 0;

      // DP (if (warmup_complete[cpu]) {
      // cout << "[RTL1] " << __func__ << " instr_id: " <<
      // LQ.entry[merged].instr_id << " rob_index: " <<
      // LQ.entry[merged].rob_index << " is added to RTL1"; cout << " head: " <<
      // RTL1_head << " tail: " << RTL1_tail << endl; });

      // DP (if (warmup_complete[cpu]) {
      // cout << "[ROB] " << __func__ << " load instr_id: " <<
      // LQ.entry[merged].instr_id; cout << " DTLB_FETCH_DONE translation: " <<
      // +LQ.entry[merged].translated << hex << " page: " <<
      // (LQ.entry[merged].physical_address>>LOG2_PAGE_SIZE); cout << "
      // full_addr: " << LQ.entry[merged].physical_address << dec << " by
      // instr_id: " << +provider->instr_id << endl; });
    }
  }
}

void O3_CPU::handle_merged_load(PACKET *provider) {
  ITERATE_SET(merged, provider->lq_index_depend_on_me, LQ.SIZE) {
    uint32_t merged_rob_index = LQ.entry[merged].rob_index;

    LQ.entry[merged].fetched = COMPLETED;
    LQ.entry[merged].event_cycle = current_core_cycle[cpu];

    ROB.entry[merged_rob_index].num_mem_ops--;
    ROB.entry[merged_rob_index].event_cycle = current_core_cycle[cpu];

#ifdef SANITY_CHECK
    if (ROB.entry[merged_rob_index].num_mem_ops < 0) {
      cerr << "instr_id: " << ROB.entry[merged_rob_index].instr_id
           << " rob_index: " << merged_rob_index << endl;
      assert(0);
    }
#endif
    if (ROB.entry[merged_rob_index].num_mem_ops == 0)
      inflight_mem_executions++;

    // DP (if (warmup_complete[cpu]) {
    // cout << "[ROB] " << __func__ << " load instr_id: " <<
    // LQ.entry[merged].instr_id; cout << " L1D_FETCH_DONE translation: " <<
    // +LQ.entry[merged].translated << hex << " address: " <<
    // (LQ.entry[merged].physical_address>>LOG2_BLOCK_SIZE); cout << " full_addr:
    // " << LQ.entry[merged].physical_address << dec << " by instr_id: " <<
    // +provider->instr_id; cout << " remain_mem_ops: " <<
    // ROB.entry[merged_rob_index].num_mem_ops << endl; });

    release_load_queue(merged);
  }
}

void O3_CPU::release_load_queue(uint32_t lq_index) {
  // release LQ entries
  // DP ( if (warmup_complete[cpu]) {
  // cout << "[LQ] " << __func__ << " instr_id: " << LQ.entry[lq_index].instr_id
  // << " releases lq_index: " << lq_index; cout << hex << " full_addr: " <<
  // LQ.entry[lq_index].physical_address << dec << endl; });

  LSQ_ENTRY empty_entry;
  LQ.entry[lq_index] = empty_entry;
  LQ.occupancy--;
}

void O3_CPU::retire_rob() {
  for (uint32_t n = 0; n < RETIRE_WIDTH; n++) {
    if (ROB.entry[ROB.head].ip == 0)
      return;

    // retire is in-order
    if (ROB.entry[ROB.head].executed != COMPLETED) {

// Neelu: Setting stall flag and stall begin cycle if loads are stuck at the
// head of ROB
#ifdef DETECT_CRITICAL_IPS
      if (warmup_complete[cpu]) {
        if (ROB.entry[ROB.head].stall_flag == 0) {
          ROB.entry[ROB.head].stall_flag = 1;
          ROB.entry[ROB.head].stall_begin_cycle = current_core_cycle[cpu];
          rob_stall_count[cpu]++;
          bool load_or_not;
          for (uint32_t i = 0; i < NUM_INSTR_SOURCES; i++) {
            if (ROB.entry[ROB.head].lq_index[i] != UINT32_MAX) {
              load_or_not = true;
              ROB.entry[ROB.head].load_stall_flag = 1;
              load_rob_stall_count[cpu]++;
              break;
            }
          }
          // Only to be done on loads
          // if(load_or_not)
          //{
          //	ROB.entry[ROB.head].load_stall_flag = 1;
          // ROB.entry[ROB.head].stall_begin_cycle = current_core_cycle[cpu];
          //}
        }
      }
#endif
      // DP ( if (warmup_complete[cpu]) {
      // cout << "[ROB] " << __func__ << " instr_id: " <<
      // ROB.entry[ROB.head].instr_id << " head: " << ROB.head << " is not
      // executed yet" << endl; });
      return;
    }

    // check store instruction
    uint32_t num_store = 0;
    for (uint32_t i = 0; i < MAX_INSTR_DESTINATIONS; i++) {
      if (ROB.entry[ROB.head].destination_memory[i])
        num_store++;
    }

    if (num_store) {
      if ((L1D.WQ.occupancy + num_store) <= L1D.WQ.SIZE) {
        for (uint32_t i = 0; i < MAX_INSTR_DESTINATIONS; i++) {
          if (ROB.entry[ROB.head].destination_memory[i]) {

            PACKET data_packet;
            uint32_t sq_index = ROB.entry[ROB.head].sq_index[i];

            // sq_index and rob_index are no longer available after retirement
            // but we pass this information to avoid segmentation fault
            data_packet.fill_level = FILL_L1;
            data_packet.fill_l1d = 1;
            data_packet.cpu = cpu;
            data_packet.data_index = SQ.entry[sq_index].data_index;
            data_packet.sq_index = sq_index;

            //@Vishal: VIPT, send virtual address
            // data_packet.address = SQ.entry[sq_index].physical_address >>
            // LOG2_BLOCK_SIZE; data_packet.full_addr =
            // SQ.entry[sq_index].physical_address;

            data_packet.address =
                SQ.entry[sq_index].virtual_address >> LOG2_BLOCK_SIZE;
            data_packet.full_addr = SQ.entry[sq_index].virtual_address;
            data_packet.full_virtual_address =
                SQ.entry[sq_index].virtual_address;

            data_packet.instr_id = SQ.entry[sq_index].instr_id;
            data_packet.rob_index = SQ.entry[sq_index].rob_index;
            data_packet.ip = SQ.entry[sq_index].ip;
            data_packet.type = RFO;
            data_packet.asid[0] = SQ.entry[sq_index].asid[0];
            data_packet.asid[1] = SQ.entry[sq_index].asid[1];
            data_packet.event_cycle = current_core_cycle[cpu];

            L1D.add_wq(&data_packet);

            sim_store_sent++;
          }
        }
      } else {
        // DP ( if (warmup_complete[cpu]) {
        // cout << "[ROB] " << __func__ << " instr_id: " <<
        // ROB.entry[ROB.head].instr_id << " L1D WQ is full" << endl; });

        L1D.WQ.FULL++;
        L1D.STALL[RFO]++;

        return;
      }
    }

    // release SQ entries
    for (uint32_t i = 0; i < MAX_INSTR_DESTINATIONS; i++) {
      if (ROB.entry[ROB.head].sq_index[i] != UINT32_MAX) {
        uint32_t sq_index = ROB.entry[ROB.head].sq_index[i];

        // DP ( if (warmup_complete[cpu]) {
        // cout << "[SQ] " << __func__ << " instr_id: " <<
        // ROB.entry[ROB.head].instr_id << " releases sq_index: " << sq_index;
        // cout << hex << " address: " <<
        // (SQ.entry[sq_index].physical_address>>LOG2_BLOCK_SIZE); cout << "
        // full_addr: " << SQ.entry[sq_index].physical_address << dec << endl;
        // });

        LSQ_ENTRY empty_entry;
        SQ.entry[sq_index] = empty_entry;

        SQ.occupancy--;
        SQ.head++;
        if (SQ.head == SQ.SIZE)
          SQ.head = 0;
      }
    }

#if defined(SHOTGUN_BTB) && !defined(PERFECT_BTB)
    // monitor retire stream here.
    if (ROB.entry[ROB.head].is_branch &&
        ((ROB.entry[ROB.head].branch_mispredicted &&
          ROB.entry[ROB.head].branch_type == BRANCH_CONDITIONAL) ||
         ROB.entry[ROB.head].branch_type == BRANCH_INDIRECT ||
         ROB.entry[ROB.head].branch_type == BRANCH_INDIRECT_CALL))
      btb_update();
#endif

    // release ROB entry
    // DP ( if (warmup_complete[cpu]) {
    // cout << "[ROB] " << __func__ << " instr_id: " <<
    // ROB.entry[ROB.head].instr_id << " is retired" << endl; });

    ooo_model_instr empty_entry;
    ROB.entry[ROB.head] = empty_entry;

    rob_out++;

    ROB.head++;
    if (ROB.head == ROB.SIZE)
      ROB.head = 0;
    ROB.occupancy--;
    completed_executions--;
    num_retired++;
  }
}
