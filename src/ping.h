#ifndef PING_G
# define PING_G

#include <stdint.h>
#include <netinet/in.h>

#define PAYLOAD_SIZE 56

typedef struct s_stats
{
	int transmitted;
	int received;
	double rtt_min;
	double rtt_max;
	double rtt_sum;
}	t_stats;

typedef struct s_icmp_reply
{
	uint16_t seq;
	uint8_t ttl;
	uint16_t bytes;
	uint8_t type;
	uint8_t code;
	struct sockaddr_in addr;
	socklen_t addr_len;
	struct timeval ts;
}	t_icmp_reply;

typedef struct s_ping
{
	int verbose;
	int sockfd;
	const char *ip;
	pid_t pid;
	struct sockaddr_in addr;
	struct s_stats stats;
}	t_ping;

#endif
