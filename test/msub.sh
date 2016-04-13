#!/bin/sh

nodes=4

min="2:30:00"
#sbatch -ppbatch -d singleton -N$nodes -J ex7 -t $min ./run_sc16_ex7.sh
msub -l nodes=${nodes} -l walltime=${min} -N ex7 ./run_sc16_ex7.sh

min="5:30:00"
#sbatch -ppbatch -d singleton -N$nodes -J ex6 -t $min ./run_sc16_ex6.sh
msub -l nodes=${nodes} -l walltime=${min} -N ex6 ./run_sc16_ex6.sh

min="3:00:00"
#sbatch -ppbatch -d singleton -N$nodes -J ex3 -t $min ./run_sc16_ex3.sh
msub -l nodes=${nodes} -l walltime=${min} -N ex3 ./run_sc16_ex3.sh




