/*
 * rtp_stream_thread.h
 *
 *  Created on: Aug 16, 2009
 *      Author: Matus Valo
 *      E-mail: matusvalo@gmail.com
 */
#ifndef RTP_STREAM_THREAD_H_
#define RTP_STREAM_THREAD_H_

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include "rtp_store.h"


/**
 * Enumeration that represents types of RTP sessions.
 */
typedef enum {
	RTP_AUDIO,							/**< audio session*/
	RTP_VIDEO							/**< video session*/
} rtp_session_type_t;

/**
 * Structure that represents RTP session.
 */
struct rtp_session {
	int rtp_sockfd;						/**< File descriptor of RTP socket*/
	int rtcp_sockfd;					/**< File descriptor of RTCP socket*/
};

/**
 * Structure that represents informations about RTP stream.
 */
struct rtp_stream_info {
	off64_t downloaded_data_size;				/**< size of downloaded data in bytes*/
	double download_speed;						/**< speed of downloading in kb/s*/
	rtp_stream_state_t rtp_stream_state;		/**< state of RTP stream*/
};

/**
 * Structure that represents RTP stream
 */
struct rtp_stream {
	struct rtp_session video_session;		/**< video session*/
	struct rtp_session audio_session;		/**< audio session*/

	char *file_name;						/**< output file name.*/
	FILE *output_file;						/**< output file, where data will be stored*/

	pthread_t *rtp_executor;				/**< thread that handles stream*/
	pthread_mutex_t stream_mutex;			/**< locking mutex to access stream_info*/
	struct rtp_stream_info stream_info;		/**< informations about stream*/
};

/**
 * Returns information about state of stream.
 * \param stream Stream which will be returned informations from.
 * \returns Struct rtp_stream_info, that represents informations about stream.
 */
struct rtp_stream_info rtp_get_stream_info(struct rtp_stream *stream);

/**
 * Creates and initializes new RTP stream.
 * \param ip IP address of recipient in dotted format.
 * \param rtp_video_port Port of RTP video session. Must be even.
 * \param rtp_audio_port Port of RTP audio session. Must be odd.
 * \param output Path to output file. If file doesnt exist, will be created, if
 * exists, will be truncated to zero length.
 * *\param max_fsize_quota Maximum permited file size, where RTP stream is stored
 * (when reached new file is created)
 * \return Pointer to structure rtp_stream that represents stream.
 */
struct rtp_stream *rtp_stream_init(char *ip, uint16_t rtp_video_port,
								   uint16_t rtp_audio_port, char *output);

/**
 * Creates and runs new thread of RTP stream.
 * \param stream Stream which will be run.
 * \return 0 on success, -1 otherwise.
 */
int rtp_stream_run(struct rtp_stream *stream);


/**
 * Closes rtp_stream.
 * \param stream Stream that should be closed.
 */
void rtp_stream_close(struct rtp_stream *stream);

#endif /* RTP_STREAM_THREAD_H_ */
