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
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <libgen.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <numeric>

#include "mpi.h"
#include "ninj_fc.h"
#include "nin_util.h"
#include "ninj_thread.h"
#include "mst_io.h"

#define NIN_CONF_PATTERN       "NIN_PATTERN" /*How to inject noise ?*/
#define NIN_CONF_PATTERN_FREE  (0)  /* Noise free */
#define NIN_CONF_PATTERN_RAND  (1)  /* Random noise */
#define NIN_CONF_PATTERN_MODEL (2)  /* Ninja noise (require NIN_CONF_MODEL_MODE) */
#define NIN_CONF_RAND_RATIO    "NIN_RAND_RATIO"
#define NIN_CONF_RAND_DELAY    "NIN_RAND_DELAY"
#define NIN_CONF_MODEL_MODE    "NIN_MODEL_MODE"
#define NIN_CONF_MODEL_PASSIVE (0)  /* Systen-centric noise */
#define NIN_CONF_MODEL_ACTIVE  (1)  /* Application-centric noise */

#define NINJ_FC_QUEUED_PACKET_NUM(head, tail) \
  (((tail - head) + NINJ_FC_RING_BUFF_SIZE) % NINJ_FC_RING_BUFF_SIZE)

#define NINJ_FC_PACKET_TRANSMIT_MODEL(msg_size) \
   (ninj_fc_model_packet_latency_usec + msg_size / ninj_fc_model_packet_throughput_bpusec)

#define NINJ_FC_MTU (2048)
#define NINJ_FC_RING_BUFF_SIZE (1024 * 1024)
#define NINJ_FC_QUEUE_CAPACITY (4)

#define NINJ_FC_MODEL_PACKET_LATENCY    (0.25)  // in usec
#define NINJ_FC_MODEL_PACKET_THROUGHPUT (314.3) // in bytes/usec e.g. (3.2GB/sec = 312.5 bytes/usec)

#define NINJ_FC_THREAD_OVERHEAD_SEC (0.000030)

//#define NIN_SA (2.0)
#define NIN_SA (1.2)

#define NINJ_FC_MSG_ID(tag, comm_id) (size_t)(tag * 10000 + comm_id)


using namespace std;
static unordered_map<int, double> rank_to_last_send_time_umap;

/* =====================================================================================  */
/*            Machine learning data NOT to be dumped for active noise injection           */
/*            This data need to be reinitilized in the beginning of each epoch            */
/*            Epoch: after consistent global synchronization/collective communication     */
/* =====================================================================================  */
/*  -------- common for passive and active -----------------------               */
static unordered_map<size_t, double> previous_send_times_umap;
/*For each matching_id, memorize send call intervals*/
static unordered_map<size_t, vector<double>*> matching_id_to_send_intervals_umap;
static int ninj_fc_delta_sum = 0;
#define NINJ_FC_SEND_COUNT (0)
#define NINJ_FC_RECV_COUNT (1)
static int ninj_fc_msg_count[2] = {0,0}, ninj_fc_msg_count_buff[2] = {0,0};
/*  -------- active mode only  -----------------------               */
static unordered_map<size_t, int> matching_id_to_active_noise_index_umap;
/* =====================================================================================  */
/*            Machine learning data to be dumped for active noise injection               */
/* =====================================================================================  */
/*For each matching_id, memorize minimal noise */
static unordered_map<size_t, double> matching_id_to_min_delay_umap;
/*For each matching_id, memorize how much delay should be added*/
static unordered_map<size_t, vector<double>*> matching_id_to_delays_umap;
/* =====================================================================================  */

static double *ninj_fc_ring_buffer;
static int ninj_fc_ring_buffer_head_index = 0, ninj_fc_ring_buffer_tail_index = 0;
static int ninj_fc_rb_max_length = -1, ninj_fc_rb_gmax_length = -1;
static timeval ninj_fc_base_time;
static double ninj_fc_mtu_transmit_time_usec;


/*Configuration: common*/
static int ninj_fc_pattern = NIN_CONF_PATTERN_MODEL;

/*Configuration variables for random noise*/
static double ninj_fc_rand_ratio;
static int ninj_fc_rand_delay_usec;

/*Configuration variables for model noise*/
static int ninj_fc_mtu_size = NINJ_FC_MTU;
static int ninj_fc_queue_capacity = NINJ_FC_QUEUE_CAPACITY;
static double ninj_fc_model_packet_latency_usec    = NINJ_FC_MODEL_PACKET_LATENCY;
static double ninj_fc_model_packet_throughput_bpusec = NINJ_FC_MODEL_PACKET_THROUGHPUT;
static int ninj_fc_queue_length_threshold = 1;
static int ninj_fc_model_mode = NIN_CONF_MODEL_PASSIVE;
static char *ninj_fc_dir;
static char ninj_fc_learning_file[256];

static int ninj_fc_comm_size;
//static unordered_map<int, unordered_set<int>*> ninj_fc_victim_dest_umap;
static unordered_set<int> ninj_fc_victim_dest_uset;
static int ninj_fc_execution_id; /* Uniq number for this execution */

static int ninj_fc_init_victim_dest()
{
  if (nin_my_rank == 0) {
    ninj_fc_execution_id = (int)NIN_get_time();
  }
  PMPI_Bcast(&ninj_fc_execution_id, 1, MPI_INT, 0, MPI_COMM_WORLD);
  ninj_fc_victim_dest_uset.clear();
  return 1;
}

static int ninj_fc_is_victim_dest(int dest, int message_id)
{
  int num_victim_rank;
  int victim_rank;
  int is_victim;


  if (ninj_fc_victim_dest_uset.size() == 0) {
    NIN_init_rand(ninj_fc_execution_id);
    num_victim_rank = (int)(ninj_fc_comm_size * 0.2);
    if (num_victim_rank == 0) num_victim_rank = 1;
    for (int i = 0; i < num_victim_rank; i++) {
      victim_rank = NIN_get_rand(ninj_fc_comm_size);
      ninj_fc_victim_dest_uset.insert(victim_rank);
    }
  }
  is_victim = (ninj_fc_victim_dest_uset.find(dest) != ninj_fc_victim_dest_uset.end())? 1:0;   
  return is_victim;
}


static void ninj_fc_print_learning_data()
{
#define DBG_PRINT_READ
#ifdef DBG_PRINT_READ
  size_t matching_id;
  double interval_threshold;
  vector<double> *delay_vec;

  unordered_map<size_t, double>::const_iterator it;
  unordered_map<size_t, double>::const_iterator it_end;
  it     = matching_id_to_min_delay_umap.cbegin();
  it_end = matching_id_to_min_delay_umap.cend();
  for (; it != it_end; it++)  {
    matching_id = it->first;
    if (matching_id_to_delays_umap.find(matching_id) != 
  	matching_id_to_delays_umap.end())  {
      interval_threshold = matching_id_to_min_delay_umap[matching_id];
      NIN_DBG("Threshold: %f", interval_threshold);
      delay_vec = matching_id_to_delays_umap[matching_id];
      for (int i = 0; i < delay_vec->size(); i++) {
	NIN_DBG(" %lu:%d:%f ", matching_id, i, delay_vec->at(i));
      }
    }
  }
#endif
}

static int ninj_fc_delay_adjustment_for_ordered_send(int dest, int delay_flag, double current_time, double *send_time)
{  
  int is_adjusted = 0;
  double last_send_time; 
  double t;
  if(rank_to_last_send_time_umap.find(dest) == 
     rank_to_last_send_time_umap.end()) {
    last_send_time = 0;
    rank_to_last_send_time_umap[dest] = last_send_time;
  } else {
    last_send_time = rank_to_last_send_time_umap[dest];
  }
  if (last_send_time > *send_time) {
    /*If there is pending message that will be sent after current_time + delay_sec,
      then this message must be right after this message. */
    is_adjusted = 1;
    *send_time = last_send_time + 1.0/1e6;
  }
  if (is_adjusted || delay_flag) {
    rank_to_last_send_time_umap[dest] = *send_time;
  } else {
    rank_to_last_send_time_umap[dest] = current_time;
  }
  return is_adjusted;
}


static void ninj_fc_get_delay_random(int dest, int tag, int comm_id, int *delay_flag, double *send_time, double *base_time)
{
  double delay_sec;
  int is_delayed, is_adjusted;
  double current_time   = NIN_get_time();
  NIN_init_ndrand();
  is_delayed = NIN_get_rand((int)(100/ninj_fc_rand_ratio)) == 0;
  if (!is_delayed) {
    *delay_flag = 0;
    *send_time = current_time;
  } else {
    *delay_flag = 1;
    *send_time = current_time + ninj_fc_rand_delay_usec / 1.0e6;
  }
  is_adjusted = ninj_fc_delay_adjustment_for_ordered_send(dest, *delay_flag, current_time, send_time);
  if (is_adjusted) *delay_flag = 1;
  *base_time = (*delay_flag)? current_time:-1;
  return;
}


static void ninj_fc_get_delay_const(int dest, int tag, int comm_id, int *delay_flag, double *send_time)
{
  double delay_sec;
  int is_delayed, is_adjusted;
  double current_time   = NIN_get_time();
  *delay_flag = 1;
  *send_time = current_time + 100000 / 1e6;
  //  NIN_DBG("delay !!");
  is_adjusted = ninj_fc_delay_adjustment_for_ordered_send(dest, *delay_flag, current_time, send_time);
  if(is_adjusted) *delay_flag = 1;
  return;
}

static void ninj_fc_ring_buffer_head_progress()
{
  timeval tval;
  gettimeofday(&tval, NULL);
  double current_time = tval.tv_sec + tval.tv_usec / 1e6;
  while(ninj_fc_ring_buffer_head_index != ninj_fc_ring_buffer_tail_index) {
    // nin_dbg("cur: %f, head: %f (index; %d)", 
    // 	    current_time, ninj_fc_ring_buffer[ninj_fc_ring_buffer_head_index],
    // 	    ninj_fc_ring_buffer_head_index);
    if (current_time < ninj_fc_ring_buffer[ninj_fc_ring_buffer_head_index]) { 
      break;
    }
    ninj_fc_ring_buffer_head_index = (ninj_fc_ring_buffer_head_index + 1) % NINJ_FC_RING_BUFF_SIZE;
  }
  return;
}

/*Compute time to "num_packets" of pakets is transmitted. 
The packests are from index:head to index:head + (num_packets - 1) */
static double ninj_fc_get_time_of_packet_transmit(int num_packets)
{
  //kent  matching_id_to_active_noise_index_umap[id];
  double time;
  if (num_packets == 0) { 
    NIN_DBG("num_packets is 0");
    exit(1);
  }
  int index = (ninj_fc_ring_buffer_head_index + num_packets - 1) % NINJ_FC_RING_BUFF_SIZE;
  time = ninj_fc_ring_buffer[index];
  //  NIN_DBGI(0, "===== send_delay: %f (index: %d): num:%d", time, index, num_packets);
  return time;
}

static double ninj_fc_get_time_of_active_delayed_transmit(int message_id, double current_time)
{
  vector<double> *delay_vec;
  double delay = 0;
  int delay_index;

  if (matching_id_to_delays_umap.find(message_id) != 
      matching_id_to_delays_umap.end()) {
    delay_vec   = matching_id_to_delays_umap[message_id];
    if (matching_id_to_active_noise_index_umap.find(message_id) != 
	matching_id_to_active_noise_index_umap.end()) {
      delay_index = matching_id_to_active_noise_index_umap[message_id];
      if (delay_index < delay_vec->size()) {
	delay = delay_vec->at(delay_index) * NIN_SA;
      }
    }
  }
  //  if(message_id == 2220000)  NIN_DBGI(32, "Active delay: %f", delay);
  return current_time + delay;
}


static double ninj_fc_get_delay_model_mode(int message_id, double current_time, int num_packets)
{
  double send_time;

  switch(ninj_fc_model_mode) {
  case NIN_CONF_MODEL_PASSIVE:
    send_time = ninj_fc_get_time_of_packet_transmit(num_packets);
    break;
  case NIN_CONF_MODEL_ACTIVE:
    send_time = ninj_fc_get_time_of_active_delayed_transmit(message_id, current_time);
    break;
  default:
    NIN_DBGI(0, "No such mode: %d", ninj_fc_model_mode);
    exit(0);
  }
  return send_time;
}


#define ENABLE_ML_PRINT
double bt = 0;
static void ninj_fc_get_delay_model(int dest, int tag, int comm_id, int *delay_flag, double *send_time, double *base_time)
{
  if (bt == 0) bt = NIN_get_time();
  size_t message_id;
  int enqueued_packet_num;
  double current_time   = NIN_get_time();
  int is_adjusted;
  int is_victim_dest;
  int is_exceeded_threshold;
  int victim_filter;

  ninj_fc_ring_buffer_head_progress();
  enqueued_packet_num = NINJ_FC_QUEUED_PACKET_NUM(ninj_fc_ring_buffer_head_index, ninj_fc_ring_buffer_tail_index);
  message_id = NINJ_FC_MSG_ID(tag, comm_id);
  
  //if (message_id == 2220000) NIN_DBGI(32, "queue: %d > %d", enqueued_packet_num, ninj_fc_queue_length_threshold);
  
  is_victim_dest = ninj_fc_is_victim_dest(dest, message_id);
  

  // NIN_DBGI(0, "queue: %d > %d (dest: %d, tag: %d, comm_id: %d): %f (victim: %d)", enqueued_packet_num, ninj_fc_queue_length_threshold,
  // 	   dest, tag, comm_id, current_time - bt, is_victim_dest);
  // if (is_victim_dest) {
  //   NIN_DBG("dest: %d is victim (id: %d)", dest, message_id);
  // }

  is_exceeded_threshold = enqueued_packet_num > ninj_fc_queue_length_threshold;
  victim_filter = is_victim_dest && (is_victim_dest != nin_my_rank);
  if ((is_exceeded_threshold && ninj_fc_model_mode == 0 && 1) || 
      (1                     && ninj_fc_model_mode == 1 && victim_filter)) {
    //    *send_time = ninj_fc_get_time_of_packet_transmit(enqueued_packet_num - ninj_fc_queue_length_threshold);
    *send_time = ninj_fc_get_delay_model_mode(message_id, current_time, enqueued_packet_num - ninj_fc_queue_length_threshold);
    *delay_flag = (*send_time == current_time)? 0:1;
    //if (message_id == 2220000) NIN_DBGI(32, "active delay(1): %f", *send_time - current_time);
    // NIN_DBGI(0,"active delay(1): %f (dest: %d, send_time: %f, current_time: %f)", 
    // 	     *send_time - current_time, dest, *send_time, current_time);
    // if (dest == 6) NIN_DBG("=== active delay(1): %f (dest: %d, send_time: %f, current_time: %f)", 
    // 			   *send_time - current_time, dest, *send_time, current_time);
  } else {
    *delay_flag = 0;
    *send_time = current_time;
    // NIN_DBGI(0,"active delay(0): %f (dest: %d, send_time: %f, current_time: %f)", 
    // 	     *send_time - current_time, dest, *send_time, current_time);
    //if(message_id == 2220000) NIN_DBGI(32, "active delay(0): %f", *send_time - current_time);
    // if (dest == 6) NIN_DBG("=== active delay(0): %f (dest: %d, send_time: %f, current_time: %f)", 
    // 			   *send_time - current_time, dest, *send_time, current_time);
  }

  //  NIN_DBGI(0, "cur: %f, send: %f", current_time, *send_time);

  /*Memorize max queue length how long the queue length grows for model tuning*/
  if (enqueued_packet_num > ninj_fc_rb_max_length) {
    ninj_fc_rb_max_length = enqueued_packet_num;
  }

  // if (*delay_flag) {
  //   NIN_DBGI(0, "before cur: %f, send: %f, aj: %d", current_time, *send_time, is_adjusted);
  // }
  is_adjusted = ninj_fc_delay_adjustment_for_ordered_send(dest, *delay_flag, current_time, send_time);
  if(is_adjusted) *delay_flag = 1;
  // if (*delay_flag) {
  //   NIN_DBGI(0, "after cur: %f, send: %f, aj: %d", current_time, *send_time, is_adjusted);
  // }
  *base_time = (*delay_flag)? current_time:-1;

#ifdef ENABLE_ML_PRINT
  if (tag == 222) {
    //    NIN_DBGI(48, "%f %f %f", current_time, *send_time, *send_time - current_time);
  }
#endif
  return;
}

static void ninj_fc_packet_transmit_model(int msgsize, int *mtu_packet_num, double *last_packet_delay)
{
  int fraction_size;
  *mtu_packet_num = msgsize/ninj_fc_mtu_size;
  fraction_size   = msgsize % ninj_fc_mtu_size;
  if (fraction_size != 0) {
    *last_packet_delay = NINJ_FC_PACKET_TRANSMIT_MODEL(fraction_size);
    //    NIN_DBG("time: %f", *last_packet_delay);
  } else {
    *last_packet_delay = 0;
  }
  return;
}

static void ninj_fc_record_send_interval(double current_time_sec, int tag, int comm_id)
{
  size_t id;
  double delta;
  vector<double> *vec;
  double previous_send_time;
  id = NINJ_FC_MSG_ID(tag, comm_id);
  if(previous_send_times_umap.find(id) == previous_send_times_umap.end()) {
    previous_send_time = -1;
    previous_send_times_umap[id] = previous_send_time;
  } else {
    previous_send_time = previous_send_times_umap[id];
  }

  if (previous_send_time == -1) {
    previous_send_times_umap[id] = current_time_sec;
    //    NIN_DBGI(0, "%f", current_time_sec);
  } else {

    if (matching_id_to_send_intervals_umap.find(id) ==  
	matching_id_to_send_intervals_umap.end()) {
      vec = new vector<double>;
      matching_id_to_send_intervals_umap[id] = vec;
    } else {
      vec = matching_id_to_send_intervals_umap[id];
    }
    delta = current_time_sec - previous_send_time;
    //    NIN_DBGI(0, " %f (%d:%d) threshold: %f", delta, tag, comm_id, matching_id_to_min_delay_umap[id] + NINJ_FC_THREAD_OVERHEAD_SEC);
    vec->push_back(delta);
    previous_send_times_umap[id] = current_time_sec;

    if (ninj_fc_model_mode == NIN_CONF_MODEL_ACTIVE) {
      if (delta > matching_id_to_min_delay_umap[id] + NINJ_FC_THREAD_OVERHEAD_SEC) {
	if(id == 2220000) NIN_DBGI(32, "index change");
	int current_index = matching_id_to_active_noise_index_umap[id];
	if (current_index + 1 < matching_id_to_delays_umap[id]->size()) {
	  matching_id_to_active_noise_index_umap[id] = current_index + 1;
	}
      }	
    }


  }
  return;
}

static void ninj_fc_update_min_delay(size_t matching_id, double delay)
{
  if (matching_id_to_min_delay_umap.find(matching_id) ==
      matching_id_to_min_delay_umap.end()) {
    matching_id_to_min_delay_umap[matching_id] = delay;
  } else {
    if (matching_id_to_min_delay_umap[matching_id] > delay) {
      matching_id_to_min_delay_umap[matching_id] = delay;
    }    
  }
  //  NIN_DBGI(0, "min delay: %lu, %f", matching_id, matching_id_to_min_delay_umap[matching_id]);
  return;
}

static double ninj_fc_get_min_delay(size_t matching_id)
{
  return matching_id_to_min_delay_umap[matching_id];
}

static void ninj_fc_read_learning_file()
{
  int fd;
  size_t matching_id;
  int delay_vec_length;
  double interval_threshold, delay;
  vector<double> *delay_vec;
  char line[256];
  FILE* file = fopen(ninj_fc_learning_file, "r"); /* should check the result */

  while (fgets(line, sizeof(line), file)) {
    matching_id = (size_t)atoi(line);
    //    NIN_DBG("%lu", matching_id);

    fgets(line, sizeof(line), file);
    interval_threshold = atof(line);
    //    NIN_DBG("%f", interval_threshold);

    matching_id_to_min_delay_umap[matching_id] = interval_threshold;   

    fgets(line, sizeof(line), file);
    delay_vec_length = atoi(line);
    //    NIN_DBG("%d", delay_vec_length);
    
    delay_vec = new vector<double>;

    for (int i = 0; i < delay_vec_length; i++) {
      fgets(line, sizeof(line), file);
      delay = atof(line);
      //      NIN_DBG("%f", delay);
      delay_vec->push_back(delay);
    }
    delay_vec->push_back(0);
    matching_id_to_delays_umap[matching_id] = delay_vec;
    matching_id_to_active_noise_index_umap[matching_id] = 0;
  }
  fclose(file);

  return;
}

static void ninj_fc_write_learning_file()
{
  int fd;
  size_t matching_id;
  vector<double> *delay_vec;
  double interval_threshold;
  char line[256];
  fd = mst_open(ninj_fc_learning_file, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
  unordered_map<size_t, double>::const_iterator it;
  unordered_map<size_t, double>::const_iterator it_end;
  it     = matching_id_to_min_delay_umap.cbegin();
  it_end = matching_id_to_min_delay_umap.cend();
  for (; it != it_end; it++)  {
    matching_id = it->first;
    if (matching_id_to_delays_umap.find(matching_id) != 
	matching_id_to_delays_umap.end())  {
      sprintf(line, "%lu\n", matching_id);
      mst_write(ninj_fc_learning_file, fd, line, strlen(line));    

      interval_threshold = matching_id_to_min_delay_umap[matching_id];
      sprintf(line, "%f\n", interval_threshold);
      mst_write(ninj_fc_learning_file, fd, line, strlen(line));    


      if (matching_id_to_delays_umap.find(matching_id) == 
	  matching_id_to_delays_umap.end()) {
	//	NIN_DBG("write vec 0");
	sprintf(line, "0\n");
	mst_write(ninj_fc_learning_file, fd, line, strlen(line));    
      } else {
	//	NIN_DBG("write vec N");
	delay_vec = matching_id_to_delays_umap[matching_id];
	sprintf(line, "%lu\n", delay_vec->size());
	mst_write(ninj_fc_learning_file, fd, line, strlen(line));    
	
	for (int i = 0; i < delay_vec->size(); i++) {
	  sprintf(line, "%f\n", delay_vec->at(i));
	  mst_write(ninj_fc_learning_file, fd, line, strlen(line));    
	}
      }
      //      NIN_DBG("write vec ... finished");
    }
  }
  mst_close(ninj_fc_learning_file, fd);
  NIN_DBGI(0, "Learning file written to %s directory", ninj_fc_dir);

  return;
}


void ninj_fc_init(int argc, char **argv)
{
  char *env;

  PMPI_Comm_size(MPI_COMM_WORLD, &ninj_fc_comm_size);



  ninj_fc_ring_buffer = (double*)malloc(sizeof(double) * NINJ_FC_RING_BUFF_SIZE);
  for (int i = 0; i < NINJ_FC_RING_BUFF_SIZE; i++) {
    ninj_fc_ring_buffer[i] = 0;
  }
  ninj_fc_ring_buffer_head_index = 0;
  ninj_fc_ring_buffer_tail_index = 0;
  ninj_fc_mtu_transmit_time_usec = NINJ_FC_PACKET_TRANSMIT_MODEL(ninj_fc_mtu_size);

  if (NULL == (env = getenv(NIN_CONF_PATTERN))) {
    //    NIN_DBGI(0, "getenv failed: Please specify %s (%s:%s:%d)", NIN_CONF_PATTERN, __FILE__, __func__, __LINE__);
    //    exit(0);
    ninj_fc_pattern = NIN_CONF_PATTERN_MODEL;
  } else {
    ninj_fc_pattern = atoi(env);
  }
  if(nin_my_rank == 0) fprintf(stderr, " %s: %d\n", NIN_CONF_PATTERN, ninj_fc_pattern);

  if (ninj_fc_pattern == NIN_CONF_PATTERN_RAND) {
    if (NULL == (env = getenv(NIN_CONF_RAND_RATIO))) {
      NIN_DBGI(0, "getenv failed: Please specify %s (%s:%s:%d)", NIN_CONF_RAND_RATIO, __FILE__, __func__, __LINE__);
      exit(0);
    }
    ninj_fc_rand_ratio = atof(env);
    if (ninj_fc_rand_ratio <= 0 || 100 < ninj_fc_rand_ratio) {
      NIN_DBGI(0, "getenv failed: Please specify (0 >) %s (<= 100) (%s:%s:%d)", NIN_CONF_RAND_DELAY, __FILE__, __func__, __LINE__);
      exit(0);
    }
    NIN_DBGI(0, " NIN_RAND_RATIO: %f %% of Send will be delayed", ninj_fc_rand_ratio);
    if (NULL == (env = getenv(NIN_CONF_RAND_DELAY))) {
      NIN_DBGI(0, "getenv failed: Please specify %s (%s:%s:%d)", NIN_CONF_RAND_DELAY, __FILE__, __func__, __LINE__);
      exit(0);
    }
    ninj_fc_rand_delay_usec = atof(env);
    if(nin_my_rank == 0) fprintf(stderr, " NIN_RAND_DELAY: %d usec\n", ninj_fc_rand_delay_usec);

  } else if (ninj_fc_pattern == NIN_CONF_PATTERN_MODEL) {
    if (NULL == (env = getenv(NIN_CONF_MODEL_MODE))) {
      //      NIN_DBGI(0, "getenv failed: Please specify %s (%s:%s:%d)", NIN_CONF_MODEL_MODE, __FILE__, __func__, __LINE__);
      //      exit(0);
      ninj_fc_model_mode = NIN_CONF_MODEL_PASSIVE;
    } else {
      ninj_fc_model_mode = atoi(env);
    }
    if(nin_my_rank == 0) fprintf(stderr, " %s: %d\n", NIN_CONF_MODEL_MODE, ninj_fc_model_mode);

    if (NULL == (env = getenv("NIN_DIR"))) {
      //      NIN_DBGI(0, "getenv failed: Please specify %s (%s:%s:%d)", "NIN_DIR", __FILE__, __func__, __LINE__);
      //      exit(0);
      ninj_fc_dir = "./.ninja";
    } else {
      ninj_fc_dir = env;
    }
    mkdir(ninj_fc_dir, S_IRWXU);
    sprintf(ninj_fc_learning_file, "./%s/%s.%d.nin", ninj_fc_dir, basename(argv[0]), nin_my_rank);
    if(nin_my_rank == 0) fprintf(stderr, " %s: %s\n", "NIN_DIR", ninj_fc_dir);

    if (ninj_fc_model_mode == NIN_CONF_MODEL_ACTIVE) {
      ninj_fc_read_learning_file();
    }
  }


  if (ninj_fc_model_mode == NIN_CONF_MODEL_ACTIVE) ninj_fc_init_victim_dest();

  ninj_fc_msg_count[NINJ_FC_SEND_COUNT] = ninj_fc_msg_count[NINJ_FC_RECV_COUNT] = 0;
  return;
}


void ninj_fc_finalize()
{
  if (ninj_fc_pattern == NIN_CONF_PATTERN_MODEL) {
    ninj_fc_write_learning_file();
  }
  return;
}


void ninj_fc_get_delay(int dest, int tag, int comm_id, int *delay_flag, double *send_time)
{
  *delay_flag = 0;
  double base_time;
  switch(ninj_fc_pattern) {
  case NIN_CONF_PATTERN_FREE:
    /*Do nothing*/
    break;
  case NIN_CONF_PATTERN_RAND:
    ninj_fc_get_delay_random(dest, tag, comm_id, delay_flag, send_time, &base_time);
    break;
  case NIN_CONF_PATTERN_MODEL:
    ninj_fc_get_delay_model(dest, tag, comm_id, delay_flag, send_time, &base_time);
    break;
  default:
    NIN_DBG("No such NIN_PATTERN: %d", ninj_fc_pattern);
    exit(0);
    break;
  }

  if (*delay_flag) {
    double delay = *send_time - base_time;
    delay = (delay < 0)? 0:delay;
    ninj_fc_update_min_delay(NINJ_FC_MSG_ID(tag, comm_id), delay);
  }

  //  ninj_fc_get_delay_const(dest, delay_flag, send_time);

  // if (*delay_flag) {
  //   NIN_DBG("delay !");
  // }
  return;
}

void ninj_fc_do_model_tuning()
{
#ifdef ENABLE_THRESHOLD_TUNING
  ninj_fc_queue_length_threshold--;
  if (ninj_fc_rb_gmax_length < 0 || ninj_fc_queue_length_threshold < 0) {
    MPI_Allreduce(&ninj_fc_rb_max_length, &ninj_fc_rb_gmax_length, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
    ninj_fc_queue_length_threshold = ninj_fc_rb_gmax_length;
    /*K-means*/
  }
  NIN_DBGI(0, "threshold: %d", ninj_fc_queue_length_threshold);
#endif
  return;
}

static void ninj_fc_update_delay_threshold(size_t matching_id, vector<double> *delta_vec, double delta_threshold)
{
  vector<double> *delay_vec;
  double delta_part_sum = 0;
  int call_count = 0;
  size_t delta_vec_size;
  double delta;
  /* Update delay */
  if (matching_id_to_delays_umap.find(matching_id) == 
      matching_id_to_delays_umap.end()){
    delay_vec = new vector<double>;
    matching_id_to_delays_umap[matching_id] = delay_vec;
  } else {
    delay_vec = matching_id_to_delays_umap[matching_id];
  }
  
  delta_vec_size = delta_vec->size();
  for (int i = 0; i < delta_vec_size; i++) {
    delta = delta_vec->at(i);
    delta_part_sum += delta;
        
    if(matching_id == 2220000) NIN_DBGI(32, "delta: %f > %f", delta, delta_threshold);
    if (delta >= delta_threshold) {
      /*If separete send call detected, ... */
      if (delay_vec->size() > call_count) {
	/*If already have the delay */
	if (delay_vec->at(call_count) < delta_part_sum) {
	  /*If longer delay between call_set, update delay  */
	  delay_vec->at(call_count) = delta_part_sum;
	}
      } else {
	/*If this is first delay */
	delay_vec->push_back(delta_part_sum);
      }
      if(matching_id == 2220000) NIN_DBGI(32, "delay: %f", delay_vec->at(call_count));
      call_count++;
      delta_part_sum = 0;
    }
  }
  delta_vec->clear();

#if 0
  NIN_DBG("Threshold: %f", delta_threshold);
  for (int i = 0; i < delay_vec->size(); i++) {
    NIN_DBG( " %lu:%d: delay %f", matching_id, i, delay_vec->at(i));
  }
#endif

  return;
}


int ninj_fc_update_delay()
{
  int is_updated;
  size_t matching_id;
  vector<double> *delta_vec;
  double delta_sum;
  double delta_threshold;
  double delta;
  double current_delay;
  int delta_vec_size;
  int call_set_count = 0;

  unordered_map<size_t, vector<double>*>::const_iterator it;
  unordered_map<size_t, vector<double>*>::const_iterator it_end;
  it = matching_id_to_send_intervals_umap.cbegin();
  it_end = matching_id_to_send_intervals_umap.cend();
  /* For each matching id */
  for (; it != it_end; it++) {
    /* Compute how many call set count is in this epoch */
    matching_id = it->first;
    delta_vec = it->second;
#if 1
    delta_threshold = ninj_fc_get_min_delay(matching_id);
#else
    NIN_DBG("id: %lu, %p %lu", matching_id, delta_vec, delta_vec->size());
    delta_vec_size = delta_vec->size();
    delta_sum = 0;
    for (int i = 0; i < delta_vec_size; i++) {
      delta_sum += delta_vec->at(i);
    }
    delta_threshold = delta_sum / delta_vec_size;
#endif

    if (delta_threshold < NINJ_FC_THREAD_OVERHEAD_SEC) {
      delta_threshold = NINJ_FC_THREAD_OVERHEAD_SEC;
    }
    ninj_fc_update_delay_threshold(matching_id, delta_vec, delta_threshold);

    // if ()
    // matching_id_to_delays_umap[];
  }
  return is_updated;
}

void ninj_fc_report_recv()
{
  /* Count # of recv for In-flight msg detection*/
  ninj_fc_msg_count[NINJ_FC_RECV_COUNT]++;
}

void ninj_fc_report_send(size_t size_bytes, int tag, int comm_id)
{
  timeval tval;
  int mtu_packet_num;
  double last_packet_delay_usec;
  double call_time_sec;
  double delay_usec = 0;
  int next_tail_index;
  ninj_fc_packet_transmit_model(size_bytes, &mtu_packet_num, &last_packet_delay_usec);
  call_time_sec = NIN_get_time();
  //  NIN_DBGI(0, "current: %f", call_time_sec);
  delay_usec = ninj_fc_mtu_transmit_time_usec;
  while (mtu_packet_num-- > 0 || last_packet_delay_usec > 0) {
    ninj_fc_ring_buffer[ninj_fc_ring_buffer_tail_index] = call_time_sec + delay_usec / 1e6;    
    //    NIN_DBGI(0, "  planded delay: %f (index: %d)", call_time_sec + delay_usec/1e6, ninj_fc_ring_buffer_tail_index);
    next_tail_index = (ninj_fc_ring_buffer_tail_index + 1) % NINJ_FC_RING_BUFF_SIZE;
    //    NIN_DBG("send_time: %f, time: %f", ninj_fc_ring_buffer[ninj_fc_ring_buffer_tail_index], call_time_sec);
    while (next_tail_index == ninj_fc_ring_buffer_head_index) {
      NIN_DBG("ring buffer is full. Waiting until a packet space available: head: %d (time: %f), tail: %d (length: %d)",
      	      ninj_fc_ring_buffer_head_index, 
      	      ninj_fc_ring_buffer[ninj_fc_ring_buffer_tail_index],
      	      ninj_fc_ring_buffer_tail_index, NINJ_FC_RING_BUFF_SIZE);
      ninj_fc_ring_buffer_head_progress();
    }
    ninj_fc_ring_buffer_tail_index = next_tail_index;
    if (mtu_packet_num < 0) break;
    delay_usec += (mtu_packet_num == 0)? last_packet_delay_usec:ninj_fc_mtu_transmit_time_usec;
  }
  /* Count # of send for In-flight msg detection*/
  ninj_fc_msg_count[NINJ_FC_SEND_COUNT]++;
  /* Record send interval for tuning*/
  ninj_fc_record_send_interval(call_time_sec, tag, comm_id); /*For auto delay tuning*/
  return;
}

static void ninj_fc_clear_for_next_epoch()
{
  size_t matching_id;
  unordered_map<size_t, double>::iterator it;
  unordered_map<size_t, double>::iterator it_end;

  it = previous_send_times_umap.begin();
  it_end = previous_send_times_umap.end();
  for (; it != it_end; it++) {
    matching_id = it->first;
    previous_send_times_umap[matching_id] = -1;
    if (matching_id_to_send_intervals_umap.find(matching_id) != 
	matching_id_to_send_intervals_umap.end()) {
      matching_id_to_send_intervals_umap[matching_id]->clear();
    }
    if (matching_id_to_active_noise_index_umap.find(matching_id) != 
	matching_id_to_active_noise_index_umap.end()) {
      matching_id_to_active_noise_index_umap[matching_id] = 0;
    }
  }
  ninj_fc_msg_count[NINJ_FC_SEND_COUNT] = ninj_fc_msg_count[NINJ_FC_RECV_COUNT] = 0;
  ninj_fc_delta_sum = 0;

  return;
}


void ninj_fc_check_in_flight_msg(MPI_Comm comm)
{
  MPI_Request req;
  if (comm == MPI_COMM_WORLD) {
    //    NIN_DBGI(0, "%d %d", ninj_fc_msg_count[NINJ_FC_SEND_COUNT], ninj_fc_msg_count[NINJ_FC_RECV_COUNT]);
    PMPI_WRAP(PMPI_Iallreduce(ninj_fc_msg_count, ninj_fc_msg_count_buff, 2, MPI_INT, MPI_SUM, comm, &req), __func__);
    MPI_Wait(&req, MPI_STATUS_IGNORE);
    if (ninj_fc_msg_count_buff[NINJ_FC_SEND_COUNT] == ninj_fc_msg_count_buff[NINJ_FC_RECV_COUNT]) {
      if (ninj_fc_model_mode == NIN_CONF_MODEL_PASSIVE) ninj_fc_update_delay();  
      if (ninj_fc_model_mode == NIN_CONF_MODEL_ACTIVE) ninj_fc_init_victim_dest();
      ninj_fc_clear_for_next_epoch();
      //      NIN_DBGI(48, "No infligth");
    } else {
      //      NIN_DBGI(48, "Infligth: %d %d", ninj_fc_msg_count[NINJ_FC_SEND_COUNT], ninj_fc_msg_count[NINJ_FC_RECV_COUNT]);
    }
  }
  return;
}


