/**
 * @file sdk_functionality_network_compat.h
 * @brief WALNUT network constants. Numeric values match lwIP enum netconn_evt
 *        (components/lwip/lwip/api.h) - same absolute meanings as the SIMCOM
 *        SC_NETCONN_EVT_* constants. On walnut these events are synthesized
 *        by the app-side TCPMON monitor (sdk_walnut_tcp.c), not the stack.
 */
#ifndef SDK_FUNCTIONALITY_NETWORK_COMPAT_H
#define SDK_FUNCTIONALITY_NETWORK_COMPAT_H

#define SDK_NET_SUCCESS               0

#define SDK_NETCONN_EVT_RCVPLUS       0
#define SDK_NETCONN_EVT_RCVMINUS      1
#define SDK_NETCONN_EVT_SENDPLUS      2
#define SDK_NETCONN_EVT_SENDMINUS     3
#define SDK_NETCONN_EVT_CONNECTED     4
#define SDK_NETCONN_EVT_ACCEPTPLUS    5
#define SDK_NETCONN_EVT_ERROR_CLSD    6
#define SDK_NETCONN_EVT_ERROR_RST     7
#define SDK_NETCONN_EVT_ERROR_ABRT    8
#define SDK_NETCONN_EVT_CLOSE_WAIT    9
#define SDK_NETCONN_EVT_SENDACKED     10
#define SDK_NETCONN_EVT_CLOSE_NORMAL  11
#define SDK_NETCONN_EVT_ERROR         12

#endif /* SDK_FUNCTIONALITY_NETWORK_COMPAT_H */
