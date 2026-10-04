// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <gst/base/gstpushsrc.h>

#ifndef PACKAGE
#define PACKAGE "surface-camera"
#endif

typedef struct {
	GstPushSrc parent;
	gboolean front;
	gchar *capture_device;
	gchar *sensor_device;
	GPid child;
	GstPoll *poll;
	GstPollFD output;
} GstSurfaceIsp;

typedef struct {
	GstPushSrcClass parent;
} GstSurfaceIspClass;

#define GST_TYPE_SURFACE_ISP (gst_surface_isp_get_type())
#define GST_SURFACE_ISP(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_SURFACE_ISP, GstSurfaceIsp))

G_DEFINE_TYPE(GstSurfaceIsp, gst_surface_isp, GST_TYPE_PUSH_SRC)

GST_DEBUG_CATEGORY_STATIC(surface_isp_debug);
#define GST_CAT_DEFAULT surface_isp_debug

enum {
	PROP_0,
	PROP_FRONT,
	PROP_CAPTURE_DEVICE,
	PROP_SENSOR_DEVICE,
};

static GstStaticPadTemplate source_template = GST_STATIC_PAD_TEMPLATE(
	"src", GST_PAD_SRC, GST_PAD_ALWAYS,
	GST_STATIC_CAPS("video/x-raw, format=YUY2, width=1296, height=972, "
		       "framerate=30/1; "
		       "video/x-raw, format=YUY2, width=1632, height=1224, "
		       "framerate=30/1"));

static GstCaps *surface_isp_get_caps(GstBaseSrc *base, GstCaps *filter)
{
	GstSurfaceIsp *self = GST_SURFACE_ISP(base);
	GstCaps *caps = gst_caps_new_simple("video/x-raw",
		"format", G_TYPE_STRING, "YUY2",
		"width", G_TYPE_INT, self->front ? 1296 : 1632,
		"height", G_TYPE_INT, self->front ? 972 : 1224,
		"framerate", GST_TYPE_FRACTION, 30, 1,
		"pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
		"interlace-mode", G_TYPE_STRING, "progressive",
		"colorimetry", G_TYPE_STRING, "bt601", NULL);

	if (filter) {
		GstCaps *intersection = gst_caps_intersect_full(
			filter, caps, GST_CAPS_INTERSECT_FIRST);

		gst_caps_unref(caps);
		return intersection;
	}
	return caps;
}

static gboolean surface_isp_stop(GstBaseSrc *base)
{
	GstSurfaceIsp *self = GST_SURFACE_ISP(base);
	gboolean success = TRUE;

	gst_poll_set_flushing(self->poll, TRUE);
	if (self->child && kill(self->child, SIGTERM) < 0 && errno != ESRCH) {
		GST_ELEMENT_ERROR(self, RESOURCE, CLOSE,
			("Cannot stop the Surface ISP"), ("%s", g_strerror(errno)));
		success = FALSE;
	}
	if (self->output.fd >= 0) {
		gst_poll_remove_fd(self->poll, &self->output);
		close(self->output.fd);
		self->output.fd = -1;
	}
	if (self->child) {
		int status;
		pid_t result;

		do {
			result = waitpid(self->child, &status, 0);
		} while (result < 0 && errno == EINTR);
		if (result < 0) {
			GST_ELEMENT_ERROR(self, RESOURCE, CLOSE,
				("Cannot reap the Surface ISP"),
				("%s", g_strerror(errno)));
			success = FALSE;
		} else if (!(WIFEXITED(status) && WEXITSTATUS(status) == 0) &&
			   !(WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM)) {
			GST_ELEMENT_ERROR(self, RESOURCE, FAILED,
				("Surface ISP exited unsuccessfully"),
				("Child wait status: %d", status));
			success = FALSE;
		}
		g_spawn_close_pid(self->child);
		self->child = 0;
	}
	return success;
}

static gboolean surface_isp_start(GstBaseSrc *base)
{
	GstSurfaceIsp *self = GST_SURFACE_ISP(base);
	GError *error = NULL;
	gchar *argv[] = {
		self->front ? "/usr/local/libexec/ipu4-softisp-surface-front" :
			      "/usr/local/libexec/ipu4-softisp-surface-rear",
		self->capture_device, "-", self->sensor_device, NULL,
	};
	int flags;

	if (!self->capture_device || !self->sensor_device) {
		GST_ELEMENT_ERROR(self, RESOURCE, SETTINGS,
			("Capture and sensor device paths are required"), (NULL));
		return FALSE;
	}
	if (!g_spawn_async_with_pipes(NULL, argv, NULL,
				    G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL,
				    &self->child, NULL, &self->output.fd,
				    NULL, &error)) {
		GST_ELEMENT_ERROR(self, RESOURCE, OPEN_READ,
			("Cannot start the Surface ISP"), ("%s", error->message));
		g_clear_error(&error);
		return FALSE;
	}
	flags = fcntl(self->output.fd, F_GETFL);
	if (flags < 0 || fcntl(self->output.fd, F_SETFL, flags | O_NONBLOCK) < 0 ||
	    !gst_poll_add_fd(self->poll, &self->output) ||
	    !gst_poll_fd_ctl_read(self->poll, &self->output, TRUE)) {
		GST_ELEMENT_ERROR(self, RESOURCE, OPEN_READ,
			("Cannot prepare the ISP frame pipe"),
			("%s", g_strerror(errno)));
		surface_isp_stop(base);
		return FALSE;
	}
	gst_poll_set_flushing(self->poll, FALSE);
	return TRUE;
}

static GstFlowReturn surface_isp_create(GstPushSrc *source, GstBuffer **output)
{
	GstSurfaceIsp *self = GST_SURFACE_ISP(source);
	gsize size = self->front ? 1296 * 972 * 2 : 1632 * 1224 * 2;
	GstBuffer *buffer = gst_buffer_new_allocate(NULL, size, NULL);
	GstMapInfo map;
	gsize offset = 0;
	GstFlowReturn result = GST_FLOW_ERROR;

	if (!buffer || !gst_buffer_map(buffer, &map, GST_MAP_WRITE)) {
		if (buffer)
			gst_buffer_unref(buffer);
		GST_ELEMENT_ERROR(self, RESOURCE, NO_SPACE_LEFT,
			("Cannot allocate an ISP frame"), (NULL));
		return GST_FLOW_ERROR;
	}
	while (offset < size) {
		ssize_t count;

		if (gst_poll_wait(self->poll, GST_CLOCK_TIME_NONE) < 0) {
			if (errno == EINTR)
				continue;
			if (errno == EBUSY) {
				result = GST_FLOW_FLUSHING;
				goto done;
			}
			GST_ELEMENT_ERROR(self, RESOURCE, READ,
				("Cannot wait for an ISP frame"),
				("%s", g_strerror(errno)));
			goto done;
		}
		count = read(self->output.fd, map.data + offset, size - offset);
		if (count < 0 && (errno == EINTR || errno == EAGAIN))
			continue;
		if (count <= 0) {
			GST_ELEMENT_ERROR(self, RESOURCE, READ,
				("Surface ISP stopped before completing a frame"),
				("Read %zd after %" G_GSIZE_FORMAT " bytes", count, offset));
			goto done;
		}
		offset += count;
	}
	result = GST_FLOW_OK;
done:
	gst_buffer_unmap(buffer, &map);
	if (result == GST_FLOW_OK) {
		GST_BUFFER_DURATION(buffer) = GST_SECOND / 30;
		*output = buffer;
	} else {
		gst_buffer_unref(buffer);
	}
	return result;
}

static gboolean surface_isp_unlock(GstBaseSrc *base)
{
	gst_poll_set_flushing(GST_SURFACE_ISP(base)->poll, TRUE);
	return TRUE;
}

static gboolean surface_isp_unlock_stop(GstBaseSrc *base)
{
	gst_poll_set_flushing(GST_SURFACE_ISP(base)->poll, FALSE);
	return TRUE;
}

static void surface_isp_set_property(GObject *object, guint id,
				    const GValue *value, GParamSpec *spec)
{
	GstSurfaceIsp *self = GST_SURFACE_ISP(object);

	switch (id) {
	case PROP_FRONT:
		self->front = g_value_get_boolean(value);
		break;
	case PROP_CAPTURE_DEVICE:
		g_free(self->capture_device);
		self->capture_device = g_value_dup_string(value);
		break;
	case PROP_SENSOR_DEVICE:
		g_free(self->sensor_device);
		self->sensor_device = g_value_dup_string(value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, id, spec);
	}
}

static void surface_isp_get_property(GObject *object, guint id,
				    GValue *value, GParamSpec *spec)
{
	GstSurfaceIsp *self = GST_SURFACE_ISP(object);

	switch (id) {
	case PROP_FRONT:
		g_value_set_boolean(value, self->front);
		break;
	case PROP_CAPTURE_DEVICE:
		g_value_set_string(value, self->capture_device);
		break;
	case PROP_SENSOR_DEVICE:
		g_value_set_string(value, self->sensor_device);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, id, spec);
	}
}

static void surface_isp_finalize(GObject *object)
{
	GstSurfaceIsp *self = GST_SURFACE_ISP(object);

	if (self->child)
		surface_isp_stop(GST_BASE_SRC(self));
	gst_poll_free(self->poll);
	g_free(self->capture_device);
	g_free(self->sensor_device);
	G_OBJECT_CLASS(gst_surface_isp_parent_class)->finalize(object);
}

static void gst_surface_isp_class_init(GstSurfaceIspClass *klass)
{
	GObjectClass *object = G_OBJECT_CLASS(klass);
	GstElementClass *element = GST_ELEMENT_CLASS(klass);
	GstBaseSrcClass *base = GST_BASE_SRC_CLASS(klass);
	GParamFlags flags = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
			    GST_PARAM_MUTABLE_READY;

	object->set_property = surface_isp_set_property;
	object->get_property = surface_isp_get_property;
	object->finalize = surface_isp_finalize;
	g_object_class_install_property(object, PROP_FRONT,
		g_param_spec_boolean("front", "Front", "Use the front camera geometry",
				     FALSE, flags));
	g_object_class_install_property(object, PROP_CAPTURE_DEVICE,
		g_param_spec_string("capture-device", "Capture device",
				    "Packed RAW10 capture device", NULL, flags));
	g_object_class_install_property(object, PROP_SENSOR_DEVICE,
		g_param_spec_string("sensor-device", "Sensor device",
				    "Sensor control subdevice", NULL, flags));
	gst_element_class_set_static_metadata(element,
		"Surface software ISP source", "Source/Video",
		"Capture RAW10 only while the input pipeline is active",
		"Surface camera contributors");
	gst_element_class_add_static_pad_template(element, &source_template);
	base->get_caps = surface_isp_get_caps;
	base->start = surface_isp_start;
	base->stop = surface_isp_stop;
	base->unlock = surface_isp_unlock;
	base->unlock_stop = surface_isp_unlock_stop;
	GST_PUSH_SRC_CLASS(klass)->create = surface_isp_create;
}

static void gst_surface_isp_init(GstSurfaceIsp *self)
{
	self->poll = gst_poll_new(TRUE);
	gst_poll_fd_init(&self->output);
	gst_base_src_set_live(GST_BASE_SRC(self), TRUE);
	gst_base_src_set_format(GST_BASE_SRC(self), GST_FORMAT_TIME);
	gst_base_src_set_do_timestamp(GST_BASE_SRC(self), TRUE);
}

static gboolean surface_isp_plugin_init(GstPlugin *plugin)
{
	GST_DEBUG_CATEGORY_INIT(surface_isp_debug, "surfaceisp", 0, "Surface ISP");
	return gst_element_register(plugin, "surfaceisp", GST_RANK_NONE,
				    GST_TYPE_SURFACE_ISP);
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, surfaceisp,
	"Surface RAW10 ISP source", surface_isp_plugin_init, "1.0", "GPL",
	"surface-camera", "local")
