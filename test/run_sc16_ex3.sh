#!/bin/sh

ulimit -c 0
. /usr/local/tools/dotkit/init.sh
use mvapich2-intel-2.1

dir=./sc16/
log_file=${dir}/mrace.log
log_file2=${dir}/mrace2.log
hostn=`hostname`
id=`date "+%Y%m%d-%H%M%S"`-${SLURM_JOB_ID}
prefix=NIN_ex3
set -x
mode=0
msafe=0
loop=10000
num_pattern=2
interleave=1000

samples=100 # 50sec per 1 sample: 100 => 83 min


#SR w/ o Ninja
for i in `seq ${samples}`
do
echo $i
srun --wait=5 -n 64 ./nin_test_matching_race ${mode} ${msafe} ${loop} ${num_pattern} ${interleave} 2> ${log_file}
grep "NIN(test):  0: loop:" ${log_file} | tail -n 1 | cut -d' ' -f5 >> ${dir}/${prefix}_NO_${mode}-${msafe}-${loop}-${num_pattern}-${interleave}.${hostn}-${id}.log
done

#SR w/ Ninja
for i in `seq ${samples}`
do
echo $i

nin_dir="./.ninj_${SLURM_JOB_ID}/"
mkdir ${nin_dir}


LD_PRELOAD=/g/g90/sato5/opt/lib/libninj.so NIN_PATTERN=2 NIN_DIR=${nin_dir} NIN_MODEL_MODE=0 NIN_LOCAL_NOISE=0 srun --wait=5 -n 64 ./nin_test_matching_race ${mode} ${msafe} ${loop} ${num_pattern} ${interleave} 2> ${log_file}
grep "NIN(test):  0: loop:" ${log_file} | tail -n 1 | cut -d' ' -f5 >> ${dir}/${prefix}_N_${mode}-${msafe}-${loop}-${num_pattern}-${interleave}.${hostn}-${id}.log

rm -rf ${nin_dir}
done


mode=1
interleave=0


#SSR w/o Ninja
set -x
for i in `seq ${samples}`
do
echo $i
srun --wait=5 -n 64 ./nin_test_matching_race ${mode} ${msafe} ${loop} ${num_pattern} ${interleave} 2> ${log_file}
grep "NIN(test):  0: loop:" ${log_file} | tail -n 1 | cut -d' ' -f5 >> ${dir}/${prefix}_NO_${mode}-${msafe}-${loop}-${num_pattern}-${interleave}.${hostn}-${id}.log
done

#SSR w/ Ninja
for i in `seq ${samples}`
do
echo $i
nin_dir="./.ninj_${SLURM_JOB_ID}/"
rm -rf ${nin_dir}
mkdir ${nin_dir}

LD_PRELOAD=/g/g90/sato5/opt/lib/libninj.so NIN_PATTERN=2 NIN_DIR=${nin_dir}  NIN_MODEL_MODE=0 NIN_LOCAL_NOISE=0 srun --wait=5 -n 64 ./nin_test_matching_race ${mode} ${msafe} ${loop} ${num_pattern} ${interleave} 2> ${log_file}
grep "NIN(test):  0: loop:" ${log_file} | tail -n 1 | cut -d' ' -f5 >> ${dir}/${prefix}_N_${mode}-${msafe}-${loop}-${num_pattern}-${interleave}.${hostn}-${id}.log

rm -rf ${nin_dir}
done
