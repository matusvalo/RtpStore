/*
 * rtp_types.h
 *
 *  Created on: Aug 15, 2009
 *      Author: Matus Valo
 *      E-mail: matusvalo@gmail.com
 */

#ifndef RTP_FOUTPUT_H_
#define RTP_FOUTPUT_H_

#include <bits/time.h>			//struct timeval
#include <stdint.h>
#include <stdio.h>
#include "rtp_stream_thread.h"

/**
 * Module for saving rtp data in files.
 */

/*
* rtpdump file format
*
* The file starts with the tool to be used for playing this file,
* the multicast/unicast receive address and the port.
*
* #!rtpplay1.0 224.2.0.1/3456\n
*
* This is followed by one binary header (RD_hdr_t) and one RD_packet_t
* structure for each received packet.  All fields are in network byte
* order.  We don't need the source IP address since we can do mapping
* based on SSRC.  This saves (a little) space, avoids non-IPv4
* problems and privacy/security concerns. The header is followed by
* the RTP/RTCP header and (optionally) the actual payload.
*/

#define RTPFILE_VERSION "2.0"

typedef struct {
  struct timeval start;  /* start of recording (GMT) */
  uint32_t source;        /* network source (multicast address) */
  uint16_t port;          /* UDP port */
} RD_hdr_t;

typedef struct {
  uint16_t length;    /* length of packet, including this header (may
                        be smaller than plen if not whole packet recorded) */
  uint16_t plen;      /* actual header+payload length for RTP, 0 for RTCP */
  uint32_t offset;    /* milliseconds since the start of recording */
} RD_packet_t;

typedef union
{
  struct
  {
    RD_packet_t hdr;
    char data[8000];
  } p;
  char byte[8192];
} RD_buffer_t;


/**
 * Initializates created file output. (Must be called for video and for audio)
 * \param stream Stream that output file belongs.
 * \param addr Address of recipient in dotted format.
 * \param port Port of recipient.
 * \return 0 on success -1 otherwise.
 */
int rtp_init_stream_output(struct rtp_stream *stream, char *addr, uint16_t port);

/**
 *Create file output.
 *\param stream Stream, for which will be file output created.
 *\param file_path Path of the file that will be output.
 *\param max_fsize_quota Maximum permited file size, where RTP stream is stored (when reached new file is created)
 *\return 0 on success, -1 otherwise.
 */
int rtp_create_stream_output(struct rtp_stream *stream, char *file_path);

/**
 * Closes file output of stream.
 * \param stream. Stream that output file belongs.
 * \return Upon successful completion 0 is returned. Otherwise, EOF is returned.
 */
int rtp_close_stream_output(struct rtp_stream *stream);

/**
 * Writes rtp packet to file.
 *\param stream_type Type of stream which packet belongs to.
 *\param packet Packet that is being stored.
 *\param len Length of the packet.
 *\param stream Stream that packet belongs to.
 */
int rtp_write_packet(rtp_session_type_t stream_type, RD_buffer_t *packet, int len, struct rtp_stream *stream);

#endif /* RTP_FOUTPUT_H_ */
