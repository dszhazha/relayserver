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

#include "server.h"
#include "rtsp.h"
#include "md5.h"

/*extern function*/
extern int WBUFFER_AddMsghdr(ST_CONN_INFO *c);
extern void CONN_SetState(ST_CONN_INFO *pstConnInfo, EN_CONN_STAT enState);
extern ST_CONN_INFO *CONN_CheckMapConnect(sint8 *name, sint8 *passwd);

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
    pTmp += sprintf(pTmp,"CSeq: %d\r\n", c->pRtspSess->u32LastRecvSeq);
    pTmp += sprintf(pTmp,"Server: "SRV_NAME" Rtsp Server "SRV_VER"\r\n");

    return (strlen(c->wbuf));
}

void RTSP_GetMulticastPara(ST_MULTICAST_PARA *pmp)
{
    memcpy(pmp->multicast_ip, MULTICAST_IP, 9);
    pmp->multicast_port = MULTICAST_PORT;
    pmp->multicast_ttl = MULTICAST_TTL;
}

sint32 RTSP_SendReply(sint32 err, sint32 simple, char *addon, ST_CONN_INFO *c)
{
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
		LOG_FUNC(Err, False, "Parse User error\n");
		return FAIL;
	}
	p+=1;
	q = strchr(p, ' ');
	memcpy(name, p, q-p);

	p = strstr(buff, RTSP_PWD);
	if(p == NULL)
	{
		LOG_FUNC(Err, False, "Parse Pwd error\n");
		return FAIL;
	}
	p+=1;
	q = strchr(p, ' ');
	memcpy(passwd, p, q-p);

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
	sint32  port;
	sint8 	server[128]={0};
    sint8   url[256]= {0};
	sint8 	szChn[16]={0};
	sint8 	user[NAME_LEN]={0};
	sint8 	pwd[NAME_LEN]={0};
	
	sint8 	*pTmp = NULL;
	sint8 	*p = NULL;
	
    MD5_CTX     md5p;
    ST_MULTICAST_PARA   stMulticastPara;
    memset(&stMulticastPara, 0x0, sizeof(ST_MULTICAST_PARA));
    memset(&md5p, 0x0, sizeof(MD5_CTX));

    RTSP_GetMulticastPara(&stMulticastPara);

    if(!sscanf(c->rbuf, " %*s %254s ", url))
    {
        RTSP_SendReply(RTSP_STATUS_BAD_REQUEST, 1, NULL, c);
        LOG_FUNC(Err, False, "sscanf url error \n");
        return FAIL;
    }

	if(SUCCESS != RTSP_ParseUrl(&port, server, szChn, url))
	{
		LOG_FUNC(Err, False, "url is not found !\n");
		RTSP_SendReply(RTSP_STATUS_BAD_REQUEST, 1, NULL, c);
		return FAIL;
	}

	if(NULL != strstr(c->rbuf, RTSP_HDR_ACCEPT))
	{
		if(NULL == strstr(c->rbuf, RTSP_SDP))
		{
			LOG_FUNC(Err, False, "only accept require. \n");
			RTSP_SendReply(551, 1, NULL, c);
			return FAIL;
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
			strncpy(c->pRtspSess->userAgent, pTmp, p - pTmp);
		}
	}

	pTmp = strstr(c->rbuf, "Ecsino");
	if(pTmp)
	{
		c->pRtspSess->clientType = enClientTypeEcsion;
	}
	else
	{
		c->pRtspSess->clientType = enClientTypeUndefined;
	}

	if(SUCCESS != RTSP_PraseUserPwd(c->rbuf, user, pwd))
	{
		LOG_FUNC(Err, False, "url is not found !\n");
		RTSP_SendReply(RTSP_STATUS_BAD_REQUEST, 1, NULL, c);
		return FAIL;
	}
	
	c->pRtspSess->pstDevSession = CONN_CheckMapConnect(user, pwd);
	if(c->pRtspSess->pstDevSession == NULL)
	{
		LOG_FUNC(Err, False, "device name found !\n");
		RTSP_SendReply(RTSP_STATUS_BAD_REQUEST, 1, NULL, c);
		return FAIL;
	}

	ST_CONN_INFO *pds = c->pRtspSess->pstDevSession;
	RTSP_CLEAR_SENDBUF(c);
	pTmp = c->wbuf;
	pTmp += sprintf(pTmp, "%s %d %s\r\n", RTSP_VERSION, 200, RTSP_GetMethodDescrib(200));
	pTmp += sprintf(pTmp, "Cseq: %d \r\n", c->pRtspSess->u32LastRecvSeq);

	/*width * heigth * rate * resolution */
	pTmp += sprintf(pTmp, 
				"Server: Ecsino Rtsp Server V200R001 "
				"%d*%d*%d*%d\r\n", 
				pds->pstDevInfo->u32VideoWidth,
				pds->pstDevInfo->u32VideoHeigth,
				pds->pstDevInfo->u32VideoFps,
				pds->pstDevInfo->u32VideoBit);

	pTmp += sprintf(pTmp, "Content-Type: application/sdp\r\n");
	
	
}

sint32 RTSP_EventHandle(int event, int stat, ST_CONN_INFO *c)
{
    sint32 ret;
    printf("rtsp recv:  %s \n", c->rbuf);

    switch(event)
    {
        case RTSP_REQ_METHOD_OPTIONS:
            ret = RTSP_EventHandleOptions(c);
            break;

        case RTSP_REQ_METHOD_DESCRIBE:
            ret = RTSP_EventHandleDescribe(c);
            break;

        case RTSP_REQ_METHOD_TEARDOWN:
            //ret = RTSP_EventHandleTeamdown(pSess);
            break;

        case RTSP_REQ_METHOD_SETUP:
            //ret = RTSP_EventHandleSetup(pSess);
            break;

        case RTSP_REQ_METHOD_PLAY:
            //memset(pSess->range,0,sizeof(pSess->range));
            //ret = RTSP_GetRange(pSess->recvBuf,pSess->range);
            //ret = RTSP_EventHandlePlay(pSess);
            break;

        case RTSP_REQ_METHOD_PAUSE:
            //ret = RTSP_EventHandlePause(pSess);
            break;

        case RTSP_REQ_METHOD_SET_PARAM:
            //ret = RTSP_EventHandleSetParam(pSess);
            break;

        default:
            //ret = RTSP_EventHandleUnknown(pSess);
            break;
    }
    return ret;
}

sint32 RTSP_SessionProcess(ST_CONN_INFO *c)
{
    assert(c != NULL);
    assert(c->pRtspSess != NULL);

    sint32 stat, seqNum = 0;
    sint32 opcode;
    sint32 cseq = -1;

    if(SUCCESS != RTSP_RecvMsgParse(c->rbuf))
    {
        RTSP_CLEAR_RECVBUF(c);
        return SUCCESS;
    }

    if(RTSP_PARSE_IS_RESP == RTSP_ResponseMsgCheck(&stat, c->rbuf))
    {
        if(seqNum != c->pRtspSess->u32LastSendSeq + 1)
        {
            LOG_FUNC(Warn, False, "last send sn is %d != resp seq = %d\n",
                     c->pRtspSess->u32LastSendSeq, seqNum);
        }
        opcode = RTSP_MAKE_RESP_CMD(c->pRtspSess->s32LastSendReq);
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
            c->pRtspSess->u32LastRecvSeq = cseq;
        }
        else
        {
            LOG_FUNC(Err, False, "invalid cseq = %d \n", cseq);
        }
        stat = 0;
    }

    return RTSP_EventHandle(opcode, stat, c);
}


