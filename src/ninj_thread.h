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
