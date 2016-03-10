/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *  	replay server 
 *
 *   	http://www.ecsino.com/
 *
 *  	Copyright 2016 Danga Interactive, Inc.  All rights reserved.
 *		File Name 	: 	thread.c
 *  	Authors		:	Vict Ding <dszhazha@163.com>
 * 		Date   		: 	2016/2/29     
 *      
 */
 
/* An item in the connection queue. */

#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "server.h"
#include "log.h"

#define ITEMS_PER_ALLOC 64

typedef struct conn_queue_item CQ_ITEM;
struct conn_queue_item {
    sint32               	sfd;
    EN_CONN_STAT  			init_state;
	EN_CONN_TYPE 			enConnType;
    sint32               	event_flags;
    sint32               	read_buffer_size;
    CQ_ITEM          	*next;
};

/* A connection queue. */
typedef struct conn_queue CQ;
struct conn_queue {
    CQ_ITEM *head;
    CQ_ITEM *tail;
    pthread_mutex_t lock;
};

/* Lock to cause worker threads to hang up after being woken */
static pthread_mutex_t worker_hang_lock;

/* Free list of CQ_ITEM structs */
static CQ_ITEM *cqi_freelist;
static pthread_mutex_t cqi_freelist_lock;

/* Lock for global stats */
static pthread_mutex_t stats_lock = PTHREAD_MUTEX_INITIALIZER;


/*
 * Number of worker threads that have finished setting themselves up.
 */
static sint32 init_count = 0;
static pthread_mutex_t init_lock;
static pthread_cond_t init_cond;

/* Which thread we assigned a connection to most recently. */
static int last_thread = -1;

//static LIBEVENT_DISPATCHER_THREAD dispatcher_thread;

/*
 * Each libevent instance has a wakeup pipe, which other threads
 * can use to signal that they've put a new connection on its queue.
 */
static ST_LIBEVENT_THREAD *pstThreads;

extern ST_RELAY_STATUS		gstStats;

extern ST_CONN_INFO *CONN_NodeNew(const sint32 sfd, EN_CONN_STAT init_state,
                const sint32 event_flags, EN_CONN_TYPE enConnType,
                const sint32 read_buffer_size,
                struct event_base *base); 

static void THREAD_LibeventProcess(sint32 fd, sint16 which, void *arg);


void STATS_LOCK(void) 
{
    pthread_mutex_lock(&stats_lock);
}

void STATS_UNLOCK(void) 
{
    pthread_mutex_unlock(&stats_lock);
}

static void THREAD_WaitForRegistration(sint32 nthreads) 
{
    while (init_count < nthreads) 
	{
        pthread_cond_wait(&init_cond, &init_lock);
    }
}

static void THREAD_RegisterInitialized(void) 
{
    pthread_mutex_lock(&init_lock);
    init_count++;
    pthread_cond_signal(&init_cond);
    pthread_mutex_unlock(&init_lock);
    /* Force worker threads to pile up if someone wants us to */
    pthread_mutex_lock(&worker_hang_lock);
    pthread_mutex_unlock(&worker_hang_lock);
}

/*
 * Initializes a connection queue.
 */
static void cq_init(CQ *cq) 
{
    pthread_mutex_init(&cq->lock, NULL);
    cq->head = NULL;
    cq->tail = NULL;
}

/*
 * Looks for an item on a connection queue, but doesn't block if there isn't
 * one.
 * Returns the item, or NULL if no item is available
 */
static CQ_ITEM *cq_pop(CQ *cq) 
{
    CQ_ITEM *item;

    pthread_mutex_lock(&cq->lock);
    item = cq->head;
    if (NULL != item) {
        cq->head = item->next;
        if (NULL == cq->head)
            cq->tail = NULL;
    }
    pthread_mutex_unlock(&cq->lock);

    return item;
}

/*
 * Adds an item to a connection queue.
 */
static void cq_push(CQ *cq, CQ_ITEM *item) 
{
    item->next = NULL;

    pthread_mutex_lock(&cq->lock);
    if (NULL == cq->tail)
        cq->head = item;
    else
        cq->tail->next = item;
    cq->tail = item;
    pthread_mutex_unlock(&cq->lock);
}

/*
 * Returns a fresh connection queue item.
 */
static CQ_ITEM *cqi_new(void) 
{
    CQ_ITEM *item = NULL;
    pthread_mutex_lock(&cqi_freelist_lock);
    if (cqi_freelist) 
	{
        item = cqi_freelist;
        cqi_freelist = item->next;
    }
    pthread_mutex_unlock(&cqi_freelist_lock);

    if (NULL == item) 
	{
        int i;

        /* Allocate a bunch of items at once to reduce fragmentation */
        item = malloc(sizeof(CQ_ITEM) * ITEMS_PER_ALLOC);
        if (NULL == item) 
		{
			return NULL;
        }

        /*
         * Link together all the new items except the first one
         * (which we'll return to the caller) for placement on
         * the freelist.
         */
        for (i = 2; i < ITEMS_PER_ALLOC; i++)
            item[i - 1].next = &item[i];

        pthread_mutex_lock(&cqi_freelist_lock);
        item[ITEMS_PER_ALLOC - 1].next = cqi_freelist;
        cqi_freelist = &item[1];
        pthread_mutex_unlock(&cqi_freelist_lock);
    }

    return item;
}

/*
 * Frees a connection queue item (adds it to the freelist.)
 */
static void cqi_free(CQ_ITEM *item) 
{
    pthread_mutex_lock(&cqi_freelist_lock);
    item->next = cqi_freelist;
    cqi_freelist = item;
    pthread_mutex_unlock(&cqi_freelist_lock);
}

/*
 * Creates a worker thread.
 */
static void THREAD_CreateWorker(void *(*func)(void *), void *arg)
{
    pthread_attr_t  attr;
    int             ret;

    pthread_attr_init(&attr);

    if ((ret = pthread_create(&((ST_LIBEVENT_THREAD*)arg)->threadId, &attr, func, arg)) != 0)
	{
        LOG_FUNC(Err, True, "Can't create thread\n");
        exit(1);
    }
}


/****************************** LIBEVENT THREADS *****************************/
/*
 * Set up a thread's information.
 */
static void THREAD_SetupThread(ST_LIBEVENT_THREAD *me) 
{
    me->base = event_init();
    if (! me->base) 
	{
        LOG_FUNC(Err, False, "Can't allocate event base\n");
        exit(1);
    }

    /* Listen for notifications from other threads */
    event_set(&me->evNotifyEvent, me->s32NotifyReceiveFd,
              EV_READ | EV_PERSIST, THREAD_LibeventProcess, me);
    event_base_set(me->base, &me->evNotifyEvent);

    if (event_add(&me->evNotifyEvent, 0) == -1) 
	{
        LOG_FUNC(Err, False, "Can't monitor libevent notify pipe\n");
        exit(1);
    }

    me->new_conn_queue = malloc(sizeof(struct conn_queue));
    if (me->new_conn_queue == NULL) 
	{
        LOG_FUNC(Err, False, "Failed to allocate memory for connection queue");
        exit(1);
    }
    cq_init(me->new_conn_queue);
}

/*
 * Worker thread: main event loop
 */
static void *THREAD_WorkerLibevent(void *arg) 
{
    ST_LIBEVENT_THREAD *me = arg;

    /* Any per-thread setup can happen here; memcached_thread_init() will block until
     * all threads have finished initializing.
     */

    THREAD_RegisterInitialized();

    event_base_loop(me->base, 0);
    return NULL;
}

/*
 * Processes an incoming "handle a new connection" item. This is called when
 * input arrives on the libevent wakeup pipe.
 */
static void THREAD_LibeventProcess(sint32 fd, sint16 which, void *arg) 
{
    ST_LIBEVENT_THREAD *me = arg;
    CQ_ITEM *item;
    char buf[1];

    if (read(fd, buf, 1) != 1)
        if (gstSettings.bVerbose)
            LOG_FUNC(Err, False, "Can't read from libevent pipe\n");

    switch (buf[0]) 
	{
    	case 'c':
		    item = cq_pop(me->new_conn_queue);
		    if (NULL != item) 
			{
		        ST_CONN_INFO *c = CONN_NodeNew(item->sfd, item->init_state, item->event_flags,
		                           item->enConnType, item->read_buffer_size, me->base);
		        if (c == NULL) 
				{  
					if (gstSettings.bVerbose) 
					{
						LOG_FUNC(Err, False, "Can't listen for events on fd %d\n",
						item->sfd);
					}
					close(item->sfd);
		        } 
				else 
				{
		            c->pstThread = me;
		        }
		        cqi_free(item);
		    }
			LOG_FUNC(Debug, False, "Thread %lu recv a connet\n", me->threadId);
			break;
    	/* we were told to pause and report in */
    	case 'p':
    		THREAD_RegisterInitialized();
        break;
    }
}

/*
 * Initializes the thread subsystem, creating various worker threads.
 *
 * nthreads  Number of worker event handler threads to spawn
 * gebMainBase Event base for main thread
 */
void THERAD_RelaysrvInit(sint32 nthreads, struct event_base *main_base) 
{
    int         i;
    //int         power;

    pthread_mutex_init(&worker_hang_lock, NULL);

    pthread_mutex_init(&init_lock, NULL);
    pthread_cond_init(&init_cond, NULL);

    pthread_mutex_init(&cqi_freelist_lock, NULL);
    cqi_freelist = NULL;

    pstThreads = calloc(nthreads, sizeof(ST_LIBEVENT_THREAD));
    if (! pstThreads) 
	{
        LOG_FUNC(Err, False, "Can't allocate thread descriptors");
        exit(1);
    }

    //dispatcher_thread.base = gebMainBase;
    //dispatcher_thread.threadId = pthread_self();

    for (i = 0; i < nthreads; i++) 
	{
        int fds[2];
        if (pipe(fds)) 
		{
            LOG_FUNC(Err, False, "Thread %d: Can't create notify pipe", i);
            exit(1);
        }

        pstThreads[i].s32NotifyReceiveFd = fds[0];
        pstThreads[i].s32NotifySendFd = fds[1];

        THREAD_SetupThread(&pstThreads[i]);
		
        /* Reserve three fds for the libevent base, and two for the pipe */
        gstStats.u32ReservedFds += 5;
    }

	/* Create threads after we've done all the libevent setup. */
    for (i = 0; i < nthreads; i++) 
	{
        THREAD_CreateWorker(THREAD_WorkerLibevent, &pstThreads[i]);
    }

    /* Wait for all the threads to set themselves up before returning. */
    pthread_mutex_lock(&init_lock);
    THREAD_WaitForRegistration(nthreads);
    pthread_mutex_unlock(&init_lock);
}

/*
 * Dispatches a new connection to another thread. This is only ever called
 * from the main thread, either during initialization (for UDP) or because
 * of an incoming connection.
 */
void THREAD_DispatchConnNew(sint32 sfd, EN_CONN_TYPE enConnType, EN_CONN_STAT init_state, 
		sint32 event_flags, sint32 read_buffer_size) 
{
    CQ_ITEM *item = cqi_new();
    char buf[1];
    if (item == NULL) 
	{
        close(sfd);
        /* given that malloc failed this may also fail, but let's try */
        LOG_FUNC(Err, False, "Failed to allocate memory for connection object\n");
        return ;
    }

    sint32 tid = (last_thread + 1) % gstSettings.s32ThreadNum;

    ST_LIBEVENT_THREAD *thread = pstThreads + tid;

    last_thread = tid;

    item->sfd = sfd;
    item->init_state = init_state;
	item->enConnType = enConnType;
    item->event_flags = event_flags;
    item->read_buffer_size = read_buffer_size;

    cq_push(thread->new_conn_queue, item);
	LOG_FUNC(Debug, False, "push to thread %lu\n", thread->threadId);
    buf[0] = 'c';
    if (write(thread->s32NotifySendFd, buf, 1) != 1) 
	{
        LOG_FUNC(Err, False, "Writing to thread notify pipe");
    }
}

