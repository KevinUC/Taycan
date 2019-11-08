#include <stddef.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

#include "queue.h"
#include "sem.h"
#include "thread.h"

struct semaphore
{
	size_t _count;			/* internal count */
	queue_t _blockingQueue; /* queue of tids of blocked pthreads */
} semaphore;

sem_t sem_create(size_t count)
{
	sem_t sem = malloc(sizeof(semaphore));

	if (sem == NULL)
	{
		return NULL;
	}

	sem->_count = count;
	sem->_blockingQueue = queue_create();

	if (sem->_blockingQueue == NULL)
	{
		return NULL;
	}

	return sem;
}

int sem_destroy(sem_t sem)
{
	if (sem == NULL || queue_length(sem->_blockingQueue) > 0)
	{
		return -1;
	}

	queue_destroy(sem->_blockingQueue);
	free(sem);

	return 0;
}

int sem_down(sem_t sem)
{

	printf("%s\n", __func__);

	if (sem == NULL)
	{
		return -1;
	}

	enter_critical_section();

	if (sem->_count == 0)
	{
		/* no resource is currently avalible, go to sleep */
		pthread_t tid = pthread_self();
		queue_enqueue(sem->_blockingQueue, (void *)tid);
		thread_block();
	}
	else
	{
		sem->_count--;
	}

	exit_critical_section();

	return 0;
}

int sem_up(sem_t sem)
{

	printf("%s\n", __func__);

	if (sem == NULL)
	{
		return -1;
	}

	enter_critical_section();

	if (sem->_count == 0)
	{
		/* check if any thread is blocked */
		if (queue_length(sem->_blockingQueue) > 0)
		{
			/* release the oldest blocked thread */

			pthread_t tid;
			queue_dequeue(sem->_blockingQueue, (void **)&tid);

			int ret = thread_unblock(tid);
			assert(ret != -1);
		}
		else
		{
			sem->_count++; /* no other thread is blocked */
		}
	}
	else
	{
		/* no other thread is blocked */
		sem->_count++;
	}

	exit_critical_section();

	return 0;
}

int sem_getvalue(sem_t sem, int *sval)
{
	if (sem == NULL && sval == NULL)
	{
		return -1;
	}

	enter_critical_section();

	if (sem->_count > 0)
	{
		*sval = sem->_count;
	}
	else
	{
		*sval = -1 * queue_length(sem->_blockingQueue);
	}

	exit_critical_section();

	return 0;
}
