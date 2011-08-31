/*
 * rtp_manager.c
 *
 *  Created on: Aug 16, 2009
 *      Author: Matus Valo
 *      E-mail: matusvalo@gmail.com
 */

#include "rtp_store.h"
#include "rtp_stream_thread.h"
#include "log.h"

#define MAX_STREAMS 100

static struct rtp_stream *streams[MAX_STREAMS];

static inline int get_avail_streamid(void) {
    int i;
    for(i = 0; i < MAX_STREAMS; i++) {
        if(streams[i] == NULL) {
            rtp_print_log(RTP_DEBUG, "Stream ID=%d allocated\n", i);
            return i;
        }
    }
    rtp_print_log(RTP_ERROR, "No avaliable ID\n");
    return -1;
}

static inline void free_streamid(int id) {
    streams[id] = NULL;
}

int rtp_store_remote_loginit(const char *ip, uint16_t port, rtp_log_level_t levels)
{
    return rtp_init_remote_log(ip, port, levels);
}

int rtp_store_loginit(const char *flog, rtp_log_level_t log_levels, unsigned int max_fsize_quota, unsigned int rb_count)
{
    return rtp_init_log(flog, log_levels, max_fsize_quota, rb_count);
}

void rtp_store_remote_logclose(void)
{
    rtp_close_remote_log();
}

void rtp_store_logclose(void)
{
    rtp_close_log();
}

void rtp_store_init(void)
{
    int i;
    for(i = 0; i < MAX_STREAMS; i++)
        streams[i] = NULL;
    rtp_print_log(RTP_INFO, "RtpStore initialized.\n");
}

int rtp_store_create_stream(char *ip, uint16_t video_port, uint16_t audio_port,
               char *file_path)
{
    int id = get_avail_streamid();
    if(id == -1 || streams[id] != NULL) return -1;

    streams[id] = rtp_stream_init(ip, video_port, audio_port, file_path);
    if(streams[id] == NULL) return -1;

    if(rtp_stream_run(streams[id]) == -1) {
        free_streamid(id);                      //uvolnenie prostriedkov.
        return -1;
    }

    rtp_print_log(RTP_INFO, "Rtp stream with ID=%d created on IP=%s vport=%d aport=%d outputfile=%s\n",
                  id, ip, video_port, audio_port, file_path);
    return id;
}

int rtp_store_close_stream(int id)
{
    if(id == -1 || streams[id] == NULL) {
        rtp_print_log(RTP_ERROR, "Wrong parameter (ID == -1 or NULL)\n");
        return -1;
    }
    rtp_stream_close(streams[id]);  //uzavrie streams[id], ale hodnota smernika streams[id] ostava nezmenena!!
    free_streamid(id);              //nastavenie na NULL, aby sa znova mohol pouzit.
    rtp_print_log(RTP_INFO, "Closing rtp stream with ID=%d\n", id);
    return 0;
}

rtp_stream_state_t rtp_get_stream_state(int id)
{
    if(id == -1 || streams[id] == NULL) {
        rtp_print_log(RTP_ERROR, "Wrong parameter (ID == -1 or NULL)\n");
        return RTP_FAILED;
    }

    return rtp_get_stream_info(streams[id]).rtp_stream_state;
}

double rtp_get_stream_download_speed(int id)
{
    if(id == -1 || streams[id] == NULL) {
        rtp_print_log(RTP_ERROR, "Wrong parameter (ID == -1 or NULL)\n");
        return -1;
    }

    return rtp_get_stream_info(streams[id]).download_speed;
}

off64_t rtp_get_stream_dsize(int id)
{
    if(id == -1 || streams[id] == NULL) {
        rtp_print_log(RTP_ERROR, "Wrong parameter (ID == -1 or NULL)\n");
        return -1;
    }

    return rtp_get_stream_info(streams[id]).downloaded_data_size;
}

void rtp_store_close(void)
{
    int i;
    for(i = 0; i < MAX_STREAMS; i++) {
        if(streams[i] != NULL)
            rtp_store_close_stream(i);
    }
    rtp_print_log(RTP_INFO, "RtpStore is closed.\n");
    rtp_close_log();
}
