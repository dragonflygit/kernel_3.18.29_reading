/*********************************************************************************************
* (C) 2017 RippleTek .CO. All right resovled
*
* Description:  Kernel module to match a http get package, and those belong to the same stream.
*			   This module matches the HTTP-GET package by string search of the package's payload.
*               It works mostly like [ipt_string], but you can specify a config file to filter ou
*			   some http package by hostname or url.
*
*			   While matching the stream, kproxym works based on [ipt_KPROXYT], which recordered
*			   all the matched stream in a hash table. kproxym search the table with the @skb's
*               5-tuple to discriminate the @skb is or not belong to a matched stream.
*
* Author:       TanLiyong <tanliyong@rippletek.com>
*
* Date:         2017-02-20
***********************************************************************************************/

#ifndef __IPT_KPORXYM_H__
#define __IPT_KPORXYM_H__

#define MAX_HOSTNAME_LEN 64
#define MAX_URL_TYPE_LEN 8


typedef struct kproxy_match_para_S {
	int index; // reserved,
    char confpath[128];
} kproxym_para_t;

#endif
