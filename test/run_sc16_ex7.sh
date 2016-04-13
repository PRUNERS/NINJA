#!/bin/sh

#samples=1 => 7min

ulimit -c 0
. /usr/local/tools/dotkit/init.sh
use mvapich2-intel-2.1

dir=./sc16/
log_file=${dir}/mrace.log
log_file2=${dir}/mrace2.log
hostn=`hostname`
prefix=NIN_ex7
procs=64
set -x
mode=1
msafe=0
loop=1000
num_pattern=2
id=`date "+%Y%m%d-%H%M%S"`-${SLURM_JOB_ID}


#CHECK
samples=100 # 1m 10 sec  per 1 sample: 100 => 116min

for interleave in 10000
do
    for i in `seq ${samples}`
    do
	echo "W/O" $i $local_delay
	time srun --wait=5 -n ${procs} ./nin_test_matching_race ${mode} ${msafe} ${loop} ${num_pattern} ${interleave} 2> ${log_file}
	grep "NIN(test):  0: loop:" ${log_file} | tail -n 1 | cut -d' ' -f5 >> ${dir}/${prefix}_NO_${local_delay}_${mode}-${msafe}-${loop}-${num_pattern}-${interleave}.${hostn}-${id}.log
    done


    for i in `seq ${samples}`
    do
	nin_dir="./.ninj_${SLURM_JOB_ID}/"
        rm -rf ${nin_dir}
        mkdir ${nin_dir}

	echo "W/" $i $local_delay
	time LD_PRELOAD=/g/g90/sato5/opt/lib/libninj.so NIN_PATTERN=2 NIN_DIR=${nin_dir} NIN_MODEL_MODE=0 NIN_LOCAL_NOISE=0 srun --wait=5 -n ${procs} ./nin_test_matching_race ${mode} ${msafe} ${loop} ${num_pattern} ${interleave} 2> ${log_file}
	grep "NIN(test):  0: loop:" ${log_file} | tail -n 1 | cut -d' ' -f5 >> ${dir}/${prefix}_N_${local_delay}_${mode}-${msafe}-${loop}-${num_pattern}-${interleave}.${hostn}-${id}.log

	time LD_PRELOAD=/g/g90/sato5/opt/lib/libninj.so NIN_PATTERN=2 NIN_DIR=${nin_dir} NIN_MODEL_MODE=1 NIN_LOCAL_NOISE=0 srun --wait=5 -n ${procs} ./nin_test_matching_race ${mode} ${msafe} ${loop} ${num_pattern} ${interleave} 2> ${log_file}
	grep "NIN(test):  0: loop:" ${log_file} | tail -n 1 | cut -d' ' -f5 >> ${dir}/${prefix}_NA_${local_delay}_${mode}-${msafe}-${loop}-${num_pattern}-${interleave}.${hostn}-${id}.log

        rm -rf ${nin_dir}
    done
done





