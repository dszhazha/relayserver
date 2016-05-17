/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *  	replay server 
 *
 *   	http://www.ecsino.com/
 *
 *  	Copyright 2016 Danga Interactive, Inc.  All rights reserved.
 *		File Name 	: 	rtp.c
 *  		Authors		:	Vict Ding <dszhazha@163.com>
 * 		Date   		: 	2016/3/30     
 *      
**/

#define 	DEFAULT_RTP_UDP_PORT		32767
#define 	RTP_DEFAULT_SSRC 			41030
#define 	RTP_MAX_SENDER				16
#define 	RTP_VERSION					2

#define RTP_HDR_SET_VERSION(pHDR, val)  ((pHDR)->version = val)
#define RTP_HDR_SET_P(pHDR, val)        ((pHDR)->p       = val)
#define RTP_HDR_SET_X(pHDR, val)        ((pHDR)->x       = val)
#define RTP_HDR_SET_CC(pHDR, val)       ((pHDR)->cc      = val)
 
#define RTP_HDR_SET_M(pHDR, val)        ((pHDR)->marker  = val)
#define RTP_HDR_SET_PT(pHDR, val)       ((pHDR)->pt      = val)
 
#define RTP_HDR_SET_SEQNO(pHDR, _sn)    ((pHDR)->seqno  = (_sn))
#define RTP_HDR_SET_TS(pHDR, _ts)       ((pHDR)->ts     = (_ts))
 
#define RTP_HDR_SET_SSRC(pHDR, _ssrc)    ((pHDR)->ssrc  = _ssrc)
 
#define RTP_HDR_LEN  					sizeof(RtpHdr_t)

#define H264_Get_NalType(c)  ( (c) & 0x1F )

typedef enum RtpPt_
{
    RTP_PT_ULAW             = 0,        /* mu-law */
    RTP_PT_GSM              = 3,        /* GSM */
    RTP_PT_G723             = 4,        /* G.723 */
    RTP_PT_ALAW             = 8,        /* a-law */
    RTP_PT_G722             = 9,        /* G.722 */
    RTP_PT_S16BE_STEREO     = 10,       /* linear 16, 44.1khz, 2 channel */
    RTP_PT_S16BE_MONO       = 11,       /* linear 16, 44.1khz, 1 channel */
    RTP_PT_MPEGAUDIO        = 14,       /* mpeg audio */
    RTP_PT_JPEG             = 26,       /* jpeg */
    RTP_PT_H261             = 31,       /* h.261 */
    RTP_PT_MPEGVIDEO        = 32,       /* mpeg video */
    RTP_PT_MPEG2TS          = 33,       /* mpeg2 TS stream */
    RTP_PT_H263             = 34,       /* old H263 encapsulation */
    //RTP_PT_PRIVATE          	= 96,
    RTP_PT_H264             = 96,       /* hisilicon define as h.264 */
    RTP_PT_G726             = 97,       /* hisilicon define as G.726 */
    RTP_PT_ADPCM            = 98,       /* hisilicon define as ADPCM */
    RTP_PT_MPEG4		= 99,	/*xsf define as Mpeg4*/
    RTP_PT_METADATA		    = 107,	/*xsf define as Mpeg4*/
    RTP_PT_AAC            = 108,       /* kjchen define as AAC */
    RTP_PT_INVALID          = 127
}EN_RTP_PT;

#ifndef BYTE_ORDER
#define BYTE_ORDER LITTLE_ENDIAN
#endif

/* total 12Bytes */
typedef struct RtpHdr_s
{

#if (BYTE_ORDER == LITTLE_ENDIAN)
    /* byte 0 */
    uint16 cc      :4;   /* CSRC count */
    uint16 x       :1;   /* header extension flag */
    uint16 p       :1;   /* padding flag */
    uint16 version :2;   /* protocol version */

    /* byte 1 */
    uint16 pt      :7;   /* payload type */
    uint16 marker  :1;   /* marker bit */
#elif (BYTE_ORDER == BIG_ENDIAN)
    /* byte 0 */
    uint16 version :2;   /* protocol version */
    uint16 p       :1;   /* padding flag */
    uint16 x       :1;   /* header extension flag */
    uint16 cc      :4;   /* CSRC count */
    /*byte 1*/
    uint16 marker  :1;   /* marker bit */
    uint16 pt      :7;   /* payload type */
#else
    #error YOU MUST DEFINE BYTE_ORDER == LITTLE_ENDIAN OR BIG_ENDIAN !
#endif


    /* bytes 2, 3 */
    uint16 seqno  :16;   /* sequence number */

    /* bytes 4-7 */
    sint32 ts;            /* timestamp in ms */

    /* bytes 8-11 */
    sint32 ssrc;          /* synchronization source */
} RtpHdr_t;

