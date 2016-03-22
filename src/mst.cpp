#include <mpi.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <vector>
#include <unordered_map>
#include <algorithm>

//#include "x86_64-gcc-rdtsc.h"
#include "mpi-wtime.h"
#include "mst_io.h"

#ifndef _EXTERN_C_
#ifdef __cplusplus
#define _EXTERN_C_ extern "C"
#else /* __cplusplus */
#define _EXTERN_C_
#endif /* __cplusplus */
#endif /* _EXTERN_C_ */

#define MST_RANK_TAG (0)
#define MST_TS_TAG   (1)

#define MST_DBG(format, ...) \
  do { \
  fprintf(stderr, "MST:%3d: " format " (%s:%d)\n", my_rank, ## __VA_ARGS__, __FILE__, __LINE__); \
  } while (0)


#if MPI_VERSION == 1 || MPI_VERSION == 2
#define mst_mpi_void void
#define mst_mpi_int  int
#else
#define mst_mpi_void const void
#define mst_mpi_int  const int
#endif

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




using namespace std;

class send_trace_t
{
public:
  int rank;
  double timestamp;

  send_trace_t(int rank, double timestamp)
    : rank(rank)
    , timestamp(timestamp)
  {}
  bool operator < (const send_trace_t st) const
  {
    return (timestamp < st.timestamp);
  }
};

bool comp(const send_trace_t *st1, const send_trace_t *st2) 
{
  return (st1->timestamp < st2->timestamp);
}

int my_rank;
int commworld_size;

char *bin_name;

unordered_map<int, vector<HRT_TIMESTAMP_T>*> send_traces_umap;
unordered_map<MPI_Request, int> persistent_send_request_umap;

HRT_TIMESTAMP_T ts;
HRT_TIMESTAMP_T delta_min = -1, delta_max = -1;
HRT_TIMESTAMP_T delta_min_uint64, delta_max_uint64;

vector<send_trace_t*> recv_traces_vec;
MPI_Comm mst_comm;

static void error_estimation()
{
  int notsmaller = -1, notbigger = -1;
  HRT_TIMESTAMP_T min_ts, max_ts, delta;
  while (notsmaller < 100 && notbigger < 100) {
    PMPI_Barrier(mst_comm);
    HRT_GET_TIMESTAMP(ts);
    PMPI_Allreduce(&ts, &min_ts, 1, MPI_DOUBLE, MPI_MIN, mst_comm);
    PMPI_Allreduce(&ts, &max_ts, 1, MPI_DOUBLE, MPI_MAX, mst_comm);
    delta = max_ts - min_ts;
    if (delta_min > delta || delta_min == -1) {
      delta_min = delta;
      notsmaller = 0;
    } else {
      notsmaller++;
    }
    if (delta_max < delta || delta_max == -1) {
      delta_max = delta;
      notbigger = 0;
    } else {
      notbigger++;
    }
  }
  PMPI_Barrier(mst_comm);
  // HRT_GET_TIME(delta_min, delta_min_uint64);
  // HRT_GET_TIME(delta_max, delta_max_uint64);
  if (my_rank == 0) MST_DBG("min: %f, max: %f", delta_min, delta_max);
}

static void record_send(int dest)
{
  HRT_TIMESTAMP_T etime;
  if (send_traces_umap.find(dest) == send_traces_umap.end()) {
    send_traces_umap[dest] = new vector<HRT_TIMESTAMP_T>;
  }
  HRT_GET_TIMESTAMP(ts);
  HRT_GET_TIMED(ts, etime);
  send_traces_umap[dest]->push_back(etime);
  return;
}

static void register_request(MPI_Request request, int dest)
{
  persistent_send_request_umap[request] = dest;
  return;
}

static int get_dest(MPI_Request request)
{
  int dest = -1;
  if (persistent_send_request_umap.find(request) != persistent_send_request_umap.end()) {
    dest = persistent_send_request_umap[request];
  }
  return dest;
}

static void alltoall_record(vector<send_trace_t*> &rtrace_vec)
{
  vector<HRT_TIMESTAMP_T> *send_traces_vec;
  HRT_TIMESTAMP_T *sendbuf = NULL, *recvbuf = NULL;

  int *sendcounts = (int*)malloc(sizeof(int) * commworld_size);
  int *sdispls    = (int*)malloc(sizeof(int) * commworld_size);
  int stotal_count;
  int offset = 0;

  //  MST_DBG("size: %lu", send_traces_umap.size());
  for (int rank = 0; rank < commworld_size; rank++) {
    if (send_traces_umap.find(rank) != send_traces_umap.end()) {
      send_traces_vec = send_traces_umap[rank];
      // if (sendcounts == NULL || send_traces_vec == NULL) {
      // 	MST_DBG("dest: %d, %p %p", rank, sendcounts, send_traces_vec);
      // 	sleep(100);
      // }
      sendcounts[rank] = send_traces_vec->size();
      sdispls[rank]    = offset;
      offset += sendcounts[rank];
      // for (int i = 0; i < sendcounts[rank]; i++) {
      // 	MST_DBG("dest: %d, time: %f", rank, send_traces_vec->at(i));
      // }
    } else {
      sendcounts[rank] = 0;
      sdispls[rank]    = 0;
    }
  }
  stotal_count = offset;
  if (stotal_count != 0) {
    sendbuf = (HRT_TIMESTAMP_T*)malloc(sizeof(HRT_TIMESTAMP_T) * stotal_count);
  }
  offset = 0;
  for (int rank = 0; rank < commworld_size; rank++) {
    send_traces_vec = send_traces_umap[rank];
    for (int i = 0; i < sendcounts[rank]; i++) {
      offset = sdispls[rank];
      sendbuf[offset + i] = send_traces_vec->at(i);
    }
  }

  int *recvcounts = (int*)malloc(sizeof(int) * commworld_size);
  int *rdispls    = (int*)malloc(sizeof(int) * commworld_size);
  int rtotal_count = 0;
  MPI_Alltoall(sendcounts, 1, MPI_INT, recvcounts, 1, MPI_INT, mst_comm);
  offset = 0;
  for (int rank = 0; rank < commworld_size; rank++) {
    rtotal_count += recvcounts[rank];
    rdispls[rank] = offset;
    offset += recvcounts[rank];
  }

  if (rtotal_count != 0) {
    recvbuf = (HRT_TIMESTAMP_T*)malloc(sizeof(HRT_TIMESTAMP_T) * rtotal_count);
  }
  // MST_DBG("recvbuf: %p", recvbuf);


  // usleep((my_rank + 1) * 1000);
  // for (int i = 0; i < commworld_size; i++) {
  //   MST_DBG("index: %d: scount: %d, soffset: %d", i, sendcounts[i], sdispls[i]);
  // }
  // for (int i = 0; i < commworld_size; i++) {

  //   MST_DBG("index: %d: rcount: %d, roffset: %d", i, recvcounts[i], rdispls[i]);
  // }
  // MST_DBG("stcount: %d, rtcount: %d", stotal_count, rtotal_count);

  MPI_Alltoallv(sendbuf, sendcounts, sdispls, MPI_DOUBLE,
  		recvbuf, recvcounts, rdispls, MPI_DOUBLE,
  		mst_comm);

  usleep((my_rank + 1) * 10000);
  offset = 0;
  for (int rank = 0; rank < commworld_size; rank++) {
    int rcount = recvcounts[rank];
    for (int i = 0; i < rcount; i++) {
      send_trace_t *st  = new send_trace_t(rank, recvbuf[offset + i]);
      rtrace_vec.push_back(st);
    }
    offset += rcount;
  }

  // for (int i = 0; i < (int)rtrace_vec.size(); i++) {
  //   if (my_rank == 1) MST_DBG("time: %f, src: %d", rtrace_vec[i]->timestamp, rtrace_vec[i]->rank);
  // }
  sort(rtrace_vec.begin(), rtrace_vec.end(), &comp);
  // for (int i = 0; i < (int)rtrace_vec.size(); i++) {
  //   if (my_rank == 1) MST_DBG("time: %f, src: %d", rtrace_vec[i]->timestamp, rtrace_vec[i]->rank);
  // }

  // MST_DBG("recvbuf: %p", recvbuf);
  if (sendbuf != NULL) free(sendbuf);
  if (recvbuf != NULL) free(recvbuf);
  free(sendcounts);
  free(sdispls);
  free(recvcounts);
  free(rdispls);
  //  MST_DBG("done");
  return;
}

static void gather_write_record(vector<send_trace_t*> &rtrace_vec)
{
  char path[256];
  char line[256];
  int fd;
  MPI_Request req[2];
  MPI_Status stat;
  int count;
  int *send_rank_buf, *recv_rank_buf;
  double *send_ts_buf, *recv_ts_buf;

  send_rank_buf = (int*)malloc(sizeof(int)       * rtrace_vec.size());
  send_ts_buf   = (double*)malloc(sizeof(double) * rtrace_vec.size());
  for (int i = 0; i < rtrace_vec.size(); i++) {
    send_rank_buf[i] = rtrace_vec[i]->rank;
    send_ts_buf[i]   = rtrace_vec[i]->timestamp;
  }

  PMPI_Isend(send_rank_buf, rtrace_vec.size(), MPI_INT,     0, MST_RANK_TAG, mst_comm, &req[0]);
  PMPI_Isend(send_ts_buf,   rtrace_vec.size(), MPI_DOUBLE, 0, MST_TS_TAG  , mst_comm, &req[1]);
   
  if (my_rank != 0) goto end;

  sprintf(path, "%s.%d.mst", bin_name, getpid());
  fd = mst_open(path, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
  sprintf(line, "procs: %d\n", commworld_size);
  mst_write(path, fd, line, strlen(line));
  sprintf(line, "delta min: %f, delta max: %f \n", delta_min, delta_max);
  mst_write(path, fd, line, strlen(line));
  sprintf(line, "------------------ \n");
  mst_write(path, fd, line, strlen(line));
  sprintf(line, "<rank> <timestamp> <src> \n");
  mst_write(path, fd, line, strlen(line));
  for (int rank = 0; rank < commworld_size; rank++) {
    PMPI_Probe(rank, MST_RANK_TAG, mst_comm, &stat);
    PMPI_Get_count(&stat, MPI_INT, &count);
    recv_rank_buf = (int*)malloc(sizeof(int) * count);
    PMPI_Recv(recv_rank_buf, count, MPI_INT, rank, MST_RANK_TAG, mst_comm, MPI_STATUS_IGNORE);
    PMPI_Probe(rank, MST_TS_TAG, mst_comm, &stat);
    PMPI_Get_count(&stat, MPI_DOUBLE, &count);
    recv_ts_buf = (double*)malloc(sizeof(double) *count);
    PMPI_Recv(recv_ts_buf, count, MPI_DOUBLE, rank, MST_TS_TAG, mst_comm, MPI_STATUS_IGNORE);
    for (int i = 0; i < count; i++) {
      sprintf(line, "%d %f %d\n", rank, recv_ts_buf[i], recv_rank_buf[i]);
      mst_write(path, fd, line, strlen(line));
    }
    free(recv_rank_buf);
    free(recv_ts_buf);
  }
  mst_close(path, fd);
  MST_DBG("Trace written to %s", path);

 end:
  MPI_Waitall(2, req, MPI_STATUS_IGNORE);
  free(send_rank_buf);
  free(send_ts_buf);
  return;

}



/* ================== C Wrappers for MPI_Ibsend ================== */
_EXTERN_C_ int PMPI_Ibsend(mst_mpi_void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6);
_EXTERN_C_ int MPI_Ibsend(mst_mpi_void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6) { 
  int _wrap_py_return_val = 0;
  record_send(arg_3);
  _wrap_py_return_val = PMPI_Ibsend(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6);
  return _wrap_py_return_val;
}


/* ================== C Wrappers for MPI_Irsend ================== */
_EXTERN_C_ int PMPI_Irsend(mst_mpi_void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6);
_EXTERN_C_ int MPI_Irsend(mst_mpi_void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6) { 
  int _wrap_py_return_val = 0;
  record_send(arg_3);
  _wrap_py_return_val = PMPI_Irsend(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Isend ================== */
_EXTERN_C_ int PMPI_Isend(mst_mpi_void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6);
_EXTERN_C_ int MPI_Isend(mst_mpi_void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6) { 
  int _wrap_py_return_val = 0;
  record_send(arg_3);
  _wrap_py_return_val = PMPI_Isend(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Issend ================== */
_EXTERN_C_ int PMPI_Issend(mst_mpi_void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6);
_EXTERN_C_ int MPI_Issend(mst_mpi_void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6) { 
  int _wrap_py_return_val = 0;
  record_send(arg_3);
  _wrap_py_return_val = PMPI_Issend(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Pcontrol ================== */
_EXTERN_C_ int PMPI_Pcontrol(const int arg_0, ...);
_EXTERN_C_ int MPI_Pcontrol(const int arg_0, ...) { 
  int _wrap_py_return_val = 0;
  _wrap_py_return_val = PMPI_Pcontrol(arg_0);
  return _wrap_py_return_val;
}


/* ================== C Wrappers for MPI_Rsend ================== */
_EXTERN_C_ int PMPI_Rsend(mst_mpi_void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5);
_EXTERN_C_ int MPI_Rsend(mst_mpi_void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5) { 
  int _wrap_py_return_val = 0;
  record_send(arg_3);
  _wrap_py_return_val = PMPI_Rsend(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5);
  return _wrap_py_return_val;
}


/* ================== C Wrappers for MPI_Send ================== */
_EXTERN_C_ int PMPI_Send(mst_mpi_void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5);
_EXTERN_C_ int MPI_Send(mst_mpi_void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5) { 
  int _wrap_py_return_val = 0;
  record_send(arg_3);
  _wrap_py_return_val = PMPI_Send(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Sendrecv ================== */
_EXTERN_C_ int PMPI_Sendrecv(mst_mpi_void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, void *arg_5, int arg_6, MPI_Datatype arg_7, int arg_8, int arg_9, MPI_Comm arg_10, MPI_Status *arg_11);
_EXTERN_C_ int MPI_Sendrecv(mst_mpi_void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, void *arg_5, int arg_6, MPI_Datatype arg_7, int arg_8, int arg_9, MPI_Comm arg_10, MPI_Status *arg_11) { 
  int _wrap_py_return_val = 0;
  record_send(arg_3);
  _wrap_py_return_val = PMPI_Sendrecv(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8, arg_9, arg_10, arg_11);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Sendrecv_replace ================== */
_EXTERN_C_ int PMPI_Sendrecv_replace(void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, int arg_5, int arg_6, MPI_Comm arg_7, MPI_Status *arg_8);
_EXTERN_C_ int MPI_Sendrecv_replace(void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, int arg_5, int arg_6, MPI_Comm arg_7, MPI_Status *arg_8) { 
  int _wrap_py_return_val = 0;
  record_send(arg_3);
  _wrap_py_return_val = PMPI_Sendrecv_replace(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Ssend ================== */
_EXTERN_C_ int PMPI_Ssend(mst_mpi_void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5);
_EXTERN_C_ int MPI_Ssend(mst_mpi_void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5) { 
  int _wrap_py_return_val = 0;
  record_send(arg_3);
  _wrap_py_return_val = PMPI_Ssend(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5);
  return _wrap_py_return_val;
}


/* ================== C Wrappers for MPI_Bsend ================== */
_EXTERN_C_ int PMPI_Bsend(mst_mpi_void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5);
_EXTERN_C_ int MPI_Bsend(mst_mpi_void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5) { 
  int _wrap_py_return_val = 0;
  record_send(arg_3);
  _wrap_py_return_val = PMPI_Bsend(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5);
  return _wrap_py_return_val;
}


/* ================== C Wrappers for MPI_Bsend_init ================== */
_EXTERN_C_ int PMPI_Bsend_init(mst_mpi_void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6);
_EXTERN_C_ int MPI_Bsend_init(mst_mpi_void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6) { 
  int _wrap_py_return_val = 0;
  _wrap_py_return_val = PMPI_Bsend_init(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6);
  register_request(*arg_6, arg_3);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Rsend_init ================== */
_EXTERN_C_ int PMPI_Rsend_init(mst_mpi_void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6);
_EXTERN_C_ int MPI_Rsend_init(mst_mpi_void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6) { 
  int _wrap_py_return_val = 0;
  _wrap_py_return_val = PMPI_Rsend_init(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6);
  register_request(*arg_6, arg_3);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Send_init ================== */
_EXTERN_C_ int PMPI_Send_init(mst_mpi_void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6);
_EXTERN_C_ int MPI_Send_init(mst_mpi_void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6) { 
  int _wrap_py_return_val = 0;
  _wrap_py_return_val = PMPI_Send_init(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6);
  register_request(*arg_6, arg_3);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Ssend_init ================== */
_EXTERN_C_ int PMPI_Ssend_init(mst_mpi_void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6);
_EXTERN_C_ int MPI_Ssend_init(mst_mpi_void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6) { 
  int _wrap_py_return_val = 0;
  _wrap_py_return_val = PMPI_Ssend_init(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6);
  register_request(*arg_6, arg_3);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Start ================== */
_EXTERN_C_ int PMPI_Start(MPI_Request *arg_0);
_EXTERN_C_ int MPI_Start(MPI_Request *arg_0) { 
  int _wrap_py_return_val = 0;
  int dest;
  if((dest = get_dest(*arg_0)) >= 0) {
    record_send(dest);
  }
  _wrap_py_return_val = PMPI_Start(arg_0);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Startall ================== */
_EXTERN_C_ int PMPI_Startall(int arg_0, MPI_Request *arg_1);
_EXTERN_C_ int MPI_Startall(int arg_0, MPI_Request *arg_1) { 
  int _wrap_py_return_val = 0;
  for (int i = 0; i < arg_0; i++) {
    int dest;
    if((dest = get_dest(arg_1[i])) >= 0) {
      record_send(dest);
    }
  }
  _wrap_py_return_val = PMPI_Startall(arg_0, arg_1);
  return _wrap_py_return_val;
}





/* ================== C Wrappers for MPI_Test ================== */
_EXTERN_C_ int PMPI_Test(MPI_Request *arg_0, int *arg_1, MPI_Status *arg_2);
_EXTERN_C_ int MPI_Test(MPI_Request *arg_0, int *arg_1, MPI_Status *arg_2) { 
  int _wrap_py_return_val = 0;

  _wrap_py_return_val = PMPI_Test(arg_0, arg_1, arg_2);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Testall ================== */
_EXTERN_C_ int PMPI_Testall(int arg_0, MPI_Request *arg_1, int *arg_2, MPI_Status *arg_3);
_EXTERN_C_ int MPI_Testall(int arg_0, MPI_Request *arg_1, int *arg_2, MPI_Status *arg_3) { 
  int _wrap_py_return_val = 0;

  _wrap_py_return_val = PMPI_Testall(arg_0, arg_1, arg_2, arg_3);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Testany ================== */
_EXTERN_C_ int PMPI_Testany(int arg_0, MPI_Request *arg_1, int *arg_2, int *arg_3, MPI_Status *arg_4);
_EXTERN_C_ int MPI_Testany(int arg_0, MPI_Request *arg_1, int *arg_2, int *arg_3, MPI_Status *arg_4) { 
  int _wrap_py_return_val = 0;

  _wrap_py_return_val = PMPI_Testany(arg_0, arg_1, arg_2, arg_3, arg_4);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Testsome ================== */
_EXTERN_C_ int PMPI_Testsome(int arg_0, MPI_Request *arg_1, int *arg_2, int *arg_3, MPI_Status *arg_4);
_EXTERN_C_ int MPI_Testsome(int arg_0, MPI_Request *arg_1, int *arg_2, int *arg_3, MPI_Status *arg_4) { 
  int _wrap_py_return_val = 0;
  _wrap_py_return_val = PMPI_Testsome(arg_0, arg_1, arg_2, arg_3, arg_4);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Wait ================== */
_EXTERN_C_ int PMPI_Wait(MPI_Request *arg_0, MPI_Status *arg_1);
_EXTERN_C_ int MPI_Wait(MPI_Request *arg_0, MPI_Status *arg_1) { 
  int _wrap_py_return_val = 0;

  _wrap_py_return_val = PMPI_Wait(arg_0, arg_1);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Waitall ================== */
_EXTERN_C_ int PMPI_Waitall(int arg_0, MPI_Request *arg_1, MPI_Status *arg_2);
_EXTERN_C_ int MPI_Waitall(int arg_0, MPI_Request *arg_1, MPI_Status *arg_2) { 
  int _wrap_py_return_val = 0;

  _wrap_py_return_val = PMPI_Waitall(arg_0, arg_1, arg_2);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Waitany ================== */
_EXTERN_C_ int PMPI_Waitany(int arg_0, MPI_Request *arg_1, int *arg_2, MPI_Status *arg_3);
_EXTERN_C_ int MPI_Waitany(int arg_0, MPI_Request *arg_1, int *arg_2, MPI_Status *arg_3) { 
  int _wrap_py_return_val = 0;

  _wrap_py_return_val = PMPI_Waitany(arg_0, arg_1, arg_2, arg_3);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Waitsome ================== */
_EXTERN_C_ int PMPI_Waitsome(int arg_0, MPI_Request *arg_1, int *arg_2, int *arg_3, MPI_Status *arg_4);
_EXTERN_C_ int MPI_Waitsome(int arg_0, MPI_Request *arg_1, int *arg_2, int *arg_3, MPI_Status *arg_4) { 
  int _wrap_py_return_val = 0;

  _wrap_py_return_val = PMPI_Waitsome(arg_0, arg_1, arg_2, arg_3, arg_4);
  return _wrap_py_return_val;
}

static void mst_init(int *arg_0, char ***arg_1)
{
  PMPI_Comm_dup(MPI_COMM_WORLD, &mst_comm);
  PMPI_Comm_rank(mst_comm, &my_rank);
  PMPI_Comm_size(mst_comm, &commworld_size);
  bin_name = basename((*arg_1)[0]);
  //  sprintf(bin_name, "%s", (*arg_1)[0]);
  int freq;
  HRT_INIT(0, freq);
  error_estimation();
  return;
}

/* ================== C Wrappers for MPI_Init ================== */
_EXTERN_C_ int PMPI_Init(int *arg_0, char ***arg_1);
_EXTERN_C_ int MPI_Init(int *arg_0, char ***arg_1) { 
  int _wrap_py_return_val = 0;
  _wrap_py_return_val = PMPI_Init(arg_0, arg_1);
  mst_init(arg_0, arg_1);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Init_thread ================== */
_EXTERN_C_ int PMPI_Init_thread(int *arg_0, char ***arg_1, int arg_2, int *arg_3);
_EXTERN_C_ int MPI_Init_thread(int *arg_0, char ***arg_1, int arg_2, int *arg_3) { 
  int _wrap_py_return_val = 0;
  _wrap_py_return_val = PMPI_Init_thread(arg_0, arg_1, arg_2, arg_3);
  mst_init(arg_0, arg_1);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Iprobe ================== */
_EXTERN_C_ int PMPI_Iprobe(int arg_0, int arg_1, MPI_Comm arg_2, int *flag, MPI_Status *arg_4);
_EXTERN_C_ int MPI_Iprobe(int arg_0, int arg_1, MPI_Comm arg_2, int *flag, MPI_Status *arg_4) { 
  int _wrap_py_return_val = 0;

  _wrap_py_return_val = PMPI_Iprobe(arg_0, arg_1, arg_2, flag, arg_4);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Irecv ================== */
_EXTERN_C_ int PMPI_Irecv(void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6);
_EXTERN_C_ int MPI_Irecv(void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6) { 
  int _wrap_py_return_val = 0;

  _wrap_py_return_val = PMPI_Irecv(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Probe ================== */
_EXTERN_C_ int PMPI_Probe(int arg_0, int arg_1, MPI_Comm arg_2, MPI_Status *arg_3);
_EXTERN_C_ int MPI_Probe(int arg_0, int arg_1, MPI_Comm arg_2, MPI_Status *arg_3) { 
  int _wrap_py_return_val = 0;

  _wrap_py_return_val = PMPI_Probe(arg_0, arg_1, arg_2, arg_3);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Recv ================== */
_EXTERN_C_ int PMPI_Recv(void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Status *arg_6);
_EXTERN_C_ int MPI_Recv(void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Status *arg_6) { 
  int _wrap_py_return_val = 0;

  _wrap_py_return_val = PMPI_Recv(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Finalize ================== */
_EXTERN_C_ int PMPI_Finalize();
_EXTERN_C_ int MPI_Finalize() { 
  int _wrap_py_return_val = 0;
  alltoall_record(recv_traces_vec);
  gather_write_record(recv_traces_vec);
  _wrap_py_return_val = PMPI_Finalize();
  return _wrap_py_return_val;
}






#if 0

/* ================== C Wrappers for MPI_Abort ================== */
_EXTERN_C_ int PMPI_Abort(MPI_Comm arg_0, int arg_1);
_EXTERN_C_ int MPI_Abort(MPI_Comm arg_0, int arg_1) { 
  int _wrap_py_return_val = 0;

  _wrap_py_return_val = PMPI_Abort(arg_0, arg_1);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Allgather ================== */
_EXTERN_C_ int PMPI_Allgather(mst_mpi_void *arg_0, int arg_1, MPI_Datatype arg_2, void *arg_3, int arg_4, MPI_Datatype arg_5, MPI_Comm arg_6);
_EXTERN_C_ int MPI_Allgather(mst_mpi_void *arg_0, int arg_1, MPI_Datatype arg_2, void *arg_3, int arg_4, MPI_Datatype arg_5, MPI_Comm arg_6) { 
  int _wrap_py_return_val = 0;

  _wrap_py_return_val = PMPI_Allgather(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Allgatherv ================== */
_EXTERN_C_ int PMPI_Allgatherv(void *arg_0, int arg_1, MPI_Datatype arg_2, void *arg_3, int *arg_4, int *arg_5, MPI_Datatype arg_6, MPI_Comm arg_7);
_EXTERN_C_ int MPI_Allgatherv(void *arg_0, int arg_1, MPI_Datatype arg_2, void *arg_3, int *arg_4, int *arg_5, MPI_Datatype arg_6, MPI_Comm arg_7) { 
  int _wrap_py_return_val = 0;

  _wrap_py_return_val = PMPI_Allgatherv(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Allreduce ================== */
_EXTERN_C_ int PMPI_Allreduce(void *arg_0, void *arg_1, int arg_2, MPI_Datatype arg_3, MPI_Op arg_4, MPI_Comm arg_5);
_EXTERN_C_ int MPI_Allreduce(void *arg_0, void *arg_1, int arg_2, MPI_Datatype arg_3, MPI_Op arg_4, MPI_Comm arg_5) { 
  int _wrap_py_return_val = 0;

  _wrap_py_return_val = PMPI_Allreduce(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Alltoall ================== */
_EXTERN_C_ int PMPI_Alltoall(void *arg_0, int arg_1, MPI_Datatype arg_2, void *arg_3, int arg_4, MPI_Datatype arg_5, MPI_Comm arg_6);
_EXTERN_C_ int MPI_Alltoall(void *arg_0, int arg_1, MPI_Datatype arg_2, void *arg_3, int arg_4, MPI_Datatype arg_5, MPI_Comm arg_6) { 
  int _wrap_py_return_val = 0;

  _wrap_py_return_val = PMPI_Alltoall(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Alltoallv ================== */
_EXTERN_C_ int PMPI_Alltoallv(void *arg_0, int *arg_1, int *arg_2, MPI_Datatype arg_3, void *arg_4, int *arg_5, int *arg_6, MPI_Datatype arg_7, MPI_Comm arg_8);
_EXTERN_C_ int MPI_Alltoallv(void *arg_0, int *arg_1, int *arg_2, MPI_Datatype arg_3, void *arg_4, int *arg_5, int *arg_6, MPI_Datatype arg_7, MPI_Comm arg_8) { 
  int _wrap_py_return_val = 0;

  _wrap_py_return_val = PMPI_Alltoallv(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Barrier ================== */
_EXTERN_C_ int PMPI_Barrier(MPI_Comm arg_0);
_EXTERN_C_ int MPI_Barrier(MPI_Comm arg_0) { 
  int _wrap_py_return_val = 0;

  _wrap_py_return_val = PMPI_Barrier(arg_0);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Bcast ================== */
_EXTERN_C_ int PMPI_Bcast(void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, MPI_Comm arg_4);
_EXTERN_C_ int MPI_Bcast(void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, MPI_Comm arg_4) { 
  int _wrap_py_return_val = 0;

  _wrap_py_return_val = PMPI_Bcast(arg_0, arg_1, arg_2, arg_3, arg_4);
  return _wrap_py_return_val;
}


/* ================== C Wrappers for MPI_Cancel ================== */
_EXTERN_C_ int PMPI_Cancel(MPI_Request *arg_0);
_EXTERN_C_ int MPI_Cancel(MPI_Request *arg_0) { 
  int _wrap_py_return_val = 0;

  _wrap_py_return_val = PMPI_Cancel(arg_0);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Comm_create ================== */
_EXTERN_C_ int PMPI_Comm_create(MPI_Comm arg_0, MPI_Group arg_1, MPI_Comm *arg_2);
_EXTERN_C_ int MPI_Comm_create(MPI_Comm arg_0, MPI_Group arg_1, MPI_Comm *arg_2) { 
  int _wrap_py_return_val = 0;

  _wrap_py_return_val = PMPI_Comm_create(arg_0, arg_1, arg_2);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Comm_dup ================== */
_EXTERN_C_ int PMPI_Comm_dup(MPI_Comm arg_0, MPI_Comm *arg_1);
_EXTERN_C_ int MPI_Comm_dup(MPI_Comm arg_0, MPI_Comm *arg_1) { 
  int _wrap_py_return_val = 0;

  _wrap_py_return_val = PMPI_Comm_dup(arg_0, arg_1);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Comm_free ================== */
_EXTERN_C_ int PMPI_Comm_free(MPI_Comm *arg_0);
_EXTERN_C_ int MPI_Comm_free(MPI_Comm *arg_0) { 
  int _wrap_py_return_val = 0;

  _wrap_py_return_val = PMPI_Comm_free(arg_0);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Comm_rank ================== */
_EXTERN_C_ int PMPI_Comm_rank(MPI_Comm arg_0, int *arg_1);
_EXTERN_C_ int MPI_Comm_rank(MPI_Comm arg_0, int *arg_1) { 
  int _wrap_py_return_val = 0;

  _wrap_py_return_val = PMPI_Comm_rank(arg_0, arg_1);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Comm_size ================== */
_EXTERN_C_ int PMPI_Comm_size(MPI_Comm arg_0, int *arg_1);
_EXTERN_C_ int MPI_Comm_size(MPI_Comm arg_0, int *arg_1) { 
  int _wrap_py_return_val = 0;

  _wrap_py_return_val = PMPI_Comm_size(arg_0, arg_1);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Comm_split ================== */
_EXTERN_C_ int PMPI_Comm_split(MPI_Comm arg_0, int arg_1, int arg_2, MPI_Comm *arg_3);
_EXTERN_C_ int MPI_Comm_split(MPI_Comm arg_0, int arg_1, int arg_2, MPI_Comm *arg_3) { 
  int _wrap_py_return_val = 0;

  _wrap_py_return_val = PMPI_Comm_split(arg_0, arg_1, arg_2, arg_3);
  return _wrap_py_return_val;
}



/* ================== C Wrappers for MPI_Gather ================== */
_EXTERN_C_ int PMPI_Gather(void *arg_0, int arg_1, MPI_Datatype arg_2, void *arg_3, int arg_4, MPI_Datatype arg_5, int arg_6, MPI_Comm arg_7);
_EXTERN_C_ int MPI_Gather(void *arg_0, int arg_1, MPI_Datatype arg_2, void *arg_3, int arg_4, MPI_Datatype arg_5, int arg_6, MPI_Comm arg_7) { 
  int _wrap_py_return_val = 0;

  _wrap_py_return_val = PMPI_Gather(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Gatherv ================== */
_EXTERN_C_ int PMPI_Gatherv(void *arg_0, int arg_1, MPI_Datatype arg_2, void *arg_3, int *arg_4, int *arg_5, MPI_Datatype arg_6, int arg_7, MPI_Comm arg_8);
_EXTERN_C_ int MPI_Gatherv(void *arg_0, int arg_1, MPI_Datatype arg_2, void *arg_3, int *arg_4, int *arg_5, MPI_Datatype arg_6, int arg_7, MPI_Comm arg_8) { 
  int _wrap_py_return_val = 0;

  _wrap_py_return_val = PMPI_Gatherv(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8);
  return _wrap_py_return_val;
}


/* ================== C Wrappers for MPI_Recv_init ================== */
_EXTERN_C_ int PMPI_Recv_init(void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6);
_EXTERN_C_ int MPI_Recv_init(void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6) { 
  int _wrap_py_return_val = 0;

  _wrap_py_return_val = PMPI_Recv_init(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Reduce ================== */
_EXTERN_C_ int PMPI_Reduce(void *arg_0, void *arg_1, int arg_2, MPI_Datatype arg_3, MPI_Op arg_4, int arg_5, MPI_Comm arg_6);
_EXTERN_C_ int MPI_Reduce(void *arg_0, void *arg_1, int arg_2, MPI_Datatype arg_3, MPI_Op arg_4, int arg_5, MPI_Comm arg_6) { 
  int _wrap_py_return_val = 0;

  _wrap_py_return_val = PMPI_Reduce(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Reduce_scatter ================== */
_EXTERN_C_ int PMPI_Reduce_scatter(void *arg_0, void *arg_1, int *arg_2, MPI_Datatype arg_3, MPI_Op arg_4, MPI_Comm arg_5);
_EXTERN_C_ int MPI_Reduce_scatter(void *arg_0, void *arg_1, int *arg_2, MPI_Datatype arg_3, MPI_Op arg_4, MPI_Comm arg_5) { 
  int _wrap_py_return_val = 0;

  _wrap_py_return_val = PMPI_Reduce_scatter(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Request_free ================== */
_EXTERN_C_ int PMPI_Request_free(MPI_Request *arg_0);
_EXTERN_C_ int MPI_Request_free(MPI_Request *arg_0) { 
  int _wrap_py_return_val = 0;

  _wrap_py_return_val = PMPI_Request_free(arg_0);
  return _wrap_py_return_val;
}


/* ================== C Wrappers for MPI_Scan ================== */
_EXTERN_C_ int PMPI_Scan(void *arg_0, void *arg_1, int arg_2, MPI_Datatype arg_3, MPI_Op arg_4, MPI_Comm arg_5);
_EXTERN_C_ int MPI_Scan(void *arg_0, void *arg_1, int arg_2, MPI_Datatype arg_3, MPI_Op arg_4, MPI_Comm arg_5) { 
  int _wrap_py_return_val = 0;

  _wrap_py_return_val = PMPI_Scan(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Scatter ================== */
_EXTERN_C_ int PMPI_Scatter(void *arg_0, int arg_1, MPI_Datatype arg_2, void *arg_3, int arg_4, MPI_Datatype arg_5, int arg_6, MPI_Comm arg_7);
_EXTERN_C_ int MPI_Scatter(void *arg_0, int arg_1, MPI_Datatype arg_2, void *arg_3, int arg_4, MPI_Datatype arg_5, int arg_6, MPI_Comm arg_7) { 
  int _wrap_py_return_val = 0;

  _wrap_py_return_val = PMPI_Scatter(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Scatterv ================== */
_EXTERN_C_ int PMPI_Scatterv(mst_mpi_void *arg_0, int *arg_1, int *arg_2, MPI_Datatype arg_3, void *arg_4, int arg_5, MPI_Datatype arg_6, int arg_7, MPI_Comm arg_8);
_EXTERN_C_ int MPI_Scatterv(mst_mpi_void *arg_0, int *arg_1, int *arg_2, MPI_Datatype arg_3, void *arg_4, int arg_5, MPI_Datatype arg_6, int arg_7, MPI_Comm arg_8) { 
  int _wrap_py_return_val = 0;

  _wrap_py_return_val = PMPI_Scatterv(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8);
  return _wrap_py_return_val;
}
#endif
