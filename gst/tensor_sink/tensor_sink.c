/*
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2018 nnstreamer <nnstreamer sec>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-tensor_sink
 *
 * Sink element for tensor stream.
 *
 * @file	tensor_sink.c
 * @date	15 June 2018
 * @brief	GStreamer plugin to handle tensor stream
 * @see		http://github.com/TO-BE-DETERMINED-SOON
 * @see		https://github.sec.samsung.net/STAR/nnstreamer
 * @author	Jaeyun Jung <jy1210.jung@samsung.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "tensor_sink.h"

/**
 * @brief Macro for debug mode.
 */
#define DBG (!_tensor_sink_get_silent (self))

/**
 * @brief Macro for debug message.
 */
#define DLOG(...) \
    debug_print (DBG, __VA_ARGS__)

GST_DEBUG_CATEGORY_STATIC (gst_tensor_sink_debug);
#define GST_CAT_DEFAULT gst_tensor_sink_debug

/* signals and args */
enum
{
  SIGNAL_NEW_DATA,
  SIGNAL_STREAM_START,
  SIGNAL_EOS,
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_RENDER_RATE,
  PROP_EMIT_SIGNAL,
  PROP_SILENT
};

/**
 * @brief Flag to emit signals.
 */
#define DEFAULT_EMIT_SIGNAL TRUE

/**
 * @brief Buffers rendered per second.
 */
#define DEFAULT_RENDER_RATE 0

/**
 * @brief Flag to print minimized log.
 */
#define DEFAULT_SILENT TRUE

/**
 * @brief Flag to synchronize on the clock.
 *
 * See GstBaseSink:sync property for more details.
 */
#define DEFAULT_SYNC TRUE

/**
 * @brief Flag for qos event.
 *
 * See GstBaseSink:qos property for more details.
 */
#define DEFAULT_QOS TRUE

/**
 * @brief Max lateness to handle delayed buffer.
 *
 * Default 30ms.
 * See GstBaseSink:max-lateness property for more details.
 */
#define DEFAULT_LATENESS (30 * GST_MSECOND)

/**
 * @brief Template for sink pad.
 */
static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

/**
 * @brief Variable for signal ids.
 */
static guint _tensor_sink_signals[LAST_SIGNAL] = { 0 };

/* GObject method implementation */
static void gst_tensor_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_tensor_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_tensor_sink_dispose (GObject * object);
static void gst_tensor_sink_finalize (GObject * object);

/* GstBaseSink method implementation */
static gboolean gst_tensor_sink_start (GstBaseSink * sink);
static gboolean gst_tensor_sink_stop (GstBaseSink * sink);
static gboolean gst_tensor_sink_event (GstBaseSink * sink, GstEvent * event);
static gboolean gst_tensor_sink_query (GstBaseSink * sink, GstQuery * query);
static GstFlowReturn gst_tensor_sink_render (GstBaseSink * sink,
    GstBuffer * buffer);
static GstFlowReturn gst_tensor_sink_render_list (GstBaseSink * sink,
    GstBufferList * buffer_list);
static gboolean gst_tensor_sink_set_caps (GstBaseSink * sink, GstCaps * caps);
static GstCaps *gst_tensor_sink_get_caps (GstBaseSink * sink, GstCaps * filter);

/* internal functions */
static void _tensor_sink_render_buffer (GstTensorSink * self,
    GstBuffer * buffer);
static void _tensor_sink_set_last_render_time (GstTensorSink * self,
    GstClockTime now);
static GstClockTime _tensor_sink_get_last_render_time (GstTensorSink * self);
static void _tensor_sink_set_render_rate (GstTensorSink * self, guint64 rate);
static guint64 _tensor_sink_get_render_rate (GstTensorSink * self);
static void _tensor_sink_set_emit_signal (GstTensorSink * self, gboolean emit);
static gboolean _tensor_sink_get_emit_signal (GstTensorSink * self);
static void _tensor_sink_set_silent (GstTensorSink * self, gboolean silent);
static gboolean _tensor_sink_get_silent (GstTensorSink * self);

/* functions to initialize */
static void _tensor_sink_do_init (GType type);
static gboolean _tensor_sink_plugin_init (GstPlugin * plugin);

#define gst_tensor_sink_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstTensorSink, gst_tensor_sink, GST_TYPE_BASE_SINK,
    _tensor_sink_do_init (g_define_type_id));

/**
 * @brief Initialize tensor_sink class.
 */
static void
gst_tensor_sink_class_init (GstTensorSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstBaseSinkClass *bsink_class;

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);
  bsink_class = GST_BASE_SINK_CLASS (klass);

  /* GObject methods */
  gobject_class->set_property = gst_tensor_sink_set_property;
  gobject_class->get_property = gst_tensor_sink_get_property;
  gobject_class->dispose = gst_tensor_sink_dispose;
  gobject_class->finalize = gst_tensor_sink_finalize;

  /* properties */
  g_object_class_install_property (gobject_class, PROP_RENDER_RATE,
      g_param_spec_uint64 ("render-rate", "Render rate",
          "Buffers rendered per second (0 for unlimited, max 500)", 0, 500,
          DEFAULT_RENDER_RATE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_EMIT_SIGNAL,
      g_param_spec_boolean ("emit-signal", "Emit signal",
          "Emit signal for new data, eos", DEFAULT_EMIT_SIGNAL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SILENT,
      g_param_spec_boolean ("silent", "Silent", "Produce verbose output",
          DEFAULT_SILENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /* signals */
  _tensor_sink_signals[SIGNAL_NEW_DATA] =
      g_signal_new ("new-data", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstTensorSinkClass, new_data), NULL, NULL, NULL,
      G_TYPE_NONE, 1, GST_TYPE_BUFFER | G_SIGNAL_TYPE_STATIC_SCOPE);

  _tensor_sink_signals[SIGNAL_STREAM_START] =
      g_signal_new ("stream-start", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstTensorSinkClass, stream_start),
      NULL, NULL, NULL, G_TYPE_NONE, 0, G_TYPE_NONE);

  _tensor_sink_signals[SIGNAL_EOS] =
      g_signal_new ("eos", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstTensorSinkClass, eos), NULL, NULL, NULL,
      G_TYPE_NONE, 0, G_TYPE_NONE);

  gst_element_class_set_static_metadata (element_class,
      "Tensor_Sink",
      "Sink/Tensor",
      "Sink element to handle tensor stream", "nnstreamer <nnstreamer sec>");

  gst_element_class_add_static_pad_template (element_class, &sinktemplate);

  /* GstBaseSink methods */
  bsink_class->start = GST_DEBUG_FUNCPTR (gst_tensor_sink_start);
  bsink_class->stop = GST_DEBUG_FUNCPTR (gst_tensor_sink_stop);
  bsink_class->event = GST_DEBUG_FUNCPTR (gst_tensor_sink_event);
  bsink_class->query = GST_DEBUG_FUNCPTR (gst_tensor_sink_query);
  bsink_class->render = GST_DEBUG_FUNCPTR (gst_tensor_sink_render);
  bsink_class->render_list = GST_DEBUG_FUNCPTR (gst_tensor_sink_render_list);
  bsink_class->set_caps = GST_DEBUG_FUNCPTR (gst_tensor_sink_set_caps);
  bsink_class->get_caps = GST_DEBUG_FUNCPTR (gst_tensor_sink_get_caps);
}

/**
 * @brief Initialize tensor_sink element.
 */
static void
gst_tensor_sink_init (GstTensorSink * self)
{
  GstBaseSink *bsink;

  bsink = GST_BASE_SINK (self);

  g_mutex_init (&self->mutex);

  /* init properties */
  self->silent = DEFAULT_SILENT;
  self->emit_signal = DEFAULT_EMIT_SIGNAL;
  self->render_rate = DEFAULT_RENDER_RATE;
  self->last_render_time = GST_CLOCK_TIME_NONE;
  self->in_caps = NULL;

  /* enable qos event */
  gst_base_sink_set_sync (bsink, DEFAULT_SYNC);
  gst_base_sink_set_max_lateness (bsink, DEFAULT_LATENESS);
  gst_base_sink_set_qos_enabled (bsink, DEFAULT_QOS);
}

/**
 * @brief Setter for tensor_sink properties.
 *
 * GObject method implementation.
 */
static void
gst_tensor_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTensorSink *self;

  self = GST_TENSOR_SINK (object);

  switch (prop_id) {
    case PROP_RENDER_RATE:
      _tensor_sink_set_render_rate (self, g_value_get_uint64 (value));
      break;

    case PROP_EMIT_SIGNAL:
      _tensor_sink_set_emit_signal (self, g_value_get_boolean (value));
      break;

    case PROP_SILENT:
      _tensor_sink_set_silent (self, g_value_get_boolean (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/**
 * @brief Getter for tensor_sink properties.
 *
 * GObject method implementation.
 */
static void
gst_tensor_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstTensorSink *self;

  self = GST_TENSOR_SINK (object);

  switch (prop_id) {
    case PROP_RENDER_RATE:
      g_value_set_uint64 (value, _tensor_sink_get_render_rate (self));
      break;

    case PROP_EMIT_SIGNAL:
      g_value_set_boolean (value, _tensor_sink_get_emit_signal (self));
      break;

    case PROP_SILENT:
      g_value_set_boolean (value, _tensor_sink_get_silent (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/**
 * @brief Function to drop all references.
 *
 * GObject method implementation.
 */
static void
gst_tensor_sink_dispose (GObject * object)
{
  GstTensorSink *self;

  self = GST_TENSOR_SINK (object);

  g_mutex_lock (&self->mutex);
  gst_caps_replace (&self->in_caps, NULL);
  g_mutex_unlock (&self->mutex);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

/**
 * @brief Function to finalize instance.
 *
 * GObject method implementation.
 */
static void
gst_tensor_sink_finalize (GObject * object)
{
  GstTensorSink *self;

  self = GST_TENSOR_SINK (object);

  g_mutex_clear (&self->mutex);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 * @brief Start processing, called when state changed null to ready.
 *
 * GstBaseSink method implementation.
 */
static gboolean
gst_tensor_sink_start (GstBaseSink * sink)
{
  /* load and init resources */
  return TRUE;
}

/**
 * @brief Stop processing, called when state changed ready to null.
 *
 * GstBaseSink method implementation.
 */
static gboolean
gst_tensor_sink_stop (GstBaseSink * sink)
{
  /* free resources */
  return TRUE;
}

/**
 * @brief Handle events.
 *
 * GstBaseSink method implementation.
 */
static gboolean
gst_tensor_sink_event (GstBaseSink * sink, GstEvent * event)
{
  GstTensorSink *self;
  GstEventType type;

  self = GST_TENSOR_SINK (sink);
  type = GST_EVENT_TYPE (event);

  switch (type) {
    case GST_EVENT_STREAM_START:
      DLOG ("event STREAM_START");
      if (_tensor_sink_get_emit_signal (self)) {
        g_signal_emit (self, _tensor_sink_signals[SIGNAL_STREAM_START], 0);
      }
      break;

    case GST_EVENT_EOS:
      DLOG ("event EOS");
      if (_tensor_sink_get_emit_signal (self)) {
        g_signal_emit (self, _tensor_sink_signals[SIGNAL_EOS], 0);
      }
      break;

    default:
      DLOG ("event type is %d", type);
      break;
  }

  return GST_BASE_SINK_CLASS (parent_class)->event (sink, event);
}

/**
 * @brief Handle queries.
 *
 * GstBaseSink method implementation.
 */
static gboolean
gst_tensor_sink_query (GstBaseSink * sink, GstQuery * query)
{
  GstTensorSink *self;
  GstQueryType type;
  GstFormat format;

  self = GST_TENSOR_SINK (sink);
  type = GST_QUERY_TYPE (query);

  switch (type) {
    case GST_QUERY_SEEKING:
      DLOG ("query SEEKING");
      /* tensor sink does not support seeking */
      gst_query_parse_seeking (query, &format, NULL, NULL, NULL);
      gst_query_set_seeking (query, format, FALSE, 0, -1);
      return TRUE;

    default:
      DLOG ("query type is %d", type);
      break;
  }

  return GST_BASE_SINK_CLASS (parent_class)->query (sink, query);
}

/**
 * @brief Handle buffer.
 *
 * GstBaseSink method implementation.
 */
static GstFlowReturn
gst_tensor_sink_render (GstBaseSink * sink, GstBuffer * buffer)
{
  GstTensorSink *self;

  self = GST_TENSOR_SINK (sink);
  _tensor_sink_render_buffer (self, buffer);

  return GST_FLOW_OK;
}

/**
 * @brief Handle list of buffers.
 *
 * GstBaseSink method implementation.
 */
static GstFlowReturn
gst_tensor_sink_render_list (GstBaseSink * sink, GstBufferList * buffer_list)
{
  GstTensorSink *self;
  GstBuffer *buffer;
  guint i;
  guint num_buffers;

  self = GST_TENSOR_SINK (sink);
  num_buffers = gst_buffer_list_length (buffer_list);

  for (i = 0; i < num_buffers; i++) {
    buffer = gst_buffer_list_get (buffer_list, i);
    _tensor_sink_render_buffer (self, buffer);
  }

  return GST_FLOW_OK;
}

/**
 * @brief Funtion for new caps.
 *
 * GstBaseSink method implementation.
 */
static gboolean
gst_tensor_sink_set_caps (GstBaseSink * sink, GstCaps * caps)
{
  GstTensorSink *self;

  self = GST_TENSOR_SINK (sink);

  g_mutex_lock (&self->mutex);
  gst_caps_replace (&self->in_caps, caps);
  g_mutex_unlock (&self->mutex);

  if (DBG) {
    guint caps_size, i;

    caps_size = gst_caps_get_size (caps);
    DLOG ("set caps, size is %d", caps_size);

    for (i = 0; i < caps_size; i++) {
      GstStructure *structure = gst_caps_get_structure (caps, i);
      gchar *str = gst_structure_to_string (structure);

      DLOG ("[%d] %s", i, str);
      g_free (str);
    }
  }

  return TRUE;
}

/**
 * @brief Funtion to return caps of subclass.
 *
 * GstBaseSink method implementation.
 */
static GstCaps *
gst_tensor_sink_get_caps (GstBaseSink * sink, GstCaps * filter)
{
  GstTensorSink *self;
  GstCaps *caps;

  self = GST_TENSOR_SINK (sink);

  g_mutex_lock (&self->mutex);
  caps = self->in_caps;

  if (caps) {
    if (filter) {
      caps = gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
    } else {
      gst_caps_ref (caps);
    }
  }
  g_mutex_unlock (&self->mutex);

  return caps;
}

/**
 * @brief Handle buffer data.
 * @return None
 * @param self pointer to GstTensorSink
 * @param buffer pointer to GstBuffer to be handled
 */
static void
_tensor_sink_render_buffer (GstTensorSink * self, GstBuffer * buffer)
{
  GstClockTime now = GST_CLOCK_TIME_NONE;
  guint64 render_rate;
  gboolean notify = FALSE;

  g_return_if_fail (GST_IS_TENSOR_SINK (self));

  render_rate = _tensor_sink_get_render_rate (self);

  if (render_rate) {
    GstClock *clock;
    GstClockTime render_time;
    GstClockTime last_render_time;

    clock = gst_element_get_clock (GST_ELEMENT (self));

    if (clock) {
      now = gst_clock_get_time (clock);
      last_render_time = _tensor_sink_get_last_render_time (self);

      /* time for next render */
      render_time = (1000 / render_rate) * GST_MSECOND + last_render_time;

      if (!GST_CLOCK_TIME_IS_VALID (last_render_time) ||
          GST_CLOCK_DIFF (now, render_time) <= 0) {
        /* send data after render time, or firstly received buffer */
        notify = TRUE;
      }

      gst_object_unref (clock);
    }
  } else {
    /* send data if render rate is 0 */
    notify = TRUE;
  }

  if (notify) {
    _tensor_sink_set_last_render_time (self, now);

    if (_tensor_sink_get_emit_signal (self)) {
      DLOG ("signal for new data [%" GST_TIME_FORMAT "], rate [%ld]",
          GST_TIME_ARGS (now), render_rate);
      g_signal_emit (self, _tensor_sink_signals[SIGNAL_NEW_DATA], 0, buffer);
    }
  }
}

/**
 * @brief Setter for value last_render_time.
 */
static void
_tensor_sink_set_last_render_time (GstTensorSink * self, GstClockTime now)
{
  g_return_if_fail (GST_IS_TENSOR_SINK (self));

  g_mutex_lock (&self->mutex);
  self->last_render_time = now;
  g_mutex_unlock (&self->mutex);
}

/**
 * @brief Getter for value last_render_time.
 */
static GstClockTime
_tensor_sink_get_last_render_time (GstTensorSink * self)
{
  GstClockTime last_render_time;

  g_return_val_if_fail (GST_IS_TENSOR_SINK (self), GST_CLOCK_TIME_NONE);

  g_mutex_lock (&self->mutex);
  last_render_time = self->last_render_time;
  g_mutex_unlock (&self->mutex);

  return last_render_time;
}

/**
 * @brief Setter for value render_rate.
 */
static void
_tensor_sink_set_render_rate (GstTensorSink * self, guint64 rate)
{
  g_return_if_fail (GST_IS_TENSOR_SINK (self));

  DLOG ("set render_rate to %ld", rate);
  g_mutex_lock (&self->mutex);
  self->render_rate = rate;
  g_mutex_unlock (&self->mutex);
}

/**
 * @brief Getter for value render_rate.
 */
static guint64
_tensor_sink_get_render_rate (GstTensorSink * self)
{
  guint64 rate;

  g_return_val_if_fail (GST_IS_TENSOR_SINK (self), 0);

  g_mutex_lock (&self->mutex);
  rate = self->render_rate;
  g_mutex_unlock (&self->mutex);

  return rate;
}

/**
 * @brief Setter for flag emit_signal.
 */
static void
_tensor_sink_set_emit_signal (GstTensorSink * self, gboolean emit)
{
  g_return_if_fail (GST_IS_TENSOR_SINK (self));

  DLOG ("set emit_signal to %d", emit);
  g_mutex_lock (&self->mutex);
  self->emit_signal = emit;
  g_mutex_unlock (&self->mutex);
}

/**
 * @brief Getter for flag emit_signal.
 */
static gboolean
_tensor_sink_get_emit_signal (GstTensorSink * self)
{
  gboolean res;

  g_return_val_if_fail (GST_IS_TENSOR_SINK (self), FALSE);

  g_mutex_lock (&self->mutex);
  res = self->emit_signal;
  g_mutex_unlock (&self->mutex);

  return res;
}

/**
 * @brief Setter for flag silent.
 */
static void
_tensor_sink_set_silent (GstTensorSink * self, gboolean silent)
{
  g_return_if_fail (GST_IS_TENSOR_SINK (self));

  DLOG ("set silent to %d", silent);
  self->silent = silent;
}

/**
 * @brief Getter for flag silent.
 */
static gboolean
_tensor_sink_get_silent (GstTensorSink * self)
{
  g_return_val_if_fail (GST_IS_TENSOR_SINK (self), TRUE);

  return self->silent;
}

/**
 * @brief Function used in type implementation.
 */
static void
_tensor_sink_do_init (GType type)
{
  /* add interface */
}

/**
 * @brief Function to initialize the plugin.
 *
 * See GstPluginInitFunc() for more details.
 */
static gboolean
_tensor_sink_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_tensor_sink_debug, "tensor_sink",
      0, "tensor_sink element");

  return gst_element_register (plugin, "tensor_sink",
      GST_RANK_NONE, GST_TYPE_TENSOR_SINK);
}

/**
 * @brief Definition for identifying tensor_sink plugin.
 *
 * PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "tensor_sink"
#endif

/**
 * @brief Macro to define the entry point of the plugin.
 */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    tensor_sink,
    "Sink element for tensor stream",
    _tensor_sink_plugin_init, VERSION, "LGPL", "GStreamer",
    "http://gstreamer.net/");
