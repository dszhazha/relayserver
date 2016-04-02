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


void RingBuffer_Init(ST_RING_BUF *ab)
{
	memset(ab, 0x0, sizeof(ST_RING_BUF));

	pthread_mutex_init(&ab->muxWriteLock, NULL); 

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
}

void RingBuffer_Release(ST_RING_BUF *ab)
{
	if(ab->strBuf != NULL)
		free(ab->strBuf);
	ab->strBuf = NULL;

	pthread_mutex_destroy(&ab->muxWriteLock);
}

/*normal write a audio buf*/
void RingBuffer_Fillbuf(ST_RING_BUF *ab, uint32 u32Len, sint8 *strData, time_t wTime)
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

	ab->u32LeftLen -= u32Len;
	ab->u32CurPos += u32Len;
	ab->u32CurIndex ++;
	ab->u32CurIndex %= MAX_INDEX;
	ab->stIndex[u32CurIndex].bIsLock = False;
	
}

/*buffer is full, loop write*/
void RingBuffer_OverlayOldestBuf(ST_RING_BUF *ab, uint32 u32Len, sint8 *strData, time_t wTime)
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
		
		ab->stIndex[u32OldestIndex].offset = 0;
		ab->stIndex[u32OldestIndex].len = 0;
		ab->stIndex[u32OldestIndex].wTime = 0;
		ab->stIndex[u32OldestIndex].frameType = 0;
		ab->stIndex[u32OldestIndex].bIsLock = False;
	}

	RingBuffer_Fillbuf(ab, u32Len, strData, wTime);
	
}

void RingBuffer_RefillBuf(ST_RING_BUF *ab, uint32 u32Len, sint8 *strData, time_t wTime)
{
	ab->u32OldIndex = ab->u32HeadIndex;
	ab->u32HeadIndex = ab->u32CurIndex;
	ab->u32LeftLen = 0;
	ab->u32CurPos = 0;

	RingBuffer_OverlayOldestBuf(ab, u32Len, strData, wTime);
}

void RingBuffer_Write(ST_RING_BUF *ab, uint32 u32Len, sint8 *strData, time_t wTime)
{
	if(ab->bIsFull == True)
	{
		if(ab->u32CurPos + u32Len <= ab->u32MaxLen)
		{
			RingBuffer_OverlayOldestBuf(ab, u32Len, strData, wTime);
		}
		else
		{
			RingBuffer_RefillBuf(ab, u32Len, strData, wTime);
		}
	}
	else
	{
		if(ab->u32CurPos + u32Len <= ab->u32MaxLen)
		{
			RingBuffer_Fillbuf(ab, u32Len, strData, wTime);
		}
		else
		{
			RingBuffer_RefillBuf(ab, u32Len, strData, wTime);
		}
	}
}

sint32 RingBuffer_GetOnePacket(ST_RING_BUF *ab, uint32 u32index, sint8 **s8pkt)
{
	sint32 len = 0;

	if(ab->stIndex[u32index].bIsLock == True)
	{
		*s8pkt = ab->strBuf + ab->stIndex[u32index].offset;
		len = ab->stIndex[u32index].len;
	}

	return len;
}