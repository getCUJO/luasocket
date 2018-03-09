#ifndef NETLINK_H
#define NETLINK_H

/*=========================================================================*\
 * * NETLINK object
 * * LuaSocket toolkit
 * *
 * * The netlink.h module provides LuaSocket with support for netlink protocol
 * * (AF_NETLINK, SOCK_RAW).
 * *
 * * Two classes are defined: connected and unconnected. Netlink objects are
 * * originally unconnected. They can be "connected" to a given address
 * * with a call to the setpeername function. The same function can be used to
 * * break the connection.
\*=========================================================================*/

#include "timeout.h"
#include "lua.h"
#include "socket.h"

#define MAX_PAYLOAD 2048

#ifndef NETLINK_API
#define NETLINK_API
#endif

typedef int t_pid;
typedef int t_groups;
typedef int t_type;
typedef int t_seq;

typedef struct {
    t_socket fd; 
    t_timeout tm;  
    t_pid srcpid; 
    t_type type; 
    t_seq seq;
} t_netlink;
typedef t_netlink *p_netlink;

NETLINK_API int luaopen_socket_netlink(lua_State *L);

#endif /* NETLINK_H */
