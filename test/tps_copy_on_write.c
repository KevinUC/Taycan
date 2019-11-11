#include <assert.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tps.h>
#include <sem.h>

/* Test if the library implements copy on write correctly */

void *latestMmapAddr = NULL; /* address returned by mmap accessible */

void *__real_mmap(void *addr, size_t len, int prot, int flags, int fildes, off_t off);

void *__wrap_mmap(void *addr, size_t len, int prot, int flags, int fildes, off_t off)
{
    latestMmapAddr = __real_mmap(addr, len, prot, flags, fildes, off);
    return latestMmapAddr;
}

static char msg1[TPS_SIZE] = "hello world!";
static char msg2[TPS_SIZE] = "HELLO WORLD!";

pthread_t tid[3];

static sem_t sem1, sem2, sem3;

void *thread2(void *arg)
{

    char buffer[TPS_SIZE];

    /* create tps for thread 2 */
    tps_clone(tid[1]);
    printf("thread 2 clones thread1's TPS\n");
    printf("thread 2's TPS address is also %p\n\n", latestMmapAddr);

    tps_write(0, 3, msg2);
    tps_read(0, TPS_SIZE, buffer);

    printf("thread 2 capitalizes the first three letters in the TPS\n");
    printf("thread 2's TPS now reads as %s\n", buffer);
    printf("now, thread 2's TPS address is changed to %p\n\n", latestMmapAddr);

    sem_up(sem1);   /* wake up main thread */
    sem_down(sem3); /* thread 2 goes to sleep */

    sem_up(sem2); /* wake up thread 1 */

    return NULL;
}

void *thread1(void *arg)
{

    char buffer[TPS_SIZE];

    /* create tps for thread 1 */
    tps_clone(tid[0]);
    printf("thread 1 clones main thread's TPS\n");
    printf("thread 1's TPS address is also %p\n\n", latestMmapAddr);

    /* Create thread 2 and get blocked */
    pthread_create(&tid[2], NULL, thread2, NULL);

    sem_down(sem2); /* thread 1 goes to sleep */

    sem_up(sem3);
    sem_down(sem2);

    tps_read(0, TPS_SIZE, buffer);
    printf("\nat this point, the content of thread 1's TPS remains unchanged: %s\n", buffer);

    return NULL;
}

int main(int argc, char **argv)
{

    char buffer[TPS_SIZE];
    tid[0] = pthread_self();

    /* Create three semaphores for thread synchro */
    sem1 = sem_create(0);
    sem2 = sem_create(0);
    sem3 = sem_create(0);

    /* Init TPS API */
    tps_init(1);

    tps_create();

    printf("\n\n**************************************\n\n");
    printf("main thread calls tps_create()\n");
    printf("address of main thread's TPS: %p\n", (void *)latestMmapAddr);
    tps_write(0, TPS_SIZE, msg1);
    printf("main thread writes %s to its TPS\n\n", msg1);

    /* Create thread 1 and wait */
    pthread_create(&tid[1], NULL, thread1, NULL);
    sem_down(sem1); /* wait for sem1 */

    /* now wakes up */

    tps_write(0, 1, msg2);
    printf("main thread capitalizes only the first letter\n");
    tps_read(0, TPS_SIZE, buffer);
    printf("main's TPS now reads as %s\n", buffer);
    printf("now, address of main thread's TPS is changed to: %p\n", (void *)latestMmapAddr);

    sem_up(sem2); /* wake up thread 1 */

    pthread_join(tid[0], NULL);
    pthread_join(tid[1], NULL);

    printf("\n**************************************\n\n");

    /* Destroy resources and quit */
    sem_destroy(sem1);
    sem_destroy(sem2);
    sem_destroy(sem3);

    return 0;
}