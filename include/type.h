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

