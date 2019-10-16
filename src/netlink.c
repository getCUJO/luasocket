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
static int meth_getfd(lua_State *L);
static int meth_setfd(lua_State *L);
static int meth_getpeername(lua_State *L);
static int meth_setpeername(lua_State *L);
static int meth_getsockpid(lua_State *L);

static const char *netlink_trybind(p_netlink nl, int grp);

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
    {"setpeername", meth_setpeername},
    {"getpeername", meth_getpeername},
    {"getsockpid",  meth_getsockpid},
    {NULL,          NULL}
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
        nl->nlb = malloc(sizeof(struct nlmsgbuf));
        if (!nl->nlb) {
            socket_destroy(&sock);
            lua_pushnil(L);
            lua_pushstring(L, "couldn't allocate buffer for netlink");
            return 2;
        }
        /* set its type as master object */
        auxiliar_setclass(L, "netlink{unconnected}", -1);
        /* initialize remaining structure fields */
        socket_setnonblocking(&sock);
        nl->fd = sock;
        nl->type = SOCK_RAW;
        timeout_init(&nl->tm, -1, -1);
        return 1;
    }

    lua_pushnil(L);
    lua_pushstring(L, socket_strerror(err));
    return 2;
}
