/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *  	replay server 
 *
 *   	http://www.ecsino.com/
 *
 *  	Copyright 2016 Danga Interactive, Inc.  All rights reserved.
 *		File Name 	: 	server.c
 *  	Authors		:	Vict Ding <dszhazha@163.com>
 * 		Date   		: 	2016/2/29     
 *      
 */
#include "server.h"
#include "log.h"

#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <sys/param.h>
#include <sys/resource.h>
#include <sys/uio.h>
#include <ctype.h>
#include <stdarg.h>

#include <pwd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <limits.h>
#include <sysexits.h>
#include <stddef.h>

/*global value declaration*/
ST_CONN_INFO 		**ppstConnList;
static ST_CONN_INFO *listen_conn = NULL;

ST_REPLAY_SETTINGS	gstSettings;
static struct event_base *main_base;
time_t process_started;     /* when the process was started */

/* This reduces the latency without adding lots of extra wiring to be able to
 * notify the listener thread of when to listen again.
 * Also, the clock timer could be broken out into its own thread and we
 * can block the listener via a condition.
 */
static volatile bool allow_new_conns = true;

static int 			gs32MaxFds;

void event_handler(const int fd, const short which, void *arg); 
static void drive_machine(ST_CONN_INFO *c);

extern void relaysrv_thread_init(int nthreads, struct event_base *main_base); 



/**
 * Do basic sanity check of the runtime environment
 * @return true if no errors found, false if we can't use this env
 */
static bool sanitycheck(void) 
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
                LOGFUNC(Err, False, "You are using libevent %s.\nPlease upgrade to"
                        " a more recent version (1.3 or newer)\n", event_get_version());
                return false;
            }
        }
    }

	LOGFUNC(Info, False, "current libevent version is %s \n", event_get_version());
    return true;
}

static void settings_init(void)
{
	gstSettings.verbose = False;
	gstSettings.s32Backlog = 1024;
	gstSettings.s32MaxConns = 1024;
	gstSettings.s32ReqsPerEvent = 4;
	gstSettings.s32ThreadNum = 4;

	process_started = time(0) - UPDATE_INTERVAL - 2;
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
static void conn_init(void)
{
	/* We're unlikely to see an FD much higher than maxconns. */
    sint32 s32NextFd = dup(1);
    sint32 headroom = 10;      /* account for extra unexpected open FDs */

	struct rlimit rl;

    gs32MaxFds = gstSettings.s32MaxConns + headroom + s32NextFd;

    /* But if possible, get the actual highest FD we can possibly ever see. */
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0) 
	{
        gs32MaxFds = rl.rlim_max;
    } 
	else 
	{
        LOGFUNC(Err, False, "Failed to query maximum file descriptor; "
                        "falling back to maxconns\n");
    }

    close(s32NextFd);

    if ((ppstConnList = calloc(gs32MaxFds, sizeof(ST_CONN_INFO *))) == NULL) 
	{
		LOGFUNC(Err, True, "Failed to allocate connection structures\n");
        /* This is unrecoverable so bail out early. */
        exit(1);
    } 

	LOGFUNC(Debug, False, "Max fds is %d\n", gs32MaxFds);
}

/*
 * We keep the current time of day in a global variable that's updated by a
 * timer event. This saves us a bunch of time() system calls (we really only
 * need to get the time once a second, whereas there can be tens of thousands
 * of requests a second) and allows us to use server-start-relative timestamps
 * rather than absolute UNIX timestamps, a space savings on systems where
 * sizeof(time_t) > sizeof(unsigned int).
 */
volatile rel_time_t current_time;
static struct event clockevent;

/* libevent uses a monotonic clock when available for event scheduling. Aside
 * from jitter, simply ticking our internal timer here is accurate enough.
 * Note that users who are setting explicit dates for expiration times *must*
 * ensure their clocks are correct before starting relay. */
static void clock_handler(const sint32 fd, const sint16 which, void *arg) 
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
        evtimer_del(&clockevent);
    } 
	else 
	{
        initialized = true;
        /* process_started is initialized to time() - 2. We initialize to 1 so
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

	//LOGFUNC(Debug, False, "current_time=%d\n", current_time);
    evtimer_set(&clockevent, clock_handler, 0);
    event_base_set(main_base, &clockevent);
    evtimer_add(&clockevent, &t);

#if defined(HAVE_CLOCK_GETTIME) && defined(CLOCK_MONOTONIC)
    if (monotonic) 
	{
        struct timespec ts;
        if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1)
            return;
        current_time = (rel_time_t) (ts.tv_sec - monotonic_start);
        return;
    }
#endif
    {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        current_time = (rel_time_t) (tv.tv_sec - process_started);
    }
}

/**
 * Convert a state name to a human readable form.
 */
static const char *state_text(EN_CONN_STAT state) 
{
    const char* const statenames[] = { "enConnListening",
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
    return statenames[state];
}

/*
 * Sets a connection's current state in the state machine. Any special
 * processing that needs to happen on certain state transitions can
 * happen here.
 */
static void conn_set_state(ST_CONN_INFO *c, EN_CONN_STAT state) 
{
    assert(c != NULL);
    assert(state >= enConnListening && state < enConnMaxState);

    if (state != c->enConnStat) 
	{
        if (gstSettings.verbose) 
		{
            LOGFUNC(Debug, False, "%d: going from %s to %s\n",
                    c->s32Sockfd, state_text(c->enConnStat),
                    state_text(state));
        }
		
        c->enConnStat = state;
    }
}

static void conn_close(ST_CONN_INFO *c) 
{
    assert(c != NULL);

    /* delete the event, the socket and the conn */
    event_del(&c->event);

    if (gstSettings.verbose)
        LOGFUNC(Err, False, "<%d connection closed.\n", c->s32Sockfd);

    conn_set_state(c, enConnClosed);
    close(c->s32Sockfd);
	
    return;
}


/*
 * Frees a connection.
 */
void conn_free(ST_CONN_INFO *c) 
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
        free(c);
    }
}
ST_CONN_INFO *conn_new(const sint32 sfd, EN_CONN_STAT init_state,
                const sint32 event_flags,
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
            LOGFUNC(Err, True, "Failed to allocate connection object\n");
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
            conn_free(c);
            LOGFUNC(Err, True, "Failed to allocate buffers for connection\n");
            return NULL;
        }

        c->s32Sockfd = sfd;
        ppstConnList[sfd] = c;
    }

    if (init_state == enConnNewCmd) 
	{
        if (getpeername(sfd, (struct sockaddr *) &c->request_addr,
                        &c->request_addr_size)) 
		{
			LOGFUNC(Err, True, "getpeername error");
            memset(&c->request_addr, 0, sizeof(c->request_addr));
        }
		c->request_addr_size = sizeof(c->request_addr);
    }

    if (gstSettings.verbose) 
	{
        if (init_state == enConnListening) 
		{
            LOGFUNC(Info, False, "<%d server listening\n", sfd);
        } 
    }

    c->enConnStat = init_state;
    c->rbytes = c->wbytes = 0;
    c->wcurr = c->wbuf;
    c->rcurr = c->rbuf;

    c->iovused = 0;
    c->msgcurr = 0;
    c->msgused = 0;

    event_set(&c->event, sfd, event_flags, event_handler, (void *)c);
    event_base_set(base, &c->event);
    c->ev_flags = event_flags;

    if (event_add(&c->event, 0) == -1) 
	{
        LOGFUNC(Err, True, "event_add");
        return NULL;
    }

    return c;
}

void event_handler(const int fd, const short which, void *arg) 
{
    ST_CONN_INFO *c;

    c = (ST_CONN_INFO *)arg;
    assert(c != NULL);

    /* sanity */
    if (fd != c->s32Sockfd)
	{
        if (gstSettings.verbose)
            LOGFUNC(Debug, False, "Catastrophic: event fd doesn't match conn fd!\n");
        conn_close(c);
        return;
    }

    drive_machine(c);

    /* wait for next event */
    return;
}

static int new_socket(struct addrinfo *ai) 
{
    int sfd;
    int flags;

    if ((sfd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol)) == -1) 
	{
        return -1;
    }

    if ((flags = fcntl(sfd, F_GETFL, 0)) < 0 ||
        fcntl(sfd, F_SETFL, flags | O_NONBLOCK) < 0)
    {
		LOGFUNC(Err, False, "setting O_NONBLOCK");
        close(sfd);
        return -1;
    }
    return sfd;
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
static int server_socket(const char *interface, int port) 
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
			LOGFUNC(Err, False, "getaddrinfo(): %s\n", gai_strerror(error));
        }
        else
        {
			LOGFUNC(Err, False, "getaddrinfo()");
        }
        return 1;
    }

    for (next= ai; next; next= next->ai_next) 
	{
        ST_CONN_INFO *listen_conn_add;
        if ((sfd = new_socket(next)) == -1) 
		{
            /* getaddrinfo can return "junk" addresses,
	             * we make sure at least one works before erroring.
	             */
            if (errno == EMFILE)
			{
                /* ...unless we're out of fds */
                LOGFUNC(Err, False, "server_socket");
                exit(-1);
            }
            continue;
        }

        setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, (void *)&flags, sizeof(flags));

		//tcp socket
		{
            error = setsockopt(sfd, SOL_SOCKET, SO_KEEPALIVE, (void *)&flags, sizeof(flags));
            if (error != 0)
                LOGFUNC(Err, False, "setsockopt");

            error = setsockopt(sfd, SOL_SOCKET, SO_LINGER, (void *)&ling, sizeof(ling));
            if (error != 0)
                LOGFUNC(Err, False, "setsockopt");

            error = setsockopt(sfd, IPPROTO_TCP, TCP_NODELAY, (void *)&flags, sizeof(flags));
            if (error != 0)
                LOGFUNC(Err, False, "setsockopt");
        }

        if (bind(sfd, next->ai_addr, next->ai_addrlen) == -1) 
		{
            if (errno != EADDRINUSE) 
			{
                LOGFUNC(Err, False, "bind()");
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
                LOGFUNC(Err, False, "listen()");
                close(sfd);
                freeaddrinfo(ai);
                return 1;
            }
        }

		{
            if (!(listen_conn_add = conn_new(sfd, enConnListening,
                                             EV_READ | EV_PERSIST, 1,
                                              main_base))) 
			{
                LOGFUNC(Err, False, "failed to create listening connection\n");
                exit(1);
            }
            listen_conn_add->pstConnNext = listen_conn;
            listen_conn = listen_conn_add;
        }
    }

    freeaddrinfo(ai);

    /* Return zero iff we detected no errors in starting up connections */
    return success == 0;
}

static void drive_machine(ST_CONN_INFO *c)
{
	bool stop = false;
    sint32 		sfd;
    socklen_t 	addrlen;
    struct sockaddr_storage addr;
    //sint32 nreqs = gstSettings.s32ReqsPerEvent;
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
	                    if (gstSettings.verbose)
	                        LOGFUNC(Debug, False, "Too many open connections\n"); 
	                    stop = true;
	                } 
					else 
					{
						LOGFUNC(Err, True, "accept error");
	                    stop = true;
	                }
	                break;
				}	

				if (fcntl(sfd, F_SETFL, fcntl(sfd, F_GETFL) | O_NONBLOCK) < 0) 
				{
                    LOGFUNC(Err, True, "setting O_NONBLOCK");
                    close(sfd);
                    break;
                }
				LOGFUNC(Debug, False, "accept a connect\n"); 

				dispatch_conn_new(sfd, enConnNewCmd, EV_READ | EV_PERSIST,
                                     DATA_BUFFER_SIZE);
			case enConnNewCmd:
			case enConnWaiting:
			case enConnRead:
			case enConnParseCmd:
			case enConnWrite:
			case enConnClosing:
			case enConnClosed:
			default :
				LOGFUNC(Debug, False, "current stat is %s\n", state_text(c->enConnStat));
				break;
		}
	}
}


int main(int argc, char *argv[])
{
	struct rlimit rlim;
	
	#ifdef OPEN_SYSLOG
	LOG_Init();
	#endif
	
	LOGFUNC(Info, False, "replay server start!\n");

	if (!sanitycheck())
	{
		return FAIL;
	}

	settings_init();
	/*
     * If needed, increase rlimits to allow as many connections
     * as needed.
     */
    if (getrlimit(RLIMIT_NOFILE, &rlim) != 0) 
	{
        LOGFUNC(Err, False, "failed to getrlimit number of files\n");
        exit(-1);
    } 
	else 
	{
        rlim.rlim_cur = gstSettings.s32MaxConns;
        rlim.rlim_max = gstSettings.s32MaxConns;
        if (setrlimit(RLIMIT_NOFILE, &rlim) != 0)
		{
            LOGFUNC(Err, False, "failed to set rlimit for open files. Try starting as root or requesting smaller maxconns value.\n");
            exit(-1);
        }
    }

	/* initialize main thread libevent instance */
    main_base = event_init();

	/* initialize*/
	conn_init();
	
	relaysrv_thread_init(gstSettings.s32ThreadNum, main_base);

	server_socket(NULL, 11211);

	clock_handler(0,0,0);
	/* enter the event loop */
    if (event_base_loop(main_base, 0) != 0) {
        exit(-1);
    }
	
	return 0;
}
