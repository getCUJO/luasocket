/*=========================================================================*\
* Netlink socket submodule
* LuaSocket toolkit
\*=========================================================================*/
#include <string.h>
#include <stdlib.h>

#include "netlink.h"
#include "timeout.h"
#include "lua.h"
#include "lauxlib.h"
#include "compat.h"

#include "auxiliar.h"
#include "options.h"
#include "socket.h"

/*=========================================================================*\
* Internal function prototypes
\*=========================================================================*/
static int global_create(lua_State *L);
static int meth_bind(lua_State *L);
static int meth_sendto(lua_State *L);
static int meth_send(lua_State *L);
static int meth_receivefrom(lua_State *L);
static int meth_receive(lua_State *L);
static int meth_close(lua_State *L);
static int meth_settimeout(lua_State *L);
static int meth_gettimeout(lua_State *L);
static int meth_setoption(lua_State *L);
static int meth_getoption(lua_State *L);
static int meth_getfd(lua_State *L);
static int meth_setfd(lua_State *L);
static int meth_getpeername(lua_State *L);
static int meth_setpeername(lua_State *L);
static int meth_getsockpid(lua_State *L);
static int meth_sendto_generic_nflua(lua_State *L);
static int meth_receivefrom_generic_nflua(lua_State *L);

static const char *netlink_trybind(p_netlink nl, int grp);

#define GENLMSG_DATA(glh) ((void *)(NLMSG_DATA(glh) + GENL_HDRLEN))
#define NLA_DATA(na) ((void *)((char*)(na) + NLA_HDRLEN))

/* netlink object methods */
static luaL_Reg netlink_methods[] = {
    {"__gc",        meth_close},
    {"__tostring",  auxiliar_tostring},
    {"bind",        meth_bind},
    {"close",       meth_close},
    {"getfd",       meth_getfd},
    {"send",        meth_send},
    {"sendto",      meth_sendto},
    {"receivefrom", meth_receivefrom},
    {"receive",     meth_receive},
    {"setfd",       meth_setfd},
    {"settimeout",  meth_settimeout},
    {"gettimeout",  meth_gettimeout},
    {"setoption",   meth_setoption},
    {"getoption",   meth_getoption},
    {"setpeername", meth_setpeername},
    {"getpeername", meth_getpeername},
    {"getsockpid",  meth_getsockpid},
    {"sendtogennflua", meth_sendto_generic_nflua},
    {"receivefromgen", meth_receivefrom_generic_nflua},
    {NULL,          NULL}
};

/* socket options for setoption */
static t_opt optset[] = {
    {NULL,          NULL}
};

/* socket options for getoption */
static t_opt optget[] = {
    {NULL,     NULL}
};

/* functions in library namespace */
static luaL_Reg func[] = {
    {"netlink", global_create},
    {NULL, NULL}
};

/*-------------------------------------------------------------------------*\
* Initializes module
\*-------------------------------------------------------------------------*/
int netlink_open(lua_State *L) {
    /* create classes */
    auxiliar_newclass(L, "netlink{connected}", netlink_methods);
    auxiliar_newclass(L, "netlink{unconnected}", netlink_methods);
    /* create class groups */
    auxiliar_add2group(L, "netlink{connected}", "netlink{any}");
    auxiliar_add2group(L, "netlink{unconnected}", "netlink{any}");
    luaL_setfuncs(L, func, 0);
    return 0;
}

/*=========================================================================*\
* Lua methods
\*=========================================================================*/
/*-------------------------------------------------------------------------*\
* Send data through connected netlink socket
\*-------------------------------------------------------------------------*/
static int meth_send(lua_State *L) {
    p_netlink nl = (p_netlink)auxiliar_checkclass(L, "netlink{connected}", 1);
    size_t payload_size;
    const char *payload = luaL_checklstring(L, 2, &payload_size);
    int flags = luaL_optinteger(L, 3, 0);
    p_timeout tm = &nl->tm;
    size_t sent = 0;
    int err;

    if (payload_size > NLMSG_ALIGN(MAX_PAYLOAD)) {
        lua_pushnil(L);
        lua_pushliteral(L, "payload too big");
        return 2;
    }

    nl->nlb->hdr = (struct nlmsghdr) {
        .nlmsg_len = NLMSG_LENGTH(payload_size),
        .nlmsg_pid = nl->srcpid,
        .nlmsg_flags = flags
    };
    memcpy(NLMSG_DATA(nl->nlb), payload, payload_size);
    timeout_markstart(tm);

    err = socket_send(&nl->fd, (const char *)nl->nlb, NLMSG_SPACE(payload_size), &sent, tm);

    if (err != IO_DONE) {
        lua_pushnil(L);
        lua_pushliteral(L, "error sending message");
        return 2;
    }

    lua_pushinteger(L, sent);
    return 1;
}

/*-------------------------------------------------------------------------*\
* Send data through unconnected netlink socket
\*-------------------------------------------------------------------------*/
static int meth_sendto(lua_State *L) {
    p_netlink nl = (p_netlink)auxiliar_checkclass(L, "netlink{unconnected}", 1);
    size_t payload_size;
    const char *payload = luaL_checklstring(L, 2, &payload_size);
    int dstpid = luaL_checkinteger(L, 3);
    int groups = luaL_optinteger(L, 4, 0);
    int flags = luaL_optinteger(L, 5, 0);

    struct sockaddr_nl addr;
    p_timeout tm = &nl->tm;
    size_t sent = 0;
    int err;

    if (payload_size > NLMSG_ALIGN(MAX_PAYLOAD)) {
        lua_pushnil(L);
        lua_pushliteral(L, "payload too big");
        return 2;
    }

    memset(&addr, 0, sizeof(addr));
    addr.nl_pid = dstpid;
    addr.nl_family = AF_NETLINK;
    addr.nl_groups = groups;

    nl->nlb->hdr = (struct nlmsghdr) {
        .nlmsg_len = NLMSG_LENGTH(payload_size),
        .nlmsg_pid = nl->srcpid,
        .nlmsg_flags = flags
    };
    memcpy(NLMSG_DATA(nl->nlb), payload, payload_size);
    timeout_markstart(tm);

    err = socket_sendto(&nl->fd, (const char *)nl->nlb, NLMSG_SPACE(payload_size), &sent,
            (SA *)&addr, sizeof(addr), tm);

    if (err != IO_DONE) {
        lua_pushnil(L);
        lua_pushliteral(L, "error sending message");
        return 2;
    }

    lua_pushinteger(L, sent);
    return 1;
}

/*-------------------------------------------------------------------------*\
* Receives data from a netlink socket
\*-------------------------------------------------------------------------*/
static int meth_receive(lua_State *L) {
    p_netlink nl = (p_netlink)auxiliar_checkclass(L, "netlink{connected}", 1);
    size_t got;
    size_t payload_size;
    p_timeout tm = &nl->tm;
    int err;

    timeout_markstart(tm);
    err = socket_recv(&nl->fd, (char *)nl->nlb, NLMSG_SPACE(MAX_PAYLOAD), &got, tm);
    if (err != IO_DONE && err != IO_CLOSED) {
        lua_pushnil(L);
        lua_pushstring(L, socket_strerror(err));
        return 2;
    }

    payload_size = got < nl->nlb->hdr.nlmsg_len ? MAX_PAYLOAD :
        NLMSG_PAYLOAD(&nl->nlb->hdr, 0);

    lua_pushinteger(L, payload_size);
    lua_pushlstring(L, NLMSG_DATA(nl->nlb), payload_size);
    return 2;
}

/*-------------------------------------------------------------------------*\
* Receives data from a netlink socket
\*-------------------------------------------------------------------------*/
static int meth_receivefrom(lua_State *L) {
    p_netlink nl = (p_netlink)auxiliar_checkclass(L, "netlink{unconnected}", 1);
    struct sockaddr_nl dst;
    size_t got;
    size_t payload_size;
    p_timeout tm = &nl->tm;
    int err;

    socklen_t len = sizeof(dst);
    timeout_markstart(tm);
    err = socket_recvfrom(&nl->fd, (char *)nl->nlb, NLMSG_SPACE(MAX_PAYLOAD), &got,
            (SA *)&dst, &len, tm);
    if (err != IO_DONE && err != IO_CLOSED) {
        lua_pushnil(L);
        lua_pushstring(L, socket_strerror(err));
        return 2;
    }

    payload_size = got < nl->nlb->hdr.nlmsg_len ? MAX_PAYLOAD :
        NLMSG_PAYLOAD(&nl->nlb->hdr, 0);

    lua_pushinteger(L, payload_size);
    lua_pushlstring(L, NLMSG_DATA(nl->nlb), payload_size);
    lua_pushinteger(L, nl->nlb->hdr.nlmsg_pid);
    return 3;
}

/*-------------------------------------------------------------------------*\
* Select support methods
\*-------------------------------------------------------------------------*/
static int meth_getfd(lua_State *L) {
    p_netlink nl = (p_netlink)auxiliar_checkgroup(L, "netlink{any}", 1);
    lua_pushnumber(L, (int)nl->fd);
    return 1;
}

/* this is very dangerous, but can be handy for those that are brave enough */
static int meth_setfd(lua_State *L) {
    p_netlink nl = (p_netlink)auxiliar_checkgroup(L, "netlink{any}", 1);
    nl->fd = (t_socket)luaL_checknumber(L, 2);
    return 0;
}

static int meth_getsockpid(lua_State *L) {
    p_netlink nl = (p_netlink)auxiliar_checkgroup(L, "netlink{any}", 1);
    lua_pushinteger(L, nl->srcpid);
    return 1;
}

/*-------------------------------------------------------------------------*\
* Binds an object to an address
\*-------------------------------------------------------------------------*/
static const char *netlink_trybind(p_netlink nl, int grp) {
    struct sockaddr_nl addr;
    int err;

    memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;
    addr.nl_groups = grp;
    addr.nl_pid = nl->srcpid;
    getpeername(nl->fd, (SA *)&addr, (socklen_t *)sizeof(addr));
    err = socket_bind(&nl->fd, (SA *)&addr, sizeof(addr));
    return socket_strerror(err);
}

static int meth_bind(lua_State *L) {
    int pid = luaL_checkinteger(L, 2);
    int grp = luaL_optinteger(L, 3, 0);
    const char *err;
    p_netlink nl = (p_netlink)auxiliar_checkclass(L, "netlink{unconnected}", 1);

    nl->srcpid = pid;
    err = netlink_trybind(nl, grp);
    if (err != NULL) {
        lua_pushnil(L);
        lua_pushstring(L, err);
        return 2;
    }

    lua_pushnumber(L, 1);
    return 1;
}


/*-------------------------------------------------------------------------*\
* Closes socket used by object
\*-------------------------------------------------------------------------*/
static int meth_close(lua_State *L) {
    p_netlink nl = (p_netlink)auxiliar_checkgroup(L, "netlink{any}", 1);
    socket_destroy(&nl->fd);
    if (nl->nlb) {
        free(nl->nlb);
        nl->nlb = NULL;
    }
    if (nl->nlgb) {
        free(nl->nlgb);
        nl->nlgb = NULL;
    }
    lua_pushnumber(L, 1);
    return 1;
}

/*-------------------------------------------------------------------------*\
* Just call peername methods
\*-------------------------------------------------------------------------*/
static int meth_setpeername(lua_State *L) {
    p_netlink nl = (p_netlink)auxiliar_checkgroup(L, "netlink{any}", 1);
    struct sockaddr_nl addr;
    int dstpid;
    int grps;
    int err;

    if (lua_isnone(L, 2)) {
        addr.nl_family = AF_UNSPEC;
        socket_connect(&nl->fd, (SA *)&addr,
                (socklen_t)sizeof(addr), &nl->tm);
        auxiliar_setclass(L, "netlink{unconnected}", 1);
        return 0;
    }

    dstpid = luaL_checkinteger(L, 2);
    grps = luaL_optinteger(L, 3, 0);
    addr.nl_pid = dstpid;
    addr.nl_groups = grps;
    addr.nl_family = AF_NETLINK;

    err = socket_connect(&nl->fd, (SA *)&addr, 
            (socklen_t)sizeof(addr), &nl->tm);
    if (err != IO_DONE) {
        lua_pushnil(L);
        lua_pushstring(L, socket_strerror(errno));
        return 2;
    }

    auxiliar_setclass(L, "netlink{connected}", 1);
    lua_pushinteger(L, err);
    return 1;
}

static int meth_getpeername(lua_State *L) {
    p_netlink nl = (p_netlink)auxiliar_checkclass(L, "netlink{connected}", 1);
    struct sockaddr_nl peer;
    socklen_t peer_len = sizeof(peer);

    if (getpeername(nl->fd, (SA *)&peer, &peer_len) < 0) {
        lua_pushnil(L);
        lua_pushstring(L, socket_strerror(errno));
    } else {
        lua_pushinteger(L, peer.nl_groups);
        lua_pushinteger(L, peer.nl_pid);
    }

    return 2;
}
/*-------------------------------------------------------------------------*\
* Just call tm methods
\*-------------------------------------------------------------------------*/
static int meth_settimeout(lua_State *L) {
    p_netlink nl = (p_netlink)auxiliar_checkgroup(L, "netlink{any}", 1);
    return timeout_meth_settimeout(L, &nl->tm);
}

static int meth_gettimeout(lua_State *L) {
    p_netlink nl = (p_netlink)auxiliar_checkgroup(L, "netlink{any}", 1);
    return timeout_meth_gettimeout(L, &nl->tm);
}

/*-------------------------------------------------------------------------*\
* Just call option handler
\*-------------------------------------------------------------------------*/
static int meth_setoption(lua_State *L) {
    p_netlink nl = (p_netlink)auxiliar_checkgroup(L, "netlink{any}", 1);
    return opt_meth_setoption(L, optset, &nl->fd);
}

/*-------------------------------------------------------------------------*\
* Just call option handler
\*-------------------------------------------------------------------------*/
static int meth_getoption(lua_State *L) {
    p_netlink nl = (p_netlink)auxiliar_checkgroup(L, "netlink{any}", 1);
    return opt_meth_getoption(L, optget, &nl->fd);
}

/*=========================================================================*\
* Library functions
\*=========================================================================*/
/*-------------------------------------------------------------------------*\
* Creates a master netlink object
\*-------------------------------------------------------------------------*/
static int global_create(lua_State *L) {
    t_socket sock;
    int prot = luaL_optinteger(L, 1, NETLINK_USERSOCK);
    int err = socket_create(&sock, AF_NETLINK, SOCK_RAW, prot);
    /* try to allocate a system socket */
    if (err == IO_DONE) {
        /* allocate netlink  object */
        p_netlink nl = (p_netlink)lua_newuserdata(L, sizeof(t_netlink));

        if (prot != NETLINK_GENERIC) {
            nl->nlb = malloc(sizeof(struct nlmsgbuf));
            if (!nl->nlb) {
                socket_destroy(&sock);
                lua_pushnil(L);
                lua_pushstring(L, "couldn't allocate buffer for netlink");
                return 2;
            }
        } else {
            nl->nlgb = malloc(sizeof(struct nlgenmsgbuf));
            if (!nl->nlgb) {
                socket_destroy(&sock);
                lua_pushnil(L);
                lua_pushstring(L, "couldn't allocate buffer for gen netlink");
                return 2;
            }
        }
        /* set its type as master object */
        auxiliar_setclass(L, "netlink{unconnected}", -1);
        /* initialize remaining structure fields */
        socket_setnonblocking(&sock);
        nl->fd = sock;
        nl->type = SOCK_RAW;
        timeout_init(&nl->tm, -1, -1);
        nl->nl_family_id = 0;
        return 1;
    }

    lua_pushnil(L);
    lua_pushstring(L, socket_strerror(err));
    return 2;
}

/*-------------------------------------------------------------------------*\
 * Generic netlink socket functions.
 *
 * resolves netlink family id.
\*-------------------------------------------------------------------------*/
static int resolve_nl_family_id(lua_State *L) {
    char family_name[32] = {0};
    struct sockaddr_nl nl_address;
    size_t sent = 0;
    int err;
    struct nlattr *nl_na;
    int nl_family_id = -1;

    sprintf(family_name, "NFLUA");
    p_netlink nl = (p_netlink)auxiliar_checkclass(L, "netlink{unconnected}", 1);
    p_timeout tm = &nl->tm;

    memset(nl->nlgb, 0, sizeof(struct nlgenmsgbuf));

    nl->nlgb->n.nlmsg_type = GENL_ID_CTRL;
    nl->nlgb->n.nlmsg_flags = NLM_F_REQUEST;
    nl->nlgb->n.nlmsg_seq = 0;
    nl->nlgb->n.nlmsg_pid = luaL_checkinteger(L, 3);
    nl->nlgb->n.nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);
    nl->nlgb->g.cmd = CTRL_CMD_GETFAMILY;
    nl->nlgb->g.version = 0x1;

    nl_na = (struct nlattr *) GENLMSG_DATA(nl->nlgb);
    nl_na->nla_type = CTRL_ATTR_FAMILY_NAME;
    nl_na->nla_len = strlen(family_name) + 1 + NLA_HDRLEN;
    strcpy(NLA_DATA(nl_na), family_name);
    nl->nlgb->n.nlmsg_len += NLMSG_ALIGN(nl_na->nla_len);

    memset(&nl_address, 0, sizeof(nl_address));
    nl_address.nl_family = AF_NETLINK;
    nl_address.nl_pid = luaL_checkinteger(L, 3);

    socklen_t len = sizeof(nl_address);
    timeout_markstart(tm);
    err = socket_sendto(&nl->fd, (char *) nl->nlgb, nl->nlgb->n.nlmsg_len, &sent,
            (SA *)&nl_address, len, tm);
    if (err != IO_DONE) {
        goto out;
    }

    memset(nl->nlgb, 0, sizeof(struct nlgenmsgbuf));
    timeout_markstart(tm);
    err = socket_recvfrom(&nl->fd, (char *)nl->nlgb, NLMSG_SPACE(MAX_PAYLOAD) + GENL_HDRLEN, &sent,
            (SA *)&nl_address, &len, tm);
    if (err != IO_DONE && err != IO_CLOSED) {
        goto out;
    }

    if (!NLMSG_OK(&nl->nlgb->n, sent)) {
        goto out;
    }

    if (nl->nlgb->n.nlmsg_type == NLMSG_ERROR) {
        goto out;
    }

    nl_na = (struct nlattr *) GENLMSG_DATA(nl->nlgb);
    /* Skip first message attribute which is family name */
    nl_na = (struct nlattr *) ((char *) nl_na + NLA_ALIGN(nl_na->nla_len));
    if (nl_na->nla_type == CTRL_ATTR_FAMILY_ID) {
        nl_family_id = *(__u16 *) NLA_DATA(nl_na);
    }

out:
    return nl_family_id;
}

static int meth_receivefrom_generic_nflua(lua_State *L) {
    p_netlink nl = (p_netlink)auxiliar_checkclass(L, "netlink{unconnected}", 1);
    size_t got;
    size_t payload_size;
    int err = 0;
    struct sockaddr_nl dst;
    p_timeout tm = &nl->tm;

    memset(&dst, 0, sizeof(struct sockaddr_nl));
    memset(nl->nlgb, 0, sizeof(struct nlgenmsgbuf));

    socklen_t len = sizeof(struct sockaddr_nl);
    timeout_markstart(tm);
    err = socket_recvfrom(&nl->fd, (char *)nl->nlgb, NLMSG_SPACE(MAX_PAYLOAD) + GENL_HDRLEN, &got,
            (SA *)&dst, &len, tm);
    if (err != IO_DONE && err != IO_CLOSED) {
        lua_pushnil(L);
        lua_pushstring(L, socket_strerror(err));
        return 2;
    }

    if (!NLMSG_OK(&nl->nlgb->n, got)) {
        lua_pushnil(L);
        lua_pushliteral(L, "invalid message length");
        return 2;
    }

    struct nlattr *nl_na = (struct nlattr *) GENLMSG_DATA(nl->nlgb);

    if (nl->nlgb->n.nlmsg_type == NLMSG_ERROR) {
        lua_pushnil(L);
        lua_pushfstring(L, "received message error: %s", (char *)NLA_DATA(nl_na));
        return 2;
    }

    payload_size = nl_na->nla_len - NLA_HDRLEN;

    lua_pushinteger(L, payload_size);
    lua_pushlstring(L, (char *)NLA_DATA(nl_na), payload_size);
    lua_pushinteger(L, nl->nlgb->n.nlmsg_pid);

    return 3;
}

/*-------------------------------------------------------------------------*\
 * Send data through unconnected generic netlink socket
\*-------------------------------------------------------------------------*/
static int meth_sendto_generic_nflua(lua_State *L) {
    struct sockaddr_nl nl_address;
    struct nlattr *nl_na;
    size_t sent = 0;
    int err;

    p_netlink nl = (p_netlink)auxiliar_checkclass(L, "netlink{unconnected}", 1);

    if (nl->nl_family_id == 0) {
        nl->nl_family_id = resolve_nl_family_id(L);
        if (nl->nl_family_id < 0) {
            lua_pushnil(L);
            lua_pushliteral(L, "error resolving family id");
            return 2;
        }
    }

    size_t payload_size;
    const char *payload = luaL_checklstring(L, 2, &payload_size);

    if (payload_size > (MAX_PAYLOAD - GENL_HDRLEN)) {
        lua_pushnil(L);
        lua_pushliteral(L, "payload too large");
        return 2;
    }

    int dstpid = luaL_checkinteger(L, 3);
    p_timeout tm = &nl->tm;
    memset(nl->nlgb, 0, sizeof(struct nlgenmsgbuf));

    nl->nlgb->n.nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);
    nl->nlgb->n.nlmsg_type = nl->nl_family_id;
    nl->nlgb->n.nlmsg_flags = NLM_F_REQUEST;
    nl->nlgb->n.nlmsg_seq = 1;
    nl->nlgb->n.nlmsg_pid = dstpid;
    nl->nlgb->g.cmd = 1; // GENL_NFLUA_MSG

    nl_na = (struct nlattr *) GENLMSG_DATA(nl->nlgb);
    nl_na->nla_type = 1; // GENL_NFLUA_ATTR_MSG
    nl_na->nla_len = payload_size+NLA_HDRLEN;
    memcpy(NLA_DATA(nl_na), payload, payload_size);

    nl->nlgb->n.nlmsg_len += NLMSG_ALIGN(nl_na->nla_len);

    memset(&nl_address, 0, sizeof(nl_address));
    nl_address.nl_family = AF_NETLINK;
    nl_address.nl_pid = dstpid;

    timeout_markstart(tm);
    err = socket_sendto(&nl->fd, (char *)nl->nlgb, nl->nlgb->n.nlmsg_len, &sent,
            (SA *)&nl_address, sizeof(nl_address), tm);

    if (err != IO_DONE) {
        lua_pushnil(L);
        lua_pushliteral(L, "error sending message");
        return 2;
    }

    lua_pushinteger(L, sent);
    return 1;
}
