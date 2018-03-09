/*=========================================================================*\
* Netlink socket submodule
* LuaSocket toolkit
\*=========================================================================*/
#include <string.h>
#include <stdlib.h>

#include <linux/netlink.h>
#include <asm/types.h>
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

static int try_send(lua_State *L, p_netlink nl, int flags, int seq, int type, 
        const char *payload, p_timeout tm, struct sockaddr_nl *addr);

static int try_receive(lua_State *L, struct nlmsghdr *nlh, p_timeout tm, 
        p_netlink nl, struct sockaddr_nl *dst);

static char *flag_process(lua_State *L, p_netlink nl, struct nlmsghdr *nlh);
static int convert_flag(const char *flag);
static int convert_type(const char *type);

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

/*-------------------------------------------------------------------------*\
* Initializes module
\*-------------------------------------------------------------------------*/
NETLINK_API int luaopen_socket_netlink(lua_State *L) {
    t_socket sock;
    int err = socket_create(&sock, AF_NETLINK, SOCK_RAW, NETLINK_USERSOCK);

    /* create classes */
    auxiliar_newclass(L, "netlink{connected}", netlink_methods);
    auxiliar_newclass(L, "netlink{unconnected}", netlink_methods);
    /* create class groups */
    auxiliar_add2group(L, "netlink{connected}", "netlink{any}");
    auxiliar_add2group(L, "netlink{unconnected}", "netlink{any}");
    /* try to allocate a system socket */
    if (err == IO_DONE) {
        /* allocate netlink  object */
        p_netlink nl = (p_netlink)lua_newuserdata(L, sizeof(t_netlink));
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
    int flags = convert_flag(luaL_optstring(L, 3, "NOP"));
    int seq = luaL_optinteger(L, 4, 1);
    int type = convert_type(luaL_optstring(L, 5, "NOP"));
    p_timeout tm = &nl->tm; 

    return try_send(L, nl, flags, seq, type, payload, tm, NULL);
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
    int flags = convert_flag(luaL_optstring(L, 5, "NOP"));
    int seq = luaL_optinteger(L, 6, 1);
    int type = convert_type(luaL_optstring(L, 7, "NOP"));
    struct sockaddr_nl addr;
    p_timeout tm = &nl->tm; 

    memset(&addr, 0, sizeof(addr));
    addr.nl_pid = dstpid;
    addr.nl_family = AF_NETLINK;
    addr.nl_groups = groups;

    return try_send(L, nl, flags, seq, type, payload, tm, &addr);
}

/*-------------------------------------------------------------------------*\
* Receives data from a netlink socket
\*-------------------------------------------------------------------------*/
static int meth_receive(lua_State *L) {
    p_netlink nl = (p_netlink)auxiliar_checkclass(L, "netlink{connected}", 1);
    char buff[NLMSG_SPACE(MAX_PAYLOAD)];
    struct nlmsghdr *nlh = (struct nlmsghdr *)buff;
    p_timeout tm = &nl->tm;

    return try_receive(L, nlh, tm, nl, NULL);
}

/*-------------------------------------------------------------------------*\
* Receives data from a netlink socket
\*-------------------------------------------------------------------------*/
static int meth_receivefrom(lua_State *L) {
    p_netlink nl = (p_netlink)auxiliar_checkclass(L, "netlink{unconnected}", 1);
    char buff[NLMSG_SPACE(MAX_PAYLOAD)];
    struct nlmsghdr *nlh = (struct nlmsghdr *)buff;
    struct sockaddr_nl dst;
    p_timeout tm = &nl->tm;

    return try_receive(L, nlh, tm, nl, &dst);
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
* Conversion support methods
\*-------------------------------------------------------------------------*/
static int convert_flag(const char *flag) {
    int mask = 0;
    if (strcmp(flag, "NLM_F_ACK") == 0)
        mask |= NLM_F_ACK;
    if (strcmp(flag, "NLM_F_MULTI") == 0)
        mask |= NLM_F_MULTI;
    if (strcmp(flag, "NLM_F_ECHO") == 0)
        mask |= NLM_F_ECHO;
    if (strcmp(flag, "NLM_F_REQUEST") == 0)
        mask |= NLM_F_REQUEST;
    if (strcmp(flag, "NLM_F_ROOT") == 0)
        mask |= NLM_F_ROOT;
    if (strcmp(flag, "NLM_F_MATCH") == 0)
        mask |= NLM_F_MATCH;
    if (strcmp(flag, "NLM_F_ATOMIC") == 0)
        mask |= NLM_F_ATOMIC;
    if (strcmp(flag, "NLM_F_DUMP") == 0)
        mask |= NLM_F_DUMP;
    if (strcmp(flag, "NLM_F_REPLACE") == 0)
        mask |= NLM_F_REPLACE;
    if (strcmp(flag, "NLM_F_EXCL") == 0)
        mask |= NLM_F_EXCL;
    if (strcmp(flag, "NLM_F_CREATE") == 0)
        mask |= NLM_F_CREATE;
    if (strcmp(flag, "NLM_F_APPEND") == 0)
        mask |= NLM_F_APPEND;
    return mask;
}

static int convert_type(const char *type) {
    if (strcmp(type, "NLMSG_NOOP") == 0)
        return NLMSG_NOOP;
    if (strcmp(type, "NLMSG_ERROR") == 0)
        return NLMSG_ERROR;
    if (strcmp(type, "NLMSG_DONE") == 0)
        return NLMSG_DONE;
    return 0;
}

/*-------------------------------------------------------------------------*\
* Flag treatment support method
\*-------------------------------------------------------------------------*/
static char *flag_process(lua_State *L, p_netlink nl, struct nlmsghdr *nlh) {
    if ((nlh->nlmsg_flags & NLM_F_MULTI) == NLM_F_MULTI)
        return "MULTI support is not available yet";
    if ((nlh->nlmsg_flags & NLM_F_ECHO) == NLM_F_ECHO)
        return "ECHO support is not available yet";
    if ((nlh->nlmsg_flags & NLM_F_ACK) == NLM_F_ACK) 
        return "ACK support is not available yet";
    if ((nlh->nlmsg_flags & NLM_F_REQUEST) == NLM_F_REQUEST) 
        return "REQUEST support is not available yet";
    if ((nlh->nlmsg_flags & NLM_F_REQUEST & NLM_F_ROOT) == 
        NLM_F_REQUEST & NLM_F_ROOT) 
        return "REQUEST ROOT support is not available yet";
    if ((nlh->nlmsg_flags & NLM_F_REQUEST & NLM_F_MATCH) == 
        NLM_F_REQUEST & NLM_F_MATCH) 
        return "REQUEST MATCH support is not available yet";
    if ((nlh->nlmsg_flags & NLM_F_REQUEST & NLM_F_ATOMIC) == 
        NLM_F_REQUEST & NLM_F_ATOMIC) 
        return "REQUEST ATOMIC support is not available yet";
    if ((nlh->nlmsg_flags & NLM_F_REQUEST & NLM_F_DUMP) == 
        NLM_F_REQUEST & NLM_F_DUMP) 
        return "REQUEST DUMP support is not available yet";
    if ((nlh->nlmsg_flags & NLM_F_REQUEST & NLM_F_REPLACE) == 
        NLM_F_REQUEST & NLM_F_REPLACE) 
        return "REQUEST REPLACE support is not available yet";
    if ((nlh->nlmsg_flags & NLM_F_REQUEST & NLM_F_EXCL) == 
        NLM_F_REQUEST & NLM_F_EXCL) 
        return "REQUEST EXCL support is not available yet";
    if ((nlh->nlmsg_flags & NLM_F_REQUEST & NLM_F_CREATE) == 
        NLM_F_REQUEST & NLM_F_CREATE) 
        return "REQUEST CREATE support is not available yet";
    if ((nlh->nlmsg_flags & NLM_F_REQUEST & NLM_F_APPEND) == 
        NLM_F_REQUEST & NLM_F_APPEND) 
        return "REQUEST APPEND support is not available yet";
    return NULL;
}

/*-------------------------------------------------------------------------*\
* Send/Receive support methods
\*-------------------------------------------------------------------------*/
static int try_send(lua_State *L, p_netlink nl, int flags, int seq, int type, 
        const char *payload, p_timeout tm, struct sockaddr_nl *addr) {
    int err;
    char *ferr;
    size_t sent = 0;
    size_t payload_size = strlen(payload);
    struct nlmsghdr *nlh = malloc(NLMSG_SPACE(payload_size));

    if (nlh == NULL) {
      lua_pushnil(L);
      lua_pushliteral(L, "error allocating nlmsghdr buffer");
      return 2;
    }

    memset(nlh, 0, NLMSG_SPACE(payload_size));
    nlh->nlmsg_len = NLMSG_SPACE(payload_size);
    nlh->nlmsg_pid = nl->srcpid;
    nlh->nlmsg_flags = flags;
    nlh->nlmsg_seq = seq;
    nlh->nlmsg_type = type;
    memcpy(NLMSG_DATA(nlh), payload, payload_size);
    timeout_markstart(tm);
    ferr = flag_process(L, nl, nlh);
    if (ferr != NULL) {
        lua_pushnil(L);
        lua_pushstring(L, ferr);
        return 2;
    }

    if (addr != NULL)
        err = socket_sendto(&nl->fd, (char *)nlh, nlh->nlmsg_len, &sent, 
            (SA *)addr, sizeof(addr), tm);
    else
        err = socket_send(&nl->fd, (char *)nlh, nlh->nlmsg_len, &sent, tm);

    free(nlh);
    if (err != IO_DONE) {
        lua_pushnil(L);
        lua_pushliteral(L, "error sending message");
        return 2;
    }

    lua_pushinteger(L, sent);
    return 1;
}

static int try_receive(lua_State *L, struct nlmsghdr *nlh, p_timeout tm, 
         p_netlink nl, struct sockaddr_nl *dst) {
    socklen_t len = sizeof(struct sockaddr_nl);
    int err;
    size_t got;

    memset(nlh, 0, NLMSG_SPACE(MAX_PAYLOAD));
    nlh->nlmsg_len = NLMSG_SPACE(MAX_PAYLOAD);
    timeout_markstart(tm);
    if (dst != NULL)
        err = socket_recvfrom(&nl->fd, (char *)nlh, nlh->nlmsg_len, &got, 
            (SA *)&dst, &len, tm);
    else
        err = socket_recv(&nl->fd, (char *)nlh, nlh->nlmsg_len, &got, tm);

    if (err != IO_DONE && err != IO_CLOSED) {
        lua_pushnil(L);
        lua_pushstring(L, socket_strerror(err));
        return 2;
    }

    lua_pushinteger(L, got);
    lua_pushlstring(L, NLMSG_DATA(nlh), got);
    if(dst != NULL) {
        lua_pushinteger(L, nlh->nlmsg_pid);
        return 3;
    }
    return 2;
}

/*-------------------------------------------------------------------------*\
* Binds an object to an address
\*-------------------------------------------------------------------------*/
static int meth_bind(lua_State *L) {
    p_netlink nl = (p_netlink)auxiliar_checkclass(L, "netlink{unconnected}", 1);
    int pid = luaL_checkinteger(L, 2);
    int grp = luaL_optinteger(L, 3, 0);
    struct sockaddr_nl addr;
    int err;

    nl->srcpid = pid;
    memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;
    addr.nl_groups = grp;	
    addr.nl_pid = nl->srcpid;
    getpeername(nl->fd, (SA *)&addr, (socklen_t *)sizeof(addr));
    err = socket_bind(&nl->fd, (SA *)&addr, sizeof(addr));
    if (err < 0) {
        lua_pushnil(L);
        lua_pushstring(L, socket_strerror(err));
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

