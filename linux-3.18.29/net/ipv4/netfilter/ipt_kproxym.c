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
#include <linux/in.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/gfp.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_string.h>
#include <linux/textsearch.h>
#include <linux/jiffies.h>

#include <linux/netfilter_ipv4/ipt_ah.h>
#include <linux/netfilter/x_tables.h>

#include <linux/netfilter_ipv4/ipt_kproxym.h>
#include <linux/list.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("TanLiyong <tanliyong@rippletek.com>");
MODULE_DESCRIPTION("Xtables: IPV4 http GET pkg match, and those belong to the same stream");

static unsigned int g_allin = 0;
static unsigned int g_filter_minlen = 0;
static unsigned int g_filter_get = 0;
static unsigned int g_filter_url = 0;
static unsigned int g_filter_accept = 0;
static unsigned int g_filter_host = 0;
static unsigned int g_filter_burst = 0;
static unsigned int g_total_matched = 0;
static unsigned long g_last_time = 0;

/**
strlen("
GET / HTTP/1.1\r\n
HOST: ...\r\n
User-Agent: \r\n
Accept: ...\r\n
")
*/
#define HTTP_MIN_SIZE 54

#define IGNORE_MARK_MASK 0x00760000

#define DEBUG 0

#if (DEBUG == 1)
#define dprintk(fmt, arg...)  printk(fmt, ##arg)
#else
#define dprintk(fmt, ...)
#endif


typedef struct conf_rcd_S {
    struct list_head list;
    int rcdlen;
    char rcddata[0];
} conf_rcd_t;


typedef struct conf_management_S {
    struct list_head hosts;
    struct list_head exts;
} conf_mng_t;


typedef enum {
    LOOKUP,
    FILL,
    DONE
} conf_fsmste_t;

static conf_mng_t *g_pconf = NULL;

static void log_statistics(void)
{
    if (unlikely(0 == g_last_time))
    {
        g_last_time = jiffies;
        return;
    }

    if (unlikely(jiffies - g_last_time >= 1800 * HZ))
    {
        printk(KERN_INFO "[kproxym] statistic [%u,%u,%u,%u,%u,%u,%u,%u]\n",
               g_allin, g_filter_minlen, g_filter_get, g_filter_url, g_filter_accept, g_filter_host,  g_filter_burst, g_total_matched);
        g_last_time = jiffies;
    }
}

int sgets(char *dst, int dst_len, char *src, int src_len)
{
    int i = 0;

    while (src_len > 0)
    {
        char c = *src;
        if (i < dst_len)
        {
            dst[i++] = c;
        }
        if (c == '\n')
        {
            break;
        }
        src++;
        src_len--;
    }
    return i;
}

void parse_list(struct list_head *plist, const char *tag, int rec_len, char *buf, int len)
{
    int taglen = strlen(tag);
    char tmp[128];
    conf_fsmste_t state = LOOKUP;
    int cur = 0;
    int linelen;
    conf_rcd_t *rec;

    if (taglen > 128)
    {
        return;
    }

    while (1)
    {
        switch (state)
        {
        case LOOKUP:
            linelen = sgets(tmp, 128, buf + cur, len - cur);
            if (linelen == 0)
            {
                state = DONE;
                break;
            }
            if (strncmp(tag, tmp, taglen) == 0)
            {
                state = FILL;
            }
            cur += linelen;
            break;
        case FILL:
            linelen = sgets(tmp, 128, buf + cur, len - cur);
            if (linelen == 0 || (linelen == 1 && tmp[0] == '\n') || linelen > rec_len)
            {
                state = DONE;
                break;
            }
            rec = kmalloc(sizeof(conf_rcd_t) + linelen + 1, GFP_KERNEL);
            if (rec == NULL)
            {
                return;
            }

            memcpy(rec->rcddata, tmp, linelen);

            if (tmp[linelen - 1] == '\n')
            {
                rec->rcddata[linelen - 1] = 0;
                rec->rcdlen = linelen - 1;
            }
            else
            {
                rec->rcddata[linelen] = 0;
                rec->rcdlen = linelen;
            }

            list_add(&rec->list, plist);
            cur += linelen;
            break;
        case DONE:
            return;
        }
    }
}

static int load_config_file(conf_mng_t *conf, char *confile)
{
    struct file *filp = NULL;
    mm_segment_t old_fs;
    ssize_t ret;
    char buf[1024] = { 0 };

    filp = filp_open(confile, O_RDWR, 0644);

    if (IS_ERR(filp))
    {
        printk(KERN_ERR "[kproxym] fail to open config file \"%s\"\n", confile);
        return -1;
    }

    old_fs = get_fs();
    set_fs(get_ds());
    filp->f_op->llseek(filp, 0, 0);
    ret = filp->f_op->read(filp, buf, sizeof(buf), &filp->f_pos);
    set_fs(old_fs);

    if (ret > 0)
    {
        parse_list(&conf->hosts, "[host_whitelist]", MAX_HOSTNAME_LEN, buf, ret);
        parse_list(&conf->exts, "[ext_whitelist]", MAX_URL_TYPE_LEN, buf, ret);
    }
    else if (ret == 0)
    {
        printk(KERN_INFO "[kproxym] the config file \"%s\" is empty\n", confile);
        return -1;
    }
    else
    {
        printk(KERN_ERR "[kproxym] read config file \"%s\" error\n", confile);
        return -1;
    }

    return 0;
}

static conf_mng_t* prepare_config(char *filepath)
{
    conf_mng_t *conf;

    conf = (conf_mng_t *)kmalloc(sizeof(conf_mng_t), GFP_KERNEL);
    if (!conf)
    {
        printk(KERN_ERR "[kproxym] malloc for config managerment fail, memory is low\n");
        return NULL;
    }
    INIT_LIST_HEAD(&conf->hosts);
    INIT_LIST_HEAD(&conf->exts);
    load_config_file(conf, filepath);
    return conf;
}

static void release_config(conf_mng_t **ppconf)
{
    conf_mng_t *conf;
    conf_rcd_t *rcd;

    if (NULL == ppconf || NULL == *ppconf)
    {
        return;
    }

    conf = *ppconf;
    while (!list_empty(&conf->hosts))
    {
        rcd = list_entry(conf->hosts.next, conf_rcd_t, list);
        list_del(&rcd->list);
        kfree(rcd);
    }

    while (!list_empty(&conf->exts))
    {
        rcd = list_entry(conf->exts.next, conf_rcd_t, list);
        list_del(&rcd->list);
        kfree(rcd);
    }
    kfree(conf);
    *ppconf = NULL;
}

conf_mng_t* get_config(void)
{
    return g_pconf;
}

conf_mng_t* set_config(char *filepath)
{
    conf_mng_t *pconf;

    pconf = prepare_config(filepath);
    if (pconf) g_pconf = pconf;

    return pconf;
}


/*
* if the @data include a hostname which was recordered in the @plist return 0
* otherwise return 1
*/
static int http_get_match_HOSTNAME_whitelist(struct list_head *plist, const char *data, int size)
{
    conf_rcd_t *cnfhost;
    char *phost;
    char *ptr;
    int len = 0;

    if (NULL == plist || list_empty(plist))
    {
        return 1;
    }

    phost = strnstr(data, "Host:", size);
    if (unlikely(!phost))
    {
        return 0;
    }
    phost += 5;
    ptr = phost;

    while (*ptr != '\r')
    {
        ptr++;
        len++;
    }

    list_for_each_entry(cnfhost, plist, list)
    {
        if (strnstr(phost, cnfhost->rcddata, len))
        {
            dprintk("[kproxym] the http-get hostname <%s> is in white list\n", cnfhost->rcddata);
            return 0;
        }
    }
    return 1;
}

/*[GET ] $url [HTTP]*/
static int http_get_match_URL(const char *data, int size)
{

    int cur = 0;
    int lim = size;
    conf_mng_t *conf = get_config();
    conf_rcd_t *prcd;

    //"GET " had been matched before
    cur += 4;

    //walk pass url
    if (lim > 256)
    {
        lim = 256;
    }

    while (data[cur] != ' ' && cur < lim)
    {
        cur++;
    }

    if (cur == lim)
    {
        return 0;
    }

    //filter static resources by matching url extension
    if (conf && !list_empty(&conf->exts))
    {
        list_for_each_entry(prcd, &conf->exts, list)
        {
            if (!strncmp(&data[cur - prcd->rcdlen], prcd->rcddata, prcd->rcdlen))
            {
                return 0;
            }
        }
    }
    return 1;
}

/* GET *** HTTP /1.1 \r\n */
static int http_get_match_GET(const char *data, int size)
{
    int cur = 0;
    int lim = size;

    //match method field: "GET "
    if (strncmp(data, "GET ", 4))
    {
        return 0;
    }
    cur += 4;

    //walk pass url
    if (lim > 256)
    {
        lim = 256;
    }

    while (data[cur] != ' ' && cur < lim)
    {
        cur++;
    }

    if (cur == lim)
    {
        return 0;
    }

    cur++;

    //match protocol field: "HTTP"
    if (cur + 4 > size)
    {
        return 0;
    }

    if (strncmp(&data[cur], "HTTP", 4))
    {
        return 0;
    }
    return 1;
}

/*
* "Accept: ***,text/html,*** \r\n"
*/
static bool http_get_match_ACCEPT(const char *data, int size)
{
    char *pacpt = NULL;
    char *ptr;
    int len = 0;

    pacpt = strnstr(data, "Accept:", size);
    if (unlikely(!pacpt))
    {
        return 0;
    }

    pacpt += 7;
    ptr = pacpt;
    while (*ptr != '\r')
    {
        ptr++;
        len++;
    }

    if (len < 9)
    {
        return 0;
    }

    if (strnstr(pacpt, "text/html", len))
    {
        return 1;
    }
    return 0;
}

static bool http_get_match(const struct tcphdr *tcph, int tcplen)
{
    const u8 *pdata;
    int pldlen =  tcplen - tcph->doff * 4;

    log_statistics();

    pdata = (u8 *)tcph + tcph->doff * 4;
    g_allin++;

    //input data is too short to be a valid http requst
    if (pldlen <= HTTP_MIN_SIZE)
    {
        g_filter_minlen++;
        return 0;
    }

    if (!http_get_match_GET(pdata, pldlen))
    {
        g_filter_get++;
        return 0;
    }

    if (!http_get_match_URL(pdata, pldlen))
    {
        g_filter_url++;
        return 0;
    }

    if (!http_get_match_ACCEPT(pdata, pldlen))
    {
        g_filter_accept++;
        return 0;
    }

    if (!http_get_match_HOSTNAME_whitelist(&get_config()->hosts, pdata, pldlen))
    {
        g_filter_host++;
        return 0;
    }

    //input data is not a complete http requst
    if (pdata[pldlen - 1] != 0x0a || pdata[pldlen - 2] != 0x0d || pdata[pldlen - 3] != 0x0a || pdata[pldlen - 4] != 0x0d)
    {
        g_filter_burst++;
        return 0;
    }
    g_total_matched++;
    return 1;
}

/*
* retrun 1 if @skb is a http get package or belongs to one stream of matched recorders
* otherwise 0
*/
static bool kproxym_match(const struct sk_buff *skb, struct xt_action_param *par)
{
    const struct iphdr *iph;
    const struct tcphdr *tcph;
    int tcplen = 0;
    int iphlen = 0;

    iph = ip_hdr(skb);

    /*
    * Although, working with other iptables matches,such as "xxx -p tcp -m tcp --dport 80 -m kproxym xxx",is recommended
    * but we should recheck this for safe, because we get a tcp header by pointer offset, which is based on TCP.
    */
    if (iph->protocol != IPPROTO_TCP)
    {
        return 0;
    }

    iphlen = iph->ihl * 4;
    tcph = (struct tcphdr *)((u8 *)iph + iphlen);
    tcplen = skb->len - iphlen;

    return http_get_match(tcph, tcplen);
}

static int xproxym_check(const struct xt_mtchk_param *par)
{
    kproxym_para_t *para;
    int err;
    struct kstat stat;
    mm_segment_t old_fs;
    conf_mng_t *pconf = get_config();

    para = (kproxym_para_t *)par->matchinfo;

    old_fs = get_fs();
    set_fs(get_ds());
    err = vfs_stat(para->confpath, &stat);
    set_fs(old_fs);

    if (err)
    {
        printk(KERN_ERR "[kproxym] get config file state err %d\n", err);
        return -EINVAL;
    }

    if (stat.size > 1024)
    {
        printk(KERN_ERR "[kproxym] config file \"%s\" is too big to parse\n", para->confpath);
        return -EINVAL;
    }

    if (NULL != pconf)
    {
        release_config(&pconf);
    }

    if (!set_config(para->confpath))
    {
        return -EINVAL;
    }

#if (DEBUG == 1)
    do
    {
        conf_mng_t *conf = get_config();
        conf_rcd_t *prcd;

        if (!list_empty(&conf->hosts))
        {
            list_for_each_entry(prcd, &conf->hosts, list)
            {
                dprintk("[%s] len:%d\n", prcd->rcddata, prcd->rcdlen);
            }
        }

        if (!list_empty(&conf->exts))
        {
            list_for_each_entry(prcd, &conf->exts, list)
            {
                dprintk("[%s] len:%d\n", prcd->rcddata, prcd->rcdlen);
            }
        }
    }
    while (0);
#endif

    return 0;
}

static struct xt_match kproxym_reg __read_mostly = {
    .name		= "kproxym",
    .family		= NFPROTO_IPV4,
    .match		= kproxym_match,
    .matchsize	= sizeof(kproxym_para_t),
    .proto		= IPPROTO_TCP,
    .checkentry	= xproxym_check,
    .me		= THIS_MODULE,
};

static int __init kproxym_match_init(void)
{
    if (g_pconf)
    {
        release_config(&g_pconf);
    }
    return xt_register_match(&kproxym_reg);
}

static void __exit kproxym_match_exit(void)
{
    printk(KERN_INFO "[kproxym] statistic [%u,%u,%u,%u,%u,%u,%u,%u]\n",
           g_allin, g_filter_minlen, g_filter_get, g_filter_url, g_filter_accept, g_filter_host,  g_filter_burst, g_total_matched);
    xt_unregister_match(&kproxym_reg);
}

module_init(kproxym_match_init);
module_exit(kproxym_match_exit);

