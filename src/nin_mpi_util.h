#ifndef __NIN_STATUS_H__
#define __NIN_STATUS_H__

#include <mpi.h>

MPI_Status *nin_status_allocate(int incount, MPI_Status *statuses, int *flag);
void nin_status_free(MPI_Status *statuses);

#endif
