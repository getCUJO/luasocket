/*=========================================================================*\
* Unix domain socket dgram submodule
* LuaSocket toolkit
\*=========================================================================*/
#include <string.h>
#include <stdlib.h>

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
static int meth_send(lua_State *L);
static int meth_receive(lua_State *L);
static int meth_close(lua_State *L);
static int meth_settimeout(lua_State *L);
static int meth_gettimeout(lua_State *L);
static int meth_getfd(lua_State *L);
static int meth_setfd(lua_State *L);
static int meth_receivefrom(lua_State *L);
static int meth_sendto(lua_State *L);

/* unixdgram object methods */
static luaL_Reg unixdgram_methods[] = {
    {"__gc",        meth_close},
    {"__tostring",  auxiliar_tostring},
    {"bind",        meth_bind},
    {"close",       meth_close},
    {"getfd",       meth_getfd},
    {"send",        meth_send},
    {"sendto",      meth_sendto},
    {"receive",     meth_receive},
    {"receivefrom", meth_receivefrom},
    {"setfd",       meth_setfd},
    {"settimeout",  meth_settimeout},
    {"gettimeout",  meth_gettimeout},
    {NULL,          NULL}
};

static luaL_Reg func[] = {
    {"dgram", global_create},
    {NULL, NULL}
};

/*-------------------------------------------------------------------------*\
* Initializes module
\*-------------------------------------------------------------------------*/
int unixdgram_open(lua_State *L)
{
    /* create classes */
    /* create class groups */

    luaL_setfuncs(L, func, 0);
    return 0;
}

/*=========================================================================*\
* Lua methods
\*=========================================================================*/
/*-------------------------------------------------------------------------*\
* Send data through connected unixdgram socket
\*-------------------------------------------------------------------------*/
static int meth_send(lua_State *L)
{
    return 1;
}

/*-------------------------------------------------------------------------*\
* Send data through unconnected unixdgram socket
\*-------------------------------------------------------------------------*/
static int meth_sendto(lua_State *L)
{
    return 1;
}

static int meth_receive(lua_State *L) {    
}

/*-------------------------------------------------------------------------*\
* Receives data and sender from a DGRAM socket
\*-------------------------------------------------------------------------*/
static int meth_receivefrom(lua_State *L) {
}

/*-------------------------------------------------------------------------*\
* Select support methods
\*-------------------------------------------------------------------------*/
static int meth_getfd(lua_State *L) {
    return 1;
}

/* this is very dangerous, but can be handy for those that are brave enough */
static int meth_setfd(lua_State *L) {
    return 0;
}

/*-------------------------------------------------------------------------*\
* Binds an object to an address
\*-------------------------------------------------------------------------*/
static const char *unixdgram_trybind(p_unix un, const char *path) {
    return socket_strerror(err);
}

static int meth_bind(lua_State *L)
{
    lua_pushnumber(L, 1);
    return 1;
}

/*-------------------------------------------------------------------------*\
* Closes socket used by object
\*-------------------------------------------------------------------------*/
static int meth_close(lua_State *L)
{
    lua_pushnumber(L, 1);
    return 1;
}

/*-------------------------------------------------------------------------*\
* Just call tm methods
\*-------------------------------------------------------------------------*/
static int meth_settimeout(lua_State *L)
{
}

static int meth_gettimeout(lua_State *L)
{
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
    /* try to allocate a system socket */
    if (err == IO_DONE) {
        /* allocate unixdgram object */
        /* set its type as master object */
        /* initialize remaining structure fields */
        socket_setnonblocking(&sock);
        return 1;
    } else {
        lua_pushnil(L);
        lua_pushstring(L, socket_strerror(err));
        return 2;
    }
}
