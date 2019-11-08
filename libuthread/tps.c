#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <assert.h>

#include "queue.h"
#include "thread.h"
#include "tps.h"

struct TPS
{
	pthread_t _tid;
	void *_storage;
} TPS;

typedef struct TPS *tps_t;

queue_t tpsQueue;			 /* queue of tps structs */


/* Callback function that finds the tps */
int findTPS(void *data, void *arg)
{
	pthread_t tid = (pthread_t)arg;
	tps_t currentTps = (tps_t)data;

	if (currentTps->_tid == tid)
	{
		return 1;
	}

	return 0;
}

/* Callback function that finds the tps page address */
int findTPSPageAddr(void *data, void *arg)
{
	void *addr = arg; /* address of beginning of page where error occurs */
	tps_t currentTps = (tps_t)data;

	if (currentTps->_storage == addr)
	{
		return 1;
	}

	return 0;
}

/* check if thread tid already has a TPS area */
int hasTPSBeenAllocated(pthread_t tid, tps_t *address)
{
	tps_t tps = NULL;

	enter_critical_section();

	int ret = queue_iterate(tpsQueue, findTPS, (void *)tid, (void **)&tps);

	exit_critical_section();

	if (ret == -1 || tps == NULL)
	{
		//printf("TPS has not been allocated \n");
		return 0; /* TPS area has not been allocated */
	}

	if (address != NULL)
	{
		*address = tps;
	}

	//printf("TPS has been allocated \n");
	return 1; /* TPS area has been allocated */
}

int isTPSReadWriteValid(size_t offset, size_t length,
						char *buffer, tps_t *address)
{
	pthread_t tid = pthread_self();

	int hasTPS = hasTPSBeenAllocated(tid, address);
	int withinBoundary = offset >= 0 && offset + length <= TPS_SIZE;
	int isBufValid = buffer != NULL;

	return hasTPS && withinBoundary && isBufValid;
}

static void segv_handler(int sig, siginfo_t *si, void *context)
{

	void *p_fault = (void *)((uintptr_t)si->si_addr & ~(TPS_SIZE - 1));

	tps_t tps = NULL;

	int ret = queue_iterate(tpsQueue, findTPSPageAddr, p_fault, (void **)&tps);

	if (ret != -1 && tps != NULL && tps->_storage == p_fault)
	{
		/* match detected */
		fprintf(stderr, "TPS protection error!\n");
	}
	else
	{
		fprintf(stderr, "Not TPS protecttion error, happy debugging!\n");
	}

	/* In any case, restore the default signal handlers */
	signal(SIGSEGV, SIG_DFL);
	signal(SIGBUS, SIG_DFL);

	/* And transmit the signal again in order to cause the program to crash */ raise(sig);
}

int tps_init(int segv)
{
	static int firstInvoation = 1;

	if (!firstInvoation)
	{
		printf("tps_init: already initialized! \n");
		return -1;
	}

	firstInvoation = 0;

	tpsQueue = queue_create();
	assert(tpsQueue != NULL);

	if (tpsQueue == NULL)
	{
		return -1;
	}

	if (segv)
	{
		/* set up tps protection handler */
		struct sigaction sa;
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = SA_SIGINFO;
		sa.sa_sigaction = segv_handler;
		sigaction(SIGBUS, &sa, NULL);
		sigaction(SIGSEGV, &sa, NULL);
	}

	return 0;
}

int tps_create(void)
{
	// printf("%s\n", __func__);
	pthread_t tid = pthread_self();

	/* first check if current thread already has a TPS area */

	if (hasTPSBeenAllocated(tid, NULL))
	{
		return -1;
	}

	tps_t newTPS = malloc(sizeof(TPS));

	newTPS->_tid = tid;
	newTPS->_storage = NULL;

	/* no read/write protection by default */
	newTPS->_storage = mmap(NULL, TPS_SIZE,
							PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	assert(newTPS->_storage != NULL);

	if (newTPS->_storage == NULL)
	{
		return -1; /* page allocation faliure */
	}

	enter_critical_section();

	queue_enqueue(tpsQueue, newTPS);

	exit_critical_section();

	return 0;
}

int tps_destroy(void)
{
	//printf("%s\n", __func__);
	pthread_t tid = pthread_self();

	tps_t tps = NULL;

	if (!hasTPSBeenAllocated(tid, &tps))
	{
		return -1; /* no TPS area associated with thread tid exists */
	}

	assert(tps != NULL);

	enter_critical_section();

	int ret = munmap(tps->_storage, TPS_SIZE); /* remove TPS area */
	assert(ret == 0);

	ret = queue_delete(tpsQueue, tps); /* remove node from queue */
	assert(ret == 0);

	exit_critical_section();

	return 0;
}

int tps_read(size_t offset, size_t length, char *buffer)
{
	//printf("%s\n", __func__);
	tps_t tps = NULL;
	int ret = 0;

	if (!isTPSReadWriteValid(offset, length, buffer, &tps))
	{
		printf("TPS read not valid \n");
		return -1;
	}

	assert(tps != NULL);

	enter_critical_section();

	ret = mprotect(tps->_storage + offset, length, PROT_READ); /* allow read */

	assert(ret == 0);

	memcpy(buffer, tps->_storage + offset, length); /* read from TPS area */

	ret = mprotect(tps->_storage + offset, length, PROT_NONE); /* reset */

	assert(ret == 0);

	exit_critical_section();

	return 0;
}

int tps_write(size_t offset, size_t length, char *buffer)
{
	//printf("%s\n", __func__);
	tps_t tps = NULL;
	int ret = 0;

	if (!isTPSReadWriteValid(offset, length, buffer, &tps))
	{
		printf("TPS write not valid \n");
		return -1;
	}

	assert(tps != NULL);

	enter_critical_section();

	ret = mprotect(tps->_storage + offset, length, PROT_WRITE);

	assert(ret == 0);

	memcpy(tps->_storage + offset, buffer, length); /* write to TPS area */

	ret = mprotect(tps->_storage + offset, length, PROT_NONE);

	assert(ret == 0);

	exit_critical_section();

	return 0;
}

int tps_clone(pthread_t tid)
{
	//printf("%s\n", __func__);
	tps_t srcTPS = NULL;
	tps_t tgtTPS = NULL;

	int isSrcTPSInvalid = !hasTPSBeenAllocated(tid, &srcTPS);
	int isTPSAllocated = hasTPSBeenAllocated(pthread_self(), NULL);

	if (isSrcTPSInvalid || isTPSAllocated)
	{
		printf("TPS clone failure\n");
		return -1;
	}

	assert(srcTPS != NULL);

	tps_create(); /* init a TPS area */

	isTPSAllocated = hasTPSBeenAllocated(pthread_self(), &tgtTPS);

	assert(tgtTPS != NULL && isTPSAllocated == 1);

	enter_critical_section();

	mprotect(tgtTPS->_storage, TPS_SIZE, PROT_WRITE);
	mprotect(srcTPS->_storage, TPS_SIZE, PROT_READ);

	/* naive clone TPS area */
	memcpy(tgtTPS->_storage, srcTPS->_storage, TPS_SIZE);

	mprotect(tgtTPS->_storage, TPS_SIZE, PROT_NONE);
	mprotect(srcTPS->_storage, TPS_SIZE, PROT_NONE);

	exit_critical_section();

	return 0;
}
