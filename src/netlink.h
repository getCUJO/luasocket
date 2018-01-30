#include "timeout.h"
#include "lua.h"
#include "socket.h"

#define MAX_PAYLOAD 2048


/*=========================================================================*\
* .h definitions here because i'm lazy lmao 
\*=========================================================================*/

#define t_pid int
#define t_groups int
#define t_type int

typedef struct t_netlink_{
	t_socket fd; /*socket number (int)*/
	t_timeout tm; /*timeout value (timeval)*/ 
	t_pid srcpid; /*port id (int) */
	t_type type; /*socket type (int)*/
}t_netlink;
typedef t_netlink *p_netlink;

int netlink_open(lua_State *L);
