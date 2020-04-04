/*  video_device.c
 *
 *
 *  Copyright (C) 2014 Toxic All Rights Reserved.
 *
 *  This file is part of Toxic.
 *
 *  Toxic is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Toxic is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Toxic.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "video_device.h"
#include "video_call.h"

#include <sys/ioctl.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xos.h>

#include <vpx/vpx_image.h>

#if defined(__OSX__)
#import "osx_video.h"
#else
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#if defined(__OpenBSD__) || defined(__NetBSD__)
#include <sys/videoio.h>
#else
#include <linux/videodev2.h>
#endif /* defined(__OpenBSD__) || defined(__NetBSD__) */
#endif /* __OSX__ */

#include "line_info.h"
#include "settings.h"

#include <errno.h>

#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>

#ifdef VIDEO

// -- Zoxcore --
#include <linux/fb.h>
int global_framebuffer_device_fd = 0;
struct fb_var_screeninfo var_framebuffer_info;
struct fb_fix_screeninfo var_framebuffer_fix_info;
size_t framebuffer_screensize = 0;
unsigned char *framebuffer_mappedmem = NULL;

int full_width = 640; // gets set later, this is just as last resort
int full_height = 480; // gets set later, this is just as last resort
int vid_width = 640; // ------- fb resolution -------
int vid_height = 480; // ------- fb resolution -------


#define STBIR_SATURATE_INT 1
#define STBIR_DEFAULT_FILTER_UPSAMPLE STBIR_FILTER_BOX
#define STBIR_NO_ALPHA_EPSILON 1
#define STB_IMAGE_RESIZE_IMPLEMENTATION 1
#define STBIR_ASSERT(x) #x
#include "stb_image_resize.h"

#define SWAP_R_AND_B_COLOR 1 // use BGRA instead of RGBA for raw framebuffer output

#define CLEAR(x) memset(&(x), 0, sizeof(x))

#define max(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

#define min(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a < _b ? _a : _b; })


#define CLIP(X) ( (X) > 255 ? 255 : (X) < 0 ? 0 : X)

// RGB -> YUV
#define RGB2Y(R, G, B) CLIP(( (  66 * (R) + 129 * (G) +  25 * (B) + 128) >> 8) +  16)
#define RGB2U(R, G, B) CLIP(( ( -38 * (R) -  74 * (G) + 112 * (B) + 128) >> 8) + 128)
#define RGB2V(R, G, B) CLIP(( ( 112 * (R) -  94 * (G) -  18 * (B) + 128) >> 8) + 128)

// YUV -> RGB
#define C(Y) ( (Y) - 16  )
#define D(U) ( (U) - 128 )
#define E(V) ( (V) - 128 )

#define YUV2R(Y, U, V) CLIP(( 298 * C(Y)              + 409 * E(V) + 128) >> 8)
#define YUV2G(Y, U, V) CLIP(( 298 * C(Y) - 100 * D(U) - 208 * E(V) + 128) >> 8)
#define YUV2B(Y, U, V) CLIP(( 298 * C(Y) + 516 * D(U)              + 128) >> 8)


void fb_copy_frame_to_fb(void *videoframe)
{
    if (framebuffer_mappedmem != NULL)
    {
        memcpy(framebuffer_mappedmem, videoframe, framebuffer_screensize);
    }
}

#include <stdarg.h>

void dbg(int dummy, const char *format, ...)
{
    FILE *log = fopen("toxic_zox.log", "a");
    va_list ap;
    va_start(ap, format);
    fprintf(log, format, ap);
    va_end(ap);
    fclose(log);
}

// -- Zoxcore --

#define inline__ inline __attribute__((always_inline))

extern struct user_settings *user_settings;

struct VideoBuffer {
    void *start;
    size_t length;
};

typedef struct VideoDevice {
    VideoDataHandleCallback cb;             /* Use this to handle data from input device usually */
    void *cb_data;                          /* Data to be passed to callback */
    int32_t friend_number;                  /* ToxAV friend number */

#if !defined(__OSX__)
    int fd;                                 /* File descriptor of video device selected/opened */
    struct v4l2_format fmt;
    struct VideoBuffer *buffers;
    uint32_t n_buffers;
#endif

    uint32_t ref_count;
    int32_t selection;
    pthread_mutex_t mutex[1];
    uint16_t video_width;
    uint16_t video_height;

    vpx_image_t input;

    Display *x_display;
    Window x_window;
    GC x_gc;

} VideoDevice;

const char *dvideo_device_names[2];              /* Default device */
const char *video_devices_names[2][MAX_DEVICES]; /* Container of available devices */
static int size[2];                        /* Size of above containers */
VideoDevice *video_devices_running[2][MAX_DEVICES] = {{NULL}};     /* Running devices */
uint32_t primary_video_device[2];          /* Primary device */

static ToxAV *av = NULL;

/* q_mutex */
#define lock pthread_mutex_lock(&video_mutex);
#define unlock pthread_mutex_unlock(&video_mutex);
pthread_mutex_t video_mutex;

bool video_thread_running = true,
     video_thread_paused = true;                /* Thread control */

void *video_thread_poll(void *);

static void yuv420tobgr(uint16_t width, uint16_t height, const uint8_t *y,
                        const uint8_t *u, const uint8_t *v, unsigned int ystride,
                        unsigned int ustride, unsigned int vstride, uint8_t *out)
{
    unsigned long int i, j;

    for (i = 0; i < height; ++i) {
        for (j = 0; j < width; ++j) {
            uint8_t *point = out + 4 * ((i * width) + j);
            int t_y = y[((i * ystride) + j)];
            int t_u = u[(((i / 2) * ustride) + (j / 2))];
            int t_v = v[(((i / 2) * vstride) + (j / 2))];
            t_y = t_y < 16 ? 16 : t_y;

            int r = (298 * (t_y - 16) + 409 * (t_v - 128) + 128) >> 8;
            int g = (298 * (t_y - 16) - 100 * (t_u - 128) - 208 * (t_v - 128) + 128) >> 8;
            int b = (298 * (t_y - 16) + 516 * (t_u - 128) + 128) >> 8;

            point[2] = r > 255 ? 255 : r < 0 ? 0 : r;
            point[1] = g > 255 ? 255 : g < 0 ? 0 : g;
            point[0] = b > 255 ? 255 : b < 0 ? 0 : b;
            point[3] = ~0;
        }
    }
}

#if !defined(__OSX__)
static void yuv422to420(uint8_t *plane_y, uint8_t *plane_u, uint8_t *plane_v,
                        uint8_t *input, uint16_t width, uint16_t height)
{
    uint8_t *end = input + width * height * 2;

    while (input != end) {
        uint8_t *line_end = input + width * 2;

        while (input != line_end) {
            *plane_y++ = *input++;
            *plane_u++ = *input++;
            *plane_y++ = *input++;
            *plane_v++ = *input++;
        }

        line_end = input + width * 2;

        while (input != line_end) {
            *plane_y++ = *input++;
            input++;//u
            *plane_y++ = *input++;
            input++;//v
        }
    }
}

static int xioctl(int fh, unsigned long request, void *arg)
{
    int r;

    do {
        r = ioctl(fh, request, arg);
    } while (-1 == r && EINTR == errno);

    return r;
}

#endif

/* Meet devices */
#ifdef VIDEO
VideoDeviceError init_video_devices(ToxAV *av_)
#else
VideoDeviceError init_video_devices()
#endif /* VIDEO */
{
    size[vdt_input] = 0;

#if defined(__OSX__)

    if (osx_video_init((char **)video_devices_names[vdt_input], &size[vdt_input]) != 0) {
        return vde_InternalError;
    }

#else /* not __OSX__*/

    for (; size[vdt_input] <= MAX_DEVICES; ++size[vdt_input]) {
        int fd;
        char device_address[] = "/dev/videoXX";
        snprintf(device_address + 10, sizeof(char) * strlen(device_address) - 10, "%i", size[vdt_input]);

        fd = open(device_address, O_RDWR | O_NONBLOCK, 0);

        if (fd == -1) {
            break;
        } else {
            struct v4l2_capability cap;
            char *video_input_name;

            /* Query V4L for capture capabilities */
            if (-1 != ioctl(fd, VIDIOC_QUERYCAP, &cap)) {
                video_input_name = (char *)malloc(strlen((const char *)cap.card) + strlen(device_address) + 4);
                strcpy(video_input_name, (char *)cap.card);
                strcat(video_input_name, " (");
                strcat(video_input_name, (char *)device_address);
                strcat(video_input_name, ")");
            } else {
                video_input_name = (char *)malloc(strlen(device_address) + 3);
                strcpy(video_input_name, "(");
                strcat(video_input_name, device_address);
                strcat(video_input_name, ")");
            }

            video_devices_names[vdt_input][size[vdt_input]] = video_input_name;

            close(fd);
        }
    }

#endif

// -- Zoxcore --
    size[vdt_output] = 0;

#ifdef X11VIDEO
    char *video_output_name = "Toxic Video Receiver";
    video_devices_names[vdt_output][size[vdt_output]] = video_output_name;
    ++size[vdt_output];
#else
    char *video_output_name2 = "Frame Buffer";
    video_devices_names[vdt_output][size[vdt_output]] = video_output_name2;
    ++size[vdt_output];
#endif
// -- Zoxcore --

    // Start poll thread
    if (pthread_mutex_init(&video_mutex, NULL) != 0) {
        return vde_InternalError;
    }

    pthread_t thread_id;

    if (pthread_create(&thread_id, NULL, video_thread_poll, NULL) != 0 || pthread_detach(thread_id) != 0) {
        return vde_InternalError;
    }

#ifdef VIDEO
    av = av_;
#endif /* VIDEO */

    return (VideoDeviceError) vde_None;
}

VideoDeviceError terminate_video_devices(void)
{
    /* Cleanup if needed */
    lock;
    video_thread_running = false;
    unlock;

    usleep(20000);

    int i;

    for (i = 0; i < size[vdt_input]; ++i) {
        free((void *)video_devices_names[vdt_input][i]);
    }

    if (pthread_mutex_destroy(&video_mutex) != 0) {
        return (VideoDeviceError) vde_InternalError;
    }

#ifdef __OSX__
    osx_video_release();
#endif /* __OSX__ */

    return (VideoDeviceError) vde_None;
}

VideoDeviceError register_video_device_callback(int32_t friend_number, uint32_t device_idx,
        VideoDataHandleCallback callback, void *data)
{
#if defined(__OSX__)

    if (size[vdt_input] <= device_idx || !video_devices_running[vdt_input][device_idx]) {
        return vde_InvalidSelection;
    }

#else /* not __OSX__ */

    if (size[vdt_input] <= device_idx || !video_devices_running[vdt_input][device_idx]
            || !video_devices_running[vdt_input][device_idx]->fd) {
        return vde_InvalidSelection;
    }

#endif

    lock;
    video_devices_running[vdt_input][device_idx]->cb = callback;
    video_devices_running[vdt_input][device_idx]->cb_data = data;
    video_devices_running[vdt_input][device_idx]->friend_number = friend_number;
    unlock;

    return vde_None;
}

VideoDeviceError set_primary_video_device(VideoDeviceType type, int32_t selection)
{
    if (size[type] <= selection || selection < 0) {
        return vde_InvalidSelection;
    }

    primary_video_device[type] = selection;

    return vde_None;
}

VideoDeviceError open_primary_video_device(VideoDeviceType type, uint32_t *device_idx)
{
    return open_video_device(type, primary_video_device[type], device_idx);
}

void get_primary_video_device_name(VideoDeviceType type, char *buf, int size)
{
    memcpy(buf, dvideo_device_names[type], size);
}

VideoDeviceError open_video_device(VideoDeviceType type, int32_t selection, uint32_t *device_idx)
{
    if (size[type] <= selection || selection < 0) {
        return vde_InvalidSelection;
    }

    lock;

    uint32_t i, temp_idx = -1;

    for (i = 0; i < MAX_DEVICES; ++i) {
        if (!video_devices_running[type][i]) {
            temp_idx = i;
            break;
        }
    }

    if (temp_idx == -1) {
        unlock;
        return vde_AllDevicesBusy;
    }

    for (i = 0; i < MAX_DEVICES; i ++) { /* Check if any device has the same selection */
        if (video_devices_running[type][i] && video_devices_running[type][i]->selection == selection) {

            video_devices_running[type][temp_idx] = video_devices_running[type][i];
            video_devices_running[type][i]->ref_count++;

            unlock;
            return vde_None;
        }
    }

    VideoDevice *device = video_devices_running[type][temp_idx] = calloc(1, sizeof(VideoDevice));
    device->selection = selection;

    if (pthread_mutex_init(device->mutex, NULL) != 0) {
        free(device);
        unlock;
        return vde_InternalError;
    }

    if (type == vdt_input) {
        video_thread_paused = true;

#if defined(__OSX__)

        if (osx_video_open_device(selection, &device->video_width, &device->video_height) != 0) {
            free(device);
            unlock;
            return vde_FailedStart;
        }

#else /* not __OSX__*/
        /* Open selected device */
        char device_address[] = "/dev/videoXX";
        snprintf(device_address + 10, sizeof(device_address) - 10, "%i", selection);

        device->fd = open(device_address, O_RDWR);

        if (device->fd == -1) {
            unlock;
            return vde_FailedStart;
        }

        /* Obtain video device capabilities */
        struct v4l2_capability cap;

        if (-1 == xioctl(device->fd, VIDIOC_QUERYCAP, &cap)) {
            close(device->fd);
            free(device);
            unlock;
            return vde_FailedStart;
        }

        /* Setup video format */
        struct v4l2_format fmt;
        memset(&(fmt), 0, sizeof(fmt));

        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;

        if (-1 == xioctl(device->fd, VIDIOC_S_FMT, &fmt)) {
            close(device->fd);
            free(device);
            unlock;
            return vde_FailedStart;
        }

        device->video_width = fmt.fmt.pix.width;
        device->video_height = fmt.fmt.pix.height;

        /* Request buffers */
        struct v4l2_requestbuffers req;
        memset(&(req), 0, sizeof(req));
        req.count = 4;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;

        if (-1 == xioctl(device->fd, VIDIOC_REQBUFS, &req)) {
            close(device->fd);
            free(device);
            unlock;
            return vde_FailedStart;
        }

        if (req.count < 2) {
            close(device->fd);
            free(device);
            unlock;
            return vde_FailedStart;
        }

        device->buffers = calloc(req.count, sizeof(struct VideoBuffer));

        for (i = 0; i < req.count; ++i) {
            struct v4l2_buffer buf;
            memset(&(buf), 0, sizeof(buf));

            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = i;

            if (-1 == xioctl(device->fd, VIDIOC_QUERYBUF, &buf)) {
                close(device->fd);
                free(device);
                unlock;
                return vde_FailedStart;
            }

            device->buffers[i].length = buf.length;
            device->buffers[i].start = mmap(NULL /* start anywhere */,
                                            buf.length,
                                            PROT_READ | PROT_WRITE /* required */,
                                            MAP_SHARED /* recommended */,
                                            device->fd, buf.m.offset);

            if (MAP_FAILED == device->buffers[i].start) {
                for (i = 0; i < buf.index; ++i) {
                    munmap(device->buffers[i].start, device->buffers[i].length);
                }

                close(device->fd);
                free(device);
                unlock;
                return vde_FailedStart;
            }
        }

        device->n_buffers = i;

        enum v4l2_buf_type type;

        for (i = 0; i < device->n_buffers; ++i) {
            struct v4l2_buffer buf;
            memset(&(buf), 0, sizeof(buf));

            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = i;

            if (-1 == xioctl(device->fd, VIDIOC_QBUF, &buf)) {
                for (i = 0; i < device->n_buffers; ++i) {
                    munmap(device->buffers[i].start, device->buffers[i].length);
                }

                close(device->fd);
                free(device);
                unlock;
                return vde_FailedStart;
            }
        }

        type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        /* Turn on video stream */
        if (-1 == xioctl(device->fd, VIDIOC_STREAMON, &type)) {
            close_video_device(vdt_input, temp_idx);
            unlock;
            return vde_FailedStart;
        }

#endif




// -- Zoxcore --
#ifdef X11VIDEO

        /* Create X11 window associated to device */
        if ((device->x_display = XOpenDisplay(NULL)) == NULL) {
            close_video_device(vdt_input, temp_idx);
            unlock;
            return vde_FailedStart;
        }

        int screen = DefaultScreen(device->x_display);

        if (!(device->x_window = XCreateSimpleWindow(device->x_display, RootWindow(device->x_display, screen), 0, 0,
                                 device->video_width, device->video_height, 0, BlackPixel(device->x_display, screen),
                                 BlackPixel(device->x_display, screen)))) {
            close_video_device(vdt_input, temp_idx);
            unlock;
            return vde_FailedStart;
        }

        XStoreName(device->x_display, device->x_window, "Video Preview");
        XSelectInput(device->x_display, device->x_window, ExposureMask | ButtonPressMask | KeyPressMask);

        if ((device->x_gc = DefaultGC(device->x_display, screen)) == NULL) {
            close_video_device(vdt_input, temp_idx);
            unlock;
            return vde_FailedStart;
        }

        /* Disable user from manually closing the X11 window */
        Atom wm_delete_window = XInternAtom(device->x_display, "WM_DELETE_WINDOW", false);
        XSetWMProtocols(device->x_display, device->x_window, &wm_delete_window, 1);

        XMapWindow(device->x_display, device->x_window);
        XClearWindow(device->x_display, device->x_window);
        XMapRaised(device->x_display, device->x_window);
        XFlush(device->x_display);

        vpx_img_alloc(&device->input, VPX_IMG_FMT_I420, device->video_width, device->video_height, 1);

#endif
// -- Zoxcore --

        video_thread_paused = false;
    } else { /* vdt_output */




// -- Zoxcore --
#ifdef X11VIDEO

        /* Create X11 window associated to device */
        if ((device->x_display = XOpenDisplay(NULL)) == NULL) {
            close_video_device(vdt_output, temp_idx);
            unlock;
            return vde_FailedStart;
        }

        int screen = DefaultScreen(device->x_display);

        if (!(device->x_window = XCreateSimpleWindow(device->x_display, RootWindow(device->x_display, screen), 0, 0,
                                 100, 100, 0, BlackPixel(device->x_display, screen), BlackPixel(device->x_display, screen)))) {
            close_video_device(vdt_output, temp_idx);
            unlock;
            return vde_FailedStart;
        }

        XStoreName(device->x_display, device->x_window, "Video Receive");
        XSelectInput(device->x_display, device->x_window, ExposureMask | ButtonPressMask | KeyPressMask);

        if ((device->x_gc = DefaultGC(device->x_display, screen)) == NULL) {
            close_video_device(vdt_output, temp_idx);
            unlock;
            return vde_FailedStart;
        }

        /* Disable user from manually closing the X11 window */
        Atom wm_delete_window = XInternAtom(device->x_display, "WM_DELETE_WINDOW", false);
        XSetWMProtocols(device->x_display, device->x_window, &wm_delete_window, 1);

        XMapWindow(device->x_display, device->x_window);
        XClearWindow(device->x_display, device->x_window);
        XMapRaised(device->x_display, device->x_window);
        XFlush(device->x_display);

        vpx_img_alloc(&device->input, VPX_IMG_FMT_I420, device->video_width, device->video_height, 1);

#else


        global_framebuffer_device_fd = 0;

        if ((global_framebuffer_device_fd = open("/dev/fb0", O_RDWR)) < 0)
        {
            dbg(0, "error opening Framebuffer device: %s\n", "/dev/fb0");
        }
        else
        {
            dbg(2, "The Framebuffer device opened: %d\n", (int)global_framebuffer_device_fd);
        }

        // Get variable screen information
        if (ioctl(global_framebuffer_device_fd, FBIOGET_VSCREENINFO, &var_framebuffer_info))
        {
            dbg(0, "Error reading Framebuffer info\n");
        }
        else
        {
        }

        dbg(2, "Framebuffer info %dx%d, %d bpp\n",  var_framebuffer_info.xres, var_framebuffer_info.yres,
            var_framebuffer_info.bits_per_pixel);

        // Get fixed screen information
        if (ioctl(global_framebuffer_device_fd, FBIOGET_FSCREENINFO, &var_framebuffer_fix_info))
        {
            dbg(0, "Error reading Framebuffer fixed information\n");
        }

        // map framebuffer to user memory
        framebuffer_screensize = (size_t)var_framebuffer_fix_info.smem_len;
        dbg(9, "framebuffer_screensize=%d\n", (int)framebuffer_screensize);
        framebuffer_mappedmem = NULL;
        framebuffer_mappedmem = (char *)mmap(NULL,
                                             (size_t)framebuffer_screensize,
                                             PROT_READ | PROT_WRITE,
                                             MAP_SHARED,
                                             global_framebuffer_device_fd, 0);

        if (framebuffer_mappedmem == NULL)
        {
            dbg(0, "Failed to mmap Framebuffer\n");
        }
        else
        {
            dbg(2, "mmap Framebuffer: %p\n", framebuffer_mappedmem);
        }
#endif
// -- Zoxcore --

    }

    *device_idx = temp_idx;
    unlock;

    return vde_None;
}

VideoDeviceError write_video_out(uint16_t width, uint16_t height,
                                 uint8_t const *y, uint8_t const *u, uint8_t const *v,
                                 int32_t ystride, int32_t ustride, int32_t vstride,
                                 void *user_data, uint32_t friendnumber)
{
    VideoDevice *device = video_devices_running[vdt_output][friendnumber];

    if (!device)
    {

        Call *this_call = &CallControl.calls[0];
        open_primary_video_device(vdt_output, &this_call->vout_idx);

        device = video_devices_running[vdt_output][0];
        if (!device)
        {
            return vde_DeviceNotActive;
        }
    }

// -- Zoxcore --
#ifdef X11VIDEO
    if (!device->x_window) {
        return vde_DeviceNotActive;
    }
#endif
// -- Zoxcore --

    pthread_mutex_lock(device->mutex);

// -- Zoxcore --
#ifdef X11VIDEO

    /* Resize X11 window to correct size */
    if (device->video_width != width || device->video_height != height) {
        device->video_width = width;
        device->video_height = height;
        XResizeWindow(device->x_display, device->x_window, width, height);

        vpx_img_free(&device->input);
        vpx_img_alloc(&device->input, VPX_IMG_FMT_I420, width, height, 1);
    }

    /* Convert YUV420 data to BGR */
    ystride = abs(ystride);
    ustride = abs(ustride);
    vstride = abs(vstride);
    uint8_t *img_data = malloc(width * height * 4);
    yuv420tobgr(width, height, y, u, v, ystride, ustride, vstride, img_data);

    /* Allocate image data in X11 */
    XImage image = {
        .width = width,
        .height = height,
        .depth = 24,
        .bits_per_pixel = 32,
        .format = ZPixmap,
        .byte_order = LSBFirst,
        .bitmap_unit = 8,
        .bitmap_bit_order = LSBFirst,
        .bytes_per_line = width * 4,
        .red_mask = 0xFF0000,
        .green_mask = 0xFF00,
        .blue_mask = 0xFF,
        .data = (char *)img_data
    };

    /* Render image data */
    Pixmap pixmap = XCreatePixmap(device->x_display, device->x_window, width, height, 24);
    XPutImage(device->x_display, pixmap, device->x_gc, &image, 0, 0, 0, 0, width, height);
    XCopyArea(device->x_display, pixmap, device->x_window, device->x_gc, 0, 0, width, height, 0, 0);
    XFreePixmap(device->x_display, pixmap);
    XFlush(device->x_display);
    free(img_data);

#else

/* ------ Framebuffer output ------ */
/* ------ Framebuffer output ------ */
/* ------ Framebuffer output ------ */

    // ----------------------
    // ----------------------
    // ----------------------
    int frame_width_px1 = width;
    int frame_height_px1 = height;
    int ystride_ = (int)ystride;
    int ustride_ = (int)ustride;
    int vstride_ = (int)vstride;
    int y_layer_size = (int) max(frame_width_px1, abs(ystride_)) * frame_height_px1;
    int u_layer_size = (int) max((frame_width_px1 / 2), abs(ustride_)) * (frame_height_px1 / 2);
    int v_layer_size = (int) max((frame_width_px1 / 2), abs(vstride_)) * (frame_height_px1 / 2);
    // ----------------------
    // ----------------------
    // ----------------------

    full_width = var_framebuffer_info.xres;
    full_height = var_framebuffer_info.yres;

    vid_width = full_width;
    vid_height = full_height;


    int downscale = 0;

    // check if we need to upscale or downscale
    if ((frame_width_px1 > (vid_width - 5)) || (frame_height_px1 > (vid_height - 5)))
    {
        // downscale to video size / or leave as is
        downscale = 1;
    }
    else
    {
        // upscale to video size
    }

    // dbg(9, "VP-DEBUG:010\n");
    int buffer_size_in_bytes = y_layer_size + v_layer_size + u_layer_size;
    // dbg(9, "frame_width_px1=%d frame_width_px=%d frame_height_px1=%d\n", (int)frame_width_px1, (int)frame_width_px, (int)frame_height_px1);
    int horizontal_stride_pixels = 0;
    int horizontal_stride_pixels_half = 0;

    if (full_width > frame_width_px1)
    {
        horizontal_stride_pixels = full_width - frame_width_px1;
        horizontal_stride_pixels_half = horizontal_stride_pixels / 2;
    }

    // dbg(9, "VP-DEBUG:011\n");
    uint8_t *bf_out_data = (uint8_t *)calloc(1, framebuffer_screensize);
    // dbg(9, "VP-DEBUG:012\n");
    long int i, j;
    // dbg(9, "full_width=%f vid_width=%f full_height=%f vid_height=%f\n", (float)full_width, (float)vid_width, (float)full_height, (float)vid_height);
    float ww = (float)var_framebuffer_info.xres / (float)vid_width;
    float hh = (float)var_framebuffer_info.yres / (float)vid_height;
    // dbg(9, "video frame scale factor: full_width/vid_width=%f full_height/vid_height=%f\n", ww, hh);
    int horizontal_stride_pixels_half_resized = 0;

    if (ww > 0)
    {
        horizontal_stride_pixels_half_resized = 0 + (int)((float)horizontal_stride_pixels_half / ww);
        // dbg(9, "horizontal_stride_pixels_half_resized=%d\n", (int)horizontal_stride_pixels_half_resized);
    }

    int i_src;
    int j_src;
    int yx;
    int ux;
    int vx;
    int vid_height_needed = vid_height;

    if (hh > 0)
    {
        vid_height_needed = 0 + (int)((float)frame_height_px1 / hh);

        if (vid_height_needed > vid_height)
        {
            vid_height_needed = vid_height;
        }
    }

    // dbg(9, "vid_height_needed=%d vid_height=%d\n", (int)vid_height_needed, (int)vid_height);
    // dbg(9, "VP-DEBUG:013\n");
    int vid_width_needed = vid_width;

    if (hh > 0)
    {
        vid_width_needed = 0 + (int)((float)frame_width_px1 / ww);

        if (vid_width_needed > vid_width)
        {
            vid_width_needed = vid_width;
        }
    }

    // dbg(9, "vid_width_needed=%d vid_width=%d\n", (int)vid_width_needed, (int)vid_width);

    if (downscale == 0)
        // if (((vid_height_needed + 10) < var_framebuffer_info.xres) && ((vid_height_needed + 10) < var_framebuffer_info.yres))
    {
        // dbg(9, "VP-DEBUG:014\n");
        // scale image up to output size -----------------------------
        // scale image up to output size -----------------------------
        // scale image up to output size -----------------------------
        // dbg(9, "scale image UP   ****\n");
        float ww2_upscale = (float)vid_width / (float)frame_width_px1;
        float hh2_upscale = (float)vid_height / (float)frame_height_px1;
        // dbg(9, "video frame scale factor2: ww=%f hh=%f\n", ww2_upscale, hh2_upscale);
        float factor2_upscale = hh2_upscale;

        if (ww2_upscale < hh2_upscale)
        {
            factor2_upscale = ww2_upscale;
        }

        // dbg(9, "factor2_upscale=%f\n", factor2_upscale);
        int scale_to_width_upscale = (int)((float)frame_width_px1 * factor2_upscale);
        int scale_to_height_upscale = (int)((float)frame_height_px1 * factor2_upscale);

        if (scale_to_width_upscale < 2)
        {
            scale_to_width_upscale = 2;
        }
        else if (scale_to_width_upscale > vid_width)
        {
            scale_to_width_upscale = vid_width;
        }

        if (scale_to_height_upscale < 2)
        {
            scale_to_height_upscale = 2;
        }
        else if (scale_to_height_upscale > vid_height)
        {
            scale_to_height_upscale = vid_height;
        }

        // dbg(9, "video frame scale to: ww=%d hh=%d\n", scale_to_width_upscale, scale_to_height_upscale);
        // convert to BGRA 1:1 size (from YUV)
        uint8_t *point = NULL;

        for (i = 0; i < frame_height_px1; i++)
        {
            i_src = i;

            for (j = 0; j < frame_width_px1; j++)
            {
                point = (uint8_t *) bf_out_data + 4 * ((i * (int)frame_width_px1) + j);
                j_src = j;
                yx = y[(i_src * abs(ystride)) + j_src];
                ux = u[((i_src / 2) * abs(ustride)) + (j_src / 2)];
                vx = v[((i_src / 2) * abs(vstride)) + (j_src / 2)];
                point[0] = YUV2B(yx, ux, vx); // B
                point[1] = YUV2G(yx, ux, vx); // G
                point[2] = YUV2R(yx, ux, vx); // R
                // point[3] = 0; // A
            }
        }

        uint8_t *bf_out_data_upscaled = (uint8_t *)calloc(1, framebuffer_screensize);
        memset(bf_out_data_upscaled, 0, framebuffer_screensize);
        // resize ---------------
        stbir_resize_uint8(bf_out_data, frame_width_px1, frame_height_px1, 0,
                           bf_out_data_upscaled, scale_to_width_upscale, scale_to_height_upscale, (int)var_framebuffer_fix_info.line_length, 4);
        // dbg(9, "upscale res=%d\n", res_upscale);
        // resize ---------------

        if (bf_out_data != NULL)
        {
            free(bf_out_data);
            bf_out_data = NULL;
        }

        if (bf_out_data_upscaled != NULL)
        {
            fb_copy_frame_to_fb(bf_out_data_upscaled);
            free(bf_out_data_upscaled);
            bf_out_data_upscaled = NULL;
        }

        // scale image up to output size -----------------------------
        // scale image up to output size -----------------------------
        // scale image up to output size -----------------------------
        // dbg(9, "VP-DEBUG:015\n");
    }
    else
    {
        // dbg(9, "VP-DEBUG:016\n");
        // scale image down to output size (or leave as is) ----------
        // scale image down to output size (or leave as is) ----------
        // scale image down to output size (or leave as is) ----------
        // dbg(9, "scale image DOWN ++++\n");
        memset(bf_out_data, 0, framebuffer_screensize);
        // dbg(9, "vid_width_needed=%d vid_height_needed=%d\n", (int)vid_width_needed, (int)vid_height_needed);
        float ww2_downscale = (float)vid_width / (float)frame_width_px1;
        float hh2_downscale = (float)vid_height / (float)frame_height_px1;
        // dbg(9, "video frame scale factor2: ww=%f hh=%f\n", ww2_downscale, hh2_downscale);
        float factor2_downscale = hh2_downscale;

        if (ww2_downscale < hh2_downscale)
        {
            factor2_downscale = ww2_downscale;
        }

        int scale_to_width_downscale = (int)((float)frame_width_px1 * factor2_downscale);
        int scale_to_height_downscale = (int)((float)frame_height_px1 * factor2_downscale);

        if (scale_to_width_downscale < 2)
        {
            scale_to_width_downscale = 2;
        }
        else if (scale_to_width_downscale > vid_width)
        {
            scale_to_width_downscale = vid_width;
        }

        if (scale_to_height_downscale < 2)
        {
            scale_to_height_downscale = 2;
        }
        else if (scale_to_height_downscale > vid_height)
        {
            scale_to_height_downscale = vid_height;
        }

        int offset_right_px = (int)(((float)vid_width - (float)scale_to_width_downscale) / 2.0f);
        int offset_down_px = (int)(((float)vid_height - (float)scale_to_height_downscale) / 2.0f);

        // downscale and convert to BGRA in 1 step
        for (i = 0; i < scale_to_height_downscale; ++i)
        {
            i_src = (int)((float)i / factor2_downscale);

            for (j = 0; j < scale_to_width_downscale; ++j)
            {
                uint8_t *point = (uint8_t *) bf_out_data + 4 * //  '4 *'  ->  to get it in bytes (because 4 bytes per pixel)
                                 (
                                     ((i + offset_down_px) * (int)var_framebuffer_fix_info.line_length / 4) + j + offset_right_px // in pixels
                                 );
                j_src = (int)((float)j / factor2_downscale);
                yx = y[(i_src * abs(ystride)) + j_src];
                ux = u[((i_src / 2) * abs(ustride)) + (j_src / 2)];
                vx = v[((i_src / 2) * abs(vstride)) + (j_src / 2)];
                point[0] = YUV2B(yx, ux, vx); // B
                point[1] = YUV2G(yx, ux, vx); // G
                point[2] = YUV2R(yx, ux, vx); // R
                // point[3] = 0; // A
            }
        }


        if (bf_out_data != NULL)
        {
            fb_copy_frame_to_fb(bf_out_data);
            free(bf_out_data);
            bf_out_data = NULL;
        }

        // scale image down to output size (or leave as is) ----------
        // scale image down to output size (or leave as is) ----------
        // scale image down to output size (or leave as is) ----------
    }



/* ------ Framebuffer output ------ */
/* ------ Framebuffer output ------ */
/* ------ Framebuffer output ------ */


#endif
// -- Zoxcore --

    pthread_mutex_unlock(device->mutex);
    return vde_None;
}

void *video_thread_poll(void *arg)  // TODO: maybe use thread for every input source
{
    /*
     * NOTE: We only need to poll input devices for data.
     */
    (void)arg;
    uint32_t i;

    while (1) {
        lock;

        if (!video_thread_running) {
            unlock;
            break;
        }

        unlock;

        if (video_thread_paused) {
            usleep(10000);    /* Wait for unpause. */
        } else {
            for (i = 0; i < size[vdt_input]; ++i) {
                lock;

                if (video_devices_running[vdt_input][i] != NULL) {
                    /* Obtain frame image data from device buffers */
                    VideoDevice *device = video_devices_running[vdt_input][i];
                    uint16_t video_width = device->video_width;
                    uint16_t video_height = device->video_height;
                    uint8_t *y = device->input.planes[0];
                    uint8_t *u = device->input.planes[1];
                    uint8_t *v = device->input.planes[2];

#if defined(__OSX__)

                    if (osx_video_read_device(y, u, v, &video_width, &video_height) != 0) {
                        unlock;
                        continue;
                    }

#else /* not __OSX__*/
                    struct v4l2_buffer buf;
                    memset(&(buf), 0, sizeof(buf));

                    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                    buf.memory = V4L2_MEMORY_MMAP;

                    if (-1 == ioctl(device->fd, VIDIOC_DQBUF, &buf)) {
                        unlock;
                        continue;
                    }

                    void *data = (void *)device->buffers[buf.index].start;

                    /* Convert frame image data to YUV420 for ToxAV */
                    yuv422to420(y, u, v, data, video_width, video_height);

#endif

                    /* Send frame data to friend through ToxAV */
                    if (device->cb) {
                        device->cb(video_width, video_height, y, u, v, device->cb_data);
                    }

// -- Zoxcore --
#ifdef X11VIDEO
                    /* Convert YUV420 data to BGR */
                    uint8_t *img_data = malloc(video_width * video_height * 4);
                    yuv420tobgr(video_width, video_height, y, u, v,
                                video_width, video_width / 2, video_width / 2, img_data);

                    /* Allocate image data in X11 */
                    XImage image = {
                        .width = video_width,
                        .height = video_height,
                        .depth = 24,
                        .bits_per_pixel = 32,
                        .format = ZPixmap,
                        .byte_order = LSBFirst,
                        .bitmap_unit = 8,
                        .bitmap_bit_order = LSBFirst,
                        .bytes_per_line = video_width * 4,
                        .red_mask = 0xFF0000,
                        .green_mask = 0xFF00,
                        .blue_mask = 0xFF,
                        .data = (char *)img_data
                    };

                    /* Render image data */
                    Pixmap pixmap = XCreatePixmap(device->x_display, device->x_window, video_width, video_height, 24);
                    XPutImage(device->x_display, pixmap, device->x_gc, &image, 0, 0, 0, 0, video_width, video_height);
                    XCopyArea(device->x_display, pixmap, device->x_window, device->x_gc, 0, 0, video_width, video_height, 0, 0);
                    XFreePixmap(device->x_display, pixmap);
                    XFlush(device->x_display);
                    free(img_data);

#endif
// -- Zoxcore --

#if !defined(__OSX__)

                    if (-1 == xioctl(device->fd, VIDIOC_QBUF, &buf)) {
                        unlock;
                        continue;
                    }

#endif

                }

                unlock;
            }

            usleep(1000 * 1000 / 24);
        }
    }

    pthread_exit(NULL);
}

VideoDeviceError close_video_device(VideoDeviceType type, uint32_t device_idx)
{
    if (device_idx >= MAX_DEVICES) {
        return vde_InvalidSelection;
    }

    lock;
    VideoDevice *device = video_devices_running[type][device_idx];
    VideoDeviceError rc = vde_None;

    if (!device) {
        unlock;
        return vde_DeviceNotActive;
    }

    video_devices_running[type][device_idx] = NULL;

    if (!device->ref_count) {

        if (type == vdt_input) {
#if defined(__OSX__)

            osx_video_close_device(device_idx);
#else /* not __OSX__ */
            enum v4l2_buf_type buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

            if (-1 == xioctl(device->fd, VIDIOC_STREAMOFF, &buf_type)) {}

            int i;

            for (i = 0; i < device->n_buffers; ++i) {
                if (-1 == munmap(device->buffers[i].start, device->buffers[i].length)) {
                }
            }

            close(device->fd);

#endif
// -- Zoxcore --
#ifdef X11VIDEO
            vpx_img_free(&device->input);
            XDestroyWindow(device->x_display, device->x_window);
            XFlush(device->x_display);
            XCloseDisplay(device->x_display);

#endif
// -- Zoxcore --

            pthread_mutex_destroy(device->mutex);

#if !defined(__OSX__)
            free(device->buffers);
#endif /* not __OSX__ */

            free(device);
        } else {

// -- Zoxcore --
#ifdef X11VIDEO
            vpx_img_free(&device->input);
            XDestroyWindow(device->x_display, device->x_window);
            XFlush(device->x_display);
            XCloseDisplay(device->x_display);
#else

            if (framebuffer_mappedmem != NULL)
            {
                int res = munmap(framebuffer_mappedmem, (size_t)framebuffer_screensize);
                framebuffer_mappedmem = NULL;
            }

            if (global_framebuffer_device_fd > 0)
            {
                close(global_framebuffer_device_fd);
                global_framebuffer_device_fd = 0;
            }

#endif
// -- Zoxcore --
            pthread_mutex_destroy(device->mutex);
            free(device);
        }

    } else {
        device->ref_count--;
    }

    unlock;
    return rc;
}

void print_video_devices(ToxWindow *self, VideoDeviceType type)
{
    int i;

    for (i = 0; i < size[type]; ++i) {
        line_info_add(self, NULL, NULL, NULL, SYS_MSG, 0, 0, "%d: %s", i, video_devices_names[type][i]);
    }

    return;
}

VideoDeviceError video_selection_valid(VideoDeviceType type, int32_t selection)
{
    return (size[type] <= selection || selection < 0) ? vde_InvalidSelection : vde_None;
}

#endif /* VIDEO */
