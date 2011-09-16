/*
 * rtp_network.h
 *
 *  Created on: Aug 16, 2009
 *      Author: Matus Valo
 *      E-mail: matusvalo@gmail.com
 */

#ifndef RTP_NETWORK_H_
#define RTP_NETWORK_H_

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "rtp_stream_thread.h"

/**
  * Structure representing sizes of packet.
  * downloaded - the size of data received on socket
  * written - the size of data written to hard drive.
  */
struct packet_size {
    ssize_t downloaded;
    ssize_t written;
};

/**
 * Function creates network connection. Creates sockets on desired port and IP address.
 * \param ip IP address, on which should be listening for RTP traffic.
 * \param port port, on which should be listening for RTP traffic.
 * \param session RTP session to which network connection belongs.
 * \return 0 on success, -1 otherwise.
 */
int rtp_net_connect(char *ip, uint16_t rtp_port, struct rtp_session *session);

/**
 * Closes network connection for RTP session session.
 * \param session RTP session, which network connection should be closed.
 */
void rtp_net_close(struct rtp_session *session);

/**
 * Reads from network connection and stores data in file specified in struct rtp_stream *stream.
 * \param sockfd File descriptor of socket.
 * \param stream_type Type of stream, which will be data read from.
 * \param is_rtcp Should be 1 if session of stream is RTCP, 0 otherwise.
 * \param stream Stream which should be data read from.
 * \param psize structure with data size informations.
 * \return 0 on success otherwise -1
 */
int read_from_sock(int sockfd, rtp_session_type_t stream_type, int is_rtcp, struct rtp_stream *stream,
                       struct packet_size *psize);

#endif /* RTP_NETWORK_H_ */
