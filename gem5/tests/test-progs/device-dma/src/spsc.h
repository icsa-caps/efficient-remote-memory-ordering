//taken from:
//https://www.cs.cmu.edu/afs/cs/academic/class/15492-f07/www/pthreads.html
//https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/stdatomic.h.html
//
//
//Google AI:
//
/*
Memory Order
The atomic_store function, by default, uses memory_order_seq_cst, which provides sequential consistency. For more fine-grained control over memory ordering, atomic_store_explicit can be used with different memory orderings such as memory_order_relaxed, memory_order_release, or memory_order_seq_cst.
*/

//https://en.cppreference.com/w/c/atomic/memory_order


#ifndef SPSC_H
#define SPSC_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h> 
#include <stdatomic.h>

struct queue_data {
	volatile uint64_t queue_size;
	volatile uint64_t * queue;
	volatile uint64_t * head;
	volatile uint64_t * tail;
};
void *producer(void *);
void *consumer(void *);

#endif



