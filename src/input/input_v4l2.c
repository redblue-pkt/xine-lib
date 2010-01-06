#define LOG_MODULE "v4l2"
#define LOG

#include <xine/input_plugin.h>
#include <xine/xine_plugin.h>
#include <xine/xine_internal.h>

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/mman.h>
#include <stdio.h>
#include <libv4l2.h>
#include <errno.h>

typedef struct  {
    void *start;
    size_t length;
} buffer_data;

typedef struct {
    int width;
    int height;
} resolution_t;

typedef struct {
    buffer_data *buffers;
    int bufcount;
    resolution_t resolution;
    int headerSent;
} v4l2_video_t;

typedef struct {
    buffer_data *buffers;
    int bufcount;
} v4l2_radio_t;

typedef struct {
    input_plugin_t input_plugin;

    int fd;
    char* mrl;
    struct v4l2_capability cap;
    xine_stream_t *stream;

    xine_event_queue_t *events;
    v4l2_video_t* video;
    v4l2_radio_t* radio;
} v4l2_input_plugin_t;

void v4l2_input_enqueue_video_buffer(v4l2_input_plugin_t *this, int idx);
void v4l2_input_dequeue_video_buffer(v4l2_input_plugin_t *this, buf_element_t *input);
int v4l2_input_setup_video_streaming(v4l2_input_plugin_t *this);


int v4l2_input_open(input_plugin_t *this_gen) {
    v4l2_input_plugin_t *this = (v4l2_input_plugin_t*) this_gen;
    lprintf("Opening %s\n", this->mrl);
    if ((this->fd = v4l2_open(this->mrl, O_RDWR))) {
        /* TODO: Clean up this mess */
        this->events = xine_event_new_queue(this->stream);
        v4l2_ioctl(this->fd, VIDIOC_QUERYCAP, &(this->cap));
        if (this->cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) {
            this->video = malloc(sizeof(v4l2_video_t));
            this->video->headerSent = 0;
            this->video->bufcount = 0;
        }
        if (this->cap.capabilities & V4L2_CAP_STREAMING) {
            lprintf("Supports streaming. Allocating buffers...\n");
            if (this->cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) {
                if (v4l2_input_setup_video_streaming(this)) {
                    lprintf("Video streaming ready.\n");
                    return 1;
                } else {
                    /* TODO: Fallbacks */
                    lprintf("Video streaming setup failed.\n");
                    return 0;
                }
            } else {
                /* TODO: Radio streaming */
                lprintf("Sorry, only video is supported for now.\n");
                return 0;
            }
        } else {
            lprintf("Device doesn't support streaming. Prod the author to support the other methods.\n");
            return 0;
        }
    } else {
        return 0;
    }
}

int v4l2_input_setup_video_streaming(v4l2_input_plugin_t *this) {
    this->video->bufcount = 0;
    struct v4l2_requestbuffers reqbuf;
    unsigned int i;
    memset(&reqbuf, 0, sizeof(reqbuf));
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    reqbuf.count = 25;

    if (-1 == v4l2_ioctl(this->fd, VIDIOC_REQBUFS, &reqbuf)) {
        lprintf("Buffer request failed. Is streaming supported?\n");
        return 0;
    }
    
    this->video->bufcount = reqbuf.count;
    lprintf("Got %i buffers for stremaing.\n", reqbuf.count);
    
    this->video->buffers = calloc(this->video->bufcount, sizeof(buffer_data));
    _x_assert(this->video->buffers);
    for (i = 0;i < this->video->bufcount;i++) {
        struct v4l2_buffer buffer;
        memset(&buffer, 0, sizeof(buffer));
        buffer.type = reqbuf.type;
        buffer.memory = reqbuf.memory;
        buffer.index = i;
        
        if (-1 == v4l2_ioctl(this->fd, VIDIOC_QUERYBUF, &buffer)) {
            lprintf("Couldn't allocate buffer %i\n", i);
            return 0;
        }
        
        this->video->buffers[i].length = buffer.length;
        this->video->buffers[i].start = (void*)v4l2_mmap(NULL, buffer.length,
                                            PROT_READ | PROT_WRITE,
                                            MAP_SHARED,
                                            this->fd, buffer.m.offset);
        if (MAP_FAILED == this->video->buffers[i].start) {
            lprintf("Couldn't mmap buffer %i\n", i);
            int j;
            for(j = 0;j<i;j++) {
                v4l2_munmap(this->video->buffers[i].start, this->video->buffers[i].length);
            }
            free(this->video->buffers);
            this->video->bufcount = 0;
            return 0;
        }
        v4l2_input_enqueue_video_buffer(this, i);
    }
        
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    /* TODO: Other formats? MPEG support? */
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    v4l2_ioctl(this->fd, VIDIOC_S_FMT, &fmt);
    this->video->resolution.width = fmt.fmt.pix.width;
    this->video->resolution.height = fmt.fmt.pix.height;
    if (-1 == v4l2_ioctl(this->fd, VIDIOC_STREAMON, &reqbuf.type)) {
        lprintf("Couldn't start streaming: %s\n", strerror(errno));
        return 0;
    }
    return 1;
}

buf_element_t* v4l2_input_read_block(input_plugin_t *this_gen, fifo_buffer_t *fifo, off_t len) {
    lprintf("Reading block\n");
    v4l2_input_plugin_t *this = (v4l2_input_plugin_t*)this_gen;
    buf_element_t *buf = fifo->buffer_pool_alloc(fifo);
    struct timeval tv;
    xine_monotonic_clock(&tv, NULL);
    buf->pts = (int64_t) tv.tv_sec * 90000 + (int64_t) tv.tv_usec * 9 / 100;
    if (!this->video->headerSent) {
        lprintf("Sending video header\n");
        xine_bmiheader bih;
        bih.biSize = sizeof(xine_bmiheader);
        bih.biWidth = this->video->resolution.width*2;
        bih.biHeight = this->video->resolution.height*2;
        lprintf("Getting size of %ix%i\n", this->video->resolution.width, this->video->resolution.height);
        buf->size = sizeof(xine_bmiheader);
        buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAME_START;
        memcpy(buf->content, &bih, sizeof(xine_bmiheader));
        this->video->headerSent = 1;
        buf->type = BUF_VIDEO_YUY2;
    } else {
        lprintf("Sending video frame\n");
        /* TODO: Add audio support */
        v4l2_input_dequeue_video_buffer(this, buf);
        this->video->headerSent = 0;
    }
    return buf;
}

uint32_t v4l2_input_blocksize(input_plugin_t *this_gen) {
    /* HACK */
    return 0;
    v4l2_input_plugin_t *this = (v4l2_input_plugin_t*)this_gen;
    if (this->video->headerSent) {
        lprintf("Returning block size of %i\n",this->video->buffers[0].length);
        return this->video->buffers[0].length;
    } else {
        lprintf("Returning block size of %i\n",sizeof(xine_bmiheader));
        return sizeof(xine_bmiheader);
    }
}

void v4l2_input_dequeue_video_buffer(v4l2_input_plugin_t *this, buf_element_t *output) {
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    output->content = output->mem;
    v4l2_ioctl(this->fd, VIDIOC_DQBUF, &buf);
    output->decoder_flags = BUF_FLAG_FRAME_START|BUF_FLAG_FRAME_END;
    xine_fast_memcpy(output->content, this->video->buffers[buf.index].start, this->video->buffers[buf.index].length);
    output->type = BUF_VIDEO_YUY2;
    v4l2_input_enqueue_video_buffer(this, buf.index);
}

void v4l2_input_enqueue_video_buffer(v4l2_input_plugin_t *this, int idx) {
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.index = idx;
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    v4l2_ioctl(this->fd, VIDIOC_QBUF, &buf);
}

void v4l2_input_dispose(input_plugin_t *this_gen) {
    lprintf("Disposing of myself.\n");
    v4l2_input_plugin_t* this = (v4l2_input_plugin_t*)this_gen;
    
    if (this->video != NULL) {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (-1 == v4l2_ioctl(this->fd, VIDIOC_STREAMOFF, &type)) {
            lprintf("Couldn't stop streaming. Uh oh.\n");
        }
        if (this->video->bufcount > 0) {
            int i;
            for(i = 0;i<this->video->bufcount;i++) {
                v4l2_munmap(this->video->buffers[i].start, this->video->buffers[i].length);
            }
            free(this->video->buffers);
        }
        free(this->video);
    }
    v4l2_close(this->fd);
    free(this->mrl);
    free(this);
}

off_t v4l2_input_read(input_plugin_t *this_gen, char *buf, off_t nlen) {
    /* Only block reads are supported. */
    return 0;
}

uint32_t v4l2_input_get_capabilities(input_plugin_t* this_gen) {
    return INPUT_CAP_BLOCK;
}

const char* v4l2_input_get_mrl(input_plugin_t* this_gen) {
    v4l2_input_plugin_t* this = (v4l2_input_plugin_t*)this_gen;
    /* HACK HACK HACK HACK */
    /* So far, the only way to get the yuv_frames demuxer to work with this */
    return "v4l:/";
    //return this->mrl;
}

int v4l2_input_get_optional_data(input_plugin_t *this_gen, void *data, int data_type) {
    return INPUT_OPTIONAL_UNSUPPORTED;
}

/* Seeking not supported. */
off_t v4l2_input_seek(input_plugin_t *this_gen, off_t offset, int origin) {
    return -1;
}

off_t v4l2_input_seek_time(input_plugin_t *this_gen, int time_offset, int origin) {
    return -1;
}

off_t v4l2_input_pos(input_plugin_t *this_gen) {
    /* TODO */
    return 0;
}

int v4l2_input_time(input_plugin_t *this_gen) {
    /* TODO */
    return 0;
}

off_t v4l2_input_length(input_plugin_t *this_gen) {
    return -1;
}

typedef struct {
    input_class_t input_class;
} v4l2_input_class_t;

static input_plugin_t *v4l2_class_get_instance(input_class_t *gen_cls, xine_stream_t *stream, const char *mrl) {
    v4l2_input_plugin_t *this;
    /* TODO: Radio devices */
    /* FIXME: Don't require devices to be of /dev/videoXXX */
    if (strncmp(mrl, "/dev/video", strlen("/dev/video")) != 0)
        return NULL;
    lprintf("We can handle %s!\n", mrl);

    this = calloc(1, sizeof(v4l2_input_plugin_t));
    _x_assert(this);
    this->mrl = strdup(mrl);
    this->input_plugin.open = v4l2_input_open;
    this->input_plugin.get_capabilities = v4l2_input_get_capabilities;
    this->input_plugin.get_blocksize = v4l2_input_blocksize;
    this->input_plugin.get_mrl = v4l2_input_get_mrl;
    this->input_plugin.dispose = v4l2_input_dispose;
    this->input_plugin.read = v4l2_input_read;
    this->input_plugin.read_block = v4l2_input_read_block;
    this->input_plugin.seek = v4l2_input_seek;
    this->input_plugin.seek_time = v4l2_input_seek_time;
    this->input_plugin.get_current_pos = v4l2_input_pos;
    this->input_plugin.get_current_time = v4l2_input_time;
    this->input_plugin.get_length = v4l2_input_length;
    this->input_plugin.get_optional_data = v4l2_input_get_optional_data;
    this->input_plugin.input_class = gen_cls;
    this->stream = stream;

    this->video = NULL;
    this->radio = NULL;
    lprintf("Ready to read!\n");

    return &this->input_plugin;
}

static const char *v4l2_class_get_description(input_class_t *this_gen) {
    /* TODO: Translatable with _() */
    return "v4l2 input plugin";
}

static const char *v4l2_class_get_identifier(input_class_t *this_gen) {
    return "v4l2";
}

static void v4l2_class_dispose(input_class_t *this_gen) {
    free(this_gen);
}

static void *v4l2_init_class(xine_t *xine, void *data) {
    v4l2_input_class_t *this;
    this = malloc(sizeof(v4l2_input_class_t));
    this->input_class.get_instance = v4l2_class_get_instance;
    this->input_class.get_description = v4l2_class_get_description;
    this->input_class.get_identifier = v4l2_class_get_identifier;
    this->input_class.get_dir = NULL;
    this->input_class.get_autoplay_list = NULL;
    this->input_class.dispose = v4l2_class_dispose;
    this->input_class.eject_media = NULL;
    return &this->input_class;
}

const input_info_t input_info_v4l2 = {
    4000
};

const plugin_info_t xine_plugin_info[] = {
    /* type, API, "name", version, special_info, init_function */  
    { PLUGIN_INPUT, 17, "v4l2", XINE_VERSION_CODE, &input_info_v4l2, v4l2_init_class },
    { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};