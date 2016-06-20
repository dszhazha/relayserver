/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *      replay server
 *
 *      http://www.ecsino.com/
 *
 *      Copyright 2016 Danga Interactive, Inc.  All rights reserved.
 *      File Name   :   rtsp.c
 *          Authors     :   Vict Ding <dszhazha@163.com>
 *      Date        :   2016/3/30
 *
**/

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <fcntl.h>

#include "server.h"
#include "rtsp.h"
#include "rtp.h"
#include "md5.h"

/*extern function*/
extern int WBUFFER_AddMsghdr(ST_CONN_INFO *c);
extern void CONN_SocketClose(ST_CONN_INFO *c) ;
extern void CONN_SetState(ST_CONN_INFO *pstConnInfo, EN_CONN_STAT enState);
extern ST_CONN_INFO *CONN_CheckMapConnect(sint8 *name, sint8 *passwd);
extern sint32 CONN_CheckStreamStart(ST_CONN_INFO *c);
extern sint32 CONN_CheckStreamStop(ST_CONN_INFO *c);

extern sint32 RINGBUF_GetOnePacket(ST_RING_BUF *ab, uint32 u32index, sint8 **s8pkt);
extern sint32 RINGBUF_GetCurPacketLen(ST_RING_BUF *ab, uint32 u32index);
extern sint32 RINGBUF_GetNewIndex(ST_RING_BUF *ab);
extern sint32 RINGBUF_GetCurNalType(ST_RING_BUF *ab, uint32 index);
extern sint32 RINGBUF_GetCurPktPts(ST_RING_BUF *ab, uint32 index);
extern sint32 RINGBUF_CheckTimeout(ST_RING_BUF *ab, uint32 sendIndex, uint32 newIndex);
extern void RINGBUF_IndexCount(ST_RING_BUF *ab, uint32 *index);

int RTP_TcpPacket(ST_RTPTCP_SENDER *pSender, EN_RTP_PT payloadType, uint32 pts,
									 sint32 marker, sint32 len, sint8 *data)
{
	RtpHdr_t *pRtpHdr = NULL;
	unsigned short *intlvd_ch = (unsigned short *)(pSender->sendBuf+2);
 
	pSender->sendLen = 0;
	pRtpHdr = (RtpHdr_t *)(pSender->sendBuf + 4);
	RTP_HDR_SET_VERSION(pRtpHdr, RTP_VERSION);
	RTP_HDR_SET_P(pRtpHdr, 0);
	RTP_HDR_SET_X(pRtpHdr, 0);
	RTP_HDR_SET_CC(pRtpHdr, 0);
	RTP_HDR_SET_M(pRtpHdr, marker);
	RTP_HDR_SET_PT(pRtpHdr, payloadType);
	if( payloadType == RTP_PT_ALAW || RTP_PT_ULAW == payloadType || 
		RTP_PT_G726 == payloadType||RTP_PT_AAC == payloadType)
	{
		RTP_HDR_SET_SEQNO(pRtpHdr, htons(pSender->AudioSeq));
		RTP_HDR_SET_SSRC(pRtpHdr, htonl(pSender->audioG711Ssrc));
		pSender->AudioSeq ++;
	}
	else if(RTP_PT_METADATA== payloadType)
	{
		RTP_HDR_SET_SEQNO(pRtpHdr, htons(pSender->metadataSeq));
		RTP_HDR_SET_SSRC(pRtpHdr, htonl(pSender->metadataSsrc));
		pSender->metadataSeq ++;
	}
	else
	{
		RTP_HDR_SET_SEQNO(pRtpHdr, htons(pSender->lastSn));
		RTP_HDR_SET_SSRC(pRtpHdr, htonl(pSender->videoH264Ssrc));
		pSender->lastSn ++;
	}
	RTP_HDR_SET_TS(pRtpHdr, htonl(pts));
 
	pSender->lastTs = pts;
 
	//#ifndef __one_copy__
	//memcpy(pSender->sendBuf + RTP_HDR_LEN + 4 , data, len);
	//#endif 
	pSender->sendLen = RTP_HDR_LEN + len;
 
	pSender->sendBuf[0] = '$';
	if((payloadType == RTP_PT_H264)
			|| (payloadType == RTP_PT_MPEG4)
			|| (payloadType == RTP_PT_JPEG)){//fixed by xsf
			pSender->sendBuf[1] = pSender->interleaved[RTP_STREAM_VIDEO].rtp;
	 }
	else if(payloadType == RTP_PT_METADATA)
	{
		pSender->sendBuf[1] = pSender->interleaved[RTP_STREAM_METADATA].rtp;
	}
	else
	{
		pSender->sendBuf[1] = pSender->interleaved[RTP_STREAM_AUDIO].rtp;
	}
	*intlvd_ch = htons((unsigned short) pSender->sendLen);
 
	return SUCCESS;
}

static sint32 RTSP_CreateSender(ST_CONN_INFO *c)
{
	c->pstRtspSess->pstRtpSender = (ST_RTPTCP_SENDER *)calloc(1, sizeof(ST_RTPTCP_SENDER));
	if (c->pstRtspSess->pstRtpSender == NULL) 
	{
		LOG_FUNC(Err, True, "Failed to allocate rtp session\n");
		return FAIL;
	}

	c->pstRtspSess->pstRtpSender->interleaved[RTP_STREAM_VIDEO].rtp =
							c->pstRtspSess->interleaved[RTP_STREAM_VIDEO].rtp;
	c->pstRtspSess->pstRtpSender->interleaved[RTP_STREAM_VIDEO].rtcp =
							c->pstRtspSess->interleaved[RTP_STREAM_VIDEO].rtcp;
	c->pstRtspSess->pstRtpSender->interleaved[RTP_STREAM_AUDIO].rtp =
							c->pstRtspSess->interleaved[RTP_STREAM_AUDIO].rtp;
	c->pstRtspSess->pstRtpSender->interleaved[RTP_STREAM_AUDIO].rtcp =
							c->pstRtspSess->interleaved[RTP_STREAM_AUDIO].rtcp;
	//metadata
	c->pstRtspSess->pstRtpSender->interleaved[RTP_STREAM_METADATA].rtp =
							c->pstRtspSess->interleaved[RTP_STREAM_METADATA].rtp;
	c->pstRtspSess->pstRtpSender->interleaved[RTP_STREAM_METADATA].rtcp =
							c->pstRtspSess->interleaved[RTP_STREAM_METADATA].rtcp;
	c->pstRtspSess->pstRtpSender->tcpSockFd = c->s32Sockfd;
	c->pstRtspSess->pstRtpSender->lastTs 	= 3600;
	c->pstRtspSess->pstRtpSender->AudioSeq 	= 1200;
	c->pstRtspSess->pstRtpSender->metadataSeq= 2400;
	c->pstRtspSess->pstRtpSender->lastSn	= 3600;
	c->pstRtspSess->pstRtpSender->metadataSsrc = RTP_DEFAULT_SSRC + 256;
	c->pstRtspSess->pstRtpSender->audioG711Ssrc = RTP_DEFAULT_SSRC + 128;
	c->pstRtspSess->pstRtpSender->videoH264Ssrc = RTP_DEFAULT_SSRC;
	return SUCCESS;
}

static void RTSP_FreeSender(ST_CONN_INFO *c)
{
	if(c->pstRtspSess->pstRtpSender != NULL)
		free(c->pstRtspSess->pstRtpSender);
}

sint32 RTP_FrameTcpSendN(sint32 sockFd, sint8 *buf, sint32 size)
{
	sint32 n,left;
	sint8 *pBuf;
	
	n = 0;
	left = size;
	pBuf = (char *)buf;
	while(left > 0)
	{
		n = send(sockFd, pBuf, left, MSG_NOSIGNAL);
		if (n <= 0)
		{
			/* EINTR A signal occurred before any data was transmitted. */
			if(errno == EINTR)
			{
				n = 0;
				LOG_FUNC(Err, False, "socket send_n eintr error");
			}
			else
			{
				LOG_FUNC(Err, False, "socket error");
				return(-1);
			}
		}
		left -= n;
		pBuf += n;
	}
	return size;
}


sint32 RTP_TcpSend(ST_CONN_INFO *c, sint32 type)
{
	sint32 	ret = 0;
	
	if(c->s32Sockfd > 0)
	{
		ret = RTP_FrameTcpSendN(c->s32Sockfd, (char *)c->pstRtspSess->pstRtpSender->sendBuf,
												c->pstRtspSess->pstRtpSender->sendLen+4);
		/*sendBuf[0] = '$'
	          sendBuf[1] = interleaved
	          sendBuf[2~3] = sendLen
	          sendBuf[4~] = data  */
		if(ret == c->pstRtspSess->pstRtpSender->sendLen + 4)
		{
			return SUCCESS;
		}
	}
	
	LOG_FUNC(Err, True, "send %d, sendLen = %d , sockfd = %d\n",
					ret, c->pstRtspSess->pstRtpSender->sendLen, c->pstRtspSess->pstRtpSender->tcpSockFd);
	return FAIL;
}

static void *RTSP_SendStreamFxn(void *arg)
{
	ST_CONN_INFO *c = (ST_CONN_INFO *)arg;
	sint8 	*pstrData = NULL;
	sint8 	*pSend = NULL;
	sint8 	*pBuf = NULL; 
	sint32 	pktLen = 0, syncIndex, type, dataLen, nalType;
	sint32	leftLen, pos;
	uint8 	sToken;
	uint32 	pts, syncStat = SYNC_WAIT;;
	uint32 	index;
	ST_CONN_INFO *pds = (ST_CONN_INFO *)c->pstRtspSess->pstDevSession;

	assert(c != NULL);
	assert(c->enConnType == enConnPlayer);
	assert(pds->enConnType == enConnDevice);
	sleep(2);

	#if 1
	printf("Send stream start\n");

	//struct event_base *base = c->evConnEvent.ev_base;
    if (event_del(&c->evConnEvent) == -1) 
    {
		printf("Del event error\n");
		return NULL;
	}
	
	if (fcntl(c->s32Sockfd, F_SETFL, fcntl(c->s32Sockfd, F_GETFL)&~O_NONBLOCK) < 0) 
	{
		LOG_FUNC(Err, True, "setting O_NONBLOCK");
		return NULL;
	}
	#endif
	while(c->pstRtspSess->sessStat == RTSP_STATE_PLAY)
	{
		if(SYNC_WAIT == syncStat)
		{
			index = RINGBUF_GetNewIndex(pds->pstRingBuf);
			while (SYNC_WAIT == syncStat)
			{
				if(RTSP_STATE_PLAY != c->pstRtspSess->sessStat)
				{
					LOG_FUNC(Err, False, "pSess stat is not play \n");
					goto __SendStreamFxn;
				}
				pktLen = RINGBUF_GetCurPacketLen(pds->pstRingBuf, index);
				if(pktLen <= 0)
				{
					usleep(1000);
					LOG_FUNC(Info, False, "find sps ,index = %d,len=%d\n", index, pktLen);
					continue;
				}
				else if(NAL_TYPE_SPS == RINGBUF_GetCurNalType(pds->pstRingBuf, index))
				{
					LOG_FUNC(Info, False, "have find sps\n");
					syncStat = SYNC_OK;
				}
				else
				{
					RINGBUF_IndexCount(pds->pstRingBuf, &index);
					continue;
				}		
			}	
		}
		
		pktLen = RINGBUF_GetOnePacket(pds->pstRingBuf, index, &pstrData);
		/*send too fast*/
		while(pktLen <= 0)
		{
			if(RTSP_STATE_PLAY != c->pstRtspSess->sessStat)
			{
				LOG_FUNC(Err, False, "pSess stat is not play \n");
				return NULL;
			}
			index = RINGBUF_GetNewIndex(pds->pstRingBuf);
			pktLen = RINGBUF_GetOnePacket(pds->pstRingBuf, index, &pstrData);
			LOG_FUNC(Err, False, "too fast , len = %d\n", pktLen);
			usleep(1000);
		}

		syncIndex = RINGBUF_GetNewIndex(pds->pstRingBuf);
		/*send too slow*/
		if(True == RINGBUF_CheckTimeout(pds->pstRingBuf, index, syncIndex))
		{
			index = RINGBUF_GetNewIndex(pds->pstRingBuf);
			LOG_FUNC(Err, False, "too slow len = %d\n", pktLen);
			return NULL;
		}

		pBuf = (sint8 *)(c->pstRtspSess->pstRtpSender->sendBuf+RTP_HDR_LEN+4);
		type = RINGBUF_GetCurNalType(pds->pstRingBuf, index);
		pts = RINGBUF_GetCurPktPts(pds->pstRingBuf, index);
		if((type == AUDIO_TYPE)||(type == AUDIO_AAC_TYPE))
		{
			if(c->pstRtspSess->reqStreamFlag[1] == True && 0)
			{
				printf("send audio data len=%d\n", pktLen);
				memcpy(pBuf, pstrData, pktLen);
				RTP_TcpPacket(c->pstRtspSess->pstRtpSender, type, pts, 1, pktLen, pstrData);
				if(SUCCESS != RTP_TcpSend(c, RTP_STREAM_AUDIO))
				{
					LOG_FUNC(Err, False, "RTP_TcpSend error\n");
					return NULL;
				}
			}
		}
		else
		{
			if(c->pstRtspSess->reqStreamFlag[0] == True)
			{
				printf("send video data\n");
				pSend  	= pstrData + H264_STARTCODE_LEN;
				dataLen = pktLen - H264_STARTCODE_LEN;
				nalType = H264_Get_NalType(*pSend);
				/* 长度小于1024不需要分包 */
				if(dataLen <= NAL_FRAGMENTATION_SIZE)
				{
					memcpy(pBuf, pSend, dataLen);
					RTP_TcpPacket(c->pstRtspSess->pstRtpSender, RTP_PT_H264, pts, 1, dataLen, pSend);
					if(SUCCESS != RTP_TcpSend(c, RTP_STREAM_VIDEO))
					{
						LOG_FUNC(Err, False, "RTP_TcpSend error\n");
						return NULL;
					}
				}
				else
				{
					/* 长度大于1024则进行 FU-A 分包 */
					pBuf[0] = 0x1c | (*pSend & (~0x1f));
					leftLen = dataLen;
					sToken  = 1;
					pos     = 0;
					while(leftLen > NAL_FRAGMENTATION_SIZE)
					{
						if(RTSP_STATE_PLAY != c->pstRtspSess->sessStat)
						{
							LOG_FUNC(Err, False, "pSess stat is not play \n");
							return NULL;
						}
	
						pBuf[1] = (sToken << 7) | nalType;
						memcpy(pBuf+2, pSend+pos+sToken, NAL_FRAGMENTATION_SIZE - sToken);
						
						RTP_TcpPacket(c->pstRtspSess->pstRtpSender, RTP_PT_H264, pts, 0,
							NAL_FRAGMENTATION_SIZE + 2 - sToken, NULL);
						if(SUCCESS != RTP_TcpSend(c, RTP_STREAM_VIDEO))
						{
							LOG_FUNC(Err, False, "RTP_TcpSend error\n");
							return NULL;
						}
						sToken 	= 0;
						leftLen -= NAL_FRAGMENTATION_SIZE;
						pos 	+= NAL_FRAGMENTATION_SIZE;
					}
					
					if(sToken)
					{
						nalType |= 128;
					}
					
					pBuf[1] = 64 | nalType;
					memcpy(pBuf+2, pSend+pos+sToken, leftLen-sToken);
					RTP_TcpPacket(c->pstRtspSess->pstRtpSender, RTP_PT_H264, pts, 1,
														leftLen+2-sToken, NULL);
					if(SUCCESS != RTP_TcpSend(c, RTP_STREAM_VIDEO))
					{
						LOG_FUNC(Err, False, "RTP_TcpSend error\n");
						return NULL;
					}
				}
			}
		}
		RINGBUF_IndexCount(pds->pstRingBuf, &index);
	}
__SendStreamFxn:
	c->pstRtspSess->sessStat = RTSP_STATE_STOP;
	printf("Send stream fxn stop\n");
	return NULL;
}

static void RTSP_SendStreamStart(ST_CONN_INFO *c)
{
    int         ret;
	pthread_t 		thd;
	pthread_attr_t  attr;

    pthread_attr_init(&attr);
    if ((ret = pthread_create(&thd, &attr, RTSP_SendStreamFxn, c)) != 0)
	{
        LOG_FUNC(Err, True, "Can't create thread\n");
        exit(1);
    }
	pthread_detach(thd);
}

/* clear recv and send buffer*/
#define RTSP_CLEAR_SENDBUF(c)   \
{\
    memset(c->wbuf, 0x0, c->wsize);\
    c->wbytes = 0;\
}

#define RTSP_CLEAR_RECVBUF(c)   \
{\
    memset(c->rbuf, 0x0, c->rsize);\
    c->rbytes = 0;\
}


sint8 *gRtspInvalidMethod = "Invalid Method";

ST_RTSP_METHOD gstRtspMethod[] =
{
    {RTSP_METHOD_PLAY,          RTSP_REQ_METHOD_PLAY},
    {RTSP_METHOD_PAUSE,         RTSP_REQ_METHOD_PAUSE},
    {RTSP_METHOD_DESCRIBE,      RTSP_REQ_METHOD_DESCRIBE},
    {RTSP_METHOD_SETUP,         RTSP_REQ_METHOD_SETUP},
    {RTSP_METHOD_REDIRECT,      RTSP_REQ_METHOD_REDIRECT},
    {RTSP_METHOD_SESSION,       RTSP_REQ_METHOD_SESSION},
    {RTSP_METHOD_OPTIONS,       RTSP_REQ_METHOD_OPTIONS},
    {RTSP_METHOD_TEARDOWN,      RTSP_REQ_METHOD_TEARDOWN},
    {RTSP_METHOD_RECORD,        RTSP_REQ_METHOD_RECORD},
    {RTSP_METHOD_GET_PARAM,     RTSP_REQ_METHOD_GET_PARAM},
    {RTSP_METHOD_SET_PARAM,     RTSP_REQ_METHOD_SET_PARAM},
    {RTSP_METHOD_EXT_METHOD,    RTSP_REQ_METHOD_EXTENSION},
    {0,                         RTSP_PARSE_INVALID_OPCODE}
};

ST_RTSP_METHOD gstRtspStatu[] =
{
    {"Continue",                        100},
    {"OK",                              200},
    {"Created",                         201},
    {"Accepted",                        202},
    {"Non-Authoritative Information",   203},
    {"No Content",                      204},
    {"Reset Content",                   205},
    {"Partial Content",                 206},
    {"Multiple Choices",                300},
    {"Moved Permanently",               301},
    {"Moved Temporarily",               302},
    {"Bad Request",                     400},
    {"Unauthorized",                    401},
    {"Payment Required",                402},
    {"Forbidden",                       403},
    {"Not Found",                       404},
    {"Method Not Allowed",              405},
    {"Not Acceptable",                  406},
    {"Proxy Authentication Required",   407},
    {"Request Time-out",                408},
    {"Conflict",                        409},
    {"Gone",                            410},
    {"Length Required",                 411},
    {"Precondition Failed",             412},
    {"Request Entity Too Large",        413},
    {"Request-URI Too Large",           414},
    {"Unsupported Media Type",          415},
    {"Bad Extension",                   420},
    {"Invalid Parameter",               450},
    {"Parameter Not Understood",        451},
    {"Conference Not Found",            452},
    {"Not Enough Bandwidth",            453},
    {"Session Not Found",               454},
    {"Method Not Valid In This State",  455},
    {"Header Field Not Valid for Resource", 456},
    {"Invalid Range",                   457},
    {"Parameter Is Read-Only",          458},
    {"Internal Server Error",           500},
    {"Not Implemented",                 501},
    {"Bad Gateway",                     502},
    {"Service Unavailable",             503},
    {"Gateway Time-out",                504},
    {"RTSP Version Not Supported",      505},
    {"Extended Error:",                 911},
    {0,             RTSP_PARSE_INVALID_OPCODE}
};


sint32 RTSP_RecvMsgParse(sint8 *pstrMsgBuf)
{
    if(pstrMsgBuf[0] == '$')
    {
        LOG_FUNC(Info, False, "Rtcp Message...\n");
        return FAIL;
    }

    return SUCCESS;
}

sint32 RTSP_ResponseMsgCheck(sint32 *stat, sint8 *buf)
{
    uint32 state;
    sint8  version[32];
    sint8  trash[256];
    uint32 parameterNum;

    parameterNum = sscanf(buf, " %31s %u %255s ", version, &state, trash);
    if(strncasecmp(version, "RTSP/", 5))
    {
        return RTSP_PARSE_ISNOT_RESP;
    }

    if(parameterNum < 3 || state == 0)
    {
        return RTSP_PARSE_ISNOT_RESP;
    }

    *stat = state;
    return RTSP_PARSE_IS_RESP;
}

sint32 RTSP_GetReq(sint8 *buf)
{
    sint32 cnt;
    sint8 method[32];
    sint8 object[256];
    sint8 ver[32];
    ST_RTSP_METHOD *m;

    *method = *object = '\0';

    cnt = sscanf(buf, " %31s %255s %31s", method, object, ver);
    if(cnt != 3)
    {
        LOG_FUNC(Warn, False, "buf: %s is not a valid req message\n", buf);
    }

    for(m=gstRtspMethod; m->opcode != -1; m++)
    {
        if(!strcmp(m->describe, method))
        {
            break;
        }
    }
    return (m->opcode);
}

sint32 RTSP_GetCseq(sint8 *buf)
{
    sint32 cseq = -1;
    sint8  trash[255];
    sint8  *pTmp = NULL;

    pTmp = strstr(buf, RTSP_HDR_CSEQ);
    if(pTmp == NULL)
    {
        LOG_FUNC(Err, False, "not found 'Cseq'. buf=%s\n", buf);
    }
    else
    {
        if(2 != sscanf(pTmp, "%254s %d", trash, &cseq))
        {
            LOG_FUNC(Err, False, "not found 'Cseq'. buf=%s\n", pTmp);
        }
    }
    return cseq;
}

sint8 *RTSP_GetMethodDescrib(uint32 code)
{
    ST_RTSP_METHOD *pMethod;

    for(pMethod = gstRtspStatu; pMethod->opcode != RTSP_PARSE_INVALID_OPCODE; pMethod ++)
    {
        if(pMethod->opcode == code)
        {
            return (pMethod->describe);
        }
    }
    return gRtspInvalidMethod;
}

sint32 RTSP_GetHead(sint32 err, ST_CONN_INFO *c)
{
    sint8 *pTmp = NULL;

    RTSP_CLEAR_SENDBUF(c);
    pTmp = c->wbuf;
    pTmp += sprintf(pTmp, "%s %d %s\r\n", RTSP_VERSION, err,
                    RTSP_GetMethodDescrib(err));
    pTmp += sprintf(pTmp,"CSeq: %d\r\n", c->pstRtspSess->u32LastRecvSeq);
    pTmp += sprintf(pTmp,"Server: "SRV_NAME" Rtsp Server "SRV_VER"\r\n");

    return (strlen(c->wbuf));
}

void RTSP_GetMulticastPara(ST_MULTICAST_PARA *pmp)
{
    memcpy(pmp->multicast_ip, MULTICAST_IP, strlen(MULTICAST_IP));
    pmp->multicast_port = MULTICAST_PORT;
    pmp->multicast_ttl = MULTICAST_TTL;
}

sint32 RTSP_SendReply(sint32 err, sint32 simple, char *addon, ST_CONN_INFO *c)
{	
	RTSP_CLEAR_SENDBUF(c);
    sint8 *pTmp = c->wbuf;

    if(simple == 1)
    {
        pTmp += RTSP_GetHead(err, c);
    }

    if(addon)
    {
        pTmp += sprintf(pTmp, "%s", addon);
    }

    if(simple)
    {
        strcat(pTmp, RTSP_LRLF);
    }

    c->wcurr = c->wbuf;
    c->wbytes = strlen(c->wbuf);

    c->msgcurr = 0;
    c->msgused = 0;
    c->iovused = 0;
    WBUFFER_AddMsghdr(c);

    RTSP_CLEAR_RECVBUF(c);
    CONN_SetState(c, enConnWrite);

    return SUCCESS;
}

sint32 RTSP_ParseUrl(sint32 *port, sint8 *server, sint8 *fileName, const sint8 *url)
{
    sint32 ret = FAIL;
    sint32 havePort = 0;
    sint32 len, n;
    sint8 *pTmp = NULL;
    sint8 *pStr = NULL;
    sint8 *pEnd, *pPort;
    sint8 *full = (char *)malloc(strlen(url) + 1);
    if(NULL == full)
    {
        LOG_FUNC(Err, True, "os_malloc error \n");
        return FAIL;
    }
    memset(full,0,strlen(url) + 1);
    *port = DEFAULT_RTSP_PORT;
    strcpy(full, url);

    if(0 == strncmp(full, "rtsp://", 7))
    {
        pStr = (char *)malloc(strlen(url) + 1);
        if(pStr != NULL)
        {
            memset(pStr,0,strlen(url) + 1);
            strcpy(pStr, &full[7]);
            if(strchr(pStr, '/'))
            {
                len = 0;
                pEnd = strchr(pStr, '/');
                len = pEnd - pStr;
                for(n=0; n<strlen(url); n++)
                {
                    pStr[n] = 0;
                }
                strncpy(pStr, &full[7], len);
            }

            if(strchr(pStr, ':'))
            {
                havePort = 1;
            }
            free(pStr);
            pStr = NULL;
            pTmp = strtok(&full[7], " :/\t\n");
            if(pTmp != NULL)
            {
                strcpy(server, pTmp);
                if(havePort)
                {
                    pPort = strtok(&full[strlen(server) + 7 + 1], " /\t\n");
                    if(pPort != NULL)
                    {
                        *port = atol(pPort);
                    }
                }
                pTmp = strtok(NULL, " ");
                if(pTmp )
                {
                    strcpy(fileName, pTmp);
                }
                else
                {
                    fileName[0] = '\0';
                }
                ret = SUCCESS;
            }
        }
        else
        {
            LOG_FUNC(Err, True, "malloc error \n");
            ret = FAIL;
        }
    }
    else
    {
        pTmp = strtok(full, "\t\n");
        if(pTmp)
        {
            strncpy(fileName, pTmp, 16);
            server[0] = '\0';
            ret = SUCCESS;
        }
    }
    free(full);
    full = NULL;

    return ret;
}

sint32 RTSP_PraseUserPwd(sint8 *buff, sint8 *name, sint8 *passwd)
{
	char *p,*q = NULL;
	
	p = strstr(buff, RTSP_USER);
	if(p == NULL)
	{
		LOG_FUNC(Err, False, "Parse User start error\n");
		return FAIL;
	}
	p += (strlen(RTSP_USER)+1);
	q = strchr(p, '+');
	if(q == NULL || q-p >= NAME_LEN)
	{
		LOG_FUNC(Err, False, "Parse User end error\n");
		return FAIL;
	}
	memcpy(name, p, q-p);

	p = strstr(buff, RTSP_PWD);
	if(p == NULL)
	{
		LOG_FUNC(Err, False, "Parse Pwd start error\n");
		return FAIL;
	}
	p += (strlen(RTSP_PWD)+1);
	q = strchr(p, ' ');
	if(q == NULL  || q-p >= NAME_LEN)
	{
		LOG_FUNC(Err, False, "Parse Pwd end error\n");
		return FAIL;
	}
	memcpy(passwd, p, q-p);

	return SUCCESS;
}

static sint32 RTSP_GetHostInfo(sint32 sockFd, sint8 *ipAddr)
{
	socklen_t addrLen;
	struct sockaddr_in hostAddr;

	addrLen = sizeof(hostAddr);
	if( -1 == getsockname(sockFd, (struct sockaddr *)&hostAddr, &addrLen))
	{
		LOG_FUNC(Err, False, "getsockname error \n");
		return FAIL;
	}
	strncpy(ipAddr, (const char *)inet_ntoa(hostAddr.sin_addr), 64);
	return SUCCESS;
}

static sint32 RTSP_GetPeerInfo(sint32 sockFd, sint8 *ipAddr, sint32 *port)
{
	socklen_t addrLen;
	struct sockaddr_in cliAddr;

	addrLen = sizeof(cliAddr);
	if(-1 == getpeername(sockFd, (struct sockaddr *)&cliAddr, &addrLen))
	{
		LOG_FUNC(Err, False, "getpeername error \n");
		return FAIL;
	}
	strncpy(ipAddr, (const char *)inet_ntoa(cliAddr.sin_addr), 64);
	*port = (int)ntohs(cliAddr.sin_port);

	return SUCCESS;
}


sint32 RTSP_GetChannel(const sint8*szChn)
{
	int chn = -1;

	if(szChn == NULL)
	{
		return FAIL;
	}
	else
	{
		chn = atoi(szChn);
	}
	return chn;
}

sint32 RTSP_GetCurClientCnt(void)
{
	sint32 clients = 0;

	return clients;
}

sint32 RTSP_CheckChn(sint32 chn)
{
	sint32 curClientCnt = 0;


	if(chn<0 || chn> MAX_VOD_CHN){
		return FAIL;
	}

	curClientCnt = RTSP_GetCurClientCnt();
	if(curClientCnt >= MAX_CLIENTS)
	{
		LOG_FUNC(Err, False, "too more clients MAX[%d] > %d!! \n", curClientCnt, MAX_CLIENTS);
		return FAIL;
	}

	return SUCCESS;
}

void RTSP_GetSessionId(sint8 *sessId, sint32 len)
{
	sint32 i;
	for(i=0; i<len; i++){
		sessId[i] = (char )((random()%10) + '0');
	}
	sessId[len] = 0;
}

sint32 RTSP_GetRange(sint8 *buf, sint8 *ntp_buf)
{
	char  *pTmp = NULL;

	pTmp = strstr(buf, RTSP_HDR_RANGE);
	if(pTmp == NULL)
	{
		return FAIL;
	}
	else
	{
		pTmp += 7;/*Range: */
		if(1 != sscanf(pTmp, "%64s", ntp_buf))
		{
			return FAIL;
		}
	}
	return SUCCESS;
}

sint32 RTSP_SessionCreate(ST_CONN_INFO *c)
{
	c->pstRtspSess = (ST_RTSP_SESSION *)calloc(1, sizeof(ST_RTSP_SESSION));
	if (c->pstRtspSess == NULL) 
	{
		LOG_FUNC(Err, True, "Failed to allocate rtsp session\n");
		return FAIL;
	}

	c->pstRtspSess->setupFlag[0] = 0;
	c->pstRtspSess->setupFlag[1] = 0;
	c->pstRtspSess->setupFlag[2] = 0;

	RTSP_GetSessionId(c->pstRtspSess->sessId, 8);

	c->pstRtspSess->sessStat = RTSP_STATE_INIT;

	if( SUCCESS != RTSP_GetPeerInfo(c->s32Sockfd, c->pstRtspSess->remoteIp, &c->pstRtspSess->remotePort))
	{
		LOG_FUNC(Err, False, "RTSP_GetPeerInfo error\n ");
		free(c->pstRtspSess);
		c->pstRtspSess = NULL;
		return FAIL;
	}

	if(SUCCESS != RTSP_GetHostInfo(c->s32Sockfd, c->pstRtspSess->hostIp))
	{
   		LOG_FUNC(Err, False, "RTSP_GetHostInfo error!\n");
		free(c->pstRtspSess);
		c->pstRtspSess = NULL;
		return FAIL;
	}

	return SUCCESS;
}



sint32 RTSP_EventHandleOptions(ST_CONN_INFO *c)
{
    sint32 station;

    memset(c->wbuf, 0x0, sizeof(c->wsize));

    station = RTSP_GetHead(RTSP_STATUS_OK, c);
    sprintf(c->wbuf + station,
            "Public: OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN, SET_PARAMETER\r\n\r\n");

    c->wcurr = c->wbuf;
    c->wbytes = strlen(c->wbuf);

    c->msgcurr = 0;
    c->msgused = 0;
    c->iovused = 0;
    WBUFFER_AddMsghdr(c);

    RTSP_CLEAR_RECVBUF(c);
    CONN_SetState(c, enConnWrite);
    return SUCCESS;
}

sint32 RTSP_EventHandleDescribe(ST_CONN_INFO *c)
{
	sint32  port, chn=0;
	sint8 	server[128]={0};
    sint8   url[256]= {0};
	sint8 	szChn[255]={0};
	sint8 	user[NAME_LEN]={0};
	sint8 	pwd[NAME_LEN]={0};
	
	sint8 	*pTmp = NULL;
	sint8 	*p = NULL;
	sint8 	*pSdp = NULL;
	
    ST_MULTICAST_PARA   stMulticastPara;
    memset(&stMulticastPara, 0x0, sizeof(ST_MULTICAST_PARA));
    //memset(&md5p, 0x0, sizeof(MD5_CTX));

    RTSP_GetMulticastPara(&stMulticastPara);

    if(!sscanf(c->rbuf, " %*s %254s ", url))
    {
        RTSP_SendReply(RTSP_STATUS_BAD_REQUEST, 1, NULL, c);
        LOG_FUNC(Err, False, "sscanf url error \n");
        return RTSP_STATUS_BAD_REQUEST;
    }

	if(SUCCESS != RTSP_ParseUrl(&port, server, szChn, url))
	{
		LOG_FUNC(Err, False, "RTSP_ParseUrl error\n");
		RTSP_SendReply(RTSP_STATUS_BAD_REQUEST, 1, NULL, c);
		return RTSP_STATUS_BAD_REQUEST;
	}

	chn = RTSP_GetChannel(szChn);
	if(chn == FAIL)
	{
		RTSP_SendReply(RTSP_STATUS_BAD_REQUEST, 1, NULL, c);
		LOG_FUNC(Err, False, "RTSP_GetChannel error\n");
		return RTSP_STATUS_BAD_REQUEST;
	}
	
	if(NULL != strstr(c->rbuf, RTSP_HDR_ACCEPT))
	{
		if(NULL == strstr(c->rbuf, RTSP_SDP))
		{
			LOG_FUNC(Err, False, "only accept require. \n");
			RTSP_SendReply(551, 1, NULL, c);
			return 551;
		}
	}

	pTmp = strstr(c->rbuf, RTSP_HDR_USER_AGENT);
	if(pTmp != NULL)
	{
		p = pTmp;
		while(0 != (char)(*p))
		{
			if((char)(*p) == 10 || (char)(*p) == 13)
			{
				break;
			}

			if((char)(*p) == ':')
			{
				pTmp = p + 2;
			}
			p ++;
		}
		if(p != pTmp)
		{
			strncpy(c->pstRtspSess->userAgent, pTmp, p - pTmp);
		}
	}

	pTmp = strstr(c->rbuf, "Ecsino");
	if(pTmp)
	{
		c->pstRtspSess->clientType = enClientTypeEcsion;
	}
	else
	{
		c->pstRtspSess->clientType = enClientTypeUndefined;
	}

	if(SUCCESS != RTSP_PraseUserPwd(c->rbuf, user, pwd))
	{
		LOG_FUNC(Err, False, "RTSP_PraseUserPwd error\n");
		RTSP_SendReply(RTSP_STATUS_BAD_REQUEST, 1, NULL, c);
		return RTSP_STATUS_BAD_REQUEST;
	}
	
	c->pstRtspSess->pstDevSession = CONN_CheckMapConnect(user, pwd);
	if (c->pstRtspSess->pstDevSession == NULL)
	{
		LOG_FUNC(Err, False, "device name not found !\n");
		RTSP_SendReply(RTSP_STATUS_BAD_REQUEST, 1, NULL, c);
		return RTSP_STATUS_BAD_REQUEST;
	}
	ST_CONN_INFO *pds = (ST_CONN_INFO *)c->pstRtspSess->pstDevSession;

#if 0
	if(SUCCESS != CONN_CheckStreamStart(pds))
	{
		LOG_FUNC(Err, False, "CONN_CheckStreamStart error\n");
		RTSP_SendReply(RTSP_STATUS_BAD_REQUEST, 1, NULL, c);
		return RTSP_STATUS_BAD_REQUEST;
	}
#endif

	RTSP_CLEAR_SENDBUF(c);
	pTmp = c->wbuf;
	pTmp += sprintf(pTmp, "%s %d %s\r\n", RTSP_VERSION, 200, RTSP_GetMethodDescrib(200));
	pTmp += sprintf(pTmp, "Cseq: %d \r\n", c->pstRtspSess->u32LastRecvSeq);

	/*width * heigth * rate * resolution */
	pTmp += sprintf(pTmp, 
				"Server: Ecsino Rtsp Server V200R001 "
				"%d*%d*%d*%d\r\n", 
				pds->pstDevInfo->u32VideoWidth,
				pds->pstDevInfo->u32VideoHeigth,
				pds->pstDevInfo->u32VideoFps,
				pds->pstDevInfo->u32VideoBit);

	pTmp += sprintf(pTmp, "Content-Type: application/sdp\r\n");

	pSdp = (sint8 *)calloc(4096 , sizeof(char));
	if(pSdp == NULL)
	{
		LOG_FUNC(Err, False, "calloc pSdp error \n");
		return FAIL;
	}
	
	p = pSdp;
	p += sprintf(p,"v=0\r\n");
    p += sprintf(p,"o=StreamingServer 3331435948 1116907222000 IN IP4 %s\r\n", c->pstRtspSess->hostIp);
    p += sprintf(p,"s=h264.mp4\r\n");
	
	if(0==strcmp(stMulticastPara.multicast_ip, "0.0.0.0"))
    	p += sprintf(p,"c=IN IP4 0.0.0.0\r\n");
	else	
		/*read db or config ,otherwise defalu 0.0.0.0*/
   		p += sprintf(p,"c=IN IP4 %s/%d\r\n",stMulticastPara.multicast_ip,stMulticastPara.multicast_ttl);
    p += sprintf(p,"t=0 0\r\n");
    p += sprintf(p,"a=control:*\r\n");

	/*video and audio description*/
	switch(pds->pstDevInfo->enVencType)
	{
		case enVencH264:
			p += sprintf(p,"m=video 0 RTP/AVP 96\r\n");
		    p += sprintf(p,"a=control:trackID=0\r\n");
		    p += sprintf(p,"a=rtpmap:96 H264/90000\r\n");
			p += sprintf(p,"a=fmtp:96 packetization-mode=1; "
				"sprop-parameter-sets=%s\r\n", pds->pstDevInfo->Base64);//加密
			break;
		case enVencMJPEG:
			p += sprintf(p,"m=video 0 RTP/AVP 26\r\n");
			p += sprintf(p,"a=control:trackID=0\r\n");
			p += sprintf(p,"a=rtpmap:26 JPEG/90000\r\n");
			break;
		default:
			p += sprintf(p,"m=video 0 RTP/AVP 96\r\n");
		    p += sprintf(p,"a=control:trackID=0\r\n");
		    p += sprintf(p,"a=rtpmap:96 H264/90000\r\n");
			p += sprintf(p,"a=fmtp:96 packetization-mode=1; "
				"sprop-parameter-sets=%s\r\n", pds->pstDevInfo->Base64);//加密
			break;
	}

	/* RTP/AVP 97 --> G726  */
	/* RTP/AVP 8  --> G711-A */
	/* RTP/AVP 0  --> G711-U */
	switch(pds->pstDevInfo->enAencType)
	{
		case enAencPcmu:
			/* G711-U */
			p += sprintf(p,"m=audio 0 RTP/AVP 0\r\n");
		    p += sprintf(p,"a=control:trackID=1\r\n");
			p += sprintf(p,"a=rtpmap:0 PCMU/8000\r\n");
			break;
		case enAencPcma:
			/* G711-A */
		    p += sprintf(p,"m=audio 0 RTP/AVP 8\r\n");
		    p += sprintf(p,"a=control:trackID=1\r\n");
			p += sprintf(p,"a=rtpmap:8 PCMA/8000\r\n");
			break;
		case enAencFaac:
			/* AAC  */
			p += sprintf(p,"m=audio 21070 RTP/AVP 108\r\n");
			p += sprintf(p,"a=control:trackID=1\r\n");
			p += sprintf(p,"a=rtpmap:108 mpeg4-generic/16000/2\r\n");
			p += sprintf(p,"a=fmtp:108 streamtype=5; profile-level-id=15; mode=AAC-hbr; config=1410; SizeLength=13; IndexLength=3; IndexDeltaLength=3; Profile=1;\r\n");
			break;
	}
	
	/* metadata streamy */
	/* RTP/AVP 107 */
	p += sprintf(p,"m=application 0 RTP/AVP 107\r\n");
	p += sprintf(p,"a=control:trackID=2\r\n");
	p += sprintf(p,"a=rtpmap:107 vnd.onvif.metadata/90000\r\n\r\n");

	pTmp += sprintf(pTmp,"Content-length: %d\r\n", strlen(pSdp));
    pTmp += sprintf(pTmp,"Content-Base: rtsp://%s/%d/\r\n\r\n", c->pstRtspSess->hostIp, chn);

	strcat(pTmp, pSdp);
	
	if(pSdp != NULL)
	{
		free(pSdp);
		pSdp = NULL;
	}

	c->wcurr = c->wbuf;
    c->wbytes = strlen(c->wbuf);

    c->msgcurr = 0;
    c->msgused = 0;
    c->iovused = 0;
    WBUFFER_AddMsghdr(c);

    RTSP_CLEAR_RECVBUF(c);
    CONN_SetState(c, enConnWrite);
    return SUCCESS; 
	
}

sint32 RTSP_EventHandleSetup(ST_CONN_INFO *c)
{
	sint8   url[256]= {0};
	sint8 	server[128]={0}, obj[255]={0}, trash[255]={0}, line[255]={0};
	sint32 	svrPort = 0, trackId = 0, chn = 0;

	sint8 	*p = NULL, *pTmp = NULL;
	
	ST_MULTICAST_PARA	stMulticastPara;
	memset(&stMulticastPara, 0x0, sizeof(ST_MULTICAST_PARA));
	
	RTSP_GetMulticastPara(&stMulticastPara);

	if(!sscanf(c->rbuf, " %*s %254s ", url))
	{
		RTSP_SendReply(RTSP_STATUS_BAD_REQUEST, 1, NULL, c);
		LOG_FUNC(Err, False, "sscanf url error \n");
		return RTSP_STATUS_BAD_REQUEST;
	}
	
	if(SUCCESS != RTSP_ParseUrl(&svrPort, server, obj, url))
	{
		LOG_FUNC(Err, False, "RTSP_ParseUrl error\n");
		RTSP_SendReply(RTSP_STATUS_BAD_REQUEST, 1, NULL, c);
		return RTSP_STATUS_BAD_REQUEST;
	}

	p = strstr(obj, "trackID");
	if(p == NULL)
	{
		LOG_FUNC(Err, False, "no track id. \n");
		RTSP_SendReply(406, 1, "Require: Transport settings "
								"of rtp/udp;port=nnnn. ", c);
		return 406;
	}

	sscanf(p, "%8s%d", trash, &trackId);
	sscanf(obj, "%d/%s", &chn, trash);
	
	if(FAIL == RTSP_CheckChn(chn))
	{
		LOG_FUNC(Err, False, "chn = %d, error \n", chn);
		RTSP_SendReply(400, 1, NULL, c);
		return 400;
	}

	if(chn > 0)
	{
		chn = 1;
	}

	p = strstr(c->rbuf, RTSP_HDR_TRANSPORT);
	if(p == NULL)
	{
		LOG_FUNC(Err, False, "get rtp transport type  error \n");
		RTSP_SendReply(406, 1, "Require: Transport settings"
										" of rtp/udp;port=nnnn. ", c);
		return 406;
	}

	if(trackId == TRACK_ID_VIDEO)
	{
		c->pstRtspSess->reqStreamFlag[RTP_STREAM_VIDEO] = True;
	}
	else if(trackId == TRACK_ID_AUDIO)
	{
		c->pstRtspSess->reqStreamFlag[RTP_STREAM_AUDIO] = True;
	}
	else if(trackId == TRACK_ID_METADATA)
	{
		c->pstRtspSess->reqStreamFlag[RTP_STREAM_METADATA] = True;
	}
	else
	{
		LOG_FUNC(Err, False, "track id = %d error \n", trackId);
		RTSP_SendReply(400, 1, NULL, c);
		return 400;
	}

	/*
	 * Transport: RTP/AVP;unicast;client_port=6972-6973;source=10.71.147.222;
	 * 		   server_port=6970-6971;ssrc=00003654
	 * trash = "Transport:"
	 * line = "RTP/AVP;unicast;client_port=6972-6973;source=10.71.147.222;
	 *         server_port=6970-6971;ssrc=00003654"
	 */

	if(2 != sscanf(p, "%10s%255s", trash, line))
	{
		LOG_FUNC(Err, False, "setup request malformed \n");
		RTSP_SendReply(400, 1, 0, c);
		return 400;
	}

	p = strstr(line, "RTP/AVP/TCP");
	if(p != NULL)
	{
		//check multicast param
		if((0!=strcmp(stMulticastPara.multicast_ip,"0.0.0.0"))
			||(stMulticastPara.multicast_port!= 0)
			||(stMulticastPara.multicast_ttl !=0))
		{
			memset(&stMulticastPara.multicast_ip,0,32);
			memcpy(stMulticastPara.multicast_ip,"0.0.0.0",7);
			stMulticastPara.multicast_port = 0;
			stMulticastPara.multicast_ttl = 0;
		}		
		
		c->pstRtspSess->transportType = RTP_TRANSPORT_TYPE_TCP;
		RTSP_CLEAR_SENDBUF(c);
		pTmp = c->wbuf;
		pTmp += RTSP_GetHead(200, c);
		pTmp += sprintf(pTmp,"Session: %s;timeout=120\r\n", c->pstRtspSess->sessId);

		p = strstr(line, "interleaved");
		if(p != NULL)
		{	
			if(trackId == TRACK_ID_VIDEO)
			{
				if(c->pstRtspSess->setupFlag[0] == 0)
				{
					p = strstr(p, "=");
					if(p != NULL)
					{
						sscanf(p+1, "%d", &c->pstRtspSess->interleaved[RTP_STREAM_VIDEO].rtp);
					}

					p = strstr(p, "-");
					if(p != NULL)
					{
						sscanf(p+1, "%d", &c->pstRtspSess->interleaved[RTP_STREAM_VIDEO].rtcp);
					}
					else
					{
						c->pstRtspSess->interleaved[RTP_STREAM_VIDEO].rtcp =
						c->pstRtspSess->interleaved[RTP_STREAM_VIDEO].rtp + 1;
					}

					pTmp += sprintf(pTmp, "Transport: RTP/AVP/TCP;unicast;"
							"interleaved=%d-%d\r\n\r\n",
							c->pstRtspSess->interleaved[RTP_STREAM_VIDEO].rtp,
							c->pstRtspSess->interleaved[RTP_STREAM_VIDEO].rtcp);
							c->pstRtspSess->setupFlag[0] = 1;
				}
			}
			else if(trackId == TRACK_ID_AUDIO)
			{
				if(c->pstRtspSess->setupFlag[1] == 0)
				{
					p = strstr(p, "=");
					if(p != NULL)
					{
						sscanf(p+1, "%d", &c->pstRtspSess->interleaved[RTP_STREAM_AUDIO].rtp);
					}

					p = strstr(p, "-");
					if(p != NULL)
					{
						sscanf(p+1, "%d", &c->pstRtspSess->interleaved[RTP_STREAM_AUDIO].rtcp);
					}
					else
					{
						c->pstRtspSess->interleaved[RTP_STREAM_AUDIO].rtcp =
						c->pstRtspSess->interleaved[RTP_STREAM_AUDIO].rtp + 1;
					}

					pTmp += sprintf(pTmp, "Transport: RTP/AVP/TCP;unicast;"
						"interleaved=%d-%d\r\n\r\n",
						c->pstRtspSess->interleaved[RTP_STREAM_AUDIO].rtp,
						c->pstRtspSess->interleaved[RTP_STREAM_AUDIO].rtcp);
						c->pstRtspSess->setupFlag[1] = 1;
				}
			}
			else if(trackId == TRACK_ID_METADATA)
			{  //metadata
				if( c->pstRtspSess->setupFlag[2] == 0 )
				{
					p = strstr(p, "=");
					if(p != NULL)
					{
						sscanf(p+1, "%d", &c->pstRtspSess->interleaved[RTP_STREAM_METADATA].rtp);
					}

					p = strstr(p, "-");
					if(p != NULL)
					{
						sscanf(p+1, "%d", &c->pstRtspSess->interleaved[RTP_STREAM_METADATA].rtcp);
					}
					else
					{
						c->pstRtspSess->interleaved[RTP_STREAM_METADATA].rtcp =
						c->pstRtspSess->interleaved[RTP_STREAM_METADATA].rtp + 1;
					}

					pTmp += sprintf(pTmp, "Transport: RTP/AVP/TCP;unicast;"
						"interleaved=%d-%d\r\n\r\n",
						c->pstRtspSess->interleaved[RTP_STREAM_METADATA].rtp,
						c->pstRtspSess->interleaved[RTP_STREAM_METADATA].rtcp);
						c->pstRtspSess->setupFlag[1] = 1;
				}
			}
			else
			{
				if(c->pstRtspSess->clientType != enClientTypeEcsion)
				{
					LOG_FUNC(Err, False, "setup chn = %d, unsupported transport\n", chn);
					RTSP_SendReply(461, 1, "Unsupported Transport", c);
					return 461;
				}
			}
		}
	}
	else
	{
		p = strstr(line, "RTP/AVP");
		if(p != NULL)
		{
			LOG_FUNC(Err, False, "setup chn = %d, unsupported transport\n",chn);
			RTSP_SendReply(461, 1, "Unsupported Transport", c);
			return 461;
		}
		else
		{
			LOG_FUNC(Err, False, "setup request malformed \n");
			RTSP_SendReply(400, 1, "Transport have not RTP/AVP or RTP/AVP/TCP", c);
			return 400;
		}
	}

	c->wcurr = c->wbuf;
    c->wbytes = strlen(c->wbuf);

    c->msgcurr = 0;
    c->msgused = 0;
    c->iovused = 0;
    WBUFFER_AddMsghdr(c);

    RTSP_CLEAR_RECVBUF(c);
    CONN_SetState(c, enConnWrite);	
	c->pstRtspSess->sessStat = RTSP_STATE_READY;
	
	return SUCCESS;
}

sint32 RTSP_EventHandlePlay(ST_CONN_INFO *c)
{
	sint8 *pTmp = NULL;

	RTSP_GetRange(c->rbuf, c->pstRtspSess->range);
	
	RTSP_CLEAR_SENDBUF(c);
	pTmp = c->wbuf;
	pTmp += RTSP_GetHead(200, c);

	if(RTP_TRANSPORT_TYPE_TCP == c->pstRtspSess->transportType)
	{
		pTmp += sprintf(pTmp, "Session: %s;timeout=120\r\n\r\n", c->pstRtspSess->sessId);
	}

	c->wcurr = c->wbuf;
    c->wbytes = strlen(c->wbuf);

    c->msgcurr = 0;
    c->msgused = 0;
    c->iovused = 0;
    WBUFFER_AddMsghdr(c);

    RTSP_CLEAR_RECVBUF(c);
    CONN_SetState(c, enConnWrite);	

	RTSP_CreateSender(c);
	c->pstRtspSess->sessStat = RTSP_STATE_PLAY;

	RTSP_SendStreamStart(c);
	
	return SUCCESS;
}

sint32 RTSP_EventHandleTeamdown(ST_CONN_INFO *c)
{
	sint8 *pTmp = NULL;
	
	CONN_CheckStreamStop((ST_CONN_INFO *)c->pstRtspSess->pstDevSession);
	RTSP_FreeSender(c);
		
	RTSP_CLEAR_SENDBUF(c);
	pTmp = c->wbuf;

	pTmp += RTSP_GetHead( RTSP_STATUS_OK, c);
	pTmp += sprintf(pTmp,"Session: %s\r\n", c->pstRtspSess->sessId);
	pTmp += sprintf(pTmp,"Connection: Close\r\n\r\n");

    //usleep(200000);

	c->wcurr = c->wbuf;
	c->wbytes = strlen(c->wbuf);
	
	c->msgcurr = 0;
	c->msgused = 0;
	c->iovused = 0;
	WBUFFER_AddMsghdr(c);
	
	RTSP_CLEAR_RECVBUF(c);
	CONN_SetState(c, enConnWrite);	
	c->pstRtspSess->sessStat = RTSP_STATE_STOP;
	
	return SUCCESS;
}

sint32 RTSP_EventHandle(int event, int stat, ST_CONN_INFO *c)
{
    sint32 ret;
    //printf("rtsp recv:  %s \n", c->rbuf);

    switch(event)
    {
        case RTSP_REQ_METHOD_OPTIONS:
            ret = RTSP_EventHandleOptions(c);
            break;
        case RTSP_REQ_METHOD_DESCRIBE:
            ret = RTSP_EventHandleDescribe(c);
            break;
        case RTSP_REQ_METHOD_SETUP:
            ret = RTSP_EventHandleSetup(c);
            break;
        case RTSP_REQ_METHOD_PLAY:
            ret = RTSP_EventHandlePlay(c);
            break;
		case RTSP_REQ_METHOD_TEARDOWN:
            ret = RTSP_EventHandleTeamdown(c);
            break;
        case RTSP_REQ_METHOD_PAUSE:
			printf("Pause\n");
            //ret = RTSP_EventHandlePause(pSess);
            break;
        case RTSP_REQ_METHOD_SET_PARAM:
			printf("Set param\n");
            //ret = RTSP_EventHandleSetParam(pSess);
            break;
        default:
			printf("Unknow\n");
            //ret = RTSP_EventHandleUnknown(pSess);
            break;
    }
    return ret;
}

sint32 RTSP_SessionProcess(ST_CONN_INFO *c)
{
    assert(c != NULL);
    assert(c->pstRtspSess != NULL);

    sint32 stat, seqNum = 0;
    sint32 opcode;
    sint32 cseq = -1;

	LOG_FUNC(Info, False, "rtsp message = %s\n", c->rbuf);
	
    if(SUCCESS != RTSP_RecvMsgParse(c->rbuf))
    {
        RTSP_CLEAR_RECVBUF(c);
        return FAIL;
    }

    if(RTSP_PARSE_IS_RESP == RTSP_ResponseMsgCheck(&stat, c->rbuf))
    {
        if(seqNum != c->pstRtspSess->u32LastSendSeq + 1)
        {
            LOG_FUNC(Warn, False, "last send sn is %d != resp seq = %d\n",
                     c->pstRtspSess->u32LastSendSeq, seqNum);
        }
        opcode = RTSP_MAKE_RESP_CMD(c->pstRtspSess->s32LastSendReq);
        if(stat > RTSP_BAD_STATUS_BEGIN)
        {
            LOG_FUNC(Warn, False, "response had status = %d. \n", stat);
        }
    }
    else
    {
        opcode = RTSP_GetReq(c->rbuf);
        if(opcode == RTSP_PARSE_INVALID_OPCODE)
        {
            LOG_FUNC(Err, False, "method request was invalid.%s\n", c->rbuf);
            RTSP_CLEAR_RECVBUF(c);
            return FAIL;
        }
        else if(opcode == RTSP_PARSE_INVALID)
        {
            LOG_FUNC(Err, False, "Bad request line encountered."
                     "Expected 4 valid tokens.  Message discarded.%s\n", c->rbuf);
            RTSP_CLEAR_RECVBUF(c);
            return FAIL;
        }

        cseq = RTSP_GetCseq(c->rbuf);
        if(cseq > 0)
        {
            c->pstRtspSess->u32LastRecvSeq = cseq;
        }
        else
        {
            LOG_FUNC(Err, False, "invalid cseq = %d \n", cseq);
        }
        stat = 0;
    }

    return RTSP_EventHandle(opcode, stat, c);
}
