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

#include <winsock2.h>
typedef SOCKET socket_t;
typedef int socklen_t;

inline static bool valid_socket(socket_t fd)
{
	return fd != INVALID_SOCKET;
}

#endif
