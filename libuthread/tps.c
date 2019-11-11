#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "queue.h"
#include "thread.h"
#include "tps.h"

struct Page
{
	void *_pageAddr; /* address to the start of page */
	int _refCount;   /* count number of TPS referencing to this page */
} Page;

typedef struct Page *page_t;

struct TPS
{
	pthread_t _tid;
	page_t _page;
} TPS;

typedef struct TPS *tps_t;

queue_t tpsQueue; /* queue of tps structs */
int init = 0;	 /* check if the TPS library has been initialized */

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

	if (currentTps->_page->_pageAddr == addr)
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
		return 0; /* TPS area has not been allocated */
	}

	if (address != NULL)
	{
		*address = tps;
	}

	return 1; /* TPS area has been allocated */
}

int isTPSReadWriteValid(size_t offset, size_t length,
						char *buffer, tps_t *address)
{
	pthread_t tid = pthread_self();

	int hasTPS = hasTPSBeenAllocated(tid, address);
	int withinBoundary = (int)offset >= 0 && (int)offset + (int)length <= TPS_SIZE;
	int isBufValid = buffer != NULL;

	return hasTPS && withinBoundary && isBufValid;
}

static void segv_handler(int sig, siginfo_t *si, void *context)
{

	void *p_fault = (void *)((uintptr_t)si->si_addr & ~(TPS_SIZE - 1));

	tps_t tps = NULL;

	int ret = queue_iterate(tpsQueue, findTPSPageAddr, p_fault, (void **)&tps);

	if (ret != -1 && tps != NULL && tps->_page->_pageAddr == p_fault)
	{
		/* match detected */
		fprintf(stderr, "TPS protection error!\n");
	}

	/* In any case, restore the default signal handlers */
	signal(SIGSEGV, SIG_DFL);
	signal(SIGBUS, SIG_DFL);

	/* And transmit the signal again in order to cause the program to crash */ raise(sig);
}

int tps_init(int segv)
{
	if (init)
	{
		return -1; /* has already been initialized */
	}

	tpsQueue = queue_create();

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

	init = 1;

	return 0;
}

int tps_create(void)
{

	if (!init)
	{
		return -1;
	}

	pthread_t tid = pthread_self();

	/* first check if current thread already has a TPS area */

	if (hasTPSBeenAllocated(tid, NULL))
	{
		return -1;
	}

	tps_t newTPS = malloc(sizeof(TPS));

	newTPS->_tid = tid;
	newTPS->_page = malloc(sizeof(Page));

	newTPS->_page->_refCount = 1;
	newTPS->_page->_pageAddr = mmap(NULL, TPS_SIZE,
									PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (newTPS->_page->_pageAddr == NULL)
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

	if (!init)
	{
		return -1;
	}

	pthread_t tid = pthread_self();
	tps_t tps = NULL;

	if (!hasTPSBeenAllocated(tid, &tps))
	{
		return -1; /* no TPS area associated with thread tid exists */
	}

	enter_critical_section();

	if (tps->_page->_refCount == 1)
	{
		/* remove page structure if not shared with other threads */
		munmap(tps->_page->_pageAddr, TPS_SIZE); /* remove TPS area */
		free(tps->_page);
		tps->_page = NULL;
	}

	queue_delete(tpsQueue, tps); /* remove node from queue */

	exit_critical_section();

	return 0;
}

int tps_read(size_t offset, size_t length, char *buffer)
{

	if (!init)
	{
		return -1;
	}

	tps_t tps = NULL;

	if (!isTPSReadWriteValid(offset, length, buffer, &tps))
	{
		return -1;
	}

	enter_critical_section();

	mprotect(tps->_page->_pageAddr + offset, length, PROT_READ); /* allow read */

	memcpy(buffer, tps->_page->_pageAddr + offset, length); /* read from TPS area */

	mprotect(tps->_page->_pageAddr + offset, length, PROT_NONE); /* reset */

	exit_critical_section();

	return 0;
}

int tps_write(size_t offset, size_t length, char *buffer)
{

	if (!init)
	{
		return -1;
	}

	tps_t tps = NULL;

	if (!isTPSReadWriteValid(offset, length, buffer, &tps))
	{
		return -1;
	}

	enter_critical_section();

	if (tps->_page->_refCount > 1)
	{
		/* the page is shared, so need to create a new one */

		tps->_page->_refCount -= 1; /* decrement reference count */

		page_t newPage = malloc(sizeof(Page));

		newPage->_refCount = 1;
		newPage->_pageAddr = mmap(NULL, TPS_SIZE,
								  PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

		/* clone content from the old page */

		mprotect(tps->_page->_pageAddr, TPS_SIZE, PROT_READ);
		mprotect(newPage->_pageAddr, TPS_SIZE, PROT_WRITE);

		memcpy(newPage->_pageAddr, tps->_page->_pageAddr, TPS_SIZE);

		mprotect(tps->_page->_pageAddr, TPS_SIZE, PROT_NONE);
		mprotect(newPage->_pageAddr, TPS_SIZE, PROT_NONE);

		tps->_page = newPage;
	}

	/* begin writing to TPS area */

	mprotect(tps->_page->_pageAddr + offset, length, PROT_WRITE);

	memcpy(tps->_page->_pageAddr + offset, buffer, length);

	mprotect(tps->_page->_pageAddr + offset, length, PROT_NONE);

	exit_critical_section();

	return 0;
}

int tps_clone(pthread_t tid)
{

	if (!init)
	{
		return -1;
	}

	tps_t srcTPS = NULL;

	int isSrcTPSInvalid = !hasTPSBeenAllocated(tid, &srcTPS);
	int isTPSAllocated = hasTPSBeenAllocated(pthread_self(), NULL);

	if (isSrcTPSInvalid || isTPSAllocated)
	{
		return -1;
	}

	/* create TPS for current thread */

	enter_critical_section();

	if (srcTPS->_page == NULL)
	{
		/* TPS clone failure: target TPS has been destroyed */
		exit_critical_section();
		return -1;
	}

	tps_t newTPS = malloc(sizeof(TPS));

	newTPS->_tid = pthread_self();
	newTPS->_page = srcTPS->_page;

	newTPS->_page->_refCount += 1; /* increment reference count */

	queue_enqueue(tpsQueue, newTPS);

	exit_critical_section();

	return 0;
}
