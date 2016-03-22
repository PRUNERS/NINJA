
#ifndef __NINJ_THREAD_H__
#define __NINJ_THREAD_H__

#include <pthread.h>

#include "mpi.h"
#include "nin_spsc_queue.h"
#include "nin_util.h"



#if 1
#define PMPI_WRAP(func, name) \
  do { \
  pthread_mutex_lock(&ninj_thread_mpi_mutex);  \
  func; \
  pthread_mutex_unlock(&ninj_thread_mpi_mutex); \
  } while(0)
#else
#define PMPI_WRAP(func, name) \
  do { \
  func; \
  } while(0)
#endif


typedef struct {
  void* send_buff;
  int dest;
  int tag;
  int is_started;
  int is_buffered;
  MPI_Request request;
  double send_time;
  int is_final;
} nin_delayed_send_request;

extern pthread_mutex_t ninj_thread_mpi_mutex;
extern nin_spsc_queue<nin_delayed_send_request*> nin_thread_input, nin_thread_output;
extern int debug_int;


void* run_delayed_send(void* args);



#endif
