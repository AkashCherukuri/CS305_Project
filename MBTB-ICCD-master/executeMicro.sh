#Neelu: Adding the list of variables here so you'll only need to modify the following to change anything.

#[branch_pred] [l1i_pref] [l1d_pref] [l2c_pref] [llc_pref] [itlb_pref] [dtlb_pref] [stlb_pref] [btb_repl] [l1i_repl] [l1d_repl] [l2c_repl] [llc_repl] [itlb_repl] [dtlb_repl] [stlb_repl] [num_core] <= binary name consists of.

#Running CVP simulations for 10-20M instructions
numwarmup=50
numsim=50

parallelismcount=16


#branch predictor
branchPredictor=hashed_perceptron
#cache prefetchers

l1ipref=(gangbtb_8k)
l1dpref=ipcp_isca
l2cpref=ipcp_isca
llcpref=no

#tlb prefetchers
tlb_pref=no-no-no

#cache replacement policies in the format: btb-l1i-l1d-l2c-llc
cache_repl=lru-lru-lru-srrip-drrip

#tlb replacement policies in the format: itlb-dtlb-stlb
tlb_repl=lru-lru-lru

numcore=1

tracedir=./micro_traces
traces=$(cat scripts/micro_trace_list.txt)

results_folder=micro_sota

numofpref=${#l1ipref[@]}


#Neelu: Variables ending now. Kindly add new variables above this line.

for((i=0; i<$numofpref; i++))
do
	echo ${l1ipref[i]}
#   ./build.sh ${cache_repl}-${tlb_repl} 1 ${l1ipref[i]}-${l1dpref}-${l2cpref}-${llcpref}-${tlb_pref} || exit
done

tracenum=0
count=0
iterator=0


for((i=0; i<$numofpref; i++))
do 
		for trace in $traces;
		do
			if [ ${count} -lt ${parallelismcount} ]
            then
				./run_champsim.sh $branchPredictor-${l1ipref[i]}-${l1dpref}-${l2cpref}-${llcpref}-${tlb_pref}-$cache_repl-$tlb_repl-1core $numwarmup $numsim $tracedir $trace $results_folder -cvp_trace &
			count=`expr $count + 1`

			else
				./run_champsim.sh $branchPredictor-${l1ipref[i]}-${l1dpref}-${l2cpref}-${llcpref}-${tlb_pref}-$cache_repl-$tlb_repl-1core $numwarmup $numsim $tracedir $trace $results_folder -cvp_trace 
				count=0
			fi
		done
		now=$(date)
		echo "Number of SERVER traces simulated - $count in iteration $iterator at time: $now" 

done

echo "Done with count $count"
