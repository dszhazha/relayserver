/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *  	replay server 
 *
 *   	http://www.ecsino.com/
 *
 *  	Copyright 2016 Danga Interactive, Inc.  All rights reserved.
 *		File Name 	: 	log.h
 *  	Authors		:	Vict Ding <dszhazha@163.com>
 * 		Date   		: 	2016/2/29     
 *      
 */
#include <syslog.h>

#define 	LOG_URL_LENGTH		128

typedef enum logLevel
{
	None = 0,
	Err,
	Warn,
	Info,
	Debug
}EN_LOG_LEVEL;

typedef struct stLogConf
{
	sint32 	s32LogLevel;
	sint32 	s32Level;
	sint32 	s32SetLevel;
	sint8	strSrvUrl[LOG_URL_LENGTH];
}ST_LOG_CONF;

#ifndef OPEN_SYSLOG
#define LOG_FUNC(levels, isperror, fmt...) { \
	if(levels <= Debug || levels > None) { \
		if(levels == Err){ \
			printf("ERR: ");	\
		}else if(levels == Warn){	\
			printf("WARN: ");	\
		}else if (levels == Info){ \
			printf("INFO: "); \
		}else if (levels == Debug){ \
			printf("DBG: "); \
		} \
		printf("[File: %-10s, Line: %05d, Func: %-20s]\n", __FILE__, \
				__LINE__, __FUNCTION__); \
		printf(fmt); \
		printf("\n"); \
		if(isperror == True){ perror("Perror: ");} \
	} \
}
#else
extern ST_LOG_CONF gstLogConf;
#define LOG_FUNC(levels, isperror, fmt, args...) { \
	if(levels <= gstLogConf.s32SetLevel) { \
		if(levels == Err){ \
			gstLogConf.s32LogLevel = LOG_ERR; \
		}else if(levels == Warn){	\
			gstLogConf.s32LogLevel = LOG_WARNING; \
		}else if (levels == Info){ \
			gstLogConf.s32LogLevel = LOG_INFO; \
		}else if (levels == Debug){ \
			gstLogConf.s32LogLevel = LOG_DEBUG; \
		} \
		if(gstLogConf.level > None && gstLogConf.level <= Debug){ \
			if(isperror == True){ \
				syslog(gstLogConf.s32LogLevel, "PERROR[%s] : " fmt, strerror(errno), ## args); \
			}else{\
				syslog(gstLogConf.s32LogLevel, fmt, ## args); }\
		} \
	} \
}
#endif 

sint32 LOG_Init(void);
sint32 LOG_UpdataConf(void);
