/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *  	replay server 
 *
 *   	http://www.ecsino.com/
 *
 *  	Copyright 2016 Danga Interactive, Inc.  All rights reserved.
 *		File Name 	: 	ringbuf.h
 *  		Authors		:	Vict Ding <dszhazha@163.com>
 * 		Date   		: 	2016/3/30     
 *      
 */

sint32 RingBuffer_FillbufIndex(ST_RING_BUF *ab);
void RingBuffer_WriteLock(ST_RING_BUF *ab, uint32 index);
void RingBuffer_WriteUnlock(ST_RING_BUF *ab, uint32 index);
void RingBuffer_WriteRecord(ST_RING_BUF *ab, uint32 u32Len, time_t wTime, sint32 streamType, uint32 pts);

