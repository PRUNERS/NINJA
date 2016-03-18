#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

#include <map>

#include "ninj_thread.h"
#include "nin_util.h"

using namespace std;

pthread_mutex_t ninj_thread_mpi_mutex = PTHREAD_MUTEX_INITIALIZER;

nin_spsc_queue<nin_delayed_send_request*> nin_thread_input, nin_thread_output;
map<double, nin_delayed_send_request*> ordered_delayed_send_request_map;

int debug_int  = -1;




static void add_delayed_send_request(nin_delayed_send_request *ds_req)
{
  double send_time = ds_req->send_time;
  while (ordered_delayed_send_request_map.find(send_time) != 
	 ordered_delayed_send_request_map.end()) {
    send_time += 1.0/1e6;
  }
  ds_req->send_time = send_time;
  ordered_delayed_send_request_map[ds_req->send_time] = ds_req;
  return;
}

void* run_delayed_send(void *args)
{
  int ret;
  nin_delayed_send_request* front_ds_req, *new_ds_req;
  map<double, nin_delayed_send_request*>::iterator it;
  int is_erased;
  double send_time;
  struct timespec stval;
  double a, b;
  stval.tv_sec = 0;
  stval.tv_nsec = 1;
  while (1) {
    /*Wait until delayed send request enqueued*/
    if (ordered_delayed_send_request_map.size() == 0) {
      while ((new_ds_req = nin_thread_input.dequeue()) == NULL) {
	//	a = NIN_get_time();
	nanosleep(&stval, NULL);//usleep(1);
	//	clock_nanosleep(CLOCK_REALTIME, 0, &stval, NULL);
	//	b = NIN_get_time();
	//	NIN_DBG("%f", b - a);
      }
      send_time = new_ds_req->send_time;
      add_delayed_send_request(new_ds_req);
    }
    it = ordered_delayed_send_request_map.begin();
    front_ds_req = it->second;


    /*Wait until send time expired*/
    while(front_ds_req->send_time >= NIN_get_time() && front_ds_req->is_final == 0) {
      /*  but if new request arrive, then update the next send request */
      if ((new_ds_req = nin_thread_input.dequeue()) != NULL) {
	add_delayed_send_request(new_ds_req);
	if (new_ds_req->send_time < front_ds_req->send_time) {
	  it = ordered_delayed_send_request_map.begin();
	  front_ds_req = it->second;
	}
      }
      nanosleep(&stval, NULL);//usleep(1);
    }
      
    if (front_ds_req->is_final) return NULL;

    /*Then, start expired send request*/
    PMPI_WRAP(ret = PMPI_Start(&front_ds_req->request), "MPI_Start");
    front_ds_req->is_started = 1;
    //    nin_thread_output.enqueue(front_ds_req);
    // NIN_DBG("send: dest: %d: tag: %d: req: %p, size: %lu, debug_int: %d, enqueuc: %lu, deqc: %lu", 
    //   	    front_ds_req->dest, front_ds_req->tag, front_ds_req->request, ordered_delayed_send_request_map.size(), debug_int, 
    // 	    nin_thread_input.get_enqueue_count(), nin_thread_input.get_dequeue_count());
    ordered_delayed_send_request_map.erase(it);
    front_ds_req = NULL;
  }
  return NULL;
}
