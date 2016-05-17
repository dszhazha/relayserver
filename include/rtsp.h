/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *  	replay server 
 *
 *   	http://www.ecsino.com/
 *
 *  	Copyright 2016 Danga Interactive, Inc.  All rights reserved.
 *		File Name 	: 	rtsp.h
 *  		Authors		:	Vict Ding <dszhazha@163.com>
 * 		Date   		: 	2016/3/30     
 *      
 */

#define 	DEFAULT_RTSP_PORT			554
#define 	RTSP_PORT_OFFSET			2000

#define RTSP_PARSE_OK 					0
#define RTSP_PARSE_IS_RESP  			-4
#define RTSP_PARSE_INVALID_OPCODE	 	-1
#define RTSP_PARSE_INVALID 				-2
#define RTSP_PARSE_ISNOT_RESP 			-3
#define RTSP_BAD_STATUS_BEGIN 			202

#define RTSP_STATUS_CONTINUE            100
#define RTSP_STATUS_OK                  200
#define RTSP_STATUS_ACCEPTED            202
#define RTSP_STATUS_BAD_REQUEST         400
#define RTSP_STATUS_METHOD_NOT_ALLOWED  405

#define NAL_TYPE_SLICE      1
#define NAL_TYPE_IDR        5
#define NAL_TYPE_SEI        6
#define NAL_TYPE_SPS        7
#define NAL_TYPE_PPS        8
#define NAL_TYPE_SEQ_END    9
#define NAL_TYPE_STREAM_END 10

#define 	MAX_VOD_CHN					5	
#define 	MAX_CLIENTS					4	/* RTSP允许的最大连接用户数 */

#define H264_STARTCODE_LEN      4 /* 00 00 00 01 */
#define	NAL_FRAGMENTATION_SIZE		1442	/*H264码流的分包单位长度为1024*/


#define RTSP_VERSION  			"RTSP/1.0"
#define RTSP_LRLF 				"\r\n"

#define SRV_NAME         		"ECSINO_RELAY_SRV"    
#define SRV_VER                  "ECSINO_V1.0"    

#define MULTICAST_IP            "0.0.0.0"
#define MULTICAST_PORT	        34767
#define MULTICAST_TTL	        1

#define RTSP_METHOD_SETUP      "SETUP"
#define RTSP_METHOD_REDIRECT   "REDIRECT"
#define RTSP_METHOD_PLAY       "PLAY"
#define RTSP_METHOD_PAUSE      "PAUSE"
#define RTSP_METHOD_SESSION    "SESSION"
#define RTSP_METHOD_RECORD     "RECORD"
#define RTSP_METHOD_EXT_METHOD "EXT-"
#define RTSP_METHOD_OPTIONS    "OPTIONS"
#define RTSP_METHOD_DESCRIBE   "DESCRIBE"
#define RTSP_METHOD_GET_PARAM  "GET_PARAMETER"
#define RTSP_METHOD_SET_PARAM  "SET_PARAMETER"
#define RTSP_METHOD_TEARDOWN   "TEARDOWN"
#define RTSP_METHOD_INVALID	   "Invalid Method"

/* message header keywords */
#define RTSP_HDR_CONTENTLENGTH 		"Content-Length"
#define RTSP_HDR_ACCEPT 			"Accept"
#define RTSP_HDR_ALLOW 				"Allow"
#define RTSP_HDR_BLOCKSIZE 			"Blocksize"
#define RTSP_HDR_CONTENTTYPE 		"Content-Type"
#define RTSP_HDR_DATE 				"Date"
#define RTSP_HDR_REQUIRE 			"Require"
#define RTSP_HDR_TRANSPORTREQUIRE 	"Transport-Require"
#define RTSP_HDR_SEQUENCENO 		"SequenceNo"
#define RTSP_HDR_CSEQ 				"CSeq"
#define RTSP_HDR_STREAM 			"Stream"
#define RTSP_HDR_SESSION 			"Session"
#define RTSP_HDR_TRANSPORT 			"Transport"
#define RTSP_HDR_RANGE 				"Range"	
#define RTSP_HDR_USER_AGENT 		"User-Agent"	
#define RTSP_HDR_AUTHORIZATION		"Authorization"

#define RTSP_USER					"user"
#define RTSP_PWD					"pwd"

#define RTSP_SDP					"application/sdp"

#define RTSP_MAKE_RESP_CMD(req)		(req+100)

#define 	AUDIO_TYPE				97
#define   	AUDIO_AAC_TYPE			108


typedef enum RtspReqMethod{
	RTSP_REQ_METHOD_SETUP = 0,
	RTSP_REQ_METHOD_DESCRIBE,
	RTSP_REQ_METHOD_REDIRECT,
	RTSP_REQ_METHOD_PLAY,
	RTSP_REQ_METHOD_PAUSE,
	RTSP_REQ_METHOD_SESSION,
	RTSP_REQ_METHOD_OPTIONS,
	RTSP_REQ_METHOD_RECORD,
	RTSP_REQ_METHOD_TEARDOWN,
	RTSP_REQ_METHOD_GET_PARAM,
	RTSP_REQ_METHOD_SET_PARAM,
	RTSP_REQ_METHOD_EXTENSION,
	RTSP_REQ_METHOD_MAX
}RtspReqMethod_e;

typedef struct RtspMethod{
	sint8   	*describe;
	sint32		opcode;
}ST_RTSP_METHOD;

enum ClientType
{
	enClientTypeUndefined = 0,
	enClientTypeEcsion 
}ST_CLITE_TYPE;

typedef enum TrackId
{
	TRACK_ID_VIDEO = 0,
	TRACK_ID_AUDIO,
	TRACK_ID_METADATA,
	TRACK_ID_UNKNOWN
}EN_TRACK_ID;

