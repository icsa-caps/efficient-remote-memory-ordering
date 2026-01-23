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

    // Set up consumer device

    // Start producer
    producer((void*) new_queue_data);

    exit(0);
}

void *producer(void * temp)
{
    struct queue_data *qd = (struct queue_data *)temp;

    printf("PRODUCER: size of queue is %d \n", qd->queue_size);
    printf("PRODUCER: queue address %d \n", qd->queue);
    printf("PRODUCER: head address %d \n", qd->head);
    printf("PRODUCER: tail address %d \n", qd->tail);

    // atomic read the head and the tail
    // make sure that the tail is not going to pass the head

    int counter = 0;
    uint64_t tailvalue = atomic_load(qd->tail);
    while (counter < 10000) {
        uint64_t headvalue = atomic_load_explicit(qd->head, memory_order_acquire);

        while (counter < 10000) {
            if (tailvalue - headvalue < (qd->queue_size - 1) && counter < 10000) {
                // this means that we can progress
                atomic_store_explicit(&((qd->queue)[tailvalue % qd->queue_size]), counter, memory_order_relaxed);
                counter++;
                tailvalue++;
                if (tailvalue - headvalue == (qd->queue_size - 1) || counter == 10000) {
                    atomic_store_explicit(qd->tail, tailvalue, memory_order_release);
                }
            }
            else {
                break;
            }
        }
    }
}
