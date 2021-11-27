
if [ "$#" -ne 1 ]; then
    echo "Illegal number of parameters"
    echo "Usage: ./build.sh [l1ipref]"
    exit 1
fi

replacment_combination=lru-lru-lru-srrip-drrip-lru-lru-lru
numcpu=1
prefetcher_combination=${1}

echo $1

IFS='-' # hyphen (-) is set as delimiter
read -ra ADDR <<< "${prefetcher_combination}" # string is read into an array as tokens separated by IFS
l1i_prefetcher=${ADDR[0]}
l1d_prefetcher=ipcp_isca
l2c_prefetcher=ipcp_isca
llc_prefetcher=no
itlb_prefetcher=no
dtlb_prefetcher=no
stlb_prefetcher=no

read -ra ADDR <<< "${replacment_combination}" # string is read into an array as tokens separated by IFS
btb_replacement=${ADDR[0]}
l1i_replacement=${ADDR[1]}
l1d_replacement=${ADDR[2]}
l2c_replacement=${ADDR[3]}
llc_replacement=${ADDR[4]}
itlb_replacement=${ADDR[5]}
dtlb_replacement=${ADDR[6]}
stlb_replacement=${ADDR[7]}

echo ./build_champsim.sh hashed_perceptron ${l1i_prefetcher} ${l1d_prefetcher} ${l2c_prefetcher} ${llc_prefetcher} ${itlb_prefetcher} ${dtlb_prefetcher} ${stlb_prefetcher} ${btb_replacement} ${l1i_replacement} ${l1d_replacement} ${l2c_replacement} ${llc_replacement} ${itlb_replacement} ${dtlb_replacement} ${stlb_replacement} ${numcpu}


./build_champsim.sh hashed_perceptron ${l1i_prefetcher} ${l1d_prefetcher} ${l2c_prefetcher} ${llc_prefetcher} ${itlb_prefetcher} ${dtlb_prefetcher} ${stlb_prefetcher} ${btb_replacement} ${l1i_replacement} ${l1d_replacement} ${l2c_replacement} ${llc_replacement} ${itlb_replacement} ${dtlb_replacement} ${stlb_replacement} ${numcpu}

