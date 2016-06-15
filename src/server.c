/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *  	replay server 
 *
 *   	http://www.ecsino.com/
 *
 *  	Copyright 2016 Danga Interactive, Inc.  All rights reserved.
 *		File Name 	: 	server.c
 *  		Authors		:	Vict Ding <dszhazha@163.com>
 * 		Date   		: 	2016/2/29     
 *      
 */
//#include <sys/stat.h>
#include <sys/socket.h>
//#include <sys/un.h>
#include <signal.h>
//#include <sys/param.h>
#include <sys/resource.h>
//#include <sys/uio.h>
#include <ctype.h>
//#include <stdarg.h>

#include <pwd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
//#include <time.h>
#include <assert.h>
#include <limits.h>
//#include <sysexits.h>
//#include <stddef.h>

#include "server.h"

/*global value declaration*/
ST_RELAY_SETTINGS 	gstSettings;
ST_RELAY_STATUS		gstStats;


ST_CONN_INFO **ppstConnList;
static ST_CONN_INFO *gpstListenConnList = NULL;
static struct event_base *gebMainBase;

static sint32 gs32MaxFds;

/*
 * We keep the current time of day in a global variable that's updated by a
 * timer event. This saves us a bunch of time() system calls (we really only
 * need to get the time once a second, whereas there can be tens of thousands
 * of requests a second) and allows us to use server-start-relative timestamps
 * rather than absolute UNIX timestamps, a space savings on systems where
 * sizeof(time_t) > sizeof(unsigned int).
 */
volatile rel_time_t grtCurrentTime;
static struct event gevClockevent;
rel_time_t grtProcessStarted;     /* when the process was started */


/* This reduces the latency without adding lots of extra wiring to be able to
 * notify the listener thread of when to listen again.
 * Also, the clock timer could be broken out into its own thread and we
 * can block the listener via a condition.
 */
static volatile bool gbAllowNewConns = true;

void EVENT_ConnHandler(const sint32 fd, const sint16 which, void *arg); 

extern void THERAD_RelaysrvInit(sint32 nthreads, struct event_base *main_base); 
extern void THREAD_DispatchConnNew(sint32 sfd, EN_CONN_TYPE enConnType, EN_CONN_STAT init_state, 
		sint32 event_flags, sint32 read_buffer_size);
extern void STATS_LOCK(void);
extern void STATS_UNLOCK(void); 

extern void RingBuffer_Init(ST_RING_BUF *ab);
extern void RingBuffer_Write(ST_RING_BUF *ab, uint32 u32Len, sint8 *strData, time_t wTime, sint32 streamType, uint32 pts);
extern sint32 RTSP_SessionProcess(ST_CONN_INFO *c);
extern sint32 RTSP_SessionCreate(ST_CONN_INFO *c);
void EVENT_SendReqCommand(ST_CONN_INFO *c, EN_CMD_TYPE enCmdType, uint16 u32Serial);

static void SETTINGS_Init(void)
{
	gstSettings.bVerbose = False;
	gstSettings.s32Backlog = 1024;
	gstSettings.s32MaxConns = 1024;
	gstSettings.s32ReqsPerEvent = 4;
	gstSettings.s32ThreadNum = 4;

	grtProcessStarted = time(0) - UPDATE_INTERVAL - 2;
}

static void STATS_Init(void)
{
	gstStats.u32CurrConns = gstStats.u32TotalConns = 0;
	gstStats.u32ReservedFds = 0;
}

static sint32 CONN_NewSocket(struct addrinfo *ai) 
{
    sint32 s32Fd;
    sint32 s32Flags;

    if ((s32Fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol)) == -1) 
	{
        return FAIL;
    }

    if ((s32Flags = fcntl(s32Fd, F_GETFL, 0)) < 0 ||
        fcntl(s32Fd, F_SETFL, s32Flags | O_NONBLOCK) < 0)
    {
		LOG_FUNC(Err, False, "setting O_NONBLOCK");
        close(s32Fd);
        return FAIL;
    }
    return s32Fd;
}


/*
 * Initializes the connections array. We don't actually allocate connection
 * structures until they're needed, so as to avoid wasting memory when the
 * maximum connection count is much higher than the actual number of
 * connections.
 *
 * This does end up wasting a few pointers' worth of memory for FDs that are
 * used for things other than connections, but that's worth it in exchange for
 * being able to directly index the conns array by FD.
 */
static void CONN_ListInit(void)
{
	/* We're unlikely to see an FD much higher than maxconns. */
    sint32 s32NextFd = dup(1);
    sint32 s32Headroom = 10;      /* account for extra unexpected open FDs */

	struct rlimit rl;

    gs32MaxFds = gstSettings.s32MaxConns + s32Headroom + s32NextFd;

    /* But if possible, get the actual highest FD we can possibly ever see. */
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0) 
	{
        gs32MaxFds = rl.rlim_max;
    } 
	else 
	{
        LOG_FUNC(Err, False, "Failed to query maximum file descriptor; "
                        "falling back to maxconns\n");
    }

    close(s32NextFd);

    if ((ppstConnList = calloc(gs32MaxFds, sizeof(ST_CONN_INFO *))) == NULL) 
	{
		LOG_FUNC(Err, True, "Failed to allocate connection structures\n");
        /* This is unrecoverable so bail out early. */
        exit(1);
    } 

	LOG_FUNC(Debug, False, "Max fds is %d\n", gs32MaxFds);
}

/**
 * Convert a state name to a human readable form.
 */
static const sint8 *CONN_StateText(EN_CONN_STAT enState) 
{
    const sint8* const statenames[] = { "enConnListening",
                                       "enConnNewCmd",
                                       "enConnWaiting",
                                       "enConnRead",
                                       "enConnParseCmd",
                                       "enConnWrite",
                                       //"conn_nread",
                                       //"conn_swallow",
                                       "enConnClosing",
                                       //"conn_mwrite",
                                       "enConnClosed" };
    return statenames[enState];
}

/*
 * Sets a connection's current state in the state machine. Any special
 * processing that needs to happen on certain state transitions can
 * happen here.
 */
void CONN_SetState(ST_CONN_INFO *pstConnInfo, EN_CONN_STAT enState) 
{
    assert(pstConnInfo != NULL);
    assert(enState >= enConnListening && enState < enConnMaxState);

    if (enState != pstConnInfo->enConnStat) 
	{
        if (gstSettings.bVerbose) 
		{
            LOG_FUNC(Debug, False, "%d: going from %s to %s\n",
                    pstConnInfo->s32Sockfd, CONN_StateText(pstConnInfo->enConnStat),
                    CONN_StateText(enState));
        }
		
        pstConnInfo->enConnStat = enState;
    }
}

void CONN_SocketClose(ST_CONN_INFO *c) 
{
    assert(c != NULL);

    /* delete the event, the socket and the conn */
    event_del(&c->evConnEvent);

    LOG_FUNC(Err, False, "<%d connection closed.\n", c->s32Sockfd);

    CONN_SetState(c, enConnClosed);
    close(c->s32Sockfd);

	if(c->pstRtspSess != NULL)
	{
		free(c->pstRtspSess);
		c->pstRtspSess = NULL;
	}
		
	if(c->pstDevInfo != NULL)
	{
		free(c->pstDevInfo);
		c->pstDevInfo = NULL;
	}
		
	if(c->pstRingBuf != NULL)
	{
		free(c->pstRingBuf);
		c->pstRingBuf = NULL;
	}

	STATS_LOCK();
    gstStats.u32CurrConns--;
    STATS_UNLOCK();
	
    return;
}

/*
 * Frees a connection.
 */
void CONN_NodeFree(ST_CONN_INFO *c) 
{
    if (c) 
	{
        assert(c != NULL);
        assert(c->s32Sockfd >= 0 && c->s32Sockfd < gs32MaxFds);

        ppstConnList[c->s32Sockfd] = NULL;
        if (c->msglist)
            free(c->msglist);
        if (c->rbuf)
            free(c->rbuf);
        if (c->wbuf)
            free(c->wbuf);
        if (c->iov)
            free(c->iov);
		if(c->pstRtspSess != NULL)
			free(c->pstRtspSess);
		if(c->pstDevInfo != NULL)
			free(c->pstDevInfo);
		if(c->pstRingBuf != NULL)
			free(c->pstRingBuf);
        free(c);
    }
}
ST_CONN_INFO *CONN_NodeNew(const sint32 sfd, EN_CONN_STAT init_state,
                const sint32 event_flags, EN_CONN_TYPE enConnType, 
                const sint32 read_buffer_size,
                struct event_base *base) 
{
    ST_CONN_INFO *c;

    assert(sfd >= 0 && sfd < gs32MaxFds);
    c = ppstConnList[sfd];
    if (NULL == c) 
	{
        if (!(c = (ST_CONN_INFO *)calloc(1, sizeof(ST_CONN_INFO)))) 
		{
            LOG_FUNC(Err, True, "Failed to allocate connection object\n");
            return NULL;
        }

        c->rbuf = c->wbuf = 0;
        c->iov = 0;
        c->msglist = 0;

        c->rsize = read_buffer_size;
        c->wsize = DATA_BUFFER_SIZE;
        c->iovsize = IOV_LIST_INITIAL;
        c->msgsize = MSG_LIST_INITIAL;

        c->rbuf = (char *)malloc((size_t)c->rsize);
        c->wbuf = (char *)malloc((size_t)c->wsize);
        c->iov = (struct iovec *)malloc(sizeof(struct iovec) * c->iovsize);
        c->msglist = (struct msghdr *)malloc(sizeof(struct msghdr) * c->msgsize);

        if (c->rbuf == 0 || c->wbuf == 0 || c->iov == 0 || c->msglist == 0) 
		{
            CONN_NodeFree(c);
            LOG_FUNC(Err, True, "Failed to allocate buffers for connection\n");
            return NULL;
        }

        c->s32Sockfd = sfd;
        ppstConnList[sfd] = c;
    }

    if (init_state == enConnNewCmd) 
	{
        if (getpeername(sfd, (struct sockaddr *) &c->requestAddr,
                        &c->requestAddrSize)) 
		{
			LOG_FUNC(Err, True, "getpeername error");
            memset(&c->requestAddr, 0, sizeof(c->requestAddr));
        }
		c->requestAddrSize = sizeof(c->requestAddr);
    }

    if (gstSettings.bVerbose) 
	{
        if (init_state == enConnListening) 
		{
            LOG_FUNC(Info, False, "<%d server listening\n", sfd);
        } 
    }

	c->enConnType = enConnType;
    c->enConnStat = init_state;
    c->rbytes = c->wbytes = 0;
    c->wcurr = c->wbuf;
    c->rcurr = c->rbuf;

    c->iovused = 0;
    c->msgcurr = 0;
    c->msgused = 0;

	c->pstRingBuf = NULL;
	//c->pstDevInfo = NULL;
	//c->pstRtspSess = NULL;
	
	if (enConnType == enConnDevice)
	{
		c->pstDevInfo = (ST_DEV_INFO *)calloc(1, sizeof(ST_DEV_INFO));
		if (c->pstDevInfo == NULL) 
		{
            LOG_FUNC(Err, True, "Failed to allocate device session\n");
            return NULL;
        }

		c->pstRingBuf = (ST_RING_BUF *)calloc(1, sizeof(ST_RING_BUF));
		if (c->pstRingBuf == NULL) 
		{
            LOG_FUNC(Err, True, "Failed to allocate ringbuf struct\n");
            exit(-1);
        }
		RingBuffer_Init(c->pstRingBuf);
	}		
	else if(enConnType == enConnPlayer)
	{
		if (SUCCESS != RTSP_SessionCreate(c))
		{
			return NULL;
		}
	}
			
    event_set(&c->evConnEvent, sfd, event_flags, EVENT_ConnHandler, (void *)c);
    event_base_set(base, &c->evConnEvent);
    c->s16Evflags = event_flags;

    if (event_add(&c->evConnEvent, 0) == -1) 
	{
        LOG_FUNC(Err, True, "event_add");
        return NULL;
    }

	STATS_LOCK();
    gstStats.u32CurrConns++;
    gstStats.u32TotalConns++;
    STATS_UNLOCK();
	
    return c;
}

/**
 * Create a socket and bind it to a specific port number
 * @param interface the interface to bind to
 * @param port the port number to bind to
 * @param transport the transport protocol (TCP / UDP)
 * @param portnumber_file A filepointer to write the port numbers to
 *        when they are successfully added to the list of ports we
 *        listen on.
 */
static int CONN_ServerSocket(const char *interface, sint32 port) 
{
    int sfd;
    struct linger ling = {0, 0};
    struct addrinfo *ai;
    struct addrinfo *next;
    struct addrinfo hints = { 
			.ai_flags = AI_PASSIVE,
            .ai_family = AF_UNSPEC,
            .ai_socktype = SOCK_STREAM
        };
    char port_buf[NI_MAXSERV];
    int error;
    int success = 0;
    int flags =1;

	EN_CONN_TYPE enConnType;

    if (port == -1) 
	{
        port = 0;
    }
    snprintf(port_buf, sizeof(port_buf), "%d", port);
    error = getaddrinfo(interface, port_buf, &hints, &ai);
    if (error != 0) 
	{
        if (error != EAI_SYSTEM)
        {
			LOG_FUNC(Err, False, "getaddrinfo(): %s\n", gai_strerror(error));
        }
        else
        {
			LOG_FUNC(Err, False, "getaddrinfo()");
        }
        return 1;
    }

    for (next= ai; next; next= next->ai_next) 
	{
        ST_CONN_INFO *pstListenConnAdd;
        if ((sfd = CONN_NewSocket(next)) == -1) 
		{
            /* getaddrinfo can return "junk" addresses,
	             * we make sure at least one works before erroring.
	             */
            if (errno == EMFILE)
			{
                /* ...unless we're out of fds */
                LOG_FUNC(Err, False, "CONN_ServerSocket");
                exit(-1);
            }
            continue;
        }

        setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, (void *)&flags, sizeof(flags));

		//tcp socket
		{
            error = setsockopt(sfd, SOL_SOCKET, SO_KEEPALIVE, (void *)&flags, sizeof(flags));
            if (error != 0)
                LOG_FUNC(Err, False, "setsockopt");

            error = setsockopt(sfd, SOL_SOCKET, SO_LINGER, (void *)&ling, sizeof(ling));
            if (error != 0)
                LOG_FUNC(Err, False, "setsockopt");

            error = setsockopt(sfd, IPPROTO_TCP, TCP_NODELAY, (void *)&flags, sizeof(flags));
            if (error != 0)
                LOG_FUNC(Err, False, "setsockopt");
        }

        if (bind(sfd, next->ai_addr, next->ai_addrlen) == -1) 
		{
            if (errno != EADDRINUSE) 
			{
                LOG_FUNC(Err, False, "bind()");
                close(sfd);
                freeaddrinfo(ai);
                return 1;
            }
            close(sfd);
            continue;
        } 
		else 
		{
            success++;
            if (listen(sfd, gstSettings.s32Backlog) == -1) 
			{
                LOG_FUNC(Err, False, "listen()");
                close(sfd);
                freeaddrinfo(ai);
                return 1;
            }
        }

		if(port != 554)
		{
            enConnType = enConnDevListen;
        }
		else
		{
			enConnType = enConnPlayerListen;
		}

		if (!(pstListenConnAdd = CONN_NodeNew(sfd, enConnListening,
										EV_READ | EV_PERSIST, enConnType, 1,
										gebMainBase))) 
		{
			LOG_FUNC(Err, False, "failed to create listening connection\n");
			exit(1);
		}
		pstListenConnAdd->pstConnNext = gpstListenConnList;
		gpstListenConnList = pstListenConnAdd;
    }

    freeaddrinfo(ai);

    /* Return zero iff we detected no errors in starting up connections */
    return success == 0;
}

/*player connect map to device connect*/
ST_CONN_INFO *CONN_CheckMapConnect(sint8 *name, sint8 *passwd)
{
	sint32 loop;
	for(loop = 0; loop < gs32MaxFds; loop++)
	{
		if(ppstConnList[loop] != NULL && ppstConnList[loop]->enConnType == enConnDevice)
		{
			if(strlen(name) == strlen(ppstConnList[loop]->pstDevInfo->devName) && 
				ppstConnList[loop]->pstDevInfo->enDevStat >= enDevHeartBeat && 
				strncmp(name, ppstConnList[loop]->pstDevInfo->devName, strlen(name)) == 0)
			{
				return ppstConnList[loop];
			}
		}
	}

	return NULL;
}

sint32 CONN_CheckStreamStart(ST_CONN_INFO *c)
{
	if(c->enConnType != enConnDevice)
		return FAIL;

	if(c->pstDevInfo->enDevStat < enDevHeartBeat)
		return FAIL;
	
	return SUCCESS;
}

sint32 CONN_CheckStreamStop(ST_CONN_INFO *c)
{
	if(c->enConnType != enConnDevice)
		return FAIL;

	return SUCCESS;
}

/*
 * Shrinks a connection's buffers if they're too big.  This prevents
 * periodic large "get" requests from permanently chewing lots of server
 * memory.
 *
 * This should only be called in between requests since it can wipe output
 * buffers!
 */
static void RBUFFER_Shrink(ST_CONN_INFO *c) 
{
    assert(c != NULL);

    if (c->rsize > READ_BUFFER_HIGHWAT && c->rbytes < DATA_BUFFER_SIZE)
	{
        char *newbuf;

        if (c->rcurr != c->rbuf)
            memmove(c->rbuf, c->rcurr, (size_t)c->rbytes);

        newbuf = (char *)realloc((void *)c->rbuf, DATA_BUFFER_SIZE);

        if (newbuf) 
		{
            c->rbuf = newbuf;
            c->rsize = DATA_BUFFER_SIZE;
        }
        /* TODO check other branch... */
        c->rcurr = c->rbuf;
    }

    /* TODO check error condition? */
    if (c->msgsize > MSG_LIST_HIGHWAT) 
	{
        struct msghdr *newbuf = (struct msghdr *) realloc((void *)c->msglist, MSG_LIST_INITIAL * sizeof(c->msglist[0]));
        if (newbuf) 
		{
            c->msglist = newbuf;
            c->msgsize = MSG_LIST_INITIAL;
        }
    /* TODO check error condition? */
    }

    if (c->iovsize > IOV_LIST_HIGHWAT)
	{
        struct iovec *newbuf = (struct iovec *) realloc((void *)c->iov, IOV_LIST_INITIAL * sizeof(c->iov[0]));
        if (newbuf) 
		{
            c->iov = newbuf;
            c->iovsize = IOV_LIST_INITIAL;
        }
    /* TODO check return value */
    }
}

/*
 * read from network as much as we can, handle buffer overflow and connection
 * close.
 * before reading, move the remaining incomplete fragment of a command
 * (if any) to the beginning of the buffer.
 *
 * To protect us from someone flooding a connection with bogus data causing
 * the connection to eat up all available memory, break out and start looking
 * at the data I've got after a number of reallocs...
 *
 * @return enum try_read_result
 */
static EN_TRYREAD_RET RBUFFER_TryReadNetwork(ST_CONN_INFO *c) 
{
    EN_TRYREAD_RET gotdata = READ_NO_DATA_RECEIVED;
    int res;
    int num_allocs = 0;
    assert(c != NULL);

    if (c->rcurr != c->rbuf) 
	{
        if (c->rbytes != 0) /* otherwise there's nothing to copy */
            memmove(c->rbuf, c->rcurr, c->rbytes);
        c->rcurr = c->rbuf;
    }

    while (1) 
	{
        if (c->rbytes >= c->rsize) 
		{
            if (num_allocs == 4) 
			{
                return gotdata;
            }
            ++num_allocs;
            char *new_rbuf = realloc(c->rbuf, c->rsize * 2);
            if (!new_rbuf) 
			{ 
                if (gstSettings.bVerbose > 0) 
				{
                    LOG_FUNC(Err, False, "Couldn't realloc input buffer\n");
                }
                c->rbytes = 0; /* ignore what we read */
                //out_of_memory(c, "SERVER_ERROR out of memory reading request");
                c->write_and_go = enConnClosing;
                return READ_MEMORY_ERROR;
            }
            c->rcurr = c->rbuf = new_rbuf;
            c->rsize *= 2;
        }

        int avail = c->rsize - c->rbytes;
        res = read(c->s32Sockfd, c->rbuf + c->rbytes, avail);
        if (res > 0) 
		{
            gotdata = READ_DATA_RECEIVED;
            c->rbytes += res;
            if (res == avail) 
			{
                continue;
            } 
			else 
           	{
                break;
            }
        }
        if (res == 0) 
		{
            return READ_ERROR;
        }
        if (res == -1) 
		{
            if (errno == EAGAIN || errno == EWOULDBLOCK)
			{
                break;
            }
            return READ_ERROR;
        }
    }
    return gotdata;
}

/*
 * Adds a message header to a connection.
 *
 * Returns 0 on success, -1 on out-of-memory.
 */
int WBUFFER_AddMsghdr(ST_CONN_INFO *c)
{
    struct msghdr *msg;

    assert(c != NULL);

    if (c->msgsize == c->msgused) 
	{
        msg = realloc(c->msglist, c->msgsize * 2 * sizeof(struct msghdr));
        if (! msg) 
		{
          	LOG_FUNC(Err, True, "realloc error");
            return -1;
        }
        c->msglist = msg;
        c->msgsize *= 2;
    }

    msg = c->msglist + c->msgused;

    /* this wipes msg_iovlen, msg_control, msg_controllen, and
       msg_flags, the last 3 of which aren't defined on solaris: */
    memset(msg, 0x0, sizeof(struct msghdr));

    msg->msg_iov = &c->iov[c->iovused];

    c->msgbytes = 0;
    c->msgused++;

    return 0;
}

/*
 * Ensures that there is room for another struct iovec in a connection's
 * iov list.
 *
 * Returns 0 on success, -1 on out-of-memory.
 */
static int WBUFFER_EnsureIovSpace(ST_CONN_INFO *c) 
{
    assert(c != NULL);

    if (c->iovused >= c->iovsize) 
	{
        int i, iovnum;
        struct iovec *new_iov = (struct iovec *)realloc(c->iov,
                                (c->iovsize * 2) * sizeof(struct iovec));
        if (! new_iov) 
		{
            LOG_FUNC(Err, True, "realloc error");
            return -1;
        }
        c->iov = new_iov;
        c->iovsize *= 2;

        /* Point all the msghdr structures at the new list. */
        for (i = 0, iovnum = 0; i < c->msgused; i++) 
		{
            c->msglist[i].msg_iov = &c->iov[iovnum];
            iovnum += c->msglist[i].msg_iovlen;
        }
    }

    return 0;
}

/*
 * Adds data to the list of pending data that will be written out to a
 * connection.
 *
 * Returns 0 on success, -1 on out-of-memory.
 */
static int WBUFFER_AddIov(ST_CONN_INFO *c, const void *buf, sint32 len) 
{
    struct msghdr *m;
    int leftover;

    assert(c != NULL);

    do {
        m = &c->msglist[c->msgused-1];

        /* We may need to start a new msghdr if this one is full. */
        if (m->msg_iovlen == IOV_MAX || (c->msgbytes >= MAX_PAYLOAD_SIZE)) 
		{
            WBUFFER_AddMsghdr(c);
            m = &c->msglist[c->msgused-1];
        }

        if (WBUFFER_EnsureIovSpace(c) != 0)
            return -1;

        /* If the fragment is too big to fit in the datagram, split it up */
        if (len + c->msgbytes > MAX_PAYLOAD_SIZE) 
		{
            leftover = len + c->msgbytes - MAX_PAYLOAD_SIZE;
            len -= leftover;
        } 
		else 
		{
            leftover = 0;
        }

        m = &c->msglist[c->msgused-1];
        m->msg_iov[m->msg_iovlen].iov_base = (void *)buf;
        m->msg_iov[m->msg_iovlen].iov_len = len;
        c->msgbytes += len;
        c->iovused++;
        m->msg_iovlen++;
		
        buf = ((char *)buf) + len;
        len = leftover;
    } while (leftover > 0);

    return 0;
}

/**
 * Do basic sanity check of the runtime environment
 * @return true if no errors found, false if we can't use this env
 */
static bool EVENT_Sanitycheck(void) 
{
    /* One of our biggest problems is old and bogus libevents */
    const char *ever = event_get_version();
    if (ever != NULL) 
	{
        if (strncmp(ever, "1.", 2) == 0) 
		{
            /* Require at least 1.3 (that's still a couple of years old) */
            if (('0' <= ever[2] && ever[2] < '3') && !isdigit(ever[3])) 
			{
                LOG_FUNC(Err, False, "You are using libevent %s.\nPlease upgrade to"
                        " a more recent version (1.3 or newer)\n", event_get_version());
                return false;
            }
        }
    }

	LOG_FUNC(Info, False, "current libevent version is %s \n", event_get_version());
    return true;
}


/* libevent uses a monotonic clock when available for event scheduling. Aside
 * from jitter, simply ticking our internal timer here is accurate enough.
 * Note that users who are setting explicit dates for expiration times *must*
 * ensure their clocks are correct before starting relay. */
static void EVENT_ClockHandler(const sint32 fd, const sint16 which, void *arg) 
{
    struct timeval t = {.tv_sec = 1, .tv_usec = 0};
    static bool initialized = false;
#if defined(HAVE_CLOCK_GETTIME) && defined(CLOCK_MONOTONIC)
    static bool monotonic = false;
    static time_t monotonic_start;
#endif
    if (initialized) 
	{
        /* only delete the event if it's actually there. */
        evtimer_del(&gevClockevent);
    } 
	else 
	{
        initialized = true;
        /* grtProcessStarted is initialized to time() - 2. We initialize to 1 so
         * flush_all won't underflow during tests. */
#if defined(HAVE_CLOCK_GETTIME) && defined(CLOCK_MONOTONIC)
        struct timespec ts;
        if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) 
		{
            monotonic = true;
            monotonic_start = ts.tv_sec - UPDATE_INTERVAL - 2;
        }
#endif
    }

	//LOG_FUNC(Debug, False, "grtCurrentTime=%d\n", grtCurrentTime);
    evtimer_set(&gevClockevent, EVENT_ClockHandler, 0);
    event_base_set(gebMainBase, &gevClockevent);
    evtimer_add(&gevClockevent, &t);

#if defined(HAVE_CLOCK_GETTIME) && defined(CLOCK_MONOTONIC)
    if (monotonic) 
	{
        struct timespec ts;
        if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1)
            return;
        grtCurrentTime = (rel_time_t) (ts.tv_sec - monotonic_start);
        return;
    }
#endif
    {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        grtCurrentTime = (rel_time_t) (tv.tv_sec - grtProcessStarted);
    }
}

static void EVENT_ResetCmdHandler(ST_CONN_INFO *c) 
{
    c->cmd = -1;
    
    RBUFFER_Shrink(c);
    if (c->rbytes > 0)
	{
        CONN_SetState(c, enConnParseCmd);
    } 
	else 
	{
        CONN_SetState(c, enConnWaiting);
    }
}

static bool EVENT_UpdateEvent(ST_CONN_INFO *c, const sint32 new_flags) 
{
    assert(c != NULL);

    struct event_base *base = c->evConnEvent.ev_base;
    if (c->s16Evflags == new_flags)
        return true;
    if (event_del(&c->evConnEvent) == -1) 
		return false;
    event_set(&c->evConnEvent, c->s32Sockfd, new_flags, EVENT_ConnHandler, (void *)c);
    event_base_set(base, &c->evConnEvent);
    c->s16Evflags = new_flags;
    if (event_add(&c->evConnEvent, 0) == -1) 
		return false;
    return true;
}

static sint32 EVENT_CheckDeviceName(ST_CONN_INFO *c, sint8 *name, uint16 len)
{
	if(len != strlen(c->pstDevInfo->devName))
	{
		LOG_FUNC(Info, False, "Device name %s not match, cur name %s\n", 
			name, c->pstDevInfo->devName);
		return FAIL;
	}
	
	if(0 != strncmp(name, c->pstDevInfo->devName ,len))
	{
		LOG_FUNC(Info, False, "Device name %s not match, cur name %s\n", 
			name, c->pstDevInfo->devName);
		return FAIL;
	}

	return SUCCESS;
}

void EVENT_SendMessage(ST_CONN_INFO *c, sint8 *str)
{
	uint16	len = 0;

	assert(c != NULL);
	len += strlen(str);
	
	memcpy(c->wbuf, str, len);
    memcpy(c->wbuf + len, "\r\n", 2);

	c->wbytes = len + 2;
	c->wcurr = c->wbuf;
	
	c->msgcurr = 0;
	c->msgused = 0;
	c->iovused = 0;
	WBUFFER_AddMsghdr(c);
	CONN_SetState(c, enConnWrite);
	c->write_and_go = enConnNewCmd;
	return;
}

static void EVENT_ProcessLogin(ST_CONN_INFO *c, ST_TAKEN *tokens, const size_t ntokens)
{
	assert(c != NULL);

	strncpy(c->pstDevInfo->devName, tokens[1].value, tokens[1].length);
	strncpy(c->pstDevInfo->devPasswd, tokens[2].value, tokens[2].length);
	
	LOG_FUNC(Debug, False, "get devname %s\n", c->pstDevInfo->devName);
	LOG_FUNC(Debug, False, "get devpasswd %s\n", c->pstDevInfo->devPasswd);
	c->pstDevInfo->enDevStat = enDevLogin;
	EVENT_SendMessage(c, "RESPONCE OK LOGIN");
	return;
}

static void EVENT_ProcessInfo(ST_CONN_INFO *c, ST_TAKEN *tokens, const size_t ntokens)
{
	sint32 s32Ret = 0;
	
	assert(c != NULL);
	if(c->pstDevInfo->enDevStat != enDevLogin)
	{
		LOG_FUNC(Info, False, "Login First\n");
		EVENT_SendMessage(c, "RESPONCE FAIL INFO");
		return;
	}

	s32Ret = EVENT_CheckDeviceName(c, tokens[1].value, tokens[1].length);
	if(s32Ret != SUCCESS)
	{
		EVENT_SendMessage(c, "RESPONCE FAIL INFO");
		return;
	}

	c->pstDevInfo->enVencType 		= atoi(tokens[2].value);
	c->pstDevInfo->u32VideoWidth	= atoi(tokens[3].value);
	c->pstDevInfo->u32VideoHeigth	= atoi(tokens[4].value);
	c->pstDevInfo->u32VideoBit		= atoi(tokens[5].value);
	c->pstDevInfo->u32VideoFps		= atoi(tokens[6].value);
	c->pstDevInfo->enVbrc			= atoi(tokens[7].value);
	c->pstDevInfo->enAencType		= atoi(tokens[8].value);
	strcpy(c->pstDevInfo->Base64, "Z00AH9oBQBbsBagQEBIAAAMA8AAAOEDAgADR9gAHYaXvfCQ=,aO48gA==");

	LOG_FUNC(Debug, False, "get enEncType %d\n", c->pstDevInfo->enVencType);
	//LOG_FUNC(Debug, False, "get enVresolution %d\n", c->pstDevInfo->enVresolution);
	LOG_FUNC(Debug, False, "get enVoWidth %d\n", c->pstDevInfo->u32VideoWidth);
	LOG_FUNC(Debug, False, "get enVoHeigth %d\n", c->pstDevInfo->u32VideoHeigth);
	LOG_FUNC(Debug, False, "get enVobit %d\n", c->pstDevInfo->u32VideoBit);
	LOG_FUNC(Debug, False, "get enVoFps %d\n", c->pstDevInfo->u32VideoFps);
	LOG_FUNC(Debug, False, "get enVoBrc %d\n", c->pstDevInfo->enVbrc);
	LOG_FUNC(Debug, False, "get enAoType %d\n", c->pstDevInfo->enAencType);
	
	c->pstDevInfo->enDevStat = enDevMessage;
	EVENT_SendMessage(c, "RESPONCE OK INFO");
	return;
}

static void EVENT_ProcessHeartbeat(ST_CONN_INFO *c, ST_TAKEN *tokens, const size_t ntokens)
{
	assert(c != NULL);
	if(c->pstDevInfo->enDevStat < enDevMessage)
	{
		LOG_FUNC(Info, False, "Login First\n");
		EVENT_SendMessage(c, "RESPONCE FAIL LINK");
		return;
	}

	sint32 	s32Ret = 0;
	s32Ret = EVENT_CheckDeviceName(c, tokens[1].value, tokens[1].length);
	if(s32Ret != SUCCESS)
	{
		EVENT_SendMessage(c, "RESPONCE FAIL LINK");
		return;
	}

	c->pstDevInfo->enDevStat = enDevHeartBeat;
	EVENT_SendMessage(c, "RESPONCE OK LINK");
	return;
	
}

static void EVENT_ProcessStreamStart(ST_CONN_INFO *c, ST_TAKEN *tokens, const size_t ntokens)
{
	
}

static void EVENT_ProcessStreamStop(ST_CONN_INFO *c, ST_TAKEN *tokens, const size_t ntokens)
{

}

static void EVENT_ProcessStreamData(ST_CONN_INFO *c, ST_TAKEN *tokens, const size_t ntokens)
{
	sint32 s32Ret = 0;
	
	assert(c != NULL);
	if(c->pstDevInfo->enDevStat < enDevHeartBeat)
	{
		LOG_FUNC(Info, False, "Status error\n");
		CONN_SetState(c, enConnClosing);
		return;
	}
	
	if(c->pstRingBuf == NULL)
	{
		LOG_FUNC(Info, False, "ringbuf not malloc space, ignore data\n");
		return;
	}
	
	c->stream_type = = atoi(tokens[1].value);
	c->stream_pts = atoi(tokens[2].value);
	c->stream_len = atoi(tokens[2].value);

	CONN_SetState(c, enConnNread);
	
}

/*
 * Tokenize the command string by replacing whitespace with '\0' and update
 * the token array tokens with pointer to start of each token and length.
 * Returns total number of tokens.  The last valid token is the terminal
 * token (value points to the first unprocessed character of the string and
 * length zero).
 *
 * Usage example:
 *
 *  while(tokenize_command(command, ncommand, tokens, max_tokens) > 0) {
 *      for(int ix = 0; tokens[ix].length != 0; ix++) {
 *          ...
 *      }
 *      ncommand = tokens[ix].value - command;
 *      command  = tokens[ix].value;
 *   }
 */
static size_t tokenize_command(char *command, ST_TAKEN *tokens, const size_t max_tokens) 
{
    char *s, *e;
    size_t ntokens = 0;
    size_t len = strlen(command);
    unsigned int i = 0;

    assert(command != NULL && tokens != NULL && max_tokens > 1);

    s = e = command;
    for (i = 0; i < len; i++) 
	{
        if (*e == ' ') 
		{
            if (s != e) 
			{
                tokens[ntokens].value = s;
                tokens[ntokens].length = e - s;
                ntokens++;
                *e = '\0';
                if (ntokens == max_tokens - 1)
				{
                    e++;
                    s = e; /* so we don't add an extra token */
                    break;
                }
            }
            s = e + 1;
        }
        e++;
    }

    if (s != e) 
	{
        tokens[ntokens].value = s;
        tokens[ntokens].length = e - s;
        ntokens++;
    }

    /*
     * If we scanned the whole string, the terminal value pointer is null,
     * otherwise it is the first unprocessed character.
     */
    tokens[ntokens].value = *e == '\0' ? NULL : e;
    tokens[ntokens].length = 0;
    ntokens++;

    return ntokens;
}

static void EVENT_ProcessCommand(ST_CONN_INFO *c, sint8 *command)
{
	ST_TAKEN tokens[MAX_TOKENS];
    uint32 ntokens;
	
	assert(c != NULL);
	
	if (c->enConnType == enConnDevice)
	{
		ntokens = tokenize_command(command, tokens, MAX_TOKENS);
		printf("ntokens = %d, cmd = %s\n",ntokens, tokens[COMMAND_TOKEN].value);
		
		if (ntokens == 3 && strcmp(tokens[COMMAND_TOKEN].value, "LINK") == 0)
		{
			EVENT_ProcessHeartbeat(c, tokens, ntokens);
		}
		else if (ntokens == 4 && strcmp(tokens[COMMAND_TOKEN].value, "LOGIN") == 0)
		{
			EVENT_ProcessLogin(c, tokens, ntokens);
		}
		else if (ntokens == 5 && strcmp(tokens[COMMAND_TOKEN].value, "STREAM") == 0)
		{
			EVENT_ProcessStreamData(c, tokens, ntokens);
		}
		else if (ntokens == 10 && strcmp(tokens[COMMAND_TOKEN].value, "INFO") == 0)
		{
			EVENT_ProcessInfo(c, tokens, ntokens);
		}
		else
		{
			printf("UNKNOW MESSAGE\n");
			EVENT_SendMessage(c, "UNKNOW");
		}
	}
	
	return;
}

/*
 * if we have a complete line in the buffer, process it.
 */
static sint32 EVENT_TryReadDevCommand(ST_CONN_INFO *c) 
{
    assert(c != NULL);
    assert(c->rcurr <= (c->rbuf + c->rsize));
    assert(c->rbytes > 0);

	sint8 *el, *cont;
	if (c->rbytes == 0)
		return SUCCESS;

	el = memchr(c->rcurr, '\n', c->rbytes);
	if (!el)
	{
		/*we need more data*/
		return FAIL;
	}
	cont = el + 1;
    if ((el - c->rcurr) > 1 && *(el - 1) == '\r') 
	{
		el--;
    }
	*el = '\0';
	
	assert(cont <= (c->rcurr + c->rbytes));
	printf("Msg:%s\n", c->rcurr);
	EVENT_ProcessCommand(c, c->rcurr);

	c->rbytes -= (cont - c->rcurr);
    c->rcurr = cont;

    assert(c->rcurr <= (c->rbuf + c->rsize));

    return SUCCESS;
}

static sint32 EVENT_TryReadPlayerCommand(ST_CONN_INFO *c)
{
	assert(c != NULL);
    assert(c->rcurr <= (c->rbuf + c->rsize));
    assert(c->rbytes > 0);

	return RTSP_SessionProcess(c);
}

/*
 * Transmit the next chunk of data from our list of msgbuf structures.
 *
 * Returns:
 *   TRANSMIT_COMPLETE   All done writing.
 *   TRANSMIT_INCOMPLETE More data remaining to write.
 *   TRANSMIT_SOFT_ERROR Can't write any more right now.
 *   TRANSMIT_HARD_ERROR Can't write (c->state is set to conn_closing)
 */
static EN_TRENSMIT_RES EVENT_Transmit(ST_CONN_INFO *c) 
{
    assert(c != NULL);

    if (c->msgcurr < c->msgused &&
            c->msglist[c->msgcurr].msg_iovlen == 0) 
	{
        /* Finished writing the current msg; advance to the next. */
        c->msgcurr++;
    }

    if (c->msgcurr < c->msgused)
	{
        ssize_t res;
        struct msghdr *m = &c->msglist[c->msgcurr];
        res = sendmsg(c->s32Sockfd, m, 0);
        if (res > 0) 
		{
            /* We've written some of the data. Remove the completed
               iovec entries from the list of pending writes. */
            while (m->msg_iovlen > 0 && res >= m->msg_iov->iov_len) 
			{
                res -= m->msg_iov->iov_len;
                m->msg_iovlen--;
                m->msg_iov++;
            }

            /* Might have written just part of the last iovec entry;
               adjust it so the next write will do the rest. */
            if (res > 0) 
			{
                m->msg_iov->iov_base = (caddr_t)m->msg_iov->iov_base + res;
                m->msg_iov->iov_len -= res;
            }
            return TRANSMIT_INCOMPLETE;
        }
        if (res == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
		{
            if (!EVENT_UpdateEvent(c, EV_WRITE | EV_PERSIST)) 
			{
                if (gstSettings.bVerbose > 0)
                    LOG_FUNC(Err, False, "Couldn't update event\n");
                CONN_SetState(c, enConnClosing);
                return TRANSMIT_HARD_ERROR;
            }
            return TRANSMIT_SOFT_ERROR;
        }
        /* if res == 0 or res == -1 and error is not EAGAIN or EWOULDBLOCK,
           we have a real error, on which we close the connection */
        
		LOG_FUNC(Err, False, "Failed to write, and not due to blocking");

		CONN_SetState(c, enConnClosing);
        return TRANSMIT_HARD_ERROR;
    }
	else 
	{
        return TRANSMIT_COMPLETE;
    }
}

static void EVENT_DriveMachine(ST_CONN_INFO *c)
{
	bool stop = false;
    sint32 		sfd;
    socklen_t 	addrlen;
    struct sockaddr_storage addr;
	sint32 	s32Ret;
    const sint8 *pstr;
    sint32 nreqs = gstSettings.s32ReqsPerEvent;
    //sint32 res;
    //const sint8 *str;

	assert(c != NULL);

	while (!stop) 
	{
		switch(c->enConnStat)
		{
			case enConnListening:
				addrlen = sizeof(addr);
				sfd = accept(c->s32Sockfd, (struct sockaddr *)&addr, &addrlen);
				if (sfd == -1)
				{
					if (errno == EAGAIN || errno == EWOULDBLOCK) 
					{
	                    /* these are transient, so don't log anything */
	                    stop = true;
	                } 
					else if (errno == EMFILE) 
					{
	                    if (gstSettings.bVerbose)
	                        LOG_FUNC(Debug, False, "Too many open connections\n"); 
	                    stop = true;
	                } 
					else 
					{
						LOG_FUNC(Err, True, "accept error");
	                    stop = true;
	                }
	                break;
				}	

				if (fcntl(sfd, F_SETFL, fcntl(sfd, F_GETFL) | O_NONBLOCK) < 0) 
				{
                    LOG_FUNC(Err, True, "setting O_NONBLOCK");
                    close(sfd);
                    break;
                }
				LOG_FUNC(Debug, False, "accept a connect\n"); 

				if(gstStats.u32CurrConns + gstStats.u32ReservedFds >= gstSettings.s32MaxConns)
				{
					pstr = "ERROR: Too many open connections\r\n";
					s32Ret = write(sfd, pstr, strlen(pstr));
					close(sfd);
				}
				else
				{
					EN_CONN_TYPE	enConnType;
					if(c->enConnType == enConnDevListen)
						enConnType = enConnDevice;
					else if(c->enConnType == enConnPlayerListen)
						enConnType = enConnPlayer;
					else
					{
						LOG_FUNC(Err, False, "connect type error\n");
						exit(-1);
					}
					THREAD_DispatchConnNew(sfd, enConnType, enConnNewCmd,
							EV_READ | EV_PERSIST, DATA_BUFFER_SIZE);
				}
				stop = true;
				break;
				
			case enConnNewCmd:
				/* Only process nreqs at a time to avoid starving other
               		connections */
               	--nreqs;
            	if (nreqs >= 0)
				{
                	EVENT_ResetCmdHandler(c);	
            	}
				else
				{
					if (c->rbytes > 0) 
					{
	                    /* We have already read in data into the input buffer,
			                       so libevent will most likely not signal read events
			                       on the socket (unless more data is available. As a
			                       hack we should just put in a request to write data,
			                       because that should be possible ;-)
			                    */
	                    if (!EVENT_UpdateEvent(c, EV_WRITE | EV_PERSIST))
						{
	                        if (gstSettings.bVerbose > 0)
	                            LOG_FUNC(Err, True, "Couldn't update event\n");
	                        CONN_SetState(c, enConnClosing);
	                        break;
	                    }
                	}
                	stop = true;
				}
				break;
			case enConnWaiting:
				if (!EVENT_UpdateEvent(c, EV_READ | EV_PERSIST))
				{
	                if (gstSettings.bVerbose > 0)
	                    LOG_FUNC(Err, True, "Couldn't update event\n");
	                CONN_SetState(c, enConnClosing);
	                break;
            	}

            	CONN_SetState(c, enConnRead);
            	stop = true;
           	 	break;
			case enConnRead:
				s32Ret = RBUFFER_TryReadNetwork(c);
				switch(s32Ret)
				{
					 case READ_NO_DATA_RECEIVED:
		                CONN_SetState(c, enConnWaiting);
		                break;
		            case READ_DATA_RECEIVED:
		                CONN_SetState(c, enConnParseCmd);
		                break;
		            case READ_ERROR:
		                CONN_SetState(c, enConnClosing);
		                break;
		            case READ_MEMORY_ERROR: /* Failed to allocate more memory */
		                /* State already set by try_read_network */
		                break;
            	}
           		break; 
			case enConnNread:
				if(c->pstRingBuf == NULL)
				{
					LOG_FUNC(Err, False, "ringbuf is not malloc\n");
					CONN_SetState(c, enConnClosing);
					break;
				}

				if(c->stream_len < 0)
				{
					LOG_FUNC(Err, False, "stream length error\n");
					CONN_SetState(c, enConnClosing);
					break;
				}

				sint8 *ptr = RingBuffer_FillbufPtr(c->pstRingBuf, c->stream_len);
				sint32 index = RingBuffer_FillbufIndex(c->pstRingBuf);
				uint32 leftlen = c->stream_len;
				RingBuffer_WriteLock(c->pstRingBuf, index);
				/* first check if we have leftovers in the conn_read buffer */
	            if (c->rbytes > 0)
				{
	                int tocopy = c->rbytes > leftlen ? leftlen : c->rbytes;
	                memmove(ptr, c->rcurr, tocopy);
	                ptr += tocopy;
	                leftlen -= tocopy;
	                c->rcurr += tocopy;
	                c->rbytes -= tocopy;
	                if (leftlen == 0) 
					{
						RingBuffer_WriteRecord(c->pstRingBuf, c->stream_len);
	                    break;
	                }
	            }	

				
				break;
			case enConnParseCmd:
				if(c->enConnType == enConnPlayer)
				{
					if(EVENT_TryReadPlayerCommand(c) == FAIL)
					{
						CONN_SetState(c, enConnWaiting);
					}
				}
				else
				{
					if(EVENT_TryReadDevCommand(c) == FAIL)
						CONN_SetState(c, enConnWaiting);
				}
				break;
			case enConnWrite:;
				/*
				* We want to write out a simple response. If we haven't already,
				* assemble it into a msgbuf list (this will be a single-entry
				* list for TCP or a two-entry list for UDP).
				*/
				if (c->iovused == 0) 
				{
					if (WBUFFER_AddIov(c, c->wcurr, c->wbytes) != 0) 
					{
						if (gstSettings.bVerbose > 0)
							LOG_FUNC(Err, False, "Couldn't build response\n");
						CONN_SetState(c, enConnClosing);
						break;
					}
				}
				switch(EVENT_Transmit(c))
				{
					case TRANSMIT_COMPLETE:
						CONN_SetState(c, enConnNewCmd);
						//CONN_SetState(c, c->write_and_go);
						break;
					case TRANSMIT_INCOMPLETE:
					case TRANSMIT_HARD_ERROR:
                		break;                   /* Continue in state machine. */	
					case TRANSMIT_SOFT_ERROR:
                		stop = true;
                		break;
				}
		
				break;
			case enConnClosing:
				LOG_FUNC(Debug, False, "current stat is %s\n", CONN_StateText(c->enConnStat));
				CONN_SocketClose(c);
            	stop = true;
            	break;
			case enConnClosed:
				LOG_FUNC(Debug, False, "current stat is %s\n", CONN_StateText(c->enConnStat));
				/* This only happens if dormando is an idiot. */
            	abort();
            	break;
			case enConnMaxState:
			default :
				assert(false);
				LOG_FUNC(Debug, False, "current stat is %s\n", CONN_StateText(c->enConnStat));
				break;
		}
	}
}

void EVENT_ConnHandler(const sint32 fd, const sint16 which, void *arg) 
{
    ST_CONN_INFO *c;

    c = (ST_CONN_INFO *)arg;
    assert(c != NULL);

    /* sanity */
    if (fd != c->s32Sockfd)
	{
        if (gstSettings.bVerbose)
            LOG_FUNC(Debug, False, "Catastrophic: event fd doesn't match conn fd!\n");
        CONN_SocketClose(c);
        return;
    }

    EVENT_DriveMachine(c);
	
    /* wait for next event */
    return;
}

int main(int argc, char *argv[])
{
	struct rlimit rlim;
	
	#ifdef OPEN_SYSLOG
	LOG_Init();
	#endif
	
	LOG_FUNC(Info, False, "replay server start!\n");

	if (!EVENT_Sanitycheck())
	{
		return FAIL;
	}

	SETTINGS_Init();
	STATS_Init();
	/*
     * If needed, increase rlimits to allow as many connections
     * as needed.
     */
    if (getrlimit(RLIMIT_NOFILE, &rlim) != 0) 
	{
        LOG_FUNC(Err, False, "failed to getrlimit number of files\n");
        exit(-1);
    } 
	else 
	{
        rlim.rlim_cur = gstSettings.s32MaxConns;
        rlim.rlim_max = gstSettings.s32MaxConns;
        if (setrlimit(RLIMIT_NOFILE, &rlim) != 0)
		{
            LOG_FUNC(Err, False, "failed to set rlimit for open files. Try starting as root or requesting smaller maxconns value.\n");
            exit(-1);
        }
    }

	/* initialize main thread libevent instance */
    gebMainBase = event_init();

	/* initialize*/
	CONN_ListInit();
	
	THERAD_RelaysrvInit(gstSettings.s32ThreadNum, gebMainBase);

	CONN_ServerSocket(NULL, 11211);

	CONN_ServerSocket(NULL, 554);

	EVENT_ClockHandler(0,0,0);
	/* enter the event loop */
    if (event_base_loop(gebMainBase, 0) != 0) {
        exit(-1);
    }
	
	return 0;
}
