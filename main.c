#include <sys/types.h>
#include <netinet/in.h>
#define BIND_8_COMPAT
#include <arpa/nameser.h>
#include <resolv.h>
#include <netdb.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include "dns.h"
#include "namestacknl.h"

#define MAX_PAYLOAD 1024  /* maximum payload size*/

int sock_fd;

#define MAX_NAME_LEN 254

static void print_a(const u_char *ptr, uint16_t rdlength,
 const u_char *start, uint16_t len)
{
    if (rdlength != sizeof(uint32_t))
        fprintf(stderr, "address record has invalid length %d\n", rdlength);
     else if (ptr + rdlength - start > len)
         fprintf(stderr, "address record overflows buffer\n");
    else
    {
        uint32_t addr = *(uint32_t *)ptr;
        u_char *addrp;

        for (addrp = (u_char *)&addr;
         addrp - (u_char *)&addr < sizeof(uint32_t);
         addrp++)
        {
            if (addrp == (u_char *)&addr + sizeof(uint32_t) - 1)
                printf("%d\n", *addrp);
            else
                printf("%d.", *addrp);
        }
    }
}

#ifndef s6_addr16
#define s6_addr16   __u6_addr.__u6_addr16
#endif

static void print_aaaa(const u_char *ptr, uint16_t rdlength,
 const u_char *start, uint16_t len)
{
    if (rdlength != sizeof(struct in6_addr))
        fprintf(stderr, "address record has invalid length %d\n", rdlength);
     else if (ptr + rdlength - start > len)
         fprintf(stderr, "address record overflows buffer\n");
    else
    {
        struct in6_addr *addr = (struct in6_addr *)ptr;
        int i, in_zero = 0;

        for (i = 0; i < 7; i++)
        {
            if (!addr->s6_addr16[i])
            {
                if (!in_zero)
                {
                    printf(":");
                    in_zero = 1;
                }
            }
            else
            {
                printf("%x:", ntohs(addr->s6_addr16[i]));
                in_zero = 0;
            }
        }
        printf("%x\n", ntohs(addr->s6_addr16[7]));
    }
}

struct query_data
{
	unsigned int seq;
	char name[MAX_NAME_LEN];
};

static void *query_thread(void *arg)
{
	struct query_data *data = arg;
	u_char buf[PACKETSZ];
	int len, msg_len, found_response = 0;
	struct nlmsghdr *nlh = NULL;
	uint16_t rdlength;
	const u_char *rdata;

	printf("querying %s (seq %d)\n", data->name, data->seq);
	len = res_query(data->name, C_IN, T_AAAA, buf, sizeof(buf));
	if (len >= 0)
	{
		found_response = !find_answer_of_type(buf, len, T_AAAA, 0,
						      &rdlength, &rdata);
		if (found_response)
		{
			printf("found a valid IPv6 address\n");
			print_aaaa(rdata, rdlength, buf, len);
		}
	}
	if (!found_response)
	{
		len = res_query(data->name, C_IN, T_A, buf, sizeof(buf));
		if (len >= 0)
		{
			found_response = !find_answer_of_type(buf, len, T_A, 0,
							      &rdlength,
							      &rdata);
			if (found_response)
			{
				printf("found a valid IPv4 address\n");
				print_a(rdata, rdlength, buf, len);
			}
		}
	}
	if (!found_response)
		printf("couldn't resolve %s: %d\n", data->name, h_errno);

	msg_len = sizeof(int);
	if (len > 0)
		msg_len += len;
	nlh = malloc(NLMSG_SPACE(msg_len));
	if (nlh)
	{
		struct sockaddr_nl dest_addr;
		struct iovec iov;
		struct msghdr msg;

		/* Send a reply message */
		memset(&dest_addr, 0, sizeof(dest_addr));
		dest_addr.nl_family = AF_NETLINK;

		nlh->nlmsg_len = NLMSG_SPACE(msg_len);
		nlh->nlmsg_type = NAME_STACK_NAME_REPLY;
		nlh->nlmsg_flags = 0;
		nlh->nlmsg_seq = data->seq;
		nlh->nlmsg_pid = 0;
		memcpy(NLMSG_DATA(nlh), &len, sizeof(len));
		if (len > 0)
			memcpy(NLMSG_DATA(nlh) + sizeof(len), buf, len);

		iov.iov_base = (void *)nlh;
		iov.iov_len = nlh->nlmsg_len;
		msg.msg_name = (void *)&dest_addr;
		msg.msg_namelen = sizeof(dest_addr);
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;

		sendmsg(sock_fd, &msg, 0);

		free(nlh);
	}
	free(data);
}

static void do_query(unsigned int seq, const char *data, size_t len)
{
	size_t measured_len;

	printf("got a query request with seq %d for %s (%d)\n", seq, data, len);
	/* Sanity-check the name */
	if (len <= MAX_NAME_LEN)
	{
		for (measured_len = 0; data[measured_len] && measured_len < len;
		     measured_len++)
			;
		if (!data[measured_len])
		{
			struct query_data *qdata =
				malloc(sizeof(struct query_data));

			if (qdata)
			{
				pthread_t thread_id;

				qdata->seq = seq;
				memcpy(qdata->name, data, measured_len + 1);
				if (pthread_create(&thread_id, NULL,
				    query_thread, qdata))
				{
					fprintf(stderr,
						"thread creation failed, can't resolve name\n");
					free(qdata);
				}
			}
			else
				fprintf(stderr, "alloc failed, can't resolve name\n");
		}
		else
			fprintf(stderr, "query has unterminated name\n");
	}
	else
		fprintf(stderr, "query has invalid name length %d\n",
			len);
}

struct register_data
{
	unsigned int seq;
	char name[MAX_NAME_LEN];
};

static void *register_thread(void *arg)
{
	struct register_data *data = arg;
	int len, err;
	struct nlmsghdr *nlh = NULL;

	printf("registering %s (seq %d)\n", data->name, data->seq);
	res_init();
	len = strlen(data->name);
	/* len is guaranteed to be <= MAX_NAME_LEN, see do_register */
	if (data->name[len - 1] == '.')
	{
		char host[MAX_NAME_LEN];
		const char *dot;

		/* Fully-qualified domain name, find domain */
		dot = strchr(data->name, '.');
		/* dot is guaranteed not to be NULL */
		memcpy(host, data->name, dot - data->name);
		host[dot - data->name] = 0;
		printf("fully-qualified name %s in domain %s\n", host, dot + 1);
		/* FIXME: actually register name, wait for response */
		err = 0;
	}
	else
	{
		printf("unqualified name %s, registering in domain %s\n",
		       data->name, _res.defdname);
		/* FIXME: actually register name, wait for response */
		err = 0;
	}

	nlh = malloc(NLMSG_SPACE(sizeof(int)));
	if (nlh)
	{
		struct sockaddr_nl dest_addr;
		struct iovec iov;
		struct msghdr msg;

		/* Send a reply message */
		memset(&dest_addr, 0, sizeof(dest_addr));
		dest_addr.nl_family = AF_NETLINK;

		nlh->nlmsg_len = NLMSG_SPACE(sizeof(int));
		nlh->nlmsg_type = NAME_STACK_REGISTER_REPLY;
		nlh->nlmsg_flags = 0;
		nlh->nlmsg_seq = data->seq;
		nlh->nlmsg_pid = 0;
		memcpy(NLMSG_DATA(nlh), &err, sizeof(err));

		iov.iov_base = (void *)nlh;
		iov.iov_len = nlh->nlmsg_len;
		msg.msg_name = (void *)&dest_addr;
		msg.msg_namelen = sizeof(dest_addr);
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;

		sendmsg(sock_fd, &msg, 0);

		free(nlh);
	}
	free(data);
}

static void do_register(unsigned int seq, const char *data, size_t len)
{
	size_t measured_len;

	printf("got a register request with seq %d for %s (%d)\n", seq, data,
	       len);
	/* Sanity-check the name */
	if (len <= MAX_NAME_LEN)
	{
		for (measured_len = 0; data[measured_len] && measured_len < len;
		     measured_len++)
			;
		if (!data[measured_len])
		{
			struct register_data *qdata =
				malloc(sizeof(struct register_data));

			if (qdata)
			{
				pthread_t thread_id;

				qdata->seq = seq;
				memcpy(qdata->name, data, measured_len + 1);
				if (pthread_create(&thread_id, NULL,
				    register_thread, qdata))
				{
					fprintf(stderr,
						"thread creation failed, can't resolve name\n");
					free(qdata);
					goto nak;
				}
			}
			else
			{
				fprintf(stderr, "alloc failed, can't resolve name\n");
				goto nak;
			}
		}
		else
		{
			fprintf(stderr, "query has unterminated name\n");
			goto nak;
		}
	}
	else
	{
		fprintf(stderr, "query has invalid name length %d\n",
			len);
		goto nak;
	}
	return;

nak:
	/* FIXME: nak the name register request */
	return;
}

int main(int argc, const char *argv[])
{
	sock_fd = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_NAME_ORIENTED_STACK);
	if (sock_fd >= 0) {
		struct sockaddr_nl src_addr, dest_addr;
		struct nlmsghdr *nlh = NULL;
		struct iovec iov;
		struct msghdr msg;
		struct pollfd pfd;

		memset(&src_addr, 0, sizeof(src_addr));
		src_addr.nl_family = AF_NETLINK;
		bind(sock_fd, (struct sockaddr*)&src_addr, sizeof(src_addr));

		memset(&dest_addr, 0, sizeof(dest_addr));
		dest_addr.nl_family = AF_NETLINK;

		nlh = malloc(NLMSG_SPACE(MAX_PAYLOAD));
		/* Send a register message
		 * FIXME: the message is empty, do I really need MAX_PAYLOAD
		 * data bytes?
		 */
		nlh->nlmsg_len = NLMSG_SPACE(MAX_PAYLOAD);
		nlh->nlmsg_type = NAME_STACK_REGISTER;
		nlh->nlmsg_pid = 0;
		nlh->nlmsg_flags = 0;
		*(char *)NLMSG_DATA(nlh) = 0;

		iov.iov_base = (void *)nlh;
		iov.iov_len = nlh->nlmsg_len;
		msg.msg_name = (void *)&dest_addr;
		msg.msg_namelen = sizeof(dest_addr);
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;

		sendmsg(sock_fd, &msg, 0);

		/* Read message from kernel */
		memset(nlh, 0, NLMSG_SPACE(MAX_PAYLOAD));
		recvmsg(sock_fd, &msg, 0);
		/* FIXME: check that it's a reply */
		printf("Received registration reply\n");

		memset(nlh, 0, NLMSG_SPACE(MAX_PAYLOAD));
		pfd.fd = sock_fd;
		pfd.events = POLLIN;
		while (poll(&pfd, 1, -1)) {
			recvmsg(sock_fd, &msg, 0);
			switch (nlh->nlmsg_type)
			{
			case NAME_STACK_NAME_QUERY:
				do_query(nlh->nlmsg_seq,
					 NLMSG_DATA(nlh),
					 NLMSG_PAYLOAD(nlh, 0));
				break;
			case NAME_STACK_REGISTER_QUERY:
				do_register(nlh->nlmsg_seq,
					 NLMSG_DATA(nlh),
					 NLMSG_PAYLOAD(nlh, 0));
				break;
			default:
				fprintf(stderr, "unhandled msg type %d\n",
					nlh->nlmsg_type);
			}
			memset(nlh, 0, NLMSG_SPACE(MAX_PAYLOAD));
		}
		/* Close Netlink Socket */
		close(sock_fd);
	}
	return 0;
}
