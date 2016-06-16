/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *  	replay server 
 *
 *   	http://www.ecsino.com/
 *
 *  	Copyright 2016 Danga Interactive, Inc.  All rights reserved.
 *		File Name 	: 	ringbuf.c
 *  		Authors		:	Vict Ding <dszhazha@163.com>
 * 		Date   		: 	2016/3/30     
 *      
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#include "server.h"

#define		BUF_TIMEOUT						3 	/* 4秒超时 */


void RingBuffer_Init(ST_RING_BUF *ab)
{
	pthread_condattr_t	condattr; 
	pthread_mutexattr_t	mutexattr; 
	memset(ab, 0x0, sizeof(ST_RING_BUF));

	//pthread_mutex_init(&ab->muxLock, NULL); 
	pthread_mutex_init(&ab->muxLock, &mutexattr); 
	pthread_cond_init(&ab->condRW, &condattr);

	ab->strBuf = (sint8 *)malloc(LEN_512K);
	if(ab->strBuf == NULL)
	{
		LOG_FUNC(Err, False, "audio buffer malloc error\n");
		exit(-2);
	}

	ab->bIsFull 	= False;
	ab->u32MaxLen 	= LEN_512K;
	ab->u32LeftLen 	= LEN_512K;	
	ab->u32HeadIndex = 0;
	ab->u32CurIndex = 0;
	ab->u32OldIndex	= 0;
	ab->u32CurPos = 0;
	ab->u32CurPlayIndex = 0;
	ab->bHaveNoDataflag = 0;
	
}

void RingBuffer_Release(ST_RING_BUF *ab)
{
	if(ab->strBuf != NULL)
		free(ab->strBuf);
	ab->strBuf = NULL;

	pthread_mutex_destroy(&ab->muxLock);
	pthread_cond_destroy(&ab->condRW);
}

/*normal write a audio buf*/
void RingBuffer_Fillbuf(ST_RING_BUF *ab, uint32 u32Len, sint8 *strData, time_t wTime, sint32 streamType, uint32 pts)
{
	uint32 	u32CurPos;
	uint32	u32CurIndex;

	u32CurIndex = ab->u32CurIndex;
	u32CurPos = ab->u32CurPos;

	ab->stIndex[u32CurIndex].bIsLock = True;
	memcpy(ab->strBuf + u32CurPos, strData, u32Len);
	ab->stIndex[u32CurIndex].len 		= u32Len;
	ab->stIndex[u32CurIndex].offset 	=  u32CurPos;
	ab->stIndex[u32CurIndex].wTime 		=  wTime;
	ab->stIndex[u32CurIndex].frameType  = streamType;
	ab->stIndex[u32CurIndex].pts  		= pts;
	ab->u32LeftLen -= u32Len;
	ab->u32CurPos += u32Len;
	ab->u32CurIndex ++;
	ab->u32CurIndex %= MAX_INDEX;
	ab->u32NewIndex = u32CurIndex;
	ab->stIndex[u32CurIndex].bIsLock = False;
	
}

/*buffer is full, loop write*/
void RingBuffer_OverlayOldestBuf(ST_RING_BUF *ab, uint32 u32Len, sint8 *strData, time_t wTime, sint32 streamType, uint32 pts)
{
	uint32 	u32OldestIndex;

	while(ab->u32LeftLen < u32Len)
	{
		u32OldestIndex = ab->u32OldIndex;
		ab->stIndex[u32OldestIndex].bIsLock = True;
		ab->u32OldIndex++;
		ab->u32OldIndex %= MAX_INDEX;
		
		if(ab->u32HeadIndex == ab->u32OldIndex) //this frame is last frame
			ab->u32LeftLen += (ab->u32MaxLen - ab->stIndex[u32OldestIndex].offset);
		else
			ab->u32LeftLen += ab->stIndex[u32OldestIndex].len;
		
		ab->stIndex[u32OldestIndex].offset 	= 0;
		ab->stIndex[u32OldestIndex].len 	= 0;
		ab->stIndex[u32OldestIndex].wTime 	= 0;
		ab->stIndex[u32OldestIndex].frameType = 0;
		ab->stIndex[u32OldestIndex].pts 	= 0;
		ab->stIndex[u32OldestIndex].bIsLock = False;
	}

	RingBuffer_Fillbuf(ab, u32Len, strData, wTime, streamType, pts);
	
}

void RingBuffer_RefillBuf(ST_RING_BUF *ab, uint32 u32Len, sint8 *strData, time_t wTime, sint32 streamType, uint32 pts)
{
	ab->u32OldIndex = ab->u32HeadIndex;
	ab->u32HeadIndex = ab->u32CurIndex;
	ab->u32LeftLen = 0;
	ab->u32CurPos = 0;

	RingBuffer_OverlayOldestBuf(ab, u32Len, strData, wTime, streamType, pts);
}

void RingBuffer_Write(ST_RING_BUF *ab, uint32 u32Len, sint8 *strData, time_t wTime, sint32 streamType, uint32 pts)
{
	if(ab->bIsFull == True)
	{
		if(ab->u32CurPos + u32Len <= ab->u32MaxLen)
		{
			RingBuffer_OverlayOldestBuf(ab, u32Len, strData, wTime, streamType, pts);
		}
		else
		{
			RingBuffer_RefillBuf(ab, u32Len, strData, wTime, streamType, pts);
		}
	}
	else
	{
		if(ab->u32CurPos + u32Len <= ab->u32MaxLen)
		{
			RingBuffer_Fillbuf(ab, u32Len, strData, wTime, streamType, pts);
		}
		else
		{
			RingBuffer_RefillBuf(ab, u32Len, strData, wTime, streamType, pts);
			ab->bIsFull = True;
		}
	}

	if(ab->bHaveNoDataflag == 1)
	{
		pthread_mutex_lock(&ab->muxLock);
		ab->bHaveNoDataflag = 0;
		pthread_cond_broadcast(&ab->condRW);
		pthread_mutex_unlock(&ab->muxLock);
	}
}

sint8* RingBuffer_FillbufPtr(ST_RING_BUF *ab, uint32 u32Len)
{
	if(ab->bIsFull == True)
	{
		if(ab->u32CurPos + u32Len <= ab->u32MaxLen)
		{
			/*fill oldest buffer*/
			uint32 	u32OldestIndex;
			while(ab->u32LeftLen < u32Len)
			{
				u32OldestIndex = ab->u32OldIndex;
				ab->stIndex[u32OldestIndex].bIsLock = True;
				ab->u32OldIndex++;
				ab->u32OldIndex %= MAX_INDEX;
				
				if(ab->u32HeadIndex == ab->u32OldIndex) //this frame is last frame
					ab->u32LeftLen += (ab->u32MaxLen - ab->stIndex[u32OldestIndex].offset);
				else
					ab->u32LeftLen += ab->stIndex[u32OldestIndex].len;
				ab->stIndex[u32OldestIndex].offset 	= 0;
				ab->stIndex[u32OldestIndex].len 	= 0;
				ab->stIndex[u32OldestIndex].wTime 	= 0;
				ab->stIndex[u32OldestIndex].frameType = 0;
				ab->stIndex[u32OldestIndex].pts 	= 0;
				ab->stIndex[u32OldestIndex].bIsLock = False;
			}
			return ab->strBuf + ab->u32CurPos;
		}
		else
		{
			/*refill buffer*/
			ab->u32OldIndex = ab->u32HeadIndex;
			ab->u32HeadIndex = ab->u32CurIndex;
			ab->u32LeftLen = 0;
			ab->u32CurPos = 0;
			
			/*fill oldest buffer*/
			uint32 	u32OldestIndex;
			while(ab->u32LeftLen < u32Len)
			{
				u32OldestIndex = ab->u32OldIndex;
				ab->stIndex[u32OldestIndex].bIsLock = True;
				ab->u32OldIndex++;
				ab->u32OldIndex %= MAX_INDEX;
				
				if(ab->u32HeadIndex == ab->u32OldIndex) //this frame is last frame
					ab->u32LeftLen += (ab->u32MaxLen - ab->stIndex[u32OldestIndex].offset);
				else
					ab->u32LeftLen += ab->stIndex[u32OldestIndex].len;
				ab->stIndex[u32OldestIndex].offset 	= 0;
				ab->stIndex[u32OldestIndex].len 	= 0;
				ab->stIndex[u32OldestIndex].wTime 	= 0;
				ab->stIndex[u32OldestIndex].frameType = 0;
				ab->stIndex[u32OldestIndex].pts 	= 0;
				ab->stIndex[u32OldestIndex].bIsLock = False;
			}
			return ab->strBuf + ab->u32CurPos;
		}
	}
	else
	{
		if(ab->u32CurPos + u32Len <= ab->u32MaxLen)
		{
			return ab->strBuf + ab->u32CurPos;
		}
		else
		{
			/*refill buffer*/
			ab->u32OldIndex = ab->u32HeadIndex;
			ab->u32HeadIndex = ab->u32CurIndex;
			ab->u32LeftLen = 0;
			ab->u32CurPos = 0;
			
			/*fill oldest buffer*/
			uint32 	u32OldestIndex;
			while(ab->u32LeftLen < u32Len)
			{
				u32OldestIndex = ab->u32OldIndex;
				ab->stIndex[u32OldestIndex].bIsLock = True;
				ab->u32OldIndex++;
				ab->u32OldIndex %= MAX_INDEX;
				
				if(ab->u32HeadIndex == ab->u32OldIndex) //this frame is last frame
					ab->u32LeftLen += (ab->u32MaxLen - ab->stIndex[u32OldestIndex].offset);
				else
					ab->u32LeftLen += ab->stIndex[u32OldestIndex].len;
				ab->stIndex[u32OldestIndex].offset 	= 0;
				ab->stIndex[u32OldestIndex].len 	= 0;
				ab->stIndex[u32OldestIndex].wTime 	= 0;
				ab->stIndex[u32OldestIndex].frameType = 0;
				ab->stIndex[u32OldestIndex].pts 	= 0;
				ab->stIndex[u32OldestIndex].bIsLock = False;
			}
			ab->bIsFull = True;
			return ab->strBuf + ab->u32CurPos;
		}
	}

}

sint32 RingBuffer_FillbufIndex(ST_RING_BUF *ab)
{
	return ab->u32CurIndex;
}

void RingBuffer_WriteLock(ST_RING_BUF *ab, uint32 index)
{
	ab->stIndex[index].bIsLock = True;
}

void RingBuffer_WriteUnlock(ST_RING_BUF *ab, uint32 index)
{
	ab->stIndex[index].bIsLock = False;
}

void RingBuffer_WriteRecord(ST_RING_BUF *ab, uint32 u32Len, time_t wTime, sint32 streamType, uint32 pts)
{
	uint32 	u32CurPos;
	uint32	u32CurIndex;

	u32CurIndex = ab->u32CurIndex;
	u32CurPos = ab->u32CurPos;

	//ab->stIndex[u32CurIndex].bIsLock = True;
	//memcpy(ab->strBuf + u32CurPos, strData, u32Len);
	ab->stIndex[u32CurIndex].len 		= u32Len;
	ab->stIndex[u32CurIndex].offset 	=  u32CurPos;
	ab->stIndex[u32CurIndex].wTime 		=  wTime;
	ab->stIndex[u32CurIndex].frameType  = streamType;
	ab->stIndex[u32CurIndex].pts  		= pts;
	ab->u32LeftLen -= u32Len;
	ab->u32CurPos += u32Len;
	ab->u32CurIndex ++;
	ab->u32CurIndex %= MAX_INDEX;
	ab->u32NewIndex = u32CurIndex;
	//ab->stIndex[u32CurIndex].bIsLock = False;
}

sint32 RINGBUF_GetNewIndex(ST_RING_BUF *ab)
{
	sint32 index ; 
	index = ab->u32NewIndex;
	return index;
}

sint32 RINGBUF_GetOnePacket(ST_RING_BUF *ab, uint32 u32index, sint8 **s8pkt)
{
	sint32 len = 0;

	if(ab->stIndex[u32index].bIsLock == False)
	{
		*s8pkt = ab->strBuf + ab->stIndex[u32index].offset;
		len = ab->stIndex[u32index].len;
	}

	return len;
}

sint32 RINGBUF_GetCurPacketLen(ST_RING_BUF *ab, uint32 u32index)
{
	sint32 len = 0;

	if(ab->stIndex[u32index].bIsLock == False)
	{
		len = ab->stIndex[u32index].len;
	}

	return len;
}

sint32 RINGBUF_GetCurNalType(ST_RING_BUF *ab, uint32 index)
{
	sint32 frameType;
	frameType = ab->stIndex[index].frameType;
	return frameType;
}

sint32 RINGBUF_GetCurPktPts(ST_RING_BUF *ab, uint32 index)
{
	uint32 pts;
	pts = ab->stIndex[index].pts;
	return pts;
}

/* 
 * sendIndex -- 当前发送的索引
 * newIndex  -- 最新的可发送索引 
 * 返回 True 表示超时, 否则返回 False
 */
sint32 RINGBUF_CheckTimeout(ST_RING_BUF *ab, uint32 sendIndex, uint32 newIndex)
{
	/* 若当前发送的包比最新的包还慢 BUF_TIMEOUT 秒,则超时 */
	if(ab->stIndex[sendIndex].wTime + BUF_TIMEOUT <= 
					ab->stIndex[newIndex].wTime)
	{
		return True;
	}
	return False;
}

void RINGBUF_IndexCount(ST_RING_BUF *ab, uint32 *index)
{
	sint32 count = *index;
	//sint32 statu ;

	count ++;
	count %= MAX_INDEX;

	while(count == ab->u32CurIndex)
	{
		//statu = CFG_ThrIsExit();
		//if( statu == True || ID_STOP == CFG_GetThrStat()){
			//break; 
		//}
		pthread_mutex_lock(&ab->muxLock);
		ab->bHaveNoDataflag = 1;
		pthread_cond_wait(&ab->condRW, &ab->muxLock);
		pthread_mutex_unlock(&ab->muxLock);
	}
	*index = count;
}

