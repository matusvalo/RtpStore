/*
 * rtp_foutput.c
 *
 *  Created on: Aug 17, 2009
 *      Author: Matus Valo
 *      E-mail: matusvalo@gmail.com
 */
#include <sys/time.h>
#include <stdio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>                             //strerror()
#include <time.h>
#include <unistd.h>                             //fdatasync()
#include "rtp_foutput.h"
#include "rtp_store.h"
#include "log.h"

//Opens new file for writing RTP stream into.
static inline int open_file(struct rtp_stream *stream)
{
    FILE *output = fopen64(stream->file_name, "w");             //for support files larger then 2 GB
    rtp_print_log(RTP_DEBUG, "Opening output file for stream. File name : %s \n", stream->file_name);
    if (output == NULL) {                                       //for fopen64, must be compiled with -D_LARGEFILE64_SOURCE
        rtp_print_log(RTP_ERROR, "Opening file:%s, failed:%s\n", stream->file_name, strerror(errno));
        return -1;
    }

    stream->output_file = output;

    return 0;
}

//Closes output file given by struct stream.
static inline int close_file(struct rtp_stream *stream)
{
    if(stream->output_file != NULL) {
        int retval = fclose(stream->output_file);
        if(retval == 0)
            rtp_print_log(RTP_DEBUG, "Output file %s on stream closed\n", stream->file_name);
        else
            rtp_print_log(RTP_ERROR, "Closing file failed with errno %s", strerror(errno));
        return retval;
    }
    return 0;
}

int rtp_create_stream_output(struct rtp_stream *stream, char *file_path)
{
    if(file_path == NULL) {
        rtp_print_log(RTP_ERROR, "file_path=NULL\n");
        return -1;
    }

    stream->file_name = (char *) malloc(strlen(file_path) * sizeof(char) + 1);  //+ 1 because of \0
    if(stream->file_name == NULL) {
        rtp_print_log(RTP_ERROR, "Malloc failed\n");
        return -1;
    }

    strcpy(stream->file_name, file_path);

    rtp_print_log(RTP_DEBUG, "Stream output successfully initialized\n");

    return open_file(stream);
}

ssize_t rtp_write_packet(rtp_session_type_t stream_type, RD_buffer_t *packet, int len, struct rtp_stream *stream)
{
    stream_type == RTP_VIDEO ? fprintf(stream->output_file, "V") : fprintf(stream->output_file, "A");

    int items = fwrite((void *)packet, len, 1, stream->output_file) ; //???? WTF + 1 because 'A'/'V' was also written
    if(items < 1) {
        if(ferror(stream->output_file)) {
            rtp_print_log(RTP_WARN, "Writing packet to file failed.\n");
            clearerr(stream->output_file);
        }
    }

    return items * len;
}

int rtp_init_stream_output(struct rtp_stream *stream, char *addr, uint16_t port)
{
    RD_hdr_t hdr;
    struct timeval start;
    off64_t wlen = 0;

    gettimeofday(&start,0);
    wlen = fprintf(stream->output_file, "#!rtpplay%s %s/%d\n", RTPFILE_VERSION, addr, htons(port));

    hdr.start.tv_sec  = htonl(start.tv_sec);
    hdr.start.tv_usec = htonl(start.tv_usec);
    hdr.source = inet_addr(addr);
    hdr.port   = htons(port);

    wlen = fwrite((char *)&hdr, sizeof(hdr), 1, stream->output_file);
    if(wlen < (off64_t) 1)
        return -1;

    return 0;
}

int rtp_close_stream_output(struct rtp_stream *stream)
{
    int retval = close_file(stream);
    if(stream->file_name != NULL)
        free(stream->file_name);
    stream->output_file = NULL;
    return retval;
}
