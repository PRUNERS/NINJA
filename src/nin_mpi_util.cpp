#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>

#include "nin_mpi_util.h"
#include "nin_util.h"


using namespace std;

MPI_Status *nin_status_allocate(int incount, MPI_Status *statuses, int *flag)
{
  MPI_Status *new_statuses;
  if (statuses == NULL || statuses == MPI_STATUS_IGNORE) {
    new_statuses = (MPI_Status*)malloc(incount * sizeof(MPI_Status));
    *flag  = 1;
  } else {
    new_statuses = statuses;
    *flag = 0;
  }

  // for (int i = 0; i < incount; i++) {
  //   NIN_DBG(" start  waitall: src: %d, tag: %d count: %d", new_statuses[i].MPI_SOURCE, new_statuses[i].MPI_TAG, incount);
  // }

  for (int i = 0; i < incount; i++) {
    new_statuses[i].MPI_SOURCE = MPI_ANY_SOURCE;
    new_statuses[i].MPI_TAG    = MPI_ANY_TAG;
  }

  // for (int i = 0; i < incount; i++) {
  //   NIN_DBG(" end  waitall: src: %d, tag: %d count: %d", new_statuses[i].MPI_SOURCE, new_statuses[i].MPI_TAG, incount);
  // }
  return new_statuses;
}

void nin_status_free(MPI_Status *statuses)
{
  free(statuses);
  return;
}
