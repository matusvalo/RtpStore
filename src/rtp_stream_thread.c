/*
 * rtp_stream_thread.c
 *
 *  Created on: Aug 16, 2009
 *      Author: Matus Valo
 *      E-mail: matusvalo@gmail.com
 */
#define _THREAD_SAFE		//additional objects for thread environment

#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <string.h>						//strerror()
#include <errno.h>
#include <sys/time.h>					//gettimeofday()
#include "rtp_store.h"
#include "log.h"
#include "rtp_stream_thread.h"
#include "rtp_network.h"
#include "rtp_foutput.h"

//Time of period in seconds. Period is time between two synchronizing events.
#define MAX_PERIOD_TIME 5

/* macro calculating length of period in seconds
 * bg - beginning of period in struct timeval
 * end - end of period in struct timeval
 * returns time in seconds in double (eg. 1,23 s)
*/
#define PERIOD_TIME(bg, end) ((end).tv_sec + (double) (end).tv_usec/(double)1000000 -	\
				((bg).tv_sec + (double) (bg).tv_usec / (double) 1000000))

/* macro calculating speed in kb/s
 * size - size of data in Bytes. Its type is size_t
 * time - time of I/O data of size <size> in seconds
 * returns speed of I/O in kb/s. Its type is double
 */
#define SPEED(size, time) ((((double) (size)) * 8 / (double) 1024) / (double) (time))

struct rtp_stream_info rtp_get_stream_info(struct rtp_stream *stream)
{
	struct rtp_stream_info stream_inf;

	pthread_mutex_lock(&(stream->stream_mutex));				//kriticka sekcia
	stream_inf = stream->stream_info;
	pthread_mutex_unlock(&(stream->stream_mutex));				//koniec krit. sekcie

	return stream_inf;
}

//creates and initializates new stream.
static struct rtp_stream *create_stream(void)
{
	struct rtp_stream *stream = (struct rtp_stream *) malloc(sizeof(struct rtp_stream));
	if(stream == NULL) {
		rtp_print_log(RTP_ERROR, "Malloc of stream failed\n");
		return NULL;
	}

	stream->output_file = NULL;
	stream->file_name = NULL;

	stream->rtp_executor = NULL;

	stream->audio_session.rtp_sockfd = -1;
	stream->audio_session.rtcp_sockfd = -1;
	stream->video_session.rtp_sockfd = -1;
	stream->video_session.rtcp_sockfd = -1;

	stream->stream_info.download_speed = 0;
	stream->stream_info.downloaded_data_size = 0;
	stream->stream_info.rtp_stream_state = RTP_INITIALIZING;

	stream->first_rtp = -1;

	rtp_print_log(RTP_DEBUG, "Rtp stream created\n");
	return stream;
}

struct rtp_stream *rtp_stream_init(char *ip, uint16_t rtp_video_port,
									uint16_t rtp_audio_port, char *file_path)
{
	struct rtp_stream *stream = create_stream();
	if(stream == NULL) goto ON_ERROR;

	if(rtp_net_connect(ip, rtp_video_port, &(stream->video_session)) == -1)
		goto ON_ERROR;					//upratanie za sebou

	if(rtp_net_connect(ip, rtp_audio_port, &(stream->audio_session)) == -1)
		goto ON_ERROR;		//co ak zbehne prvy rtp_connect a druhy uz nezbehne -> free prvy :)

	if(rtp_create_stream_output(stream, file_path) == -1)
		goto ON_ERROR;
	rtp_init_stream_output(stream, ip, rtp_video_port);
	rtp_init_stream_output(stream, ip, rtp_audio_port);

	//inicializacia mutexov.
	if(pthread_mutex_init(&(stream->stream_mutex), NULL) != 0) {
		rtp_print_log(RTP_ERROR, "Initializing thread mutex failed:%s\n", strerror(errno));
		goto ON_ERROR;
	}

	return stream;

	ON_ERROR:
	rtp_stream_close(stream);
	return NULL;
}

//Initializates file descriptors for select(). Returns max. number of fds.
static inline int init_select(fd_set *readfds, struct rtp_stream *stream)
{
	FD_ZERO(readfds);
	FD_SET(stream->audio_session.rtp_sockfd, readfds);
	FD_SET(stream->video_session.rtp_sockfd, readfds);
	FD_SET(stream->audio_session.rtcp_sockfd, readfds);
	FD_SET(stream->video_session.rtcp_sockfd, readfds);

	int maxfds = -1;
	if(stream->audio_session.rtp_sockfd > maxfds) maxfds = stream->audio_session.rtp_sockfd;
	if(stream->video_session.rtp_sockfd > maxfds) maxfds = stream->video_session.rtp_sockfd;
	if(stream->audio_session.rtcp_sockfd > maxfds) maxfds = stream->audio_session.rtcp_sockfd;
	if(stream->video_session.rtcp_sockfd > maxfds) maxfds = stream->video_session.rtcp_sockfd;

	return maxfds;
}

//Reads from selected file descriptors. Returns number of received bytes.
static inline ssize_t read_from_fds(fd_set *readfds, struct rtp_stream *stream)
{
	ssize_t downloaded_size = 0;
	if(FD_ISSET(stream->audio_session.rtp_sockfd, readfds))
		downloaded_size += read_from_sock(stream->audio_session.rtp_sockfd, RTP_AUDIO, 0, stream);
	if(FD_ISSET(stream->audio_session.rtcp_sockfd, readfds))
		downloaded_size += read_from_sock(stream->audio_session.rtcp_sockfd, RTP_AUDIO, 1, stream);
	if(FD_ISSET(stream->video_session.rtp_sockfd, readfds))
		downloaded_size += read_from_sock(stream->video_session.rtp_sockfd, RTP_VIDEO, 0, stream);
	if(FD_ISSET(stream->video_session.rtcp_sockfd, readfds))
		downloaded_size += read_from_sock(stream->video_session.rtcp_sockfd, RTP_VIDEO, 1, stream);
	return downloaded_size;
}

//Handler of signal SIGQUIT that will be called on canceling thread.
static void on_cancel(int sig)
{
	pthread_exit(NULL);
}

//Execution handler of stream.
static void *rtp_stream_handler(void *param)
{
	struct rtp_stream *stream = (struct rtp_stream *) param;
	signal(SIGQUIT, on_cancel);

	//Start time of last synchronizing part.
	struct timeval select_timeout = {
			.tv_sec = MAX_PERIOD_TIME,
			.tv_usec = 0
	 };

	stream->stream_info.rtp_stream_state = RTP_WAITING;

	ssize_t period_downloaded_size = 0;						//amount of data downloaded in period in Bytes
	struct timeval start_time;								//start time of period
	gettimeofday(&start_time, NULL);
	rtp_print_log(RTP_DEBUG, "Starting main loop of stream\n");
	double speed = 0;										//speed in kb/s
	while(1) {
		fd_set readfds;
		int maxfds = init_select(&readfds, stream);
		select(maxfds + 1, &readfds, NULL, NULL, &select_timeout);
		select_timeout.tv_sec = MAX_PERIOD_TIME;				//because select() updates parameter timeout

		ssize_t downloaded_size = read_from_fds(&readfds, stream);
		period_downloaded_size += downloaded_size;

		struct timeval end_time;							//end time of period
		gettimeofday(&end_time, NULL);

		if(end_time.tv_sec - start_time.tv_sec >= MAX_PERIOD_TIME) {
			speed = SPEED(period_downloaded_size, PERIOD_TIME(start_time, end_time));
			rtp_print_log(RTP_DEBUG, "Speed=%.1f kb/s, downloaded_size=%zd B\n",
					speed, period_downloaded_size);
			start_time = end_time;					//inicializing for next period
			period_downloaded_size = 0;
		}

		//Synchronization part - synchronizing state of stream
		pthread_mutex_lock(&(stream->stream_mutex));					//critical section
		stream->stream_info.rtp_stream_state = (downloaded_size == 0 ? RTP_WAITING : RTP_RECORDING);
		stream->stream_info.downloaded_data_size += (off64_t) downloaded_size;
		stream->stream_info.download_speed = speed;
		pthread_mutex_unlock(&(stream->stream_mutex));					//out crit. section
	}

	return NULL;
}

//Runs stream given as parameter.
int rtp_stream_run(struct rtp_stream *stream)
{
	if(stream == NULL) goto ON_ERROR;
	if(stream->rtp_executor != NULL) goto ON_ERROR;

	stream->rtp_executor = (pthread_t *) malloc(sizeof(pthread_t));
	if(stream->rtp_executor == NULL) {
		rtp_print_log(RTP_ERROR, "Rtp executor malloc failed\n");
		goto ON_ERROR;
	}

	int res = -1;										//rozlisovat chybove cisla
	pthread_attr_t thread_attr;
	res = pthread_attr_init(&thread_attr);
	if(res != 0) goto ON_ERROR;
	res = pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_JOINABLE);
	if(res != 0) goto ON_ERROR;

	res = pthread_create(stream->rtp_executor, &thread_attr, rtp_stream_handler, (void *) stream);
	if(res != 0) goto ON_ERROR;

	pthread_attr_destroy(&thread_attr);
	return 0;

	ON_ERROR:
	rtp_stream_close(stream);
	return -1;
}


void rtp_stream_close(struct rtp_stream *stream)
{
	if(stream == NULL) return;
	if(stream->rtp_executor != NULL) {
		pthread_kill(*(stream->rtp_executor), SIGQUIT);
		void *foo = NULL;
		if(pthread_join(*(stream->rtp_executor), &foo) == -1 && errno == EDEADLK)
			pthread_kill(*(stream->rtp_executor), SIGKILL);		//killing thread in deadlock
		stream->stream_info.rtp_stream_state = RTP_ENDED;
		pthread_mutex_destroy(&(stream->stream_mutex));
		free(stream->rtp_executor);
		rtp_print_log(RTP_DEBUG, "Stream canceled\n");
	}
	rtp_net_close(&(stream->video_session));
	rtp_net_close(&stream->audio_session);

	rtp_close_stream_output(stream);

	free(stream);
}
