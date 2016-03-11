#ifndef _NIN_H_
#define _NIN_H_

extern int nin_my_rank;


#if MPI_VERSION == 1 || MPI_VERSION == 2
#define nin_mpi_const 
#else
#define nin_mpi_const const
#endif

#define NIN_DBG(format, ...)		\
  do { \
    fprintf(stderr, "NIN:%d: " format " (%s:%d)\n", nin_my_rank, ## __VA_ARGS__, __FILE__, __LINE__); \
  } while (0)

#define NIN_DBGI(rank, format, ...)	\
  do { \
    if (nin_my_rank == rank) fprintf(stderr, "NIN:%d: " format " (%s:%d)\n", nin_my_rank, ## __VA_ARGS__, __FILE__, __LINE__); \
  } while (0)


void NIN_Init();
double NIN_Wtime();
double NIN_get_time();
int NIN_init_ndrand();
int NIN_init_rand(int seed);
int NIN_get_rand(int max);

#endif

