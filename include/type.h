/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *  	replay server 
 *
 *   	http://www.ecsino.com/
 *
 *  	Copyright 2016 Danga Interactive, Inc.  All rights reserved.
 *		File Name 	: 	type.h
 *  	Authors		:	Vict Ding <dszhazha@163.com>
 * 		Date   		: 	2016/2/29     
 *      
 */

/*common typedef*/
typedef	unsigned char 	uint8;
typedef char			sint8;
typedef	unsigned short	uint16;
typedef short			sint16;
typedef unsigned int	uint32;
typedef int				sint32;
typedef char			bool;

/** Time relative to server start. Smaller than time_t on 64-bit systems. */
typedef unsigned int rel_time_t;

enum 
{
	LEN_32   = 32,
	LEN_64   = 64,
	LEN_128  = 128,
	LEN_256  = 256,
	LEN_512  = 512,
	LEN_1K	 = 0x400,
	LEN_2K	 = 0x800,
	LEN_4K	 = 0x1000,
	LEN_16K  = 0X4000,
	LEN_32K  = 0x8000,
	LEN_64K  = 0x10000,
	LEN_128K = 0x20000,
	LEN_256K = 0x40000,
	LEN_512K = 0x80000,
	LEN_1M	 = 0x100000,
	LEN_2M   = 0x200000,
	LEN_3M   = 0x300000,
	LEN_4M	 = 0x400000,
	LEN_5M	 = 0x500000,
	LEN_6M   = 0x600000,
	LEN_8M   = 0x800000,
	LEN_10M  = 0xa00000,
	LEN_16M  = 0x1000000,
	LEN_24M	 = 0x1800000,
};

#define FAIL	(-1)
#define	SUCCESS	0

#ifndef TRUE
#define TRUE 	1
#endif

#ifndef FALSE
#define	FALSE 	0
#endif

#ifndef true
#define true 	1
#endif

#ifndef false
#define	false 	0
#endif

#ifndef True
#define True 	1
#endif

#ifndef False
#define	False 	0
#endif

/*name length*/
#define NAME_LEN		128
#define MAX_BASE64_LEN  128


