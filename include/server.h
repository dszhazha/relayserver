/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *  	replay server 
 *
 *   	http://www.ecsino.com/
 *
 *  	Copyright 2016 Danga Interactive, Inc.  All rights reserved.
 *		File Name 	: 	server.h
 *  	Authors		:	Vict Ding <dszhazha@163.com>
 * 		Date   		: 	2016/2/29     
 *      
 */
#include "type.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <event.h>
#include <netdb.h>
#include <pthread.h>
#include <unistd.h>

#define UPDATE_INTERVAL 	0

/**
 * Possible states of a connection.
 */
typedef enum connectionStates 
{
    enConnListening,  	/**< the socket which listens for connections */
    enConnNewCmd,    	/**< Prepare connection for next command */
    enConnWaiting,    	/**< waiting for a readable socket */
    enConnRead,       	/**< reading in a command line */
    enConnParseCmd,  	/**< try to parse a command from the input buffer */
    enConnWrite,      	/**< writing out a simple response */
    //conn_nread,      	/**< reading in a fixed number of bytes */
    //conn_swallow,    	/**< swallowing unnecessary bytes w/o storing */
    enConnClosing,    	/**< closing this connection */
    //conn_mwrite,     	/**< writing out many items sequentially */
    enConnClosed,     	/**< connection is closed */
    enConnMaxState   	/**< Max state value (used for assertion) */
}EN_CONN_STAT;

typedef struct libeventThread
{
    pthread_t thread_id;        /* unique ID of this thread */
    struct event_base *base;    /* libevent handle this thread uses */
    struct event notify_event;  /* listen event for notify pipe */
    sint32 s32NotifyReceiveFd;      /* receiving end of notify pipe */
    sint32 s32NotifySendFd;         /* sending end of notify pipe */
    struct conn_queue *new_conn_queue; /* queue of new connections to handle */
  
} ST_LIBEVENT_THREAD;

/**
 * The structure representing a connection into replay server.
 */
typedef struct connectInformation ST_CONN_INFO;
struct connectInformation
{
	sint32	s32Sockfd;
	struct event event;

	rel_time_t	last_cmd_time;

	EN_CONN_STAT	enConnStat;

	/*read buffer struct*/
	sint8   *s8Rbuf;   /** buffer to read commands into */
    sint8   *s8Rcurr;  /** but if we parsed some already, this is where we stopped */
    sint32    s32Rsize;   /** total allocated size of rbuf */
    sint32    s32Rbytes;  /** how much data, starting from rcur, do we have unparsed */

	/*write buffer struct*/
    sint8   *s8Wbuf;
    sint8   *s8Wcurr;
    sint32    s32Wsize;
    sint32    s32Wbytes;

	/* data for the mwrite state */
    struct iovec *iov;
    int    iovsize;   /* number of elements allocated in iov[] */
    int    iovused;   /* number of elements used in iov[] */

    struct msghdr *msglist;
    int    msgsize;   /* number of elements allocated in msglist[] */
    int    msgused;   /* number of elements used in msglist[] */
    int    msgcurr;   /* element in msglist[] being transmitted now */
    int    msgbytes;  /* number of bytes in current msg */

	ST_CONN_INFO		*pstConnNext;
	ST_LIBEVENT_THREAD	*pstThread;
};

typedef struct replaySettings
{
	sint32 		s32MaxConns;	/*Maximum connect number*/
	bool		bVerbose;		/*display detail debug information*/
	sint32		s32ThreadNum; 	/* number of worker (without dispatcher) libevent threads to run */
	sint32		s32Backlog;	
	sint32 		s32ReqsPerEvent;     /* Maximum number of io to process on each io-event. */
}ST_REPLAY_SETTINGS;
