#pragma once

#include <stdbool.h>

#ifndef _WIN32

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/poll.h>
#include <unistd.h>

typedef int socket_t;
#define INVALID_SOCKET (-1)

inline static bool valid_socket(socket_t fd)
{
	return fd >= 0;
}

inline static int closesocket(socket_t fd)
{
	return close(fd);
}

#else // _WIN32

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
typedef SOCKET socket_t;
typedef unsigned int socklen_t;

inline static bool valid_socket(socket_t fd)
{
	return fd != INVALID_SOCKET;
}

#ifndef sendto
inline int sendto_unix_type(SOCKET sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr,
			    socklen_t addrlen)
{
	return sendto(sockfd, buf, (int)len, flags, dest_addr, (int)addrlen);
}
#define sendto sendto_unix_type
#endif // sendto

#endif
