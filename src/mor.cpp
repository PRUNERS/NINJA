#include <mpi.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>

#include <vector>
#include <unordered_map>
#include <algorithm>

//#include "x86_64-gcc-rdtsc.h"
#include "mpi-wtime.h"
#include "mst_io.h"
#include "nin_util.h"
#include "nin_mpi_util.h"

#ifndef _EXTERN_C_
#ifdef __cplusplus
#define _EXTERN_C_ extern "C"
#else /* __cplusplus */
#define _EXTERN_C_
#endif /* __cplusplus */
#endif /* _EXTERN_C_ */

#define TMP_REQUEST_LEN (256)
#define MOR_WRITE_TAG  (1515)


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

MPI_Comm mor_comm;
int mor_next_comm_id = 1;
char mor_comm_name[256];
int mor_my_rank;
int mor_size;
char *mor_bin_name;
MPI_Request tmp_request[TMP_REQUEST_LEN];

#define RECORD_RANK (0)

#define NODELAY

#ifdef NODELAY
#define DELAY
#else
#define DELAY \
  do { \
    usleep(5000);\ 
  } while(0) 
#endif



class msg_id {
public:
  int src;
  int tag;
  int comm_id;
  int id;
};

vector<unordered_map<int, int>*> id_counter;
vector<msg_id> recv_record;
unordered_map<MPI_Request, MPI_Comm> recv_request_to_comm;


static void mor_set_comm_id(MPI_Comm comm)
{
  mor_comm_name[0] = (char)mor_next_comm_id++;
  PMPI_Comm_set_name(comm, mor_comm_name);
  return;
}

static int mor_get_comm_id(MPI_Comm comm)
{
  int len;
  if (comm == NULL) {
    NIN_DBG("comm is NULL");
    exit(1);
    assert(0);
  }
  PMPI_Comm_get_name(comm, mor_comm_name, &len);
  return (int)mor_comm_name[0];
}

static void mor_init(int *arg_0, char ***arg_1)
{
  PMPI_Comm_dup(MPI_COMM_WORLD, &mor_comm);
  PMPI_Comm_rank(mor_comm, &mor_my_rank);
  PMPI_Comm_size(mor_comm, &mor_size);
  mor_bin_name = basename((*arg_1)[0]);

  NIN_Init();

  memset(mor_comm_name, 0, 256);
  mor_set_comm_id(MPI_COMM_WORLD);
  id_counter.resize(mor_size);
  for (int i = 0; i < mor_size; i++) {
    id_counter[i] = new unordered_map<int, int>();
  }
  return;
}

static void mor_record_recv(int src, int tag, MPI_Comm comm)
{
  msg_id id;
  unordered_map<int, int> *idc;
  //  if (mor_my_rank != 0) return;

  if (src != MPI_ANY_SOURCE && tag != MPI_ANY_TAG && comm != NULL) {
    id.src = src;
    id.tag = tag;
    id.comm_id = mor_get_comm_id(comm);
    idc = id_counter[src];
    if (idc->find(tag) == idc->end()) {
      (*idc)[tag]  = 0;
    }
    id.id = idc->at(tag);
    recv_record.push_back(id);
    idc->at(tag)++;
  }
  return;
}

static void mor_record_recvs(int outcount, int *matched_request_indices, MPI_Request *requests, MPI_Status *statuses)
{
  MPI_Comm comm;
  MPI_Request req;
  for (int i = 0; i < outcount; i++) {
    int matched_index = (matched_request_indices == NULL)? i:matched_request_indices[i];
    // if (mor_my_rank == 1) NIN_DBG("comm: %p,  req: %p, index; %d s:%d, t:%d",
    // 				  recv_request_to_comm[requests[matched_index]], requests[matched_index], matched_index,
    // 				  statuses[i].MPI_SOURCE, statuses[i].MPI_TAG);
    req  =	    requests[matched_index];
    comm = 	    recv_request_to_comm[req];
    //    NIN_DBGI(0, "check: %p (i:%d) %p (source:%d tag:%d)", req, i, MPI_REQUEST_NULL, statuses[i].MPI_SOURCE, statuses[i].MPI_TAG);
    mor_record_recv(statuses[i].MPI_SOURCE, statuses[i].MPI_TAG, comm);
  }
  return;
}

static void mor_copy_request(int count, MPI_Request *requests)
{
  if (count > TMP_REQUEST_LEN) {
    NIN_DBG("Request length (%d) exceed limit: %d", count, TMP_REQUEST_LEN);
    exit(1); 
  }
  for (int i = 0; i < count; i++) {
    tmp_request[i] = requests[i];
  }
  return;
}

static void register_recv_request(MPI_Request request, MPI_Comm comm)
{
  //  NIN_DBGI(0, "register: %p", request);
  recv_request_to_comm[request] = comm;
  return;
}

static void deregister_recv_request(MPI_Request request)
{
  recv_request_to_comm.erase(request);
  //  NIN_DBGI(0, "deregister: %p", request);
  return;
}

static void gather_write_record(vector<msg_id> &rtrace_vec)
{
  char path[256];
  char line[256];
  int fd;
  MPI_Request req;
  MPI_Status stat;
  int count;
  msg_id *send_buf, *recv_buf;
  size_t wsize = 0;

  send_buf = (msg_id*)malloc(sizeof(msg_id)       * rtrace_vec.size());
  for (int i = 0; i < (int)rtrace_vec.size(); i++) {
    send_buf[i] = rtrace_vec[i];
  }


  PMPI_Isend(send_buf, sizeof(msg_id) * rtrace_vec.size(), MPI_BYTE,     0, MOR_WRITE_TAG, mor_comm, &req);

  if (mor_my_rank != 0) goto end;

#ifdef RECORD_RANK
  sprintf(path, "%s.%d-%d.mor", mor_bin_name, getpid(), RECORD_RANK);
#else
  sprintf(path, "%s.%d.mor", mor_bin_name, getpid());
#endif
  fd = mst_open(path, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
  sprintf(line, "procs: %d\n", mor_size);
  mst_write(path, fd, line, strlen(line));
  wsize += strlen(line);
  sprintf(line, "------------------ \n");
  mst_write(path, fd, line, strlen(line));
  wsize += strlen(line);
  sprintf(line, "<rank> <src> <tag> <comm_id> <id>\n");
  mst_write(path, fd, line, strlen(line));
  wsize += strlen(line);
  for (int rank = 0; rank < mor_size; rank++) {
    PMPI_Probe(rank, MOR_WRITE_TAG, mor_comm, &stat);
    PMPI_Get_count(&stat, MPI_BYTE, &count);
    //NIN_DBG("rank: %d: length: %lu", rank ,count/sizeof(msg_id));
    recv_buf = (msg_id*)malloc(count);
    PMPI_Recv(recv_buf, count, MPI_BYTE, rank, MOR_WRITE_TAG, mor_comm, MPI_STATUS_IGNORE);
#ifdef RECORD_RANK
    if (rank == RECORD_RANK) {
      for (int i = 0; i < count/sizeof(msg_id); i++) {
	sprintf(line, "%d %d %d %d %d\n", rank, recv_buf[i].src, recv_buf[i].tag, recv_buf[i].comm_id, recv_buf[i].id);
	mst_write(path, fd, line, strlen(line));
	wsize += strlen(line);
      }
    }
#else 
    for (int i = 0; i < count/sizeof(msg_id); i++) {
      sprintf(line, "%d %d %d %d %d\n", rank, recv_buf[i].src, recv_buf[i].tag, recv_buf[i].comm_id, recv_buf[i].id);
      mst_write(path, fd, line, strlen(line));
      wsize += strlen(line);
    }
#endif
    free(recv_buf);
  }
  mst_close(path, fd);
  NIN_DBG("Trace has been written to %s (size: %lu KB)", path, wsize/1000);

 end:
  MPI_Wait(&req, MPI_STATUS_IGNORE);
  return;

}


/* ================== C Wrappers for MPI_Ibsend ================== */
_EXTERN_C_ int PMPI_Ibsend(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6);
_EXTERN_C_ int MPI_Ibsend(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6) { 
  int _wrap_py_return_val = 0;
  _wrap_py_return_val = PMPI_Ibsend(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6);
  DELAY;
  return _wrap_py_return_val;
}


/* ================== C Wrappers for MPI_Irsend ================== */
_EXTERN_C_ int PMPI_Irsend(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6);
_EXTERN_C_ int MPI_Irsend(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6) { 
  int _wrap_py_return_val = 0;
  _wrap_py_return_val = PMPI_Irsend(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6);
  DELAY;
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Isend ================== */
_EXTERN_C_ int PMPI_Isend(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6);
_EXTERN_C_ int MPI_Isend(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6) { 
  int _wrap_py_return_val = 0;
  _wrap_py_return_val = PMPI_Isend(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6);
  DELAY;
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Issend ================== */
_EXTERN_C_ int PMPI_Issend(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6);
_EXTERN_C_ int MPI_Issend(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6) { 
  int _wrap_py_return_val = 0;
  _wrap_py_return_val = PMPI_Issend(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6);
  DELAY;
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
_EXTERN_C_ int PMPI_Rsend(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5);
_EXTERN_C_ int MPI_Rsend(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5) { 
  int _wrap_py_return_val = 0;
  _wrap_py_return_val = PMPI_Rsend(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5);
  return _wrap_py_return_val;
}


/* ================== C Wrappers for MPI_Send ================== */
_EXTERN_C_ int PMPI_Send(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5);
_EXTERN_C_ int MPI_Send(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5) { 
  int _wrap_py_return_val = 0;
  _wrap_py_return_val = PMPI_Send(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Sendrecv ================== */
_EXTERN_C_ int PMPI_Sendrecv(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, void *arg_5, int arg_6, MPI_Datatype arg_7, int arg_8, int arg_9, MPI_Comm arg_10, MPI_Status *arg_11);
_EXTERN_C_ int MPI_Sendrecv(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, void *arg_5, int arg_6, MPI_Datatype arg_7, int arg_8, int arg_9, MPI_Comm arg_10, MPI_Status *arg_11) { 
  int _wrap_py_return_val = 0;
  _wrap_py_return_val = PMPI_Sendrecv(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8, arg_9, arg_10, arg_11);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Sendrecv_replace ================== */
_EXTERN_C_ int PMPI_Sendrecv_replace(void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, int arg_5, int arg_6, MPI_Comm arg_7, MPI_Status *arg_8);
_EXTERN_C_ int MPI_Sendrecv_replace(void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, int arg_5, int arg_6, MPI_Comm arg_7, MPI_Status *arg_8) { 
  int _wrap_py_return_val = 0;
  _wrap_py_return_val = PMPI_Sendrecv_replace(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Ssend ================== */
_EXTERN_C_ int PMPI_Ssend(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5);
_EXTERN_C_ int MPI_Ssend(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5) { 
  int _wrap_py_return_val = 0;
  _wrap_py_return_val = PMPI_Ssend(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5);
  return _wrap_py_return_val;
}


/* ================== C Wrappers for MPI_Bsend ================== */
_EXTERN_C_ int PMPI_Bsend(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5);
_EXTERN_C_ int MPI_Bsend(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5) { 
  int _wrap_py_return_val = 0;
  _wrap_py_return_val = PMPI_Bsend(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5);
  return _wrap_py_return_val;
}


/* ================== C Wrappers for MPI_Bsend_init ================== */
_EXTERN_C_ int PMPI_Bsend_init(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6);
_EXTERN_C_ int MPI_Bsend_init(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6) { 
  int _wrap_py_return_val = 0;
  _wrap_py_return_val = PMPI_Bsend_init(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Rsend_init ================== */
_EXTERN_C_ int PMPI_Rsend_init(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6);
_EXTERN_C_ int MPI_Rsend_init(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6) { 
  int _wrap_py_return_val = 0;
  _wrap_py_return_val = PMPI_Rsend_init(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Send_init ================== */
_EXTERN_C_ int PMPI_Send_init(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6);
_EXTERN_C_ int MPI_Send_init(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6) { 
  int _wrap_py_return_val = 0;
  _wrap_py_return_val = PMPI_Send_init(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Ssend_init ================== */
_EXTERN_C_ int PMPI_Ssend_init(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6);
_EXTERN_C_ int MPI_Ssend_init(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6) { 
  int _wrap_py_return_val = 0;
  _wrap_py_return_val = PMPI_Ssend_init(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Start ================== */
_EXTERN_C_ int PMPI_Start(MPI_Request *arg_0);
_EXTERN_C_ int MPI_Start(MPI_Request *arg_0) { 
  int _wrap_py_return_val = 0;
  _wrap_py_return_val = PMPI_Start(arg_0);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Startall ================== */
_EXTERN_C_ int PMPI_Startall(int arg_0, MPI_Request *arg_1);
_EXTERN_C_ int MPI_Startall(int arg_0, MPI_Request *arg_1) { 
  int _wrap_py_return_val = 0;
  _wrap_py_return_val = PMPI_Startall(arg_0, arg_1);
  return _wrap_py_return_val;
}


/* ================== C Wrappers for MPI_Test ================== */
_EXTERN_C_ int PMPI_Test(MPI_Request *arg_0, int *arg_1, MPI_Status *arg_2);
_EXTERN_C_ int MPI_Test(MPI_Request *arg_0, int *arg_1, MPI_Status *arg_2) { 
  int _wrap_py_return_val = 0;
  int flag;
  arg_2 = nin_status_allocate(1, arg_2, &flag);
  mor_copy_request(1, arg_0);
  _wrap_py_return_val = PMPI_Test(arg_0, arg_1, arg_2);
  if (*arg_1) mor_record_recvs(1, NULL, tmp_request, arg_2);
  if (flag) nin_status_free(arg_2);
  DELAY;
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Testall ================== */
_EXTERN_C_ int PMPI_Testall(int arg_0, MPI_Request *arg_1, int *arg_2, MPI_Status *arg_3);
_EXTERN_C_ int MPI_Testall(int arg_0, MPI_Request *arg_1, int *arg_2, MPI_Status *arg_3) { 
  int _wrap_py_return_val = 0;
  int flag;
  arg_3 = nin_status_allocate(arg_0, arg_3, &flag);
  mor_copy_request(arg_0, arg_1);
  _wrap_py_return_val = PMPI_Testall(arg_0, arg_1, arg_2, arg_3);
  if (*arg_2) mor_record_recvs(arg_0, NULL, tmp_request, arg_3);
  if (flag) nin_status_free(arg_3);
  DELAY;
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Testany ================== */
_EXTERN_C_ int PMPI_Testany(int arg_0, MPI_Request *arg_1, int *arg_2, int *arg_3, MPI_Status *arg_4);
_EXTERN_C_ int MPI_Testany(int arg_0, MPI_Request *arg_1, int *arg_2, int *arg_3, MPI_Status *arg_4) { 
  int _wrap_py_return_val = 0;
  int flag;
  arg_4 = nin_status_allocate(1, arg_4, &flag);
  mor_copy_request(arg_0, arg_1);
  _wrap_py_return_val = PMPI_Testany(arg_0, arg_1, arg_2, arg_3, arg_4);
  if (*arg_3) mor_record_recvs(1, arg_2, tmp_request, arg_4);
  if (flag) nin_status_free(arg_4);
  DELAY;
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Testsome ================== */
_EXTERN_C_ int PMPI_Testsome(int arg_0, MPI_Request *arg_1, int *arg_2, int *arg_3, MPI_Status *arg_4);
_EXTERN_C_ int MPI_Testsome(int arg_0, MPI_Request *arg_1, int *arg_2, int *arg_3, MPI_Status *arg_4) { 
  int _wrap_py_return_val = 0;
  int flag;
  arg_4 = nin_status_allocate(arg_0, arg_4, &flag);  
  mor_copy_request(arg_0, arg_1);
  _wrap_py_return_val = PMPI_Testsome(arg_0, arg_1, arg_2, arg_3, arg_4);
  if (*arg_2 > 0) mor_record_recvs(*arg_2, arg_3, tmp_request, arg_4);
  if (flag) nin_status_free(arg_4);
  DELAY;
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Wait ================== */
_EXTERN_C_ int PMPI_Wait(MPI_Request *arg_0, MPI_Status *arg_1);
_EXTERN_C_ int MPI_Wait(MPI_Request *arg_0, MPI_Status *arg_1) { 
  int _wrap_py_return_val = 0;
  int flag;
  arg_1 = nin_status_allocate(1, arg_1, &flag);
  mor_copy_request(1, arg_0);
  //  NIN_DBG("---> before status: %d", arg_1->MPI_SOURCE);
  _wrap_py_return_val = PMPI_Wait(arg_0, arg_1);
  //  NIN_DBG("---> after  status: %d", arg_1->MPI_SOURCE);
  mor_record_recvs(1, NULL, tmp_request, arg_1);
  if (flag) nin_status_free(arg_1);
  DELAY;
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Waitall ================== */
_EXTERN_C_ int PMPI_Waitall(int arg_0, MPI_Request *arg_1, MPI_Status *arg_2);
_EXTERN_C_ int MPI_Waitall(int arg_0, MPI_Request *arg_1, MPI_Status *arg_2) { 
  int _wrap_py_return_val = 0;
  int flag;
  arg_2 = nin_status_allocate(arg_0, arg_2, &flag);
  mor_copy_request(arg_0, arg_1);
  _wrap_py_return_val = PMPI_Waitall(arg_0, arg_1, arg_2);
  mor_record_recvs(arg_0, NULL, tmp_request, arg_2);
  if (flag) nin_status_free(arg_2);
  DELAY;
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Waitany ================== */
_EXTERN_C_ int PMPI_Waitany(int arg_0, MPI_Request *arg_1, int *arg_2, MPI_Status *arg_3);
_EXTERN_C_ int MPI_Waitany(int arg_0, MPI_Request *arg_1, int *arg_2, MPI_Status *arg_3) { 
  int _wrap_py_return_val = 0;
  int flag;
  arg_3 = nin_status_allocate(1, arg_3, &flag);
  mor_copy_request(arg_0, arg_1);
  _wrap_py_return_val = PMPI_Waitany(arg_0, arg_1, arg_2, arg_3);
  mor_record_recvs(1, arg_2, tmp_request, arg_3);
  if (flag) nin_status_free(arg_3);
  DELAY;
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Waitsome ================== */
_EXTERN_C_ int PMPI_Waitsome(int arg_0, MPI_Request *arg_1, int *arg_2, int *arg_3, MPI_Status *arg_4);
_EXTERN_C_ int MPI_Waitsome(int arg_0, MPI_Request *arg_1, int *arg_2, int *arg_3, MPI_Status *arg_4) { 
  int _wrap_py_return_val = 0;
  int flag;
  arg_4 = nin_status_allocate(arg_0, arg_4, &flag);
  mor_copy_request(arg_0, arg_1);
  _wrap_py_return_val = PMPI_Waitsome(arg_0, arg_1, arg_2, arg_3, arg_4);
  mor_record_recvs(*arg_2, arg_3, tmp_request, arg_4);
  if (flag) nin_status_free(arg_4);
  DELAY;
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Init ================== */
_EXTERN_C_ int PMPI_Init(int *arg_0, char ***arg_1);
_EXTERN_C_ int MPI_Init(int *arg_0, char ***arg_1) { 
  int _wrap_py_return_val = 0;
  _wrap_py_return_val = PMPI_Init(arg_0, arg_1);
  mor_init(arg_0, arg_1);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Init_thread ================== */
_EXTERN_C_ int PMPI_Init_thread(int *arg_0, char ***arg_1, int arg_2, int *arg_3);
_EXTERN_C_ int MPI_Init_thread(int *arg_0, char ***arg_1, int arg_2, int *arg_3) { 
  int _wrap_py_return_val = 0;
  _wrap_py_return_val = PMPI_Init_thread(arg_0, arg_1, arg_2, arg_3);
  mor_init(arg_0, arg_1);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Iprobe ================== */
_EXTERN_C_ int PMPI_Iprobe(int arg_0, int arg_1, MPI_Comm arg_2, int *flag, MPI_Status *arg_4);
_EXTERN_C_ int MPI_Iprobe(int arg_0, int arg_1, MPI_Comm arg_2, int *flag, MPI_Status *arg_4) { 
  int _wrap_py_return_val = 0;
  _wrap_py_return_val = PMPI_Iprobe(arg_0, arg_1, arg_2, flag, arg_4);
  DELAY;
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Irecv ================== */
_EXTERN_C_ int PMPI_Irecv(void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6);
_EXTERN_C_ int MPI_Irecv(void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6) { 
  int _wrap_py_return_val = 0;
  _wrap_py_return_val = PMPI_Irecv(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6);
  register_recv_request(*arg_6, arg_5);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Probe ================== */
_EXTERN_C_ int PMPI_Probe(int arg_0, int arg_1, MPI_Comm arg_2, MPI_Status *arg_3);
_EXTERN_C_ int MPI_Probe(int arg_0, int arg_1, MPI_Comm arg_2, MPI_Status *arg_3) { 
  int _wrap_py_return_val = 0;
  _wrap_py_return_val = PMPI_Probe(arg_0, arg_1, arg_2, arg_3);
  DELAY;
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Recv ================== */
_EXTERN_C_ int PMPI_Recv(void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Status *arg_6);
_EXTERN_C_ int MPI_Recv(void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Status *arg_6) { 
  int _wrap_py_return_val = 0;
  int flag;
  arg_6 = nin_status_allocate(1, arg_6, &flag);
  _wrap_py_return_val = PMPI_Recv(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6);
  mor_record_recv(arg_6->MPI_SOURCE, arg_6->MPI_TAG, arg_5);
  if (flag) nin_status_free(arg_6);
  DELAY;
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Finalize ================== */
_EXTERN_C_ int PMPI_Finalize();
_EXTERN_C_ int MPI_Finalize() { 
  int _wrap_py_return_val = 0;
  gather_write_record(recv_record);
  _wrap_py_return_val = PMPI_Finalize();
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
  mor_set_comm_id(*arg_1);
  return _wrap_py_return_val;
}


/* ================== C Wrappers for MPI_Comm_split ================== */
_EXTERN_C_ int PMPI_Comm_split(MPI_Comm arg_0, int arg_1, int arg_2, MPI_Comm *arg_3);
_EXTERN_C_ int MPI_Comm_split(MPI_Comm arg_0, int arg_1, int arg_2, MPI_Comm *arg_3) {
  int _wrap_py_return_val = 0;
  _wrap_py_return_val = PMPI_Comm_split(arg_0, arg_1, arg_2, arg_3);
  mor_set_comm_id(*arg_3);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Recv_init ================== */
_EXTERN_C_ int PMPI_Recv_init(void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6);
_EXTERN_C_ int MPI_Recv_init(void *arg_0, int arg_1, MPI_Datatype arg_2, int arg_3, int arg_4, MPI_Comm arg_5, MPI_Request *arg_6) { 
  int _wrap_py_return_val = 0;
  _wrap_py_return_val = PMPI_Recv_init(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6);
  register_recv_request(*arg_6, arg_5);
  return _wrap_py_return_val;
}

/* ================== C Wrappers for MPI_Request_free ================== */
_EXTERN_C_ int PMPI_Request_free(MPI_Request *arg_0);
_EXTERN_C_ int MPI_Request_free(MPI_Request *arg_0) { 
  int _wrap_py_return_val = 0;
  _wrap_py_return_val = PMPI_Request_free(arg_0);
  deregister_recv_request(*arg_0);
  return _wrap_py_return_val;
}



/* ================== C Wrappers for MPI_Cancel ================== */
_EXTERN_C_ int PMPI_Cancel(MPI_Request *arg_0);
_EXTERN_C_ int MPI_Cancel(MPI_Request *arg_0) { 
  int _wrap_py_return_val = 0;
  _wrap_py_return_val = PMPI_Cancel(arg_0);
  deregister_recv_request(*arg_0);
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
_EXTERN_C_ int PMPI_Allgather(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, void *arg_3, int arg_4, MPI_Datatype arg_5, MPI_Comm arg_6);
_EXTERN_C_ int MPI_Allgather(nin_mpi_const void *arg_0, int arg_1, MPI_Datatype arg_2, void *arg_3, int arg_4, MPI_Datatype arg_5, MPI_Comm arg_6) { 
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
_EXTERN_C_ int PMPI_Scatterv(nin_mpi_const void *arg_0, int *arg_1, int *arg_2, MPI_Datatype arg_3, void *arg_4, int arg_5, MPI_Datatype arg_6, int arg_7, MPI_Comm arg_8);
_EXTERN_C_ int MPI_Scatterv(nin_mpi_const void *arg_0, int *arg_1, int *arg_2, MPI_Datatype arg_3, void *arg_4, int arg_5, MPI_Datatype arg_6, int arg_7, MPI_Comm arg_8) { 
  int _wrap_py_return_val = 0;

  _wrap_py_return_val = PMPI_Scatterv(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8);
  return _wrap_py_return_val;
}
#endif
