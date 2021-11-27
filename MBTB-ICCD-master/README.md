<p align="center">
  <h1 align="center"> ChampSim </h1>
  <p> ChampSim is a trace-based simulator for a microarchitecture study. You can find more information about ChampSim here (https://github.com/ChampSim/ChampSim). This repository contains extension to ChampSim including a two-level Branch Target Buffer (BTB), a five-level Page Table Walker (PTW) (backed by four MMU Caches) and Virtually Indexed, Physically Tagged (VIPT) level 1 caches. <p>
  <p> This repository contains implementation for state-of-the-art BTB designs including Shotgun, SN4L+Dis+BTB, FDIPX and Skewed BTB. It also contains implementation for our proposal, MicroBTB(MBTB). MBTB is a storage efficient and high performance BTB design. You can find more information about MBTB in the paper.
</p>

# Requirements 

This setup is tested with GCC 7.5 and Ubuntu 18.04. To install GCC 7.5 in Ubuntu 20.04 :-

```
sudo apt install build-essential
sudo apt install g++-7
```

# Compile

Use build.sh script to compile different BTB and L1I prefetcher. Different BTB designs can be enabled using macros in `inc/champsim.h`. Here are the combinations for building different BTB designs :-

|BTB Desgin|Enable Macro|Default Size|L1I Prefetcher|
|----------|------------|------------|--------------|
|Baseline|BASELINE_BTB|8K-entry BTB (4-way)|fdip|
|[Shotgun](https://dl.acm.org/doi/abs/10.1145/3296957.3173178)|SHOTGUN_BTB|4K-entry UBTB (4-way), 2K-entry CBTB (4-way), 2K-entry RIB (4-way)|shotgun|
|[SN4L+Dis+BTB](https://ieeexplore.ieee.org/abstract/document/9138943)|DIV_CONQ|8K-entry BTB (4-way)|div_conq|
|[Skewed BTB](https://dl.acm.org/doi/10.1145/165123.165152)|SKEWED_BTB|8K-entry BTB (4-way)|fdip|
|[FDIP-X](https://arxiv.org/abs/2006.13547)|FDIPX_BTB|8K-entry BTB (4-way)|fdip|
|MicroBTB|MICRO_BTB|4K-entry (4-way) skewed and compressed BTB|fdip|


MicroBTB configurations in `inc/mbtb.h`

|Macro Name|Description|
|----------|-----------|
|MBTB_6K| MicroBTB with 1536 sets and 4 ways|
|MBTB_8K| MicroBTB with 2048 sets and 4 ways|
|TWO_VARIANTS| MicroBTB with two variants|
|FOUR_VARIANTS| MicroBTB with four variants|
|SKEWED| Enable skewed indexing|
|MBTB_PREDECODE| Enable predecoding of cache block which miss icache|

```
$ ./build.sh fdip

$ ./build.sh {L1I_PREFETCHER}
```

# Download server traces

You can download the server traces from here (http://hpca23.cse.tamu.edu/CVP-1/secret_traces/). Download the server trace files : cvp1_secret_srv_part1.tar.gz and cvp1_secret_srv_part2.tar.gz. Use the below script to convert the traces into ChampSim format. 
```
Build:

$ g++ scripts/cvp_champsim_trace_converter.cpp -o convertor

Running:

Usage: ./generate_traces.sh [CVP_TRACE_DIR] [TRACELIST] [OUTPUR_FOLDER]
$ ./generate_traces ./cvp1_secret_srv_part1 cvp1_secret_srv_part1.txt ./champsim_cvp1_secret_srv_part1

${CVP_TRACE_DIR}: Trace directory containing CVP traces (cvp1_secret_srv_part1)
${TRACELIST}: List of traces to be converted (cvp1_secret_srv_part1.txt)
${OUTPUR_FOLDER}:  Output folder containing ChampSim traces (champsim_cvp1_secret_srv_part1)
```

# Run simulation

Execute `run_champsim.sh` with proper input arguments. <br>

```
Usage: ./run_champsim.sh [BINARY] [N_WARM] [N_SIM] [TRACE_DIR] [TRACE] [OUTPUT_DIR] [OPTION]
$ ./run_champsim.sh bimodal-no-no-no-lru-1core 1 10 ./iccd_traces 400.perlbench-41B.champsimtrace.xz ./iccd_sota -cvp_trace

${BINARY}: ChampSim binary compiled by "build_champsim.sh" (bimodal-no-no-lru-1core)
${N_WARM}: number of instructions for warmup (1 million)
${N_SIM}:  number of instructinos for detailed simulation (10 million)
${TRACE_DIR}: Directory containing traces.
${TRACE}: trace name (400.perlbench-41B.champsimtrace.xz)
${OUTPUT_DIR}: Directory containing results file.
${OPTION}: Provide -cvp_trace for running CVP traces.
```

# Replicating the results from the paper

The traces used in evalution can be found in `scripts/iccd_trace_list.txt`. For each trace, the results are captured for 50M instructions after a warmup of 50M instructions. The results for all the state-of-the-art BTB designs can be found in `iccd_sota_50M`. The scripts for generating plots can be found in `iccd_plots`.


