#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include <vector>

#include "mpi.h"
#include "nin_util.h"

#define NNM_MINL_TAG (15)
#define NNM_MINL_STABLE_COUNT (1000)
#define NNM_MINL_BEGIN (0)
#define NNM_MINL_END   (1)
#define NNM_NETN_TAG (16)

#define NNM_MINL_TAG2 (14)

//#define NNM_BST_MSG_NUM (128)
#define NNM_BST_MSG_NUM (64)
#define NNM_BST_TAG (17)

#define NNM_BST_MSG (1.5)
#define NNM_1ST_MSG (0)
#define NNM_2ND_MSG (3.5)
#define NNM_TRM_MSG (4.5)

using namespace std;

int my_rank;
double lat_01, lat_12, lat_02;

int fd;

static double measure_min_latency(int peer_rank, int is_server)
{
  double start, end, elapse;
  int min_stable_count = 0;
  double rtt_min = 1000.0 * 1e6;
  double send_buf = 0;
  double recv_buf = 0;
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




typedef struct {
  double ts;
  double detour;
  double period;
} detour_record_t;


typedef struct {
  vector<detour_record_t*> *detour_records_vec;
  int sample_count;
  int detour_count;
  double ave_delay;
  double ave_start_time;
  double ave_period_all;
  double total_period_all;
  double delay_ratio;
} nnm_record_t;



static void measure_net_noise(int src, int middle, int dest,
			      double latency_sm, double latency_md, double latency_sd, 
			      int group_id, double detour_th, 
			      int length, int bytes, int sleep_usec, double period_sec,
			      nnm_record_t *record)
{
  MPI_Request *reqb, req[2];
  double recv_ts[2];
  int counter = 0;
  double Dm, Dsd;
  double start = NIN_Wtime();
  char **bbuf;
  double send_buf1 = 0, send_buf2 = 0;
  double recv_buf = 0;
  int flag = 0;
  double ts_1st_msg, ts_2nd_msg_previous, ts_2nd_msg;
  double current_time;
  double bperiod;
  
  if (length < 10) {
    NIN_DBG("must be length > 10");
    exit(0);
  }

  reqb = (MPI_Request*)malloc(sizeof(MPI_Request) * length);
  bbuf = (char**)malloc(sizeof(char*) * length);
  for (int i = 0; i < length; i++) {
    bbuf[i] = (char*)malloc(bytes);
    memset(bbuf[i], 0, bytes);
  }

  memset(recv_ts, 0, sizeof(double) * 2);
  while(1) {

    if (my_rank == src) {
      current_time = NIN_Wtime();
      if (period_sec < current_time - start) {
	send_buf1 = NNM_TRM_MSG;
	send_buf2 = NNM_TRM_MSG;
      } else {
	send_buf1 = NNM_1ST_MSG;
	send_buf2 = NNM_2ND_MSG;
      }
      usleep(100);
      current_time = NIN_Wtime();
      for (int i = 0; i < length; i++) {
	MPI_Isend(bbuf[i], bytes, MPI_CHAR, dest,   NNM_BST_TAG, MPI_COMM_WORLD, &reqb[i]);
	//	MPI_Send(bbuf[i], bytes, MPI_CHAR, dest,   NNM_BST_TAG, MPI_COMM_WORLD);
      }
      if (send_buf2 != NNM_TRM_MSG) {
	send_buf2 = NIN_Wtime() - current_time;
      }

      //      NIN_DBG("Send : %d (%f %f)", counter, send_buf1, send_buf2);
      MPI_Isend(&send_buf1, 1, MPI_DOUBLE, dest,   NNM_NETN_TAG, MPI_COMM_WORLD, &req[0]);
      MPI_Isend(&send_buf2, 1, MPI_DOUBLE, middle, NNM_NETN_TAG, MPI_COMM_WORLD, &req[1]);
      //      NIN_DBG("Send ... done : %d", counter++);
      //      MPI_Send(&send_buf1, 1, MPI_DOUBLE, dest,   NNM_NETN_TAG, MPI_COMM_WORLD);
      //      MPI_Send(&send_buf2, 1, MPI_DOUBLE, middle, NNM_NETN_TAG, MPI_COMM_WORLD);
      MPI_Recv(&recv_buf , 1, MPI_DOUBLE, dest,   NNM_NETN_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      MPI_Waitall(length, reqb, MPI_STATUS_IGNORE);
      MPI_Waitall(2, req, MPI_STATUS_IGNORE);
      if (send_buf1 == NNM_TRM_MSG) break;
    } else if (my_rank == middle) {
      //      NIN_DBG("Recv : %d", counter);
      MPI_Recv(&recv_buf, 1, MPI_DOUBLE, src,  NNM_NETN_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      //      NIN_DBG("Send : %d (%f)", counter, recv_buf);
      MPI_Send(&recv_buf, 1, MPI_DOUBLE, dest, NNM_NETN_TAG, MPI_COMM_WORLD);
      //      NIN_DBG("Send  ... done : %d", counter++);
      if (recv_buf == NNM_TRM_MSG) {      
	//	NIN_DBG("break"); 
	break;}

    } else if (my_rank == dest) {
      flag = 0;
      for (int i = 0 ; i < length; i++) {
	MPI_Irecv(bbuf[i], bytes, MPI_CHAR, MPI_ANY_SOURCE, NNM_BST_TAG, MPI_COMM_WORLD, &reqb[i]);
      }
      for (int i = 0; i < 2; i++) {
	MPI_Irecv(&recv_ts[i], 1, MPI_DOUBLE, MPI_ANY_SOURCE, NNM_NETN_TAG, MPI_COMM_WORLD, &req[i]);
      }


      MPI_Wait(&req[0], MPI_STATUS_IGNORE);

      ts_1st_msg = ts_2nd_msg = NIN_Wtime();
      if (recv_ts[0] == NNM_1ST_MSG || recv_ts[0] == NNM_BST_MSG) {
	//	NIN_DBG("Wait: %d", counter);
	MPI_Wait(&req[1], MPI_STATUS_IGNORE);
	//	NIN_DBG("Wait ... done: %d", counter++);
	bperiod = recv_ts[1];
      } else {
	while (!flag) {
	  ts_2nd_msg_previous = ts_2nd_msg;
	  ts_2nd_msg = NIN_Wtime();
	  MPI_Test(&req[1], &flag, MPI_STATUS_IGNORE);
	}
	bperiod = recv_ts[0];
	//	NIN_DBG("%f ", bperiod);

	Dm = ts_2nd_msg_previous - ts_1st_msg;
	Dsd = Dm + (latency_sm + latency_md) - latency_sd;
	if (Dsd > latency_sd * detour_th) {
	  detour_record_t *dr = (detour_record_t*)malloc(sizeof(detour_record_t));
	  dr->ts     =  NIN_Wtime() - start;
	  dr->detour = Dsd;
	  dr->period = bperiod;
	  record->detour_records_vec->push_back(dr);
	  record->detour_count++;
	}
      }

      MPI_Waitall(length, reqb, MPI_STATUS_IGNORE);
      MPI_Send(&send_buf1, 1, MPI_DOUBLE, src,   NNM_NETN_TAG, MPI_COMM_WORLD);
      record->total_period_all += bperiod;
      record->sample_count++;
      if (recv_ts[0] == NNM_TRM_MSG) break;
    } else {
      exit(1);
    }
  }

  free(reqb);
  for (int i = 0; i < length; i++) {
    free(bbuf[i]);
  }
  free(bbuf);
  return;
}

static void init_min_latences(int src, int middle, int dest,
			      double *latency_sm, double *latency_md, double *latency_sd)
{
  
  if (my_rank == src) {
    measure_min_latency(middle, 1);
    measure_min_latency(dest, 1);
  } else if (my_rank == middle) {
    *latency_sm = measure_min_latency(src, 0);
    measure_min_latency(dest, 1);
    //    NIN_DBG("sm: %f", *latency_sm);
    MPI_Send(latency_sm, 1, MPI_DOUBLE, dest, NNM_MINL_TAG2, MPI_COMM_WORLD);
  } else if (my_rank == dest) {
    *latency_sd = measure_min_latency(src, 0);
    *latency_md = measure_min_latency(middle, 0);
    MPI_Recv(latency_sm, 1, MPI_DOUBLE, middle, NNM_MINL_TAG2, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    //    NIN_DBG("sd: %f", *latency_sd);
    //    NIN_DBG("md: %f", *latency_md);
  }
  return;
}


static int init_all_min_latencies(double *latency_sm, double *latency_md, double *latency_sd, int *group_num)
{
  int size;
  int shrinked_size;
  int group_id;
  int src;

  MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  shrinked_size = size - size % 3;
  group_id  = my_rank / 3;
  *group_num = shrinked_size / 3;
  for (int i = 0; i < *group_num; i++) {
    src = i * 3;
    if (group_id == i) {
      //       if (my_rank == src)  NIN_DBG("Initializing nin latencyes: gid=%d, src: %d", i, src);
      init_min_latences(src, src + 1, src + 2, latency_sm, latency_md, latency_sd);
    }
    MPI_Barrier(MPI_COMM_WORLD);
  }
  if (my_rank >= shrinked_size) {
    group_id = -1;
  }
  return group_id;
}


static nnm_record_t* create_record_t()
{
  nnm_record_t *record = (nnm_record_t*)malloc(sizeof(nnm_record_t));
  record->detour_records_vec = new vector<detour_record_t*>();
  record->detour_count = 0;
  record->sample_count = 0;
  record->ave_delay = 0;
  record->ave_start_time = 0;
  record->total_period_all = 0;
  record->ave_period_all = 0;
  record->delay_ratio = 0;
  return record;
}

static void free_record_t(nnm_record_t* record)
{
  for (int i = 0; i < record->detour_records_vec->size(); i++) {
    free(record->detour_records_vec->at(i));
  }
  delete(record->detour_records_vec);
  free(record);
}

static void get_network_configuration(int *mtu, int *num_rail)
{
  /*TODO: dynamically query to HCA*/
  *mtu = 2048;
  *num_rail = 1;
  return;
}

static void do_statistics(nnm_record_t *record)
{
  double detour_sum = 0;
  double period_sum = 0;
  for (int i = 0; i < (int)record->detour_records_vec->size(); i++) {
    detour_sum += record->detour_records_vec->at(i)->detour;
    period_sum += record->detour_records_vec->at(i)->period;
  }
  record->ave_delay      = detour_sum/record->detour_count;
  record->ave_start_time = period_sum/record->detour_count;
  record->delay_ratio    = record->detour_count/double(record->sample_count);
  record->ave_period_all = record->total_period_all/double(record->sample_count);
  return;
}

static void dump(nnm_record_t *record, int num_packets)
{
  // sprintf(line, "procs: %d\n", commworld_size);
  // mst_write(path, fd, line, strlen(line));
  // sprintf(line, "delta min: %f, delta max: %f \n", delta_min, delta_max);
  // mst_write(path, fd, line, strlen(line));
  // sprintf(line, "------------------ \n");
  // mst_write(path, fd, line, strlen(line));
  // sprintf(line, "<rank> <timestamp> <src> \n");
  // mst_write(path, fd, line, strlen(line));
  // for (int rank = 0; rank < commworld_size; rank++) {
  //   PMPI_Probe(rank, MST_RANK_TAG, mst_comm, &stat);
  //   PMPI_Get_count(&stat, MPI_INT, &count);
  //   recv_rank_buf = (int*)malloc(sizeof(int) * count);
  //   PMPI_Recv(recv_rank_buf, count, MPI_INT, rank, MST_RANK_TAG, mst_comm, MPI_STATUS_IGNORE);
  //   PMPI_Probe(rank, MST_TS_TAG, mst_comm, &stat);
  //   PMPI_Get_count(&stat, MPI_DOUBLE, &count);
  //   recv_ts_buf = (double*)malloc(sizeof(double) *count);
  //   PMPI_Recv(recv_ts_buf, count, MPI_DOUBLE, rank, MST_TS_TAG, mst_comm, MPI_STATUS_IGNORE);
  //   for (int i = 0; i < count; i++) {
  //     sprintf(line, "%d %f %d\n", rank, recv_ts_buf[i], recv_rank_buf[i]);
  //     mst_write(path, fd, line, strlen(line));
  //   }
  //   free(recv_rank_buf);
  //   free(recv_ts_buf);
  // }
  // mst_close(path, fd);
  // MST_DBG("Trace written to %s", path);
  return;
}

static void experiment()
{
  int mtu, num_rail;
  /*TODO: Process distribution check 
    Check if three processes in each set are distributed across different nodes.
    Currently, I assume prcessess are distributed across exact "3" nodes:
      rank0, rank3, rank6, rank9  on node 0 
      rank1, rank4, rank7, rank10 on node 1
      rank2, rank5, rank8         on node 2
    In this case, rank9 and rank10 do not paticipate in this measurement.
    And, correctly run on 3-node case.
   */

  get_network_configuration(&mtu, &num_rail);

  if (my_rank == 0)  NIN_DBG("Initizliaing min latencyes... ");
  int group_num;
  int group_id = init_all_min_latencies(&lat_01, &lat_12, &lat_02, &group_num);
  if (my_rank == 0)  NIN_DBG("Start measruing network noises with %d groups", group_num);

  if (group_id >= 0) {
    int src = group_id * 3;

    //    for (int usec = 0; usec <=100; usec += 5) {
    for (int usec = 0; usec <=0; usec += 5) {
      //      for (int length = 16; length <= 64; length += 4) {
      for (int length = 60; length <= 70; length += 1) {
	//	for (int bytes = 1; bytes <= 1; bytes += 16) {
	for (int bytes = 2048/*1024 + 512*/; bytes <= 2048; bytes += 1) {
	  //      for (int length = 32; length <= 32; length += 4) {
	  //int length = 32; /* length x bytes < 64 x 2K in cab*/
	  
	  nnm_record_t *record = create_record_t();
	  record->detour_count = 0;
	  record->sample_count = 0;
	  measure_net_noise(src, src + 1, src + 2, lat_01, lat_12, lat_02, group_id, 1, 
			    length, bytes, usec, 1 * 1e6, record);
	  if (my_rank == src + 2) {
	    do_statistics(record);
	    NIN_DBG("usec: %d, num_packets:%d, bytes:%d, ratio:%f, delay:%f,  start:%f", usec, length, bytes, record->delay_ratio,
		    record->ave_delay, record->ave_period_all);
	  }
	  free_record_t(record);
	}
      }

    }
    
    
    //    measure_net_noise(src + 1, src, src + 2, lat_01, lat_12, lat_02, group_id, 10);
    //   measure_net_noise(src, src + 2, src + 1, lat_01, lat_12, lat_02, group_id, 0);
  }

}

static int diagnose_packet_process_latency(int group_id, int src, int mtu, int num_packet_threshold)
{
  int sleep_usec;
  int bytes;
  nnm_record_t *record;
  int num_packet = num_packet_threshold;
  double packet_process_latency = 0;
  double recv;
  bytes = 1;
  for (int sleep_usec = 100; sleep_usec <= 10000; sleep_usec += 10) {
    record = create_record_t();
    // NIN_DBG("src:%d, (%f %f %f), gid: %d num_packets:%d, bytes:%d, usec:%d",
    //  	    src, lat_01, lat_02, lat_12, group_id, num_packet, bytes, sleep_usec);
    measure_net_noise(src, src + 1, src + 2, lat_01, lat_12, lat_02, group_id, 1, 
		      num_packet, bytes, sleep_usec, 5 * 1e6, record);
    if (my_rank == src + 2) {
      do_statistics(record);
      NIN_DBG("LAT: usec: %d, num_packets:%d, bytes:%d, ratio:%f, delay:%f,  start:%f (%d/%d)", sleep_usec, num_packet, bytes, record->delay_ratio,
       	      record->ave_delay, record->ave_period_all, record->detour_count, record->sample_count);
      if (record->delay_ratio < 0.01) {
	packet_process_latency = record->ave_period_all / num_packet;
	NIN_DBG("latency %f", packet_process_latency);
      }
    }
    free_record_t(record);
    MPI_Allreduce(&packet_process_latency, &recv, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    if (recv > 0) break;
  }
  // NIN_DBG("--> %Lf", recv);
  return recv;
}

static int diagnose_threshold(int group_id, int src, int mtu)
{
  int bytes, usec;
  nnm_record_t *record;
  int num_packet_threshold = 0;
  int recv;
  bytes = 1;
  usec = 0;
  //  for (int num_packet = 600; num_packet <= 1000; num_packet += 10) {
  //  for (int num_packet = 10; num_packet <= 90; num_packet += 1) {
  for (int num_packet = 60; num_packet <= 90; num_packet += 1) {
    record = create_record_t();
    // NIN_DBG("src:%d, (%f %f %f), gid: %d num_packets:%d, bytes:%d, usec:%d",
    // 	    src, lat_01, lat_02, lat_12, group_id, num_packet, bytes, usec);
    measure_net_noise(src, src + 1, src + 2, lat_01, lat_12, lat_02, group_id, 1, 
		      num_packet, bytes, usec, 1 * 1e6, record);
    //    NIN_DBG("===");
    if (my_rank == src + 2) {
      do_statistics(record);
      // NIN_DBG("THR: usec: %d, num_packets:%d, bytes:%d, ratio:%f, delay:%f,  start:%f", usec, num_packet, bytes, record->delay_ratio,
      //  	      record->ave_delay, record->ave_period_all);
      NIN_DBG("THR: %d %d %d %f %f %f", usec, num_packet, bytes, record->delay_ratio,
       	      record->ave_delay, record->ave_period_all);
      if (record->delay_ratio > 0.3 && num_packet_threshold == 0) {
	num_packet_threshold = num_packet;
      }
      //      dump(record);
    }
    free_record_t(record);
    MPI_Allreduce(&num_packet_threshold, &recv, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
    //    if (recv > 0) break;
  }
  //  NIN_DBG("sleep 10 seconds: threshold: %d: num:%d", recv, num_packet_threshold);
  
  return recv;
}

static void diagnose()
{
  /*TODO: Process distribution check 
    Check if three processes in each set are distributed across different nodes.
    Currently, I assume prcessess are distributed across exact "3" nodes:
      rank0, rank3, rank6, rank9  on node 0 
      rank1, rank4, rank7, rank10 on node 1
      rank2, rank5, rank8         on node 2
    In this case, rank9 and rank10 do not paticipate in this measurement.
    And, correctly run on 3-node case.
   */
  int mtu, num_rail;
  int src;
  int num_packet_threshold;
  double packet_transmit_latency;

  get_network_configuration(&mtu, &num_rail);

  if (my_rank == 0)  NIN_DBG("Initizliaing min latencyes... ");
  int group_num;
  int group_id = init_all_min_latencies(&lat_01, &lat_12, &lat_02, &group_num);
  if (my_rank == 0)  NIN_DBG("Start measruing network noises with %d groups", group_num);

  if (group_id == 0) {
    src = group_id * 3;
    num_packet_threshold    = diagnose_threshold(group_id, src, mtu);
    exit(0);
    packet_transmit_latency = diagnose_packet_process_latency(group_id, src, mtu, num_packet_threshold);    
  }
  return;
}


int main(int argc, char **argv)
{


  MPI_Init(&argc, &argv);
  signal(SIGSEGV, SIG_DFL);
  NIN_Init();
  MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

  // bin_name = basename((*arg_1)[0]);
  // sprintf(path, "%s.%d.nnm", bin_name, getpid());
  // fd = mst_open(path, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
  diagnose();
  //  close(fd);


  //  experiment();

  MPI_Finalize();

  return 0;
}
