/*=========================================================================*\
* Unix domain socket dgram submodule
* LuaSocket toolkit
\*=========================================================================*/
#include <string.h>
#include <stdlib.h>

#include <linux/netlink.h>
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
static int meth_setoptions(lua_State *L);
static int meth_setfd(lua_State *L);
static int meth_getpeername(lua_State *L);
static int meth_setpeername(lua_State *L);

static const char *netlink_trybind(p_netlink nl);

/* unixdgram object methods */
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
    {"setsockname", meth_bind},
    {"settimeout",  meth_settimeout},
    {"setoptions",  meth_setoptions},
    {"gettimeout",  meth_gettimeout},
    {"setpeername", meth_setpeername},
    {"getpeername", meth_getpeername},
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
int netlink_open(lua_State *L)
{
    /* create classes */
    auxiliar_newclass(L, "netlink{connected}", netlink_methods);
    auxiliar_newclass(L, "netlink{unconnected}", netlink_methods);
    /* create class groups */
    auxiliar_add2group(L, "netlink{connected}", "netlink{any}");
    auxiliar_add2group(L, "netlink{unconnected}", "netlink{any}");

    auxiliar_add2group(L, "netlink{connected}", "select{able}");  
    auxiliar_add2group(L, "netlink{unconnected}", "select{able}");  
    luaL_setfuncs(L, func, 0);
    return 0;
}

/*=========================================================================*\
* Lua methods
\*=========================================================================*/
/*-------------------------------------------------------------------------*\
* Send data through connected netlink socket
\*-------------------------------------------------------------------------*/
static int meth_send(lua_State *L){
	p_netlink nl= (p_netlink) auxiliar_checkclass(L,"netlink{connected}",1);
	size_t payload_size, sent = 0;
	const char *payload=luaL_checklstring(L,2,&payload_size);
	p_timeout tm = &nl->tm; 

	int groups = luaL_optinteger(L,3,0);
	int flags = luaL_optinteger(L,4,0);

	struct sockaddr_nl addr;
	struct nlmsghdr *nlh;

	memset(&addr,0,sizeof(addr));
	addr.nl_pid=nl->dstpid;
	addr.nl_family=AF_NETLINK;
	addr.nl_groups=groups;

	nlh = (struct nlmsghdr *)malloc(NLMSG_SPACE(payload_size));
	memset(nlh, 0, NLMSG_SPACE(payload_size));
	nlh->nlmsg_len = NLMSG_SPACE(payload_size);
	nlh->nlmsg_pid = nl->srcpid;
	nlh->nlmsg_flags = flags;

	memcpy(NLMSG_DATA(nlh),payload,payload_size);

	int err = socket_sendto(&nl->fd,NLMSG_DATA(nlh),nlh->nlmsg_len,&sent,(struct sockaddr *)&addr,sizeof(addr),tm);
	if(err!=IO_DONE){
		lua_pushnil(L);
		lua_pushliteral(L,"error sending message");
		return 2;
	}
	lua_pushinteger(L,sent);
	return 1;
}

/*-------------------------------------------------------------------------*\
* Send data through unconnected netlink socket
\*-------------------------------------------------------------------------*/
static int meth_sendto(lua_State *L){
	p_netlink nl= (p_netlink) auxiliar_checkclass(L,"netlink{unconnected}",1);
	size_t payload_size, sent = 0;
	const char *payload=luaL_checklstring(L,2,&payload_size);
	p_timeout tm = &nl->tm; 

	int dstpid = luaL_checkinteger(L,3);
	int groups = luaL_optinteger(L,4,0);
	int flags = luaL_optinteger(L,5,0);

	struct sockaddr_nl addr;
	struct nlmsghdr *nlh;

	memset(&addr,0,sizeof(addr));
	addr.nl_pid=dstpid;
	addr.nl_family=AF_NETLINK;
	addr.nl_groups=groups;

	nlh = (struct nlmsghdr *)malloc(NLMSG_SPACE(payload_size));
	memset(nlh, 0, NLMSG_SPACE(payload_size));
	nlh->nlmsg_len = NLMSG_SPACE(payload_size);
	nlh->nlmsg_pid = nl->srcpid;
	nlh->nlmsg_flags = flags;

	memcpy(NLMSG_DATA(nlh),payload,payload_size);

	int err = socket_sendto(&nl->fd,NLMSG_DATA(nlh),nlh->nlmsg_len,&sent,(struct sockaddr *)&addr,sizeof(addr),tm);
	if(err!=IO_DONE){
		lua_pushnil(L);
		lua_pushliteral(L,"error sending message");
		return 2;
	}
	lua_pushinteger(L,sent);
	return 1;
}

/*-------------------------------------------------------------------------*\
* Receives data from a netlink socket
\*-------------------------------------------------------------------------*/
static int meth_receive(lua_State *L){
	p_netlink nl= (p_netlink) auxiliar_checkclass(L,"netlink{connected}",1);
	struct sockaddr_nl dst;
	struct nlmsghdr *nlh;
	struct iovec iov;
	size_t got;
	p_timeout tm = &nl->tm;

	nlh=(struct nlmsghdr *)malloc(NLMSG_SPACE(MAX_PAYLOAD));
	memset(nlh,0,NLMSG_SPACE(MAX_PAYLOAD));
	nlh->nlmsg_len=NLMSG_SPACE(MAX_PAYLOAD);

	iov.iov_base=(void *)nlh;
	iov.iov_len=nlh->nlmsg_len;
	socklen_t len=sizeof(dst);
	int err= socket_recvfrom(&nl->fd,NLMSG_DATA(nlh),nlh->nlmsg_len,&got,(struct sockaddr *)&dst,&len,tm);
    	if (err != IO_DONE && err != IO_CLOSED) {
        	lua_pushnil(L);
       		lua_pushstring(L,socket_strerror(err));
        	return 2;
    	}
    	lua_pushinteger(L,got);
    	lua_pushlstring(L,NLMSG_DATA(nlh), got);
    	return 2;
}

/*-------------------------------------------------------------------------*\
* Receives data from a netlink socket
\*-------------------------------------------------------------------------*/
static int meth_receivefrom(lua_State *L) {
	p_netlink nl= (p_netlink) auxiliar_checkclass(L,"netlink{unconnected}",1);
	struct sockaddr_nl dst;
	struct nlmsghdr *nlh;
	struct iovec iov;
	size_t got;
	p_timeout tm = &nl->tm;

	nlh=(struct nlmsghdr *)malloc(NLMSG_SPACE(MAX_PAYLOAD));
	memset(nlh,0,NLMSG_SPACE(MAX_PAYLOAD));
	nlh->nlmsg_len=NLMSG_SPACE(MAX_PAYLOAD);

	iov.iov_base=(void *)nlh;
	iov.iov_len=nlh->nlmsg_len;
	socklen_t len=sizeof(dst);
	int err= socket_recvfrom(&nl->fd,NLMSG_DATA(nlh),nlh->nlmsg_len,&got,(struct sockaddr *)&dst,&len,tm);
    	if (err != IO_DONE && err != IO_CLOSED) {
        	lua_pushnil(L);
       		lua_pushstring(L,socket_strerror(err));
        	return 2;
    	}
    	lua_pushinteger(L,got);
    	lua_pushlstring(L,NLMSG_DATA(nlh), got);
	lua_pushinteger(L,nlh->nlmsg_pid);
    	return 3;
}

/*-------------------------------------------------------------------------*\
* Select support methods
\*-------------------------------------------------------------------------*/
static int meth_getfd(lua_State *L) {
    p_netlink nl = (p_netlink) auxiliar_checkgroup(L, "netlink{any}", 1);
    lua_pushnumber(L, (int) nl->fd);
    return 1;
}

/* this is very dangerous, but can be handy for those that are brave enough */
static int meth_setfd(lua_State *L) {
    p_netlink nl = (p_netlink) auxiliar_checkgroup(L, "netlink{any}", 1);
    nl->fd = (t_socket) luaL_checknumber(L, 2);
    return 0;
}

/*-------------------------------------------------------------------------*\
* Binds an object to an address
\*-------------------------------------------------------------------------*/
static const char *netlink_trybind(p_netlink nl) {
	struct sockaddr_nl addr;

	memset(&addr,0,sizeof(addr));
	addr.nl_pid=nl->srcpid;
	addr.nl_family=AF_NETLINK;	
	addr.nl_groups=nl->grp;

	//int res=bind(p,(struct sockaddr *)&addr,sizeof(addr));
	int err = socket_bind(&nl->fd,(struct sockaddr *)&addr,sizeof(addr));
	return socket_strerror(err);
}

static int meth_bind(lua_State *L)
{
    	p_netlink nl = (p_netlink) auxiliar_checkclass(L, "netlink{unconnected}", 1);
	int pid=luaL_checkinteger(L,2);
	int grp=luaL_checkinteger(L,3);
    	if(!nl){
		lua_pushnil(L);
		lua_pushliteral(L,"invalid socket");
		return 2;	
	}
	nl->srcpid=pid;
	nl->grp=grp;
	const char *err = netlink_trybind(nl);
	if (err) {
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
static int meth_close(lua_State *L)
{
    p_netlink nl = (p_netlink) auxiliar_checkgroup(L, "netlink{any}", 1);
    socket_destroy(&nl->fd);
    lua_pushnumber(L, 1);
    return 1;
}

/*-------------------------------------------------------------------------*\
* Just call tm methods
\*-------------------------------------------------------------------------*/
static int meth_setpeername(lua_State *L){
	p_netlink nl = (p_netlink) auxiliar_checkclass(L,"netlink{unconnected}",1);
	int dstpid = luaL_checkinteger(L,2);
	nl->dstpid=dstpid;
	auxiliar_setclass(L,"netlink{connected}",1);
	return 0;
}

static int meth_getpeername(lua_State *L){
	p_netlink nl = (p_netlink) auxiliar_checkclass(L,"netlink{connected}",1);
	lua_pushinteger(L,nl->dstpid);
	return 1;
}
/*-------------------------------------------------------------------------*\
* Just call tm methods
\*-------------------------------------------------------------------------*/
static int meth_settimeout(lua_State *L)
{
    p_netlink nl = (p_netlink) auxiliar_checkgroup(L, "netlink{any}", 1);
    return timeout_meth_settimeout(L, &nl->tm);
}

static int meth_gettimeout(lua_State *L)
{
    p_netlink nl = (p_netlink) auxiliar_checkgroup(L, "netlink{any}", 1);
    return timeout_meth_gettimeout(L, &nl->tm);
}

/*-------------------------------------------------------------------------*\
* Just call tm methods
\*-------------------------------------------------------------------------*/
static int meth_setoptions(lua_State *L){
	p_netlink nl = (p_netlink) auxiliar_checkgroup(L,"netlink{any}",1);
	if(!lua_isnil(L,2))
		nl->srcpid = luaL_checkinteger(L,2);
	if(!lua_isnil(L,3))
		nl->grp = luaL_checkinteger(L,3);
	return 0;
}

/*=========================================================================*\
* Library functions
\*=========================================================================*/
/*-------------------------------------------------------------------------*\
* Creates a master unixdgram object
\*-------------------------------------------------------------------------*/
static int global_create(lua_State *L)
{
    t_socket sock;
    int err = socket_create(&sock, AF_NETLINK, SOCK_RAW, NETLINK_USERSOCK);
    /* try to allocate a system socket */
    if (err == IO_DONE){
        /* allocate unixdgram object */
        p_netlink nl = (p_netlink) lua_newuserdata(L, sizeof(t_netlink));
        /* set its type as master object */
        auxiliar_setclass(L, "netlink{unconnected}", -1);
        /* initialize remaining structure fields */
        socket_setnonblocking(&sock);
        nl->fd = sock;
	nl->type=SOCK_RAW;
        /*io_init(&un->io, (p_send) socket_send, (p_recv) socket_recv,
                (p_error) socket_ioerror, &un->sock);*/
        timeout_init(&nl->tm, -1, -1);
        /*buffer_init(&un->buf, &un->io, &un->tm);*/
        return 1;
    } else {
        lua_pushnil(L);
        lua_pushstring(L, socket_strerror(err));
        return 2;
    }
}
