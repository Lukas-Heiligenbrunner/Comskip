/*
 * mpeg2dec.c
 * Copyright (C) 2000-2003 Michel Lespinasse <walken@zoy.org>
 * Copyright (C) 1999-2000 Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *
 * This file is part of mpeg2dec, a free MPEG-2 video stream decoder.
 * See http://libmpeg2.sourceforge.net/ for updates.
 *
 * mpeg2dec is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * mpeg2dec is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "platform.h"
#include "comskip.h"
#include "comskip.h"
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/pixdesc.h>
#include <libavutil/samplefmt.h>
#include <expat_config.h>

#include "Settings.h"
#include "mpeg2dec.h"

#include <argtable2.h>

//#define SELFTEST
#define SHOW_VIDEO_TIMING

int pass = 0;
double test_pts = 0.0;

#ifdef HARDWARE_DECODE
#include <fftools/ffmpeg.h>
const HWAccel hwaccels[] = {
#if HAVE_VDPAU_X11
    { "vdpau", vdpau_init, HWACCEL_VDPAU, AV_PIX_FMT_VDPAU },
#endif
#if HAVE_DXVA2_LIB
    { "dxva2", dxva2_init, HWACCEL_DXVA2, AV_PIX_FMT_DXVA2_VLD },
#endif
#if CONFIG_VDA
    { "vda",   vda_init,   HWACCEL_VDA,   AV_PIX_FMT_VDA },
#endif
    { 0 },
};

static InputStream inputs;
static InputStream *ist = &inputs;
#endif


int av_log_level = AV_LOG_INFO;

typedef struct VideoPicture {
    int width, height; /* source height & width */
    int allocated;
    double pts;
} VideoPicture;

typedef struct VideoState {
    AVFormatContext *pFormatCtx;
    AVCodecContext *dec_ctx;
    int videoStream, audioStream, subtitleStream;

    int av_sync_type;
    int seek_req;
    int seek_by_bytes;
    int seek_no_flush;
    double seek_pts;
    int seek_flags;
    int64_t seek_pos;
    double audio_clock;
    AVStream *audio_st;
    AVStream *subtitle_st;
    AVPacket audio_pkt;
    AVPacket audio_pkt_temp;

    double video_clock; ///<pts of last decoded frame / predicted pts of next decoded frame
    double video_clock_submitted;
    AVStream *video_st;
    AVFrame *pFrame;
    char filename[1024];
    int quit;
    AVFrame *frame;
    double duration;
    double fps;
} VideoState;

VideoState *is;


AVDictionary *myoptions = NULL;


/* Since we only have one decoding thread, the Big Struct
   can be global in case we need it. */
VideoState *global_video_state;


int64_t pev_best_effort_timestamp = 0;

int stream_index;


int64_t best_effort_timestamp;


extern int audio_channels;

void InitComSkip(void);

void BuildCommListAsYouGo(void);

int video_packet_process(VideoState *is, AVPacket *packet);


static FILE *in_file;
static FILE *sample_file;
static FILE *timing_file = 0;

extern int lastFrameCommCalculated;

int is_h264 = 0;
int demux_pid = 0;
int pid;
int selected_video_pid = 0;
int selected_audio_pid = 0;
int selected_subtitle_pid = 0;

int64_t pts;
double initial_pts = 0.0;
int64_t final_pts;
double pts_offset = 0.0;

int initial_pts_set = 0;
double initial_apts;
double apts_offset = 0.0;
int initial_apts_set = 0;
int do_audio_repair = 1;
extern int timeline_repair;

// The following two functions are undocumented and not included in any public header,
// so we need to declare them ourselves
extern int _fseeki64(FILE *, int64_t, int);

extern int64_t _ftelli64(FILE *);

extern char inbasename[];

char pict_type;

char tempstring[512];
//test

#define DUMP_OPEN if (output_timing) { sprintf(tempstring, "%s.timing.csv", inbasename); timing_file = myfopen(tempstring, "w"); DUMP_HEADER }
#define DUMP_HEADER if (timing_file) fprintf(timing_file, "sep=,\ntype   ,real_pts, step        ,pts         ,clock       ,delta       ,offset, repeat\n");
#define DUMP_TIMING(T, D, P, C, O, S) if (timing_file && !csStepping && !csJumping && !csStartJump) fprintf(timing_file, "%7s, %12.3f, %12.3f, %12.3f, %12.3f, %12.3f, %12.3f, %d\n", \
    T, (double) (D), (double) calculated_delay, (double) (P), (double) (C), ((double) (P) - (double) (C)), (O), (S));
#define DUMP_CLOSE if (timing_file) { fclose(timing_file); timing_file = NULL; }


extern int skip_B_frames;
extern int lowres;

static int sigint = 0;

static int verbose = 0;

extern int selftest;
double selftest_target = 0.0;

extern int frame_count;
int framenum;


int64_t infopos, headerpos;

extern int max_repair_size;
int reviewing = 0;
int cur_hour = 0;
int cur_minute = 0;
int cur_second = 0;

extern char HomeDir[256];
extern int processCC;
int reorderCC = 0;

extern int live_tv;
int csRestart;
int csStartJump;
int csStepping;
int csJumping;

extern FILE *out_file;
extern uint8_t ccData[500];
extern int ccDataLen;

extern int height, width, videowidth;
extern int output_debugwindow;
extern int output_console;
extern int output_timing;
extern int output_srt;
extern int output_smi;

extern unsigned char *frame_ptr;
extern int live_tv_retries;
int retries;

//extern void set_fps(double frame_delay, double dfps, int ticks, double rfps, double afps);
extern void set_fps(double frame_delay);

extern void dump_video(char *start, char *end);

extern void dump_audio(char *start, char *end);

extern void Debug(int level, char *fmt, ...);

extern void dump_video_start(void);

extern void dump_audio_start(void);

void file_open();

int DetectCommercials(int, double);

int BuildMasterCommList(void);

FILE *LoadSettings(int argc, char **argv);

void ProcessCCData(void);

void dump_data(char *start, int length);

void close_data();

static void signal_handler(int sig) {
    sigint = 1;
    signal(sig, SIG_DFL);
    //return (RETSIGTYPE)0;
    return;
}


#define AUDIOBUFFER    1600000

static double base_apts = 0.0, apts, top_apts = 0.0;
static DECLARE_ALIGNED(16, short, audio_buffer[AUDIOBUFFER]);
static short *audio_buffer_ptr = audio_buffer;
static int audio_samples = 0;
#define ISSAME(T1, T2) (fabs((T1) - (T2)) < 0.001)

//extern double fps;
static int sound_frame_counter = 0;

extern double get_fps();

extern void set_frame_volume(uint32_t framenr, int volume);

extern double get_frame_pts(int f);


static int max_volume_found = 0;

int ms_audio_delay = 5;
int frames_without_sound = 0;
int frames_with_loud_sound = 0;


void list_codecs() {
    AVCodec *p;
    int i = 0;
    avcodec_register_all();
    p = av_codec_next(NULL);
    printf("Decoders:\n");
    printf("---------\n");
    while (p != NULL) {
        if (av_codec_is_decoder(p)) {
            printf("%s", p->name);
            i += strlen(p->name);
            if (i > 80) {
                printf("\n");
                i = 0;
            } else
                printf(", ");
        }
        p = av_codec_next(p);
    }
    printf("\n");
}


int retreive_frame_volume(double from_pts, double to_pts) {
    short *buffer;
    int volume = -1;
    VideoState *is = global_video_state;
    int i;
    double calculated_delay;
    int s_per_frame = (to_pts - from_pts) * (double) (is->audio_st->codec->sample_rate + 1);


    if (s_per_frame > 1 && base_apts <= from_pts && to_pts < top_apts) {
        calculated_delay = 0.0;


        //       Debug(1,"fame=%d, =base=%6.3f, from=%6.3f, samples=%d, to=%6.3f, top==%6.3f\n", -1, base_apts, from_pts, s_per_frame, to_pts, top_apts);
        buffer = &audio_buffer[(int) ((from_pts - base_apts) * ((double) is->audio_st->codec->sample_rate + 0.5))];

        volume = 0;
        if (sample_file) fprintf(sample_file, "Frame %i\n", sound_frame_counter);
        for (i = 0; i < s_per_frame; i++) {
            if (sample_file) fprintf(sample_file, "%i\n", *buffer);
            volume += (*buffer > 0 ? *buffer : -*buffer);
            buffer++;
        }
        volume = volume / s_per_frame;
        DUMP_TIMING("a  read", is->audio_clock, to_pts, from_pts, (double) volume, s_per_frame);

        audio_samples -= (int) ((from_pts - base_apts) * (is->audio_st->codec->sample_rate + 0.5)); // incomplete frame before complete frame
        audio_samples -= s_per_frame;


        if (volume == 0) {
            frames_without_sound++;
        } else if (volume > 20000) {
            if (volume > 256000)
                volume = 220000;
            frames_with_loud_sound++;
            volume = -1;
        } else {
            frames_without_sound = 0;
        }

        if (max_volume_found < volume)
            max_volume_found = volume;

        // Remove use samples
        audio_buffer_ptr = audio_buffer;
        if (audio_samples > 0) {
            for (i = 0; i < audio_samples; i++) {
                *audio_buffer_ptr++ = *buffer++;
            }
        }
        base_apts = to_pts;
        top_apts = base_apts + audio_samples / (double) (is->audio_st->codec->sample_rate);
        sound_frame_counter++;
    }
    return (volume);
}

void backfill_frame_volumes() {
    int f;
    int volume;
    double local_initial_pts = initial_pts;
    if (framenum < 3)
        return;
    f = framenum - 2;
    if (abs(local_initial_pts) > 200)
        local_initial_pts = 0;
    while (get_frame_pts(f) + local_initial_pts > base_apts && f > 1) // Find first frame with samples available, could be incomplete
        f--;
    while (f < framenum - 1 && (get_frame_pts(f + 1) + local_initial_pts) <= top_apts && (top_apts - base_apts) > .2 /* && get_frame_pts(f-1) >= base_apts */) {
        volume = retreive_frame_volume(fmax(get_frame_pts(f) + local_initial_pts, base_apts), get_frame_pts(f + 1) + local_initial_pts);
        if (volume > -1) set_frame_volume(f, volume);
        f++;
    }
}

int ALIGN_AC3_PACKETS = 0;


void sound_to_frames(VideoState *is, short **b, int s, int c, int format) {
    int i, l;
    int volume;
    static int old_c = 0;
    double old_base_apts;
    static double old_audio_clock = 0.0;
    double calculated_delay = 0.0;
    double avg_volume = 0.0;
    int planar = av_sample_fmt_is_planar(format);
    float *(fb[16]);
    short *(sb[16]);
    static int old_sample_rate = 0;

    audio_samples = (audio_buffer_ptr - audio_buffer);

    if (old_sample_rate == is->audio_st->codec->sample_rate &&
        ((audio_buffer_ptr - audio_buffer) < 0 || (audio_buffer_ptr - audio_buffer) >= AUDIOBUFFER
         || (top_apts - base_apts) * (is->audio_st->codec->sample_rate + 0.5) > AUDIOBUFFER
         || (top_apts < base_apts)
         || !ISSAME(((double) audio_samples / (double) (is->audio_st->codec->sample_rate + 0.5)) + base_apts, top_apts)
         || audio_samples < 0
         || audio_samples >= AUDIOBUFFER)) {
        Debug(1, "Panic: Audio buffering corrupt\n");
        audio_buffer_ptr = audio_buffer;
        top_apts = base_apts = 0;
        audio_samples = 0;
        return;
    }

    if (old_c != 0 && old_c != c) {
        Debug(5, "Audio channels switched at pts=%6.5f from %d to %d\n", base_apts, old_c, c);
//        InsertBlackFrame()
    }
    audio_channels = c;
    old_c = c;
    if (old_sample_rate != 0 && old_sample_rate != is->audio_st->codec->sample_rate) {
        Debug(5, "Audio samplerate switched from %d to %d\n", old_sample_rate, is->audio_st->codec->sample_rate);
    }
    old_sample_rate = is->audio_st->codec->sample_rate;

    old_base_apts = base_apts;
    if (fabs(base_apts - (is->audio_clock - ((double) audio_samples / (double) (is->audio_st->codec->sample_rate)))) > 0.0001)
        base_apts = (is->audio_clock - ((double) audio_samples / (double) (is->audio_st->codec->sample_rate)));
    if (ALIGN_AC3_PACKETS && is->audio_st->codec->codec_id == AV_CODEC_ID_AC3) {
        if (ISSAME(base_apts - old_base_apts, 0.032)
            || ISSAME(base_apts - old_base_apts, -0.032)
            || ISSAME(base_apts - old_base_apts, 0.064)
            || ISSAME(base_apts - old_base_apts, -0.064)
            || ISSAME(base_apts - old_base_apts, -0.096)
                )
            old_base_apts = base_apts; // Ignore AC3 packet jitter
    }
    if (old_base_apts != 0.0 && (fabs(base_apts - old_base_apts) > 0.01)) {
        Debug(8, "Jump in base apts from %6.5f to %6.5f, delta=%6.5f\n", old_base_apts, base_apts, base_apts - old_base_apts);
    }

    if (s + audio_samples > AUDIOBUFFER) {
        Debug(1, "Panic: Audio buffer overflow, resetting audio buffer\n");
        audio_buffer_ptr = audio_buffer;
        top_apts = base_apts = 0;
        audio_samples = 0;
        return;
    }

    if (s > 0) {
        if (format == AV_SAMPLE_FMT_FLTP) {
            for (l = 0; l < is->audio_st->codec->channels; l++) {
                fb[l] = (float *) b[l];
            }
            for (i = 0; i < s; i++) {
                volume = 0;
                if (planar)
                    for (l = 0; l < is->audio_st->codec->channels; l++) volume += *((fb[l])++) * 64000;
                else
                    for (l = 0; l < is->audio_st->codec->channels; l++) volume += *((fb[0])++) * 64000;
                *audio_buffer_ptr++ = volume / is->audio_st->codec->channels;
                avg_volume += abs(volume / is->audio_st->codec->channels);
            }
        } else {
            for (l = 0; l < is->audio_st->codec->channels; l++) {
                sb[l] = (short *) b[l];
            }
            for (i = 0; i < s; i++) {
                volume = 0;
                if (planar)
                    for (l = 0; l < is->audio_st->codec->channels; l++) volume += *((sb[l])++);
                else
                    for (l = 0; l < is->audio_st->codec->channels; l++) volume += *((sb[0])++);
                *audio_buffer_ptr++ = volume / is->audio_st->codec->channels;
                avg_volume += abs(volume / is->audio_st->codec->channels);
            }
        }
    }
    avg_volume /= s;
    audio_samples = (audio_buffer_ptr - audio_buffer);
    top_apts = base_apts + audio_samples / (double) (is->audio_st->codec->sample_rate);

    calculated_delay = is->audio_clock - old_audio_clock;
    DUMP_TIMING("a frame", is->audio_clock, top_apts, base_apts, avg_volume, s);
    old_audio_clock = is->audio_clock;

    backfill_frame_volumes();
}


#define AC3_BUFFER_SIZE 100000
static uint8_t ac3_packet[AC3_BUFFER_SIZE];
static int ac3_packet_index = 0;

int ac3_package_misalignment_count = 0;

void audio_packet_process(VideoState *is, AVPacket *pkt) {
    int prev_codec_id = -1;
    int len1, data_size;
    uint8_t *pp;
    double prev_audio_clock;
//    AC3DecodeContext *s = is->audio_st->codec->priv_data;
    int rps, ps;
    AVPacket *pkt_temp = &is->audio_pkt_temp;

    int got_frame;
    if (!reviewing) {
        dump_audio_start();
        dump_audio((char *) pkt->data, (char *) (pkt->data + pkt->size));
    }


    pkt_temp->data = pkt->data;
    pkt_temp->size = pkt->size;

    if (!ALIGN_AC3_PACKETS && is->audio_st->codec->codec_id == AV_CODEC_ID_AC3
        && ((pkt_temp->data[0] != 0x0b || pkt_temp->data[1] != 0x77))) {
//        Debug(1, "AC3 packet misaligned, audio decoding will fail\n");
        ac3_package_misalignment_count++;
    } else {
        ac3_package_misalignment_count = 0;
    }
    if (!ALIGN_AC3_PACKETS && ac3_package_misalignment_count > 4) {
        Debug(8, "AC3 packets misaligned, enabling AC3 re-alignment\n");
        ALIGN_AC3_PACKETS = 1;
    }

    if (ALIGN_AC3_PACKETS && is->audio_st->codec->codec_id == AV_CODEC_ID_AC3) {
        if (ac3_packet_index + pkt_temp->size >= AC3_BUFFER_SIZE) {
            Debug(8, "AC3 sync error\n");
            return;
        }
        memcpy(&ac3_packet[ac3_packet_index], pkt_temp->data, pkt_temp->size);
        pkt_temp->data = ac3_packet;
        pkt_temp->size += ac3_packet_index;
        ac3_packet_index = pkt_temp->size;
        ps = 0;
        while (pkt_temp->size >= 2 && (pkt_temp->data[0] != 0x0b || pkt_temp->data[1] != 0x77)) {
            pkt_temp->data++;
            pkt_temp->size--;
            ps++;
        }
        if (pkt_temp->size < 2)
            return; // No packet start found
        if (ps > 0)
            Debug(8, "Skipped %d of added %d bytes in audio input stream around frame %d\n", ps, pkt->size, framenum);
        pp = pkt_temp->data;
        rps = pkt_temp->size - 2;
        while (rps > 1 && (pp[rps] != 0x0b || pp[rps + 1] != 0x77)) {
            rps--;
        }
        if (rps >= 2) {
            pkt_temp->size = rps;
        } else {
            // No complete packet found;
            rps = pkt_temp->size;
            pp = &pkt_temp->data[0];
            pkt_temp->size = 0;
            return;
        }
        if ((pkt_temp->size % 768) != 0)
            Debug(8, "Strange packet size of %d bytes in audio input stream around frame %d\n", rps, framenum);

    }

    /*  Try to align on packet boundary as some demuxers don't do that, in particular dvr-ms */




    if (pkt->pts != AV_NOPTS_VALUE) {
        prev_audio_clock = is->audio_clock;
        is->audio_clock = av_q2d(is->audio_st->time_base) * (pkt->pts - (is->audio_st->start_time != AV_NOPTS_VALUE ? is->audio_st->start_time : 0)) - apts_offset;
        if (ALIGN_AC3_PACKETS && is->audio_st->codec->codec_id == AV_CODEC_ID_AC3) {
            if (ISSAME(is->audio_clock - prev_audio_clock, 0.032)
                || ISSAME(is->audio_clock - prev_audio_clock, -0.032)
                || ISSAME(is->audio_clock - prev_audio_clock, 0.064)
                || ISSAME(is->audio_clock - prev_audio_clock, -0.064)
                || ISSAME(is->audio_clock - prev_audio_clock, -0.096)
                    )
                prev_audio_clock = is->audio_clock; // Ignore AC3 packet jitter
        }

        if (initial_apts_set && is->audio_clock != 0.0 && fabs(is->audio_clock - prev_audio_clock) > 0.02) {
            if (do_audio_repair && fabs(is->audio_clock - prev_audio_clock) < 1) {
                is->audio_clock = prev_audio_clock; //Ignore small jitter
            } else {
                Debug(8, "Strange audio pts step of %6.5f instead of %6.5f at frame %d\n", (is->audio_clock - prev_audio_clock) + 0.0005, 0.0, framenum);
                if (do_audio_repair) {
//                    apts_offset += is->audio_clock - prev_audio_clock ;
//                    is->audio_clock = prev_audio_clock;
                }
            }
        }
        if (!initial_apts_set) {
            initial_apts = is->audio_clock;
            Debug(10, "\nInitial audio pts = %10.3f\n", initial_apts);

        }
    }

    initial_apts_set = 1;

    //		fprintf(stderr, "sac = %f\n", is->audio_clock);
    while (pkt_temp->size > 0) {
        //       data_size = STORAGE_SIZE;

//        if (!is->frame)
//        {
        if (!(is->frame = av_frame_alloc()))
            return;
        //       }
        //       else
        //           avcodec_get_frame_defaults(is->frame);

        len1 = avcodec_decode_audio4(is->audio_st->codec, is->frame, &got_frame, pkt_temp);

        if (prev_codec_id != -1 && (unsigned int) prev_codec_id != is->audio_st->codec->codec_id) {
            Debug(2, "Audio format change\n");
        }
        prev_codec_id = is->audio_st->codec->codec_id;
        if (len1 < 0 && !ALIGN_AC3_PACKETS) {
            /* if error, we skip the frame */
            pkt_temp->size = 0;
            if (is->audio_st->codec->codec_id == AV_CODEC_ID_AC3) ac3_packet_index = 0;

            break;
        }
        if (len1 < 0 && ALIGN_AC3_PACKETS) {
            len1 = 2; // Skip over packet start
            pkt_temp->data += len1;
            pkt_temp->size -= len1;
            break;
        }
        pkt_temp->data += len1;
        pkt_temp->size -= len1;
        if (!got_frame) {
            /* stop sending empty packets if the decoder is finished */
            continue;
        }


        data_size = av_samples_get_buffer_size(NULL, is->frame->channels,
                                               is->frame->nb_samples,
                                               is->frame->format, 1);
        if (data_size > 0) {
            sound_to_frames(is, (short **) is->frame->data, is->frame->nb_samples, is->frame->channels, is->frame->format);
        }
        is->audio_clock += (double) data_size /
                           (is->frame->channels * is->frame->sample_rate * av_get_bytes_per_sample(is->frame->format));
        av_frame_free(&(is->frame));
    }

    if (ALIGN_AC3_PACKETS && is->audio_st->codec->codec_id == AV_CODEC_ID_AC3) {
        ps = 0;
        rps = (pkt_temp->data - ac3_packet);
        while (0 < ac3_packet_index - rps) {
            ac3_packet[ps] = ac3_packet[rps];
            ps++;
            rps++;
        }
        ac3_packet_index = ps;
    }
}

static double print_fps(int final) {
    static uint32_t frame_counter = 0;
    static struct timeval tv_beg, tv_start;
    static int total_elapsed;
    static int last_count = 0;
    struct timeval tv_end;
    double fps, tfps;
    int frames, elapsed;
    char cur_pos[100] = "0:00:00";

    if (verbose)
        return 0.0;

    if (csStepping)
        return 0.0;

    if (final < 0) {
        frame_counter = 0;
        last_count = 0;
        return 0.0;
    }

    gettimeofday(&tv_end, NULL);

    if (!frame_counter) {
        tv_start = tv_beg = tv_end;
        signal(SIGINT, signal_handler);
    }

    elapsed = (tv_end.tv_sec - tv_beg.tv_sec) * 100 + (tv_end.tv_usec - tv_beg.tv_usec) / 10000;
    total_elapsed = (tv_end.tv_sec - tv_start.tv_sec) * 100 + (tv_end.tv_usec - tv_start.tv_usec) / 10000;

    if (final) {
        if (total_elapsed)
            tfps = frame_counter * 100.0 / total_elapsed;
        else
            tfps = 0;

        fprintf(stderr, "\n%d frames decoded in %.2f seconds (%.2f fps)\n",
                frame_counter, total_elapsed / 100.0, tfps);
        fflush(stderr);
        return tfps;
    }

    frame_counter++;

    frames = frame_counter - last_count;

    if (elapsed < 100)    /* only display every 1.00 seconds */
        return 0.0;

    tv_beg = tv_end;

//    cur_second = (int)(get_frame_pts(framenum));
    cur_second = (int) ((framenum) / get_fps());
    cur_hour = cur_second / (60 * 60);
    cur_second -= cur_hour * 60 * 60;
    cur_minute = cur_second / 60;
    cur_second -= cur_minute * 60;


    sprintf(cur_pos, "%2i:%.2i:%.2i", cur_hour, cur_minute, cur_second);

    fps = frames * 100.0 / elapsed;
    tfps = frame_counter * 100.0 / total_elapsed;

    fprintf(stderr, "%s - %d frames in %.2f sec(%.2f fps), "
                    "%.2f sec(%.2f fps), %d%%\r", cur_pos, frame_counter,
//             total_elapsed / 100.0, tfps, elapsed / 100.0, fps, (int) (100.0 * get_frame_pts(framenum) / global_video_state->duration));
            total_elapsed / 100.0, tfps, elapsed / 100.0, fps, (int) (100.0 * (framenum) / get_fps() / global_video_state->duration));
    fflush(stderr);
    last_count = frame_counter;
    return tfps;
}

#ifdef PROCESS_CC
void CEW_reinit();
long process_block (unsigned char *data, long length);
#endif


int SubmitFrame(AVStream *video_st, AVFrame *pFrame, double pts) {
    int res = 0;
    int changed = 0;

//	bitrate = pFrame->bit_rate;
    if (pFrame->linesize[0] > MAXWIDTH || pFrame->height > MAXHEIGHT || pFrame->linesize[0] < 100 || pFrame->height < 100) {
        //				printf("Panic: illegal height, width or frame period\n");
        frame_ptr = NULL;
        return (0);
    }
    if (height != pFrame->height && pFrame->height > 100 && pFrame->height < MAXHEIGHT) {
        height = pFrame->height;
        changed = 1;
    }
    if (width != pFrame->linesize[0] && pFrame->linesize[0] > 100 && pFrame->linesize[0] < MAXWIDTH) {
        width = pFrame->linesize[0];
        changed = 1;
    }
    if (videowidth != pFrame->width && pFrame->width > 100 && pFrame->width < MAXWIDTH) {
        videowidth = pFrame->width;
        changed = 1;
    }
    if (changed) Debug(5, "Format changed to [%d : %d]\n", videowidth, height);
    infopos = headerpos;
    frame_ptr = pFrame->data[0];
    if (frame_ptr == NULL) {
        return (0);; // return; // exit(2);
    }

    if (pFrame->pict_type == AV_PICTURE_TYPE_B)
        pict_type = 'B';
    else if (pFrame->pict_type == AV_PICTURE_TYPE_I)
        pict_type = 'I';
    else
        pict_type = 'P';

    if (selftest == 2 && framenum == 0 && pass == 0 && test_pts == 0.0) //Reset file test
        test_pts = pts;
    if (selftest == 2 && pass > 0) //Reset file test
    {
        if (test_pts != pts) {
            sample_file = fopen("seektest.log", "a+");
            fprintf(sample_file, "Reset file Failed, initial pts = %6.3f, seek pts = %6.3f, pass = %d, \"%s\"\n", test_pts, pts, pass + 1, is->filename);
            fclose(sample_file);
            Debug(1, "\nSelftest %d FAILED: Reset\n", selftest);
        } else
            Debug(1, "\nSelftest 2 OK: Reset\n");

        exit(1);
    }

    if (!reviewing) {

        print_fps(0);
        res = DetectCommercials((int) framenum, pts);
        framenum++;
#ifdef SELFTEST
        if (selftest == 2 && pass == 0 && framenum > 20) //Reset input file
        {
            res = true;
            pass++;
        }
#endif
        if (res) {
            framenum = 0;
            sound_frame_counter = 0;
            is->seek_req = 1;
            is->seek_pos = 0;
            is->seek_pts = 0.0;
        }
    }
    return (res);
}

void Set_seek(VideoState *is, double pts) {
    AVFormatContext *ic = is->pFormatCtx;

    double length = is->duration;

    is->seek_flags = AVSEEK_FLAG_ANY;
    is->seek_flags = AVSEEK_FLAG_BACKWARD;
    is->seek_req = true;
    is->seek_pts = pts;
#ifdef DEBUG
    printf("Seek to %8.2f\n", pts);
#endif // DEBUG

#define MAX_GOP_SIZE 2.0
    pts = fmax(0.0, pts - MAX_GOP_SIZE);

    if (is->seek_by_bytes) {
//                            pos = avio_tell(is->pFormatCtx->pb);
        uint64_t size = avio_size(ic->pb);
        if (length < 0) {
            is->seek_pos = size * fmax(0, pts - 4.0) / (frame_count * get_fps());
//            Debug(0,"Impossible to reposition this file, aborting\n");
            //          exit(-1);
        } else {
            is->seek_pos = size * fmax(0, pts - 4.0) / length;
        }
        is->seek_flags |= AVSEEK_FLAG_BYTE;
    } else {
        pts = fmax(0, pts + initial_pts);
        is->seek_pos = pts / av_q2d(is->video_st->time_base);
        if (is->video_st->start_time != AV_NOPTS_VALUE) {
            is->seek_pos += is->video_st->start_time;
        }
    }
}

void DoSeekRequest(VideoState *is) {
    int ret;
    again:
//           ret = avformat_seek_file(is->pFormatCtx, is->videoStream, INT64_MIN, is->seek_pos, INT64_MAX, is->seek_flags);
    ret = av_seek_frame(is->pFormatCtx, is->videoStream, is->seek_pos, is->seek_flags);
//            ret = av_seek_frame(is->pFormatCtx, -1,  is->seek_pos,  is->seek_flags);
    pev_best_effort_timestamp = 0;
    best_effort_timestamp = 0;
    is->video_clock = 0.0;
    is->audio_clock = 0.0;
    if (ret < 0) {
        char *error_text;
        if (is->pFormatCtx->iformat->read_seek) {
            error_text = "Format specific";
        } else if (is->pFormatCtx->iformat->read_timestamp) {
            error_text = "Frame binary";
        } else {
            error_text = "Generic";
        }

        fprintf(stderr, "%s error while seeking. target=%6.3f, \"%s\"\n", error_text, is->seek_pts, is->pFormatCtx->filename);

        if (selftest) {
            sample_file = fopen("seektest.log", "a+");
            fprintf(sample_file, "%s error while seeking, target=%6.3f, \"%s\"\n", error_text, is->seek_pts, is->pFormatCtx->filename);
            fclose(sample_file);
        }

        if (!is->seek_by_bytes) {
            is->seek_by_bytes = 1; // Fall back to byte seek
            Set_seek(is, is->seek_pts);
            goto again;
        }
    }
    if (!is->seek_no_flush) {
        if (is->audioStream >= 0) {
            avcodec_flush_buffers(is->audio_st->codec);
        }
        if (is->videoStream >= 0) {
            avcodec_flush_buffers(is->video_st->codec);
        }
    }
    is->seek_no_flush = 0;
}

extern char mpegfilename[];


int video_packet_process(VideoState *is, AVPacket *packet) {
    double frame_delay;
    int len1, frameFinished;
    int repeat;
    double pts;
    double real_pts;
    static int find_29fps = 0;
    static int force_29fps = 0;
    static int find_25fps = 0;
    static int force_25fps = 0;
    static int find_24fps = 0;
    static int force_24fps = 0;
    static double prev_pts = 0.0;
    static double prev_real_pts = 0.0;
    static double prev_strange_step = 0.0;
    static int prev_strange_framenum = 0;

    double calculated_delay;

    if (!reviewing) {
        dump_video_start();
        dump_video((char *) packet->data, (char *) (packet->data + packet->size));
    }
    real_pts = 0.0;
    pts = 0;
    //is->video_st->codec.thread_type
    if (!Settings.hardware_decode) is->video_st->codec->flags |= AV_CODEC_FLAG_GRAY;
    // Decode video frame
    len1 = avcodec_decode_video2(is->video_st->codec, is->pFrame, &frameFinished,
                                 packet);

    if (len1 < 0) {
        printf("Warning: frame size is changing \n");
    }

    // Did we get a video frame?
    if (frameFinished) {

        if (is->video_st->codec->framerate.den && is->video_st->codec->framerate.num) {
            frame_delay = (1 / av_q2d(is->video_st->codec->framerate)) /* * is->video_st->codec->ticks_per_frame */ ;
        } else {
            frame_delay = av_q2d(is->video_st->codec->time_base) * is->video_st->codec->ticks_per_frame;
        }

//        frame_delay = av_q2d(is->video_st->codec->time_base) * is->video_st->codec->ticks_per_frame ;
        repeat = av_stream_get_parser(is->video_st) ? av_stream_get_parser(is->video_st)->repeat_pict : 4;

        //       if (prev_frame_delay != 0.0 && frame_delay != prev_frame_delay)
        //           Debug(1, "Changing fps from %6.3f to %6.3f", 1.0/prev_frame_delay, 1.0/frame_delay);
        pev_best_effort_timestamp = best_effort_timestamp;
        if (Settings.use_cuvid)
            av_frame_set_best_effort_timestamp(is->pFrame, is->pFrame->pkt_pts);
        best_effort_timestamp = av_frame_get_best_effort_timestamp(is->pFrame);
        calculated_delay = (best_effort_timestamp - pev_best_effort_timestamp) * av_q2d(is->video_st->time_base);

        if (best_effort_timestamp == AV_NOPTS_VALUE)
            real_pts = 0;
        else {
            headerpos = avio_tell(is->pFormatCtx->pb);
            if ((initial_pts_set < 3 && !reviewing) || (reviewing && initial_pts_set < 2)) {
//              if (!ISSAME(initial_pts, av_q2d(is->video_st->time_base)* (best_effort_timestamp - (frame_delay * framenum) / av_q2d(is->video_st->time_base) - (is->video_st->start_time != AV_NOPTS_VALUE ? is->video_st->start_time : 0)))) {
                if (!ISSAME(initial_pts, (best_effort_timestamp - (is->video_st->start_time != AV_NOPTS_VALUE ? is->video_st->start_time : 0)) * av_q2d(is->video_st->time_base) - (frame_delay * framenum))) {
                    initial_pts = (best_effort_timestamp - (is->video_st->start_time != AV_NOPTS_VALUE ? is->video_st->start_time : 0)) * av_q2d(is->video_st->time_base) - (frame_delay * framenum);
                    Debug(10, "\nInitial video pts = %10.3f\n", initial_pts);
//                    if (timeline_repair<2)
//                        initial_pts = 0.0;
                }
                initial_pts_set++;
                final_pts = 0;
                pts_offset = 0.0;

            }
            real_pts = av_q2d(is->video_st->time_base) * (best_effort_timestamp - (is->video_st->start_time != AV_NOPTS_VALUE ? is->video_st->start_time : 0)) - initial_pts;
            final_pts = best_effort_timestamp - (is->video_st->start_time != AV_NOPTS_VALUE ? is->video_st->start_time : 0);
        }

//        dts =  av_q2d(is->video_st->time_base)* ( is->pFrame->pkt_dts - (is->video_st->start_time != AV_NOPTS_VALUE ? is->video_st->start_time : 0)) ;


        calculated_delay = real_pts - prev_real_pts;

        if (framenum < 500) {
            if ((!(fabs(frame_delay - 0.03336666) < 0.001)) &&
                ((fabs(calculated_delay - 0.0333333) < 0.0001) || (fabs(calculated_delay - 0.033) < 0.0001) || (fabs(calculated_delay - 0.034) < 0.0001) ||
                 (fabs(calculated_delay - 0.067) < 0.0001) || (fabs(calculated_delay - 0.066) < 0.0001) || (fabs(calculated_delay - 0.06673332) < 0.0001))) {
                find_29fps++;
            } else
                find_29fps = 0;
            if (force_29fps == 0 && find_29fps == 5) {
                force_29fps = 1;
                force_25fps = 0;
                force_24fps = 0;
            }

            if ((!(fabs(frame_delay - 0.040) < 0.001)) &&
                (((fabs(calculated_delay - 0.04) < 0.0001)) || (fabs(calculated_delay - 0.039) < 0.0001) || (fabs(calculated_delay - 0.041) < 0.0001))) {
                find_25fps++;
            } else
                find_25fps = 0;
            if (force_25fps == 0 && find_25fps == 5) {
                force_29fps = 0;
                force_25fps = 1;
                force_24fps = 0;
            }

            if (((find_24fps & 1) == 0 && (fabs(calculated_delay - 0.050) < 0.001)) ||
                ((find_24fps & 1) == 1 && (fabs(calculated_delay - 0.033) < 0.001))
                    ) {
                find_24fps++;
            } else
                find_24fps = 0;
            if (force_24fps == 0 && find_24fps == 5) {
                force_29fps = 0;
                force_25fps = 0;
                force_24fps = 1;
            }
        }

        if (force_29fps && find_29fps == 5) {
            frame_delay = 0.033366666666666669;
            Debug(1, "Framerate forced %6.3f fps at frame %d\n", 1.0 / frame_delay, frame_count);
            set_fps(frame_delay);
        }
        if (force_25fps && find_25fps == 5) {
            frame_delay = 0.04;
            Debug(1, "Framerate forced %6.3f fps at frame %d\n", 1.0 / frame_delay, frame_count);
            set_fps(frame_delay);
        }
        if (force_24fps && find_24fps == 5) {
            frame_delay = 0.0416666666666667;
            Debug(1, "Framerate forced %6.3f fps at frame %d\n", 1.0 / frame_delay, frame_count);
            set_fps(frame_delay);
        }

//#define SHOW_VIDEO_TIMING
#ifdef SHOW_VIDEO_TIMING
        if (framenum==0)
            Debug(1,"Video timing ---------------------------------------------------\n", frame_delay/is->video_st->codec->ticks_per_frame, is->video_st->codec->ticks_per_frame, repeat, real_pts,calculated_delay);
        else if (framenum<20)
            Debug(1,"Video timing fr=%6.5f, tick=%d, repeat=%d, pts=%6.3f, step=%6.5f\n", frame_delay/is->video_st->codec->ticks_per_frame, is->video_st->codec->ticks_per_frame, repeat, real_pts,calculated_delay);
#endif // SHOW_VIDEO_TIMING


        pts_offset *= 0.9;
        if (!reviewing && timeline_repair) {
            if (framenum > 1 && fabs(calculated_delay - pts_offset - frame_delay) < 1.0) { // Allow max 0.5 second timeline jitter to be compensated
                if (!ISSAME(3 * frame_delay / is->video_st->codec->ticks_per_frame, calculated_delay))
                    if (!ISSAME(1 * frame_delay / is->video_st->codec->ticks_per_frame, calculated_delay))
                        pts_offset = pts_offset + frame_delay - calculated_delay;
            }
        } else
            do_audio_repair = 0;

//		Debug(0 ,"pst[%3d] = %12.3f, inter = %d, ticks = %d\n", framenum, pts/frame_delay, is->pFrame->interlaced_frame, is->video_st->codec->ticks_per_frame);

        pts = real_pts + pts_offset;

        calculated_delay = pts - prev_pts;

        if (!reviewing
            && framenum > 1 && fabs(calculated_delay - frame_delay) > 0.01
            && !ISSAME(3 * frame_delay / is->video_st->codec->ticks_per_frame, calculated_delay)
            && !ISSAME(2 * frame_delay / is->video_st->codec->ticks_per_frame, calculated_delay)
            && !ISSAME(1 * frame_delay / is->video_st->codec->ticks_per_frame, calculated_delay)
                ) {
            if ((prev_strange_framenum + 1 != framenum) && (prev_strange_step < fabs(calculated_delay - frame_delay))) {
                Debug(8, "Strange video pts step of %6.5f instead of %6.5f at frame %d\n", calculated_delay + 0.0000005, frame_delay + 0.0000005, framenum); // Unknown strange step
                if (calculated_delay < -0.5)
                    do_audio_repair = 0;        // Disable audio repair with messed up video timeline
            }
            prev_strange_framenum = framenum;
            prev_strange_step = fabs(calculated_delay - frame_delay);
        }

        // set_fps(calculated_delay, is->fps, repeat, av_q2d(is->video_st->r_frame_rate),  av_q2d(is->video_st->avg_frame_rate));

        if (pts != 0) {
            is->video_clock = pts;
            DUMP_TIMING("v   set", real_pts, pts, is->video_clock, pts_offset, repeat);
        } else {
            /* if we aren't given a pts, set it to the clock */
            DUMP_TIMING("v clock", real_pts, pts, is->video_clock, pts_offset, repeat);
            pts = is->video_clock;
        }
        is->video_clock_submitted = is->video_clock;

#ifdef HARDWARE_DECODE
        if (ist->hwaccel_retrieve_data && is->pFrame->format == ist->hwaccel_pix_fmt) {
            if (ist->hwaccel_retrieve_data(ist->dec_ctx, is->pFrame) < 0)
                goto quit;
        }
        ist->hwaccel_retrieved_pix_fmt = is->pFrame->format;
#endif


        if (retries == 0) {
            if (is->video_clock - is->seek_pts > -frame_delay / 2.0) {
                if (SubmitFrame(is->video_st, is->pFrame, is->video_clock)) {
                    goto quit;
                }
            }
        } else {
            if (is->video_clock - is->seek_pts > -frame_delay / 2.0) {
                if (selftest == 3) //Reopen at same location
                {
                    if (is->video_clock < selftest_target - 0.05 || is->video_clock > selftest_target + 0.05) {
                        sample_file = fopen("seektest.log", "a+");
                        fprintf(sample_file, "Reopen error: target=%8.1f, result=%8.1f, error=%6.3f, size=%8.1f, mode=%s, \"%s\"\n",
                                is->seek_pts,
                                is->video_clock,
                                is->video_clock - is->seek_pts,
                                is->duration,
                                (is->seek_by_bytes ? "byteseek" : "timeseek"),
                                is->filename);
                        fclose(sample_file);
                        Debug(1, "\nSelftest 3 FAILED: Reopen\n");
                    } else
                        Debug(1, "\nSelftest 3 OK: Reopen\n");
                    exit(1);
                }
                retries = 0;
                if (SubmitFrame(is->video_st, is->pFrame, is->video_clock)) {
                    goto quit;
                }
            } else {
                if (fabs(is->seek_pts - is->video_clock) > 80) {
                    Debug(1, "Positioning file failing with pts=%6.2f\n", is->video_clock);
                    if (selftest == 1 || selftest == 3) {
                        sample_file = fopen("seektest.log", "a+");
                        fprintf(sample_file, "Seek error : target=%8.1f, result=%8.1f, error=%6.3f, size=%8.1f, mode=%s, \"%s\"\n",
                                is->seek_pts,
                                is->video_clock,
                                is->video_clock - is->seek_pts,
                                is->duration,
                                (is->seek_by_bytes ? "byteseek" : "timeseek"),
                                is->filename);
                        fclose(sample_file);
                        Debug(1, "\nSelftest %d FAILED\n", selftest);
                        exit(1);
                    }
                    goto quit;          //Temporary till the seek error is fixed.
                    retries = 0;
                    exit(-1);
                }
            }
//            if (selftest == 4) exit(1);
        }
        /* update the video clock */
        is->video_clock += frame_delay;
        prev_pts = pts;
        prev_real_pts = real_pts;
//        prev_frame_delay = frame_delay;

#ifdef PROCESS_CC
        if (is->pFrame->nb_side_data) {
            int i;
            for (i = 0; i < is->pFrame->nb_side_data; i++) {
                AVFrameSideData *sd = is->pFrame->side_data[i];
                if (sd->type != AV_FRAME_DATA_A53_CC) continue;
                ccDataLen = sd->size + 7;
                ccData[0] = 'G';
                ccData[1] = 'A';
                ccData[2] = '9';
                ccData[3] = '4';
                ccData[4] = 3;
                ccData[5] = sd->size / 3 + 64;
                for (i=0; i<sd->size; i++) {
                  ccData[i+7] = sd->data[i];
                }
                dump_data((char *)ccData, (int)ccDataLen);
                if (processCC) ProcessCCData();
                if (output_srt) process_block(ccData, (int)ccDataLen);
            }
        }
#endif

        return 1;
    }
    quit:
    return 0;
}


//extern int dxva2_init(AVCodecContext *s);

#ifdef HARDWARE_DECODE
static const HWAccel *get_hwaccel(enum AVPixelFormat pix_fmt)
{
    int i;
    for (i = 0; hwaccels[i].name; i++)
        if (hwaccels[i].pix_fmt == pix_fmt)
            return &hwaccels[i];
    return NULL;
}

static enum AVPixelFormat get_format(AVCodecContext *s, const enum AVPixelFormat *pix_fmts)
{
    InputStream *ist = s->opaque;
    const enum AVPixelFormat *p;
    int ret;

    for (p = pix_fmts; *p != -1; p++) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(*p);
        const HWAccel *hwaccel;

        if (!(desc->flags & AV_PIX_FMT_FLAG_HWACCEL))
            break;

        hwaccel = get_hwaccel(*p);
        if (!hwaccel ||
  //          (ist->active_hwaccel_id && ist->active_hwaccel_id != hwaccel->id) ||
            (ist->hwaccel_id != HWACCEL_AUTO && ist->hwaccel_id != hwaccel->id))
            continue;

        ret = hwaccel->init(s);
        if (ret < 0) {
            if (ist->hwaccel_id == hwaccel->id) {
                av_log(NULL, AV_LOG_FATAL,
                       "%s hwaccel requested for input stream #%d:%d, "
                       "but cannot be initialized.\n", hwaccel->name,
                       ist->file_index, ist->st->index);
                return AV_PIX_FMT_NONE;
            }
            continue;
        }
//        ist->active_hwaccel_id = hwaccel->id;
        ist->hwaccel_pix_fmt   = *p;
        break;
    }

    return *p;
}

static int get_buffer(AVCodecContext *s, AVFrame *frame, int flags)
{
    InputStream *ist = s->opaque;

    if (ist->hwaccel_get_buffer && frame->format == ist->hwaccel_pix_fmt)
        return ist->hwaccel_get_buffer(s, frame, flags);

    return avcodec_default_get_buffer2(s, frame, flags);
}
#endif

int stream_component_open(VideoState *is, int stream_index) {

    AVFormatContext *pFormatCtx = is->pFormatCtx;
    AVCodecContext *codecCtx;
    AVCodec *codec;
    AVCodec *codec_hw = NULL;


    if (stream_index < 0 || (unsigned int) stream_index >= pFormatCtx->nb_streams) {
        return -1;
    }

    if (strcmp(pFormatCtx->iformat->name, "mpegts") == 0)
        demux_pid = 1;

    // Get a pointer to the codec context for the video stream

    codecCtx = pFormatCtx->streams[stream_index]->codec;
    avcodec_close(codecCtx);

    if (codecCtx->codec_type == AVMEDIA_TYPE_VIDEO) {
        if (!Settings.hardware_decode) codecCtx->flags |= AV_CODEC_FLAG_GRAY;
        is->dec_ctx = codecCtx;
#ifdef HARDWARE_DECODE
        ist->dec_ctx = codecCtx;
        ist->dec_ctx->opaque = ist;
        ist->dec_ctx->get_format            = get_format;
        ist->dec_ctx->get_buffer2           = get_buffer;
//        ist->dec_ctx->thread_safe_callbacks = 1;
        ist->hwaccel_id = -1; //HWACCEL_AUTO;
        if (hardware_decode) {
            ist->hwaccel_id = HWACCEL_AUTO;
        }
#endif

//        codecCtx->flags2 |= CODEC_FLAG2_FAST /* | AV_CODEC_FLAG2_SHOW_ALL */ ;
//        codecCtx->flags2 |= AV_CODEC_FLAG2_CHUNKS /* | AV_CODEC_FLAG2_SHOW_ALL */ ;



        if (codecCtx->codec_id != AV_CODEC_ID_MPEG1VIDEO) {
            codecCtx->thread_count = Settings.thread_count;
        }

        if (codecCtx->codec_id == AV_CODEC_ID_H264) {
            is_h264 = 1;
        } else {
            int w;
            if (lowres == 10) {
                w = codecCtx->width;
                lowres = 0;
                while (w > 600) {
                    w = w >> 1;
                    lowres++;
                }
            }
        }

        if (codecCtx->codec_id != AV_CODEC_ID_MPEG1VIDEO) {
            codecCtx->thread_count = Settings.thread_count;
        }
    }


    codec = avcodec_find_decoder(codecCtx->codec_id);

    // If decoding in hardware try if running on a Raspberry Pi and then use it's decoder instead.
    if (Settings.hardware_decode) {
        if (codecCtx->codec_id == AV_CODEC_ID_MPEG2VIDEO && avcodec_find_decoder_by_name("mpeg2_mmal") != NULL) codec_hw = avcodec_find_decoder_by_name("mpeg2_mmal");
        if (codecCtx->codec_id == AV_CODEC_ID_H264 && avcodec_find_decoder_by_name("h264_mmal") != NULL) codec_hw = avcodec_find_decoder_by_name("h264_mmal");
        if (codecCtx->codec_id == AV_CODEC_ID_MPEG4 && avcodec_find_decoder_by_name("mpeg4_mmal") != NULL) codec_hw = avcodec_find_decoder_by_name("mpeg4_mmal");
        if (codecCtx->codec_id == AV_CODEC_ID_VC1 && avcodec_find_decoder_by_name("vc1_mmal") != NULL) codec_hw = avcodec_find_decoder_by_name("vc1_mmal");
    }

    if (Settings.use_cuvid) {
        if (codecCtx->codec_id == AV_CODEC_ID_MPEG2VIDEO && avcodec_find_decoder_by_name("mpeg2_cuvid") != NULL) codec_hw = avcodec_find_decoder_by_name("mpeg2_cuvid");
        if (codecCtx->codec_id == AV_CODEC_ID_H264 && avcodec_find_decoder_by_name("h264_cuvid") != NULL) codec_hw = avcodec_find_decoder_by_name("h264_cuvid");
        if (codecCtx->codec_id == AV_CODEC_ID_HEVC && avcodec_find_decoder_by_name("hevc_cuvid") != NULL) codec_hw = avcodec_find_decoder_by_name("hevc_cuvid");
        if (codecCtx->codec_id == AV_CODEC_ID_MPEG4 && avcodec_find_decoder_by_name("mpeg4_cuvid") != NULL) codec_hw = avcodec_find_decoder_by_name("mpeg4_cuvid");
        if (codecCtx->codec_id == AV_CODEC_ID_VC1 && avcodec_find_decoder_by_name("vc1_cuvidl") != NULL) codec_hw = avcodec_find_decoder_by_name("vc1_cuvidl");
    }

    if (Settings.use_vdpau) {
        if (codecCtx->codec_id == AV_CODEC_ID_MPEG2VIDEO && avcodec_find_decoder_by_name("mpeg2_vdpau") != NULL) codec_hw = avcodec_find_decoder_by_name("mpeg2_vdpau");
        if (codecCtx->codec_id == AV_CODEC_ID_H264 && avcodec_find_decoder_by_name("h264_vdpau") != NULL) codec_hw = avcodec_find_decoder_by_name("h264_vdpau");
        if (codecCtx->codec_id == AV_CODEC_ID_MPEG4 && avcodec_find_decoder_by_name("mpeg4_vdpau") != NULL) codec_hw = avcodec_find_decoder_by_name("mpeg4_vdpau");
        if (codecCtx->codec_id == AV_CODEC_ID_VC1 && avcodec_find_decoder_by_name("vc1_vdpau") != NULL) codec_hw = avcodec_find_decoder_by_name("vc1_vdpau");
    }
    if (Settings.use_dxva2) {
        if (codecCtx->codec_id == AV_CODEC_ID_MPEG2VIDEO && avcodec_find_decoder_by_name("mpeg2_dxva2") != NULL) codec_hw = avcodec_find_decoder_by_name("mpeg2_dxva2");
        if (codecCtx->codec_id == AV_CODEC_ID_H264 && avcodec_find_decoder_by_name("h264_dxva2") != NULL) codec_hw = avcodec_find_decoder_by_name("h264_dxva2");
        if (codecCtx->codec_id == AV_CODEC_ID_MPEG4 && avcodec_find_decoder_by_name("mpeg4_dxva2") != NULL) codec_hw = avcodec_find_decoder_by_name("mpeg4_dxva2");
        if (codecCtx->codec_id == AV_CODEC_ID_VC1 && avcodec_find_decoder_by_name("vc1_dxva2") != NULL) codec_hw = avcodec_find_decoder_by_name("vc1_dxva2");
    }

    if (!Settings.hardware_decode) av_dict_set_int(&myoptions, "gray", 1, 0);


    //       av_dict_set_int(&myoptions, "fastint", 1, 0);
    //       av_dict_set_int(&myoptions, "skip_alpha", 1, 0);
//        av_dict_set(&myoptions, "threads", "auto", 0);

    if (codec_hw != NULL && codec_hw != codec) {
        fprintf(stderr, "Using Codec: %s instead of %s\n", codec_hw->name, codec->name);
        codec = codec_hw;
    }

    if (!codec || (avcodec_open2(codecCtx, codec, &myoptions) < 0)) {
        fprintf(stderr, "Unsupported codec!\n");
        return -1;
    }

    switch (codecCtx->codec_type) {
        case AVMEDIA_TYPE_SUBTITLE:
            is->subtitleStream = stream_index;
            is->subtitle_st = pFormatCtx->streams[stream_index];
            if (demux_pid)
                selected_subtitle_pid = is->subtitle_st->id;
            break;
        case AVMEDIA_TYPE_AUDIO:
            is->audioStream = stream_index;
            is->audio_st = pFormatCtx->streams[stream_index];
//          is->audio_buf_size = 0;
//          is->audio_buf_index = 0;

            /* averaging filter for audio sync */
//          is->audio_diff_avg_coef = exp(log(0.01 / AUDIO_DIFF_AVG_NB));
//          is->audio_diff_avg_count = 0;
            /* Correct audio only if larger error than this */
//          is->audio_diff_threshold = 2.0 * SDL_AUDIO_BUFFER_SIZE / codecCtx->sample_rate;
            if (demux_pid)
                selected_audio_pid = is->audio_st->id;


            //       memset(&is->audio_pkt, 0, sizeof(is->audio_pkt));
            break;
        case AVMEDIA_TYPE_VIDEO:
            is->videoStream = stream_index;
            is->video_st = pFormatCtx->streams[stream_index];

//          is->frame_timer = (double)av_gettime() / 1000000.0;
//          is->frame_last_delay = 40e-3;
//          is->video_current_pts_time = av_gettime();

            is->pFrame = av_frame_alloc();
            if (!Settings.hardware_decode) codecCtx->flags |= AV_CODEC_FLAG_GRAY;
//       codecCtx->thread_type = 1; // Frame based threading
            codecCtx->lowres = min(av_codec_get_max_lowres(codecCtx->codec), lowres);
            if (codecCtx->codec_id == AV_CODEC_ID_H264) {
                is_h264 = 1;
            }

            //        codecCtx->flags2 |= CODEC_FLAG2_FAST;
            if (codecCtx->codec_id != AV_CODEC_ID_MPEG1VIDEO) {
                codecCtx->thread_count = Settings.thread_count;
            }
            if (codecCtx->codec_id == AV_CODEC_ID_MPEG1VIDEO)
                is->video_st->codec->ticks_per_frame = 1;
            if (demux_pid)
                selected_video_pid = is->video_st->id;
            /*
            MPEG
                            if(  (codecCtx->skip_frame >= AVDISCARD_NONREF && s2->pict_type==FF_B_TYPE)
                                ||(codecCtx->skip_frame >= AVDISCARD_NONKEY && s2->pict_type!=FF_I_TYPE)
                                || codecCtx->skip_frame >= AVDISCARD_ALL)


                            if(  (s->avctx->skip_idct >= AVDISCARD_NONREF && s->pict_type == FF_B_TYPE)
                               ||(codecCtx->skip_idct >= AVDISCARD_NONKEY && s->pict_type != FF_I_TYPE)
                               || s->avctx->skip_idct >= AVDISCARD_ALL)
            h.264
                if(   s->codecCtx->skip_loop_filter >= AVDISCARD_ALL
                   ||(s->codecCtx->skip_loop_filter >= AVDISCARD_NONKEY && h->slice_type_nos != FF_I_TYPE)
                   ||(s->codecCtx->skip_loop_filter >= AVDISCARD_BIDIR  && h->slice_type_nos == FF_B_TYPE)
                   ||(s->codecCtx->skip_loop_filter >= AVDISCARD_NONREF && h->nal_ref_idc == 0))

            Both
                            if(  (codecCtx->skip_frame >= AVDISCARD_NONREF && s2->pict_type==FF_B_TYPE)
                                ||(codecCtx->skip_frame >= AVDISCARD_NONKEY && s2->pict_type!=FF_I_TYPE)
                                || codecCtx->skip_frame >= AVDISCARD_ALL)
                                break;

            */
            if (skip_B_frames)
                codecCtx->skip_frame = AVDISCARD_NONREF;
            //          codecCtx->skip_loop_filter = AVDISCARD_NONKEY;
//           codecCtx->skip_idct = AVDISCARD_NONKEY;

            break;
        default:
            break;
    }

    return (0);
}

static void log_callback_report(void *ptr, int level, const char *fmt, va_list vl) {
    va_list vl2;
//    int l;
    char line[1024];
    static int print_prefix = 1;

    if (reviewing)
        return;
    av_log_get_level();
    if (level > av_log_level)
        return;
    va_copy(vl2, vl);
    //   av_log_default_callback(ptr, level, fmt, vl);
    av_log_format_line(ptr, level, fmt, vl2, line, sizeof(line), &print_prefix);
    va_end(vl2);
    Debug(10, line);

    //   fputs(line, report_file);
    //   fflush(report_file);
}

void file_open() {
    VideoState *is;
    int subtitle_index = -1, audio_index = -1, video_index = -1;
    int openretries = 0;

    if (global_video_state == NULL) {
        is = av_mallocz(sizeof(VideoState));
        memset(&is->audio_pkt, 0, sizeof(is->audio_pkt));
        strcpy(is->filename, mpegfilename);
        // Register all formats and codecs
        av_log_level = AV_LOG_INFO;
        av_log_set_callback(log_callback_report);

        av_log_set_flags(AV_LOG_SKIP_REPEATED);
        avcodec_register_all();
//    avfilter_register_all();
        av_register_all();
        avformat_network_init();
        global_video_state = is;
        is->videoStream = -1;
        is->audioStream = -1;
        is->subtitleStream = -1;
        is->pFormatCtx = NULL;

//        av_dict_set_int(&opts, "lowres", stream_lowres, 0);
        if (!Settings.hardware_decode) {
//            codecCtx->flags |= AV_CODEC_FLAG_GRAY;
            av_dict_set_int(&myoptions, "gray", 1, 0);
        }

        //        if (thread_count == 1)
        av_dict_set_int(&myoptions, "threads", Settings.thread_count, 0);
        //        else
        //            av_dict_set(&myoptions, "threads", "auto", 0);
        //           codecCtx->thread_count= Settings.thread_count;

        av_dict_set_int(&myoptions, "refcounted_frames", 1, 0); // No need to keep multiple buffers
    } else
        is = global_video_state;
    // Open video file
    if (is->pFormatCtx == NULL) {
        is->pFormatCtx = avformat_alloc_context();
        is->pFormatCtx->max_analyze_duration *= 4;
//        pFormatCtx->probesize = 400000;
        again:
        if (avformat_open_input(&is->pFormatCtx, is->filename, NULL, &myoptions) != 0) {
            fprintf(stderr, "%s: Can not open file\n", is->filename);
            if (openretries++ < live_tv_retries) {
                Sleep(1000L);
                goto again;
            }
            exit(-1);

        }
        is->seek_by_bytes = !!(is->pFormatCtx->iformat->flags & AVFMT_TS_DISCONT) && strcmp("ogg", is->pFormatCtx->iformat->name);



        // Retrieve stream information
        if (avformat_find_stream_info(is->pFormatCtx, 0L) < 0) {
            fprintf(stderr, "%s: Can not find stream info\n", is->filename);
            exit(-1);
        }
        // Dump information about file onto standard error
        if (retries == 0) av_dump_format(is->pFormatCtx, 0, is->filename, 0);
    }

    if (is->videoStream == -1) {
        video_index = av_find_best_stream(is->pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
        if (video_index >= 0) {
            stream_component_open(is, video_index);
        }
        if (is->videoStream < 0) {
            Debug(0, "Could not open video codec\n");
            fprintf(stderr, "%s: could not open video codec\n", is->filename);
            exit(-1);
        }

        if (is->video_st->duration == AV_NOPTS_VALUE || is->video_st->duration < 0)
            is->duration = ((float) is->pFormatCtx->duration) / AV_TIME_BASE;
        else
            is->duration = av_q2d(is->video_st->time_base) * is->video_st->duration;

        if (is->duration < 0 && (live_tv_retries > 0)) {
            Debug(0, "Could not establish duration, live TV decoding may fail\n");
        }


        /* Calc FPS */
        if (is->video_st->r_frame_rate.den && is->video_st->r_frame_rate.num) {
            is->fps = av_q2d(is->video_st->r_frame_rate);
        } else {
            Debug(10, "Warning, no stream frame rate, deriving from codec\n");
            is->fps = 1 / (av_q2d(is->video_st->codec->time_base) * is->video_st->codec->ticks_per_frame);
        }
        set_fps(1.0 / is->fps);
//        Debug(1, "Stream frame rate is %5.3f f/s\n", is->fps);


    }

    if (is->audioStream == -1 && video_index >= 0) {

        audio_index = av_find_best_stream(is->pFormatCtx, AVMEDIA_TYPE_AUDIO, -1, video_index, NULL, 0);
        if (audio_index >= 0) {
            stream_component_open(is, audio_index);

            if (is->audioStream < 0) {
                Debug(1, "Could not open audio decoder or no audio present\n");
            }
        }

    }

    if (is->subtitleStream == -1 && video_index >= 0) {
        subtitle_index = av_find_best_stream(is->pFormatCtx, AVMEDIA_TYPE_SUBTITLE, -1, video_index, NULL, 0);
        if (subtitle_index >= 0) {
            is->subtitle_st = is->pFormatCtx->streams[subtitle_index];
            if (demux_pid)
                selected_subtitle_pid = is->subtitle_st->id;
        }

    }
    av_log_level = AV_LOG_ERROR;


    is->seek_req = 0;
//                    framenum = 0;
    pts_offset = 0.0;
    is->video_clock = 0.0;
    is->audio_clock = 0.0;
    initial_apts = 0;
    apts_offset = 0.0;
    base_apts = 0.0;
    top_apts = 0.0;
    apts = 0.0;
    audio_buffer_ptr = audio_buffer;
    audio_samples = 0;
}


void file_close() {
    is = global_video_state;

//    av_freep(&ist->hwaccel_device);


    if (is->videoStream != -1) avcodec_close(is->pFormatCtx->streams[is->videoStream]->codec);
    is->videoStream = -1;
//    avcodec_free_context(&is->pFormatCtx->streams[is->videoStream]->codec);

    if (is->audioStream != -1) avcodec_close(is->pFormatCtx->streams[is->audioStream]->codec);
    is->audioStream = -1;
    if (is->subtitleStream != -1) avcodec_close(is->pFormatCtx->streams[is->subtitleStream]->codec);
    is->subtitleStream = -1;
//    is->pFormatCtx = NULL;


    avformat_close_input(&is->pFormatCtx);

    av_frame_free(&is->frame);

#ifdef HARDWARE_DECODE
    ist->hwaccel_ctx = NULL;
#endif

    avformat_network_deinit();
//  global_video_state = NULL;
};

void searchCom(char* videoname){
    AVPacket pkt1, *packet = &pkt1;
    int result = 0;
    int ret;
    double tfps;
    double old_clock = 0.0;
    int empty_packet_count = 0;

    int64_t last_packet_pos = 0;
    int64_t last_packet_pts = 0;
    double retry_target = 0.0;

    retries = 0;

    char *ptr;
    size_t len;

    sett_initDefaults();


    fprintf(stderr, "%s, made using ffmpeg\n", PACKAGE_STRING);


    char *argv[30];
    argv[0] = "executeable";
    argv[1] = videoname;
// todo here
    in_file = LoadSettings(2, argv);

    file_open();


    csRestart = 0;
    framenum = 0;

//        DUMP_OPEN

    if (output_timing) {
        sprintf(tempstring, "%s.timing.csv", inbasename);
        timing_file = myfopen(tempstring, "w");
        DUMP_HEADER
    }

    av_log_set_level(AV_LOG_DEBUG);
    av_log_set_flags(AV_LOG_SKIP_REPEATED);
    av_log_set_callback(log_callback_report);
    is = global_video_state;
    packet = &(is->audio_pkt);

    // main decode loop
    again:
    for (;;) {
        if (is->quit) {
            break;
        }
        // seek stuff goes here
        if (is->seek_req) {
            if (is->seek_pts > 0.0) {
                DoSeekRequest(is);
            } else {
                is->seek_req = 0;
                file_close();
                file_open();

                DUMP_CLOSE
                DUMP_OPEN
                DUMP_HEADER
                close_data();
#ifdef PROCESS_CC
                if (output_srt || output_smi) CEW_reinit();
#endif
            }
        }
        nextpacket:
        ret = av_read_frame(is->pFormatCtx, packet);

        if (ret >= 0 && is->seek_req) {
            double packet_time = (packet->pts - (is->video_st->start_time != AV_NOPTS_VALUE ? is->video_st->start_time : 0)) * av_q2d(is->video_st->time_base);
            if (packet->pts == AV_NOPTS_VALUE || packet->pts == 0) {
                av_packet_unref(packet);
                goto nextpacket;
            }
            if (is->seek_req < 6 && (is->seek_flags & AVSEEK_FLAG_BYTE) && is->duration > 0 && fabs(packet_time - (is->seek_pts - 2.5)) < is->duration / (10 * is->seek_req)) {
                is->seek_pos += ((is->seek_pts - 2.5 - packet_time) / is->duration) * avio_size(is->pFormatCtx->pb) * 0.9;
                is->seek_req++;
                goto again;
            }
            if (retries)
                Debug(9, "Retry t_pos=%" PRId64 ", l_pos=%" PRId64 ", t_pts=%" PRId64 ", l_pts=%" PRId64 "\n", last_packet_pos, packet->pos, last_packet_pts, packet->pts);
            is->seek_req = 0;
        }
        /*
                if (ret < 0 && is->seek_req && !is->seek_by_bytes) {
                    is->seek_by_bytes = 1;
                    Set_seek(is, is->seek_pts, is->duration);
                    goto again;
                }
        */
        is->seek_req = 0;


#define REOPEN_TIME 500.0

        if ((selftest == 3 && retries == 0 && is->video_clock >= REOPEN_TIME)) {
            ret = AVERROR_EOF;  // Simulate EOF
            live_tv = 1;
            live_tv_retries = 2;

        }
        if ((selftest == 4 && retries == 0 && framenum > 0 && (framenum % 500) == 0)) // Test reopen every 500 frames
        {
            ret = AVERROR_EOF;  // Simulate EOF
            live_tv = 1;
            live_tv_retries = 2;

        }
        if (ret < 0) {
            if (ret == AVERROR_EOF || is->pFormatCtx->pb->eof_reached) {
                if (selftest == 3)   // Either simulated EOF or real EOF before REOPEN_TIME
                {
                    if (retries > 0) {
                        if (is->video_clock < selftest_target - 0.05 || is->video_clock > selftest_target + 0.05) {
                            sample_file = fopen("seektest.log", "a+");
                            fprintf(sample_file, "\"%s\": reopen file failed, size=%8.1f, pts=%6.2f\n", is->filename, is->duration, is->video_clock);
                            fclose(sample_file);
                            Debug(1, "\nSelftest %d FAILED\n", selftest);
                            exit(1);
                        }
                    } else {
                        if (is->video_clock < REOPEN_TIME) {
                            selftest_target = is->video_clock - 2.0;
                        } else {
                            selftest_target = REOPEN_TIME;
                        }
                        Debug(1, "\nSelftest %d starting: Reopen\n", selftest);
                        selftest_target = fmax(selftest_target, 0.5);
                        live_tv = 1;
                        live_tv_retries = 2;

                    }
                }

                if ((live_tv && retries < live_tv_retries)) {
                    double frame_delay = av_q2d(is->video_st->codec->time_base) * is->video_st->codec->ticks_per_frame;
                    if (retries == 0) {
                        if (selftest == 3)
                            retry_target = selftest_target;
                        else
                            retry_target = is->video_clock + frame_delay;
                    }
                    file_close();
                    Debug(1, "\nRetry=%d at frame=%d, time=%8.2f seconds\n", retries, framenum, retry_target);
                    Debug(9, "Retry target pos=%" PRId64 ", pts=%" PRId64 "\n", last_packet_pos, last_packet_pts);

                    if (selftest == 0) Sleep(4000L);
                    file_open();
                    Set_seek(is, retry_target);

                    retries++;
                    goto again;
                }

                break;
            }


        }

        if (packet->pts != AV_NOPTS_VALUE && packet->pts != 0) {
            last_packet_pts = packet->pts;
        }
        if (packet->pos != 0 && packet->pos != -1) {
            last_packet_pos = packet->pos;
        }

        if (packet->stream_index == is->videoStream) {
            if (packet->size > 0 && packet->data != NULL)
                video_packet_process(is, packet);
        } else if (packet->stream_index == is->audioStream) {
            if (packet->size > 0 && packet->data != NULL)
                audio_packet_process(is, packet);
        } else {
            /*
                          ccDataLen = (int)packet->size;
                          for (i=0; i<ccDataLen; i++) {
                              ccData[i] = packet->data[i];
                          }
                          dump_data((char *)ccData, (int)ccDataLen);
                                if (output_srt)
                                    process_block(ccData, (int)ccDataLen);
                                if (processCC) ProcessCCData();
            */
        }
        av_packet_unref(packet);
        if (is->video_clock == old_clock) {
            empty_packet_count++;
            if (empty_packet_count > 1000)
                Debug(0, "Empty input\n");
            empty_packet_count = 0;
        } else {
            old_clock = is->video_clock;
            empty_packet_count = 0;
        }
    }

    if (selftest == 1 && pass == 1 /*&& framenum > 501 && is->video_clock > 0 */) {
        if (is->video_clock < selftest_target - 0.08 || is->video_clock > selftest_target + 0.08) {
            sample_file = fopen("seektest.log", "a+");
            fprintf(sample_file, "Seek error: target=%8.1f, result=%8.1f, error=%6.3f, size=%8.1f, mode=%s\"%s\"\n",
                    is->seek_pts,
                    is->video_clock,
                    is->video_clock - is->seek_pts,
                    is->duration,
                    (is->seek_by_bytes ? "byteseek" : "timeseek"),
                    is->filename);
            fclose(sample_file);
        } else
            Debug(1, "\nSelftest 1 OK: Seektest\n");
        selftest = 3;
        pass = 0;
    }


    if (live_tv) {
        lastFrameCommCalculated = 0;
        BuildCommListAsYouGo();
    }

    tfps = print_fps(1);

    Debug(10, "\nParsed %d video frames and %d audio frames at %8.2f fps\n", framenum, sound_frame_counter, tfps);
    Debug(10, "\nMaximum Volume found is %d\n", max_volume_found);


    in_file = 0;
    if (framenum > 0) {
        if (BuildMasterCommList()) {
            result = 1;
            printf("Commercials were found.\n");
        } else {
            result = 0;
            printf("Commercials were not found.\n");
        }
        if (output_debugwindow) {
            processCC = 0;
            printf("Close window when done\n");

            DUMP_CLOSE
            if (output_timing) {
                output_timing = 0;
            }
        }
    }

    exit(!result);
}