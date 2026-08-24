
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include "arpa/inet.h"
#include "netinet/in.h"
#include "sys/socket.h"

in_addr_t inet_addr(const char *cp)
{
    struct in_addr in;

    if (inet_aton(cp, &in)) {
        return in.s_addr;
    }

    return INADDR_NONE;
}

char *inet_ntoa(struct in_addr in)
{
    static char buf[INET_ADDRSTRLEN];
    return (char *)inet_ntop(AF_INET, &in, buf, INET_ADDRSTRLEN);
}

// const char *inet_ntop(int af, const void *src, char *dst, socklen_t size) {
// if (af == AF_INET) {
// return inet_ntoa(*(struct in_addr *)src);
// }
// return NULL;
// }

// int inet_pton(int af, const char *src, void *dst) {
// if (af == AF_INET) {
// return inet_aton(src, (struct in_addr *)dst);
// }
// return 0;
// }

int ascii2addr(int af, const char *src, void *dst)
{
    return inet_pton(af, src, dst);
}

char *addr2ascii(int af, const void *src, int len, char *dst)
{
    return (char *)inet_ntop(af, src, dst, len);
}

int inet_aton(const char *cp, struct in_addr *inp)
{
    return inet_pton(AF_INET, cp, inp);
}

in_addr_t inet_lnaof(struct in_addr in)
{
    return in.s_addr &INADDR_BROADCAST;
}

struct in_addr inet_makeaddr(in_addr_t net, in_addr_t host)
{
    struct in_addr addr;
    addr.s_addr = (net &IN_CLASSA_NET) | (host &IN_CLASSA_HOST);
    return addr;
}

char *inet_neta(in_addr_t net, char *buf, size_t size)
{
    return (char *)inet_ntop(AF_INET, &net, buf, size);
}

in_addr_t inet_netof(struct in_addr in)
{
    return in.s_addr &IN_CLASSA_NET;
}

in_addr_t inet_network(const char *cp)
{
    struct in_addr in;

    if (inet_aton(cp, &in)) {
        return in.s_addr;
    }

    return INADDR_NONE;
}

char *inet_net_ntop(int af, const void *src, int bits, char *dst, size_t size)
{
    return (char *)inet_ntop(af, src, dst, size);
}

int inet_net_pton(int af, const char *src, void *dst, size_t size)
{
    return inet_pton(af, src, dst);
}

unsigned inet_nsap_addr(const char *ascii, unsigned char *binary, int maxlen)
{
    // This is a simplified example. Real implementation may vary.
    int len = strlen(ascii);

    if (len > maxlen) {
        return 0;
    }

    memcpy(binary, ascii, len);
    return len;
}

char *inet_nsap_ntoa(int len, const unsigned char *binary, char *ascii)
{
    // This is a simplified example. Real implementation may vary.
    strncpy(ascii, (char *)binary, len);
    ascii[len] = '\0';
    return ascii;
}

unsigned long int htonl(unsigned long int h)
{
    return ((h & 0xff) << 24) |
           ((h & 0xff00) << 8) |
           ((h & 0xff0000UL) >> 8) |
           ((h & 0xff000000UL) >> 24);
}

unsigned short int htons(unsigned short int h)
{
    return ((h & 0xff) << 8) | ((h & 0xff00) >> 8);
}

unsigned long int ntohl(unsigned long int n)
{
    return htonl(n);
}

unsigned short int ntohs(unsigned short int n)
{
    return htons(n);
}
