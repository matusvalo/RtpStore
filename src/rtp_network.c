/*
 * rtp_network.c
 *
 *  Created on: Aug 16, 2009
 *      Author: Matus Valo
 *      E-mail: matusvalo@gmail.com
 */

#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <errno.h>
#include <string.h>										//strerror()
#include "rtp_foutput.h"
#include "rtp_stream_thread.h"
#include "rtp_store.h"
#include "rtp_network.h"
#include "rtp.h"
#include "vat.h"
#include "log.h"

#define TRUNC 1000000

/*
 * Module of network implementation.
 */


//Sets sockets of session to no blocking mode.
static int set_socks_nonblock(struct rtp_session *session)
{
	int flags;
	//RTP:
	if((flags = fcntl(session->rtp_sockfd, F_GETFL, 0)) < 0) {			//getting olg flags
		rtp_print_log(RTP_WARN, "Getting flags from RTP socket(FD=%d) failed(%s)\n",
					session->rtp_sockfd, strerror(errno));
		return -1;
	}
	if(fcntl(session->rtp_sockfd, F_SETFL, flags | O_NONBLOCK) < 0) {	//setting nonblocking mode
		rtp_print_log(RTP_WARN, "Setting O_NOBLOCK flag from RTP socket(FD=%d) failed(%s)\n",
					session->rtp_sockfd, strerror(errno));
		return -1;
	}

	//RTCP:
	if((flags = fcntl(session->rtcp_sockfd, F_GETFL, 0)) < 0) {
		rtp_print_log(RTP_WARN, "Getting flags from RTCP socket(FD=%d) failed(%s)\n",
					session->rtcp_sockfd, strerror(errno));
		return -1;
	}
	if(fcntl(session->rtcp_sockfd, F_SETFL, flags | O_NONBLOCK) < 0) {
		rtp_print_log(RTP_WARN, "Setting O_NOBLOCK flag from RTCP socket(FD=%d) failed(%s)\n",
					session->rtcp_sockfd, strerror(errno));
		return -1;
	}

	return 0;
}

//TODO:implementacia DNS
int rtp_net_connect(char *ip, uint16_t rtp_port, struct rtp_session *session)
{
	if(rtp_port % 2 != 0)								//RTP port must be even
		goto ON_ERROR;									//RTCP port must be RTP port + 1

	struct sockaddr_in rtp_addr = {
		.sin_family = AF_INET,
		.sin_port = htons(rtp_port),
		.sin_addr.s_addr = inet_addr(ip)
	};

	struct sockaddr_in rtcp_addr = {
			.sin_family = AF_INET,
			.sin_port = htons(rtp_port + 1),
			.sin_addr.s_addr = inet_addr(ip)
	};

	//------------------------------------------------------
	//skopirovane z RtpStore
	struct ip_mreq rtp_mreq;      /* multicast group */
	struct ip_mreq rtcp_mreq;
	if(ip) {
	      rtp_mreq.imr_multiaddr = rtp_addr.sin_addr;
	      rtp_mreq.imr_interface.s_addr = htonl(INADDR_ANY);
	      rtp_addr.sin_addr = rtp_mreq.imr_multiaddr;

	      rtcp_mreq.imr_multiaddr = rtcp_addr.sin_addr;
	      rtcp_mreq.imr_interface.s_addr = htonl(INADDR_ANY);
	      rtcp_addr.sin_addr = rtcp_mreq.imr_multiaddr;
	}
	/* unicast */
	else {
	      rtp_mreq.imr_multiaddr.s_addr = INADDR_ANY;
	      rtp_addr.sin_addr.s_addr = INADDR_ANY;

	      rtcp_mreq.imr_multiaddr.s_addr = INADDR_ANY;
	      rtcp_addr.sin_addr.s_addr = INADDR_ANY;
	}
	//------------------------------------------------------

	session->rtp_sockfd = socket(PF_INET, SOCK_DGRAM, 0);
	session->rtcp_sockfd = socket(PF_INET, SOCK_DGRAM, 0);
	if(session->rtp_sockfd == -1) {
		rtp_print_log(RTP_ERROR, "Rtp socket(ip=%s, port=%d) failed with errno=%d\n",
				ip, rtp_port, strerror(errno));
		goto ON_ERROR;
	}
	if(session->rtcp_sockfd == -1) {
		rtp_print_log(RTP_ERROR, "Rtcp socket(ip=%s, port=%d) failed with errno=%d\n",
				ip, rtp_port + 1, strerror(errno));
		goto ON_ERROR;
	}

	//setting nonblocking mod of sockets (see man 2 select - part bugs)
	set_socks_nonblock(session);

	//------------------------------------------------------
	//skopirovane z RtpStore
	int one = 1;
	if(IN_CLASSD(ntohl(rtp_mreq.imr_multiaddr.s_addr)))
	          setsockopt(session->rtp_sockfd, SOL_SOCKET, SO_REUSEADDR, (char *) &one, sizeof(one));

	if(IN_CLASSD(ntohl(rtcp_mreq.imr_multiaddr.s_addr)))
		          setsockopt(session->rtcp_sockfd, SOL_SOCKET, SO_REUSEADDR, (char *) &one, sizeof(one));
	//-----------------------------------------------------

	//TODO:kontrola podla errno + kontrola parnosti, neparnosti portov

	if(bind(session->rtp_sockfd, (struct sockaddr *) &rtp_addr, sizeof(rtp_addr)) < 0) {
		rtp_print_log(RTP_ERROR, "RTP socket (ip=%s, port=%d) bind failed with errno=%s\n",
					ip, rtp_port, strerror(errno));
		goto ON_ERROR;
	}

	if(bind(session->rtcp_sockfd, (struct sockaddr *) &rtcp_addr, sizeof(rtcp_addr)) < 0) {
		rtp_print_log(RTP_ERROR, "RTCP socket (ip=%s, port=%d) bind failed with errno=%s\n",
							ip, rtp_port + 1, strerror(errno));
		goto ON_ERROR;
	}

	//------------------------------------------------------
	//skopirovane z RtpStore
	if(IN_CLASSD(ntohl(rtp_mreq.imr_multiaddr.s_addr))) {
		if(setsockopt(session->rtp_sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&rtp_mreq, sizeof(rtp_mreq)) < 0)
			goto ON_ERROR;
	}
	if(IN_CLASSD(ntohl(rtcp_mreq.imr_multiaddr.s_addr))) {
		if(setsockopt(session->rtcp_sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&rtcp_mreq, sizeof(rtcp_mreq)) < 0)
			goto ON_ERROR;
	}
	//------------------------------------------------------

	rtp_print_log(RTP_DEBUG, "Rtp session (ip=%s, rtp port=%d) net connect successed\n",
			ip, rtp_port);
	return 0;


	ON_ERROR:
	rtp_net_close(session);
	return -1;
}

void rtp_net_close(struct rtp_session *session)
{
	if(session->rtp_sockfd > 0) {
		close(session->rtp_sockfd);
		session->rtp_sockfd = -1;
		rtp_print_log(RTP_DEBUG, "RTP session closed.\n");
	}
	if(session->rtcp_sockfd > 0) {
		close(session->rtcp_sockfd);
		session->rtcp_sockfd = -1;
		rtp_print_log(RTP_DEBUG, "RTCP session closed.\n");
	}
}

// Konverzia timeval na double.
static inline double tdbl(struct timeval *a)
{
   return(a->tv_sec + a->tv_usec/1e6);
}

static int parse_header(char *buf)
{
  rtp_hdr_t *r = (rtp_hdr_t *)buf;
  int hlen = 0;

  if (r->version == 0) {
     vat_hdr_t *v = (vat_hdr_t *) buf;
     hlen = 8 + v->nsid * 4;
  }
  else if (r->version == RTP_VERSION) {
     hlen = 12 + r->cc * 4;
  }

  return(hlen);
}

static inline int rtcp_packet_filter(char *buf, int len)
{
	return 1;
}

static inline int rtp_packet_filter(char *buf, int len)
{
   rtp_hdr_t *r = (rtp_hdr_t *) buf;

   if (r->version == RTP_VERSION)
		return 1;

	return 0;
}

static int packet_handler(struct timeval now, int is_rtcp, RD_buffer_t *packet, int len,
						rtp_session_type_t stream_type, struct rtp_stream *stream)
{
	double dnow = tdbl(&now);
	int	hlen;   								/* header length */
	int	offset;

	if((stream->first_rtp == -1) && (!is_rtcp)) {
		stream->first_rtp = dnow - 3;
		if(stream->first_rtp < 0) stream->first_rtp = 0;
	}

	hlen = is_rtcp ? len : parse_header(packet->p.data);
	offset = (int)((dnow - stream->first_rtp) * 1000);
	packet->p.hdr.offset = htonl(offset);
	packet->p.hdr.plen = is_rtcp ? 0 : htons(len);

	// truncation of payload
	if(!is_rtcp && (len - hlen > TRUNC))
		len = hlen + TRUNC;
	packet->p.hdr.length = htons(len + sizeof(packet->p.hdr));

	if(stream->first_rtp >= 0) {
		if(is_rtcp) {
			if(rtcp_packet_filter(packet->p.data, len) != 0)
				return rtp_write_packet(stream_type, packet, len + sizeof(packet->p.hdr), stream);
		}
		else {
			if (rtp_packet_filter(packet->p.data, len) != 0)
				return rtp_write_packet(stream_type, packet, len + sizeof(packet->p.hdr), stream);
		}
	}
	return 0;
}

ssize_t read_from_sock(int sockfd, rtp_session_type_t session_type, int is_rtcp, struct rtp_stream *stream)
{
	struct timeval now;
	ssize_t len = -1;
	RD_buffer_t packet;
	gettimeofday(&now, 0);
	len = recv(sockfd, packet.p.data, sizeof(packet.p.data), 0);	//v originale recvfrom
	if(len == -1) {
		rtp_print_log(RTP_WARN, "recv() failed with errno %s\n", strerror(errno));
		return 0;
	}

	packet_handler(now, is_rtcp, &packet, len, session_type, stream);
	return len;
}
