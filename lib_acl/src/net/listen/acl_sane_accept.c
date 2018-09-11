/* System library. */
#include "StdAfx.h"
#ifndef ACL_PREPARE_COMPILE

#include "stdlib/acl_define.h"
#ifdef  HP_UX
#define _XOPEN_SOURCE
#define _XOPEN_SOURCE_EXTENDED  1
#endif

#include <errno.h>
#include <stdio.h>
#include <string.h>

#ifdef ACL_UNIX
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#ifdef ACL_BCB_COMPILER
#pragma hdrstop
#endif

/* Utility library. */

#include "stdlib/acl_msg.h"
#include "net/acl_tcp_ctl.h"
#include "net/acl_sane_inet.h"
#include "net/acl_sane_socket.h"
#include "net/acl_listen.h"

#endif

static acl_accept_fn __sys_accept = accept;

void acl_set_accept(acl_accept_fn fn)
{
	__sys_accept = fn;
}

/* acl_sane_accept - sanitize accept() error returns */

ACL_SOCKET acl_sane_accept(ACL_SOCKET sock, struct sockaddr * sa, socklen_t *len)
{
	static int accept_ok_errors[] = {
		ACL_EAGAIN,
		ACL_ECONNREFUSED,
		ACL_ECONNRESET,
		ACL_EHOSTDOWN,
		ACL_EHOSTUNREACH,
		ACL_EINTR,
		ACL_ENETDOWN,
		ACL_ENETUNREACH,
		ACL_ENOTCONN,
		ACL_EWOULDBLOCK,
		ACL_ENOBUFS,			/* HPUX11 */
		ACL_ECONNABORTED,
		0,
	};
	ACL_SOCKET fd;

	/*
	 * XXX Solaris 2.4 accept() returns EPIPE when a UNIX-domain client
	 * has disconnected in the mean time. From then on, UNIX-domain
	 * sockets are hosed beyond recovery. There is no point treating
	 * this as a beneficial error result because the program would go
	 * into a tight loop.
	 * XXX LINUX < 2.1 accept() wakes up before the three-way handshake is
	 * complete, so it can fail with ECONNRESET and other "false alarm"
	 * indications.
	 * 
	 * XXX FreeBSD 4.2-STABLE accept() returns ECONNABORTED when a
	 * UNIX-domain client has disconnected in the mean time. The data
	 * that was sent with connect() write() close() is lost, even though
	 * the write() and close() reported successful completion.
	 * This was fixed shortly before FreeBSD 4.3.
	 * 
	 * XXX HP-UX 11 returns ENOBUFS when the client has disconnected in
	 * the mean time.
	 */
	fd = __sys_accept(sock, (struct sockaddr *) sa, (socklen_t *) len);
	if (fd == ACL_SOCKET_INVALID) {
		int  count = 0, err, error = acl_last_error();
		for (; (err = accept_ok_errors[count]) != 0; count++) {
			if (error == err) {
				acl_set_error(ACL_EAGAIN);
				break;
			}
		}
	}

	/*
	 * XXX Solaris select() produces false read events, so that read()
	 * blocks forever on a blocking socket, and fails with EAGAIN on
	 * a non-blocking socket. Turning on keepalives will fix a blocking
	 * socket provided that the kernel's keepalive timer expires before
	 * the Postfix watchdog timer.
	 */
#ifdef AF_INET6
	else if (sa && (sa->sa_family == AF_INET || sa->sa_family == AF_INET6))
#else
	else if (sa && sa->sa_family == AF_INET)
#endif
	{
		int on = 1;

		/* default set client to nodelay --- add by zsx, 2008.9.4 */
		acl_tcp_nodelay(fd, on);

#if defined(BROKEN_READ_SELECT_ON_TCP_SOCKET) && defined(SO_KEEPALIVE)
		(void) setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE,
			(char *) &on, sizeof(on));
#endif
	}

	return fd;
}

ACL_SOCKET acl_accept(ACL_SOCKET sock, char *buf, size_t size, int* sock_type)
{
	ACL_SOCKADDR addr;
	socklen_t len = sizeof(addr);
	struct sockaddr *sa = (struct sockaddr*) &addr;
	ACL_SOCKET fd;
	size_t n;

	memset(&addr, 0, sizeof(addr));

	fd = acl_sane_accept(sock, sa, &len);
	if (fd == ACL_SOCKET_INVALID)
		return fd;

	if (sock_type != NULL)
		*sock_type = sa->sa_family;

	if (buf == NULL || size == 0)
		return fd;

	buf[0] = 0;

	if (sa->sa_family == AF_INET) {
#ifdef ACL_WINDOWS
		if (!inet_ntop(sa->sa_family, &addr.in.sin_addr, buf, size))
#else
		if (!inet_ntop(sa->sa_family, &addr.in.sin_addr,
			buf, (socklen_t) size))
#endif
		{
			acl_msg_error("%s(%d): inet_ntop error=%s",
				__FUNCTION__, __LINE__, acl_last_serror());
			return fd;
		}

		n = strlen(buf);
		if (n >= size)
			return fd;
		snprintf(buf + n, size - n, ":%u",
			(unsigned short) ntohs(addr.in.sin_port));
		buf[size - 1] = 0;
		return fd;
	}
#ifdef AF_INET6
	else if (sa->sa_family == AF_INET6) {
#ifdef ACL_WINDOWS
		if (!inet_ntop(sa->sa_family, &addr.in6.sin6_addr, buf, size))
#else
		if (!inet_ntop(sa->sa_family, &addr.in6.sin6_addr,
			buf, (socklen_t) size))
#endif
		{
			acl_msg_error("%s(%d): inet_ntop error=%s",
				__FUNCTION__, __LINE__, acl_last_serror());
			return fd;
		}

		n = strlen(buf);
		if (n >= size)
			return fd;
		snprintf(buf + n, size - n, ":%u",
			(unsigned short) ntohs(addr.in6.sin6_port));
		buf[size - 1] = 0;
		return fd;
	}
#endif
#ifdef	ACL_UNIX
	else if (sa->sa_family == AF_UNIX) {
		if (acl_getsockname(fd, buf, size) < 0)
			buf[0] = 0;
		return fd;
	}
#endif
	else {
		return fd;
	}
}
