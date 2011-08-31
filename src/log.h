/*
 * log.h
 *
 *  Created on: Aug 29, 2009
 *      Author: Matus Valo
 *      E-mail: matusvalo@gmail.com
 */

#ifndef LOG_H_
#define LOG_H_

/**
 * Inits logging to file. rtp_init_log() or rtp_init_remote_log()
 * (or both) must be called first.
 * \param flog_path Path to logging file. Can be "stdin" for output to std. output and
 * "stderr" for std. error output.
 * \levels Levels of logging, that should be logged to file.
 * \returns 0 on success and -1 on error.
 */
int rtp_init_log(const char *flog_path, rtp_log_level_t levels, unsigned int max_fsize_quota, unsigned int rb_count);

/**
 * Inits logging to remote location. The log messages are sent by UDP protocol.
 * rtp_init_log() or rtp_init_remote_log() (or both) must be called first.
 * \param ip IP address of remote destination, where logs should be sent.
 * \param port Port of remote destination, where logs shoud be sent.
 * \levels Levels of logging, that should be logged to remote destination.
 * \returns 0 on success and -1 on error. //TODO:rozlisenie na zaklade chyby.
 */
int rtp_init_remote_log(const char *ip, uint16_t port, rtp_log_level_t levels);

/**
 * Prints log message. Takes same addtional parameters and text modifiers
 * (%d, %s ...) to print variables as printf().
 * \param rtp_log_level_t log_level Level of logging message.
 * \param char *message String of desired logging message.
 */
#ifdef __GNUC__
#define rtp_print_log(log_level, message, ...) __rtp_print_log(log_level, (char *)__FUNCTION__, \
                                                               message, ##__VA_ARGS__)
#endif

/**
 * Prints log message. This function shouldn't be used. Use makro rtp_print_log instead.
 * Takes same addtional parameters and text modifiers
 * (%d, %s ...) to print variables as printf().
 * \param log_level Level of message.
 * \param func Function, where was this function called.
 * \param message String of desired logging message.
 */
void __rtp_print_log(rtp_log_level_t log_level, char *func, char *message, ...);

/**
 * Closes logging to remote destination.
 */

void rtp_close_remote_log(void);

/**
 * Closes logging to file.
 */
void rtp_close_log(void);

#endif /* LOG_H_ */
