
#include "sys/socket.h"
#include "osi_api.h"
#include <string.h>
#include "errno.h"
#include "common_api.h"
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET
#include "wifi.h"
#include "atiny_socket.h"
#include <stdio.h>
#include <stdlib.h>
#include "network.h"
#endif

#if CONFIG_LOSCFG_COMPONENTS_NET_AT_DEBUG
#define SocketDebug SDK_LOG_I
#else
#define SocketDebug(FMT, ...)
#endif

struct addrinfo {
    int               ai_flags;      /* Input flags. */
    int               ai_family;     /* Address family of socket. */
    int               ai_socktype;   /* Socket type. */
    int               ai_protocol;   /* Protocol of socket. */
    socklen_t         ai_addrlen;    /* Length of socket address. */
    struct sockaddr  *ai_addr;       /* Socket address of socket. */
    char             *ai_canonname;  /* Canonical name of service location. */
    struct addrinfo  *ai_next;       /* Pointer to next in list. */
};

void lwip_socket_init(void);
int lwip_accept(int s, struct sockaddr *addr, socklen_t *addrlen);
int lwip_bind(int s, const struct sockaddr *name, socklen_t namelen);
int lwip_shutdown(int s, int how);
int lwip_shutdown2(int s, int how, char *filename, int line);
int lwip_getpeername(int s, struct sockaddr *name, socklen_t *namelen);
int lwip_getsockname(int s, struct sockaddr *name, socklen_t *namelen);
int lwip_getsockopt(int s, int level, int optname, void *optval, socklen_t *optlen);
int lwip_setsockopt(int s, int level, int optname, const void *optval, socklen_t optlen);
int lwip_close(int s);
int lwip_close2(int s, char *filename, int line);
int lwip_connect(int s, const struct sockaddr *name, socklen_t namelen);
int lwip_listen(int s, int backlog);
int lwip_recv(int s, void *mem, size_t len, int flags);
int lwip_read(int s, void *mem, size_t len);
int lwip_recvfrom(int s, void *mem, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen);
int lwip_send(int s, const void *dataptr, size_t size, int flags);
int lwip_sendto(int s, const void *dataptr, size_t size, int flags, const struct sockaddr *to, socklen_t tolen);
int lwip_socket(int domain, int type, int protocol);
int lwip_write(int s, const void *dataptr, size_t size);
int lwip_select(int maxfdp1, void *readset, void *writeset, void *exceptset, struct timeval *timeout);
int lwip_ioctl(int s, long cmd, void *argp);
int lwip_fcntl(int s, int cmd, int val);
int lwip_eventfd(unsigned int initval, int flags);
int lwip_getthreaderrno(int s);
int lwip_getsockvalid(int s);
int lwip_getsockerrno(int s);
int lwip_getsocktype(int s);
int lwip_gethostbyname_r(const char *name, struct hostent *ret, char *buf, size_t buflen, struct hostent **result,
                         int *h_errnop);

int lwip_getaddrinfo(const char *nodename,
                           const char *servname,
                           const struct addrinfo *hints,
                           struct addrinfo **res);

void lwip_freeaddrinfo(struct addrinfo *ai);

extern const char *inet_ntop(int af, const void *src, char *dst, int size);
extern unsigned short int ntohs(unsigned short int n);

int id_convert_inter(int id)
{
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET

    if (id & 0xFFFF0000) {
        return (id & 0xBFFFFFFF) >> 16; //最多支持63个socket
    }

#endif
    return id;
}

int id_convert_ext(int id, uint8_t wifi_flag)
{
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET

    if (id == -1) {
        return -1;
    }

    if (wifi_flag) {
        return id;
    }

    return (id << 16) | 0x40000000; //最多支持63个socket
#else
    return id;
#endif
}

uint8_t is_wifi_socket_id(int inter_id)
{
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET

    if (inter_id & 0xFFFF0000) {
        return 0;
    }

    return 1;
#else
    return 0;
#endif
}

int accept(int s, struct sockaddr *addr, socklen_t *addrlen)
{
    int ret = -1;
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET
    if (is_wifi_socket_id(s) == 0) {
        s = id_convert_inter(s);
        goto cat1Return;
    }

    s = id_convert_inter(s);

    if (wifi_socket_is_enable() != 1) {
        goto cat1Return;
    }

    if (s < 0 || s > SOCKET_NUM_MAX) {
        return -1;
    }

    if (!isIDOccupied(s)) {
        return -1;
    }

    if (getWifiSocketManager()->wifi_sockets[s].wifi_context == NULL) {
        return -1;
    }

    atiny_net_context *clientid = NULL;
    clientid = malloc(sizeof(atiny_net_context));

    if (clientid == NULL) {
        return -1;
    }

    memset(clientid, 0, sizeof(atiny_net_context));
    ret = atiny_net_accept(&getWifiSocketManager()->wifi_sockets[s].wifi_context, clientid, addr, 0, 0);

    if (ret != 0) {
        return -1;
    }

    if (clientid != NULL) {
        int socket_id = getFreeID();

        if (getWifiSocketManager()->wifi_sockets[socket_id].wifi_context != NULL) {
            free(getWifiSocketManager()->wifi_sockets[socket_id].wifi_context);
            getWifiSocketManager()->wifi_sockets[socket_id].wifi_context = NULL;
        }

        getWifiSocketManager()->wifi_sockets[socket_id].wifi_context = clientid;
        getWifiSocketManager()->wifi_sockets[socket_id].wifi_context->type = ATINY_SOCKET_TYPE_CLIENT;
        return id_convert_ext(socket_id, 1);
    }

    return -1;
cat1Return:
#endif
    ret = lwip_accept(s, addr, addrlen);
    errno = lwip_getsockerrno(s);
    return id_convert_ext(ret, 0);
}

int bind(int s, const struct sockaddr *name, socklen_t namelen)
{
    int ret = -1;
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET

    if (is_wifi_socket_id(s) == 0) {
        s = id_convert_inter(s);
        goto cat1Return;
    }

    s = id_convert_inter(s);

    if (wifi_socket_is_enable() != 1) {
        goto cat1Return;
    }

    if (s < 0 || s > SOCKET_NUM_MAX) {
        return -1;
    }

    if (!isIDOccupied(s)) {
        return -1;
    }

    if (getWifiSocketManager()->wifi_sockets[s].wifi_context == NULL) {
        return -1;
    }

    getWifiSocketManager()->wifi_sockets[s].wifi_context->type = ATINY_SOCKET_TYPE_SERVER;
    getWifiSocketManager()->wifi_sockets[s].con_context.port = ((struct sockaddr_in *)name)->sin_port;
    getWifiSocketManager()->wifi_sockets[s].con_context.family = ((struct sockaddr_in *)name)->sin_family;

    char host[20];
    char port[10];
    int port_int = 0;
    memset(host, 0, sizeof(host));
    memset(port, 0, sizeof(port));
    port_int = ntohs(((struct sockaddr_in *)name)->sin_port);
    inet_ntop(((struct sockaddr_in *)name)->sin_family, (const void *)(&((struct sockaddr_in *)name)->sin_addr), host,
              sizeof(host));
    sprintf(port, "%d", port_int);

    if (((atiny_net_context *)atiny_net_bind(host, port, name->sa_family)) != 0) {
        return -1;
    }

    return 0;

cat1Return:
#endif

    ret = lwip_bind(s, name, namelen);
    errno = lwip_getsockerrno(s);
    return ret;
}

int shutdown(int s, int how)
{
    int ret = 0;
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET

    if (is_wifi_socket_id(s) == 0) {
        s = id_convert_inter(s);
        goto cat1Return;
    }

    s = id_convert_inter(s);

    return -1;
cat1Return:
#endif
    ret = lwip_shutdown(s, how);
    errno = lwip_getsockerrno(s);
    return ret;
}

int closesocket(int s)
{
    int ret = 0;
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET

    if (is_wifi_socket_id(s) == 0) {
        s = id_convert_inter(s);
        goto cat1Return;
    }

    s = id_convert_inter(s);

    if (wifi_socket_is_enable() != 1) {
        return -1;
    }

    if (s < 0 || s > SOCKET_NUM_MAX) {
        return -1;
    }

    if (!isIDOccupied(s)) {
        return -1;
    }

    if (getWifiSocketManager()->wifi_sockets[s].wifi_context == NULL) {
        return -1;
    }

    if (getWifiSocketManager()->wifi_sockets[s].wifi_context->type == ATINY_SOCKET_TYPE_SERVER) {
        ret = atiny_net_server_close();
        // SocketDebug("close server socket");
    } else {
        ret = atiny_net_close(getWifiSocketManager()->wifi_sockets[s].wifi_context);
        // SocketDebug("close client socket");
    }

    SocketDebug("close socket[%d]", s);
    free(getWifiSocketManager()->wifi_sockets[s].wifi_context);
    getWifiSocketManager()->wifi_sockets[s].wifi_context = NULL;
    releaseID(s);
    return ret;
cat1Return:
#endif
    ret = lwip_close(s);
    errno = lwip_getsockerrno(s);
    return ret;
}

int connect(int s, const struct sockaddr *name, socklen_t namelen)
{
    struct sockaddr lwip_addr = {0};
    int ret = 0;
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET

    SocketDebug("ext id [%d]", s);

    if (is_wifi_socket_id(s) == 0) {
        SocketDebug("socket id is not support wifi");
        s = id_convert_inter(s);
        goto cat1Return;
    }

    s = id_convert_inter(s);

    SocketDebug("inter id [%d]", s);

    if (wifi_socket_is_enable() != 1) {
        SocketDebug("control value not support wifi");
        return -1;
    }

    if (s < 0 || s > SOCKET_NUM_MAX) {
        return -1;
    }

    if (!isIDOccupied(s)) {
        return -1;
    }

    if (getWifiSocketManager()->wifi_sockets[s].wifi_context == NULL) {
        return -1;
    }

    getWifiSocketManager()->wifi_sockets[s].con_context.port = ((struct sockaddr_in *)name)->sin_port;
    getWifiSocketManager()->wifi_sockets[s].con_context.family = ((struct sockaddr_in *)name)->sin_family;

    char host[20];
    char port[10];
    int port_int = 0;
    memset(host, 0, sizeof(host));
    memset(port, 0, sizeof(port));
    port_int = ntohs(((struct sockaddr_in *)name)->sin_port);
    inet_ntop(((struct sockaddr_in *)name)->sin_family, (const void *)(&((struct sockaddr_in *)name)->sin_addr), host,
              sizeof(host));
    sprintf(port, "%d", port_int);

    if (atiny_net_connect(getWifiSocketManager()->wifi_sockets[s].wifi_context, host, port,
                          getWifiSocketManager()->wifi_sockets[s].con_context.protocol) != 0) {
        //WIFI ����ʧ�ܣ��ͷ���Դ
        free(getWifiSocketManager()->wifi_sockets[s].wifi_context);
        getWifiSocketManager()->wifi_sockets[s].wifi_context = NULL;
        releaseID(s);
        return -1;
    }

    return 0;

cat1Return:
    memcpy(&lwip_addr, name, sizeof(struct sockaddr));

    if (((struct sockaddr_in *)name)->sin_family == AF_INET) {
        lwip_addr.sa_family = 0x210;
    } else if (((struct sockaddr_in *)name)->sin_family == AF_INET6) {
        lwip_addr.sa_family = 0x1018;
    }

#endif
    memcpy(&lwip_addr, name, sizeof(struct sockaddr));

    if (name->sa_family == AF_INET) {
        lwip_addr.sa_family = 0x0210;
    } else if (name->sa_family == AF_INET6) {
        lwip_addr.sa_family = 0x1018;
    }

    ret = lwip_connect(s, &lwip_addr, namelen);
    errno = lwip_getsockerrno(s);
    return ret;
}

int getsockname(int s, struct sockaddr *name, socklen_t *namelen)
{
    int ret = 0;
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET

    if (is_wifi_socket_id(s) == 0) {
        s = id_convert_inter(s);
        goto cat1Return;
    }

    s = id_convert_inter(s);

    return -1;
cat1Return:
#endif
    ret = lwip_getsockname(s, name, namelen);
    errno = lwip_getsockerrno(s);
    return ret;
}

int getpeername(int s, struct sockaddr *name, socklen_t *namelen)
{
    int ret = 0;
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET

    if (is_wifi_socket_id(s) == 0) {
        s = id_convert_inter(s);
        goto cat1Return;
    }

    s = id_convert_inter(s);

    return -1;
cat1Return:
#endif
    ret = lwip_getsockname(s, name, namelen);
    errno = lwip_getsockerrno(s);
    return ret;
}

int setsockopt(int s, int level, int optname, const void *optval, socklen_t optlen)
{
    int ret = -1;
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET

    if (is_wifi_socket_id(s) == 0) {
        s = id_convert_inter(s);
        goto cat1Return;
    }

    s = id_convert_inter(s);

    if (wifi_socket_is_enable() != 1) {
        return -1;
    }

    if (s < 0 || s > SOCKET_NUM_MAX) {
        return -1;
    }

    if (!isIDOccupied(s)) {
        return -1;
    }

    if (getWifiSocketManager()->wifi_sockets[s].wifi_context == NULL) {
        return -1;
    }

    if (optname == SO_SNDTIMEO) {
        int time_ms = (*(struct timeval *)optval).tv_sec * 1000;
        time_ms += (*(struct timeval *)optval).tv_usec / 1000;
        int opt[2];
        opt[0] = s;
        opt[1] = time_ms;
        wifi_set_opt(3, opt, sizeof(opt));
    } else if (optname == SO_RCVTIMEO) {
        int time_ms = (*(struct timeval *)optval).tv_sec * 1000;
        time_ms += (*(struct timeval *)optval).tv_usec / 1000;
        int opt[2];
        opt[0] = s;
        opt[1] = time_ms;
        wifi_set_opt(2, opt, sizeof(opt));
    }

    return 0;

cat1Return:
#endif
    ret = lwip_setsockopt(s, level, optname, optval, optlen);
    errno = lwip_getsockerrno(s);
    return ret;
}

int getsockopt(int s, int level, int optname, void *optval, socklen_t *optlen)
{
    int ret = 0;
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET

    if (is_wifi_socket_id(s) == 0) {
        s = id_convert_inter(s);
        goto cat1Return;
    }

    s = id_convert_inter(s);

    return -1;
cat1Return:
#endif
    ret = lwip_getsockopt(s, level, optname, optval, optlen);
    errno = lwip_getsockerrno(s);
    return ret;
}

int listen(int s, int backlog)
{
    int ret = -1;
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET

    if (is_wifi_socket_id(s) == 0) {
        s = id_convert_inter(s);
        goto cat1Return;
    }

    s = id_convert_inter(s);

    if (wifi_socket_is_enable() != 1) {
        return -1;
    }

    if (s < 0 || s > SOCKET_NUM_MAX) {
        return -1;
    }

    if (!isIDOccupied(s)) {
        return -1;
    }

    if (getWifiSocketManager()->wifi_sockets[s].wifi_context == NULL) {
        return -1;
    }

    return 0;
cat1Return:
#endif
    ret = lwip_listen(s, backlog);
    errno = lwip_getsockerrno(s);
    return ret;
}

int recv(int s, void *mem, size_t len, int flags)
{
    int ret = 0;
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET

    if (is_wifi_socket_id(s) == 0) {
        s = id_convert_inter(s);
        goto cat1Return;
    }

    s = id_convert_inter(s);

    if (wifi_socket_is_enable() != 1) {
        errno = ECONNRESET;
        return -1;
    }

    if (s < 0 || s > SOCKET_NUM_MAX) {
        errno = ECONNRESET;
        return -1;
    }

    if (!isIDOccupied(s)) {
        errno = ECONNRESET;
        return -1;
    }

    if (getWifiSocketManager()->wifi_sockets[s].wifi_context == NULL) {
        errno = ECONNRESET;
        return -1;
    }

    ret = atiny_net_recv(getWifiSocketManager()->wifi_sockets[s].wifi_context, mem, len);
    if (ret == -1) {
        errno = EAGAIN;
    }

    return ret;

cat1Return:
#endif

    ret = lwip_recv(s, mem, len, flags);
    errno = lwip_getsockerrno(s);
    //RTI_LOG("RECV ERROR:%d", errno);
    return ret;
}

int recvfrom(int s, void *mem, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen)
{
    int ret = 0;
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET

    if (is_wifi_socket_id(s) == 0) {
        s = id_convert_inter(s);
        goto cat1Return;
    }

    s = id_convert_inter(s);

    return -1;
cat1Return:
#endif
    ret = lwip_recvfrom(s, mem, len, flags, from, fromlen);
    errno = lwip_getsockerrno(s);
    return ret;
}

int send(int s, const void *dataptr, size_t size, int flags)
{
    int ret = 0;
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET

    if (is_wifi_socket_id(s) == 0) {
        s = id_convert_inter(s);
        goto cat1Return;
    }

    s = id_convert_inter(s);

    if (wifi_socket_is_enable() != 1) {
        errno = ECONNRESET;
        return -1;
    }

    if (s < 0 || s > SOCKET_NUM_MAX) {
        errno = ECONNRESET;
        return -1;
    }

    if (!isIDOccupied(s)) {
        errno = ECONNRESET;
        return -1;
    }

    if (getWifiSocketManager()->wifi_sockets[s].wifi_context == NULL) {
        errno = ECONNRESET;
        return -1;
    }

    ret = atiny_net_send(getWifiSocketManager()->wifi_sockets[s].wifi_context, dataptr, size);
    if (ret == -1) {
        errno = EAGAIN;
    }

    return ret;

cat1Return:
#endif
    ret = lwip_send(s, dataptr, size, flags);
    errno = lwip_getsockerrno(s);
    return ret;
}

int sendto(int s, const void *dataptr, size_t size, int flags, const struct sockaddr *to, socklen_t tolen)
{
    int ret = 0;
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET

    if (is_wifi_socket_id(s) == 0) {
        s = id_convert_inter(s);
        goto cat1Return;
    }

    s = id_convert_inter(s);

    return -1;
cat1Return:
#endif
    ret = lwip_sendto(s, dataptr, size, flags, to, tolen);
    errno = lwip_getsockerrno(s);
    return ret;
}

int socket(int domain, int type, int protocol)
{
    int ret = 0;
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET

    if (wifi_socket_is_enable() != 1) {
        SocketDebug("create LTE Socket");
        goto cat1Return;
    }

    SocketDebug("create WIFI Socket");

    int socket_id = getFreeID();

    if (socket_id < 0 || socket_id > SOCKET_NUM_MAX) {
        goto cat1Return; // use LTE socket
    }

    getWifiSocketManager()->wifi_sockets[socket_id].con_context.type = type;
    getWifiSocketManager()->wifi_sockets[socket_id].con_context.protocol = protocol;

    if (getWifiSocketManager()->wifi_sockets[socket_id].wifi_context != NULL) {
        SocketDebug("free id[%d][%x]", socket_id, getWifiSocketManager()->wifi_sockets[socket_id].wifi_context);
        free(getWifiSocketManager()->wifi_sockets[socket_id].wifi_context);
        getWifiSocketManager()->wifi_sockets[socket_id].wifi_context = NULL;
    }

    getWifiSocketManager()->wifi_sockets[socket_id].wifi_context = (atiny_net_context *)malloc(sizeof(atiny_net_context));

    if (getWifiSocketManager()->wifi_sockets[socket_id].wifi_context == NULL) {
        releaseID(socket_id);
        return -1;
    }

    getWifiSocketManager()->wifi_sockets[socket_id].wifi_context->fd = -1;
    getWifiSocketManager()->wifi_sockets[socket_id].wifi_context->type = ATINY_SOCKET_TYPE_CLIENT;
    return id_convert_ext(socket_id, 1);

cat1Return:
#endif

    ret = lwip_socket(domain, type, protocol);
    errno = lwip_getsockerrno(0);
    return id_convert_ext(ret, 0);
}

int select(int maxfdp1, void *readset, void *writeset, void *exceptset, struct timeval *timeout)
{
    int ret = 0;
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET

#endif
    ret = lwip_select(maxfdp1, readset, writeset, exceptset, timeout);
    errno = lwip_getsockerrno(0);
    return ret;
}

int ioctlsocket(int s, long cmd, void *argp)
{
    int ret = 0;
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET

    if (is_wifi_socket_id(s) == 0) {
        s = id_convert_inter(s);
        goto cat1Return;
    }

    s = id_convert_inter(s);

    return -1;
cat1Return:
#endif
    ret = lwip_ioctl(s, cmd, argp);
    errno = lwip_getsockerrno(s);
    return ret;
}

struct hostent *gethostbyname_safe(const char *name, void *host_buf, int len)
{

    struct hostent *result_buf = malloc(sizeof(struct hostent));
    struct hostent *result;
    int err;
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET

    if (wifi_socket_is_enable() != 1) {
        goto cat1Return;
    }

    memset(result_buf, 0, sizeof(struct hostent));
    memset(host_buf, 0, len);
    result_buf->h_name = (char *)name;
    result_buf->h_addrtype = AF_INET;
    result_buf->h_addr_list = (char **)host_buf;
    result_buf->h_addr_list[0] = (char *)(host_buf + 8);
    int32_t ret = atiny_net_get_host_ip_by_name((uint8_t *)name, (uint32_t *)result_buf->h_addr_list[0]);

    if (ret != 0) {
        //errno =
        free(result_buf);
        return NULL;
    }

    return result_buf;

cat1Return:
#endif
    err = lwip_gethostbyname_r(name, result_buf, host_buf, len, &result, &err);

    if (err == 0 && result != NULL) {
        return result_buf;
    }

    return NULL;
}



int getaddrinfo(const char *nodename, const char *servname,
                const struct addrinfo *hints, struct addrinfo **res) {

    
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET
    struct addrinfo *ai;
    struct sockaddr_in *sin;
    if (wifi_socket_is_enable() != 1) {
        goto cat1Return;
    }
    int port = atoi(servname);
    // 分配 addrinfo 结构
    ai = (struct addrinfo *)malloc(sizeof(struct addrinfo));
    if (!ai) {
        return 203;
    }
    memset(ai, 0, sizeof(struct addrinfo));

    // 分配 sockaddr_in 结构
    sin = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in));
    if (!sin) {
        free(ai);
        return 203;
    }
    memset(sin, 0, sizeof(struct sockaddr_in));

    // 填充 sockaddr_in 结构
    sin->sin_family = AF_INET;
    sin->sin_port = ((port & 0xff) << 8) | ((port & 0xff00) >> 8);
    int32_t ret = atiny_net_get_host_ip_by_name((uint8_t *)nodename, (uint32_t *)&sin->sin_addr);

    if (ret != 0) {
        //errno =
        free(ai);
        free(sin);
        return 201;
    }
    

    // 填充 addrinfo 结构
    ai->ai_family = AF_INET;
    ai->ai_socktype = hints ? hints->ai_socktype : 0;
    ai->ai_protocol =  0;
    ai->ai_addrlen = sizeof(struct sockaddr_in);
    ai->ai_addr = (struct sockaddr *)sin;
    ai->ai_next = NULL;

    *res = ai;
    return 0;
cat1Return:
#endif
    return lwip_getaddrinfo(nodename, servname, hints, res);
}

void freeaddrinfo(struct addrinfo *ai) {
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET
    if (wifi_socket_is_enable() != 1) {
        goto cat1Return;
    }
    while (ai) {
        struct addrinfo *next = ai->ai_next;
        if (ai->ai_addr) {
            free(ai->ai_addr);
        }
        ai = next;
    }
    free(ai);
    return;
cat1Return:
    #endif
    lwip_freeaddrinfo(ai);
}

int getsockerrno(int s){
    return errno;
}