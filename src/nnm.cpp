#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mpi.h"
#include "nin_util.h"

#define NNM_MINL_TAG (15)
#define NNM_MINL_STABLE_COUNT (1000)
#define NNM_MINL_BEGIN (0)
#define NNM_MINL_END   (1)
#define NNM_NETN_TAG (16)

int my_rank;
double send_buf = 0;
double recv_buf = 0;
double lat_01, lat_12, lat_02;



static double measure_min_latency(int peer_rank, int is_server)
{
  double start, end, elapse;
  int min_stable_count = 0;
  double rtt_min = 1000.0 * 1e6;
  send_buf = NNM_MINL_BEGIN;
  while (1) {
    if (is_server) {
      //      NIN_DBG("recv: %d", peer_rank);
      MPI_Recv(&recv_buf, 1, MPI_DOUBLE, peer_rank, NNM_MINL_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      MPI_Send(&send_buf, 1, MPI_DOUBLE, peer_rank, NNM_MINL_TAG, MPI_COMM_WORLD);
      if (recv_buf == NNM_MINL_END) return 0;
    } else {
      /* Wait for some time for getting server ready */
      if (min_stable_count >= NNM_MINL_STABLE_COUNT) send_buf = NNM_MINL_END;
      usleep(100);
      start = NIN_Wtime();
      //      NIN_DBG("send: %d", peer_rank);
      MPI_Send(&send_buf, 1, MPI_DOUBLE, peer_rank, NNM_MINL_TAG, MPI_COMM_WORLD);
      MPI_Recv(&recv_buf, 1, MPI_DOUBLE, peer_rank, NNM_MINL_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      end = NIN_Wtime();
      elapse = end - start;
      if (rtt_min > elapse) {
	rtt_min = elapse;
	min_stable_count = 0;
	//	NIN_DBG("min: %f", rtt_min/2.0);
      } else {
	min_stable_count++;
      }
      if (send_buf == NNM_MINL_END) return rtt_min/2.0;
    }
  }
  exit(1);
}

static void measure_net_noise()
{
  MPI_Request req[2];
  double recv_ts[2];    
  int counter = 0;
  int d0;
  double start = NIN_Wtime();
  memset(recv_ts, 0, sizeof(double) * 2);
  while(1) {
    if (my_rank == 0) {
      usleep(100);
      for (int i = 0, rank = 2; i < 2; i++, rank--) {
        send_buf = NIN_Wtime();
	MPI_Send(&send_buf, 1, MPI_DOUBLE, rank, NNM_NETN_TAG, MPI_COMM_WORLD);
      }
    } else if (my_rank == 1) {
      MPI_Recv(&recv_buf, 1, MPI_DOUBLE, 0, NNM_NETN_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      MPI_Send(&recv_buf, 1, MPI_DOUBLE, 2, NNM_NETN_TAG, MPI_COMM_WORLD);
    } else if (my_rank == 2) {
      for (int i = 0 ; i < 2; i++) {
	MPI_Irecv(&recv_ts[i], 1, MPI_DOUBLE, MPI_ANY_SOURCE, NNM_NETN_TAG, MPI_COMM_WORLD, &req[i]);
      }
      MPI_Waitall(2, req, MPI_STATUS_IGNORE);
      d0 = recv_ts[0] - recv_ts[1];
      if (d0 > 0) {
	double noise = d0 + lat_01 + lat_12  - lat_02;
	fprintf(stderr, "%f %f\n", NIN_Wtime() - start, noise);
      }
      counter++;
    } else {
      exit(1);
    }
  }
   
}

int main(int argc, char **argv)
{
  int size;

  MPI_Init(&argc, &argv);
  NIN_Init();
  MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  if (size != 3) {
    if (my_rank == 0) NIN_DBG("Run with 3 procs");
    exit(0);
  }

  if (my_rank == 0) {
    measure_min_latency(1, 1);
    measure_min_latency(2, 1);
  } else if (my_rank == 1) {
    lat_01 = measure_min_latency(0, 0);
    measure_min_latency(2, 1);
    NIN_DBG("lat_01: %f", lat_01);
  } else if (my_rank == 2) {
    lat_02 = measure_min_latency(0, 0);
    NIN_DBG("lat_02: %f", lat_02);
    lat_12 = measure_min_latency(1, 0);
    NIN_DBG("lat_12: %f", lat_12);
  }

  measure_net_noise();

  MPI_Finalize();

  return 0;
}
