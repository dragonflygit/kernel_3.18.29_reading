/*********************************************************************************************
* (C) 2017 RippleTek .CO. All right resovled
*
* Description:  Kernel module to proxy a http get pkg, and those belong to the same stream
* Author:       TanLiyong <tanliyong@rippletek.com>
* Date:         2017-01-10
***********************************************************************************************/

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/netdevice.h>
#include <linux/jiffies.h>
#include <net/ip.h>
#include <net/tcp.h>
#include <net/route.h>
#include <net/dst.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/list.h>
#include <linux/inet.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/rwlock_types.h>
#include <linux/inetdevice.h>
#include <linux/rtnetlink.h>
#include <net/net_namespace.h>
#include <linux/notifier.h>
#include <linux/if.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tanliyong <tanliyong@rippletek.com>");
MODULE_DESCRIPTION("Xtables: http get packet \"proxy\" target for IPv4");

#define DEBUG 0

#if (DEBUG == 1)
#define dprintk(fmt, arg...) printk(KERN_INFO fmt, ##arg)
#else
#define dprintk(fmt, ...)
#endif

#define HASH_BUCKET_NUM 16
#define HASH_BUCKET_SIZE 4

#define IGNORE_MARK_MASK 0x00060000

#define BUCKET_HASH_MASK 0x0000000f

#define MAX_HOSTNAME_LEN 64
#define MAX_URL_TYPE_LEN 8

#define TIMESTAMP_OPT_TYPE 8

#define NODE_FLAG_NONE_TIMESTAMP 1

#define LOCAL_IF_NAME "br-lan"
#define LOCAL_IF_NAME_LEN 6

extern int ip_rcv_finish(struct sk_buff *skb);

typedef struct virtual_conntrack_node_S vct_node_t;
typedef struct virtual_conntrack_nodes_bkt_S vct_bkt_t;
typedef struct virtual_conntrack_mng_S vct_mng_t;

typedef struct tasklet_para_S tasklet_para_t;
typedef struct tasklet_desc_S tasklet_desc_t;

struct ipt_kproxyt_info {
    u32 proxy_addr;
    u16 proxy_port;
};

typedef enum PROXY_DIRECTION {
    DIRECTION_OUT,
    DIRECTION_IN
} direction_e;


enum VCT_MNG_STATE_E
{
    VCT_MNG_STATE_IDLE,
    VCT_MNG_STATE_RUNNING,
    VCT_MNG_STATE_PAUSE,
    VCT_MNG_STATE_STOPING,
    VCT_MNG_STATE_ERROR,
    VCT_MNG_STATE_CLOSED
};

typedef enum VIRTUAL_CONTRACK_STATES
{
    VC_STATE_CLOSED,
    VC_STATE_NEW,
    VC_STATE_SYN_SEND,
    VC_STATE_SYN_ACK_RECV,
    VC_STATE_ESTABLISHED,

    // the client start closing connection
    VC_STATE_FIN_WAIT_1,
    VC_STATE_FIN_WAIT_2,
    VC_STATE_TIME_WAIT,

    // the proxy-server start closing connecion
    VC_STATE_CLOSE_WAIT,
    VC_STATE_LAST_ACK,
    VC_STATE_CLOSING
} vc_state_e;

typedef enum TASKLET_STATES_E {
    TASKLET_IDLE,
    TASKLET_READY,
    TASKLET_BUSY,
} tasklet_state_e;


struct tasklet_para_S {
    vct_node_t *node;

    tasklet_state_e state;
    void *data;
};

struct tasklet_desc_S {
    vct_node_t *node;
    tasklet_para_t para;
    struct sk_buff *pad_skbs;
    struct tasklet_struct task;
};

struct virtual_conntrack_node_S {
    struct list_head list;

    u32 src_addr;
    u16 src_port;

    u32 dst_addr;
    u16 dst_port;

    u32 proxy_addr;
    u16 proxy_port;

    u16 state;
    u16 flags;

    u32 cliseq;
    u32 cliack_seq;

    u32 psvseq;
    u32 psvack_seq;

    u32 svseq;
    u32 svack_seq;

    u32 outfin;
    u32 infin;

    /*
     *  the difference between seq number between SERVER and proxy-Server
     *  seq_diff = seq_server - seq_proxy-server
     *
     *  so while tranfer a skb from CLIENT to proxy-Server, seq = seq_skb - seq_diff
     *  and while tranfer a skb from proxy-Server to CLIENT, seq = seq_skb + seq_diff
     */
    int seq_diff;

    u32 clits;
    u32 psvts;
    u32 svts;
    int timestamp_diff; // time_sever - time_PSV

    rwlock_t lock;
    vct_bkt_t *bkt;
    struct sk_buff *pad_rqst;
    struct sk_buff *pad_data;
    tasklet_desc_t tasklet_desc;
};


struct virtual_conntrack_nodes_bkt_S {
    int ndnum;
    unsigned long timestamp;
    struct list_head ndhd;
    rwlock_t lock;
    vct_mng_t *mng;
};


struct virtual_conntrack_mng_S {
    int cntsize;
    int cntnum;
    int bktsize;
    int state;
    rwlock_t lock;

    vct_bkt_t buckets[HASH_BUCKET_NUM];

    struct list_head fndhd; // free node list head

    struct sk_buff *pad_syn;
    struct sk_buff *pad_synack;
};

static vct_mng_t *g_vct_mng = NULL;

static vct_mng_t* get_mng_entry(void);
void bkt_stop_node(vct_node_t *node);
void bkt_free_node(vct_node_t *node);
static void node_release_pending_skbs(vct_node_t *node);

static inline void mng_restore_syn(vct_mng_t *mng, struct sk_buff *syn)
{
    struct sk_buff *skb = NULL;

    if (syn)
    {
        nf_reset(syn);
    }

    write_lock(&mng->lock);
    skb = mng->pad_syn;
    mng->pad_syn = syn;
    write_unlock(&mng->lock);

    if (skb)
    {
        kfree_skb(skb);
    }
}

static inline void mng_restore_ack(vct_mng_t *mng, struct sk_buff *ack)
{
    struct sk_buff *skb = NULL;

    if (ack)
    {
        nf_reset(ack);
    }

    write_lock(&mng->lock);
    skb = mng->pad_synack;
    mng->pad_synack = ack;
    write_unlock(&mng->lock);

    if (skb)
    {
        kfree_skb(skb);
    }
}

static inline int mng_is_running(vct_mng_t *mng)
{
    int ret = 0;

    read_lock(&mng->lock);
    ret = (VCT_MNG_STATE_RUNNING == mng->state);
    read_unlock(&mng->lock);
    return ret;
}

static inline int is_netdevice_valid(const struct net_device *dev)
{
    return (dev && netif_device_present(dev) && netif_running(dev) && (dev->flags & IFF_UP));
}

#if (DEBUG == 1)
static void debug_dump_iphead(struct iphdr *iph)
{
    printk(KERN_INFO "ip dump::\n");
    printk(KERN_INFO "------------------------------------------------------------------");
    printk(KERN_INFO "ver<%u> ihl<%u> TOS<%u> tot_len<%u> ID<%u> frag_off<0x%x> ", iph->version, iph->ihl, iph->tos, iph->tot_len, ntohs(iph->id), iph->frag_off);
    printk(KERN_INFO "ttl<%u> protocl<%u> check<0x%x> daddr<0x%x> saddr<0x%x>\n", iph->ttl, iph->protocol, iph->check, iph->daddr, iph->saddr);
    printk(KERN_INFO "------------------------------------------------------------------");
}

static void debug_dump_tcphead(struct tcphdr *tcph)
{
    u32 optlen = 0;
    u8 *opt;
    u32 len = 0;

    if (NULL == tcph)
    {
        return;
    }

    printk(KERN_INFO "tcp dumping::\n");
    printk(KERN_INFO "----------------------------------------------------------------------");
    printk(KERN_INFO "source<%u> dest<%u> seq<%u> ack_seq<%u> doff<%u> \n", ntohs(tcph->source), ntohs(tcph->dest), ntohl(tcph->seq), ntohl(tcph->ack_seq), tcph->doff);
    printk(KERN_INFO "%s", tcph->urg ? "urg " : "");
    printk(KERN_INFO "%s", tcph->ack ? "ack " : "");
    printk(KERN_INFO "%s", tcph->psh ? "psh " : "");
    printk(KERN_INFO "%s", tcph->rst ? "rst " : "");
    printk(KERN_INFO "%s", tcph->syn ? "syn " : "");
    printk(KERN_INFO "%s", tcph->fin ? "fin " : "");
    printk(KERN_INFO "\nwindow<%u> check<0x%x> urg_ptr<0x%x>\n", ntohs(tcph->window), ntohs(tcph->check), ntohs(tcph->urg_ptr));
    printk(KERN_INFO "options:\n");

    optlen = tcph->doff * 4 - 20;

    if (optlen <= 0)
    {
        return;
    }

    opt = (u8 *)tcph + 20;

    while (optlen > 0)
    {
        switch (*opt)
        {
        case 0: // end of options
            printk(KERN_INFO "\tEnd of option\n");
            len = 1;
            break;
        case 1: // empty option, always use for pad
            printk(KERN_INFO "\tNOP\n");
            len = 1;
            break;
        case 2: // MSS option
            printk(KERN_INFO "\tMSS:%u\n", (u32)(opt[2] * 256 + opt[3]));
            len = 4;
            break;
        case 3:
            printk(KERN_INFO "\tWindow scale:%u\n", (u32)opt[2]);
            len = 3;
            break;
        case 4:
            printk(KERN_INFO "\tTCP SACK Permitted:True\n");
            len = 2;
        case 5:
            printk(KERN_INFO "\tSACK data list, len:%u\n", (u32)(opt[1] - 2));
            len = (u32)opt[1];
            break;
        case 8:
            printk(KERN_INFO "\tTimestameps: TSval %u, TSecr %u\n", (u32)((opt[2] << 24) + (opt[3] << 16) + (opt[4] << 8) + opt[5]),
                   (u32)((opt[6] << 24) + (opt[7] << 16) + (opt[8] << 8) + opt[9]));
            len = (u32)opt[1];
            break;
        default:
            printk(KERN_INFO "\tUNKNOW\n");
            return;

        }

        optlen -= len;
        opt += len;
    }
    printk(KERN_INFO "----------------------------------------------------------------------");
}

static void debug_dump_skb(struct sk_buff *skb)
{
    struct iphdr *iph;
    struct tcphdr *tcph;

    iph = ip_hdr(skb);
    tcph = (struct tcphdr *)((u8 *)iph + iph->ihl * 4);

    printk(KERN_INFO "****************************************************************");
    printk(KERN_INFO "skb<%p> iph<%p> tcph<%p> check type: %u", skb, iph, tcph, skb->ip_summed);
    debug_dump_iphead(iph);
    debug_dump_tcphead(tcph);
    printk(KERN_INFO "****************************************************************\n\n");
}
#endif

/*
* get the local interface's dev of DUT
* we don't handle a reference at this time
*/
static struct net_device* mng_get_local_dev(void)
{
    struct net_device *dev = NULL;

    dev = dev_get_by_name(&init_net, LOCAL_IF_NAME);
    if (likely(dev))
    {
        dev_put(dev);
        return dev;
    }
    return NULL;
}

void tasklet_local_deliver_skb2(unsigned long para)
{
    vct_mng_t *mng = get_mng_entry();
    tasklet_para_t *tasklet_para = (tasklet_para_t *)para;
    vct_node_t *node;
    struct sk_buff *skb;
    int ret;

    if (unlikely(!tasklet_para))
    {
        return;
    }
    node = tasklet_para->node;
    if (unlikely(!node))
    {
        return;
    }

    write_lock(&node->lock);
    skb = (struct sk_buff *)tasklet_para->data;
    tasklet_para->data = NULL;
    write_unlock(&node->lock);
    if (unlikely(!skb))
    {
        return;
    }

    ret = NF_HOOK(NFPROTO_IPV4, NF_INET_PRE_ROUTING, skb, skb->dev, NULL, ip_rcv_finish);
    if (ret)
    {
        if (node && node->state < VC_STATE_ESTABLISHED)
        {
            printk(KERN_ERR "[KPROXYT] task deliver skb fail, ret:%d, stop proxying\n", ret);

            write_lock(&node->lock);
            skb = node->pad_rqst;
            node->pad_rqst = NULL;
            write_unlock(&node->lock);
            if (skb && is_netdevice_valid(skb->dev))
            {
                skb->mark |= IGNORE_MARK_MASK;
                ret = NF_HOOK(NFPROTO_IPV4, NF_INET_PRE_ROUTING, skb, skb->dev, NULL, ip_rcv_finish);

                if (ret)
                {
                    printk(KERN_ERR "[KPROXYT] task deliver requst skb fail too, ret:%d\n", ret);
                }
            }
            else if (skb)
            {
                kfree_skb(skb);
            }
            bkt_free_node(node);
        }
        dprintk("[KPROXYT] send fail after ESTABLISHED\n");
    }

    write_lock(&node->lock);
    tasklet_para->state = TASKLET_IDLE;
    write_unlock(&node->lock);
}

static int node_tasklet_schedule_once(tasklet_desc_t *tasklet_desc)
{
    struct sk_buff *pskb;
    struct sk_buff *ptr;
    tasklet_para_t *tasklet_para = &tasklet_desc->para;
    vct_node_t *node = tasklet_desc->node;

    read_lock(&node->lock);
    if (!tasklet_desc->pad_skbs || tasklet_para->state == TASKLET_BUSY)
    {
        read_unlock(&node->lock);
        return -1;
    }
    read_unlock(&node->lock);

    write_lock(&node->lock);
    // fetch the firsh pad_skbs
    pskb = tasklet_desc->pad_skbs;
    ptr = pskb->next;
    tasklet_desc->pad_skbs = ptr;

    pskb->next = NULL;
    tasklet_para->state = TASKLET_READY;
    tasklet_para->data = pskb;
    tasklet_hi_schedule(&tasklet_desc->task);
    tasklet_para->state = TASKLET_BUSY;
    write_unlock(&node->lock);

    return 0;
}

static int ilocal_deliver_skb2(vct_node_t *node, struct sk_buff *skb)
{
    tasklet_desc_t *tasklet_desc = &node->tasklet_desc;
    struct sk_buff *ptr;

    write_lock(&node->lock);
    if (tasklet_desc->pad_skbs)
    {
        ptr = tasklet_desc->pad_skbs;
        while (ptr->next)
        {
            ptr = ptr->next;
        }
        ptr->next = skb;
    }
    else
    {
        tasklet_desc->pad_skbs = skb;
    }
    write_unlock(&node->lock);

    return node_tasklet_schedule_once(tasklet_desc);
}

static int mng_entry_init(vct_mng_t **pmng)
{
    vct_mng_t *mng;
    vct_bkt_t *bkt;
    int i;

    if (!pmng)
    {
        return -1;
    }

    mng = kmalloc(sizeof(vct_mng_t), GFP_ATOMIC);
    if (NULL == mng)
    {
        return  -1;
    }

    memset(mng, 0, sizeof(vct_mng_t));

    mng->bktsize = HASH_BUCKET_SIZE;
    mng->cntsize = HASH_BUCKET_SIZE * HASH_BUCKET_NUM / 2;
    INIT_LIST_HEAD(&mng->fndhd);
    rwlock_init(&mng->lock);

    for (i = 0; i < HASH_BUCKET_NUM; i++)
    {
        bkt = &mng->buckets[i];
        INIT_LIST_HEAD(&bkt->ndhd);
        rwlock_init(&bkt->lock);
        bkt->mng = mng;
    }

    mng->state = VCT_MNG_STATE_IDLE;

    *pmng = mng;
    return 0;
}

static vct_mng_t* get_mng_entry(void)
{
    if (unlikely(NULL == g_vct_mng))
    {
        mng_entry_init(&g_vct_mng);
    }

    return g_vct_mng;
}

static void mng_entry_stop(vct_mng_t *mng)
{
    vct_bkt_t *bkt;
    vct_node_t *node;
    int i;

    write_lock(&mng->lock);
    mng->state = VCT_MNG_STATE_STOPING;
    write_unlock(&mng->lock);

    for (i = 0; i < HASH_BUCKET_NUM; i++)
    {
        bkt = &mng->buckets[i];
        while (!list_empty(&bkt->ndhd))
        {
            node = list_entry(bkt->ndhd.next, vct_node_t, list);
            bkt_stop_node(node);
        }
    }
}

static void destroy_mng_entry(vct_mng_t *mng)
{
    vct_node_t *node;
    vct_node_t *tmp;

    mng_entry_stop(mng);
    mng_restore_syn(mng, NULL);
    mng_restore_ack(mng, NULL);

    printk(KERN_INFO "[KPROXYT] there are %d node all togeater, free them and  going down\n", mng->cntnum);
    list_for_each_entry_safe(node, tmp, &mng->fndhd, list)
    {
        kfree(node);
        mng->cntnum--;
    }

    if (mng->cntnum)
    {
        printk(KERN_ERR "[KPROXYT] manger's cnutnum is geater than 0, %d node not free\n", mng->cntnum);
    }
    kfree(mng);
}


static void mng_put_free_node(vct_mng_t *mng, vct_node_t *node)
{
    struct sk_buff *skb;
    struct sk_buff *pnext;

    if (node->pad_data)
    {
        node_release_pending_skbs(node);
    }
    write_lock(&node->lock);
    if (node->pad_rqst)
    {
        kfree_skb(node->pad_rqst);
        node->pad_rqst = NULL;
    }

    if (node->tasklet_desc.para.data)
    {
        kfree_skb((struct sk_buff *)node->tasklet_desc.para.data);
        node->tasklet_desc.para.data = NULL;
    }

    if (node->tasklet_desc.pad_skbs)
    {
        skb = node->tasklet_desc.pad_skbs;
        while (skb)
        {
            pnext = skb->next;
            kfree_skb(skb);
            skb = pnext;
        }
        node->tasklet_desc.pad_skbs = NULL;
    }
    write_unlock(&node->lock);

    write_lock(&mng->lock);
    list_add_tail(&(node->list), &(mng->fndhd));
    write_unlock(&mng->lock);
}


static vct_node_t* mng_get_free_node(vct_mng_t *mng)
{
    struct list_head *frees;
    struct list_head *ndhd;
    vct_node_t *node = NULL;

    write_lock(&mng->lock);
    frees = &(mng->fndhd);
    if (list_empty(frees))
    {
        if (mng->cntnum >= mng->cntsize)
        {
            write_unlock(&mng->lock);
            return NULL;
        }

        node = kmalloc(sizeof(vct_node_t), GFP_ATOMIC);
        if (NULL == node)
        {
            write_unlock(&mng->lock);
            return NULL;
        }

        mng->cntnum++;
        dprintk("[KPROXYT] conntrack management memery use:%d * %d = %d", mng->cntnum, sizeof(vct_node_t), sizeof(vct_node_t) * mng->cntnum);
    }
    else
    {
        ndhd = frees->next;
        node = list_entry(ndhd, vct_node_t, list);
        list_del(ndhd);
    }
    write_unlock(&mng->lock);

    memset(node, 0, sizeof(vct_node_t));
    node->state = VC_STATE_CLOSED;
    INIT_LIST_HEAD(&node->list);
    tasklet_init(&(node->tasklet_desc.task), tasklet_local_deliver_skb2, (unsigned long)&(node->tasklet_desc.para));
    node->tasklet_desc.node = node;
    node->tasklet_desc.para.node = node;
    rwlock_init(&node->lock);
    return node;
}

static int mng_try_rcd_pkg(vct_mng_t *mng, struct sk_buff *skb, struct iphdr *iph, struct tcphdr *tcph, u32 psv_addr, u16 psv_port)
{
    vct_node_t *node = NULL;
    vct_bkt_t *bkt = NULL;
    int hash = -1;

    node = mng_get_free_node(mng);
    if (NULL == node)
    {
        return -1;
    }

    hash = ntohl(iph->saddr) & BUCKET_HASH_MASK;
    bkt = &mng->buckets[hash];

    node->dst_addr = iph->daddr;
    node->dst_port = tcph->dest;

    node->src_addr = iph->saddr;
    node->src_port = tcph->source;

    node->proxy_addr = psv_addr;
    node->proxy_port = psv_port;

    node->state = VC_STATE_NEW;
    node->bkt = bkt;

    write_lock(&bkt->lock);
    if (bkt->ndnum >= mng->bktsize)
    {
        write_unlock(&bkt->lock);
        mng_put_free_node(mng, node);
        return -1;
    }

    list_add(&node->list, &bkt->ndhd);
    bkt->ndnum++;
    write_unlock(&bkt->lock);

    dprintk("[KPROXYT] a new node had been add into bucket<%d>, total %d\n", hash, bkt->ndnum);
    return 0;
}

static vct_node_t* mng_get_matched_node(vct_mng_t *mng, struct iphdr *iph,  struct tcphdr *tcph, direction_e direct)
{
    vct_node_t *node = NULL;
    vct_bkt_t *bkt = NULL;
    struct list_head *pnodes;
    int hash = -1;

    if (direct == DIRECTION_OUT)
    {
        hash = ntohl(iph->saddr) & BUCKET_HASH_MASK;
        bkt = &mng->buckets[hash];

        read_lock(&bkt->lock);
        pnodes = &bkt->ndhd;

        if (list_empty(pnodes))
        {
            read_unlock(&bkt->lock);
            return  NULL;
        }

        list_for_each_entry(node, pnodes, list)
        {
            if (node->dst_addr == iph->daddr && node->src_addr == iph->saddr
                && node->dst_port == tcph->dest && node->src_port == tcph->source)
            {
                read_unlock(&bkt->lock);
                return node;
            }
        }
        read_unlock(&bkt->lock);

        return NULL;
    }
    else if (DIRECTION_IN == direct)
    {
        hash = ntohl(iph->daddr) & BUCKET_HASH_MASK;
        bkt = &mng->buckets[hash];

        read_lock(&bkt->lock);
        pnodes = &bkt->ndhd;
        if (list_empty(pnodes))
        {
            read_unlock(&bkt->lock);
            return  NULL;
        }

        list_for_each_entry(node, pnodes, list)
        {
            if (node->proxy_addr == iph->saddr && node->src_addr == iph->daddr
                && node->proxy_port == tcph->source && node->src_port == tcph->dest)
            {
                read_unlock(&bkt->lock);
                return node;
            }
        }
        read_unlock(&bkt->lock);
    }
    return NULL;
}

static void mng_start_proxy(vct_mng_t *mng)
{
    write_lock(&mng->lock);
    mng->state = VCT_MNG_STATE_RUNNING;
    write_unlock(&mng->lock);
}

static void mng_pause_proxy(vct_mng_t *mng)
{
    if (!mng_is_running(mng))
    {
        return;
    }

    mng_entry_stop(mng);
    mng_restore_syn(mng, NULL);
    mng_restore_ack(mng, NULL);
}

static void tcp_fake_send_check(struct sk_buff *skb, u32 saddr, u32 daddr)
{
    struct tcphdr *th = tcp_hdr(skb);

    if (skb->ip_summed == CHECKSUM_PARTIAL)
    {
        th->check = ~tcp_v4_check(skb->len, saddr, daddr, 0);
        skb->csum_start = skb_transport_header(skb) - skb->head;
        skb->csum_offset = offsetof(struct tcphdr, check);
    }
    else
    {
        th->check = tcp_v4_check(skb->len, saddr, daddr, csum_partial(th, th->doff << 2, skb->csum));
    }
}

static void tcp_fake_rechecksum(struct sk_buff *skb, struct iphdr *iph, struct tcphdr *tcph)
{
    u32 tcplen = 0;

    tcplen = skb->len - iph->ihl * 4;

    tcph->check = 0;
    tcph->check = tcp_v4_check(tcplen, iph->saddr, iph->daddr, csum_partial(tcph, tcplen, 0));
    ip_send_check(iph);
    skb->ip_summed = CHECKSUM_NONE;
}

static u8* get_tcp_option(u8 type, struct tcphdr *tcph)
{
    u32 optlen = 0;
    u8 *opt  = NULL;
    u32 len = 0;

    optlen = tcph->doff * 4 - sizeof(struct tcphdr);

    if (optlen <= 0)
    {
        return NULL;
    }

    opt = (u8 *)tcph + sizeof(struct tcphdr);

    while (optlen > 0)
    {
        if (type == *opt)
        {
            return opt;
        }

        if (*opt == 0 || *opt == 1)
        {
            len = 1;
        }
        else if (*opt == 2 || *opt == 3 || *opt == 4 || *opt == 5 || *opt == 8)
        {
            len = opt[1];
        }
        optlen -= len;
        opt += len;
    }
    return NULL;
}

static int get_tcp_timestamp_opt(struct tcphdr *tcph, u32 *ltime, u32 *rtime)
{
    u32 tsval = 0;
    u32 tsecr = 0;
    u8 *opt;

    opt = get_tcp_option(TIMESTAMP_OPT_TYPE, tcph);
    if (NULL == opt)
    {
        return -1;
    }

#if defined(__LITTLE_ENDIAN_BITFIELD)
    tsval = (opt[2]) + (opt[3] << 8) + (opt[4] << 16) + (opt[5] << 24);
    tsecr = (opt[6]) + (opt[7] << 8) + (opt[8] << 16) + (opt[9] << 24);
#elif defined(__BIG_ENDIAN_BITFIELD)
    tsval = (opt[2] << 24) + (opt[3] << 16) + (opt[4] << 8) + opt[5];
    tsecr = (opt[6] << 24) + (opt[7] << 16) + (opt[8] << 8) + opt[9];
#endif

    if (ltime)
    {
        *ltime = tsval;
    }

    if (rtime)
    {
        *rtime = tsecr;
    }
    return 0;
}

static int set_tcp_timestamp_opt(struct tcphdr *tcph, u32 ltime, u32 rtime)
{
    u8 *opt;

    opt = get_tcp_option(TIMESTAMP_OPT_TYPE, tcph);
    if (NULL == opt)
    {
        return -1;
    }

    memcpy(opt + 2, &ltime, sizeof(u32));
    memcpy(opt + 6, &rtime, sizeof(u32));
    return 0;
}

static void clear_tcp_timestamp_opt(struct tcphdr *tcph)
{
    u8 *opt;
    opt = get_tcp_option(TIMESTAMP_OPT_TYPE, tcph);
    if (NULL == opt)
    {
        return;
    }
    memset(opt, 1, 10);
}

/*
* set destination ip and port to be the proxy-server's
* fix up the seq of skb (and timestamp?)
*/
static void proxy_nat_2_psv(vct_node_t *node, struct iphdr *iph, struct tcphdr *tcph)
{
    int ltime;
    int rtime;

    iph->daddr = node->proxy_addr;
    tcph->dest = node->proxy_port;

    // recorder the CLIENT's seq
    node->cliseq = ntohl(tcph->seq);

    // transfer the ack_seq from SERVER to PSV
    node->psvack_seq = ntohl(tcph->ack_seq) - node->seq_diff;
    tcph->ack_seq = htonl(node->psvack_seq);

    if ((node->flags & NODE_FLAG_NONE_TIMESTAMP) && node->state >= VC_STATE_ESTABLISHED)
    {
        clear_tcp_timestamp_opt(tcph);
        return;
    }

    // transfer the timestamp
    if (!get_tcp_timestamp_opt(tcph, &ltime, &rtime))
    {
        node->clits = ntohl(ltime);
        node->svts = ntohl(rtime);

        set_tcp_timestamp_opt(tcph, ltime, htonl(node->psvts));
    }
}

static void proxy_nat_2_client(vct_node_t *node, struct iphdr *iph, struct tcphdr *tcph)
{
    int ltime;
    int rtime;
    int diff;

    iph->saddr = node->dst_addr;
    tcph->source = node->dst_port;

    // recorder the PSV's seq and CLIENT's ack_seq
    node->psvseq = ntohl(tcph->seq);
    node->cliack_seq = ntohl(tcph->ack_seq);

    // transfer the seq from proxy-Server to SERVER
    tcph->seq = htonl(node->psvseq + node->seq_diff);

    if ((node->flags & NODE_FLAG_NONE_TIMESTAMP) && node->state >= VC_STATE_ESTABLISHED)
    {
        clear_tcp_timestamp_opt(tcph);
        return;
    }
    // fixup the timestamp
    if (!get_tcp_timestamp_opt(tcph, &rtime, &ltime))
    {
        diff = ntohl(rtime) - node->psvts;
        node->psvts = ntohl(rtime);
        node->svts += diff;
        set_tcp_timestamp_opt(tcph, htonl(node->svts), ltime);
    }
}

static void struct_and_set_reset(u32 src_addr, u32 dst_addr, u16 src_port, u16 dst_port, u32 seq, u32 ack_seq)
{
    struct sk_buff *nskb;
    struct iphdr *niph;
    struct tcphdr *ntcph;
    vct_mng_t *mng = get_mng_entry();

    nskb = skb_copy(mng->pad_synack, GFP_ATOMIC);

    if (!nskb)
    {
        return;
    }

    niph = ip_hdr(nskb);

    niph->version = 4;
    niph->ihl = sizeof(struct iphdr) / 4;
    niph->tos = 0;
    niph->id = 0;
    niph->frag_off = htons(IP_DF);
    niph->protocol = IPPROTO_TCP;
    niph->check = 0;
    niph->saddr = src_addr;
    niph->daddr = dst_addr;

    ntcph = (struct tcphdr *)((u8 *)niph + niph->ihl * 4);
    memset(ntcph, 0, sizeof(struct tcphdr));

    ntcph->source = src_port;
    ntcph->dest = dst_port;
    ntcph->doff = sizeof(struct tcphdr) / 4;
    if (seq)
    {
        ntcph->seq = seq;
    }

    if (ack_seq)
    {
        ntcph->ack_seq = ack_seq;
        ntcph->ack = 1;
    }

    ntcph->rst = 1;
    ntcph->check =  ~tcp_v4_check(sizeof(struct tcphdr), niph->saddr, niph->daddr, 0);

    nskb->ip_summed = CHECKSUM_PARTIAL;
    nskb->csum_start = (u8 *)ntcph - nskb->head;
    nskb->csum_offset = offsetof(struct tcphdr, check);


    // ip_route_me_harder expects skb->dst to be set, sinse the skb is copyed, treate as sed done
    // skb_dst_set_noref(nskb, skb_dst(oldskb));

    nskb->protocol = htons(ETH_P_IP);
    if (ip_route_me_harder(nskb, RTN_UNSPEC)) goto free_nskb;

    niph->ttl = ip4_dst_hoplimit(skb_dst(nskb));

    if (nskb->len > dst_mtu(skb_dst(nskb))) goto free_nskb;

    //nf_ct_attach(nskb, oldskb);
    ip_local_out(nskb);
    return;

free_nskb:
    kfree_skb(nskb);
}

void bkt_stop_node(vct_node_t *node)
{
    node->state = VC_STATE_CLOSING;

    if (TASKLET_IDLE != node->tasklet_desc.para.state)
    {
        // there is a skb in tasklet  schedule, wait for schedule done if it had started
        tasklet_disable_nosync(&node->tasklet_desc.task);
    }
    bkt_free_node(node);
}

void bkt_free_node(vct_node_t *node)
{
    vct_bkt_t *bkt;

    bkt = node->bkt;

    write_lock(&bkt->lock);
    list_del(&node->list);
    bkt->ndnum--;
    write_unlock(&bkt->lock);

    mng_put_free_node(bkt->mng, node);
}

static void node_store_pending_skb(vct_node_t *node, struct sk_buff *skb)
{
    struct iphdr *iph;
    struct tcphdr *tcph;
    struct iphdr *piph;
    struct tcphdr *ptcph;
    struct sk_buff *ptr;
    struct sk_buff *pnext;
    u32 seq;
    u32 tmp;

    if (!node->pad_data)
    {
        node->pad_data = skb;
        return;
    }

    iph = ip_hdr(skb);
    tcph = (struct tcphdr *)((u8 *)iph + iph->ihl * 4);
    seq = ntohl(tcph->seq);

    write_lock(&node->lock);
    ptr = node->pad_data;
    piph = ip_hdr(ptr);
    ptcph = (struct tcphdr *)((u8 *)piph + piph->ihl * 4);
    tmp = ntohl(ptcph->seq);

    if (seq < tmp)
    {
        skb->next = ptr;
        node->pad_data = skb;
        write_unlock(&node->lock);
        return;
    }

    while (ptr->next)
    {
        pnext = ptr->next;
        piph = ip_hdr(pnext);
        ptcph = (struct tcphdr *)((u8 *)piph + piph->ihl * 4);
        tmp = ntohl(ptcph->seq);
        if (seq < tmp)
        {
            //found the position to insert
            break;
        }
        ptr = pnext;
    }

    skb->next = ptr->next;
    ptr->next = skb;
    write_unlock(&node->lock);
}

static void node_release_pending_skbs(vct_node_t *node)
{
    struct sk_buff *pskb;
    struct sk_buff *pnext;

    write_lock(&node->lock);
    pskb = node->pad_data;
    node->pad_data = NULL;
    write_unlock(&node->lock);

    while (pskb)
    {
        pnext = pskb->next;
        kfree_skb(pskb);
        pskb = pnext;
    }
}

static void node_send_pending_skbs(vct_node_t *node)
{
    struct sk_buff *pskb;
    struct sk_buff *pnext;
    struct iphdr *riph;
    struct tcphdr *rtcph;
    struct iphdr *piph;
    struct tcphdr *ptcph;
    u32 rseq;
    int len;

    if (!node->pad_rqst)
    {
        printk(KERN_ERR "[KPROXYT] lost http requst\n");
        node_release_pending_skbs(node);
        return;
    }

    riph = ip_hdr(node->pad_rqst);
    rtcph = (struct tcphdr *)((u8 *)riph + riph->ihl * 4);
    rseq = ntohl(rtcph->seq);

    write_lock(&node->lock);
    pskb = node->pad_data;
    node->pad_data = NULL;
    write_unlock(&node->lock);

    while (pskb)
    {
        pnext = pskb->next;
        piph = ip_hdr(pskb);
        ptcph = (struct tcphdr *)((u8 *)piph + piph->ihl * 4);
        len  = pskb->len - piph->ihl * 4 - ptcph->doff * 4;
        if (rseq != ntohl(ptcph->seq))
        {
            // out of order?
            break;
        }

        // check ok, send it
        proxy_nat_2_psv(node, piph, ptcph);
        tcp_fake_rechecksum(pskb, piph, ptcph);

        // set mark for the skb, that we do NOT process those only more
        pskb->mark |= IGNORE_MARK_MASK;
        ilocal_deliver_skb2(node, pskb);

        rseq += len;
        pskb = pnext;
    }

    // those skbs is out of order, we drop them and wait the client to do retransmit
    while (pskb)
    {
        pnext = pskb->next;
        kfree_skb(pskb);
        pskb = pnext;
    }
}

static void node_reset_sv(vct_node_t *node, const struct sk_buff *skb)
{
    u32 src_addr = 0;
    u32 dst_addr = 0;
    u16 src_port = 0;
    u16 dst_port = 0;
    u32 seq = 0;
    u32 ack_seq = 0;

    struct iphdr *iph;
    struct tcphdr *tcph;

    if (skb)
    {
        iph = ip_hdr(skb);
        tcph = (struct tcphdr *)((u8 *)iph + iph->ihl * 4);

        src_addr = iph->saddr;
        dst_addr = iph->daddr;

        src_port = tcph->source;
        dst_port = tcph->dest;

        seq = tcph->seq;
        ack_seq = tcph->ack_seq;
    }
    else
    {
        src_addr = node->src_addr;
        dst_addr = node->dst_addr;

        src_port = node->src_port;
        dst_port = node->dst_port;
        seq = htonl(node->cliseq);
        ack_seq = htonl(node->svack_seq);
    }
    struct_and_set_reset(src_addr, dst_addr, src_port, dst_port, seq, ack_seq);
}

static unsigned int node_fsm_handle_closing(vct_node_t *node, struct sk_buff *skb,  struct iphdr *iph, struct tcphdr *tcph, direction_e direct)
{
    node->state = VC_STATE_CLOSED;
    bkt_stop_node(node);
    return NF_DROP;
}

/*
* the state is only valid in prerouting
*/
static unsigned int node_fsm_handle_new(vct_node_t *node, struct sk_buff *skb, struct iphdr *iph, struct tcphdr *tcph)
{
    vct_mng_t *mng = get_mng_entry();
    struct sk_buff *synskb = NULL;
    struct iphdr *siph;
    struct tcphdr *stcph;

    u32 ltime;
    u32 rtime;

    if (likely(mng->pad_syn && mng->pad_synack))
    {
        synskb = skb_copy(mng->pad_syn, GFP_KERNEL);
        if (!synskb)
        {
            goto ERROR_STOP_FSM;
        }

        node->cliseq = ntohl(tcph->seq) - 1;
        node->svack_seq = ntohl(tcph->ack_seq);
        node->svseq = node->svack_seq - 1;

        siph = ip_hdr(synskb);
        siph->saddr = node->src_addr;
        stcph = (struct tcphdr *)((u8 *)siph + siph->ihl * 4);
        stcph->seq = htonl(node->cliseq);
        stcph->source = node->src_port;

        if (get_tcp_timestamp_opt(tcph, &ltime, &rtime) < 0)
        {
            // the HTTP-GET package had no timestamp option
            ltime = htonl(jiffies);
            rtime = ltime;
            node->flags |= NODE_FLAG_NONE_TIMESTAMP;
        }

        ltime = htonl(ntohl(ltime) - 20);

        set_tcp_timestamp_opt(stcph, ltime, rtime);

        proxy_nat_2_psv(node, siph, stcph);
        tcp_fake_rechecksum(synskb, siph, stcph);

        ilocal_deliver_skb2(node, synskb);

        node->state = VC_STATE_SYN_SEND;
        dprintk("[KPROXYT] match the pkg, node [%p]send a syn to PSV\n", node);
        node->pad_rqst = skb;
        return NF_STOLEN;
    }

ERROR_STOP_FSM:
    node_fsm_handle_closing(node, skb, iph, tcph, 0);
    return NF_ACCEPT;
}

// this state process only valid in postrouting
static unsigned int node_fsm_handle_syn_send(vct_node_t *node, struct sk_buff *skb, struct iphdr *iph, struct tcphdr *tcph)
{
    struct iphdr *siph;
    struct tcphdr *stcph;

    vct_mng_t *mng = get_mng_entry();
    struct sk_buff *ackskb;
    u32 ltime = 0;
    u32 rtime = 0;

    if (!tcph->syn || !tcph->ack)
    {
        // waiting for a SYN-ACK package, ignore all other packages
        return NF_DROP;
    }

    if (unlikely(!mng->pad_synack))
    {
        goto ERROR_STOP_FSM;
    }

    node->psvseq = ntohl(tcph->seq);
    node->cliack_seq = ntohl(tcph->ack_seq);
    node->seq_diff = node->svseq - node->psvseq;

    dprintk("[KPROXYT] [node:%p] recv SNY+ACK from PSV, psvseq:%u cliack_seq:%u seq_diff:%d\n", node, tcph->seq, tcph->ack_seq, node->seq_diff);
    ackskb = skb_copy(mng->pad_synack, GFP_ATOMIC);

    siph = ip_hdr(ackskb);
    siph->saddr = node->src_addr;

    stcph = (struct tcphdr *)((u8 *)siph + siph->ihl * 4);
    stcph->source = node->src_port;

    /*
     * here we fake a ACK looks like the CLIENT send  to SERVER
     * and proxy_nat_2_psv will fixup it later
     */
    stcph->seq = htonl(node->cliseq  + 1);
    stcph->ack_seq = htonl(node->svack_seq);
    stcph->psh = 0;

    // set timestump: get from @skb and set to @ackskb
    if (get_tcp_timestamp_opt(tcph, &rtime, &ltime))
    {
        ltime = htonl(jiffies - 20);
        dprintk("[KPROXYT] get the psv SYN+ACK, with NONE timestamp\n");
        node->flags |= NODE_FLAG_NONE_TIMESTAMP;
    }

    // if the nginx return SYN+ACK has no timestamp, psvts = 0
    node->psvts = ntohl(rtime);

    // reset timestamp: @clits + 10, keep @svts
    set_tcp_timestamp_opt(stcph, htonl(ntohl(ltime) + 10), htonl(node->svts));

    proxy_nat_2_psv(node, siph, stcph);
    stcph->ack_seq = htonl(node->psvseq + 1);

    tcp_fake_rechecksum(ackskb, siph, stcph);

    ilocal_deliver_skb2(node, ackskb);
    dprintk("[KPROXYT] [node:%p] send ACK to PSV, run into SYN_ACK_RECV\n", node);
    node->state = VC_STATE_SYN_ACK_RECV;
    kfree_skb(skb);

    if (node->pad_data)
    {
        dprintk("[KPROXYT] [node:%p] send the HTTP-GET rqst to PSV\n", node);
        node_send_pending_skbs(node);

        // reset the origin connection
        node_reset_sv(node, node->pad_rqst);

        if (node->pad_rqst)
        {
            kfree_skb(node->pad_rqst);
            node->pad_rqst = NULL;
        }
        node->state = VC_STATE_ESTABLISHED;
    }

    return NF_STOLEN;

ERROR_STOP_FSM:
    if (node->pad_rqst)
    {
        node->pad_rqst->mark |= IGNORE_MARK_MASK;
        NF_HOOK(NFPROTO_IPV4, NF_INET_PRE_ROUTING, node->pad_rqst, node->pad_rqst->dev, NULL, ip_rcv_finish);
        node->pad_rqst = NULL;
    }
    node_fsm_handle_closing(node, skb, iph, tcph, 0);
    return NF_ACCEPT;
}

/*valid in prerouting*/
static unsigned int node_fsm_handle_synack_recv(vct_node_t *node, struct sk_buff *skb,  struct iphdr *iph, struct tcphdr *tcph, direction_e direct)
{
    struct iphdr *rqstiph;
    struct tcphdr *rqsttcph;

    if (unlikely(!node->pad_rqst))
    {
        node_fsm_handle_closing(node, skb, iph, tcph, 0);
        return NF_DROP;
    }

    rqstiph = ip_hdr(node->pad_rqst);
    rqsttcph = (struct tcphdr *)((u8 *)rqstiph + rqstiph->ihl * 4);

    if (tcph->seq == rqsttcph->seq)
    {
        node->state = VC_STATE_ESTABLISHED;

        // reset the origin connection
        node_reset_sv(node, node->pad_rqst);

        proxy_nat_2_psv(node, iph, tcph);
        tcp_fake_rechecksum(skb, iph, tcph);

        return NF_ACCEPT;
    }

    // wait for the HTTP-GET, drop others
    return NF_DROP;
}

static unsigned int node_fsm_handle_establish(vct_node_t *node, struct sk_buff *skb,  struct iphdr *iph, struct tcphdr *tcph, direction_e direct)
{
    if (tcph->fin)
    {
        if (DIRECTION_OUT == direct)
        {
            node->state = VC_STATE_FIN_WAIT_1;
            node->outfin = ntohl(tcph->seq);
            dprintk("[KPROXYT] [node:%p] run into FIN_WAIT1", node);
        }
        else
        {
            node->state = VC_STATE_CLOSE_WAIT;
            node->infin = ntohl(tcph->seq);
            dprintk("[KPROXYT] [node:%p] run into CLOSE_WAIT", node);
        }
    }

    if (DIRECTION_OUT == direct)
    {
        proxy_nat_2_psv(node, iph, tcph);
        tcp_fake_rechecksum(skb, iph, tcph);
    }
    else
    {
        struct skb_shared_info *info;
        info = skb_shinfo(skb);
        if (skb->ip_summed == CHECKSUM_PARTIAL && info->gso_size)
        {
            proxy_nat_2_client(node, iph, tcph);
            tcph->check = 0;
            tcp_fake_send_check(skb, iph->saddr, iph->daddr);
            ip_send_check(iph);
        }
        else
        {
            proxy_nat_2_client(node, iph, tcph);
            tcp_fake_rechecksum(skb, iph, tcph);
        }
    }
    return NF_ACCEPT;
}

static unsigned int node_fsm_handle_finwait1(vct_node_t *node, struct sk_buff *skb,  struct iphdr *iph, struct tcphdr *tcph, direction_e direct)
{
    // check the seq
    if (tcph->ack && DIRECTION_IN)
    {
        if (ntohl(tcph->ack_seq) > node->outfin)
        {
            dprintk("[KPROXYT] [node:%p] run into FIN_WAIT2", node);
            node->state = VC_STATE_FIN_WAIT_2;
        }
    }

    if (tcph->fin && DIRECTION_IN == direct)
    {
        node->infin = ntohl(tcph->seq);

        dprintk("[KPROXYT] [node:%p] run into TIME_WAIT", node);
        node->state  = VC_STATE_TIME_WAIT;

        //  fix-me here wait for 2 rrt-time
        node->state  = VC_STATE_CLOSING;
        dprintk("[KPROXYT] [node:%p] directly run into CLOSING", node);
    }

    if (DIRECTION_OUT == direct)
    {
        proxy_nat_2_psv(node, iph, tcph);
    }
    else
    {
        proxy_nat_2_client(node, iph, tcph);
    }
    tcp_fake_rechecksum(skb, iph, tcph);

    return NF_ACCEPT;
}

static  unsigned int node_fsm_handle_finwait2(vct_node_t *node, struct sk_buff *skb, struct iphdr *iph, struct tcphdr *tcph, direction_e direct)
{
    if (tcph->fin && DIRECTION_IN == direct)
    {
        node->infin = ntohl(tcph->seq);

        dprintk("[KPROXYT] [node:%p] run into TIME_WAIT", node);
        node->state  = VC_STATE_TIME_WAIT;
    }

    if (DIRECTION_OUT == direct)
    {
        proxy_nat_2_psv(node, iph, tcph);
    }
    else
    {
        proxy_nat_2_client(node, iph, tcph);
    }
    tcp_fake_rechecksum(skb, iph, tcph);

    return NF_ACCEPT;
}

static unsigned int node_fsm_handle_timewait(vct_node_t *node, struct sk_buff *skb,  struct iphdr *iph, struct tcphdr *tcph, direction_e direct)
{
    int do_close = 0;

    if (tcph->ack && DIRECTION_OUT == direct && ntohl(tcph->ack_seq) > node->infin)
    {
        do_close = 1;
        dprintk("[KPROXYT] [node:%p] directly run into CLOSING", node);
    }

    if (DIRECTION_OUT == direct)
    {
        proxy_nat_2_psv(node, iph, tcph);
    }
    else
    {
        proxy_nat_2_client(node, iph, tcph);
    }
    tcp_fake_rechecksum(skb, iph, tcph);

    if (do_close)
    {
        node->state  = VC_STATE_CLOSING;
        node_fsm_handle_closing(node, skb, iph, tcph, direct);
    }
    return NF_ACCEPT;
}

static unsigned int node_fsm_handle_closewait(vct_node_t *node, struct sk_buff *skb, struct iphdr *iph, struct tcphdr *tcph, direction_e direct)
{
    if (tcph->fin && DIRECTION_OUT == direct)
    {
        node->outfin = ntohl(tcph->seq);

        dprintk("[KPROXYT] [node:%p] run into LAST_ACK", node);
        node->state = VC_STATE_LAST_ACK;
    }

    if (DIRECTION_OUT == direct)
    {
        proxy_nat_2_psv(node, iph, tcph);
    }
    else
    {
        proxy_nat_2_client(node, iph, tcph);
    }
    tcp_fake_rechecksum(skb, iph, tcph);

    return NF_ACCEPT;
}

static unsigned int node_fsm_handle_lastack(vct_node_t *node, struct sk_buff *skb,  struct iphdr *iph, struct tcphdr *tcph, direction_e direct)
{
    int do_close = 0;

    if (tcph->ack && DIRECTION_IN == direct && ntohl(tcph->ack_seq) > node->outfin)
    {
        dprintk("[KPROXYT] [node:%p] run into CLOSING", node);
        do_close = 1;
    }

    if (DIRECTION_OUT == direct)
    {
        proxy_nat_2_psv(node, iph, tcph);
    }
    else
    {
        proxy_nat_2_client(node, iph, tcph);
    }
    tcp_fake_rechecksum(skb, iph, tcph);

    if (do_close)
    {
        node->state = VC_STATE_CLOSING;
        node_fsm_handle_closing(node, skb,  iph, tcph, direct);
    }
    return NF_ACCEPT;
}

static unsigned int  proxy_node_FSM_process(vct_node_t *node, struct sk_buff *skb, struct iphdr *iph, struct tcphdr *tcph, direction_e direct)
{
    // once there is some skb to send, send them one by one
    if (node->tasklet_desc.pad_skbs)
    {
        node_tasklet_schedule_once(&node->tasklet_desc);
    }

    // some one RESET the connection, stop to conntracking
    if (tcph->rst)
    {
        dprintk("[KPROXYT] <0x%x:%u --> 0x%x:%u>  RESET the connection[%p], state:%d\n",
                ntohl(iph->saddr), ntohs(tcph->source), ntohl(iph->daddr), ntohs(tcph->dest), node, node->state);

        if (node->state == VC_STATE_SYN_SEND && DIRECTION_IN == direct)
        {
            // the proxy server reset the connection while rcev SYN
            goto STOP_PROXY;
        }

        if (DIRECTION_OUT == direct)
        {
            proxy_nat_2_psv(node, iph, tcph);
        }
        else
        {
            proxy_nat_2_client(node, iph, tcph);
        }
        tcp_fake_rechecksum(skb, iph, tcph);

        node->state = VC_STATE_CLOSING;
        node_fsm_handle_closing(node, skb, iph, tcph, direct);
        return NF_ACCEPT;
    }

    switch (node->state)
    {
    case VC_STATE_NEW:
        return NF_DROP;

    case VC_STATE_SYN_SEND:
        if (DIRECTION_OUT == direct)
        {
            struct iphdr *riph;
            struct tcphdr *rtcph;

            if (unlikely(!node->pad_rqst))
            {
                node_fsm_handle_closing(node, skb, iph, tcph, direct);
                return NF_DROP;
            }
            riph = ip_hdr(node->pad_rqst);
            rtcph = (struct tcphdr *)((u8 *)riph + riph->ihl * 4);

            if (rtcph->seq != tcph->seq)
            {
                return NF_DROP;
            }

            if (node->pad_data)
            {
                // twice retransmited
                goto STOP_PROXY;
            }
            node->pad_data = skb;
            return NF_STOLEN;
        }
        return node_fsm_handle_syn_send(node, skb, iph, tcph);

    case VC_STATE_SYN_ACK_RECV:
        if (DIRECTION_IN == direct)
        {
            return NF_DROP;
        }
        return node_fsm_handle_synack_recv(node, skb,  iph, tcph, direct);

    case VC_STATE_ESTABLISHED:
        return node_fsm_handle_establish(node, skb,  iph, tcph, direct);

    case VC_STATE_FIN_WAIT_1:
        return node_fsm_handle_finwait1(node, skb, iph, tcph, direct);

    case VC_STATE_FIN_WAIT_2:
        return node_fsm_handle_finwait2(node, skb, iph, tcph, direct);

    case VC_STATE_TIME_WAIT:
        return node_fsm_handle_timewait(node, skb, iph, tcph, direct);

    case VC_STATE_CLOSE_WAIT:
        return node_fsm_handle_closewait(node, skb, iph, tcph, direct);

    case VC_STATE_LAST_ACK:
        return node_fsm_handle_lastack(node, skb, iph, tcph, direct);

    case VC_STATE_CLOSING:
    case VC_STATE_CLOSED:
        return node_fsm_handle_closing(node, skb, iph, tcph, direct);

    default:
        printk(KERN_ERR "[KPROXYT] [node:%p] FSM run out of mind\n", node);
        return node_fsm_handle_closing(node, skb, iph, tcph, direct);
    }

STOP_PROXY:
    if (node->pad_rqst)
    {
        node->pad_rqst->mark |= IGNORE_MARK_MASK;
        NF_HOOK(NFPROTO_IPV4, NF_INET_PRE_ROUTING, node->pad_rqst, node->pad_rqst->dev, NULL, ip_rcv_finish);
        node->pad_rqst = NULL;
        node_fsm_handle_closing(node, skb, iph, tcph, direct);
        return NF_DROP;
    }
    return NF_DROP;
}

/*
 *Catch the packages from br-lan, and the dest is $svip:$svpor
 *Recorder it, and DNAT to local server $pxip:$pxpor
 */
static unsigned int kproxy_handle_prerouting(struct sk_buff *skb, struct iphdr *iph, struct tcphdr *tcph)
{
    vct_mng_t *mng = get_mng_entry();
    vct_node_t *node = NULL;

    // those package send from this module, should NOT be process any more
    if (skb->mark & IGNORE_MARK_MASK)
    {
        return NF_ACCEPT;
    }

    if (unlikely(!mng->pad_syn && tcph->syn))
    {
        if (skb->dev && !memcmp(skb->dev->name, LOCAL_IF_NAME, LOCAL_IF_NAME_LEN))
        {
            mng_restore_syn(mng, skb_copy(skb, GFP_ATOMIC));
        }
        dprintk("[KPROXYT] dump a tcp-syn package\n");
    }

    if (unlikely(!mng->pad_synack && tcph->ack && !tcph->syn))
    {
        if (skb->dev && !memcmp(skb->dev->name, LOCAL_IF_NAME, LOCAL_IF_NAME_LEN))
        {
            mng_restore_ack(mng, skb_copy(skb, GFP_ATOMIC));
        }
        dprintk("[KPROXYT] dump a tcp-ack package\n");
    }

    node = mng_get_matched_node(mng, iph, tcph, DIRECTION_OUT);

    if (unlikely(!node))
    {
        return NF_ACCEPT;
    }

    return proxy_node_FSM_process(node, skb, iph, tcph, DIRECTION_OUT);
}

static unsigned int kproxy_handle_postrouting(struct sk_buff *skb, struct iphdr *iph, struct tcphdr *tcph)
{
    vct_mng_t *mng = get_mng_entry();
    vct_node_t *node = NULL;

    node = mng_get_matched_node(mng, iph, tcph, DIRECTION_IN);

    if (!node)
    {
        return NF_ACCEPT;
    }

    return proxy_node_FSM_process(node, skb,  iph, tcph, DIRECTION_IN);
}

static unsigned int hooks_handler_entry(unsigned int hooknum, struct sk_buff *skb, const struct net_device *in,
                                        const struct net_device *out, int (*okfn)(struct sk_buff *))
{
    struct iphdr *iph;
    struct tcphdr *tcph;
    vct_mng_t *mng = get_mng_entry();

    if (unlikely(NULL == mng))
    {
        return NF_ACCEPT;
    }

    if (unlikely(!mng_is_running(mng)))
    {
        return NF_ACCEPT;
    }

    if (NULL == skb)
    {
        return NF_ACCEPT;
    }
    iph = ip_hdr(skb);

    if (NULL == iph)
    {
        return NF_ACCEPT;
    }

    if (iph->protocol != 0x06)
    {
        return NF_ACCEPT;
    }

    if (0 != skb_linearize(skb))
    {
        return NF_ACCEPT;
    }

    // skb_linearize may change skb's header pointers, so get them once more
    iph = ip_hdr(skb);
    tcph = (struct tcphdr *)((unsigned char *)iph + iph->ihl * 4);

    if (hooknum == NF_INET_POST_ROUTING)
    {
        return kproxy_handle_postrouting(skb, iph, tcph);
    }
    else if (hooknum == NF_INET_PRE_ROUTING)
    {
        return kproxy_handle_prerouting(skb, iph, tcph);
    }
    // others
    return NF_ACCEPT;
}

static unsigned int KPROXYT_tg(struct sk_buff *skb, const struct xt_action_param *par)
{
    const struct ipt_kproxyt_info *kproxytinfo = par->targinfo;

    struct iphdr *iph;
    struct tcphdr *tcph;
    vct_mng_t *mng = get_mng_entry();

    vct_node_t *node = NULL;

    if (unlikely(NULL == mng))
    {
        return NF_ACCEPT;
    }

    if (unlikely(!mng_is_running(mng)))
    {
        return NF_ACCEPT;
    }

    // those package send from this module, should NOT be process any more
    if (skb->mark & IGNORE_MARK_MASK)
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

    tcph = (struct tcphdr *)((u8 *)iph + iph->ihl * 4);

    node = mng_get_matched_node(mng, iph, tcph, DIRECTION_OUT);
    if (unlikely(node))
    {
        return NF_ACCEPT;
    }

    if (mng_try_rcd_pkg(mng, skb, iph, tcph, kproxytinfo->proxy_addr, kproxytinfo->proxy_port) < 0)
    {
        dprintk("[KPROXYT] fail to add a node to track<0x%x:%u --> 0x%x:%u>\n", ntohl(iph->saddr), ntohs(tcph->source), ntohl(iph->daddr), ntohs(tcph->dest));
        return NF_ACCEPT;
    }

    // this will never fail
    node = mng_get_matched_node(mng, iph, tcph, DIRECTION_OUT);

    /*
    * fix-me
    * here we drop the HTTP-GET packe which just been tracked before
    * let the client to do a retransmi
    *
    * the reson of doing this is at this point the package had been conntracked
    * by system connect-conntrack, if we transfer this package may cause problem
    */
    return node_fsm_handle_new(node, skb, iph, tcph);
}

static int KPROXYT_tg_check(const struct xt_tgchk_param *par)
{
    const struct ipt_kproxyt_info *kproxytinfo = par->targinfo;

    if (kproxytinfo->proxy_addr == 0 || kproxytinfo->proxy_port == 0)
    {
        printk(KERN_ERR "[KPROXYT] proxy server address and port could NOT be empty!\n");
        return -EINVAL;
    }

    dprintk("[KPROXYT]: proxy config addr:0x%x : %u\n", ntohl(kproxytinfo->proxy_addr), ntohs(kproxytinfo->proxy_port));
    return 0;
}

static int mng_device_event(struct notifier_block *unused, unsigned long event, void *ptr)
{
    struct net_device *dev = ptr;
    vct_mng_t *mng = get_mng_entry();

    if (!mng || !dev)
    {
        return NOTIFY_OK;
    }

    switch (event)
    {
    case NETDEV_GOING_DOWN:
    case NETDEV_UNREGISTER:
    case NETDEV_DOWN:
        if (!memcmp(dev->name, LOCAL_IF_NAME, LOCAL_IF_NAME_LEN))
        {
            mng_pause_proxy(mng);
        }
        break;

    case NETDEV_UP:
        if (!memcmp(dev->name, LOCAL_IF_NAME, LOCAL_IF_NAME_LEN))
        {
            mng_start_proxy(mng);
        }
        break;

    default:
        break;
    }
    return NOTIFY_OK;
}

struct notifier_block mng_device_notifier = {
    .notifier_call =  mng_device_event
};

static struct nf_hook_ops kproxyt_cooperate_hooks[]__read_mostly = {
    {
        .hook = hooks_handler_entry,
        .owner = THIS_MODULE,
        .pf = PF_INET,
        .hooknum = NF_INET_PRE_ROUTING,
        .priority = NF_IP_PRI_FIRST - 30,
    },
    {
        .hook = hooks_handler_entry,
        .owner = THIS_MODULE,
        .pf = PF_INET,
        .hooknum = NF_INET_POST_ROUTING,
        .priority = NF_IP_PRI_LAST + 30,
    }
};

static struct xt_target kproxyt_tg_reg __read_mostly = {
    .name		= "KPROXYT",
    .family		= NFPROTO_IPV4,
    .target		= KPROXYT_tg,
    .targetsize	= sizeof(struct ipt_kproxyt_info),
    .hooks		= (1 << NF_INET_PRE_ROUTING) | (1 << NF_INET_POST_ROUTING) | (1 << NF_INET_FORWARD) | (1 << NF_INET_LOCAL_IN) | (1 << NF_INET_LOCAL_OUT),
    .checkentry	= KPROXYT_tg_check,
    .me		= THIS_MODULE,
};

static int __init kproxyt_tg_init(void)
{
    int ret = 0;

    if (mng_entry_init(&g_vct_mng) < 0)
    {
        printk(KERN_ERR "[KPROXYT] init manager fail\n");
        ret = -1;
        goto ERR_OUT1;
    }

    if ((ret = nf_register_hooks(kproxyt_cooperate_hooks, sizeof(kproxyt_cooperate_hooks) / sizeof(struct nf_hook_ops))) < 0)
    {
        printk(KERN_ERR "[KPROXYT] register hooks fail:%d\n", ret);
        goto ERR_OUT2;
    }

    if ((ret = register_netdevice_notifier(&mng_device_notifier)) < 0)
    {
        printk(KERN_ERR "[KPROXYT] register net device notifier fail:%d\n", ret);
        goto ERR_OUT3;
    }

    if ((ret = xt_register_target(&kproxyt_tg_reg)) < 0)
    {
        printk(KERN_ERR "[KPROXYT] register KPROXYT target fail:%d\n", ret);
        goto ERR_OUT4;
    }
    printk(KERN_INFO  "[KPROXYT]init done, ver 0.13\n");
    return 0;

ERR_OUT4:
    unregister_netdevice_notifier(&mng_device_notifier);
ERR_OUT3:
    nf_unregister_hooks(kproxyt_cooperate_hooks, sizeof(kproxyt_cooperate_hooks) / sizeof(struct nf_hook_ops));
ERR_OUT2:
    destroy_mng_entry(g_vct_mng);
ERR_OUT1:
    return ret;
}

static void __exit kproxyt_tg_exit(void)
{
    xt_unregister_target(&kproxyt_tg_reg);
    unregister_netdevice_notifier(&mng_device_notifier);
    nf_unregister_hooks(kproxyt_cooperate_hooks, sizeof(kproxyt_cooperate_hooks) / sizeof(struct nf_hook_ops));
    destroy_mng_entry(g_vct_mng);
}

module_init(kproxyt_tg_init);
module_exit(kproxyt_tg_exit);
