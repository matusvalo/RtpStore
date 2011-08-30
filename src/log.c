/*
 * log.c
 *
 *  Created on: Aug 29, 2009
 *      Author: Matus Valo
 *      E-mail: matusvalo@gmail.com
 */

#define _THREAD_SAFE		//additional objects for thread environment
#define _GNU_SOURCE

#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include "rtp_foutput.h"
#include "log.h"


//makro that prints log to string log_msg (without variable parameters)
//log_msg - *char
//log_level - rtp_log_level_t
//time - * char
//msec - (miliseconds) int
//date - * char
//message - * char
//code - * char (function, where was rtp_print_log() called)
#define FORMAT_LOG(log_msg, log_level, time, msec, date, message, code) sprintf(log_msg, "%s %s,%d [RtpStore:%s()] %s - %s",						\
																	date, time, msec, code, strlog_levels[(int) (log_level)], message);

#define HEADER_LEN 100						//length of header of log message

//Array position of pipe descriptors
#define PIPE_READ 0
#define PIPE_WRITE 1

#define MAX_FSIZE_UNBOUNDED 0				//maximum size of logfile is unbounded

static const char *strlog_levels[] = {"", "DEBUG", "INFO", "", "WARN", "", "", "", "ERROR"};
//bit array of enabled log levels
static rtp_log_level_t log_levels = RTP_OFF;
static rtp_log_level_t remote_log_levels = RTP_OFF;

static FILE *flog = NULL;			//output file, where log is written.
static int roll_backup_maxnum = 0;	//max number suffix of log files
static int roll_backup_num = 0;		//current maximum number suffix of log files
static off_t max_fsize = 0;		//maximum size of log file
static off_t cur_fsize = 0;		//current size of log file
static char *flog_name = NULL;		//name of log file

enum log_thread_state_t {RUNNING, ENDING};
static volatile enum log_thread_state_t log_thread_state = RUNNING;
static pthread_t *log_thread = NULL;	//logging thread.

static int sockfd = -1;				//file deskriptor of socket, where logs are sent.
static int pipefd[2];						//file descriptors of pipe.
static struct sockaddr_in addr;

//Structure containing informations about a log message.
struct log_head {
	unsigned int log_len;				//length of log message
	rtp_log_level_t log_level;			//level of log message
};

//Puts log message into pipe with given loggin level.
static inline int put_log(char *log, rtp_log_level_t log_level)
{
	struct log_head lh = {
		.log_len = strlen(log) + 1,
		.log_level = log_level
	};

	/* Part, where log message and log are joined in a single variable tmp.
	 * They are joined because we need to call write() only once (This is so
	 * because separate writing header and log message to pipe can be interrupted
	 * by another thread).*/
	uint8_t *tmp = (uint8_t *) malloc(sizeof(lh) + lh.log_len * sizeof(uint8_t));
	memcpy(tmp, (void *) &lh, sizeof(lh));
	memcpy((void *) (tmp + sizeof(lh)), (void *) log, lh.log_len);

	if(write(pipefd[PIPE_WRITE], tmp, sizeof(lh) + lh.log_len * sizeof(uint8_t)) == -1)
			goto ON_ERROR;

	free(tmp);
	return 0;

	ON_ERROR:
	free(tmp);
	return -1;

}

//Gets header (contains length of log message and logging level) of log message from a pipe.
static inline int get_log_header(unsigned int *len, rtp_log_level_t *log_level)
{
	struct log_head lh = {
		.log_len = 0,
		.log_level = RTP_OFF
	};
	if(TEMP_FAILURE_RETRY(read(pipefd[PIPE_READ], (void *) &lh, sizeof(lh))) == -1)
		return -1;

	*log_level = lh.log_level;
	*len = lh.log_len;
	return 0;
}

//Gets log message from a pipe. (get_log_header needs to by called first to get length of
//the message
static inline int get_log(char *log, size_t len)
{
	return TEMP_FAILURE_RETRY(read(pipefd[PIPE_READ], (void *) log, len));
}

//Adds suffix number after filename in form:<string>.<number>
//outstr (out) - String with suffix
//instr - String to add suffix
//suffnum - number to add in suffix
static inline int add_suffix(char *outstr, char *instr, int suffnum)
{
	return sprintf(outstr, "%s.%d", instr, suffnum);
}

//Gets file size of most recent rollback (flog_size) and maximum current rollback suffix
//number (roll_backup_num)
static inline int get_flogsize_num(off_t *flog_size, int *roll_backup_num)
{
	struct stat flogstat;
	if(fstat(fileno(flog), &flogstat) == -1)
		return -1;
	*flog_size = flogstat.st_size;
	if(flog_name == NULL)
		return -1;
	int fname_size = strlen(flog_name) + 5;
	int retval = -1;
	int curnum = 0;
	do {
		char fname[fname_size];
		add_suffix(fname, flog_name, ++curnum);
		retval = stat(fname, &flogstat);
	} while(retval != -1);
	*roll_backup_num = curnum - 1;
	return 0;
}

//Shifts rollbacks.
//max_shift_num - maximum number suffix of files
//fnamesize - size of name of rollback files
static inline void shift_rollbacks(int max_shift_num, int fnamesize)
{
	int i;
	for(i = max_shift_num - 1; i >= 1; i--) {
		char foldname[fnamesize];
		char fnewname[fnamesize];
		add_suffix(foldname, flog_name, i);
		add_suffix(fnewname, flog_name, i + 1);
		rename(foldname, fnewname);
	}
	char fname[fnamesize];
	add_suffix(fname, flog_name, 1);
	rename(flog_name, fname);
}

//Creates next rollback in form:<logfile name>.i+1, where previous form was:<logfile name>.i.
//The most actual logfile is in form:<logfile name>. When is reached maximum count of rollbacks
//then last rollback(with the biggist number) is erased, and the rest are shifted adding 1 to
//number.New rollback is created in form <logfile name> and it is opened and writing to it is
//beginned.
static void create_new_rollback(void)
{
	cur_fsize = 0;
	if(flog == stdout || flog == stderr)
		return;

	TEMP_FAILURE_RETRY(fclose(flog));
	flog = NULL;

	int fnamesize = strlen(flog_name) + 5;
	char fname[fnamesize];
	if(roll_backup_num < roll_backup_maxnum) {					//create next rollback
		roll_backup_num++;
		shift_rollbacks(roll_backup_num, fnamesize);
	} else {													//delete last rollback and shit rest
		add_suffix(fname, flog_name, roll_backup_maxnum);
		remove(fname);
		shift_rollbacks(roll_backup_maxnum, fnamesize);
	}
	do {
		flog = fopen(flog_name, "w");
	} while(flog == NULL && errno == EINTR);
}

//Writes logstr to file.
static inline void write_logfile(char *logstr)
{
	if(max_fsize != MAX_FSIZE_UNBOUNDED) {
		cur_fsize += strlen(logstr);
		if(cur_fsize >= max_fsize)
			create_new_rollback();
	}
	if(flog != NULL) {
		fprintf(flog, "%s", logstr);
		TEMP_FAILURE_RETRY(fflush(flog));
	}
}

//Creates new log message.
//\param log_level - level of logging
//\param func - name of function, where where rtp_print_log() was called
//\param message - desired message to print
//\returns log message
static char *create_log(rtp_log_level_t log_level, char *func, char *message)
{
	char *log_msg = (char *) malloc(sizeof(char) * (strlen(message) + HEADER_LEN));

	struct tm *now;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	now = localtime(&tv.tv_sec);
	int msecs = tv.tv_usec / 1000;							//miliseconds

	char time[10];
	char date[12];
	strftime(time, sizeof(time), "%H:%M:%S", now);			//format:H:M:S (hod)
	strftime(date, sizeof(date), "%F", now);				//format:Y-M-D (ISO8601)
	FORMAT_LOG(log_msg, log_level, time, msecs, date, message, func);

	return log_msg;
}

//Loggs given message string logstr of logging level log_level.
//Message can be logged in 2 ways, depending whethever they are turned on (see API in log.h):
//1. Written to a log_file
//2. Sended by UDP protocol to remote host.
static inline void log_msg(char *logstr, rtp_log_level_t log_level)
{
	if((log_levels & log_level) == log_level)
		write_logfile(logstr);						//printing log to file
	if(((remote_log_levels & log_level) == log_level)) {
											//sending log by network to remote host
		int retval = TEMP_FAILURE_RETRY(sendto(sockfd, (void *) logstr, strlen(logstr) + 1,
				MSG_DONTWAIT, (struct sockaddr *) &addr, sizeof(addr)));

		//if sending log failed
		if(retval == -1 && (log_levels & RTP_WARN) == log_level) {
			char dmsg[55];
			sprintf(dmsg, "Sending log by network failed: %s\n", strerror(errno));
			char *msg = create_log(RTP_WARN, "__rtp_print_log", dmsg);
			write_logfile(msg);
			if(msg != NULL)
				free(msg);
		} //if sending log succeed
		else if((log_levels & RTP_DEBUG) == log_level) {
			char dmsg[50];
			sprintf(dmsg, "Sending log by network with size: %d\n", retval);
			char *msg = create_log(RTP_DEBUG, "__rtp_print_log", dmsg);
			write_logfile(msg);
			if(msg != NULL)
				free(msg);
		}
	}
}

//Set O_NONBLOCK flag to file descriptor fd
static inline int set_noblock_fd(int fd)
{
	int flags = 0;
	if((flags = fcntl(fd, F_GETFL)) != -1) {
		if(fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1)
			return 0;
	}
	return -1;
}

static inline int handle_log(void)
{
		rtp_log_level_t log_level = RTP_OFF;
		unsigned int msg_len = 0;

		int retval = get_log_header(&msg_len, &log_level);
		int err = errno;
		if(retval == -1 && err == EAGAIN)
			return -1;

		char *msg = (char *) malloc(sizeof(char) * msg_len);
		if(get_log(msg, msg_len) != -1) {
			log_msg(msg, log_level);
		}
		if(msg != NULL)
			free(msg);
		return 0;
}

//clean-up handler of log thread.
static void on_cancel(void *data)
{
	log_thread_state = ENDING;
	set_noblock_fd(pipefd[PIPE_READ]);
	int ret_val = 0;
	do {
		ret_val = handle_log();
	} while(ret_val == 0);
}

//Handler of logging thread. It reads logs from pipe log them by function log_msg().
//parameter attr is not used. It returns nothing.
static void *rtp_log_thread(void *attr)
{
	pthread_cleanup_push(on_cancel, NULL)
	while(1) {
		handle_log();
	}
	pthread_cleanup_pop(1);
	return NULL;
}

//Creating and initializing logging thread.
static int rtp_log_thread_init(void)
{
	if(log_thread == NULL) {
		log_thread = (pthread_t *) malloc(sizeof(pthread_t));
		if(log_thread == NULL)
			goto ON_ERROR;
	} else
		goto ON_ERROR;

	if(pipe(pipefd) == -1)
		goto ON_ERROR;
	if(set_noblock_fd(pipefd[PIPE_WRITE]) == -1)
		goto ON_ERROR;

	log_thread_state = RUNNING;

	int res = -1;							//rozlisovat chybove cisla
	pthread_attr_t thread_attr;
	res = pthread_attr_init(&thread_attr);
	if(res != 0) goto ON_ERROR;
	res = pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_JOINABLE);
	if(res != 0) goto ON_ERROR;

	res = pthread_create(log_thread, &thread_attr, rtp_log_thread, NULL);
	if(res != 0) goto ON_ERROR;

	pthread_attr_destroy(&thread_attr);
	return 0;

	ON_ERROR:
	return -1;
}

int rtp_init_log(const char *flog_path, rtp_log_level_t levels, unsigned int max_fsize_quota, unsigned int rb_count)
{
	if(flog_path == NULL)
		return -1;
	if(strcmp(flog_path, "stdout") == 0)
		flog = stdout;
	else if(strcmp(flog_path, "stderr") == 0)
		flog = stderr;
	else {
		flog = fopen(flog_path, "a");
		if(flog == NULL)
			return -1;
	}

	if(rb_count <= 0)
		return -1;
	roll_backup_maxnum = rb_count - 1; //because of its numbered starting by zero.
	max_fsize = max_fsize_quota;

	flog_name = (char *) malloc(sizeof(char) * (strlen(flog_path) + 1));
	strcpy(flog_name, flog_path);

	if(get_flogsize_num(&cur_fsize, &roll_backup_num) == -1)
		return -1;

	if(remote_log_levels == RTP_OFF)
		rtp_log_thread_init();

	log_levels = levels;		//Must be last, to wait for all inicialization job is done.

	return 0;
}

int rtp_init_remote_log(const char *ip, uint16_t port, rtp_log_level_t levels)
{
	if(ip == NULL)
		return -1;
	sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if(sockfd == -1)
		return -1;

	in_addr_t in_addr;
	if((in_addr = inet_addr(ip)) == INADDR_NONE)
		return -1;

	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = in_addr;

	if(log_levels == RTP_OFF)
		rtp_log_thread_init();

	remote_log_levels = levels;
	return 0;
}

void __rtp_print_log(rtp_log_level_t log_level, char *func, char *message, ...)
{
	if(log_level == RTP_OFF && remote_log_levels == RTP_OFF)
		return;

	if(message == NULL) return;

	char *log_msg = NULL;

	//omiting logs that dont belong to enabled log level
	if(((remote_log_levels | log_levels) & log_level) != 0)
			log_msg = create_log(log_level, func, message);
	else
		return;

	va_list args;									//list of variables to print
	va_start(args, message);
	char logstr[strlen(log_msg) + 50];
	vsprintf(logstr, log_msg, args);				// adding variables into string
	va_end(args);

	put_log(logstr, log_level);

	if(log_msg != NULL) {
		free((void *) log_msg);
		log_msg = NULL;
	}
}

//Close pipe, cancel thread and wait for thread ending.
static inline void rtp_close_log_thread()
{
	void *foo = NULL;
	if(log_thread == NULL)
		return;

	pthread_cancel(*log_thread);
	pthread_join(*log_thread, &foo);

	free(log_thread);
	log_thread = NULL;

	close(pipefd[PIPE_WRITE]);
	close(pipefd[PIPE_READ]);
}

void rtp_close_remote_log(void)
{
	//must be first, because of turning off writing logs to the pipe
	if(remote_log_levels == RTP_OFF) return;
	remote_log_levels = RTP_OFF;

	if(log_levels == RTP_OFF)
		rtp_close_log_thread();

	if(sockfd != -1) {
		close(sockfd);
		sockfd = -1;
	}
}

void rtp_close_log(void)
{
	//must be first, because of turning off writing logs to the pipe
	if(log_levels == RTP_OFF) return;
	log_levels = RTP_OFF;

	if(remote_log_levels == RTP_OFF)
		rtp_close_log_thread();

	if(flog != NULL && flog != stdout && flog != stderr) {
		fclose(flog);
		flog = NULL;
	}
	if(flog_name != NULL) {
		free(flog_name);
		flog_name = NULL;
	}
}
