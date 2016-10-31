#!/bin/sh

nodes=4

msub -l nodes=128   -l walltime=8:00:00 -N p3072 ./test.sh 3072 1340740 10
msub -l nodes=64   -l walltime=4:00:00 -N p1536 ./test.sh 1536  729375 10
msub -l nodes=32   -l walltime=2:00:00 -N p768 ./test.sh 768    385853 10
msub -l nodes=16   -l walltime=2:00:00 -N p384 ./test.sh 384    254504 10
msub -l nodes=8    -l walltime=1:00:00 -N p192 ./test.sh 192    65769 10
msub -l nodes=4    -l walltime=1:00:00 -N p96  ./test.sh 96     27696 10

msub -l nodes=128   -l walltime=8:00:00 -N p3072 ./test.sh 3072 1340740 90
msub -l nodes=64   -l walltime=4:00:00 -N p1536 ./test.sh 1536  729375 90
msub -l nodes=32   -l walltime=2:00:00 -N p768 ./test.sh 768    385853 90
msub -l nodes=16   -l walltime=2:00:00 -N p384 ./test.sh 384    254504 90
msub -l nodes=8    -l walltime=1:00:00 -N p192 ./test.sh 192    65769 90
msub -l nodes=4    -l walltime=1:00:00 -N p96  ./test.sh 96     27696 90


exit

msub -l nodes=4    -l walltime=1:00 -N p96  ./test.sh 96     27696
msub -l nodes=8    -l walltime=1:00 -N p192 ./test.sh 192    65769
msub -l nodes=16   -l walltime=2:00 -N p384 ./test.sh 384    254504
msub -l nodes=32   -l walltime=2:00 -N p768 ./test.sh 768    385853
msub -l nodes=64   -l walltime=4:00 -N p1536 ./test.sh 1536  729375
msub -l nodes=128   -l walltime=4:00 -N p3072 ./test.sh 3072 1340740
exit



exit

min="2:30:00"
#sbatch -ppbatch -d singleton -N$nodes -J ex7 -t $min ./run_sc16_ex7.sh
msub -l nodes=${nodes} -l walltime=${min} -N ex7 ./run_sc16_ex7.sh

min="5:30:00"
#sbatch -ppbatch -d singleton -N$nodes -J ex6 -t $min ./run_sc16_ex6.sh
msub -l nodes=${nodes} -l walltime=${min} -N ex6 ./run_sc16_ex6.sh

min="3:00:00"
#sbatch -ppbatch -d singleton -N$nodes -J ex3 -t $min ./run_sc16_ex3.sh
msub -l nodes=${nodes} -l walltime=${min} -N ex3 ./run_sc16_ex3.sh




