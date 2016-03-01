/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *  	replay server 
 *
 *   	http://www.ecsino.com/
 *
 *  	Copyright 2016 Danga Interactive, Inc.  All rights reserved.
 *		File Name 	: 	log.c
 *  	Authors		:	Vict Ding <dszhazha@163.com>
 * 		Date   		: 	2016/2/29     
 *      
 */
#include "type.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>

ST_LOG_CONF gstLogConf;

static sint32 LOG_ReadConf(void)
{
	gstLogConf.s32Level = 0;
	return SUCCESS;
}

sint32 LOG_Init(void)
{
	sint32 s32Ret = 0;

	s32Ret = LOG_ReadConf();
	if(s32Ret != SUCCESS)
	{
		gstLogConf.s32Level = 0;
	}

	switch(gstLogConf.s32Level)
	{
		case enLvlErr:
			setlogmask (LOG_UPTO (LOG_ERR));
			gstLogConf.s32SetLevel = enLvlErr;
			break;
		case enLvlWarn:
			setlogmask (LOG_UPTO (LOG_WARNING));
			gstLogConf.s32SetLevel = enLvlWarn;
			break;		
		case enLvlInfo:
			setlogmask (LOG_UPTO (LOG_INFO));
			gstLogConf.s32SetLevel = enLvlInfo;
			break;		
		case enLvlDebug:
			setlogmask (LOG_UPTO (LOG_DEBUG));
			gstLogConf.s32SetLevel = enLvlDebug;
			break;	
		default:
			return SUCCESS;
	}

	openlog ("ReplayServer",  LOG_NDELAY | LOG_PERROR  , LOG_LOCAL1);
	return SUCCESS;
}

static void LOG_Close(void)
{
	closelog();
}

sint32 LOG_UpdataConf(void)
{
	sint32 s32Ret = 0;
	
	LOG_Close();
	memset(&gstLogConf, 0x0, sizeof(ST_LOG_CONF));

	s32Ret = LOG_ReadConf();
	if(s32Ret != SUCCESS)
	{
		gstLogConf.s32Level = 0;
	}

	switch(gstLogConf.s32Level)
	{
		case enLvlErr:
			setlogmask (LOG_UPTO (LOG_ERR));
			gstLogConf.s32SetLevel = enLvlErr;
			break;
		case enLvlWarn:
			setlogmask (LOG_UPTO (LOG_WARNING));
			gstLogConf.s32SetLevel = enLvlWarn;
			break;		
		case enLvlInfo:
			setlogmask (LOG_UPTO (LOG_INFO));
			gstLogConf.s32SetLevel = enLvlInfo;
			break;		
		case enLvlDebug:
			setlogmask (LOG_UPTO (LOG_DEBUG));
			gstLogConf.s32SetLevel = enLvlDebug;
			break;	
		default:
			return SUCCESS;
	}

	openlog ("ReplayServer",  LOG_NDELAY | LOG_PERROR  , LOG_LOCAL1);
	return SUCCESS;
}