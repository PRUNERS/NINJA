#!/bin/sh

ulimit -c 0
. /usr/local/tools/dotkit/init.sh
use mvapich2-intel-2.1

dir=./sc16/
log_file=${dir}/mrace.log
log_file2=${dir}/mrace2.log
hostn=`hostname`
prefix=NIN_ex6
procs=64

set -x

mode=1
msafe=0
loop=1000
num_pattern=2
interleave=0
id=`date "+%Y%m%d-%H%M%S"`-${SLURM_JOB_ID}

#CHECK
samples=100 # 2 min 40 sec  per 1 sample: 100 => 266 min

for local_delay in 1 10 100
do
    for i in `seq ${samples}`
    do
	echo "W/O" $i $local_delay
	LD_PRELOAD=/g/g90/sato5/opt/lib/libninj.so NIN_PATTERN=0 NIN_LOCAL_NOISE=1 NIN_LOCAL_NOISE_AMOUNT=${local_delay} srun --wait=5 -n ${procs} ./nin_test_matching_race ${mode} ${msafe} ${loop} ${num_pattern} ${interleave} 2> ${log_file}
	grep "NIN(test):  0: loop:" ${log_file} | tail -n 1 | cut -d' ' -f5 >> ${dir}/${prefix}_NO_${local_delay}_${mode}-${msafe}-${loop}-${num_pattern}-${interleave}.${hostn}-${id}.log
    done


    for i in `seq ${samples}`
    do
	nin_dir="./.ninj_${SLURM_JOB_ID}/"
	rm -rf ${nin_dir}

        mkdir ${nin_dir}


	echo "W/" $i $local_delay
	time LD_PRELOAD=/g/g90/sato5/opt/lib/libninj.so NIN_PATTERN=2 NIN_DIR=${nin_dir} NIN_MODEL_MODE=0 NIN_LOCAL_NOISE=1 NIN_LOCAL_NOISE_AMOUNT=${local_delay} srun --wait=5 -n ${procs} ./nin_test_matching_race ${mode} ${msafe} ${loop} ${num_pattern} ${interleave} 2> ${log_file}
	grep "NIN(test):  0: loop:" ${log_file} | tail -n 1 | cut -d' ' -f5 >> ${dir}/${prefix}_N_${local_delay}_${mode}-${msafe}-${loop}-${num_pattern}-${interleave}.${hostn}-${id}.log

	time LD_PRELOAD=/g/g90/sato5/opt/lib/libninj.so NIN_PATTERN=2 NIN_DIR=${nin_dir} NIN_MODEL_MODE=1 NIN_LOCAL_NOISE=1 NIN_LOCAL_NOISE_AMOUNT=${local_delay} srun --wait=5 -n ${procs} ./nin_test_matching_race ${mode} ${msafe} ${loop} ${num_pattern} ${interleave} 2> ${log_file}
	grep "NIN(test):  0: loop:" ${log_file} | tail -n 1 | cut -d' ' -f5 >> ${dir}/${prefix}_NA_${local_delay}_${mode}-${msafe}-${loop}-${num_pattern}-${interleave}.${hostn}-${id}.log

	 rm -rf ${nin_dir}
    done
done





