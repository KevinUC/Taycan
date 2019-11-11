#include <assert.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tps.h>
#include <sem.h>

/*
 *
 *  Test if the library can capture TPS protection errors.
 *  The program should output "TPS protection error!" and then quit on seg
 *  fault.
*/

void *latestMmapAddr = NULL; /* address returned by mmap accessible */

void *__real_mmap(void *addr, size_t len, int prot, int flags, int fildes, off_t off);

void *__wrap_mmap(void *addr, size_t len, int prot, int flags, int fildes, off_t off)
{
    latestMmapAddr = __real_mmap(addr, len, prot, flags, fildes, off);
    return latestMmapAddr;
}

static char msg1[TPS_SIZE] = "Hello world!\n";

static sem_t sem1;

void *thread2(void *arg)
{
    /* invalid access to thread1's TPS area */
    char *addr = latestMmapAddr;

    addr[0] = 'a'; /* causes TPS protection error */

    sem_up(sem1);

    return NULL;
}

void *thread1(void *arg)
{
    pthread_t tid;

    /* create tps for thread 1 */
    tps_create();
    tps_write(0, TPS_SIZE, msg1);

    /* Create thread 2 and get blocked */
    pthread_create(&tid, NULL, thread2, NULL);

    sem_down(sem1);

    return NULL;
}

int main(int argc, char **argv)
{
    pthread_t tid;

    /* Create a semaphore for thread synchro */
    sem1 = sem_create(0);

    /* Init TPS API */
    tps_init(1);

    /* Create thread 1 and wait */
    pthread_create(&tid, NULL, thread1, NULL);
    pthread_join(tid, NULL);

    /* Destroy resources and quit */
    sem_destroy(sem1);

    return 0;
}