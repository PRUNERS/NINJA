#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <pthread.h>

#include <map>
#include <unordered_map>

#include "ninj_thread.h"
#include "ninj_fc.h"
#include "nin_util.h"
#include "nin_mpi_util.h"

#ifndef _EXTERN_C_
#ifdef __cplusplus
#define _EXTERN_C_ extern "C"
#else /* __cplusplus */
#define _EXTERN_C_
#endif /* __cplusplus */
#endif /* _EXTERN_C_ */




#if (defined(PIC) || defined(__PIC__))
/* For shared libraries, declare these weak and figure out which one was linked
   based on which init wrapper was called.  See mpi_init wrappers.  */
#pragma weak pmpi_init
#pragma weak PMPI_INIT
#pragma weak pmpi_init_
#pragma weak pmpi_init__
#pragma weak pmpi_init_thread
#pragma weak PMPI_INIT_THREAD
#pragma weak pmpi_init_thread_
#pragma weak pmpi_init_thread__
#endif /* PIC */

_EXTERN_C_ void pmpi_init(MPI_Fint *ierr);
_EXTERN_C_ void PMPI_INIT(MPI_Fint *ierr);
_EXTERN_C_ void pmpi_init_(MPI_Fint *ierr);
_EXTERN_C_ void pmpi_init__(MPI_Fint *ierr);
_EXTERN_C_ void pmpi_init_thread(MPI_Fint *required, MPI_Fint *provided, MPI_Fint *ierr);
_EXTERN_C_ void PMPI_INIT_THREAD(MPI_Fint *required, MPI_Fint *provided, MPI_Fint *ierr);
_EXTERN_C_ void pmpi_init_thread_(MPI_Fint *required, MPI_Fint *provided, MPI_Fint *ierr);
_EXTERN_C_ void pmpi_init_thread__(MPI_Fint *required, MPI_Fint *provided, MPI_Fint *ierr);


#define NIN_EAGER_LIMIT (64000)

#define NIN_REQUEST_NOT_STARTED (0)
#define NIN_REQUEST_STARTED     (1)


using namespace std;


pthread_t nin_nosie_thread;
static unordered_map<MPI_Request, nin_delayed_send_request*> pending_send_request_to_ds_request_umap;
static unordered_map<MPI_Request, nin_delayed_send_request*> completed_send_request_to_ds_request_umap;
int counter=0;


static void nin_init(int *argc, char ***argv)
{
  int ret;
  NIN_Init();
  signal(SIGSEGV, SIG_DFL);
  ret = pthread_create(&nin_nosie_thread, NULL, run_delayed_send, NULL);
  NIN_init_ndrand();
  ninj_fc_init();


  return;
}

static void nin_send_request_completed(int count, MPI_Request *requests)
{
  for (int i = 0; i < count; i++) {
    nin_delayed_send_request *ds_req;
    if (pending_send_request_to_ds_request_umap.find(requests[i])
	== pending_send_request_to_ds_request_umap.end()) continue;

    ds_req = pending_send_request_to_ds_request_umap.at(requests[i]);
    if (ds_req->is_buffered) {
      free(ds_req->send_buff);
    }
    free(ds_req);
    if(!pending_send_request_to_ds_request_umap.erase(requests[i])) {
      NIN_DBG("Request: %p is not registered", requests[i]);
    }
    PMPI_WRAP(PMPI_Request_free(&requests[i]), __func__);    
  }
  return;
}


static int nin_is_started(MPI_Request *send_request)
{
  nin_delayed_send_request *ds_req;
  if (pending_send_request_to_ds_request_umap.find(*send_request) ==
      pending_send_request_to_ds_request_umap.end()) {
    /*If this send request is not delayed*/
    return NIN_REQUEST_STARTED;
  }
  /*If this send request is delayed, check is_started flag*/
  ds_req = pending_send_request_to_ds_request_umap.at(*send_request);
  return (ds_req->is_started)? NIN_REQUEST_STARTED:NIN_REQUEST_NOT_STARTED;

  // while((ds_req = nin_thread_output.dequeue()) != NULL) {
  //   pending_send_request_to_ds_request_umap.erase(ds_req->request);

  //   if (ds_req->is_buffered) {
  //     free(ds_req->send_buff);
  //   }
  //   free(ds_req);
  // }

  // if (pending_send_request_to_ds_request_umap.find(*send_request) 
  //     != pending_send_request_to_ds_request_umap.end()) {
  //   return 0;
  // }
  // return 1;
}

static int nin_is_all_started(int count, MPI_Request *requests)
{
  int is_started;
  for (int i = 0; i < count; i++) {
    is_started = nin_is_started(&requests[i]);
    if (is_started == NIN_REQUEST_NOT_STARTED) {
      return NIN_REQUEST_NOT_STARTED;
    }
  }
  return NIN_REQUEST_STARTED;
}

static size_t ninj_get_delay(int count, MPI_Datatype datatype, int src, int *delay_flag, double *send_time)
{
  size_t msg_size;
  int dt_size;
  PMPI_WRAP(PMPI_Type_size(datatype, &dt_size), "MPI_Type_size");
  msg_size = dt_size * count;
  ninj_fc_report_send((size_t)msg_size);
  ninj_fc_get_delay(src, delay_flag, send_time);
  return msg_size;
}

static void ninj_post_delayed_start(void* send_buffer, int is_buffered, int dest, int tag, MPI_Request *request, double send_time)
{
  nin_delayed_send_request *ds_req;

  ds_req = (nin_delayed_send_request*)malloc(sizeof(nin_delayed_send_request));
  ds_req->send_buff = send_buffer;
  ds_req->is_buffered = is_buffered;
  ds_req->dest = dest;
  ds_req->tag = tag;
  ds_req->is_started = 0;
  ds_req->request = *request;
  ds_req->send_time = send_time;
  ds_req->is_final = 0;

  pending_send_request_to_ds_request_umap[*request] = ds_req;
  nin_thread_input.enqueue(ds_req);
  return;
}

static void ninj_get_send_buffer(size_t msg_size, nin_mpi_const void* old_send_buffer, void** new_send_buffer, int *is_buffered)
{
  if (msg_size <= NIN_EAGER_LIMIT) {
    *new_send_buffer = malloc(msg_size);
    if (*new_send_buffer == NULL) {
      NIN_DBG("malloc failed");
      exit(1);
    }
    memcpy(*new_send_buffer, old_send_buffer, msg_size);
    *is_buffered = 1;
  } else {
    *new_send_buffer = (void*)old_send_buffer;
    *is_buffered = 0;
  }
  return;
}


/* ================== C Wrappers for MPI_Irsend ================== */
_EXTERN_C_ int PMPI_Irsend(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6);
_EXTERN_C_ int MPI_Irsend(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6)
{ 
    int _wrap_py_return_val = 0;
    size_t msg_size;
    int delay_flag;
    double send_time;
    void* send_buffer = NULL;
    int is_buffered;

    msg_size =  ninj_get_delay(arg_1, arg_2, arg_3, &delay_flag, &send_time);
    if (!delay_flag) {
      PMPI_WRAP(_wrap_py_return_val = PMPI_Irsend(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6), __func__);
      return _wrap_py_return_val;
    }
    ninj_get_send_buffer(msg_size, arg_0, &send_buffer, &is_buffered);
    PMPI_WRAP(_wrap_py_return_val = PMPI_Rsend_init(send_buffer, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6), "MPI_Send_init");
    ninj_post_delayed_start(send_buffer, is_buffered, arg_3, arg_4, arg_6, send_time);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Isend ================== */
_EXTERN_C_ int PMPI_Isend(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6);
_EXTERN_C_ int MPI_Isend(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6) { 
    int _wrap_py_return_val = 0;
    size_t msg_size;
    int delay_flag;
    double send_time;
    void* send_buffer = NULL;
    int is_buffered;
    msg_size =  ninj_get_delay(arg_1, arg_2, arg_3, &delay_flag, &send_time);
    if (!delay_flag) {
      PMPI_WRAP(_wrap_py_return_val = PMPI_Isend(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6), __func__);
      return _wrap_py_return_val;
    }
    ninj_get_send_buffer(msg_size, arg_0, &send_buffer, &is_buffered);
    PMPI_WRAP(_wrap_py_return_val = PMPI_Send_init(send_buffer, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6), "MPI_Send_init");
    ninj_post_delayed_start(send_buffer, is_buffered, arg_3, arg_4, arg_6, send_time);
    return _wrap_py_return_val;
}


#if 0
/* ================== C Wrappers for MPI_Isend ================== */
_EXTERN_C_ int PMPI_Isend(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6);
_EXTERN_C_ int MPI_Isend(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6) { 
    int _wrap_py_return_val = 0;
    int msg_size;
    nin_delayed_send_request *ds_req;
    int delay_flag;
    double send_time;

    PMPI_WRAP(PMPI_Type_size(arg_2, &msg_size), "MPI_Type_size");
    msg_size = msg_size * arg_1;
    ninj_fc_report_send((size_t)msg_size);
    ninj_fc_get_delay(arg_3, &delay_flag, &send_time);
    if (!delay_flag) {
      /*TODO: Avoid this message to rank X from overtaking the past messages to rank X */
      PMPI_WRAP(_wrap_py_return_val = PMPI_Isend(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6), __func__);
      return _wrap_py_return_val;
    }

    ds_req = (nin_delayed_send_request*)malloc(sizeof(nin_delayed_send_request));

    if (msg_size <= NIN_EAGER_LIMIT) {
      ds_req->send_buff = malloc(msg_size);
      if (ds_req->send_buff == NULL) {
	NIN_DBG("malloc failed");
	exit(1);
      }
      ds_req->is_buffered = 1;
      /*TODO: copy from arg_0 to send_buff*/
    } else {
      ds_req->send_buff = (void*)arg_0;
      ds_req->is_buffered = 0;
    }
    ds_req->dest = arg_3;
    ds_req->tag = arg_4;

    PMPI_WRAP(_wrap_py_return_val = PMPI_Send_init(ds_req->send_buff, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6), "MPI_Send_init");
    
    ds_req->is_started = 0;
    ds_req->request = *arg_6;
    ds_req->send_time = send_time;
    ds_req->is_final = 0;
    pending_send_request_to_ds_request_umap[*arg_6] = ds_req;
    nin_thread_input.enqueue(ds_req);
    
    //    NIN_DBG("enqueu: %f: dest: %d: req: %p", ds_req->send_time, arg_3, *arg_6);
    //    PMPI_WRAP(_wrap_py_return_val = PMPI_Start(arg_6), "MPI_Start");
    return _wrap_py_return_val;
}
#endif

/* ================== C Wrappers for MPI_Issend ================== */
_EXTERN_C_ int PMPI_Issend(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6);
_EXTERN_C_ int MPI_Issend(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6) { 
    int _wrap_py_return_val = 0;
    size_t msg_size;
    int delay_flag;
    double send_time;
    void* send_buffer = NULL;
    int is_buffered;

    msg_size =  ninj_get_delay(arg_1, arg_2, arg_3, &delay_flag, &send_time);
    if (!delay_flag) {
      PMPI_WRAP(_wrap_py_return_val = PMPI_Issend(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6), __func__);
      return _wrap_py_return_val;
    }
    ninj_get_send_buffer(msg_size, arg_0, &send_buffer, &is_buffered);
    PMPI_WRAP(_wrap_py_return_val = PMPI_Ssend_init(send_buffer, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6), "MPI_Send_init");
    ninj_post_delayed_start(send_buffer, is_buffered, arg_3, arg_4, arg_6, send_time);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Ibsend ================== */
_EXTERN_C_ int PMPI_Ibsend(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6);
_EXTERN_C_ int MPI_Ibsend(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6) { 
    int _wrap_py_return_val = 0;
    size_t msg_size;
    int delay_flag;
    double send_time;
    void* send_buffer = NULL;
    int is_buffered;

    msg_size =  ninj_get_delay(arg_1, arg_2, arg_3, &delay_flag, &send_time);
    if (!delay_flag) {
      PMPI_WRAP(_wrap_py_return_val = PMPI_Ibsend(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6), __func__);
      return _wrap_py_return_val;
    }
    ninj_get_send_buffer(msg_size, arg_0, &send_buffer, &is_buffered);
    PMPI_WRAP(_wrap_py_return_val = PMPI_Bsend_init(send_buffer, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6), "MPI_Send_init");
    ninj_post_delayed_start(send_buffer, is_buffered, arg_3, arg_4, arg_6, send_time);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Bsend ================== */
_EXTERN_C_ int PMPI_Bsend(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5);
_EXTERN_C_ int MPI_Bsend(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5) { 
    int _wrap_py_return_val = 0;
    MPI_Request req;
    MPI_Ibsend(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, &req);
    MPI_Wait(&req, MPI_STATUS_IGNORE);
    //    PMPI_WRAP(_wrap_py_return_val = PMPI_Bsend(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5), __func__);
    return _wrap_py_return_val;
}


/* ================== C Wrappers for MPI_Rsend ================== */
_EXTERN_C_ int PMPI_Rsend(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5);
_EXTERN_C_ int MPI_Rsend(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5) { 
    int _wrap_py_return_val = 0;
    MPI_Request req;
    MPI_Irsend(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, &req);
    MPI_Wait(&req, MPI_STATUS_IGNORE);
    //    PMPI_WRAP(_wrap_py_return_val = PMPI_Rsend(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5), __func__);
    return _wrap_py_return_val;
}


/* ================== C Wrappers for MPI_Send ================== */
_EXTERN_C_ int PMPI_Send(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5);
_EXTERN_C_ int MPI_Send(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5) { 
    int _wrap_py_return_val = 0;
    MPI_Request req;
    MPI_Isend(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, &req);
    MPI_Wait(&req, MPI_STATUS_IGNORE);
    //    PMPI_WRAP(_wrap_py_return_val = PMPI_Send(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Ssend ================== */
_EXTERN_C_ int PMPI_Ssend(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5);
_EXTERN_C_ int MPI_Ssend(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5) { 
    int _wrap_py_return_val = 0;
    MPI_Request req;
    MPI_Issend(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, &req);
    MPI_Wait(&req, MPI_STATUS_IGNORE);
    //    PMPI_WRAP(_wrap_py_return_val = PMPI_Ssend(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5), __func__);
    return _wrap_py_return_val;
}



/* ================== C Wrappers for MPI_Bsend_init ================== */
_EXTERN_C_ int PMPI_Bsend_init(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6);
_EXTERN_C_ int MPI_Bsend_init(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Bsend_init(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Rsend_init ================== */
_EXTERN_C_ int PMPI_Rsend_init(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6);
_EXTERN_C_ int MPI_Rsend_init(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Rsend_init(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6), __func__);
    return _wrap_py_return_val;
}
/* ================== C Wrappers for MPI_Send_init ================== */
_EXTERN_C_ int PMPI_Send_init(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6);
_EXTERN_C_ int MPI_Send_init(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Send_init(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Ssend_init ================== */
_EXTERN_C_ int PMPI_Ssend_init(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6);
_EXTERN_C_ int MPI_Ssend_init(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Ssend_init(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6), __func__);
    return _wrap_py_return_val;
}





/* ================== C Wrappers for MPI_Sendrecv ================== */
_EXTERN_C_ int PMPI_Sendrecv(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, void *arg_5, int arg_6, MPI_Datatype arg_7, int arg_8, int arg_9, MPI_Comm arg_10, MPI_Status *arg_11);
_EXTERN_C_ int MPI_Sendrecv(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, void *arg_5, int arg_6, MPI_Datatype arg_7, int arg_8, int arg_9, MPI_Comm arg_10, MPI_Status *arg_11) { 
    int _wrap_py_return_val = 0;
    MPI_Request send_request, recv_request;;
    MPI_Isend(arg_0, arg_1, arg_2, arg_3, arg_4, arg_10, &send_request);
    MPI_Irecv(arg_5, arg_6, arg_7, arg_8, arg_9, arg_10, &recv_request);
    MPI_Wait(&send_request, MPI_STATUS_IGNORE);
    MPI_Wait(&recv_request, arg_11);
    //    PMPI_WRAP(_wrap_py_return_val = PMPI_Sendrecv(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8, arg_9, arg_10, arg_11), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Sendrecv_replace ================== */
_EXTERN_C_ int PMPI_Sendrecv_replace(void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, int arg_5, int arg_6, MPI_Comm arg_7, MPI_Status *arg_8);
_EXTERN_C_ int MPI_Sendrecv_replace(void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, int arg_5, int arg_6, MPI_Comm arg_7, MPI_Status *arg_8) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Sendrecv_replace(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8), __func__);
    return _wrap_py_return_val;
}



/* ================== C Wrappers for MPI_Start ================== */
_EXTERN_C_ int PMPI_Start(MPI_Request *arg_0);
_EXTERN_C_ int MPI_Start(MPI_Request *arg_0) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Start(arg_0), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Startall ================== */
_EXTERN_C_ int PMPI_Startall(int arg_0, MPI_Request *arg_1);
_EXTERN_C_ int MPI_Startall(int arg_0, MPI_Request *arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Startall(arg_0, arg_1), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Test_cancelled ================== */
_EXTERN_C_ int PMPI_Test_cancelled(nin_mpi_const MPI_Status *arg_0, int *arg_1);
_EXTERN_C_ int MPI_Test_cancelled(nin_mpi_const MPI_Status *arg_0, int *arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Test_cancelled(arg_0, arg_1), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Test ================== */
_EXTERN_C_ int PMPI_Test(MPI_Request *arg_0, int *arg_1, MPI_Status *arg_2);
_EXTERN_C_ int MPI_Test(MPI_Request *arg_0, int *arg_1, MPI_Status *arg_2) { 
    int _wrap_py_return_val = 0;
    int is_started = nin_is_started(arg_0);
    if (is_started == NIN_REQUEST_NOT_STARTED) {
      *arg_1 = 0;
      return MPI_SUCCESS;
    }
    PMPI_WRAP(_wrap_py_return_val = PMPI_Test(arg_0, arg_1, arg_2), __func__);
    if (is_started == NIN_REQUEST_STARTED && *arg_1 == 1) {
      /* TODO: If this request came 
	           from Isend,     then *arg_0 = MPI_REQUEST_NULL
	           from send_init, then do nothing*/
      nin_send_request_completed(1, arg_0);
    }
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Wait ================== */
_EXTERN_C_ int PMPI_Wait(MPI_Request *arg_0, MPI_Status *arg_1);
_EXTERN_C_ int MPI_Wait(MPI_Request *arg_0, MPI_Status *arg_1) { 
    int _wrap_py_return_val = 0;
    int flag = 0;
    while(!flag) {
      _wrap_py_return_val = MPI_Test(arg_0, &flag, arg_1);
    }
    //PMPI_WRAP(_wrap_py_return_val = PMPI_Request_free(arg_0), __func__);
    return _wrap_py_return_val;
}


/* ================== C Wrappers for MPI_Testall ================== */
_EXTERN_C_ int PMPI_Testall(int arg_0, MPI_Request *arg_1, int *arg_2, MPI_Status *arg_3);
_EXTERN_C_ int MPI_Testall(int arg_0, MPI_Request *arg_1, int *arg_2, MPI_Status *arg_3) { 
    int _wrap_py_return_val = 0;
    int is_started = nin_is_all_started(arg_0, arg_1);
    if (is_started == NIN_REQUEST_NOT_STARTED) {
      *arg_2 = 0;
      return MPI_SUCCESS;
    }
    PMPI_WRAP(_wrap_py_return_val = PMPI_Testall(arg_0, arg_1, arg_2, arg_3), __func__);
    if (is_started == NIN_REQUEST_STARTED && *arg_2 == 1) {
      nin_send_request_completed(arg_0, arg_1);
    }
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Waitall ================== */
_EXTERN_C_ int PMPI_Waitall(int arg_0, MPI_Request *arg_1, MPI_Status *arg_2);
_EXTERN_C_ int MPI_Waitall(int arg_0, MPI_Request *arg_1, MPI_Status *arg_2) { 
    int _wrap_py_return_val = 0;
    int flag = 0;
    while (!flag) {
      _wrap_py_return_val = MPI_Testall(arg_0, arg_1, &flag, arg_2);
    }
    //    PMPI_WRAP(_wrap_py_return_val = PMPI_Waitall(arg_0, arg_1, arg_2), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Testany ================== */
_EXTERN_C_ int PMPI_Testany(int arg_0, MPI_Request *arg_1, int *arg_2, int *arg_3, MPI_Status *arg_4);
_EXTERN_C_ int MPI_Testany(int arg_0, MPI_Request *arg_1, int *arg_2, int *arg_3, MPI_Status *arg_4) { 
    int _wrap_py_return_val = 0;
    int flag  = 0;
    MPI_Status stat;
    *arg_3 = 0;
    for (int i = 0; i < arg_0; i++) {
      MPI_Test(&arg_1[i], &flag, &stat);
      if (flag) {
	*arg_2 = i;
	*arg_3 = 1;
	if (arg_4 != MPI_STATUS_IGNORE) *arg_4 = stat;
	break;
      }
    }
    //    PMPI_WRAP(_wrap_py_return_val = PMPI_Testany(arg_0, arg_1, arg_2, arg_3, arg_4), __func__);
    return _wrap_py_return_val;
}


/* ================== C Wrappers for MPI_Waitany ================== */
_EXTERN_C_ int PMPI_Waitany(int arg_0, MPI_Request *arg_1, int *arg_2, MPI_Status *arg_3);
_EXTERN_C_ int MPI_Waitany(int arg_0, MPI_Request *arg_1, int *arg_2, MPI_Status *arg_3) { 
    int _wrap_py_return_val = 0;
    int flag = 0;
    while(!flag) {
      MPI_Testany(arg_0, arg_1, arg_2, &flag, arg_3);
    }
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Testsome ================== */
_EXTERN_C_ int PMPI_Testsome(int arg_0, MPI_Request *arg_1, int *arg_2, int *arg_3, MPI_Status *arg_4);
_EXTERN_C_ int MPI_Testsome(int arg_0, MPI_Request *arg_1, int *arg_2, int *arg_3, MPI_Status *arg_4)
{ 
    int _wrap_py_return_val = 0;
    int flag;
    MPI_Status stat;
    int index = 0;
    for (int i = 0; i < arg_0; i++) {
      MPI_Test(&arg_1[i], &flag, &stat);
      if (flag) {
	arg_3[index] = i;
	if (arg_4 != MPI_STATUS_IGNORE) arg_4[i] = stat;
	index++;
      }
    }
    *arg_2 = index;
    //    PMPI_WRAP(_wrap_py_return_val = PMPI_Testsome(arg_0, arg_1, arg_2, arg_3, arg_4), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Waitsome ================== */
_EXTERN_C_ int PMPI_Waitsome(int arg_0, MPI_Request *arg_1, int *arg_2, int *arg_3, MPI_Status *arg_4);
_EXTERN_C_ int MPI_Waitsome(int arg_0, MPI_Request *arg_1, int *arg_2, int *arg_3, MPI_Status *arg_4) { 
    int _wrap_py_return_val = 0;
    *arg_2 = 0;
    while(1) {
      MPI_Testsome(arg_0, arg_1, arg_2, arg_3, arg_4);
      if (*arg_2 > 0) break;
    }
    //    PMPI_WRAP(_wrap_py_return_val = PMPI_Waitsome(arg_0, arg_1, arg_2, arg_3, arg_4), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Probe ================== */
_EXTERN_C_ int PMPI_Probe(int arg_0, int arg_1, MPI_Comm arg_2, MPI_Status *arg_3);
_EXTERN_C_ int MPI_Probe(int arg_0, int arg_1, MPI_Comm arg_2, MPI_Status *arg_3) { 
    int _wrap_py_return_val = 0;
    int flag = 0;
    //    NIN_DBG("=== %s called: %d ===", __func__, counter1);
    while(!flag) {
      _wrap_py_return_val = MPI_Iprobe(arg_0, arg_1, arg_2, &flag, arg_3);
    }
    //    PMPI_WRAP(_wrap_py_return_val = PMPI_Probe(arg_0, arg_1, arg_2, arg_3), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Iprobe ================== */
_EXTERN_C_ int PMPI_Iprobe(int arg_0, int arg_1, MPI_Comm arg_2, int *flag, MPI_Status *arg_4);
_EXTERN_C_ int MPI_Iprobe(int arg_0, int arg_1, MPI_Comm arg_2, int *flag, MPI_Status *arg_4) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Iprobe(arg_0, arg_1, arg_2, flag, arg_4), __func__);
    return _wrap_py_return_val;
}















/* ================== C Wrappers for MPI_Abort ================== */
_EXTERN_C_ int PMPI_Abort(MPI_Comm arg_0, int arg_1);
_EXTERN_C_ int MPI_Abort(MPI_Comm arg_0, int arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Abort(arg_0, arg_1), __func__);
    return _wrap_py_return_val;
}

#ifdef ENABLE_DEPRECATED_FUNC
/* ================== C Wrappers for MPI_Address ================== */
_EXTERN_C_ int PMPI_Address(void *arg_0, MPI_Aint *arg_1);
_EXTERN_C_ int MPI_Address(void *arg_0, MPI_Aint *arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Address(arg_0, arg_1), __func__);
    return _wrap_py_return_val;
}
#endif

/* ================== C Wrappers for MPI_Allgather ================== */
_EXTERN_C_ int PMPI_Allgather(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, void *arg_3, int arg_4, MPI_Datatype arg_5, MPI_Comm arg_6);
_EXTERN_C_ int MPI_Allgather(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, void *arg_3, int arg_4, MPI_Datatype arg_5, MPI_Comm arg_6) { 
    int _wrap_py_return_val = 0;
    MPI_Request req;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Iallgather(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, &req), __func__);
    MPI_Wait(&req, MPI_STATUS_IGNORE);
    //    PMPI_WRAP(_wrap_py_return_val = PMPI_Allgather(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Allgatherv ================== */
_EXTERN_C_ int PMPI_Allgatherv(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, void *arg_3, nin_mpi_const int *arg_4, nin_mpi_const int *arg_5, MPI_Datatype arg_6, MPI_Comm arg_7);
_EXTERN_C_ int MPI_Allgatherv(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, void *arg_3, nin_mpi_const int *arg_4, nin_mpi_const int *arg_5, MPI_Datatype arg_6, MPI_Comm arg_7) { 
    int _wrap_py_return_val = 0;
    MPI_Request req;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Iallgatherv(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, &req), __func__);
    MPI_Wait(&req, MPI_STATUS_IGNORE);
    //    PMPI_WRAP(_wrap_py_return_val = PMPI_Allgatherv(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Iallreduce ================== */
_EXTERN_C_ int PMPI_Iallreduce(const void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype, MPI_Op op, MPI_Comm comm, MPI_Request *request);
_EXTERN_C_ int MPI_Iallreduce(const void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype, MPI_Op op, MPI_Comm comm, MPI_Request *request)
{
  int _wrap_py_return_val = 0;
  PMPI_WRAP(_wrap_py_return_val = PMPI_Iallreduce(sendbuf, recvbuf, count, datatype, op, comm, request), __func__);
  return _wrap_py_return_val;
}


/* ================== C Wrappers for MPI_Allreduce ================== */
_EXTERN_C_ int PMPI_Allreduce(nin_mpi_const void *arg_0, void *arg_1, int arg_2, MPI_Datatype arg_3, MPI_Op arg_4, MPI_Comm arg_5);
_EXTERN_C_ int MPI_Allreduce(nin_mpi_const void *arg_0, void *arg_1, int arg_2, MPI_Datatype arg_3, MPI_Op arg_4, MPI_Comm arg_5) { 
    int _wrap_py_return_val = 0;
    MPI_Request req;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Iallreduce(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, &req), __func__);
    //    -wrap_py_return_val = MPI_Iallreduce(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, &req);
    _wrap_py_return_val = MPI_Wait(&req, MPI_STATUS_IGNORE);
    //PMPI_WRAP(_wrap_py_return_val = PMPI_Allreduce(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Iallgather ================== */
_EXTERN_C_ int PMPI_Iallgather(const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, MPI_Comm comm, MPI_Request *request);
_EXTERN_C_ int MPI_Iallgather(const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, MPI_Comm comm, MPI_Request *request) {
  int _wrap_py_return_val = 0;
  PMPI_WRAP(_wrap_py_return_val = PMPI_Iallgather(sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, comm, request), __func__);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Alltoall ================== */
_EXTERN_C_ int PMPI_Alltoall(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, void *arg_3, int arg_4, MPI_Datatype arg_5, MPI_Comm arg_6);
_EXTERN_C_ int MPI_Alltoall(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, void *arg_3, int arg_4, MPI_Datatype arg_5, MPI_Comm arg_6) { 
    int _wrap_py_return_val = 0;
    MPI_Request req;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Ialltoall(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, &req), __func__);
    MPI_Wait(&req, MPI_STATUS_IGNORE);
    //    PMPI_WRAP(_wrap_py_return_val = PMPI_Alltoall(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Alltoallv ================== */
_EXTERN_C_ int PMPI_Alltoallv(nin_mpi_const void *arg_0, nin_mpi_const int *arg_1, nin_mpi_const int *arg_2, MPI_Datatype arg_3, void *arg_4, nin_mpi_const int *arg_5, nin_mpi_const int *arg_6, MPI_Datatype arg_7, MPI_Comm arg_8);
_EXTERN_C_ int MPI_Alltoallv(nin_mpi_const void *arg_0, nin_mpi_const int *arg_1, nin_mpi_const int *arg_2, MPI_Datatype arg_3, void *arg_4, nin_mpi_const int *arg_5, nin_mpi_const int *arg_6, MPI_Datatype arg_7, MPI_Comm arg_8) { 
    int _wrap_py_return_val = 0;
    MPI_Request req;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Ialltoallv(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8, &req), __func__);
    MPI_Wait(&req, MPI_STATUS_IGNORE);
    
    //    PMPI_WRAP(_wrap_py_return_val = PMPI_Alltoallv(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8), __func__);
    return _wrap_py_return_val;
}

#ifdef ENABLE_DEPRECATED_FUNC
/* ================== C Wrappers for MPI_Attr_delete ================== */
_EXTERN_C_ int PMPI_Attr_delete(MPI_Comm arg_0, int arg_1);
_EXTERN_C_ int MPI_Attr_delete(MPI_Comm arg_0, int arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Attr_delete(arg_0, arg_1), __func__);
    return _wrap_py_return_val;
}
#endif

#ifdef ENABLE_DEPRECATED_FUNC
/* ================== C Wrappers for MPI_Attr_get ================== */
_EXTERN_C_ int PMPI_Attr_get(MPI_Comm arg_0, int arg_1, void *arg_2, int *arg_3);
_EXTERN_C_ int MPI_Attr_get(MPI_Comm arg_0, int arg_1, void *arg_2, int *arg_3) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Attr_get(arg_0, arg_1, arg_2, arg_3), __func__);
    return _wrap_py_return_val;
}
#endif

#ifdef ENABLE_DEPRECATED_FUNC
/* ================== C Wrappers for MPI_Attr_put ================== */
_EXTERN_C_ int PMPI_Attr_put(MPI_Comm arg_0, int arg_1, void *arg_2);
_EXTERN_C_ int MPI_Attr_put(MPI_Comm arg_0, int arg_1, void *arg_2) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Attr_put(arg_0, arg_1, arg_2), __func__);
    return _wrap_py_return_val;
}
#endif

/* ================== C Wrappers for MPI_Barrier ================== */
_EXTERN_C_ int PMPI_Barrier(MPI_Comm arg_0);
_EXTERN_C_ int MPI_Barrier(MPI_Comm arg_0) { 
    int _wrap_py_return_val = 0;
    MPI_Request req;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Ibarrier(arg_0, &req), __func__);
    MPI_Wait(&req, MPI_STATUS_IGNORE);
    //    PMPI_WRAP(_wrap_py_return_val = PMPI_Barrier(arg_0), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Bcast ================== */
_EXTERN_C_ int PMPI_Bcast(void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, MPI_Comm arg_4);
_EXTERN_C_ int MPI_Bcast(void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, MPI_Comm arg_4) { 
    int _wrap_py_return_val = 0;
    MPI_Request req;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Ibcast(arg_0, arg_1, arg_2, arg_3, arg_4, &req), __func__);
    MPI_Wait(&req, MPI_STATUS_IGNORE);
    //    PMPI_WRAP(_wrap_py_return_val = PMPI_Bcast(arg_0, arg_1, arg_2, arg_3, arg_4), __func__);
    return _wrap_py_return_val;
}



/* ================== C Wrappers for MPI_Buffer_attach ================== */
_EXTERN_C_ int PMPI_Buffer_attach(void *arg_0, int arg_1);
_EXTERN_C_ int MPI_Buffer_attach(void *arg_0, int arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Buffer_attach(arg_0, arg_1), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Buffer_detach ================== */
_EXTERN_C_ int PMPI_Buffer_detach(void *arg_0, int *arg_1);
_EXTERN_C_ int MPI_Buffer_detach(void *arg_0, int *arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Buffer_detach(arg_0, arg_1), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Cancel ================== */
_EXTERN_C_ int PMPI_Cancel(MPI_Request *arg_0);
_EXTERN_C_ int MPI_Cancel(MPI_Request *arg_0) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Cancel(arg_0), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Cart_coords ================== */
_EXTERN_C_ int PMPI_Cart_coords(MPI_Comm arg_0, int arg_1, nin_mpi_const int arg_2, int *arg_3);
_EXTERN_C_ int MPI_Cart_coords(MPI_Comm arg_0, int arg_1, nin_mpi_const int arg_2, int *arg_3) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Cart_coords(arg_0, arg_1, arg_2, arg_3), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Cart_create ================== */
_EXTERN_C_ int PMPI_Cart_create(MPI_Comm arg_0, int arg_1, nin_mpi_const int *arg_2, nin_mpi_const int *arg_3, int arg_4, MPI_Comm *arg_5);
_EXTERN_C_ int MPI_Cart_create(MPI_Comm arg_0, int arg_1, nin_mpi_const int *arg_2, nin_mpi_const int *arg_3, int arg_4, MPI_Comm *arg_5) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Cart_create(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Cart_get ================== */
_EXTERN_C_ int PMPI_Cart_get(MPI_Comm arg_0, int arg_1, int *arg_2, int *arg_3, int *arg_4);
_EXTERN_C_ int MPI_Cart_get(MPI_Comm arg_0, int arg_1, int *arg_2, int *arg_3, int *arg_4) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Cart_get(arg_0, arg_1, arg_2, arg_3, arg_4), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Cart_map ================== */
_EXTERN_C_ int PMPI_Cart_map(MPI_Comm arg_0, int arg_1, nin_mpi_const int *arg_2, nin_mpi_const int *arg_3, int *arg_4);
_EXTERN_C_ int MPI_Cart_map(MPI_Comm arg_0, int arg_1, nin_mpi_const int *arg_2, nin_mpi_const int *arg_3, int *arg_4) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Cart_map(arg_0, arg_1, arg_2, arg_3, arg_4), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Cart_rank ================== */
_EXTERN_C_ int PMPI_Cart_rank(MPI_Comm arg_0, nin_mpi_const int *arg_1, int *arg_2);
_EXTERN_C_ int MPI_Cart_rank(MPI_Comm arg_0, nin_mpi_const int *arg_1, int *arg_2) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Cart_rank(arg_0, arg_1, arg_2), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Cart_shift ================== */
_EXTERN_C_ int PMPI_Cart_shift(MPI_Comm arg_0, int arg_1, int arg_2, int *arg_3, int *arg_4);
_EXTERN_C_ int MPI_Cart_shift(MPI_Comm arg_0, int arg_1, int arg_2, int *arg_3, int *arg_4) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Cart_shift(arg_0, arg_1, arg_2, arg_3, arg_4), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Cart_sub ================== */
_EXTERN_C_ int PMPI_Cart_sub(MPI_Comm arg_0, nin_mpi_const int *arg_1, MPI_Comm *arg_2);
_EXTERN_C_ int MPI_Cart_sub(MPI_Comm arg_0, nin_mpi_const int *arg_1, MPI_Comm *arg_2) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Cart_sub(arg_0, arg_1, arg_2), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Cartdim_get ================== */
_EXTERN_C_ int PMPI_Cartdim_get(MPI_Comm arg_0, int *arg_1);
_EXTERN_C_ int MPI_Cartdim_get(MPI_Comm arg_0, int *arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Cartdim_get(arg_0, arg_1), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Comm_compare ================== */
_EXTERN_C_ int PMPI_Comm_compare(MPI_Comm arg_0, MPI_Comm arg_1, int *arg_2);
_EXTERN_C_ int MPI_Comm_compare(MPI_Comm arg_0, MPI_Comm arg_1, int *arg_2) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Comm_compare(arg_0, arg_1, arg_2), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Comm_create ================== */
_EXTERN_C_ int PMPI_Comm_create(MPI_Comm arg_0, MPI_Group arg_1, MPI_Comm *arg_2);
_EXTERN_C_ int MPI_Comm_create(MPI_Comm arg_0, MPI_Group arg_1, MPI_Comm *arg_2) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Comm_create(arg_0, arg_1, arg_2), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Comm_dup ================== */
_EXTERN_C_ int PMPI_Comm_dup(MPI_Comm arg_0, MPI_Comm *arg_1);
_EXTERN_C_ int MPI_Comm_dup(MPI_Comm arg_0, MPI_Comm *arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Comm_dup(arg_0, arg_1), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Comm_free ================== */
_EXTERN_C_ int PMPI_Comm_free(MPI_Comm *arg_0);
_EXTERN_C_ int MPI_Comm_free(MPI_Comm *arg_0) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Comm_free(arg_0), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Comm_get_name ================== */
_EXTERN_C_ int PMPI_Comm_get_name(MPI_Comm arg_0, char *arg_1, int *arg_2);
_EXTERN_C_ int MPI_Comm_get_name(MPI_Comm arg_0, char *arg_1, int *arg_2) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Comm_get_name(arg_0, arg_1, arg_2), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Comm_group ================== */
_EXTERN_C_ int PMPI_Comm_group(MPI_Comm arg_0, MPI_Group *arg_1);
_EXTERN_C_ int MPI_Comm_group(MPI_Comm arg_0, MPI_Group *arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Comm_group(arg_0, arg_1), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Comm_rank ================== */
_EXTERN_C_ int PMPI_Comm_rank(MPI_Comm arg_0, int *arg_1);
_EXTERN_C_ int MPI_Comm_rank(MPI_Comm arg_0, int *arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Comm_rank(arg_0, arg_1), __func__);
    //    PMPI_Comm_rank(arg_0, arg_1);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Comm_remote_group ================== */
_EXTERN_C_ int PMPI_Comm_remote_group(MPI_Comm arg_0, MPI_Group *arg_1);
_EXTERN_C_ int MPI_Comm_remote_group(MPI_Comm arg_0, MPI_Group *arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Comm_remote_group(arg_0, arg_1), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Comm_remote_size ================== */
_EXTERN_C_ int PMPI_Comm_remote_size(MPI_Comm arg_0, int *arg_1);
_EXTERN_C_ int MPI_Comm_remote_size(MPI_Comm arg_0, int *arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Comm_remote_size(arg_0, arg_1), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Comm_set_name ================== */
_EXTERN_C_ int PMPI_Comm_set_name(MPI_Comm arg_0, nin_mpi_const char *arg_1);
_EXTERN_C_ int MPI_Comm_set_name(MPI_Comm arg_0, nin_mpi_const char *arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Comm_set_name(arg_0, arg_1), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Comm_size ================== */
_EXTERN_C_ int PMPI_Comm_size(MPI_Comm arg_0, int *arg_1);
_EXTERN_C_ int MPI_Comm_size(MPI_Comm arg_0, int *arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Comm_size(arg_0, arg_1), __func__);
    //_wrap_py_return_val = PMPI_Comm_size(arg_0, arg_1);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Comm_split ================== */
_EXTERN_C_ int PMPI_Comm_split(MPI_Comm arg_0, int arg_1, int arg_2, MPI_Comm *arg_3);
_EXTERN_C_ int MPI_Comm_split(MPI_Comm arg_0, int arg_1, int arg_2, MPI_Comm *arg_3) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Comm_split(arg_0, arg_1, arg_2, arg_3), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Comm_test_inter ================== */
_EXTERN_C_ int PMPI_Comm_test_inter(MPI_Comm arg_0, int *arg_1);
_EXTERN_C_ int MPI_Comm_test_inter(MPI_Comm arg_0, int *arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Comm_test_inter(arg_0, arg_1), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Dims_create ================== */
_EXTERN_C_ int PMPI_Dims_create(int arg_0, int arg_1, int *arg_2);
_EXTERN_C_ int MPI_Dims_create(int arg_0, int arg_1, int *arg_2) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Dims_create(arg_0, arg_1, arg_2), __func__);
    return _wrap_py_return_val;
}

#ifdef ENABLE_DEPRECATED_FUNC
/* ================== C Wrappers for MPI_Errhandler_create ================== */
_EXTERN_C_ int PMPI_Errhandler_create(MPI_Handler_function *arg_0, MPI_Errhandler *arg_1);
_EXTERN_C_ int MPI_Errhandler_create(MPI_Handler_function *arg_0, MPI_Errhandler *arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Errhandler_create(arg_0, arg_1), __func__);
    return _wrap_py_return_val;
}
#endif

/* ================== C Wrappers for MPI_Errhandler_free ================== */
_EXTERN_C_ int PMPI_Errhandler_free(MPI_Errhandler *arg_0);
_EXTERN_C_ int MPI_Errhandler_free(MPI_Errhandler *arg_0) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Errhandler_free(arg_0), __func__);
    return _wrap_py_return_val;
}

#ifdef ENABLE_DEPRECATED_FUNC
/* ================== C Wrappers for MPI_Errhandler_get ================== */
_EXTERN_C_ int PMPI_Errhandler_get(MPI_Comm arg_0, MPI_Errhandler *arg_1);
_EXTERN_C_ int MPI_Errhandler_get(MPI_Comm arg_0, MPI_Errhandler *arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Errhandler_get(arg_0, arg_1), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Errhandler_set ================== */
_EXTERN_C_ int PMPI_Errhandler_set(MPI_Comm arg_0, MPI_Errhandler arg_1);
_EXTERN_C_ int MPI_Errhandler_set(MPI_Comm arg_0, MPI_Errhandler arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Errhandler_set(arg_0, arg_1), __func__);
    return _wrap_py_return_val;
}
#endif

/* ================== C Wrappers for MPI_Error_class ================== */
_EXTERN_C_ int PMPI_Error_class(int arg_0, int *arg_1);
_EXTERN_C_ int MPI_Error_class(int arg_0, int *arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Error_class(arg_0, arg_1), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Error_string ================== */
_EXTERN_C_ int PMPI_Error_string(int arg_0, char *arg_1, int *arg_2);
_EXTERN_C_ int MPI_Error_string(int arg_0, char *arg_1, int *arg_2) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Error_string(arg_0, arg_1, arg_2), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_close ================== */
_EXTERN_C_ int PMPI_File_close(MPI_File *arg_0);
_EXTERN_C_ int MPI_File_close(MPI_File *arg_0) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_close(arg_0), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_delete ================== */
_EXTERN_C_ int PMPI_File_delete(nin_mpi_const char *arg_0, MPI_Info arg_1);
_EXTERN_C_ int MPI_File_delete(nin_mpi_const char *arg_0, MPI_Info arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_delete(arg_0, arg_1), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_get_amode ================== */
_EXTERN_C_ int PMPI_File_get_amode(MPI_File arg_0, int *arg_1);
_EXTERN_C_ int MPI_File_get_amode(MPI_File arg_0, int *arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_get_amode(arg_0, arg_1), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_get_atomicity ================== */
_EXTERN_C_ int PMPI_File_get_atomicity(MPI_File arg_0, int *arg_1);
_EXTERN_C_ int MPI_File_get_atomicity(MPI_File arg_0, int *arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_get_atomicity(arg_0, arg_1), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_get_byte_offset ================== */
_EXTERN_C_ int PMPI_File_get_byte_offset(MPI_File arg_0, MPI_Offset arg_1, MPI_Offset *arg_2);
_EXTERN_C_ int MPI_File_get_byte_offset(MPI_File arg_0, MPI_Offset arg_1, MPI_Offset *arg_2) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_get_byte_offset(arg_0, arg_1, arg_2), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_get_errhandler ================== */
_EXTERN_C_ int PMPI_File_get_errhandler(MPI_File arg_0, MPI_Errhandler *arg_1);
_EXTERN_C_ int MPI_File_get_errhandler(MPI_File arg_0, MPI_Errhandler *arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_get_errhandler(arg_0, arg_1), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_get_group ================== */
_EXTERN_C_ int PMPI_File_get_group(MPI_File arg_0, MPI_Group *arg_1);
_EXTERN_C_ int MPI_File_get_group(MPI_File arg_0, MPI_Group *arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_get_group(arg_0, arg_1), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_get_info ================== */
_EXTERN_C_ int PMPI_File_get_info(MPI_File arg_0, MPI_Info *arg_1);
_EXTERN_C_ int MPI_File_get_info(MPI_File arg_0, MPI_Info *arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_get_info(arg_0, arg_1), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_get_position ================== */
_EXTERN_C_ int PMPI_File_get_position(MPI_File arg_0, MPI_Offset *arg_1);
_EXTERN_C_ int MPI_File_get_position(MPI_File arg_0, MPI_Offset *arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_get_position(arg_0, arg_1), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_get_position_shared ================== */
_EXTERN_C_ int PMPI_File_get_position_shared(MPI_File arg_0, MPI_Offset *arg_1);
_EXTERN_C_ int MPI_File_get_position_shared(MPI_File arg_0, MPI_Offset *arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_get_position_shared(arg_0, arg_1), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_get_size ================== */
_EXTERN_C_ int PMPI_File_get_size(MPI_File arg_0, MPI_Offset *arg_1);
_EXTERN_C_ int MPI_File_get_size(MPI_File arg_0, MPI_Offset *arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_get_size(arg_0, arg_1), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_get_type_extent ================== */
_EXTERN_C_ int PMPI_File_get_type_extent(MPI_File arg_0, MPI_Datatype arg_1, MPI_Aint *arg_2);
_EXTERN_C_ int MPI_File_get_type_extent(MPI_File arg_0, MPI_Datatype arg_1, MPI_Aint *arg_2) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_get_type_extent(arg_0, arg_1, arg_2), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_get_view ================== */
_EXTERN_C_ int PMPI_File_get_view(MPI_File arg_0, MPI_Offset *arg_1, MPI_Datatype *arg_2, MPI_Datatype *arg_3, char *arg_4);
_EXTERN_C_ int MPI_File_get_view(MPI_File arg_0, MPI_Offset *arg_1, MPI_Datatype *arg_2, MPI_Datatype *arg_3, char *arg_4) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_get_view(arg_0, arg_1, arg_2, arg_3, arg_4), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_iread ================== */
_EXTERN_C_ int PMPI_File_iread(MPI_File arg_0, void *arg_1, int arg_2, MPI_Datatype arg_3, MPIO_Request *arg_4);
_EXTERN_C_ int MPI_File_iread(MPI_File arg_0, void *arg_1, int arg_2, MPI_Datatype arg_3, MPIO_Request *arg_4) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_iread(arg_0, arg_1, arg_2, arg_3, arg_4), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_iread_at ================== */
_EXTERN_C_ int PMPI_File_iread_at(MPI_File arg_0, MPI_Offset arg_1, void *arg_2, int arg_3, MPI_Datatype arg_4, MPIO_Request *arg_5);
_EXTERN_C_ int MPI_File_iread_at(MPI_File arg_0, MPI_Offset arg_1, void *arg_2, int arg_3, MPI_Datatype arg_4, MPIO_Request *arg_5) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_iread_at(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_iread_shared ================== */
_EXTERN_C_ int PMPI_File_iread_shared(MPI_File arg_0, void *arg_1, int arg_2, MPI_Datatype arg_3, MPIO_Request *arg_4);
_EXTERN_C_ int MPI_File_iread_shared(MPI_File arg_0, void *arg_1, int arg_2, MPI_Datatype arg_3, MPIO_Request *arg_4) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_iread_shared(arg_0, arg_1, arg_2, arg_3, arg_4), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_iwrite ================== */
_EXTERN_C_ int PMPI_File_iwrite(MPI_File arg_0, nin_mpi_const void *arg_1, int arg_2, MPI_Datatype arg_3, MPIO_Request *arg_4);
_EXTERN_C_ int MPI_File_iwrite(MPI_File arg_0, nin_mpi_const void *arg_1, int arg_2, MPI_Datatype arg_3, MPIO_Request *arg_4) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_iwrite(arg_0, arg_1, arg_2, arg_3, arg_4), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_iwrite_at ================== */
_EXTERN_C_ int PMPI_File_iwrite_at(MPI_File arg_0, MPI_Offset arg_1, nin_mpi_const void *arg_2, int arg_3, MPI_Datatype arg_4, MPIO_Request *arg_5);
_EXTERN_C_ int MPI_File_iwrite_at(MPI_File arg_0, MPI_Offset arg_1, nin_mpi_const void *arg_2, int arg_3, MPI_Datatype arg_4, MPIO_Request *arg_5) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_iwrite_at(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_iwrite_shared ================== */
_EXTERN_C_ int PMPI_File_iwrite_shared(MPI_File arg_0, nin_mpi_const void *arg_1, int arg_2, MPI_Datatype arg_3, MPIO_Request *arg_4);
_EXTERN_C_ int MPI_File_iwrite_shared(MPI_File arg_0, nin_mpi_const void *arg_1, int arg_2, MPI_Datatype arg_3, MPIO_Request *arg_4) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_iwrite_shared(arg_0, arg_1, arg_2, arg_3, arg_4), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_open ================== */
_EXTERN_C_ int PMPI_File_open(MPI_Comm arg_0, nin_mpi_const char *arg_1, int arg_2, MPI_Info arg_3, MPI_File *arg_4);
_EXTERN_C_ int MPI_File_open(MPI_Comm arg_0, nin_mpi_const char *arg_1, int arg_2, MPI_Info arg_3, MPI_File *arg_4) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_open(arg_0, arg_1, arg_2, arg_3, arg_4), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_preallocate ================== */
_EXTERN_C_ int PMPI_File_preallocate(MPI_File arg_0, MPI_Offset arg_1);
_EXTERN_C_ int MPI_File_preallocate(MPI_File arg_0, MPI_Offset arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_preallocate(arg_0, arg_1), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_read ================== */
_EXTERN_C_ int PMPI_File_read(MPI_File arg_0, void *arg_1, int arg_2, MPI_Datatype arg_3, MPI_Status *arg_4);
_EXTERN_C_ int MPI_File_read(MPI_File arg_0, void *arg_1, int arg_2, MPI_Datatype arg_3, MPI_Status *arg_4) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_read(arg_0, arg_1, arg_2, arg_3, arg_4), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_read_all ================== */
_EXTERN_C_ int PMPI_File_read_all(MPI_File arg_0, void *arg_1, int arg_2, MPI_Datatype arg_3, MPI_Status *arg_4);
_EXTERN_C_ int MPI_File_read_all(MPI_File arg_0, void *arg_1, int arg_2, MPI_Datatype arg_3, MPI_Status *arg_4) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_read_all(arg_0, arg_1, arg_2, arg_3, arg_4), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_read_all_begin ================== */
_EXTERN_C_ int PMPI_File_read_all_begin(MPI_File arg_0, void *arg_1, int arg_2, MPI_Datatype arg_3);
_EXTERN_C_ int MPI_File_read_all_begin(MPI_File arg_0, void *arg_1, int arg_2, MPI_Datatype arg_3) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_read_all_begin(arg_0, arg_1, arg_2, arg_3), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_read_all_end ================== */
_EXTERN_C_ int PMPI_File_read_all_end(MPI_File arg_0, void *arg_1, MPI_Status *arg_2);
_EXTERN_C_ int MPI_File_read_all_end(MPI_File arg_0, void *arg_1, MPI_Status *arg_2) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_read_all_end(arg_0, arg_1, arg_2), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_read_at ================== */
_EXTERN_C_ int PMPI_File_read_at(MPI_File arg_0, MPI_Offset arg_1, void *arg_2, int arg_3, MPI_Datatype arg_4, MPI_Status *arg_5);
_EXTERN_C_ int MPI_File_read_at(MPI_File arg_0, MPI_Offset arg_1, void *arg_2, int arg_3, MPI_Datatype arg_4, MPI_Status *arg_5) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_read_at(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_read_at_all ================== */
_EXTERN_C_ int PMPI_File_read_at_all(MPI_File arg_0, MPI_Offset arg_1, void *arg_2, int arg_3, MPI_Datatype arg_4, MPI_Status *arg_5);
_EXTERN_C_ int MPI_File_read_at_all(MPI_File arg_0, MPI_Offset arg_1, void *arg_2, int arg_3, MPI_Datatype arg_4, MPI_Status *arg_5) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_read_at_all(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_read_at_all_begin ================== */
_EXTERN_C_ int PMPI_File_read_at_all_begin(MPI_File arg_0, MPI_Offset arg_1, void *arg_2, int arg_3, MPI_Datatype arg_4);
_EXTERN_C_ int MPI_File_read_at_all_begin(MPI_File arg_0, MPI_Offset arg_1, void *arg_2, int arg_3, MPI_Datatype arg_4) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_read_at_all_begin(arg_0, arg_1, arg_2, arg_3, arg_4), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_read_at_all_end ================== */
_EXTERN_C_ int PMPI_File_read_at_all_end(MPI_File arg_0, void *arg_1, MPI_Status *arg_2);
_EXTERN_C_ int MPI_File_read_at_all_end(MPI_File arg_0, void *arg_1, MPI_Status *arg_2) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_read_at_all_end(arg_0, arg_1, arg_2), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_read_ordered ================== */
_EXTERN_C_ int PMPI_File_read_ordered(MPI_File arg_0, void *arg_1, int arg_2, MPI_Datatype arg_3, MPI_Status *arg_4);
_EXTERN_C_ int MPI_File_read_ordered(MPI_File arg_0, void *arg_1, int arg_2, MPI_Datatype arg_3, MPI_Status *arg_4) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_read_ordered(arg_0, arg_1, arg_2, arg_3, arg_4), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_read_ordered_begin ================== */
_EXTERN_C_ int PMPI_File_read_ordered_begin(MPI_File arg_0, void *arg_1, int arg_2, MPI_Datatype arg_3);
_EXTERN_C_ int MPI_File_read_ordered_begin(MPI_File arg_0, void *arg_1, int arg_2, MPI_Datatype arg_3) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_read_ordered_begin(arg_0, arg_1, arg_2, arg_3), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_read_ordered_end ================== */
_EXTERN_C_ int PMPI_File_read_ordered_end(MPI_File arg_0, void *arg_1, MPI_Status *arg_2);
_EXTERN_C_ int MPI_File_read_ordered_end(MPI_File arg_0, void *arg_1, MPI_Status *arg_2) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_read_ordered_end(arg_0, arg_1, arg_2), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_read_shared ================== */
_EXTERN_C_ int PMPI_File_read_shared(MPI_File arg_0, void *arg_1, int arg_2, MPI_Datatype arg_3, MPI_Status *arg_4);
_EXTERN_C_ int MPI_File_read_shared(MPI_File arg_0, void *arg_1, int arg_2, MPI_Datatype arg_3, MPI_Status *arg_4) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_read_shared(arg_0, arg_1, arg_2, arg_3, arg_4), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_seek ================== */
_EXTERN_C_ int PMPI_File_seek(MPI_File arg_0, MPI_Offset arg_1, int arg_2);
_EXTERN_C_ int MPI_File_seek(MPI_File arg_0, MPI_Offset arg_1, int arg_2) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_seek(arg_0, arg_1, arg_2), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_seek_shared ================== */
_EXTERN_C_ int PMPI_File_seek_shared(MPI_File arg_0, MPI_Offset arg_1, int arg_2);
_EXTERN_C_ int MPI_File_seek_shared(MPI_File arg_0, MPI_Offset arg_1, int arg_2) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_seek_shared(arg_0, arg_1, arg_2), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_set_atomicity ================== */
_EXTERN_C_ int PMPI_File_set_atomicity(MPI_File arg_0, int arg_1);
_EXTERN_C_ int MPI_File_set_atomicity(MPI_File arg_0, int arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_set_atomicity(arg_0, arg_1), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_set_errhandler ================== */
_EXTERN_C_ int PMPI_File_set_errhandler(MPI_File arg_0, MPI_Errhandler arg_1);
_EXTERN_C_ int MPI_File_set_errhandler(MPI_File arg_0, MPI_Errhandler arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_set_errhandler(arg_0, arg_1), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_set_info ================== */
_EXTERN_C_ int PMPI_File_set_info(MPI_File arg_0, MPI_Info arg_1);
_EXTERN_C_ int MPI_File_set_info(MPI_File arg_0, MPI_Info arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_set_info(arg_0, arg_1), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_set_size ================== */
_EXTERN_C_ int PMPI_File_set_size(MPI_File arg_0, MPI_Offset arg_1);
_EXTERN_C_ int MPI_File_set_size(MPI_File arg_0, MPI_Offset arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_set_size(arg_0, arg_1), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_set_view ================== */
_EXTERN_C_ int PMPI_File_set_view(MPI_File arg_0, MPI_Offset arg_1, MPI_Datatype arg_2, MPI_Datatype arg_3, nin_mpi_const char *arg_4, MPI_Info arg_5);
_EXTERN_C_ int MPI_File_set_view(MPI_File arg_0, MPI_Offset arg_1, MPI_Datatype arg_2, MPI_Datatype arg_3, nin_mpi_const char *arg_4, MPI_Info arg_5) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_set_view(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_sync ================== */
_EXTERN_C_ int PMPI_File_sync(MPI_File arg_0);
_EXTERN_C_ int MPI_File_sync(MPI_File arg_0) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_sync(arg_0), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_write ================== */
_EXTERN_C_ int PMPI_File_write(MPI_File arg_0, nin_mpi_const void *arg_1, int arg_2, MPI_Datatype arg_3, MPI_Status *arg_4);
_EXTERN_C_ int MPI_File_write(MPI_File arg_0, nin_mpi_const void *arg_1, int arg_2, MPI_Datatype arg_3, MPI_Status *arg_4) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_write(arg_0, arg_1, arg_2, arg_3, arg_4), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_write_all ================== */
_EXTERN_C_ int PMPI_File_write_all(MPI_File arg_0, nin_mpi_const void *arg_1, int arg_2, MPI_Datatype arg_3, MPI_Status *arg_4);
_EXTERN_C_ int MPI_File_write_all(MPI_File arg_0, nin_mpi_const void *arg_1, int arg_2, MPI_Datatype arg_3, MPI_Status *arg_4) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_write_all(arg_0, arg_1, arg_2, arg_3, arg_4), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_write_all_begin ================== */
_EXTERN_C_ int PMPI_File_write_all_begin(MPI_File arg_0, nin_mpi_const void *arg_1, int arg_2, MPI_Datatype arg_3);
_EXTERN_C_ int MPI_File_write_all_begin(MPI_File arg_0, nin_mpi_const void *arg_1, int arg_2, MPI_Datatype arg_3) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_write_all_begin(arg_0, arg_1, arg_2, arg_3), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_write_all_end ================== */
_EXTERN_C_ int PMPI_File_write_all_end(MPI_File arg_0, nin_mpi_const void *arg_1, MPI_Status *arg_2);
_EXTERN_C_ int MPI_File_write_all_end(MPI_File arg_0, nin_mpi_const void *arg_1, MPI_Status *arg_2) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_write_all_end(arg_0, arg_1, arg_2), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_write_at ================== */
_EXTERN_C_ int PMPI_File_write_at(MPI_File arg_0, MPI_Offset arg_1, nin_mpi_const void *arg_2, int arg_3, MPI_Datatype arg_4, MPI_Status *arg_5);
_EXTERN_C_ int MPI_File_write_at(MPI_File arg_0, MPI_Offset arg_1, nin_mpi_const void *arg_2, int arg_3, MPI_Datatype arg_4, MPI_Status *arg_5) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_write_at(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_write_at_all ================== */
_EXTERN_C_ int PMPI_File_write_at_all(MPI_File arg_0, MPI_Offset arg_1, nin_mpi_const void *arg_2, int arg_3, MPI_Datatype arg_4, MPI_Status *arg_5);
_EXTERN_C_ int MPI_File_write_at_all(MPI_File arg_0, MPI_Offset arg_1, nin_mpi_const void *arg_2, int arg_3, MPI_Datatype arg_4, MPI_Status *arg_5) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_write_at_all(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_write_at_all_begin ================== */
_EXTERN_C_ int PMPI_File_write_at_all_begin(MPI_File arg_0, MPI_Offset arg_1, nin_mpi_const void *arg_2, int arg_3, MPI_Datatype arg_4);
_EXTERN_C_ int MPI_File_write_at_all_begin(MPI_File arg_0, MPI_Offset arg_1, nin_mpi_const void *arg_2, int arg_3, MPI_Datatype arg_4) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_write_at_all_begin(arg_0, arg_1, arg_2, arg_3, arg_4), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_write_at_all_end ================== */
_EXTERN_C_ int PMPI_File_write_at_all_end(MPI_File arg_0, nin_mpi_const void *arg_1, MPI_Status *arg_2);
_EXTERN_C_ int MPI_File_write_at_all_end(MPI_File arg_0, nin_mpi_const void *arg_1, MPI_Status *arg_2) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_write_at_all_end(arg_0, arg_1, arg_2), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_write_ordered ================== */
_EXTERN_C_ int PMPI_File_write_ordered(MPI_File arg_0, nin_mpi_const void *arg_1, int arg_2, MPI_Datatype arg_3, MPI_Status *arg_4);
_EXTERN_C_ int MPI_File_write_ordered(MPI_File arg_0, nin_mpi_const void *arg_1, int arg_2, MPI_Datatype arg_3, MPI_Status *arg_4) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_write_ordered(arg_0, arg_1, arg_2, arg_3, arg_4), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_write_ordered_begin ================== */
_EXTERN_C_ int PMPI_File_write_ordered_begin(MPI_File arg_0, nin_mpi_const void *arg_1, int arg_2, MPI_Datatype arg_3);
_EXTERN_C_ int MPI_File_write_ordered_begin(MPI_File arg_0, nin_mpi_const void *arg_1, int arg_2, MPI_Datatype arg_3) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_write_ordered_begin(arg_0, arg_1, arg_2, arg_3), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_write_ordered_end ================== */
_EXTERN_C_ int PMPI_File_write_ordered_end(MPI_File arg_0, nin_mpi_const void *arg_1, MPI_Status *arg_2);
_EXTERN_C_ int MPI_File_write_ordered_end(MPI_File arg_0, nin_mpi_const void *arg_1, MPI_Status *arg_2) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_write_ordered_end(arg_0, arg_1, arg_2), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_File_write_shared ================== */
_EXTERN_C_ int PMPI_File_write_shared(MPI_File arg_0, nin_mpi_const void *arg_1, int arg_2, MPI_Datatype arg_3, MPI_Status *arg_4);
_EXTERN_C_ int MPI_File_write_shared(MPI_File arg_0, nin_mpi_const void *arg_1, int arg_2, MPI_Datatype arg_3, MPI_Status *arg_4) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_File_write_shared(arg_0, arg_1, arg_2, arg_3, arg_4), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Finalize ================== */
_EXTERN_C_ int PMPI_Finalize();
_EXTERN_C_ int MPI_Finalize() { 
    int _wrap_py_return_val = 0;
    nin_delayed_send_request ds_req;
    //    NIN_DBG("MPI finalizing");
    ds_req.is_final = 1;
    nin_thread_input.enqueue(&ds_req);
    pthread_join(nin_nosie_thread, NULL);
    //    NIN_DBG("MPI finalizing: joined");
    PMPI_WRAP(_wrap_py_return_val = PMPI_Finalize(), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Finalized ================== */
_EXTERN_C_ int PMPI_Finalized(int *arg_0);
_EXTERN_C_ int MPI_Finalized(int *arg_0) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Finalized(arg_0), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Gather ================== */
_EXTERN_C_ int PMPI_Gather(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, void *arg_3, int arg_4, MPI_Datatype arg_5, int arg_6, MPI_Comm arg_7);
_EXTERN_C_ int MPI_Gather(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, void *arg_3, int arg_4, MPI_Datatype arg_5, int arg_6, MPI_Comm arg_7) { 
    int _wrap_py_return_val = 0;
    MPI_Request req;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Igather(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, &req), __func__);
    MPI_Wait(&req, MPI_STATUS_IGNORE);
    //    PMPI_WRAP(_wrap_py_return_val = PMPI_Gather(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Gatherv ================== */
_EXTERN_C_ int PMPI_Gatherv(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, void *arg_3, nin_mpi_const int *arg_4, nin_mpi_const int *arg_5, MPI_Datatype arg_6, int arg_7, MPI_Comm arg_8);
_EXTERN_C_ int MPI_Gatherv(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, void *arg_3, nin_mpi_const int *arg_4, nin_mpi_const int *arg_5, MPI_Datatype arg_6, int arg_7, MPI_Comm arg_8) { 
    int _wrap_py_return_val = 0;
    MPI_Request req;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Igatherv(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8, &req), __func__);
    MPI_Wait(&req, MPI_STATUS_IGNORE);
    //    PMPI_WRAP(_wrap_py_return_val = PMPI_Gatherv(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Get_count ================== */
_EXTERN_C_ int PMPI_Get_count(nin_mpi_const MPI_Status *arg_0, MPI_Datatype arg_1, int *arg_2);
_EXTERN_C_ int MPI_Get_count(nin_mpi_const MPI_Status *arg_0, MPI_Datatype arg_1, int *arg_2) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Get_count(arg_0, arg_1, arg_2), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Get_elements ================== */
_EXTERN_C_ int PMPI_Get_elements(nin_mpi_const MPI_Status *arg_0, MPI_Datatype arg_1, int *arg_2);
_EXTERN_C_ int MPI_Get_elements(nin_mpi_const MPI_Status *arg_0, MPI_Datatype arg_1, int *arg_2) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Get_elements(arg_0, arg_1, arg_2), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Get_processor_name ================== */
_EXTERN_C_ int PMPI_Get_processor_name(char *arg_0, int *arg_1);
_EXTERN_C_ int MPI_Get_processor_name(char *arg_0, int *arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Get_processor_name(arg_0, arg_1), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Get_version ================== */
_EXTERN_C_ int PMPI_Get_version(int *arg_0, int *arg_1);
_EXTERN_C_ int MPI_Get_version(int *arg_0, int *arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Get_version(arg_0, arg_1), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Graph_create ================== */
_EXTERN_C_ int PMPI_Graph_create(MPI_Comm arg_0, int arg_1, nin_mpi_const int *arg_2, nin_mpi_const int *arg_3, int arg_4, MPI_Comm *arg_5);
_EXTERN_C_ int MPI_Graph_create(MPI_Comm arg_0, int arg_1, nin_mpi_const int *arg_2, nin_mpi_const int *arg_3, int arg_4, MPI_Comm *arg_5) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Graph_create(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Graph_get ================== */
_EXTERN_C_ int PMPI_Graph_get(MPI_Comm arg_0, int arg_1, int arg_2, int *arg_3, int *arg_4);
_EXTERN_C_ int MPI_Graph_get(MPI_Comm arg_0, int arg_1, int arg_2, int *arg_3, int *arg_4) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Graph_get(arg_0, arg_1, arg_2, arg_3, arg_4), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Graph_map ================== */
_EXTERN_C_ int PMPI_Graph_map(MPI_Comm arg_0, int arg_1, nin_mpi_const int *arg_2, nin_mpi_const int *arg_3, int *arg_4);
_EXTERN_C_ int MPI_Graph_map(MPI_Comm arg_0, int arg_1, nin_mpi_const int *arg_2, nin_mpi_const int *arg_3, int *arg_4) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Graph_map(arg_0, arg_1, arg_2, arg_3, arg_4), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Graph_neighbors ================== */
_EXTERN_C_ int PMPI_Graph_neighbors(MPI_Comm arg_0, int arg_1, int arg_2, int *arg_3);
_EXTERN_C_ int MPI_Graph_neighbors(MPI_Comm arg_0, int arg_1, int arg_2, int *arg_3) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Graph_neighbors(arg_0, arg_1, arg_2, arg_3), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Graph_neighbors_count ================== */
_EXTERN_C_ int PMPI_Graph_neighbors_count(MPI_Comm arg_0, int arg_1, int *arg_2);
_EXTERN_C_ int MPI_Graph_neighbors_count(MPI_Comm arg_0, int arg_1, int *arg_2) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Graph_neighbors_count(arg_0, arg_1, arg_2), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Graphdims_get ================== */
_EXTERN_C_ int PMPI_Graphdims_get(MPI_Comm arg_0, int *arg_1, int *arg_2);
_EXTERN_C_ int MPI_Graphdims_get(MPI_Comm arg_0, int *arg_1, int *arg_2) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Graphdims_get(arg_0, arg_1, arg_2), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Group_compare ================== */
_EXTERN_C_ int PMPI_Group_compare(MPI_Group arg_0, MPI_Group arg_1, int *arg_2);
_EXTERN_C_ int MPI_Group_compare(MPI_Group arg_0, MPI_Group arg_1, int *arg_2) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Group_compare(arg_0, arg_1, arg_2), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Group_difference ================== */
_EXTERN_C_ int PMPI_Group_difference(MPI_Group arg_0, MPI_Group arg_1, MPI_Group *arg_2);
_EXTERN_C_ int MPI_Group_difference(MPI_Group arg_0, MPI_Group arg_1, MPI_Group *arg_2) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Group_difference(arg_0, arg_1, arg_2), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Group_excl ================== */
_EXTERN_C_ int PMPI_Group_excl(MPI_Group group, int arg_1, nin_mpi_const int *arg_2, MPI_Group *arg_3);
_EXTERN_C_ int MPI_Group_excl(MPI_Group group, int arg_1, nin_mpi_const int *arg_2, MPI_Group *arg_3) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Group_excl(group, arg_1, arg_2, arg_3), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Group_free ================== */
_EXTERN_C_ int PMPI_Group_free(MPI_Group *arg_0);
_EXTERN_C_ int MPI_Group_free(MPI_Group *arg_0) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Group_free(arg_0), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Group_incl ================== */
_EXTERN_C_ int PMPI_Group_incl(MPI_Group group, int arg_1, nin_mpi_const int *arg_2, MPI_Group *arg_3);
_EXTERN_C_ int MPI_Group_incl(MPI_Group group, int arg_1, nin_mpi_const int *arg_2, MPI_Group *arg_3) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Group_incl(group, arg_1, arg_2, arg_3), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Group_intersection ================== */
_EXTERN_C_ int PMPI_Group_intersection(MPI_Group arg_0, MPI_Group arg_1, MPI_Group *arg_2);
_EXTERN_C_ int MPI_Group_intersection(MPI_Group arg_0, MPI_Group arg_1, MPI_Group *arg_2) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Group_intersection(arg_0, arg_1, arg_2), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Group_range_excl ================== */
_EXTERN_C_ int PMPI_Group_range_excl(MPI_Group group, int arg_1, int arg_2[][3], MPI_Group *arg_3);
_EXTERN_C_ int MPI_Group_range_excl(MPI_Group group, int arg_1, int arg_2[][3], MPI_Group *arg_3) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Group_range_excl(group, arg_1, arg_2, arg_3), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Group_range_incl ================== */
_EXTERN_C_ int PMPI_Group_range_incl(MPI_Group group, int arg_1, int arg_2[][3], MPI_Group *arg_3);
_EXTERN_C_ int MPI_Group_range_incl(MPI_Group group, int arg_1, int arg_2[][3], MPI_Group *arg_3) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Group_range_incl(group, arg_1, arg_2, arg_3), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Group_rank ================== */
_EXTERN_C_ int PMPI_Group_rank(MPI_Group group, int *arg_1);
_EXTERN_C_ int MPI_Group_rank(MPI_Group group, int *arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Group_rank(group, arg_1), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Group_size ================== */
_EXTERN_C_ int PMPI_Group_size(MPI_Group group, int *arg_1);
_EXTERN_C_ int MPI_Group_size(MPI_Group group, int *arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Group_size(group, arg_1), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Group_translate_ranks ================== */
_EXTERN_C_ int PMPI_Group_translate_ranks(MPI_Group arg_0, int arg_1, nin_mpi_const int *arg_2, MPI_Group arg_3, int *arg_4);
_EXTERN_C_ int MPI_Group_translate_ranks(MPI_Group arg_0, int arg_1, nin_mpi_const int *arg_2, MPI_Group arg_3, int *arg_4) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Group_translate_ranks(arg_0, arg_1, arg_2, arg_3, arg_4), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Group_union ================== */
_EXTERN_C_ int PMPI_Group_union(MPI_Group arg_0, MPI_Group arg_1, MPI_Group *arg_2);
_EXTERN_C_ int MPI_Group_union(MPI_Group arg_0, MPI_Group arg_1, MPI_Group *arg_2) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Group_union(arg_0, arg_1, arg_2), __func__);
    return _wrap_py_return_val;
}



/* ================== C Wrappers for MPI_Info_create ================== */
_EXTERN_C_ int PMPI_Info_create(MPI_Info *arg_0);
_EXTERN_C_ int MPI_Info_create(MPI_Info *arg_0) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Info_create(arg_0), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Info_delete ================== */
_EXTERN_C_ int PMPI_Info_delete(MPI_Info arg_0, nin_mpi_const char *arg_1);
_EXTERN_C_ int MPI_Info_delete(MPI_Info arg_0, nin_mpi_const char *arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Info_delete(arg_0, arg_1), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Info_dup ================== */
_EXTERN_C_ int PMPI_Info_dup(MPI_Info arg_0, MPI_Info *arg_1);
_EXTERN_C_ int MPI_Info_dup(MPI_Info arg_0, MPI_Info *arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Info_dup(arg_0, arg_1), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Info_free ================== */
_EXTERN_C_ int PMPI_Info_free(MPI_Info *info);
_EXTERN_C_ int MPI_Info_free(MPI_Info *info) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Info_free(info), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Info_get ================== */
_EXTERN_C_ int PMPI_Info_get(MPI_Info arg_0, nin_mpi_const char *arg_1, int arg_2, char *arg_3, int *arg_4);
_EXTERN_C_ int MPI_Info_get(MPI_Info arg_0, nin_mpi_const char *arg_1, int arg_2, char *arg_3, int *arg_4) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Info_get(arg_0, arg_1, arg_2, arg_3, arg_4), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Info_get_nkeys ================== */
_EXTERN_C_ int PMPI_Info_get_nkeys(MPI_Info arg_0, int *arg_1);
_EXTERN_C_ int MPI_Info_get_nkeys(MPI_Info arg_0, int *arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Info_get_nkeys(arg_0, arg_1), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Info_get_nthkey ================== */
_EXTERN_C_ int PMPI_Info_get_nthkey(MPI_Info arg_0, int arg_1, char *arg_2);
_EXTERN_C_ int MPI_Info_get_nthkey(MPI_Info arg_0, int arg_1, char *arg_2) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Info_get_nthkey(arg_0, arg_1, arg_2), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Info_get_valuelen ================== */
_EXTERN_C_ int PMPI_Info_get_valuelen(MPI_Info arg_0, nin_mpi_const char *arg_1, int *arg_2, int *arg_3);
_EXTERN_C_ int MPI_Info_get_valuelen(MPI_Info arg_0, nin_mpi_const char *arg_1, int *arg_2, int *arg_3) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Info_get_valuelen(arg_0, arg_1, arg_2, arg_3), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Info_set ================== */
_EXTERN_C_ int PMPI_Info_set(MPI_Info arg_0, nin_mpi_const char *arg_1, nin_mpi_const char *arg_2);
_EXTERN_C_ int MPI_Info_set(MPI_Info arg_0, nin_mpi_const char *arg_1, nin_mpi_const char *arg_2) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Info_set(arg_0, arg_1, arg_2), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Init ================== */
_EXTERN_C_ int PMPI_Init(int *arg_0, char ***arg_1);
_EXTERN_C_ int MPI_Init(int *arg_0, char ***arg_1) { 
    int _wrap_py_return_val = 0;
    int required = MPI_THREAD_SERIALIZED, provided;
    _wrap_py_return_val = MPI_Init_thread(arg_0, arg_1, required, &provided);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Init_thread ================== */
_EXTERN_C_ int PMPI_Init_thread(int *arg_0, char ***arg_1, int arg_2, int *arg_3);
_EXTERN_C_ int MPI_Init_thread(int *arg_0, char ***arg_1, int arg_2, int *arg_3) { 
    int _wrap_py_return_val = 0;
    int required = MPI_THREAD_SERIALIZED;

    if (required < arg_2) {
      required = arg_2;
    }
    PMPI_WRAP(_wrap_py_return_val = PMPI_Init_thread(arg_0, arg_1, required, arg_3), __func__);
    if (MPI_THREAD_SERIALIZED > *arg_3) {
      NIN_DBG("NIN requires MPI_THREAD_SERIALIZED or higher: required:%d provided:%d (MPI_THREAD_SERIALIZED: %d)", 
	      required, *arg_3, MPI_THREAD_SERIALIZED);
      exit(0);
    }
    nin_init(arg_0, arg_1);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Initialized ================== */
_EXTERN_C_ int PMPI_Initialized(int *arg_0);
_EXTERN_C_ int MPI_Initialized(int *arg_0) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Initialized(arg_0), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Intercomm_create ================== */
_EXTERN_C_ int PMPI_Intercomm_create(MPI_Comm arg_0, int arg_1, MPI_Comm arg_2, int arg_3, int arg_4, MPI_Comm *arg_5);
_EXTERN_C_ int MPI_Intercomm_create(MPI_Comm arg_0, int arg_1, MPI_Comm arg_2, int arg_3, int arg_4, MPI_Comm *arg_5) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Intercomm_create(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Intercomm_merge ================== */
_EXTERN_C_ int PMPI_Intercomm_merge(MPI_Comm arg_0, int arg_1, MPI_Comm *arg_2);
_EXTERN_C_ int MPI_Intercomm_merge(MPI_Comm arg_0, int arg_1, MPI_Comm *arg_2) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Intercomm_merge(arg_0, arg_1, arg_2), __func__);
    return _wrap_py_return_val;
}



/* ================== C Wrappers for MPI_Irecv ================== */
_EXTERN_C_ int PMPI_Irecv(void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6);
_EXTERN_C_ int MPI_Irecv(void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6) { 
    int _wrap_py_return_val = 0;
    //    NIN_DBG("%s called", __func__);
    PMPI_WRAP(_wrap_py_return_val = PMPI_Irecv(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6), __func__);
    //    NIN_DBG("%s ended", __func__);
    return _wrap_py_return_val;
}

#ifdef ENABLE_DEPRECATED_FUNC
/* ================== C Wrappers for MPI_Keyval_create ================== */
_EXTERN_C_ int PMPI_Keyval_create(MPI_Copy_function *arg_0, MPI_Delete_function *arg_1, int *arg_2, void *arg_3);
_EXTERN_C_ int MPI_Keyval_create(MPI_Copy_function *arg_0, MPI_Delete_function *arg_1, int *arg_2, void *arg_3) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Keyval_create(arg_0, arg_1, arg_2, arg_3), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Keyval_free ================== */
_EXTERN_C_ int PMPI_Keyval_free(int *arg_0);
_EXTERN_C_ int MPI_Keyval_free(int *arg_0) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Keyval_free(arg_0), __func__);
    return _wrap_py_return_val;
}
#endif

/* ================== C Wrappers for MPI_Op_create ================== */
_EXTERN_C_ int PMPI_Op_create(MPI_User_function *arg_0, int arg_1, MPI_Op *arg_2);
_EXTERN_C_ int MPI_Op_create(MPI_User_function *arg_0, int arg_1, MPI_Op *arg_2) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Op_create(arg_0, arg_1, arg_2), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Op_free ================== */
_EXTERN_C_ int PMPI_Op_free(MPI_Op *arg_0);
_EXTERN_C_ int MPI_Op_free(MPI_Op *arg_0) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Op_free(arg_0), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Pack ================== */
_EXTERN_C_ int PMPI_Pack(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, void *arg_3, int arg_4, int *arg_5, MPI_Comm arg_6);
_EXTERN_C_ int MPI_Pack(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, void *arg_3, int arg_4, int *arg_5, MPI_Comm arg_6) { 
  int _wrap_py_return_val = 0;
  PMPI_WRAP(_wrap_py_return_val = PMPI_Pack(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6), __func__);
  //_wrap_py_return_val = PMPI_Pack(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Pack_size ================== */
_EXTERN_C_ int PMPI_Pack_size(int arg_0, MPI_Datatype arg_1, MPI_Comm arg_2, int *arg_3);
_EXTERN_C_ int MPI_Pack_size(int arg_0, MPI_Datatype arg_1, MPI_Comm arg_2, int *arg_3) { 
    int _wrap_py_return_val = 0;
    //_wrap_py_return_val = PMPI_Pack_size(arg_0, arg_1, arg_2, arg_3);
    PMPI_WRAP(_wrap_py_return_val = PMPI_Pack_size(arg_0, arg_1, arg_2, arg_3), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Pcontrol ================== */
_EXTERN_C_ int PMPI_Pcontrol(const int arg_0, ...);
_EXTERN_C_ int MPI_Pcontrol(const int arg_0, ...) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Pcontrol(arg_0), __func__);
    ninj_fc_do_model_tuning();
    return _wrap_py_return_val;
}


/* ================== C Wrappers for MPI_Recv ================== */
_EXTERN_C_ int PMPI_Recv(void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Status *arg_6);
_EXTERN_C_ int MPI_Recv(void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Status *arg_6) { 
    int _wrap_py_return_val = 0;
    MPI_Request req;
    //    NIN_DBG("%s  called", __func__);
    _wrap_py_return_val = MPI_Irecv(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, &req);
    _wrap_py_return_val = MPI_Wait(&req, arg_6);
    //NIN_DBG("%s   ended", __func__);
    //    NIN_DBG("%s   received: src: %d", __func__, arg_3);
    //PMPI_WRAP(_wrap_py_return_val = PMPI_Recv(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Recv_init ================== */
_EXTERN_C_ int PMPI_Recv_init(void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6);
_EXTERN_C_ int MPI_Recv_init(void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Recv_init(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Reduce ================== */
_EXTERN_C_ int PMPI_Reduce(nin_mpi_const void *arg_0, void *arg_1, int arg_2, MPI_Datatype arg_3, MPI_Op arg_4, int arg_5, MPI_Comm arg_6);
_EXTERN_C_ int MPI_Reduce(nin_mpi_const void *arg_0, void *arg_1, int arg_2, MPI_Datatype arg_3, MPI_Op arg_4, int arg_5, MPI_Comm arg_6) { 
    int _wrap_py_return_val = 0;
    MPI_Request req;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Ireduce(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, &req), __func__);
    MPI_Wait(&req, MPI_STATUS_IGNORE);
    //    PMPI_WRAP(_wrap_py_return_val = PMPI_Reduce(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Reduce_scatter ================== */
_EXTERN_C_ int PMPI_Reduce_scatter(nin_mpi_const void *arg_0, void *arg_1, nin_mpi_const int *arg_2, MPI_Datatype arg_3, MPI_Op arg_4, MPI_Comm arg_5);
_EXTERN_C_ int MPI_Reduce_scatter(nin_mpi_const void *arg_0, void *arg_1, nin_mpi_const int *arg_2, MPI_Datatype arg_3, MPI_Op arg_4, MPI_Comm arg_5) { 
    int _wrap_py_return_val = 0;
    MPI_Request req;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Ireduce_scatter(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, &req), __func__);
    MPI_Wait(&req, MPI_STATUS_IGNORE);
    //    PMPI_WRAP(_wrap_py_return_val = PMPI_Reduce_scatter(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Register_datarep ================== */
_EXTERN_C_ int PMPI_Register_datarep(nin_mpi_const char *arg_0, MPI_Datarep_conversion_function *arg_1, MPI_Datarep_conversion_function *arg_2, MPI_Datarep_extent_function *arg_3, void *arg_4);
_EXTERN_C_ int MPI_Register_datarep(nin_mpi_const char *arg_0, MPI_Datarep_conversion_function *arg_1, MPI_Datarep_conversion_function *arg_2, MPI_Datarep_extent_function *arg_3, void *arg_4) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Register_datarep(arg_0, arg_1, arg_2, arg_3, arg_4), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Request_free ================== */
_EXTERN_C_ int PMPI_Request_free(MPI_Request *arg_0);
_EXTERN_C_ int MPI_Request_free(MPI_Request *arg_0) { 
    int _wrap_py_return_val = 0;
    /*TODO: free this later*/
    //PMPI_WRAP(_wrap_py_return_val = PMPI_Request_free(arg_0), __func__);
    return _wrap_py_return_val;
}



/* ================== C Wrappers for MPI_Scan ================== */
_EXTERN_C_ int PMPI_Scan(nin_mpi_const void *arg_0, void *arg_1, int arg_2, MPI_Datatype arg_3, MPI_Op arg_4, MPI_Comm arg_5);
_EXTERN_C_ int MPI_Scan(nin_mpi_const void *arg_0, void *arg_1, int arg_2, MPI_Datatype arg_3, MPI_Op arg_4, MPI_Comm arg_5) { 
    int _wrap_py_return_val = 0;
    MPI_Request req;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Iscan(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, &req), __func__);
    MPI_Wait(&req, MPI_STATUS_IGNORE);
    //    PMPI_WRAP(_wrap_py_return_val = PMPI_Scan(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Scatter ================== */
_EXTERN_C_ int PMPI_Scatter(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, void *arg_3, int arg_4, MPI_Datatype arg_5, int arg_6, MPI_Comm arg_7);
_EXTERN_C_ int MPI_Scatter(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, void *arg_3, int arg_4, MPI_Datatype arg_5, int arg_6, MPI_Comm arg_7) { 
    int _wrap_py_return_val = 0;
    MPI_Request req;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Iscatter(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, &req), __func__);
    MPI_Wait(&req, MPI_STATUS_IGNORE);
    //    PMPI_WRAP(_wrap_py_return_val = PMPI_Scatter(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Scatterv ================== */
_EXTERN_C_ int PMPI_Scatterv(nin_mpi_const void *arg_0, nin_mpi_const int *arg_1, nin_mpi_const int *arg_2, MPI_Datatype arg_3, void *arg_4, int arg_5, MPI_Datatype arg_6, int arg_7, MPI_Comm arg_8);
_EXTERN_C_ int MPI_Scatterv(nin_mpi_const void *arg_0, nin_mpi_const int *arg_1, nin_mpi_const int *arg_2, MPI_Datatype arg_3, void *arg_4, int arg_5, MPI_Datatype arg_6, int arg_7, MPI_Comm arg_8) { 
    int _wrap_py_return_val = 0;
    MPI_Request req;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Iscatterv(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8, &req), __func__);
    MPI_Wait(&req, MPI_STATUS_IGNORE);
    //    PMPI_WRAP(_wrap_py_return_val = PMPI_Scatterv(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8), __func__);
    return _wrap_py_return_val;
}



/* ================== C Wrappers for MPI_Status_set_cancelled ================== */
_EXTERN_C_ int PMPI_Status_set_cancelled(MPI_Status *arg_0, int arg_1);
_EXTERN_C_ int MPI_Status_set_cancelled(MPI_Status *arg_0, int arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Status_set_cancelled(arg_0, arg_1), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Status_set_elements ================== */
_EXTERN_C_ int PMPI_Status_set_elements(MPI_Status *arg_0, MPI_Datatype arg_1, int arg_2);
_EXTERN_C_ int MPI_Status_set_elements(MPI_Status *arg_0, MPI_Datatype arg_1, int arg_2) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Status_set_elements(arg_0, arg_1, arg_2), __func__);
    return _wrap_py_return_val;
}



/* ================== C Wrappers for MPI_Topo_test ================== */
_EXTERN_C_ int PMPI_Topo_test(MPI_Comm arg_0, int *arg_1);
_EXTERN_C_ int MPI_Topo_test(MPI_Comm arg_0, int *arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Topo_test(arg_0, arg_1), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Type_commit ================== */
_EXTERN_C_ int PMPI_Type_commit(MPI_Datatype *arg_0);
_EXTERN_C_ int MPI_Type_commit(MPI_Datatype *arg_0) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Type_commit(arg_0), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Type_contiguous ================== */
_EXTERN_C_ int PMPI_Type_contiguous(int arg_0, MPI_Datatype arg_1, MPI_Datatype *arg_2);
_EXTERN_C_ int MPI_Type_contiguous(int arg_0, MPI_Datatype arg_1, MPI_Datatype *arg_2) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Type_contiguous(arg_0, arg_1, arg_2), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Type_create_darray ================== */
_EXTERN_C_ int PMPI_Type_create_darray(int arg_0, int arg_1, int arg_2, nin_mpi_const int *arg_3, nin_mpi_const int *arg_4, nin_mpi_const int *arg_5, nin_mpi_const int *arg_6, int arg_7, MPI_Datatype arg_8, MPI_Datatype *arg_9);
_EXTERN_C_ int MPI_Type_create_darray(int arg_0, int arg_1, int arg_2, nin_mpi_const int *arg_3, nin_mpi_const int *arg_4, nin_mpi_const int *arg_5, nin_mpi_const int *arg_6, int arg_7, MPI_Datatype arg_8, MPI_Datatype *arg_9) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Type_create_darray(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8, arg_9), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Type_create_indexed_block ================== */
_EXTERN_C_ int PMPI_Type_create_indexed_block(int arg_0, int arg_1, nin_mpi_const int *arg_2, MPI_Datatype arg_3, MPI_Datatype *arg_4);
_EXTERN_C_ int MPI_Type_create_indexed_block(int arg_0, int arg_1, nin_mpi_const int *arg_2, MPI_Datatype arg_3, MPI_Datatype *arg_4) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Type_create_indexed_block(arg_0, arg_1, arg_2, arg_3, arg_4), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Type_create_subarray ================== */
_EXTERN_C_ int PMPI_Type_create_subarray(int arg_0, nin_mpi_const int *arg_1, nin_mpi_const int *arg_2, nin_mpi_const int *arg_3, int arg_4, MPI_Datatype arg_5, MPI_Datatype *arg_6);
_EXTERN_C_ int MPI_Type_create_subarray(int arg_0, nin_mpi_const int *arg_1, nin_mpi_const int *arg_2, nin_mpi_const int *arg_3, int arg_4, MPI_Datatype arg_5, MPI_Datatype *arg_6) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Type_create_subarray(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6), __func__);
    return _wrap_py_return_val;
}

#ifdef ENABLE_DEPRECATED_FUNC
/* ================== C Wrappers for MPI_Type_extent ================== */
_EXTERN_C_ int PMPI_Type_extent(MPI_Datatype arg_0, MPI_Aint *arg_1);
_EXTERN_C_ int MPI_Type_extent(MPI_Datatype arg_0, MPI_Aint *arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Type_extent(arg_0, arg_1), __func__);
    return _wrap_py_return_val;
}
#endif

/* ================== C Wrappers for MPI_Type_free ================== */
_EXTERN_C_ int PMPI_Type_free(MPI_Datatype *arg_0);
_EXTERN_C_ int MPI_Type_free(MPI_Datatype *arg_0) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Type_free(arg_0), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Type_get_contents ================== */
_EXTERN_C_ int PMPI_Type_get_contents(MPI_Datatype arg_0, int arg_1, int arg_2, int arg_3, int *arg_4, MPI_Aint *arg_5, MPI_Datatype *arg_6);
_EXTERN_C_ int MPI_Type_get_contents(MPI_Datatype arg_0, int arg_1, int arg_2, int arg_3, int *arg_4, MPI_Aint *arg_5, MPI_Datatype *arg_6) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Type_get_contents(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Type_get_envelope ================== */
_EXTERN_C_ int PMPI_Type_get_envelope(MPI_Datatype arg_0, int *arg_1, int *arg_2, int *arg_3, int *arg_4);
_EXTERN_C_ int MPI_Type_get_envelope(MPI_Datatype arg_0, int *arg_1, int *arg_2, int *arg_3, int *arg_4) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Type_get_envelope(arg_0, arg_1, arg_2, arg_3, arg_4), __func__);
    return _wrap_py_return_val;
}

#ifdef ENABLE_DEPRECATED_FUNC
/* ================== C Wrappers for MPI_Type_hindexed ================== */
_EXTERN_C_ int PMPI_Type_hindexed(int arg_0, int *arg_1, MPI_Aint *arg_2, MPI_Datatype arg_3, MPI_Datatype *arg_4);
_EXTERN_C_ int MPI_Type_hindexed(int arg_0, int *arg_1, MPI_Aint *arg_2, MPI_Datatype arg_3, MPI_Datatype *arg_4) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Type_hindexed(arg_0, arg_1, arg_2, arg_3, arg_4), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Type_hvector ================== */
_EXTERN_C_ int PMPI_Type_hvector(int arg_0, int arg_1, MPI_Aint arg_2, MPI_Datatype arg_3, MPI_Datatype *arg_4);
_EXTERN_C_ int MPI_Type_hvector(int arg_0, int arg_1, MPI_Aint arg_2, MPI_Datatype arg_3, MPI_Datatype *arg_4) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Type_hvector(arg_0, arg_1, arg_2, arg_3, arg_4), __func__);
    return _wrap_py_return_val;
}
#endif

/* ================== C Wrappers for MPI_Type_indexed ================== */
_EXTERN_C_ int PMPI_Type_indexed(int arg_0, nin_mpi_const int *arg_1, nin_mpi_const int *arg_2, MPI_Datatype arg_3, MPI_Datatype *arg_4);
_EXTERN_C_ int MPI_Type_indexed(int arg_0, nin_mpi_const int *arg_1, nin_mpi_const int *arg_2, MPI_Datatype arg_3, MPI_Datatype *arg_4) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Type_indexed(arg_0, arg_1, arg_2, arg_3, arg_4), __func__);
    return _wrap_py_return_val;
}

#ifdef ENABLE_DEPRECATED_FUNC
/* ================== C Wrappers for MPI_Type_lb ================== */
_EXTERN_C_ int PMPI_Type_lb(MPI_Datatype arg_0, MPI_Aint *arg_1);
_EXTERN_C_ int MPI_Type_lb(MPI_Datatype arg_0, MPI_Aint *arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Type_lb(arg_0, arg_1), __func__);
    return _wrap_py_return_val;
}
#endif

/* ================== C Wrappers for MPI_Type_size ================== */
_EXTERN_C_ int PMPI_Type_size(MPI_Datatype arg_0, int *arg_1);
_EXTERN_C_ int MPI_Type_size(MPI_Datatype arg_0, int *arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Type_size(arg_0, arg_1), __func__);
    //_wrap_py_return_val = PMPI_Type_size(arg_0, arg_1);
    return _wrap_py_return_val;
}

#ifdef ENABLE_DEPRECATED_FUNC
/* ================== C Wrappers for MPI_Type_struct ================== */
_EXTERN_C_ int PMPI_Type_struct(int arg_0, int *arg_1, MPI_Aint *arg_2, MPI_Datatype *arg_3, MPI_Datatype *arg_4);
_EXTERN_C_ int MPI_Type_struct(int arg_0, int *arg_1, MPI_Aint *arg_2, MPI_Datatype *arg_3, MPI_Datatype *arg_4) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Type_struct(arg_0, arg_1, arg_2, arg_3, arg_4), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Type_ub ================== */
_EXTERN_C_ int PMPI_Type_ub(MPI_Datatype arg_0, MPI_Aint *arg_1);
_EXTERN_C_ int MPI_Type_ub(MPI_Datatype arg_0, MPI_Aint *arg_1) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Type_ub(arg_0, arg_1), __func__);
    return _wrap_py_return_val;
}
#endif

/* ================== C Wrappers for MPI_Type_vector ================== */
_EXTERN_C_ int PMPI_Type_vector(int arg_0, int arg_1, int arg_2, MPI_Datatype arg_3, MPI_Datatype *arg_4);
_EXTERN_C_ int MPI_Type_vector(int arg_0, int arg_1, int arg_2, MPI_Datatype arg_3, MPI_Datatype *arg_4) { 
    int _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Type_vector(arg_0, arg_1, arg_2, arg_3, arg_4), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Unpack ================== */
_EXTERN_C_ int PMPI_Unpack(nin_mpi_const void *arg_0, int arg_1, int *arg_2, void *arg_3, int arg_4, MPI_Datatype arg_5, MPI_Comm arg_6);
_EXTERN_C_ int MPI_Unpack(nin_mpi_const void *arg_0, int arg_1, int *arg_2, void *arg_3, int arg_4, MPI_Datatype arg_5, MPI_Comm arg_6) { 
    int _wrap_py_return_val = 0;
    //_wrap_py_return_val = PMPI_Unpack(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6);
    PMPI_WRAP(_wrap_py_return_val = PMPI_Unpack(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6), __func__);
    return _wrap_py_return_val;
}



/* ================== C Wrappers for MPI_Wtick ================== */
_EXTERN_C_ double PMPI_Wtick();
_EXTERN_C_ double MPI_Wtick() { 
    double _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Wtick(), __func__);
    return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Wtime ================== */
_EXTERN_C_ double PMPI_Wtime();
_EXTERN_C_ double MPI_Wtime() { 
    double _wrap_py_return_val = 0;
    PMPI_WRAP(_wrap_py_return_val = PMPI_Wtime(), __func__);
    return _wrap_py_return_val;
}



