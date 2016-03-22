#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>

#include "mpi.h"
#include "nin_util.h"

int nin_my_rank;


//void NIN_Init()
void NIN_Init()
{
  MPI_Comm_rank(MPI_COMM_WORLD, &nin_my_rank);
}

double NIN_Wtime()
{
  return MPI_Wtime() * 1e6;
}

double NIN_get_time()
{
  double t;
  struct timeval tv;
  gettimeofday(&tv, NULL);
  t = ((double)(tv.tv_sec) + (double)(tv.tv_usec) * 0.001 * 0.001);
  return t;
}

int NIN_init_ndrand()
{
  srand((int)(NIN_get_time() * 1000000 + nin_my_rank));
  return 0;  
}

int NIN_init_rand(int seed)
{
  srand(seed);
  return 0;
}

int NIN_get_rand(int max)
{
  return rand() % max;
}

