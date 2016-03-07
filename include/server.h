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

#define DATA_BUFFER_SIZE 2048

/** Initial size of the sendmsg() scatter/gather array. */
#define IOV_LIST_INITIAL 400

/** Initial number of sendmsg() argument structures to allocate. */
#define MSG_LIST_INITIAL 10

/** High water marks for buffer shrinking */
#define READ_BUFFER_HIGHWAT 8192
#define IOV_LIST_HIGHWAT 600
#define MSG_LIST_HIGHWAT 100

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

typedef enum try_read_result 
{
    READ_DATA_RECEIVED,
    READ_NO_DATA_RECEIVED,
    READ_ERROR,            /** an error occurred (on the socket) (or client closed connection) */
    READ_MEMORY_ERROR      /** failed to allocate more memory */
}EN_TRYREAD_RET;


typedef struct libeventThread
{
    pthread_t threadId;        /* unique ID of this thread */
    struct event_base *base;    /* libevent handle this thread uses */
    struct event evNotifyEvent;  /* listen event for notify pipe */
    sint32 s32NotifyReceiveFd;      /* receiving end of notify pipe */
    sint32 s32NotifySendFd;         /* sending end of notify pipe */
    struct conn_queue *new_conn_queue; /* queue of new connections to handle */
  
} ST_LIBEVENT_THREAD;

/**
 * The structure representing a connection into replay server.
 */
typedef struct connectInformation
{
	sint32	s32Sockfd;
	struct event evConnEvent;
	sint16  s16Evflags;
	
	rel_time_t	lastCmdTime;

	EN_CONN_STAT	enConnStat;
	 /** which state to go into after finishing current write */
    EN_CONN_STAT  write_and_go;

	/*read buffer struct*/
	sint8   *rbuf;   /** buffer to read commands into */
    sint8   *rcurr;  /** but if we parsed some already, this is where we stopped */
    sint32    rsize;   /** total allocated size of rbuf */
    sint32    rbytes;  /** how much data, starting from rcur, do we have unparsed */

	/*write buffer struct*/
    sint8   *wbuf;
    sint8   *wcurr;
    sint32    wsize;
    sint32    wbytes;

	/* data for the mwrite state */
    struct iovec *iov;
    sint32    iovsize;   /* number of elements allocated in iov[] */
    sint32    iovused;   /* number of elements used in iov[] */

    struct msghdr *msglist;
    sint32    msgsize;   /* number of elements allocated in msglist[] */
    sint32    msgused;   /* number of elements used in msglist[] */
    sint32    msgcurr;   /* element in msglist[] being transmitted now */
    sint32    msgbytes;  /* number of bytes in current msg */

	struct sockaddr_in requestAddr;
    socklen_t requestAddrSize;

	sint16 	  cmd; /* current command being processed */

	struct connectInformation		*pstConnNext;
	ST_LIBEVENT_THREAD	*pstThread;
}ST_CONN_INFO;

typedef struct relaySettings
{
	sint32 		s32MaxConns;	/*Maximum connect number*/
	bool		bVerbose;		/*display detail debug information*/
	sint32		s32ThreadNum; 	/* number of worker (without dispatcher) libevent threads to run */
	sint32		s32Backlog;	
	sint32 		s32ReqsPerEvent;     /* Maximum number of io to process on each io-event. */
}ST_RELAY_SETTINGS;

typedef struct relayStatus
{
	uint32		u32ReservedFds;
	uint32  	u32CurrConns;
    uint32  	u32TotalConns;
	
}ST_RELAY_STATUS;


extern ST_RELAY_SETTINGS	gstSettings;

