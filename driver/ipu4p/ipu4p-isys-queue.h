/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2013--2024 Intel Corporation */

#ifndef IPU4P_ISYS_QUEUE_H
#define IPU4P_ISYS_QUEUE_H

#include <linux/container_of.h>
#include <linux/atomic.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/spinlock_types.h>

#include <media/videobuf2-v4l2.h>

#include "ipu4p-fw-isys.h"
#include "ipu4p-isys-video.h"

struct ipu4p_isys_stream;

struct ipu4p_isys_queue {
	struct vb2_queue vbq;
	struct list_head node;
	spinlock_t lock; /* Protects active and incoming lists */
	struct list_head active;
	struct list_head incoming;
	unsigned int fw_output;
};

struct ipu4p_isys_buffer {
	struct list_head head;
	atomic_t str2mmio_flag;
};

struct ipu4p_isys_video_buffer {
	struct vb2_v4l2_buffer vb_v4l2;
	struct ipu4p_isys_buffer ib;
	dma_addr_t dma_addr;
};

#define IPU4P_ISYS_BUFFER_LIST_FL_INCOMING	BIT(0)
#define IPU4P_ISYS_BUFFER_LIST_FL_ACTIVE	BIT(1)
#define IPU4P_ISYS_BUFFER_LIST_FL_SET_STATE	BIT(2)

struct ipu4p_isys_buffer_list {
	struct list_head head;
	unsigned int nbufs;
};

#define vb2_queue_to_isys_queue(__vb2) \
	container_of(__vb2, struct ipu4p_isys_queue, vbq)

#define ipu4p_isys_to_isys_video_buffer(__ib) \
	container_of(__ib, struct ipu4p_isys_video_buffer, ib)

#define vb2_buffer_to_ipu4p_isys_video_buffer(__vvb) \
	container_of(__vvb, struct ipu4p_isys_video_buffer, vb_v4l2)

#define ipu4p_isys_buffer_to_vb2_buffer(__ib) \
	(&ipu4p_isys_to_isys_video_buffer(__ib)->vb_v4l2.vb2_buf)

void ipu4p_isys_buffer_list_queue(struct ipu4p_isys_buffer_list *bl,
				 unsigned long op_flags,
				 enum vb2_buffer_state state);
void
ipu4p_isys_buf_to_fw_frame_buf(struct ipu4p_fw_isys_frame_buff_set_abi *set,
			      struct ipu4p_isys_stream *stream,
			      struct ipu4p_isys_buffer_list *bl);
void ipu4p_isys_queue_buf_ready(struct ipu4p_isys_stream *stream,
			       struct ipu4p_fw_isys_resp_info_abi *info);
int ipu4p_isys_queue_init(struct ipu4p_isys_queue *aq);
#endif /* IPU4P_ISYS_QUEUE_H */
