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


#ifndef NIN_SPSC_QUEUE_H_
#define NIN_SPSC_QUEUE_H_

#include <unistd.h>
#include <ninj_thread.h>

#if ( (__GNUC__ == 4) && (__GNUC_MINOR__ >= 1) || __GNUC__ > 4) && \
 (defined(__x86_64__) || defined(__i386__))
#define __memory_barrier __sync_synchronize
#endif



// load with 'consume' (data-dependent) memory ordering
template<typename T> T load_consume(T const* addr)
{
    // hardware fence is implicit on x86
    T v = *const_cast<T const volatile*>(addr);
    //    __memory_barrier(); // compiler fence
    __sync_synchronize(); // compiler fence
    return v;
}

// store with 'release' memory ordering
template<typename T>
void store_release(T* addr, T v)
{
    // hardware fence is implicit on x86
  //    __memory_barrier(); // compiler fence
    __sync_synchronize(); // compiler fence
    *const_cast<T volatile*>(addr) = v;
}


// cache line size on modern x86 processors (in bytes)
const size_t cache_line_size = 64;
// single-producer/single-consumer queue
template<typename T>
class nin_spsc_queue
{
private:
  //

public:
  size_t enqueue_count;
  size_t dequeue_count;
  nin_spsc_queue() {
    node* n = new node;
    n->next_ = 0;
    tail_ = head_ = first_= tail_copy_ = n;
    enqueue_count = 0;
    dequeue_count = 0;
  }

  ~nin_spsc_queue()
  {
      node* n = first_;
      do
      {
          node* next = n->next_;
          delete n;
          n = next;
      }
      while (n);
  }

  size_t rough_size()
  {
    /*TODO: find out why this pointer sometime becomes NULL*/
    while (this == NULL) {
      NIN_DBG("this: %p", this);
      sleep(1);
    }
    return enqueue_count - dequeue_count;

  }

  size_t get_enqueue_count() {
    return enqueue_count;
  }

  size_t get_dequeue_count() {
    return dequeue_count;
  }

  void enqueue(T v)
  {
      node* n = alloc_node();
      n->next_ = 0;
      n->value_ = v;
      //      nin_dbg("----- push: %p", v);
      store_release(&head_->next_, n);
      enqueue_count++;
      head_ = n;
  }

  // returns 'false' if queue is empty
/*
  bool dequeue(T& v)
  {
      if (load_consume(&tail_->next_))
		{
          v = tail_->next_->value_;
          store_release(&tail_, tail_->next_);
          return true;
      }
      else
      {
          return false;
      }
  }
*/

  T dequeue()
  {
	  T v;
      if (load_consume(&tail_->next_))
		{
          v = tail_->next_->value_;
          store_release(&tail_, tail_->next_);
          dequeue_count++;
          return v;
      }
      else
      {
          return (T)NULL;
      }
  }

  T front()
  {
    /*TODO: find out why this pointer sometime becomes NULL*/
    T v;
    if (load_consume(&tail_->next_)) {
      v = tail_->next_->value_;
      return v;
    } else {
      return (T)NULL;
    }
  }

private:
  // internal node structure
  struct node
  {
      node* next_;
      T value_;
  };

  // consumer part
  // accessed mainly by consumer, infrequently be producer
  node* tail_; // tail of the queue

  // delimiter between consumer part and producer part,
  // so that they situated on different cache lines
  char cache_line_pad_ [cache_line_size];

  // producer part
  // accessed only by producer
  node* head_; // head of the queue
  node* first_; // last unused node (tail of node cache)
  node* tail_copy_; // helper (points somewhere between first_ and tail_)

  node* alloc_node()
  {
      // first tries to allocate node from internal node cache,
      // if attempt fails, allocates node via ::operator new()

      if (first_ != tail_copy_)
      {
          node* n = first_;
          first_ = first_->next_;
          return n;
      }
      tail_copy_ = load_consume(&tail_);
      if (first_ != tail_copy_)
      {
          node* n = first_;
          first_ = first_->next_;
          return n;
      }
      node* n = new node;
      return n;
  }

  nin_spsc_queue(nin_spsc_queue const&);
  nin_spsc_queue& operator = (nin_spsc_queue const&);

};


#endif /* NIN_SPSC_QUEUE_H_ */
