/********************************************************************************************************************
* (C) 2017 RippleTek .CO. All right resovled
*
* Description:  Kernel module to hook HTTP and parse the HTTP header. Right now only USER-AGENT parsing was supported
* Author:       TanLiyong <tanliyong@rippletek.com>
* Date:         2017-12-14
*********************************************************************************************************************/

#include <linux/in.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/if_ether.h>
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
#include <linux/netfilter/nf_nat.h>

#include <linux/netfilter_ipv4/ipt_kproxym.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/version.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tanliyong <tanliyong@rippletek.com>");
MODULE_DESCRIPTION("Xtables: http hook to parse HTTP HEADER for IPv4. Version 1.0, support UA only");

#define HASH_BKT_NUM 256
#define PROC_FILE_NAME "hkhttp"

typedef struct HTTP_USER_AGENT_RECODE {
    struct list_head list;
    unsigned char hdaddr[6];
    unsigned int addr;
    unsigned char ua[256];
} ua_node_t;

typedef struct UA_NODE_POOL {
    unsigned int ndnum;
    struct list_head pool;
} ua_pool_t;

static const char HTTP_SPLITTER[] = { 0x0d, 0x0a, 0x0 };

static spinlock_t hkhttp_lock = __SPIN_LOCK_UNLOCKED(hkhttp_lock);
static ua_node_t *g_rcds[HASH_BKT_NUM];

static ua_pool_t g_pool;
static struct list_head *ppool = NULL;

static struct list_head* uapool_init(void)
{
    int i;
    ua_node_t *node;

    INIT_LIST_HEAD(&g_pool.pool);
    ppool = &g_pool.pool;
    for (i = 0; i < 16; i++)
    {
        node = kmalloc((sizeof(ua_node_t)), GFP_ATOMIC);
        if (node)
        {
            memset(node, 0, sizeof(ua_node_t));
            INIT_LIST_HEAD(&node->list);
            list_add_tail(&node->list, ppool);
            g_pool.ndnum++;
        }
    }
    return ppool;
}

static void uapool_cleanup(struct list_head *ppool)
{
    ua_node_t *node;
    ua_node_t *tmp;
    if (NULL == ppool)
    {
        return;
    }
    list_for_each_entry_safe(node, tmp, ppool, list)
    {
        list_del(&node->list);
        kfree(node);
    }
}

static ua_node_t* uapool_get_node(ua_pool_t *pool)
{
    ua_node_t *node;
    struct list_head *ppool = &pool->pool;
    int i;

    spin_lock_bh(&hkhttp_lock);
    if (list_empty(ppool))
    {
        if (pool->ndnum >= HASH_BKT_NUM / 4)
        {
            goto FAIL_OUT;
        }
        for (i = 0; i < 16; i++)
        {
            node = kmalloc((sizeof(ua_node_t)), GFP_ATOMIC);
            if (node)
            {
                memset(node, 0, sizeof(ua_node_t));
                INIT_LIST_HEAD(&node->list);
                list_add_tail(&node->list, ppool);
                pool->ndnum++;
            }
        }
    }
    if (list_empty(ppool))
        goto FAIL_OUT;
    node = list_entry(ppool->next, ua_node_t, list);
    list_del(&node->list);
    spin_unlock_bh(&hkhttp_lock);
    return node;

FAIL_OUT:
    spin_unlock_bh(&hkhttp_lock);
    return NULL;
}

static void uapool_put_node(struct list_head *ppool, ua_node_t *node)
{
    spin_lock_bh(&hkhttp_lock);
    list_add_tail(&node->list, ppool);
    spin_unlock_bh(&hkhttp_lock);
}

//***********************************************************************************************************
static void* hkhttp_proc_start(struct seq_file *seq, loff_t *loff_pos)
{
    static unsigned long counter = 0;

    /* beginning a new sequence ? */
    if (*loff_pos == 0)
    {
        /* yes => return a non null value to begin the sequence */
        return &counter;
    }
    else
    {
        /* no => it's the end of the sequence, return end to stop reading */
        *loff_pos = 0;
        return NULL;
    }
    return NULL;
}

static void* hkhttp_proc_next(struct seq_file *seq, void *v, loff_t *pos)
{
    return NULL;
}

static void hkhttp_proc_stop(struct seq_file *seq, void *v)
{
    //don't need to do anything
}
static int hkhttp_proc_show(struct seq_file *m, void *v)
{
    int i;
    ua_node_t *ptr;
    unsigned int ip = 0;

    for (i = 0; i < HASH_BKT_NUM; i++)
    {
        ptr = g_rcds[i];
        if (ptr)
        {
            ip = ntohl(ptr->addr);
            seq_printf(m, "%02X:%02X:%02X:%02X:%02X:%02X %02u.%02u.%02u.%02u %s\n",
                       ptr->hdaddr[0], ptr->hdaddr[1], ptr->hdaddr[2], ptr->hdaddr[3], ptr->hdaddr[4], ptr->hdaddr[5],
                       (ip >> 24), (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff,
                       ptr->ua);

            // when the node had been read, release the recode
            uapool_put_node(ppool, ptr);
            g_rcds[i] = NULL;
        }
    }
    return 0;
}

static struct seq_operations hkhttp_proc_sops = {
    .start = hkhttp_proc_start,
    .next  = hkhttp_proc_next,
    .stop  = hkhttp_proc_stop,
    .show  = hkhttp_proc_show
};

static int hkhttp_proc_open(struct inode *inode, struct file *file)
{
    return seq_open(file, &hkhttp_proc_sops);
}

static struct file_operations hkhttp_proc_main_ops = {
    .owner   = THIS_MODULE,
    .open    = hkhttp_proc_open,
    .read    = seq_read,
    .llseek  = seq_lseek,
    .release = seq_release
};

static int init_proc_file(void)
{
    struct proc_dir_entry *file;
#if (LINUX_VERSION_CODE > KERNEL_VERSION(3, 10, 1))
    file = proc_create(PROC_FILE_NAME, 0644, NULL, &hkhttp_proc_main_ops);
#else
    file  = create_proc_entry(PROC_FILE_NAME, 0, NULL);
    if (file)
    {
        file->proc_fops = &hkhttp_proc_main_ops;
    }
#endif

    if (!file)
        return -ENOMEM;
    return 0;
}

static void free_proc_file(void)
{
    int i;
    for (i = 0; i < HASH_BKT_NUM; i++)
    {
        if (g_rcds[i])
        {
            uapool_put_node(ppool, g_rcds[i]);
            g_rcds[i] = NULL;
        }
    }
    remove_proc_entry(PROC_FILE_NAME, NULL);
}

//*********************************************************************************
static int HKHTTP_tg_check(const struct xt_tgchk_param *par)
{
    return 0;
}

static int http_header_key_extract(const char *pdata, int size, const char *key, int keylen, char *buf, int buflen)
{
    int len = -1;
    char *val = NULL;
    char *valend = NULL;

    if (NULL == pdata || NULL == key || NULL == buf)
    {
        return -1;
    }
    val = strnstr(pdata, key, size);
    if (unlikely(!val))
    {
        return -1;
    }

    //[KEY]: [VALUE]\r\n
    val = val + keylen;
    if (*val != ':' || *(val + 1) != ' ')
    {
        return -1;
    }
    val += 2;
    len = size - keylen - 2;
    valend = strnstr(val, HTTP_SPLITTER, len);
    if (valend)
    {
        len = valend - val + 1;
    }

    len = len > (buflen - 1) ? (buflen - 1) : len;
    if (len == 0)
    {
        return 0;
    }
    memcpy(buf, val, len);
    buf[len] = '\0';
    return len;
}

static int http_USER_AGENT_extract(char *buf, int buflen,  char *pdata, int size)
{
    return http_header_key_extract(pdata, size, "User-Agent", 10, buf, buflen);
}

static unsigned int HKHTTP_tg(struct sk_buff *skb, const struct xt_action_param *par)
{
    struct iphdr *iph;
    struct tcphdr *tcph;
    struct ethhdr *ethh;
    int iphlen, tcplen, pldlen;
    int ualen = -1;
    char buf[256] = { 0 };
    ua_node_t *node = NULL;
    u8 *pdata;

    ethh = (struct ethhdr *)skb_mac_header(skb);
    if (NULL == ethh || g_rcds[ethh->h_source[5]])
    {
        return NF_ACCEPT;
    }

    iph = ip_hdr(skb);
    if (unlikely(!iph))
    {
        return NF_ACCEPT;
    }

    /*
    * Mostly the @skb is a tcp, we do a recheck here for safe
    * because we will get a tcp header by pointer offset later
    */
    if (unlikely(iph->protocol != IPPROTO_TCP))
    {
        return NF_ACCEPT;
    }

    iphlen = iph->ihl * 4;
    tcph = (struct tcphdr *)((u8 *)iph + iph->ihl * 4);
    tcplen = skb->len - iphlen;
    pldlen =  tcplen - tcph->doff * 4;
    pdata = (char *)tcph + tcph->doff * 4;

    ualen = http_USER_AGENT_extract(buf, 256, pdata, pldlen);
    if (ualen > 0)
    {
        if (NULL == ethh)
        {
            printk(KERN_ERR "[HKHTTP] Can NOT get the eth head of skb<ip:0x%x>...\n", ntohl(iph->saddr));
            goto END_OUT;
        }
        node = uapool_get_node(&g_pool);
        if (NULL == node)
        {
            printk(KERN_ERR "[HKHTTP] <%02X:%02X:%02X:%02X:%02X:%02X /0x%x> fail to get recoder node...\n",
                   ethh->h_source[0], ethh->h_source[1], ethh->h_source[2], ethh->h_source[3], ethh->h_source[4], ethh->h_source[5], ntohl(iph->saddr));
            goto END_OUT;
        }
        node->addr = iph->saddr;
        memcpy(node->hdaddr, ethh->h_source, 6);
        memcpy(node->ua, buf, ualen);
        g_rcds[ethh->h_source[5]] = node;
    }

END_OUT:
    return NF_ACCEPT;
}


static struct xt_target hkhttp_tg_reg __read_mostly = {
    .name		= "HKHTTP",
    .family		= NFPROTO_IPV4,
    .target		= HKHTTP_tg,
    .targetsize	= 8,
    .hooks		= (1 << NF_INET_PRE_ROUTING) | (1 << NF_INET_POST_ROUTING) | (1 << NF_INET_FORWARD) | (1 << NF_INET_LOCAL_IN) | (1 << NF_INET_LOCAL_OUT),
    .checkentry	= HKHTTP_tg_check,
    .me		= THIS_MODULE,
};

static int __init hkhttp_tg_init(void)
{
    int ret = 0;
    uapool_init();
    if ((ret = init_proc_file()) != 0)
    {
        printk(KERN_ERR "[HKHTTP] fail to create proc file :%d\n", ret);
        return ret;
    }

    if ((ret = xt_register_target(&hkhttp_tg_reg)) < 0)
    {
        printk(KERN_ERR "[HKHTTP] register HKHTTP target fail:%d\n", ret);
        free_proc_file();
        return ret;
    }
    printk(KERN_INFO  "[HKHTTP]init done, ver 0.46\n");
    return 0;
}

static void __exit hkhttp_tg_exit(void)
{
    xt_unregister_target(&hkhttp_tg_reg);
    free_proc_file();
    uapool_cleanup(ppool);
}

module_init(hkhttp_tg_init);
module_exit(hkhttp_tg_exit);

