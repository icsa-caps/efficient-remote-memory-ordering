#include "spsc.h"

int main()
{
    int queue_size = 128;
    volatile uint64_t *queue = malloc(queue_size * sizeof(uint64_t));
    volatile uint64_t *head = malloc(sizeof(uint64_t));
    volatile uint64_t *tail = malloc(sizeof(uint64_t));

    volatile struct queue_data *new_queue_data = malloc(sizeof(struct queue_data));
    new_queue_data->queue_size = queue_size;
    new_queue_data->queue = queue;
    new_queue_data->head = head;
    new_queue_data->tail = tail;

    atomic_store(head, 0);
    atomic_store(tail, 0);

    // Set up producer device

    // Start consumer
    consumer((void*) new_queue_data);

    exit(0);
}


void *consumer(void * temp)
{
    struct queue_data *qd = (struct queue_data *)temp;
    printf("CONSUMER: size of queue is %d \n", qd->queue_size);
    printf("CONSUMER: queue address %d \n", qd->queue);
    printf("CONSUMER: head address %d \n", qd->head);
    printf("CONSUMER: tail address %d \n", qd->tail);

    int counter = 0;
    int oldvalue = -1;
    uint64_t headvalue = atomic_load(qd->head);
    while (counter < 10000) {
        uint64_t tailvalue = atomic_load_explicit(qd->tail, memory_order_acquire);
        while (counter < 10000) {

            // use 2 bit trick to avoid using modulo with queue that is size of a power of 2
            if (headvalue < tailvalue && counter < 10000) {
                // this means that we can progress
                uint64_t readvalue = atomic_load_explicit(&((qd->queue)[headvalue % qd->queue_size]), memory_order_relaxed);
                // printf("readvalue %d\n", readvalue);
                if ((int)readvalue != oldvalue + 1) {
                    printf("FAILURE %d %d", readvalue, oldvalue);
                    printf("FAILURE");
                    printf("FAILURE");
                    printf("FAILURE");
                    printf("FAILURE");
                    printf("FAILURE");
                    printf("FAILURE");
                    printf("FAILURE");
                    printf("FAILURE");
                    return NULL;
                }
                oldvalue = readvalue;
                counter++;
                headvalue++;

                // only perform the update after we read everything
                if (headvalue == tailvalue || counter == 10000) {
                    atomic_store_explicit(qd->head, headvalue, memory_order_release);
                }
            }
            else {
                break;
            }
        }
    }
}


