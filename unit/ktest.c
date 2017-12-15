/*
 * This file is part of the KNOT Project
 *
 * Copyright (c) 2015, CESAR. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *	* Redistributions of source code must retain the above copyright
 *	  notice, this list of conditions and the following disclaimer.
 *	* Redistributions in binary form must reproduce the above copyright
 *	  notice, this list of conditions and the following disclaimer in the
 *	  documentation and/or other materials provided with the distribution.
 *	* Neither the name of the CESAR nor the
 *	  names of its contributors may be used to endorse or promote products
 *	  derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL CESAR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>

#include <glib.h>


#include <knot_protocol.h>
#include <knot_types.h>

#include <hal/linux_log.h>

/* Abstract unit socket namespace */
#define KNOT_UNIX_SOCKET			"knot"

/* device name for the register */
#define	KTEST_DEVICE_NAME			"ktest_unit_test"

static uint64_t reg_id = 0x0123456789abcdef;
static int sockfd;
static char uuid128[KNOT_PROTOCOL_UUID_LEN];
static char token[KNOT_PROTOCOL_TOKEN_LEN];
static knot_msg kmsg;
static knot_msg kresp;

static int tcp_connect(void)
{
	struct sockaddr_in addr;
	int err, sock;

	sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock < 0) {
		err = errno;
		hal_log_error("tcp socket(): %s (%d)", strerror(err), err);
		return -err;
	}

	memset(&addr,0,sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons(8081);

	if (connect(sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		err = errno;
		hal_log_error("tcp connect(): %s (%d)", strerror(err), err);
		close(sock);
		return -err;
	}

	return sock;
}

static int unix_connect(void)
{
	struct sockaddr_un addr;
	int err, sock;

	sock = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
	if (sock < 0) {
		err = errno;
		hal_log_error(" >socket failure: %s (%d)", strerror(err), err);
		return -err;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	/* Abstract namespace: first character must be null */
	strcpy(addr.sun_path + 1, KNOT_UNIX_SOCKET);

	if (connect(sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		err = errno;
		hal_log_error("connect(): %s (%d)", strerror(err), err);
		close(sock);
		return -err;
	}

	return sock;
}

static void unix_connect_test(void)
{
	sockfd = unix_connect();
	g_assert(sockfd > 0);
}

static void unix_close_test(void)
{
	g_assert(close(sockfd) == 0);
	sockfd = -1;
}

static ssize_t do_request(const knot_msg *kmsg, size_t len, knot_msg *kresp)
{
	ssize_t nbytes;
	int err;

	nbytes = write(sockfd, kmsg, len);
	if (nbytes < 0) {
		err = errno;
		hal_log_error("write(): %s(%d)", strerror(err), err);
		return -err;
	}

	nbytes = read(sockfd, kresp, sizeof(knot_msg));
	if (nbytes < 0) {
		err = errno;
		hal_log_error("read(): %s (%d)", strerror(err), err);
		return -err;
	}

	return nbytes;
}

static void authenticate_test(void)
{
	ssize_t size;

	memset(&kmsg, 0, sizeof(kmsg));
	kmsg.hdr.type = KNOT_MSG_AUTH_REQ;

	memcpy(kmsg.auth.uuid, uuid128, sizeof(kmsg.auth.uuid));
	memcpy(kmsg.auth.token, token, sizeof(kmsg.auth.token));

	kmsg.hdr.payload_len = sizeof(kmsg.auth) - sizeof(kmsg.hdr);
	size = do_request(&kmsg, sizeof(kmsg.auth), &kresp);

	/* Response consistency */
	g_assert(size == sizeof(kresp.action));
	g_assert(kresp.hdr.payload_len == sizeof(kresp.action.result));

	/* Response opcode & result */
	g_assert(kresp.hdr.type == KNOT_MSG_AUTH_RESP);
	g_assert(kresp.action.result == KNOT_SUCCESS);
}

static void register_missing_devname_test(void)
{
	ssize_t size, plen;

	memset(&kmsg, 0, sizeof(kmsg));
	kmsg.hdr.type = KNOT_MSG_REGISTER_REQ;

	/* Sending register message with missing parameters. */

	kmsg.reg.id = ++reg_id;
	kmsg.hdr.payload_len = sizeof(kmsg.reg.id); /* No device name */

	plen = sizeof(kmsg.reg.hdr) + kmsg.hdr.payload_len;
	size = do_request(&kmsg, plen, &kresp);

	/* Response consistency */
	g_assert(size == sizeof(kresp.action));
	g_assert(kresp.hdr.payload_len == sizeof(kresp.action.result));

	/* Response opcode & result */
	g_assert(kresp.hdr.type == KNOT_MSG_REGISTER_RESP);
	g_assert(kresp.action.result == KNOT_REGISTER_INVALID_DEVICENAME);
}

static void register_empty_devname_test(void)
{
	ssize_t size, plen;

	memset(&kmsg, 0, sizeof(kmsg));
	kmsg.hdr.type = KNOT_MSG_REGISTER_REQ;

	kmsg.hdr.payload_len = sizeof(kmsg.reg.id) + 1;
	kmsg.reg.id = ++reg_id;

	plen = sizeof(kmsg.reg.hdr) + kmsg.hdr.payload_len;
	size = do_request(&kmsg, plen, &kresp);

	/* Response consistency */
	g_assert(size == sizeof(kresp.action));
	g_assert(kresp.hdr.payload_len == sizeof(kresp.action.result));

	/* Response opcode & result */
	g_assert(kresp.hdr.type == KNOT_MSG_REGISTER_RESP);
	g_assert(kresp.action.result == KNOT_REGISTER_INVALID_DEVICENAME);
}

static void register_valid_devname_test(void)
{
	ssize_t size, plen;

	memset(&kresp, 0, sizeof(kresp));
	memset(&kmsg, 0, sizeof(kmsg));
	kmsg.hdr.type = KNOT_MSG_REGISTER_REQ;

	/* Copying name to registration message */
	kmsg.hdr.payload_len = strlen(KTEST_DEVICE_NAME);
	kmsg.reg.id = ++reg_id;
	strcpy(kmsg.reg.devName, KTEST_DEVICE_NAME);

	plen = sizeof(kmsg.reg.hdr) + kmsg.hdr.payload_len;
	size = do_request(&kmsg, plen, &kresp);

	/* Response consistency */
	g_assert(size == sizeof(kresp.cred));

	/* Response opcode & result */
	g_assert(kresp.hdr.type == KNOT_MSG_REGISTER_RESP);
	g_assert(kresp.action.result == KNOT_SUCCESS);

	g_message("UUID: %s token:%s", kresp.cred.uuid, kresp.cred.token);
	memcpy(uuid128, kresp.cred.uuid, sizeof(kresp.cred.uuid));
	memcpy(token, kresp.cred.token, sizeof(kresp.cred.token));
}

static void register_repeated_attempt_test(void)
{
	ssize_t size, plen;
	knot_msg kresp2;

	memset(&kresp2, 0, sizeof(kresp2));
	memset(&kmsg, 0, sizeof(kmsg));
	kmsg.hdr.type = KNOT_MSG_REGISTER_REQ;

	/* Copying name to registration message */
	kmsg.hdr.payload_len = strlen(KTEST_DEVICE_NAME);

	/* Do not increment: Use latest registered id  */
	kmsg.reg.id = reg_id;
	strcpy(kmsg.reg.devName, KTEST_DEVICE_NAME);

	plen = sizeof(kmsg.reg.hdr) + kmsg.hdr.payload_len;
	size = do_request(&kmsg, plen, &kresp2);

	/* Response consistency */
	g_assert(size == sizeof(kresp2.cred));

	/* Response opcode & result */
	g_assert(kresp2.hdr.type == KNOT_MSG_REGISTER_RESP);
	g_assert(kresp2.action.result == KNOT_SUCCESS);
	g_assert_cmpmem(&kresp, size, &kresp2, size);
}

static void register_new_id(void)
{
	ssize_t size, plen;
	knot_msg kresp2;
	int ret;

	/*
	 * Simulates new registration attempt from the
	 * same process using a different id. A new device
	 * (different UUID) must be registered.
	 */
	memset(&kresp2, 0, sizeof(kresp2));
	memset(&kmsg, 0, sizeof(kmsg));
	kmsg.hdr.type = KNOT_MSG_REGISTER_REQ;

	/* Copying name to registration message */
	kmsg.hdr.payload_len = strlen(KTEST_DEVICE_NAME);
	kmsg.reg.id = ++reg_id;
	strcpy(kmsg.reg.devName, KTEST_DEVICE_NAME);

	plen = sizeof(kmsg.reg.hdr) + kmsg.hdr.payload_len;
	size = do_request(&kmsg, plen, &kresp2);

	/* Response consistency */
	g_assert(size == sizeof(kresp2.cred));

	/* Response opcode & result */
	g_assert(kresp2.hdr.type == KNOT_MSG_REGISTER_RESP);
	g_assert(kresp2.action.result == KNOT_SUCCESS);

	/* Compare with the first received response */
	ret = memcmp(&kresp, &kresp2, size);
	g_assert(ret != 0);
}

static void unregister_valid_device_test(void)
{
	memset(&kmsg, 0, sizeof(kmsg));
	memset(&kresp, 0, sizeof(kresp));
	kmsg.hdr.type = KNOT_MSG_UNREGISTER_REQ;

	kmsg.hdr.payload_len = 0;
	g_assert(do_request(&kmsg, sizeof(kmsg.unreg), &kresp) ==
							sizeof(kresp.action));
	g_assert(kresp.hdr.payload_len == sizeof(kresp.action.result));
	g_assert(kresp.hdr.type == KNOT_MSG_UNREGISTER_RESP);
	g_assert(kresp.action.result == KNOT_SUCCESS);
}

static void tcp_connect_test(void)
{
	sockfd = tcp_connect();
	g_assert(sockfd > 0);
}

static void tcp_close_test(void)
{
	g_assert(close(sockfd) == 0);
	sockfd = -1;
}

/* Register and run all tests */
int main(int argc, char *argv[])
{

	signal(SIGPIPE, SIG_IGN);

	g_test_init (&argc, &argv, NULL);

	g_test_add_func("/1/unix_connect", unix_connect_test);
	g_test_add_func("/1/register_missing_devname",
				register_missing_devname_test);
	g_test_add_func("/1/unix_close", unix_close_test);

	g_test_add_func("/2/tcp_connect", tcp_connect_test);
	g_test_add_func("/2/register_missing_devname",
				register_missing_devname_test);
	g_test_add_func("/2/tcp_close", tcp_close_test);

	g_test_add_func("/3/unix_connect", unix_connect_test);
	g_test_add_func("/3/register_empty_devname",
				register_empty_devname_test);
	g_test_add_func("/3/unix_close", unix_close_test);

	g_test_add_func("/4/tcp_connect", tcp_connect_test);
	g_test_add_func("/4/tcp_register_empty_devname",
				register_empty_devname_test);
	g_test_add_func("/4/tcp_close", tcp_close_test);

	g_test_add_func("/5/tcp_connect", tcp_connect_test);
	g_test_add_func("/5/tcp_register_empty_devname",
				register_empty_devname_test);
	g_test_add_func("/5/tcp_close", tcp_close_test);

	g_test_add_func("/6/unix_connect", unix_connect_test);
	g_test_add_func("/6/register_valid_devname",
				register_valid_devname_test);
	g_test_add_func("/6/register_repeated_attempt",
				register_repeated_attempt_test);
	g_test_add_func("/6/register_new_id",
				register_new_id);
	g_test_add_func("/6/unix_close", unix_close_test);

	g_test_add_func("/7/tcp_connect", unix_connect_test);
	g_test_add_func("/7/register_valid_devname",
				register_valid_devname_test);
	g_test_add_func("/7/register_repeated_attempt",
				register_repeated_attempt_test);
	g_test_add_func("/7/register_new_id",
				register_new_id);
	g_test_add_func("/7/tcp_close", unix_close_test);

	g_test_add_func("/8/unix_connect", unix_connect_test);
	g_test_add_func("/8/authenticate",
				authenticate_test);
	g_test_add_func("/8/unregister_valid_device",
				unregister_valid_device_test);
	g_test_add_func("/8/unix_close", unix_close_test);

	return g_test_run();
}
