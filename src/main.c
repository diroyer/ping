#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include <float.h>
#include "ping.h"

#ifdef DEBUG
#define debug_print(fmt, ...) \
	do { \
		fprintf(stderr, "DEBUG: %s:%d:%s(): " fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
	} while (0)
#else
#define debug_print(fmt, ...) do {} while (0)
#endif

void print_help()
{

	static char *help_message = "Usage: ping [-v] <target>\n"
		"Options:\n"
		"  -v\tVerbose output\n"
		"  -h\tShow this help message\n";

	fprintf(stderr, "%s", help_message);
	exit(0);
}

char *get_reverse_dns(struct sockaddr_in *addr)
{
	static char host[NI_MAXHOST];

	if (getnameinfo((struct sockaddr *)addr, sizeof(*addr),
					host, sizeof(host),
					NULL, 0, NI_NAMEREQD) == 0)
	{
		return host;
	}
	return NULL;
}

int	check_args(int argc, char **argv, t_ping *data)
{
	debug_print("Checking arguments...\n");
	if (argc < 2)
	{
		fprintf(stderr, "Usage: %s [-v] <target>\n", argv[0]);
		return -1;
	}

	for (int i = 1; i < argc; i++)
	{
		if (argv[i][0] == '-' && strlen(argv[i]) > 1)
		{
			for (size_t j = 1; j < strlen(argv[i]); j++)
			{
				if (argv[i][j] == 'v')
					data->verbose = 1;
				else if (argv[i][j] == 'h')
					print_help();
				else
				{
					fprintf(stderr, "Unknown option: -%c\n", argv[i][j]);
					return -1;
				}
			}
		}
		else {
			if (data->ip != NULL)
			{
				fprintf(stderr, "Multiple targets specified: %s and %s\n", data->ip, argv[i]);
				return -1;
			}
			data->ip = argv[i];
		}
	}

	return 0;
}

int resolve_hostname(t_ping *data)
{
	struct addrinfo hints = {
		.ai_flags = AI_CANONNAME,
		.ai_family = AF_INET,
		.ai_socktype = SOCK_RAW,
		.ai_protocol = IPPROTO_ICMP
	};

	struct addrinfo *res;

	int status = getaddrinfo(data->ip, NULL, &hints, &res);
	if (status != 0)
	{
		fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
		return -1;
	}

	memcpy(&data->addr, res->ai_addr, sizeof(struct sockaddr_in));

	freeaddrinfo(res);

	return 0;
}

int setup_socket(t_ping *data)
{
	int sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
	if (sockfd < 0)
	{
		fprintf(stderr, "Error creating socket: %s\n", strerror(errno));
		return -1;
	}

	data->sockfd = sockfd;
	return 0;
}

uint16_t setup_checksum(unsigned char *data, size_t len)
{
	uint32_t sum = 0;
	uint16_t *ptr = (uint16_t *)data;

	while (len > 1)
	{
		sum += *ptr++;
		len -= 2;
	}

	if (len > 0)
		sum += *(unsigned char *)ptr;

	while (sum >> 16)
		sum = (sum & 0xFFFF) + (sum >> 16);

	return ~sum;
}

void setup_packet(unsigned char *packet)
{
	static uint16_t seq_num = 0;

	struct timeval ts;

	struct icmphdr *icmp = (struct icmphdr *)packet;

	memset(packet, 0, sizeof(struct icmphdr) + PAYLOAD_SIZE);

	icmp->type = ICMP_ECHO;
	icmp->code = 0;

	icmp->un.echo.id = htons(getpid() & 0xFFFF);
	icmp->un.echo.sequence = htons(seq_num++);

	gettimeofday(&ts, NULL);

	memcpy(packet + sizeof(struct icmphdr), &ts, sizeof(struct timeval));

	icmp->checksum = setup_checksum(packet, sizeof(struct icmphdr) + PAYLOAD_SIZE);

}

int send_ping(t_ping *data, unsigned char *packet)
{
	ssize_t bytes_sent = sendto(data->sockfd, packet, sizeof(struct icmphdr) + PAYLOAD_SIZE, 0,
		(struct sockaddr *)&data->addr, sizeof(data->addr));

	if (bytes_sent < 0)
	{
		fprintf(stderr, "Error sending ping: %s\n", strerror(errno));
		return -1;
	}

	data->stats.transmitted++;

	return 0;
}

/* icmp_seq is the sequence number of the ICMP packet, ident is the identifier, ttl is the time to live, and rtt is the round trip time in milliseconds */

void print_first(const t_ping *data)
{
	if (data->verbose)
		printf("PING %s (%s): %d data bytes, id 0x%04x = %d\n",
				data->ip, inet_ntoa(data->addr.sin_addr), PAYLOAD_SIZE, data->pid, data->pid);
	else
		printf("PING %s (%s): %d data bytes\n",
				data->ip, inet_ntoa(data->addr.sin_addr), PAYLOAD_SIZE);
}

void print_mid(t_icmp_reply *reply, double rtt)
{
	printf("%ld bytes from %s (%s): icmp_seq=%d ttl=%d time=%.3f ms\n",
		sizeof(struct icmphdr) + PAYLOAD_SIZE,
		get_reverse_dns(&reply->addr) ? get_reverse_dns(&reply->addr) : "unknown",
		inet_ntoa(reply->addr.sin_addr),
		ntohs(reply->seq),
		reply->ttl,
		rtt);
}

void print_last(t_ping *data)
{
	t_stats *stats = &data->stats;

	const int packet_loss = stats->transmitted > 0 ? (stats->transmitted - stats->received) * 100 / stats->transmitted : 0;

	printf("\n--- %s ping statistics ---\n", data->ip);
	printf("%d packets transmitted, %d received, %d%% packet loss\n",
		stats->transmitted, stats->received, packet_loss);

	if (stats->received > 0)
	{
		double rtt_avg = stats->rtt_sum / stats->received;
		printf("rtt min/avg/max = %.3f/%.3f/%.3f ms\n",
			stats->rtt_min, rtt_avg, stats->rtt_max);
	}
}

int icmp_parse(const char* buffer, ssize_t len, t_icmp_reply* out)
{
    const struct ip* ip_hdr  = (const struct ip*)buffer;
    const int        ip_hlen = (int)(ip_hdr->ip_hl) * 4;

    if (len < ip_hlen + (ssize_t)sizeof(struct icmp))
        return -1;

    const struct icmp* icmp = (const struct icmp*)(buffer + ip_hlen);

    if (icmp->icmp_type != ICMP_ECHOREPLY) {
        out->type = icmp->icmp_type;
        out->code = icmp->icmp_code;
        return 1;
    }

	if (ntohs(icmp->icmp_id) != (uint16_t)(getpid() & 0xffff))
		return -1;


    out->seq   = icmp->icmp_seq;
    out->ttl   = ip_hdr->ip_ttl;
    out->bytes = len - ip_hlen;

	out->ts = *(struct timeval *)(icmp->icmp_data);

    return 0;
}

int receive_ping(t_ping *data)
{
	unsigned char buffer[4096];
	struct timeval end;
	t_icmp_reply reply;


	reply.addr_len = sizeof(reply.addr);

	const ssize_t bytes_received = recvfrom(data->sockfd,
		buffer, sizeof(buffer), 0,
		(struct sockaddr *)&reply.addr,
		&reply.addr_len);

	gettimeofday(&end, NULL);

	if (bytes_received < 0)
	{
		if (errno == EINTR)
			return 1;

		fprintf(stderr, "Error receiving ping: %s\n", strerror(errno));
		return -1;
	}

	const int parse_result = icmp_parse((const char*)buffer, bytes_received, &reply);

	if (parse_result == -1)
		return 1;

	if (parse_result == 1)
	{
		if (data->verbose)
		{
			printf("Received ICMP type %d code %d from %s\n",
				reply.type, reply.code, inet_ntoa(reply.addr.sin_addr));
		}
		return 1;
	}


	const double rtt = (end.tv_sec - reply.ts.tv_sec) * 1000.0 + (end.tv_usec - reply.ts.tv_usec) / 1000.0;

	t_stats *stats = &data->stats;

	stats->received++;

	if (rtt < stats->rtt_min)
		stats->rtt_min = rtt;

	if (rtt > stats->rtt_max)
		stats->rtt_max = rtt;

	stats->rtt_sum += rtt;

	print_mid(&reply, rtt);

	return 1;
}

volatile sig_atomic_t ping_loop = 1;
volatile sig_atomic_t send_packet = 1;

void handler(int signum)
{
	if (signum == SIGINT)
		ping_loop = 0;
	else if (signum == SIGALRM)
		send_packet = 1;
}

int ft_ping(t_ping *data)
{
	unsigned char packet[sizeof(struct icmphdr) + PAYLOAD_SIZE];
	struct sigaction sa;
	int ret;

	if (resolve_hostname(data) != 0)
		return -1;

	if (setup_socket(data) != 0)
		return -1;

	print_first(data);

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;

	if (sigaction(SIGINT, &sa, NULL) < 0)
	{
		perror("sigaction SIGINT");
		close(data->sockfd);
		return -1;
	}

	if (sigaction(SIGALRM, &sa, NULL) < 0)
	{
		perror("sigaction SIGALRM");
		close(data->sockfd);
		return -1;
	}

	while (ping_loop)
	{
		if (send_packet)
		{
			send_packet = 0;

			setup_packet(packet);

			if (send_ping(data, packet) != 0)
				break;

			alarm(1);
		}

		ret = receive_ping(data);

		if (ret < 0)
			break;
	}

	alarm(0);
	close(data->sockfd);

	print_last(data);

	return 0;
}

void init_ping(t_ping *data)
{
	data->verbose = 0;
	data->sockfd = -1;
	data->ip = NULL;
	data->pid = getpid() & 0xFFFF;
	memset(&data->addr, 0, sizeof(data->addr));
	memset(&data->stats, 0, sizeof(data->stats));
	data->stats.rtt_min = DBL_MAX;
}

int	main(int argc, char **argv)
{
	t_ping data;
	init_ping(&data);

	if (check_args(argc, argv, &data) != 0)
		return -1;

	debug_print("Arguments parsed successfully: verbose=%d, target=%s\n", data.verbose, data.ip);

	ft_ping(&data);

	return 0;
}
