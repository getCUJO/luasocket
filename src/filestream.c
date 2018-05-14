/*=========================================================================*\
* File stream sub module
* LuaSocket toolkit
\*=========================================================================*/
#include <string.h>

#include "lua.h"
#include "lauxlib.h"
#include "compat.h"

#include "auxiliar.h"
#include "socket.h"
#include "options.h"
#include "filestream.h"

 #include <sys/types.h>
 #include <sys/stat.h>
 #include <fcntl.h>

/*=========================================================================*\
* Internal function prototypes
\*=========================================================================*/
static int global_create(lua_State *L);
static int meth_send(lua_State *L);
static int meth_receive(lua_State *L);
static int meth_close(lua_State *L);
static int meth_settimeout(lua_State *L);
static int meth_getfd(lua_State *L);
static int meth_setfd(lua_State *L);
static int meth_dirty(lua_State *L);
static int meth_getstats(lua_State *L);
static int meth_setstats(lua_State *L);

/* filestream object methods */
static luaL_Reg filestream_methods[] = {
	{"__gc",        meth_close},
	{"__tostring",  auxiliar_tostring},
	{"close",       meth_close},
	{"dirty",       meth_dirty},
	{"getfd",       meth_getfd},
	{"getstats",    meth_getstats},
	{"setstats",    meth_setstats},
	{"receive",     meth_receive},
	{"send",        meth_send},
	{"setfd",       meth_setfd},
	{"settimeout",  meth_settimeout},
	{NULL,          NULL}
};

/* functions in library namespace */
static luaL_Reg func[] = {
	{"file", global_create},
	{NULL, NULL}
};

/*-------------------------------------------------------------------------*\
* Initializes module
\*-------------------------------------------------------------------------*/
int luaopen_socket_file(lua_State *L)
{
	/* create classes */
	auxiliar_newclass(L, "filestream", filestream_methods);

	lua_newtable(L);
	luaL_setfuncs(L, func, 0);
	return 1;
}

/*=========================================================================*\
* Lua methods
\*=========================================================================*/
/*-------------------------------------------------------------------------*\
* Just call buffered IO methods
\*-------------------------------------------------------------------------*/
static int meth_send(lua_State *L) {
	p_file fl = (p_file) auxiliar_checkclass(L, "filestream", 1);
	return buffer_meth_send(L, &fl->buf);
}

static int meth_receive(lua_State *L) {
	p_file fl = (p_file) auxiliar_checkclass(L, "filestream", 1);
	return buffer_meth_receive(L, &fl->buf);
}

static int meth_getstats(lua_State *L) {
	p_file fl = (p_file) auxiliar_checkclass(L, "filestream", 1);
	return buffer_meth_getstats(L, &fl->buf);
}

static int meth_setstats(lua_State *L) {
	p_file fl = (p_file) auxiliar_checkclass(L, "filestream", 1);
	return buffer_meth_setstats(L, &fl->buf);
}

/*-------------------------------------------------------------------------*\
* Select support methods
\*-------------------------------------------------------------------------*/
static int meth_getfd(lua_State *L) {
	p_file fl = (p_file) auxiliar_checkgroup(L, "filestream", 1);
	lua_pushnumber(L, (int) fl->sock);
	return 1;
}

/* this is very dangerous, but can be handy for those that are brave enough */
static int meth_setfd(lua_State *L) {
	p_file fl = (p_file) auxiliar_checkgroup(L, "filestream", 1);
	fl->sock = (t_socket) luaL_checknumber(L, 2);
	return 0;
}

static int meth_dirty(lua_State *L) {
	p_file fl = (p_file) auxiliar_checkgroup(L, "filestream", 1);
	lua_pushboolean(L, !buffer_isempty(&fl->buf));
	return 1;
}

/*-------------------------------------------------------------------------*\
* Closes socket used by object
\*-------------------------------------------------------------------------*/
static int meth_close(lua_State *L)
{
	p_file fl = (p_file) auxiliar_checkgroup(L, "filestream", 1);
	socket_destroy(&fl->sock);
	lua_pushnumber(L, 1);
	return 1;
}

/*-------------------------------------------------------------------------*\
* Just call tm methods
\*-------------------------------------------------------------------------*/
static int meth_settimeout(lua_State *L) {
	p_file fl = (p_file) auxiliar_checkgroup(L, "filestream", 1);
	return timeout_meth_settimeout(L, &fl->tm);
}

/*=========================================================================*\
* Library functions
\*=========================================================================*/
/*-------------------------------------------------------------------------*\
* Creates a master filestream object
\*-------------------------------------------------------------------------*/

typedef struct {
	const char *modename;
	int oflags;
} open_mode_t;

static open_mode_t open_mode[] = {
	{ "r" , O_RDONLY },
	{ "r+", O_RDWR },
	{ "w" , O_WRONLY|O_TRUNC |O_CREAT },
	{ "w+", O_RDWR  |O_TRUNC |O_CREAT },
	{ "a" , O_WRONLY|O_APPEND|O_CREAT },
	{ "a+", O_RDWR  |O_APPEND|O_CREAT },
	{ NULL, 0 }
};

static int checkmodeopt (lua_State *L, int arg, const char *def) {
	const char *name = (def) ? luaL_optstring(L, arg, def)
	                         : luaL_checkstring(L, arg);
	int i;
	for (i=0; open_mode[i].modename; i++)
		if (strcmp(open_mode[i].modename, name) == 0)
			return open_mode[i].oflags;
	return luaL_argerror(L, arg, lua_pushfstring(L, "invalid option '%s'", name));
}

static int global_create(lua_State *L) {
	const char *path = luaL_checkstring(L, 1);
	int oflags = checkmodeopt(L, 2, "r");
	int err = open(path, oflags, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH);
	/* try to allocate a system socket */
	if (err != -1) {
		t_socket sock = err;
		/* allocate filestream object */
		p_file fl = (p_file) lua_newuserdata(L, sizeof(t_file));
		/* set its type as master object */
		auxiliar_setclass(L, "filestream", -1);
		/* initialize remaining structure fields */
		socket_setnonblocking(&sock);
		fl->sock = sock;
		io_init(&fl->io, (p_send) socket_send, (p_recv) socket_recv,
		        (p_error) socket_ioerror, &fl->sock);
		timeout_init(&fl->tm, -1, -1);
		buffer_init(&fl->buf, &fl->io, &fl->tm);
		return 1;
	} else {
		lua_pushnil(L);
		lua_pushstring(L, socket_strerror(errno));
		return 2;
	}
}
