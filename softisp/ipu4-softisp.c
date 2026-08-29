// SPDX-License-Identifier: GPL-2.0
/*
 * Software ISP relay for the XPS 13 7390 2-in-1 front camera.
 *
 * Reads SGRBG10P (packed RAW10 Bayer) 1280x800 frames from the IPU4P
 * capture node, produces 640x400 YUYV frames by 2x2 demosaic, applies
 * gray-world white balance and a simple auto-exposure loop (sensor
 * analogue gain), and writes the result to a v4l2loopback device.
 *
 * Usage: ipu4-softisp <raw-capture-dev> <loopback-dev>
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <math.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#define IN_W 1280
#define IN_H 800
#define IN_STRIDE 1600		/* 1280 * 10 / 8 */
#define OUT_W (IN_W / 2)
#define OUT_H (IN_H / 2)
#define N_BUFS 4
#define BAD_FRAME_LIMIT 12

#define AE_TARGET 340.0		/* target mean luma, 10-bit scale */
#define AE_TOL 0.15		/* relative dead zone */
#define GAIN_MIN 256		/* sensor analogue gain range */
#define GAIN_MAX 8192
#define EXP_MIN 4		/* sensor exposure range, lines */
#define EXP_MAX 888

static int xioctl(int fd, unsigned long req, void *arg)
{
	int r;

	do {
		r = ioctl(fd, req, arg);
	} while (r == -1 && errno == EINTR);
	return r;
}

/* find the sensor subdev that exposes analogue gain */
static int open_sensor_subdev(void)
{
	char path[64];
	int i, fd;

	for (i = 0; i < 32; i++) {
		struct v4l2_queryctrl qc = { .id = V4L2_CID_ANALOGUE_GAIN };

		snprintf(path, sizeof(path), "/dev/v4l-subdev%d", i);
		fd = open(path, O_RDWR);
		if (fd < 0)
			continue;
		if (xioctl(fd, VIDIOC_QUERYCTRL, &qc) == 0)
			return fd;
		close(fd);
	}
	return -1;
}

static int set_ctrl(int fd, unsigned int id, int value)
{
	struct v4l2_control c = { .id = id, .value = value };

	return xioctl(fd, VIDIOC_S_CTRL, &c);
}

struct buf {
	void *start;
	size_t length;
};

int main(int argc, char **argv)
{
	const char *in_name = argc > 1 ? argv[1] : "/dev/video-ipu4-raw";
	const char *out_name = argc > 2 ? argv[2] : "/dev/video-ipu4";
	static uint16_t bayer[IN_H][IN_W];
	static uint8_t yuyv[OUT_H][OUT_W * 2];
	struct buf bufs[N_BUFS];
	double wb_r = 1.0, wb_b = 1.0;
	int in_fd, out_fd, sd_fd;
	int gain = 1024, expo = EXP_MAX;
	unsigned int bad_frames = 0;
	unsigned int i;

	in_fd = open(in_name, O_RDWR);
	if (in_fd < 0) {
		perror(in_name);
		return 1;
	}
	out_fd = open(out_name, O_WRONLY);
	if (out_fd < 0) {
		perror(out_name);
		return 1;
	}
	sd_fd = open_sensor_subdev();
	if (sd_fd >= 0) {
		set_ctrl(sd_fd, V4L2_CID_ANALOGUE_GAIN, gain);
		set_ctrl(sd_fd, V4L2_CID_EXPOSURE, expo);
	}

	/* input format */
	{
		struct v4l2_format f = {
			.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
		};

		f.fmt.pix.width = IN_W;
		f.fmt.pix.height = IN_H;
		f.fmt.pix.pixelformat = v4l2_fourcc('p', 'B', 'A', 'A');
		f.fmt.pix.field = V4L2_FIELD_NONE;
		if (xioctl(in_fd, VIDIOC_S_FMT, &f) < 0) {
			perror("input S_FMT");
			return 1;
		}
	}

	/* output format on the loopback device */
	{
		struct v4l2_format f = {
			.type = V4L2_BUF_TYPE_VIDEO_OUTPUT,
		};

		f.fmt.pix.width = OUT_W;
		f.fmt.pix.height = OUT_H;
		f.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
		f.fmt.pix.field = V4L2_FIELD_NONE;
		f.fmt.pix.bytesperline = OUT_W * 2;
		f.fmt.pix.sizeimage = OUT_W * 2 * OUT_H;
		if (xioctl(out_fd, VIDIOC_S_FMT, &f) < 0) {
			perror("output S_FMT");
			return 1;
		}
	}

	/* request and map input buffers */
	{
		struct v4l2_requestbuffers rb = {
			.count = N_BUFS,
			.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
			.memory = V4L2_MEMORY_MMAP,
		};

		if (xioctl(in_fd, VIDIOC_REQBUFS, &rb) < 0) {
			perror("REQBUFS");
			return 1;
		}
		for (i = 0; i < rb.count; i++) {
			struct v4l2_buffer b = {
				.index = i,
				.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
				.memory = V4L2_MEMORY_MMAP,
			};

			if (xioctl(in_fd, VIDIOC_QUERYBUF, &b) < 0) {
				perror("QUERYBUF");
				return 1;
			}
			bufs[i].length = b.length;
			bufs[i].start = mmap(NULL, b.length,
					     PROT_READ | PROT_WRITE,
					     MAP_SHARED, in_fd, b.m.offset);
			if (bufs[i].start == MAP_FAILED) {
				perror("mmap");
				return 1;
			}
			if (xioctl(in_fd, VIDIOC_QBUF, &b) < 0) {
				perror("QBUF");
				return 1;
			}
		}
	}

	{
		int t = V4L2_BUF_TYPE_VIDEO_CAPTURE;

		if (xioctl(in_fd, VIDIOC_STREAMON, &t) < 0) {
			perror("STREAMON");
			return 1;
		}
	}

	fprintf(stderr, "ipu4-softisp: %s -> %s, %dx%d YUYV\n",
		in_name, out_name, OUT_W, OUT_H);

	for (unsigned long frame = 0;; frame++) {
		struct v4l2_buffer b = {
			.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
			.memory = V4L2_MEMORY_MMAP,
		};
		struct pollfd pfd = { .fd = in_fd, .events = POLLIN };
		const uint8_t *raw;
		double sum_r = 0, sum_g = 0, sum_b = 0;
		double wb_sum_r = 0, wb_sum_g = 0, wb_sum_b = 0;
		unsigned int x, y;

		if (poll(&pfd, 1, 2000) <= 0) {
			fprintf(stderr, "ipu4-softisp: poll timeout\n");
			return 1;
		}
		if (xioctl(in_fd, VIDIOC_DQBUF, &b) < 0) {
			perror("DQBUF");
			return 1;
		}
		raw = bufs[b.index].start;

		/*
		 * A failed CSI session returns a short valid prefix followed by
		 * 0xff padding while still reporting a full buffer. Do not send
		 * those white frames to applications. Restarting the stream fixes
		 * the session; persistent teardown failures make the driver power
		 * cycle ISYS.
		 */
		{
			unsigned int ff = 0, samples = 0;
			size_t off;

			for (off = 0; off < (size_t)IN_STRIDE * IN_H; off += 32) {
				ff += raw[off] == 0xff;
				samples++;
			}
			if (ff * 100 > samples * 95) {
				bad_frames++;
				if (xioctl(in_fd, VIDIOC_QBUF, &b) < 0) {
					perror("re-QBUF invalid frame");
					return 1;
				}
				if (bad_frames >= BAD_FRAME_LIMIT) {
					fprintf(stderr,
						"ipu4-softisp: invalid padded frames; restarting stream\n");
					return 1;
				}
				continue;
			}
			bad_frames = 0;
		}

		/* unpack RAW10: 5 bytes -> 4 pixels */
		for (y = 0; y < IN_H; y++) {
			const uint8_t *l = raw + (size_t)y * IN_STRIDE;
			uint16_t *o = bayer[y];

			for (x = 0; x < IN_W; x += 4, l += 5) {
				o[x + 0] = (l[0] << 2) | (l[4] & 3);
				o[x + 1] = (l[1] << 2) | ((l[4] >> 2) & 3);
				o[x + 2] = (l[2] << 2) | ((l[4] >> 4) & 3);
				o[x + 3] = (l[3] << 2) | ((l[4] >> 6) & 3);
			}
		}

		/*
		 * SBGGR quad (pBAA / SBGGR10P):
		 *   row 2y:   B  Gb
		 *   row 2y+1: Gr R
		 * One output pixel per quad.
		 */
		for (y = 0; y < OUT_H; y++) {
			const uint16_t *e = bayer[2 * y];
			const uint16_t *o = bayer[2 * y + 1];
			uint8_t *dst = yuyv[y];

			for (x = 0; x < OUT_W; x += 2) {
				double r0, g0, b0, r1, g1, b1;
				double yy0, yy1, u, v;
				unsigned int c0 = 4 * (x / 2);
				unsigned int c2 = c0 + 2;

				b0 = e[c0];
				g0 = (e[c0 + 1] + o[c0]) / 2.0;
				r0 = o[c0 + 1];
				b1 = e[c2];
				g1 = (e[c2 + 1] + o[c2]) / 2.0;
				r1 = o[c2 + 1];

				sum_r += r0 + r1;
				sum_g += g0 + g1;
				sum_b += b0 + b1;
				if (r0 < 1000 && g0 < 1000 && b0 < 1000) {
					wb_sum_r += r0;
					wb_sum_g += g0;
					wb_sum_b += b0;
				}

				r0 *= wb_r; b0 *= wb_b;
				r1 *= wb_r; b1 *= wb_b;
				if (r0 > 1023) r0 = 1023;
				if (b0 > 1023) b0 = 1023;
				if (r1 > 1023) r1 = 1023;
				if (b1 > 1023) b1 = 1023;

				/* BT.601, 10-bit input -> 8-bit */
				yy0 = (0.257 * r0 + 0.504 * g0 + 0.098 * b0) / 4.0 + 16;
				yy1 = (0.257 * r1 + 0.504 * g1 + 0.098 * b1) / 4.0 + 16;
				u = (-0.148 * r0 - 0.291 * g0 + 0.439 * b0) / 4.0 + 128;
				v = (0.439 * r0 - 0.368 * g0 - 0.071 * b0) / 4.0 + 128;
				if (yy0 > 235) yy0 = 235;
				if (yy1 > 235) yy1 = 235;
				if (u < 16) u = 16; else if (u > 240) u = 240;
				if (v < 16) v = 16; else if (v > 240) v = 240;

				dst[2 * x + 0] = (uint8_t)yy0;
				dst[2 * x + 1] = (uint8_t)u;
				dst[2 * x + 2] = (uint8_t)yy1;
				dst[2 * x + 3] = (uint8_t)v;
			}
		}

		if (xioctl(in_fd, VIDIOC_QBUF, &b) < 0) {
			perror("re-QBUF");
			return 1;
		}

		if (write(out_fd, yuyv, sizeof(yuyv)) < 0) {
			perror("loopback write");
			return 1;
		}

		/* gray-world white balance on unclipped pixels, smoothed */
		if (wb_sum_r > 1000 && wb_sum_b > 1000) {
			double tr = wb_sum_g / wb_sum_r;
			double tb = wb_sum_g / wb_sum_b;

			if (tr > 4.0) tr = 4.0;
			if (tb > 4.0) tb = 4.0;
			if (tr < 0.25) tr = 0.25;
			if (tb < 0.25) tb = 0.25;
			wb_r += 0.1 * (tr - wb_r);
			wb_b += 0.1 * (tb - wb_b);
		}

		if (getenv("SOFTISP_DEBUG") && frame % 30 == 0) {
			double npix = (double)OUT_W * OUT_H;

			fprintf(stderr, "luma %.0f exp %d gain %d\n",
				(0.30 * sum_r + 0.55 * sum_g + 0.15 * sum_b) /
				npix, expo, gain);
		}

		/*
		 * Auto exposure, every 6 frames. Exposure lines first (best
		 * SNR), analogue gain when exposure hits its limits.
		 */
		if (sd_fd >= 0 && frame % 6 == 0 && !getenv("SOFTISP_NO_AE")) {
			double npix = (double)OUT_W * OUT_H;
			double luma = (0.30 * sum_r + 0.55 * sum_g +
				       0.15 * sum_b) / npix;
			double err = luma > 1.0 ? AE_TARGET / luma : 4.0;

			if (err > 1.0 + AE_TOL || err < 1.0 - AE_TOL) {
				double step = pow(err, 0.5);
				double want = (double)expo * gain * step;
				int ne, ng;

				/* prefer low gain */
				ne = (int)(want / GAIN_MIN);
				ng = GAIN_MIN;
				if (ne > EXP_MAX) {
					ng = (int)(want / EXP_MAX);
					ne = EXP_MAX;
				}
				if (ne < EXP_MIN)
					ne = EXP_MIN;
				if (ng < GAIN_MIN)
					ng = GAIN_MIN;
				if (ng > GAIN_MAX)
					ng = GAIN_MAX;
				if (ne != expo) {
					expo = ne;
					if (set_ctrl(sd_fd, V4L2_CID_EXPOSURE,
						     expo) < 0)
						fprintf(stderr,
							"set exposure %d: %s\n",
							expo, strerror(errno));
				}
				if (ng != gain) {
					gain = ng;
					if (set_ctrl(sd_fd,
						     V4L2_CID_ANALOGUE_GAIN,
						     gain) < 0)
						fprintf(stderr,
							"set gain %d: %s\n",
							gain, strerror(errno));
				}
			}
		}
	}
}
