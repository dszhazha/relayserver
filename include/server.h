/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *  	replay server 
 *
 *   	http://www.ecsino.com/
 *
 *  	Copyright 2016 Danga Interactive, Inc.  All rights reserved.
 *		File Name 	: 	server.h
 *  		Authors		:	Vict Ding <dszhazha@163.com>
 * 		Date   		: 	2016/2/29     
 *      
 */

#include "type.h"
#include "log.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <event.h>
#include <netdb.h>
#include <pthread.h>
#include <unistd.h>

#define UPDATE_INTERVAL 	0

#define DATA_BUFFER_SIZE 	20000//2048

#define IOV_MAX			1024
#define MAX_PAYLOAD_SIZE 1400

/** Initial size of the sendmsg() scatter/gather array. */
#define IOV_LIST_INITIAL 400

/** Initial number of sendmsg() argument structures to allocate. */
#define MSG_LIST_INITIAL 10

/** High water marks for buffer shrinking */
#define READ_BUFFER_HIGHWAT 40000//8192
#define IOV_LIST_HIGHWAT 600
#define MSG_LIST_HIGHWAT 100

#define MAX_RTP_LEN		1600


/*protocal head and tail byte defined*/
#define PROTOCAL_HEAD_BYTE	0xFAF5F6
#define PROTOCAL_TAIL_BYTE	0xFAF6F5

/*ringbuffer index*/
#define MAX_INDEX			65000

#define MAX_TOKENS 12
#define COMMAND_TOKEN 0
#define SUBCOMMAND_TOKEN 1

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
    enConnNread,      	/**< reading in a fixed number of bytes */
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

typedef struct token_s 
{
    sint8 *value;
    uint32 length;
} ST_TAKEN;

/*command type*/
typedef enum commandType
{
	enDevLoginCmd = 0x21,   /*device login first*/
	enDevInfoCmd,			/*upload device message*/
	enSendHeartBeat,		/*upload device heartBeat*/
	enStreamStart,			/*device start transmit data*/
	enStreamEnd,			/*device stop transmit data*/
	enStreamSenddata			/*video and audio data*/
}EN_CMD_TYPE;

typedef enum attributeType
{
	enCmdRes = 0x01,  /*message result*/
	enErrReason = 0x02,	/*message error code*/
	enDevName = 0x10,	/*device name*/
	enDevPasswd = 0x11,	/*device password*/
	enEncType = 0x12,	/*video encodec type*/
	//enVoReso = 0x13,	/*video resolution*/
	enVoWidth = 0x13,	/*video width*/
	enVoHeigth, 		/*video heigth*/
	enVobit,			/*video bit rate*/
	enVoFps,			/*video frame per second*/
	enVoBrc ,  			/*Bit Rate Control*/
	enAoType ,			/*audio encodec type*/
	enStreamData,		/*video or audio stream data*/
	enStreamType,		/*stream Type*/
	enStreamPts,		/*stream pts*/
	enVencBase64		/*video decodec message*/
}EN_ATTR_TYPE;

typedef enum commandRQ
{
	enCmdTypeReq,		/*request*/
	enCmdTypeRes		/*responce*/
}EN_CMD_RQ;

typedef enum connectType
{
	enConnDevListen,	/*listen socket*/
	enConnPlayerListen,
	enConnDevice,   /*ipc*/
	enConnPlayer	/*video player*/
}EN_CONN_TYPE;

typedef enum deviceStatus
{
	enDevInit,		/*login*/
	enDevLogin,
	enDevMessage,
	enDevHeartBeat,
	enDevTransfStream
}EN_DEV_STAT;

typedef enum videoEncodeType
{
	enVencH264,
	enVencMJPEG
}EN_VENC_TYPE;

typedef enum videoResolution
{
	enResoCif,
	enResoD1,
	enReso720p,
	enReso1080p
}EN_VIDEO_RESO;

typedef enum videoBitrateControl
{
	enBrcCbr,
	enBrcVbr
}EN_VIDEO_BRC;

typedef enum audioEncodeType
{
	enAencPcmu,
	enAencPcma,
	enAencFaac
}EN_AENC_TYPE;

typedef struct deviceInformation
{
	sint8 			devName[NAME_LEN];
	sint8 			devPasswd[NAME_LEN];
	EN_VENC_TYPE 	enVencType;
	EN_VIDEO_RESO	enVresolution;
	uint32			u32VideoWidth;
	uint32			u32VideoHeigth;
	uint32			u32VideoBit;
	uint32 			u32VideoFps;
	EN_VIDEO_BRC	enVbrc;
	EN_AENC_TYPE	enAencType;
	sint8 			Base64[MAX_BASE64_LEN];
	
	EN_DEV_STAT 	enDevStat;
}ST_DEV_INFO;

typedef enum transmitResult 
{
    TRANSMIT_COMPLETE,   /** All done writing. */
    TRANSMIT_INCOMPLETE, /** More data remaining to write. */
    TRANSMIT_SOFT_ERROR, /** Can't write any more right now. */
    TRANSMIT_HARD_ERROR  /** Can't write (c->state is set to conn_closing) */
}EN_TRENSMIT_RES;

typedef struct FrameIndex
{
	sint32			bIsLock;			/* 共享锁 */
	sint32 			frameType;		/* 当前包的类型 */
	uint32			len;			/* 当前包的长度 */
	uint32			offset;			/* 当前包数据的起始地址 */
	uint32			pts;			/* 时间戳 */
	time_t			wTime; 			/* 写入时间 */
}ST_FRAME_INDEX;

typedef struct RingBuffer
{
	bool			bHaveNoDataflag; 
	sint32			bIsFull;			/* 满标志 */
	uint32 			u32MaxLen;			/* 最大容量 */
	uint32			u32LeftLen;			/* 剩余容量 */
	uint32			u32HeadIndex;		/* 指向ringBuf头的索引 */
	uint32 			u32CurIndex; 		/* 当前操作的索引 */
	uint32			u32OldIndex; 		/* 最旧的索引 */
	uint32			u32NewIndex;		/* 最新的索引 */
	uint32			u32CurPos;			/* 当前位置 */
	uint32			u32CurPlayIndex;	/*接下来需要播放的索引*/
	sint8			*strBuf;			/* 数据指针 */

	pthread_mutex_t		muxLock;
	pthread_cond_t		condRW;
	ST_FRAME_INDEX	stIndex[MAX_INDEX];	/* 索引 */	
}ST_RING_BUF;

typedef enum RtpStreamType
{
	RTP_STREAM_VIDEO = 0,
	RTP_STREAM_AUDIO,
	RTP_STREAM_METADATA,
	RTP_STREAM_MAX
}EN_RTPSTREAM_TYPE;

typedef enum RtpTransportType
{
	RTP_TRANSPORT_TYPE_UDP, 
	RTP_TRANSPORT_TYPE_TCP, 
	RTP_TRANSPORT_TYPE_BUTT
}EN_RTPTRANSPORT_TYPE;

typedef struct RtpTcpTicket
{
	uint32		rtp;
	uint32		rtcp;
}ST_RTPTCPTICKET;

typedef enum VodSessionState
{
	RTSP_STATE_INIT		= 0,
	RTSP_STATE_READY,
	RTSP_STATE_PLAY,
	RTSP_STATE_STOP,
	RTSP_TCP_EXIT,
	RTSP_STATE_BUTT
}EN_VODSESSION_STATE;

typedef struct RtpTcpSender_s{
	ST_RTPTCPTICKET 	interleaved[RTP_STREAM_MAX];
	uint32		audioG726Ssrc;
	uint32		audioG711Ssrc;
	uint32		videoH264Ssrc;
	uint32      metadataSsrc;
	uint16      metadataSeq;
	uint16		lastSn;
	uint16		AudioSeq;
	uint16		lastTs;
	sint32		channel;
	sint32		tcpSockFd;
	uint8		sendBuf[MAX_RTP_LEN];
	uint32		sendLen;
}ST_RTPTCP_SENDER;

typedef struct rtspSession
{
	sint8	userAgent[128];
	sint8	range[64];
	sint8	hostIp[64];
	sint8	remoteIp[64];
	sint32	remotePort;
	
	sint32	clientType;
	sint32	s32LastSendReq;
	uint32	u32LastSendSeq;	
	uint32	u32LastRecvSeq;

	bool	reqStreamFlag[RTP_STREAM_MAX];
	bool	setupFlag[3];
	sint8	sessId[16];

	sint32	remoteRtpPort[RTP_STREAM_MAX];
	sint32	remoteRtcpPort[RTP_STREAM_MAX];

	ST_RTPTCPTICKET			interleaved[RTP_STREAM_MAX];
	EN_RTPTRANSPORT_TYPE	transportType;
	EN_VODSESSION_STATE		sessStat;

	ST_RTPTCP_SENDER		*pstRtpSender;
	void 	*pstDevSession;
	struct event evSendDataEvent;
}ST_RTSP_SESSION;

typedef struct
{
	sint32 	multicast_port;
	sint32 	multicast_ttl;
	sint8 	multicast_ip[32];
}ST_MULTICAST_PARA;

/**
 * The structure representing a connection into replay server.
 */
typedef struct connectInformation
{
	sint32	s32Sockfd;
	struct event evConnEvent;
	sint16  s16Evflags;

	EN_CONN_TYPE	enConnType;
	rel_time_t	lastCmdTime;
	EN_CONN_STAT	enConnStat;
	 /** which state to go into after finishing current write */
    EN_CONN_STAT  write_and_go;

	/*read buffer struct*/
	sint8   *rbuf;   /** buffer to read commands into */
    sint8   *rcurr;  /** but if we parsed some already, this is where we stopped */
    sint32   rsize;   /** total allocated size of rbuf */
    sint32   rbytes;  /** how much data, starting from rcur, do we have unparsed */
					
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

	/*stream intformation*/
	uint32	stream_type;
	uint32	stream_pts;
	uint32	stream_len;
	uint32	stream_leftlen;
	uint32	rbindex;   //ringbuf index
	sint8 	*rbptr;//ringbuf free space ptr

	ST_RING_BUF 	*pstRingBuf; 
	ST_DEV_INFO 	*pstDevInfo;
	ST_RTSP_SESSION *pstRtspSess;

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

