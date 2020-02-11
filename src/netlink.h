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

#include <linux/netlink.h>
#include <linux/genetlink.h>

#include "timeout.h"
#include "lua.h"
#include "socket.h"

#define MAX_PAYLOAD 65536

typedef int t_pid;
typedef int t_groups;
typedef int t_type;

struct nlmsgbuf{
    struct nlmsghdr hdr;
    char msg[NLMSG_ALIGN(MAX_PAYLOAD)] __attribute__((aligned(NLMSG_ALIGNTO)));
};

struct nlgenmsgbuf {
	struct nlmsghdr n;
	struct genlmsghdr g;
	char msg[NLMSG_ALIGN(MAX_PAYLOAD)] __attribute__((aligned(NLMSG_ALIGNTO)));
};

typedef struct {
    t_socket fd; 
    t_timeout tm;  
    t_pid srcpid; 
    t_type type; 
    struct nlmsgbuf *nlb;
    int nl_family_id;
    struct nlgenmsgbuf *nlgb;
} t_netlink;
typedef t_netlink *p_netlink;

int netlink_open(lua_State *L);

#endif /* NETLINK_H */
