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

typedef struct conn_queue_item CQ_ITEM;
struct conn_queue_item {
    int               	sfd;
    EN_CONN_STAT  		init_state;
    int               	event_flags;
    int               	read_buffer_size;
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

/*
 * Number of worker threads that have finished setting themselves up.
 */
static int init_count = 0;
static pthread_mutex_t init_lock;
static pthread_cond_t init_cond;

//static LIBEVENT_DISPATCHER_THREAD dispatcher_thread;

/*
 * Each libevent instance has a wakeup pipe, which other threads
 * can use to signal that they've put a new connection on its queue.
 */
static ST_LIBEVENT_THREAD *pstThreads;

/*
 * Initializes the thread subsystem, creating various worker threads.
 *
 * nthreads  Number of worker event handler threads to spawn
 * main_base Event base for main thread
 */
void relaysrv_thread_init(int nthreads, struct event_base *main_base) 
{
    int         i;
    int         power;

    pthread_mutex_init(&worker_hang_lock, NULL);

    pthread_mutex_init(&init_lock, NULL);
    pthread_cond_init(&init_cond, NULL);

    pthread_mutex_init(&cqi_freelist_lock, NULL);
    cqi_freelist = NULL;

    pstThreads = calloc(nthreads, sizeof(ST_LIBEVENT_THREAD));
    if (! pstThreads) 
	{
        perror("Can't allocate thread descriptors");
        exit(1);
    }

    //dispatcher_thread.base = main_base;
    //dispatcher_thread.thread_id = pthread_self();

    for (i = 0; i < nthreads; i++) 
	{
        int fds[2];
        if (pipe(fds)) 
		{
            perror("Can't create notify pipe");
            exit(1);
        }

        pstThreads[i].s32NotifyReceiveFd = fds[0];
        pstThreads[i].s32NotifySendFd = fds[1];

        //setup_thread(&pstThreads[i]);
        
    }
}


