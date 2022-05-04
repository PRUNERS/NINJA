/* ==========================NINJA:LICENSE==========================================   
  Copyright (c) 2016, Lawrence Livermore National Security, LLC.                     
  Produced at the Lawrence Livermore National Laboratory.                            
                                                                                    
  Written by Kento Sato, kento@llnl.gov. LLNL-CODE-713637.                           
  All rights reserved.                                                               
                                                                                    
  This file is part of NINJA. For details, see https://github.com/PRUNER/NINJA      
  Please also see the LICENSE.TXT file for our notice and the LGPL.                      
                                                                                    
  This program is free software; you can redistribute it and/or modify it under the 
  terms of the GNU General Public License (as published by the Free Software         
  Foundation) version 2.1 dated February 1999.                                       
                                                                                    
  This program is distributed in the hope that it will be useful, but WITHOUT ANY    
  WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or                  
  FITNESS FOR A PARTICULAR PURPOSE. See the terms and conditions of the GNU          
  General Public License for more details.                                           
                                                                                    
  You should have received a copy of the GNU Lesser General Public License along     
  with this program; if not, write to the Free Software Foundation, Inc., 59 Temple 
  Place, Suite 330, Boston, MA 02111-1307 USA                                 
  ============================NINJA:LICENSE========================================= */

#include <stdio.h>
#include <mpi.h>
#include <sys/time.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "ninja_test_util.h"
#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include <string.h>

typedef int HYPRE_Int;
typedef double HYPRE_Real;

#define HYPRE_MPI_INT  MPI_INT
#define hypre_MPI_DOUBLE  MPI_DOUBLE
#define hypre_MPI_SUM MPI_SUM
#define hypre_MPI_ANY_SOURCE MPI_ANY_SOURCE
#define hypre_MPI_SOURCE MPI_SOURCE
#define hypre_MPI_Request MPI_Request
#define hypre_MPI_Status MPI_Status

#define hypre_MPI_Probe MPI_Probe
#define hypre_MPI_Wtime MPI_Wtime
#define hypre_MPI_Allreduce MPI_Allreduce
#define hypre_MPI_Comm_rank MPI_Comm_rank
#define hypre_MPI_Comm_size MPI_Comm_size
#define hypre_MPI_Isend MPI_Isend
#define hypre_MPI_Request_free MPI_Request_free
#define hypre_MPI_Get_count MPI_Get_count
#define hypre_MPI_Recv MPI_Recv
#define hypre_MPI_Waitall MPI_Waitall

//#define MSGLEN_ROW    (10 * 1000 * 1000)
#define MSGLEN_ROW    (24576)
#define MSGLEN_STORED (1)
HYPRE_Int sendrequest_vals[MSGLEN_ROW];

//#define ENABLE_PRINT_TIME
//#define ENABLE_NOISE

#ifdef ENABLE_PRINT_TIME
double s, e;
#define PRINT_TIME(func,name)			\
  do { \
  s = get_time();				\
  func;						\
  e = get_time();					\
  if (my_rank == 0) mst_test_dbg_print("func: %s: %f", name, e - s);	\
  } while(0)
#else
#define PRINT_TIME(func,name) \
  do { \
    func; \
  } while(0)
#endif


#define ENABLE_HANG

#define ROW_REQP_TAG        221
#ifdef ENABLE_HANG
#define ROW_REQS_TAG        221
#else
#define ROW_REQS_TAG        222
#endif
#define ROW_REPI_TAG       223
#define ROW_REPV_TAG       224

#define TEST_COMM_SIZE (64)
#define TEST_MAX_ROWS (32)

/* HYPRE_Int dest_list_pruned[][TEST_MAX_ROWS] = { */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1}, */
/*   {0, -1} */
/* }; */

HYPRE_Int dest_list_pruned[][TEST_MAX_ROWS] = {
  {1, 4, 7, 8, 10, 12, 13, 14, 16, 17, 20, 21, 23, -1}, /*0 hangs 1*/
  {0, 3, 4, 5, 7, 10, 12, 13, -1},  /*node 1*/
  {-1},
  {1, 4, 13, 14, 32, 43, -1},
  {0, 1, 3, 5, 6, 7, 12, 13, 17, 18, 19, 20, 21, 22, 23, -1}, /*4 hangs 1*/
  {1, 4, 6, 7, 12, 13, 17, 18, 20, 28, 29, -1},  /*node 1*/
  {4, 5, 7, 18, 28, 29, -1},
  {0, 1, 4, 5, 6, 17, 18, 19, 20, 21, 22, 23, 28, 29, -1},
  {0, 10, 17, -1},
  {-1},  /*node 1*/
  {0, 1, 8, 13, 14, 16, 17, -1}, /*10*/
  {-1},
  {0, 1, 4, 5, 13, 14, 16, 17, 20, 28, 32, 33, 42, 43, 45, -1},
  {0, 1, 3, 4, 5, 10, 12, 14, 32, 43, -1},  /*node 1*/
  {0, 3, 10, 12, 13, 16, 17, 20, 32, 33, 42, 43, 45, -1},
  {-1},
  {0, 10, 12, 14, 17, 20, 30, 32, 38, 45, 50, 54, -1},
  {0, 4, 5, 7, 8, 10, 12, 14, 16, 18, 19, 20, 21, 22, 30, -1},  /*node 1*/
  {4, 5, 6, 7, 17, 19, 29, -1},
  {4, 7, 17, 18, 20, 22, -1},
  {0, 4, 5, 7, 12, 14, 16, 17, 19, 21, 22, 23, 28, 30, 31, 38, 45, -1}, /*20 hangs 1 */
  {0, 4, 7, 17, 20, 22, 23, 30, 31, -1},  /*node 1*/
  {4, 7, 17, 19, 20, 21, 23, -1},
  {0, 4, 7, 20, 21, 22, -1},
  {25, 26, 27, -1},
  {24, 26, 27, 28, 29, 44, 47, -1},  /*node 1*/
  {24, 25, 27, 28, 29, 30, 31, 56, 57, 58, -1},
  {24, 25, 26, 28, 31, -1},
  {5, 6, 7, 12, 20, 25, 26, 27, 29, 30, 31, 44, 45, 50, 51, 56, 58, -1}, /*28 hangs 1*/
  {5, 6, 7, 18, 25, 26, 28, 36, 44, 45, 47, 56, 57, 58, 62, -1},  /*node 1*/
  {16, 17, 20, 21, 26, 28, 31, 38, 45, 50, 54, 56, 58, -1}, /*30*/
  {20, 21, 26, 27, 28, 30, -1},
  {3, 12, 13, 14, 16, 33, 40, 41, 42, 43, 45, 46, 47, 51, -1},
  {12, 14, 32, 34, 36, 38, 40, 42, 43, 44, 45, 46, 51, -1},  /*node 1*/
  {33, 36, 42, 45, 46, -1},
  {-1},
  {29, 33, 34, 38, 44, 45, 46, 47, 56, 62, -1},
  {-1},  /*node 1*/
  {16, 20, 30, 33, 36, 44, 45, 46, 50, 51, 56, 62, -1},
  {-1},
  {32, 33, 41, 42, 43, 46, 47, -1}, /*40*/
  {32, 40, 43, -1},  /*node 1*/
  {12, 14, 32, 33, 34, 40, 43, 45, 46, -1},
  {3, 12, 13, 14, 32, 33, 40, 41, 42, -1},
  {25, 28, 29, 33, 36, 38, 45, 46, 47, 62, -1},
  {12, 14, 16, 20, 28, 29, 30, 32, 33, 34, 36, 38, 42, 44, 46, 50, 51, 56, -1}, /*45 hanges 7 */ /*node 1*/
  {32, 33, 34, 36, 38, 40, 42, 44, 45, 47, -1},
  {25, 29, 32, 36, 40, 44, 46, -1},
  {-1},
  {-1},  /*node 1*/
  {16, 28, 30, 38, 45, 51, 54, 56, 58, 62, -1}, /*50*/
  {28, 32, 33, 38, 45, 50, 56, -1},
  {-1},
  {-1},  /*node 1*/
  {16, 30, 50, -1},
  {-1},
  {26, 28, 29, 30, 36, 38, 45, 50, 51, 57, 58, 62, -1},
  {26, 29, 56, 58, -1},  /*node 1*/
  {26, 28, 29, 30, 50, 56, 57, -1},
  {-1},
  {-1}, /*60*/
  {-1},  /*node 1*/
  {29, 36, 38, 44, 50, 56, -1},
  {-1}
};


HYPRE_Int dest_list_stored[][TEST_MAX_ROWS] = {
  {-1}, /*0*/
  {0, -1},
  {-1},
  {1, -1},
  {-1},
  {0, 1, 4, -1},
  {1, 4, 5, -1},
  {0, 1, 4, 5, 6, -1},
  {0, -1},
  {-1},
  {0, 1, 8, -1}, /*10*/
  {-1},
  {0, 1, 4, 5, -1},
  {0, 1, 3, 4, 5, 10, 12, -1},
  {0, 1, 3, 10, 12, 13, -1},
  {-1},
  {0, 10, 12, 13, 14, -1},
  {0, 1, 4, 5, 7, 8, 10, 12, 14, 16, -1},
  {4, 5, 6, 7, 17, -1},
  {4, 7, 17, 18, -1},
  {0, 1, 4, 5, 7, 12, 13, 14, 16, 17, 19, -1}, /*20*/
  {0, 4, 7, 17, 20, -1},
  {0, 4, 7, 17, 19, 20, 21, -1},
  {0, 4, 7, 17, 20, 21, 22, -1},
  {-1},
  {24, -1},
  {21, 24, 25, -1},
  {24, 25, 26, -1},
  {1, 4, 5, 6, 7, 12, 20, 21, 25, 26, 27, -1},
  {5, 6, 7, 12, 18, 25, 26, 28, -1},
  {0, 14, 16, 17, 20, 21, 26, 28, -1}, /*30*/
  {20, 21, 26, 27, 28, 30, -1},
  {3, 12, 13, 14, 16, -1},
  {12, 13, 14, 16, 32, -1},
  {32, 33, -1},
  {-1},
  {25, 28, 29, 33, 34, -1},
  {-1},
  {12, 14, 16, 20, 29, 30, 33, 36, -1},
  {-1},
  {32, 33, -1}, /*40*/
  {32, 40, -1},
  {12, 13, 14, 32, 33, 34, 40, -1},
  {3, 12, 13, 14, 32, 33, 40, 41, 42, -1},
  {25, 28, 29, 32, 33, 34, 36, 38, 42, -1},
  {12, 13, 14, 16, 20, 26, 28, 29, 30, 32, 33, 34, 36, 38, 40, 42, 44, -1},
  {32, 33, 34, 36, 38, 40, 42, 44, 45, -1},
  {25, 29, 32, 36, 40, 44, 46, -1},
  {-1},
  {-1},
  {16, 20, 26, 28, 29, 30, 36, 38, 45, -1}, /*50*/
  {28, 32, 33, 38, 45, 50, -1},
  {-1},
  {-1},
  {16, 30, 50, -1},
  {-1},
  {20, 26, 28, 29, 30, 36, 38, 44, 45, 50, 51, -1},
  {26, 29, 56, -1},
  {26, 28, 29, 30, 50, 56, 57, -1},
  {-1},
  {-1}, /*60*/
  {-1},
  {29, 36, 38, 44, 50, 56, -1},
  {-1}
};

int my_rank;
int comm_size;

#define LEN (64)
float calibration = 1;
static int a[LEN];
static void do_noise_work(float msec, int bool)
{
#ifdef ENABLE_NOISE
  int i;
  if (bool) {
    for (i = 0; i < msec * calibration; i++) {
      a[i++ % LEN] = 0;
    }
  }
#endif
  return;
}


HYPRE_Int FindNumReplies(MPI_Comm comm, HYPRE_Int *replies_list)
{
    HYPRE_Int num_replies;
    HYPRE_Int npes, mype;
    HYPRE_Int *replies_list2;

    hypre_MPI_Comm_rank(comm, &mype);
    hypre_MPI_Comm_size(comm, &npes);

    replies_list2 = (HYPRE_Int *) malloc(npes * sizeof(HYPRE_Int));

    PRINT_TIME(hypre_MPI_Allreduce(replies_list, replies_list2, npes, HYPRE_MPI_INT, hypre_MPI_SUM, comm), "MPI_Allreduce");
    num_replies = replies_list2[mype];

    free(replies_list2);

    return num_replies;
}


static void SendRequests(MPI_Comm comm, HYPRE_Int msglen, HYPRE_Int *reqind,
			 HYPRE_Int *num_requests, HYPRE_Int *replies_list, HYPRE_Int dest_list[][TEST_MAX_ROWS], int tag)
{
    hypre_MPI_Request request;
    //    HYPRE_Int i, j, this_pe;
    HYPRE_Int i, this_pe;


    *num_requests = 0;

    for (i=0; dest_list[my_rank % TEST_COMM_SIZE][i] >= 0; i++) /* j is set below */
    {
      /* The processor that owns the row with index reqind[i] */
      this_pe = dest_list[my_rank % TEST_COMM_SIZE][i];
      if (this_pe >= comm_size) continue;
      /* Request rows in reqind[i..j-1] */
      
      //      if (my_rank == 63) sleep(1);
      /* mst_test_dbg_print("call send ! %lu", sizeof(HYPRE_MPI_INT)); */
      /* MPI_Request req; */
      /* MPI_Isend(&sendrequest_vals, 64001, MPI_BYTE, this_pe, ROW_REQ_TAG, */
      /* 		comm, &req); */
      /* MPI_Wait(&req, MPI_STATUS_IGNORE); */
      /* mst_test_dbg_print("good !"); */
      /* sleep(100); */
      

      PRINT_TIME(hypre_MPI_Isend(&sendrequest_vals, msglen, HYPRE_MPI_INT, this_pe, tag,
      				 comm, &request), "MPI_Isend 0");
      hypre_MPI_Request_free(&request);
      //MPI_Wait(&request, MPI_STATUS_IGNORE);
      (*num_requests)++;
      
      if (replies_list != NULL)
	replies_list[this_pe] = 1;
    }
}


static void ReceiveRequest(MPI_Comm comm, HYPRE_Int *source, HYPRE_Int **buffer,
			   HYPRE_Int *buflen, HYPRE_Int *count, int tag)
{
    hypre_MPI_Status status;

    PRINT_TIME(hypre_MPI_Probe(hypre_MPI_ANY_SOURCE, tag, comm, &status), "MPI_Probe 0");
    *source = status.hypre_MPI_SOURCE;
    hypre_MPI_Get_count(&status, HYPRE_MPI_INT, count);

    if (*count > *buflen)
    {
        free(*buffer);
        *buflen = *count;
        *buffer = (HYPRE_Int *) malloc(*buflen * sizeof(HYPRE_Int));
    }


    PRINT_TIME(hypre_MPI_Recv(*buffer, *count, HYPRE_MPI_INT, *source, tag, comm, &status), "MPI_Recv 0");
}


static void SendReplyPrunedRows(MPI_Comm comm,  HYPRE_Int dest, 
				HYPRE_Int *buffer, HYPRE_Int count, hypre_MPI_Request *request)
{
  //    HYPRE_Int sendbacksize, j;
    //    HYPRE_Int len, *ind, *indbuf, *indbufp;
    //    HYPRE_Int temp;
    HYPRE_Int val = 0;

    PRINT_TIME(hypre_MPI_Isend(&val, 1, HYPRE_MPI_INT, dest, ROW_REPI_TAG, comm, request), "MPI_Isend 1");
}


static void ReceiveReplyPrunedRows(MPI_Comm comm)
{
    hypre_MPI_Status status;
    HYPRE_Int source, count;
    //    HYPRE_Int len, *ind, num_rows, *row_nums, j;
    HYPRE_Int val;

    /* Don't know the size of reply, so use probe and get count */
    PRINT_TIME(hypre_MPI_Probe(hypre_MPI_ANY_SOURCE, ROW_REPI_TAG, comm, &status), "MPI_Probe 1");
    source = status.hypre_MPI_SOURCE;
    hypre_MPI_Get_count(&status, HYPRE_MPI_INT, &count);
    PRINT_TIME(hypre_MPI_Recv(&val, 1, HYPRE_MPI_INT, source, ROW_REPI_TAG, comm, &status), "MPI_Recv 1");
}


static void SendReplyStoredRows(MPI_Comm comm, HYPRE_Int dest, 
				HYPRE_Int *buffer, HYPRE_Int count, hypre_MPI_Request *request)
{
    HYPRE_Int val;

    PRINT_TIME(hypre_MPI_Isend(&val, 1, HYPRE_MPI_INT, dest, ROW_REPI_TAG,
			      comm, request), "MPI_Isend 2");

    hypre_MPI_Request_free(request);
    //MPI_Wait(request, MPI_STATUS_IGNORE);

    PRINT_TIME(hypre_MPI_Isend(&val, 1, HYPRE_MPI_INT, dest, ROW_REPV_TAG,
			       comm, request), "MPI_Isend 3");
}


static void ReceiveReplyStoredRows(MPI_Comm comm)
{
    hypre_MPI_Status status;
    HYPRE_Int source, count;
    HYPRE_Int val;

    /* Don't know the size of reply, so use probe and get count */
    PRINT_TIME(hypre_MPI_Probe(hypre_MPI_ANY_SOURCE, ROW_REPI_TAG, comm, &status), "MPI_Probe 2");
    source = status.hypre_MPI_SOURCE;
    hypre_MPI_Get_count(&status, HYPRE_MPI_INT, &count);

    /* Allocate space in stored rows data structure */
    PRINT_TIME(hypre_MPI_Recv(&val, 1, HYPRE_MPI_INT, source, ROW_REPI_TAG, comm, &status), "MPI_Recv 2");
    PRINT_TIME(hypre_MPI_Recv(&val, 1, HYPRE_MPI_INT, source, ROW_REPV_TAG, comm, &status), "MPI_Recv 3");
}


static void ExchangePrunedRows(MPI_Comm comm, HYPRE_Int num_levels)
{
  //    HYPRE_Int row, len, *ind;
  //    HYPRE_Int *ind = NULL;

    HYPRE_Int num_requests;
    HYPRE_Int source;

    HYPRE_Int bufferlen;
    HYPRE_Int *buffer;

    HYPRE_Int level;

    HYPRE_Int i;
    HYPRE_Int count;
    hypre_MPI_Request *requests;
    hypre_MPI_Status *statuses;
    HYPRE_Int npes;
    HYPRE_Int num_replies, *replies_list;


    hypre_MPI_Comm_size(comm, &npes);
    requests = (hypre_MPI_Request *) malloc(npes * sizeof(hypre_MPI_Request));
    statuses = (hypre_MPI_Status *) malloc(npes * sizeof(hypre_MPI_Status));

    /* Loop to construct pattern of pruned rows on this processor */
    bufferlen = 10; /* size will grow if get a long msg */
    buffer = (HYPRE_Int *) malloc(bufferlen * sizeof(HYPRE_Int));

    for (level=1; level<=num_levels; level++)
    {

        replies_list = (HYPRE_Int *) calloc(npes, sizeof(HYPRE_Int));
	//	int r = get_rand(1000000);
	int msglen;
	/* if (r % 100000 == 0) { */
	/*   msglen = MSGLEN_ROW; */
	/* } else { */
	  msglen = 1;
	  //	}
	  SendRequests(comm, msglen, NULL, &num_requests, replies_list, dest_list_pruned, ROW_REQP_TAG);
	
	//	mst_test_dbg_print("fundnumreplayes");
        num_replies = FindNumReplies(comm, replies_list);
	//	mst_test_dbg_print("fundnumreplayes end");
        free(replies_list);

        for (i=0; i<num_replies; i++)
        {
            /* Receive count indices stored in buffer */
	  ReceiveRequest(comm, &source, &buffer, &bufferlen, &count, ROW_REQP_TAG);
	    //    mst_test_dbg_print("source: %d", source);
            SendReplyPrunedRows(comm, source, buffer, count, &requests[i]);

        }

        for (i=0; i<num_requests; i++)
        {
            /* Will also merge the pattern of received rows into "patt" */
	  ReceiveReplyPrunedRows(comm);
        }

        PRINT_TIME(hypre_MPI_Waitall(num_replies, requests, statuses), "MPI_Waitall 0");
    }

    free(buffer);
    free(requests);
    free(statuses);
}


static void ExchangeStoredRows(MPI_Comm comm)
{
  //    HYPRE_Int row, len, *ind;
  //    HYPRE_Int *ind = NULL;
    //    HYPRE_Real *val;

    HYPRE_Int num_requests;
    HYPRE_Int source;

    HYPRE_Int bufferlen;
    HYPRE_Int *buffer;

    HYPRE_Int i;
    HYPRE_Int count;
    hypre_MPI_Request *requests = NULL;
    hypre_MPI_Status *statuses = NULL;
    HYPRE_Int npes;
    HYPRE_Int num_replies, *replies_list;

    hypre_MPI_Comm_size(comm, &npes);

    replies_list = (HYPRE_Int *) calloc(npes, sizeof(HYPRE_Int));

    SendRequests(comm, MSGLEN_STORED, NULL, &num_requests, replies_list, dest_list_stored, ROW_REQS_TAG);

    
    //    mst_test_dbg_print("fundnumreplayes 2");
    num_replies = FindNumReplies(comm, replies_list);
    //    mst_test_dbg_print("fundnumreplayes 2 end");


    free(replies_list);

    if (num_replies)
    {
        requests = (hypre_MPI_Request *) malloc(num_replies * sizeof(hypre_MPI_Request));
        statuses = (hypre_MPI_Status *) malloc(num_replies * sizeof(hypre_MPI_Status));
    }

    bufferlen = 10; /* size will grow if get a long msg */
    buffer = (HYPRE_Int *) malloc(bufferlen * sizeof(HYPRE_Int));

    for (i=0; i<num_replies; i++)
    {
        /* Receive count indices stored in buffer */
      ReceiveRequest(comm, &source, &buffer, &bufferlen, &count, ROW_REQS_TAG);
        SendReplyStoredRows(comm, source, buffer, count, &requests[i]);
    }

    for (i=0; i<num_requests; i++)
    {
      ReceiveReplyStoredRows(comm);
    }

    PRINT_TIME(hypre_MPI_Waitall(num_replies, requests, statuses), "MPI_Waitall 1");

    /* Free all send buffers */
    free(buffer);
    free(requests);
    free(statuses);
}


static HYPRE_Real SelectThresh(MPI_Comm comm, HYPRE_Real param)
{
  //    HYPRE_Int row, len, *ind, i, npes;
  HYPRE_Int npes;
    //    HYPRE_Real *val;
    HYPRE_Real localsum = 0.0, sum;
    //    HYPRE_Real temp;

    /* Find the average across all processors */
    hypre_MPI_Allreduce(&localsum, &sum, 1, hypre_MPI_DOUBLE, hypre_MPI_SUM, comm);
    hypre_MPI_Comm_size(comm, &npes);

    return sum;
}

static HYPRE_Real SelectFilter(MPI_Comm comm, HYPRE_Real param, HYPRE_Int symmetric)
{
  //    HYPRE_Int row, len, *ind, i, npes;
  HYPRE_Int npes;
    //    HYPRE_Real *val;
    HYPRE_Real localsum = 0.0, sum;

    /* Find the average across all processors */
    hypre_MPI_Allreduce(&localsum, &sum, 1, hypre_MPI_DOUBLE, hypre_MPI_SUM, comm);
    hypre_MPI_Comm_size(comm, &npes);

    return sum;
}


void ParaSailsDestroy()
{
  return;
}

void ParaSailsSetupPattern(HYPRE_Real thresh, HYPRE_Int num_levels)
{
  HYPRE_Real time0, time1, time;

  time0 = hypre_MPI_Wtime();
  SelectThresh(MPI_COMM_WORLD, 0); /*Allreduce*/
  ExchangePrunedRows(MPI_COMM_WORLD, num_levels);
  time1 = hypre_MPI_Wtime();
  time = time1 - time0;
  return;
}


HYPRE_Int ParaSailsSetupValues(HYPRE_Real filter)
{
  //    HYPRE_Int row, len, *ind;
    //    HYPRE_Real *val;
    //    HYPRE_Int i;
    HYPRE_Real time0, time1, time;
    MPI_Comm comm = MPI_COMM_WORLD;
    HYPRE_Int error = 0, error_sum;

    time0 = hypre_MPI_Wtime();


    ExchangeStoredRows(comm);

    time1 = hypre_MPI_Wtime();
    time  = time1 - time0;

    /* check if there was an error in computing the approximate inverse */
    hypre_MPI_Allreduce(&error, &error_sum, 1, HYPRE_MPI_INT, hypre_MPI_SUM, comm);
    SelectFilter(comm, 0, 0);

    return 0;
}


int main(int argc, char* argv[])
{
  int num_loop = 100000000;
  MPI_Init(&argc, &argv);
  signal(SIGSEGV, SIG_DFL);
  hypre_MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
  hypre_MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
  
  if (comm_size != 64) {
    if (my_rank == 0) mst_test_dbg_print("Please run this test with 64 processes");
    exit(0);
  }

  if (argc == 2) {
    num_loop = atoi(argv[1]);
  } 

#ifdef ENABLE_NOISE
  int noise_calcount = 100000000;
  double noise_s = get_time();
  do_noise_work(noise_calcount, 1);
  double noise_e = get_time();
  calibration = noise_calcount / (noise_e - noise_s) / 1000;
  /* fprintf(stderr, "%d: %f\n", my_rank, noise_e - noise_s); */
  /* noise_s = get_time(); */
  /* do_noise_work(150, 1); */
  /* noise_e = get_time(); */
  /* fprintf(stderr, "%d: %f\n", my_rank, noise_e - noise_s); */
  /* noise_s = get_time(); */
  /* do_noise_work(150, 1); */
  /* noise_e = get_time(); */
  /* fprintf(stderr, "%d: %f\n", my_rank, noise_e - noise_s); */
  /* exit(0); */
  init_rand(my_rank);
#endif

  //  int pid = getpid();
  char path[32];
  int fd = 0;;
  if (my_rank == 0) {
    //    sprintf(path, "parasails-%d.log", pid);
    sprintf(path, "parasails.log");
    fd = open(path, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
  }
  
  int i;

  //  int print_rank = 0;
  //  for (i = 0; i < 50 ;i++) {
  double start = get_time();
  for (i = 0; i< num_loop ;i++) {
    //    if (i % 100000 == 0) {
    if (1) {
      //      if (my_rank == print_rank++ % comm_size) {
      if (my_rank == 0) {
	//	dprintf(fd, "loop %d\n", i);
	write(fd, "loop\n", 5);
    	mst_test_dbg_print("loop %d", i);
      }
    }
    ParaSailsSetupPattern(0, 1);
    sleep(1);
    ParaSailsSetupValues(0);
    MPI_Pcontrol(1);
  }
  double end = get_time();
  if (my_rank == 0) {
    fprintf(stderr, "Time: %f\n", end - start);
    close(fd);
  }

  MPI_Finalize();
  return 0;
}
