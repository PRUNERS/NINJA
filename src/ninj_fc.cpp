#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>

#include <unordered_map>

#include "mpi.h"
#include "ninj_fc.h"
#include "nin_util.h"

#define NINJ_FC_QUEUED_PACKET_NUM(head, tail) \
  (((tail - head) + NINJ_FC_RING_BUFF_SIZE) % NINJ_FC_RING_BUFF_SIZE)

#define NINJ_FC_PACKET_TRANSMIT_MODEL(msg_size) \
   (ninj_fc_model_packet_latency_usec + msg_size / ninj_fc_model_packet_throughput_bpusec)

#define NINJ_FC_MTU (2048)
#define NINJ_FC_RING_BUFF_SIZE (1024 * 1024)
#define NINJ_FC_QUEUE_CAPACITY (4)

#define NINJ_FC_MODEL_PACKET_LATENCY    (0.2)  // in usec
#define NINJ_FC_MODEL_PACKET_THROUGHPUT (312.5) // in bytes/usec e.g. (3.2GB/sec = 312.5 bytes/usec)

using namespace std;
static unordered_map<int, double> rank_to_last_send_time_umap;
static double *ninj_fc_ring_buffer;
static int ninj_fc_ring_buffer_head_index = 0, ninj_fc_ring_buffer_tail_index = 0;
static timeval ninj_fc_base_time;
static double ninj_fc_mtu_transmit_time_usec;

static int ninj_fc_mtu_size = NINJ_FC_MTU;
static int ninj_fc_queue_capacity = NINJ_FC_QUEUE_CAPACITY;
static double ninj_fc_model_packet_latency_usec    = NINJ_FC_MODEL_PACKET_LATENCY;
static double ninj_fc_model_packet_throughput_bpusec = NINJ_FC_MODEL_PACKET_THROUGHPUT;
static double ninj_fc_queue_length_threshold = 4;

static int ninj_fc_delay_adjustment_for_ordered_send(int dest, int delay_flag, double current_time, double *send_time)
{  
  int is_adjusted = 0;
  double last_send_time = rank_to_last_send_time_umap[dest];
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

static void ninj_fc_get_delay_random(int dest, int *delay_flag, double *send_time)
{
  double delay_sec;
  int is_delayed, is_adjusted;
  double current_time   = NIN_get_time();
  is_delayed = NIN_get_rand(1000000) <= 2;
  is_delayed = 1;
  if (!is_delayed) {
    *delay_flag = 0;
    *send_time = current_time;
  } else {
    *delay_flag = 1;
    *send_time = current_time + 100.0 / 1e6;
  }
  is_adjusted = ninj_fc_delay_adjustment_for_ordered_send(dest, *delay_flag, current_time, send_time);
  if (is_adjusted) *delay_flag = 1;
  return;
}


static void ninj_fc_get_delay_const(int dest, int *delay_flag, double *send_time)
{
  double delay_sec;
  int is_delayed, is_adjusted;
  double current_time   = NIN_get_time();
  *delay_flag = 1;
  *send_time = current_time + 1 / 1e6;
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

static double ninj_fc_get_time_of_packet_transmit(int num_packets)
{
  double time;
  int index = (ninj_fc_ring_buffer_head_index + num_packets) % NINJ_FC_RING_BUFF_SIZE;
  time = ninj_fc_ring_buffer[index];
  return time;
}

static void ninj_fc_get_delay_model(int dest, int *delay_flag, double *send_time)
{
  int enqueued_packet_num;
  double current_time   = NIN_get_time();
  int is_adjusted;
  ninj_fc_ring_buffer_head_progress();
  enqueued_packet_num = NINJ_FC_QUEUED_PACKET_NUM(ninj_fc_ring_buffer_head_index, ninj_fc_ring_buffer_tail_index);
  if (enqueued_packet_num >= ninj_fc_queue_length_threshold) {
    *delay_flag = 1;
    *send_time = ninj_fc_get_time_of_packet_transmit(enqueued_packet_num - ninj_fc_queue_length_threshold + 1);
    NIN_DBG("delayed send time: %f", *send_time - current_time);
  }
  is_adjusted = ninj_fc_delay_adjustment_for_ordered_send(dest, *delay_flag, current_time, send_time);
  if(is_adjusted) *delay_flag = 1;
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


void ninj_fc_init()
{
  ninj_fc_ring_buffer = (double*)malloc(sizeof(double) * NINJ_FC_RING_BUFF_SIZE);
  for (int i = 0; i < NINJ_FC_RING_BUFF_SIZE; i++) {
    ninj_fc_ring_buffer[i] = 0;
  }
  ninj_fc_ring_buffer_head_index = 0;
  ninj_fc_ring_buffer_tail_index = 0;
  ninj_fc_mtu_transmit_time_usec = NINJ_FC_PACKET_TRANSMIT_MODEL(ninj_fc_mtu_size);
  return;
}

void ninj_fc_get_delay(int dest, int *delay_flag, double *send_time)
{
  ninj_fc_get_delay_const(dest, delay_flag, send_time);
  //  ninj_fc_get_delay_random(dest, delay_flag, send_time);
  // ninj_fc_get_delay_model(dest, delay_flag, send_time);
  return;
}


void ninj_fc_report_send(size_t size_bytes)
{
  timeval tval;
  int mtu_packet_num;
  double last_packet_delay_usec;
  double call_time_sec;
  double delay_usec;
  int next_tail_index;
  ninj_fc_packet_transmit_model(size_bytes, &mtu_packet_num, &last_packet_delay_usec);
  gettimeofday(&tval, NULL);
  call_time_sec = tval.tv_sec + tval.tv_usec / 1e6;
  delay_usec += ninj_fc_mtu_transmit_time_usec;
  while (mtu_packet_num-- > 0 || last_packet_delay_usec > 0) {
    ninj_fc_ring_buffer[ninj_fc_ring_buffer_tail_index] = call_time_sec + delay_usec / 1e6;    
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

  return;
}



