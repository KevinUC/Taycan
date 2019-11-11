#include <assert.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tps.h>
#include <sem.h>

sem_t sem1;

void *thread1(void *arg)
{
    assert(tps_destroy() == -1); /* test error handling for tps_destroy */

    sem_up(sem1);

    return NULL;
}

int main(int argc, char **argv)
{
    char buffer[TPS_SIZE];
    char msg1[TPS_SIZE] = "Hello world!\n";

    sem1 = sem_create(0);

    /* calling any method prior to tps_init() is invalid */
    assert(tps_create() == -1);
    assert(tps_destroy() == -1);
    assert(tps_read(0, 1, buffer) == -1);
    assert(tps_write(0, 1, buffer) == -1);
    assert(tps_clone(pthread_self()) == -1);

    tps_init(1);               /* init tps library */
    assert(tps_init(1) == -1); /* tps_init() cannot be called more than once */

    tps_create(); /* create tps for main */

    assert(tps_create() == -1); /* main already has a tps */

    /* test error handling for tps_read */
    assert(tps_read(-1, 1, buffer) == -1);
    assert(tps_read(0, TPS_SIZE + 1, buffer) == -1);
    assert(tps_read(5, TPS_SIZE - 1, buffer) == -1);
    assert(tps_read(0, TPS_SIZE, NULL) == -1);

    /* test error handling for tps_write */
    assert(tps_write(-1, 1, msg1) == -1);
    assert(tps_write(0, TPS_SIZE + 1, msg1) == -1);
    assert(tps_write(5, TPS_SIZE - 1, msg1) == -1);
    assert(tps_write(0, TPS_SIZE, NULL) == -1);

    /* test error handling for tps_clone */
    assert(tps_clone(-1) == -1);
    assert(tps_clone(pthread_self()) == -1);

    pthread_t tid;

    pthread_create(&tid, NULL, thread1, NULL);

    sem_down(sem1);

    return 0;
}