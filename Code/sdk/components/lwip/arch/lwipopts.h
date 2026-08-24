/*
 * Copyright (c) 2001-2004 Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 *
 * Author: Adam Dunkels <adam@sics.se>
 *
 */
#ifndef __LWIP_OPTS_H__
#define __LWIP_OPTS_H__

#define LWIP_LIB_ENABLE              (1)
#if LWIP_LIB_ENABLE

#include "cc.h"

#define LWIP_VERSION_MPART           "master" /*major part*/

/*****************************************
*lwip macro define for different platform
*default settings is according to LWIP_PLATFORM_DEFINE
******************************************/
#define LWIP_PLATFORM_DEFINE_1802           (0x0001)
#define LWIP_PLATFORM_DEFINE_1802S          (0x0002)
#define LWIP_PLATFORM_DEFINE_CRANE          (0x0004)
#define LWIP_PLATFORM_DEFINE_CRANE_MODULE   (0x0008) /*based on crane*/
#define LWIP_PLATFORM_DEFINE_NBIOT   	    (0x0010)  /*based on crane*/
#define LWIP_PLATFORM_DEFINE_ALIOS   	    (0x0020)  /*based on platform define*/

/*set default platform to crane*/
#define LWIP_PLATFORM_DEFINE                (LWIP_PLATFORM_DEFINE_CRANE)

#ifdef PLAT_NEZHAC /*this macro is defined from tavor/build/arbel.mak*/
#undef LWIP_PLATFORM_DEFINE
#define LWIP_PLATFORM_DEFINE                LWIP_PLATFORM_DEFINE_1802S
#endif

#ifdef CRANE_MCU_DONGLE /*this macro is defined from tavor/build/arbel.mak*/
#undef LWIP_PLATFORM_DEFINE
#define LWIP_PLATFORM_DEFINE                (LWIP_PLATFORM_DEFINE_CRANE | LWIP_PLATFORM_DEFINE_CRANE_MODULE)
#endif

#ifdef LWIP_NBIOT /*this macro is defined from tavor/build/arbel.mak*/
#undef LWIP_PLATFORM_DEFINE
#define LWIP_PLATFORM_DEFINE                (LWIP_PLATFORM_DEFINE_CRANE | LWIP_PLATFORM_DEFINE_NBIOT)
#endif

#ifdef PLAT_USE_ALIOS /*this macro is defined from tavor/build/arbel.mak*/
#undef LWIP_PLATFORM_DEFINE
#define LWIP_PLATFORM_DEFINE                (LWIP_PLATFORM_DEFINE_CRANE | LWIP_PLATFORM_DEFINE_ALIOS)
#endif

#define IS_LWIP_PLATFORM_1802_1802S         (((LWIP_PLATFORM_DEFINE & (LWIP_PLATFORM_DEFINE_1802 | LWIP_PLATFORM_DEFINE_1802S)) > 0) ? 1 : 0 )
#define IS_LWIP_PLATFORM_1802               (((LWIP_PLATFORM_DEFINE & LWIP_PLATFORM_DEFINE_1802) > 0) ? 1 : 0 )
#define IS_LWIP_PLATFORM_1802S              (((LWIP_PLATFORM_DEFINE & LWIP_PLATFORM_DEFINE_1802S) > 0) ? 1 : 0 )
#define IS_LWIP_PLATFORM_CRANE              (((LWIP_PLATFORM_DEFINE & LWIP_PLATFORM_DEFINE_CRANE) > 0) ? 1 : 0 )
#define IS_LWIP_PLATFORM_CRANE_MODULE       (((LWIP_PLATFORM_DEFINE & LWIP_PLATFORM_DEFINE_CRANE_MODULE) > 0) ? 1 : 0 )
#define IS_LWIP_PLATFORM_NBIOT              (((LWIP_PLATFORM_DEFINE & LWIP_PLATFORM_DEFINE_NBIOT) > 0) ? 1 : 0 )
#define IS_LWIP_PLATFORM_USE_ALIOS          (((LWIP_PLATFORM_DEFINE & LWIP_PLATFORM_DEFINE_ALIOS) > 0) ? 1 : 0 )

/*****************************************
*Global macro define here for Lwip
******************************************/
/*mifi service port setting config here!*/
#define MFG_S_PORT_NUM          (9930)
#define MFG_C_PORT_NUM          (9931)
#define SHELL_SVR_PORT          (4320)
#define REDIRECT_PORT_80        (80)
#define REDIRECT_PORT_8090      (8090)
#define MONGOOSE_LISTEN_PORT    (80)

/*bit set for IPv4v6, bit 0 set 1 for ipv4, bit 1 set 1 for ipv6*/
/*if both v4 and v6, value is 0x3*/
#ifndef IP_TYPE_IPV4V6
#define IP_TYPE_IPV4V6			0x3
#endif
#ifndef IP_TYPE_IPV4
#define IP_TYPE_IPV4			0x1
#endif
#ifndef IP_TYPE_IPV6
#define IP_TYPE_IPV6			0x2
#endif

#ifndef PDP_TYPE_DEFAULT
#define PDP_TYPE_DEFAULT        0x0
#endif
#ifndef PDP_TYPE_DEDICATED
#define PDP_TYPE_DEDICATED      0x1
#endif

#define LWIP_INVALID_CID        (127) /*for char type*/
#define LWIP_DL_DIRECTION       (0)
#define LWIP_UL_DIRECTION       (1)
#define LWIP_IN_DIRECTION       (0)
#define LWIP_OUT_DIRECTION      (1)
#define LWIP_DNS_NAME_LEN       (128)

#define ETH_PAD_SIZE                    0   /*2*/
#define RNDIS_HEADER_SIZE               44  /*42*/
#define PMSG_SIZE                       16
#define RNDIS_HDR_LEN                   74  /*16+44+14*/
#define WIFI_HDR_LEN                    122 /*108+14*/
#define WIFI_HDR                        108
#define LWIP_CPY_HDR                    96  /*max 128, IP6 Header (40) + TCP Header(32) + eth(14) + extend(10)*/
#define LWIP_ACK_SIZE                   54  /*only ipv4: mac(14) + ip(20) + tcp(20)*/

/*lte ack, only ipv4: ip(20) + tcp(20)*/
#define LWIP_LTE_ACK_LEN                (LWIP_ACK_SIZE - 14)
/*72: IP Header (40) + TCP Header(32), tcp type*/
/*extend to 88 */
#define LWIP_LTE_IP6_PLEN               (88)
/*52: IP Header (20) + TCP Header(32), tcp type*/
/*56: IP Header (20) + ICMP Header(8) + IP Header (20) + ICMP Header(8), ICMP_TE type*/
/*extend to 64 */
#define LWIP_LTE_IP4_PLEN               (64)

/***feature macro define for all platform***/
#define LWIP_NETCONN_DO_RECV_NOBLOCK    (1)
#define LWIP_ATCTL_SOCKET_TEST          (1)
#define LWIP_SOCKET_DEBUG_OP_FLAG       (1)
#define LWIP_NETCONN_DEBUG_OP_FLAG      (1)
#define LWIP_NETCONN_EVENT_OP_FLAG      (1)
#define LWIP_MALLOC_NETCONN_STATIC      (1)
#define LWIP_WND_SCALE                  (1)
#define LWIP_TCPFLOW_FALG               (1)

/*macro replaced by variable, and will use api to control*/
extern u32_t tcp_rcv_win;
extern u32_t tcp_snd_win;
extern u8_t tcp_rcv_scale;
extern u8_t ipreass_maxage;
extern u8_t ip_default_ttl;

#define TCP_SND_BUF                     tcp_snd_win
#define TCP_WND                         tcp_rcv_win
#define TCP_RCV_SCALE                   tcp_rcv_scale
#define IP_REASS_MAXAGE                 ipreass_maxage
#define IP_DEFAULT_TTL                  ip_default_ttl

#ifdef MIN_SYS
#define TCP_DNS_TURN_EN                 0
#define TCP_MSS_ADJUST_EN               0
#define IP_NAT_FTP_ALG                  0
#define IP_NAT						    0
#else
#if IS_LWIP_PLATFORM_NBIOT
#define TCP_DNS_TURN_EN                 0
#define TCP_MSS_ADJUST_EN               0
#define IP_NAT_FTP_ALG                  0
#define IP_NAT						    0
#else
#define TCP_DNS_TURN_EN                 1
#define TCP_MSS_ADJUST_EN               1
#define IP_NAT_FTP_ALG                  1
#define IP_NAT						    1
#endif
#endif

#if IS_LWIP_PLATFORM_USE_ALIOS
#include "lwipopts_alios.h"
#elif IS_LWIP_PLATFORM_NBIOT
#include "lwipopts_nb.h"
#elif IS_LWIP_PLATFORM_CRANE
#include "lwipopts_crane.h"
#elif IS_LWIP_PLATFORM_1802_1802S
#include "lwipopts_1802.h"
#else
#include "lwipopts_crane.h" /*set for default*/
#endif

/**Debug macro control, quickly setting here**/
#define LWIP_DEBUG                      LWIP_DBG_ON /*LWIP_DBG_ON or LWIP_DBG_OFF */

#define LWIP_DEBUG_HIGH_BITS            8
#define DIAG_ON_DEBUG                   0x00 << LWIP_DEBUG_HIGH_BITS
#define ETHARP_DEBUG                    0x01 << LWIP_DEBUG_HIGH_BITS //Enable debugging in etharp.c
#define NETIF_DEBUG                     0x02 << LWIP_DEBUG_HIGH_BITS //Enable debugging in netif.c
#define PBUF_DEBUG                      0x03 << LWIP_DEBUG_HIGH_BITS //Enable debugging in pbuf.c
#define API_LIB_DEBUG                   0x04 << LWIP_DEBUG_HIGH_BITS //Enable debugging in api_lib.c
#define API_MSG_DEBUG                   0x05 << LWIP_DEBUG_HIGH_BITS //Enable debugging in api_msg.c
#define SOCKETS_DEBUG                   0x06 << LWIP_DEBUG_HIGH_BITS //Enable debugging in sockets.c
#define NETCONN_DEBUG                   0x07 << LWIP_DEBUG_HIGH_BITS //Enable debugging in api_msg.c or api_lib.c
#define ICMP_DEBUG                      0x08 << LWIP_DEBUG_HIGH_BITS //Enable debugging in icmp.c
#define IGMP_DEBUG                      0x09 << LWIP_DEBUG_HIGH_BITS //Enable debugging in igmp.c
#define INET_DEBUG                      0x0A << LWIP_DEBUG_HIGH_BITS //Enable debugging in inet.c
#define IP_DEBUG                        0x0B << LWIP_DEBUG_HIGH_BITS //Enable debugging for IP
#define IP_REASS_DEBUG                  0x0C << LWIP_DEBUG_HIGH_BITS //Enable debugging in ip_frag.c for both frag & reass
#define RAW_DEBUG                       0x0D << LWIP_DEBUG_HIGH_BITS //Enable debugging in raw.c
#define MEM_DEBUG                       0x0E << LWIP_DEBUG_HIGH_BITS //Enable debugging in mem.c
#define MEMP_DEBUG                      0x0F << LWIP_DEBUG_HIGH_BITS //Enable debugging in memp.c
#define SYS_DEBUG                       0x10 << LWIP_DEBUG_HIGH_BITS //Enable debugging in sys.c
#define TIMERS_DEBUG                    0x11 << LWIP_DEBUG_HIGH_BITS //Enable debugging in timers.c
#define TCP_DEBUG                       0x12 << LWIP_DEBUG_HIGH_BITS //Enable debugging for TCP
#define TCP_SLOWTMR_DEBUG               0x13 << LWIP_DEBUG_HIGH_BITS //Enable debugging for TCP
#define TCP_INPUT_DEBUG                 0x14 << LWIP_DEBUG_HIGH_BITS //Enable debugging in tcp_in.c for incoming debug
#define TCP_FR_DEBUG                    0x15 << LWIP_DEBUG_HIGH_BITS //Enable debugging in tcp_in.c for fast retransmit
#define TCP_RTO_DEBUG                   0x16 << LWIP_DEBUG_HIGH_BITS //Enable debugging in TCP for retransmi
#define TCP_CWND_DEBUG                  0x17 << LWIP_DEBUG_HIGH_BITS //Enable debugging for TCP congestion window.
#define TCP_WND_DEBUG                   0x18 << LWIP_DEBUG_HIGH_BITS //Enable debugging in tcp_in.c for window updating
#define TCP_OUTPUT_DEBUG                0x19 << LWIP_DEBUG_HIGH_BITS //Enable debugging in tcp_out.c output functions
#define TCP_RST_DEBUG                   0x1A << LWIP_DEBUG_HIGH_BITS //Enable debugging for TCP with the RST message
#define TCP_QLEN_DEBUG                  0x1B << LWIP_DEBUG_HIGH_BITS //Enable debugging for TCP queue lengths
#define UDP_DEBUG                       0x1C << LWIP_DEBUG_HIGH_BITS //Enable debugging in UDP
#define TCPIP_DEBUG                     0x1D << LWIP_DEBUG_HIGH_BITS //Enable debugging in tcpip.c
#define LWIP_PPP_DEBUG                  0x1E << LWIP_DEBUG_HIGH_BITS //Enable debugging for PPP, fix build error--redefine PPP_DEBUG,so define LWIP_PPP_DEBUG
#define SLIP_DEBUG                      0x1F << LWIP_DEBUG_HIGH_BITS //Enable debugging in slipif.c
#define DHCP_DEBUG                      0x20 << LWIP_DEBUG_HIGH_BITS //Enable debugging in dhcp.c
#define AUTOIP_DEBUG                    0x21 << LWIP_DEBUG_HIGH_BITS //Enable debugging in autoip.c
#define SNMP_MSG_DEBUG                  0x22 << LWIP_DEBUG_HIGH_BITS //Enable debugging for SNMP messages
#define SNMP_MIB_DEBUG                  0x23 << LWIP_DEBUG_HIGH_BITS //Enable debugging for SNMP MIBs
#define DNS_DEBUG                       0x24 << LWIP_DEBUG_HIGH_BITS //Enable debugging for DNS
#define IP6_DEBUG                       0x25 << LWIP_DEBUG_HIGH_BITS //Enable debugging for IPv6
#define IP_NAT_DEBUG                    0x26 << LWIP_DEBUG_HIGH_BITS //Enable debugging in ip_nat.c
#define LWIP_API_DEBUG                  0x27 << LWIP_DEBUG_HIGH_BITS //Enable debugging in lwip_api.c
#define ND6_DEBUG                       0x28 << LWIP_DEBUG_HIGH_BITS //Enable debugging in nd6.c
#define IP_TRACE                        0x29 << LWIP_DEBUG_HIGH_BITS //Enable debugging in ipv4 trace
#define IF_BRIDGE_DEBUG                 0x2A << LWIP_DEBUG_HIGH_BITS //Enable debugging in if bridge
#define IF_USB_DEBUG                    0x2B << LWIP_DEBUG_HIGH_BITS //Enable debugging in if usb
#define IF_WIFI_DEBUG                   0x2C << LWIP_DEBUG_HIGH_BITS //Enable debugging in if wifi
#define IF_PDP_DEBUG                    0x2D << LWIP_DEBUG_HIGH_BITS //Enable debugging in if pdp
#define IP_SEC_DEBUG                    0x2E << LWIP_DEBUG_HIGH_BITS //Enable debugging in ipsec
#define TFT_DEBUG                       0x2F << LWIP_DEBUG_HIGH_BITS //Enable debugging in tft module

#else /* LWIP_LIB_ENABLE */

#define NO_SYS                          1
#define LWIP_NETCONN                    0
#define LWIP_DEBUG                      0
#define LWIP_SOCKET                     0
#define LWIP_DNS                        0
#define LWIP_NETIF_API                  0
#define UDP_ECHO_SVR                    0
#define TCP_ECHO_SVR                    0
#define LWIP_DNS_RELAY                  0
#define LWIP_DHCP_SERVER                0
#define IP_NAT                          0
#define LWIP_ICMP                       0
#define LWIP_IGMP                       0
#define LWIP_STATS                      0
#define LWIP_RAW                        0
#define LWIP_DHCP                       0
#define LWIP_UDP                        0
#define LWIP_TCP                        0
#define LWIP_TIMERS                     0
#define LWIP_NETIF_LTE                  0
#define LWIP_NETIF_PC                   0
#define LWIP_NETIF_BRIDGE               0
#define LWIP_ARP                        0
#define LWIP_ETHERNET                   0
#define LWIP_NETIF_WF_UAP               0
#define IP_FILTER_EN			        0

#endif /* LWIP_LIB_ENABLE */

#endif /* __LWIP_OPTS_H__ */
