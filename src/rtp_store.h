/*
 * rtp_store.h
 *
 *  Created on: Aug 15, 2009
 *      Author: Matus Valo
 *      E-mail: matusvalo@gmail.com
 */

#ifndef RTP_STORE_H_
#define RTP_STORE_H_

#include <stdint.h>
#include <limits.h>
#include <sys/types.h>

/**
 * Enumeration that represents states of RTP stream.
 */
typedef enum {
	RTP_FAILED = 0,					/**< error occured during finding out stream state*/
	RTP_INITIALIZING = 1,			/**< Stream is initializing*/
	RTP_RECORDING = 2, 				/**< stream is recording*/
	RTP_WAITING = 3, 				/**< Stream is waiting for data on network*/
	RTP_ENDED = 4					/**< stream has ended*/
} rtp_stream_state_t;

/**
 * Sets maximum file size quota to unbounded.
 */
#define MAX_FSIZE_QUOTA_UNBOUNDED 0

/**
 * Enumeration that represents level of logging.
 */
typedef enum {
	RTP_DEBUG = 1 << 0,		/**<Debugging informations only*/		//1
	RTP_INFO = 1 << 1,		/**<Information logs only*/			//2
	RTP_WARN = 1 << 2,		/**<Warning logs only*/				//4
	RTP_ERROR = 1 << 3,		/**<Error logs only*/				//8
	RTP_FATAL = 1 << 3,		/**<Fatal Error logs only*/			//8
	RTP_OFF = 0,			/**<Logging off*/				//0
	RTP_ALL = INT_MAX		/**<All logs*/ 					//pole 1-tiek
} rtp_log_level_t;

/**
 * Initializes logging to file.
 * \param flog - file path of logging output file. "stdout" for standard output and
 * "stderr for standard error output.
 * \param log_levels - Enabled levels of logging.
 * \param max_fsize_quota - Maximum size of rollback (log file) in bytes.
 * When passed macro MAX_FSIZE_QUOTA_UNBOUNDED, then is rollback size quota unbounded.
 * \param rb_count - Count of rollbacks (log files).
 * \returns 0 on success, -1 otherwise.
 */
int rtp_store_loginit(const char *flog, rtp_log_level_t log_levels, unsigned int max_fsize_quota, unsigned int rb_count);

/**
 * Inits logging to remote location. The log messages are sent by UDP protocol.
 * \param ip IP address of remote destination, where logs should be sent.
 * \param port Port of remote destination, where logs shoud be sent.
 * \levels Levels of logging, that should be logged to remote destination.
 * \returns 0 on success and -1 on error. //TODO:rozlisenie na zaklade chyby.
 */
int rtp_store_remote_loginit(const char *ip, uint16_t port, rtp_log_level_t levels);

/**
 * Closes logging to remote destination.
 */
void rtp_store_remote_logclose(void);

/**
 * Closes logging to file.
 */
void rtp_store_logclose(void);

/**
 * Returns state of stream.
 * \param id ID of stream.

 * \returns state of stream on success, (rtp_stream_state_t) -1 otherwise.
 */
rtp_stream_state_t rtp_get_stream_state(int id);

/**
 * Returns speed of downloading.
 * \param id ID of stream.
 * \returns current speed on success, -1 otherwise.
 */
double rtp_get_stream_download_speed(int id);

/**
 * Returns size of downloaded data.
 * \param id ID of stream.
 * \return size of downloaded data on success, -1 otherwise.
 */
off64_t rtp_get_stream_dsize(int id);

/**
 * Initializates RTPStore.
 */
void rtp_store_init(void);

/**
 * Creates new RTP stream.
 * \param ip IP of recipient.
 * \param video_port Port of video session.
 * \param audio_port Port of audio session.
 * \param file_path File name, of index file. Data will be stored in files <file_path>.<integer suffix>.irtp
 * If file doesnt exist, will be created, if
 * exists, will be truncated to zero length.
 * *\param max_fsize_quota Maximum permited file size (in bytes), where RTP
 * stream is stored (when reached new file is created). When passed macro
 * MAX_FSIZE_QUOTA_UNBOUNDED, then is file size quota unbounded.
 * \return ID of created RTP stream on success, -1 otherwise.
 */
int rtp_store_create_stream(char *ip, uint16_t video_port, uint16_t audio_port,
						   char *file_path);

/**
 * Closes and frees all resources of RTP stream
 * \param id ID of stream, that will be closed.
 */
int rtp_store_close_stream(int id);

/**
 * Closes and frees all resources of RtpStore. All running streams will be closed.
 */
void rtp_store_close(void);

#endif /* RTP_STORE_H_ */
