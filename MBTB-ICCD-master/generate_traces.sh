#!/bin/bash

if [ "$#" -lt 3 ]; then
    echo "Illegal number of parameters"
    echo "Usage: ./generate_traces.sh [CVP_TRACE_DIR] [TRACELIST] [OUTPUR_FOLDER]"
    exit 1
fi

TRACE_DIR=${1}

tracelist=${2}

total_workloads=`wc ${tracelist} | awk '{print $1}'`
output_folder=${3}

mkdir -p ${output_folder}

for((i=1;i<=${total_workloads};i++));
do
	trace=`sed -n ''${i}'p' ${tracelist} | awk '{print $1}'`
	echo ${trace}
	./converter ${TRACE_DIR}/${trace} || exit
	trace_name=`echo ${trace} | awk '{print substr($1,1,length($1)-3)}'`
	mv trace.out ${trace_name}.trace || exit
	echo ${trace}
	gzip ${trace_name}.trace || exit
	mv ${trace_name}.trace.gz ${output_folder}/ || exit
	rm ${TRACE_DIR}/${trace}
done
