// Common definitions
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdint.h>

// abstraction over concrete socket types
enum socket_type {
    STYPE_TCP,
    STYPE_UDP,
    STYPE_UNIX
};

// data type to abstract the supported socket designators

// #pragma pack push
// #pragma pack(1)
struct socket_uri {
    enum socket_type type;
    union _sdata {
        struct _tcpudp_addr {       // designates host:port or ip:port pair
            in_port_t port;
            struct in_addr ip;
            char *host;
        };
        const char *path;
    };
};
// #pragma pack pop
