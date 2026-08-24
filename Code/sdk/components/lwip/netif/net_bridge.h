#include "err.h"
#include "sys.h"
#include "stats.h"
#include "snmp.h"
#include "netif.h"
#include "sys_arch.h"
#include "etharp.h"

#ifndef __NET_BRIDGE__
#define __NET_BRIDGE__

#if LWIP_NETIF_BRIDGE

#define BRIDGE_TMR_SEC  60  /* 60 seconds */

/**********************************/
/*define net_mac_rec_t flags definition*/
/**********************************/
/* can do bridge network Interface */
#define NETB_REC_FLAG_BRIDGE        0x0001
/* define this ip config is form dhcp*/
#define NETB_REC_FLAG_DHCP_IP       0x0002
/* define this ip config is form arp msg*/
#define NETB_REC_FLAG_ARP_IP        0x0004

#define net_pool_is_bridge(net)            (((net)->flags & NETB_REC_FLAG_BRIDGE) ? 1 : 0)
#define net_pool_is_dhcpip(net)            (((net)->flags & NETB_REC_FLAG_DHCP_IP) ? 1 : 0)

struct _net_mac_rec_st{
    s32_t            ttl;
    u32_t            src_ip;
    u32_t            flags;
    struct netif *   netif;
    struct eth_addr  mac_addr;
};
typedef struct _net_mac_rec_st net_mac_rec_t;

net_mac_rec_t *net_pool_findall_by_mac(char *mac_addr);
net_mac_rec_t *net_pool_findall_by_ip(u32_t ip);
net_mac_rec_t *net_pool_find_by_ip(u32_t ip);
net_mac_rec_t *net_pool_fetch_latest(void);
net_mac_rec_t *net_pool_fetch_head(void);
struct netif * net_pool_get_if_dongle(void);
void net_pool_update_latest(net_mac_rec_t * net_rec);
int net_pool_fetch_size(void);
err_t net_pool_update_ip(u32_t ipaddr, char *mac_addr, u32_t flag);
err_t net_pool_remove_ip(u32_t ipaddr);

void net_bridge_tmr(void);
void net_bridge_init(void);
u8_t net_bridge_input(struct netif *netif, struct pbuf *p);
u8_t net_bridge_arp(struct netif *netif, struct pbuf *p);
u8_t net_bridge_input_ip6( struct netif *netif, struct pbuf *p);
err_t net_pre_routing(struct netif *netif, struct pbuf *p);

#if LWIP_BRIDGE_DIRECTLY_DONGLE
err_t net_bridge_directly_dongle(struct netif * netif_in, struct pbuf * p);
#endif

#if 0
struct netif * netif net_post_routing(struct netif *netif_in, struct pbuf *p);
#endif

#endif

#endif /* __NET_BRIDGE__ */
