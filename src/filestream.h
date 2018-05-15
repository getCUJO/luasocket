#ifndef FILESTREAM_H
#define FILESTREAM_H
#include "lua.h"

#include "buffer.h"
#include "timeout.h"
#include "socket.h"

#ifndef FILESOCK_API
#define FILESOCK_API extern
#endif

typedef struct t_file_ {
	t_socket sock;
	t_io io;
	t_buffer buf;
	t_timeout tm;
} t_file;
typedef t_file *p_file;

FILESOCK_API int luaopen_socket_file(lua_State *L);

#endif /* FILESTREAM_H */
