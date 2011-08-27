/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David Schleef <ds@schleef.org>
 * Copyright (C) <2010> Christoph Reiter <christoph.reiter@gmx.at>
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

/* This uses the bs2b library, created by Boris Mikhaylov
 * http://bs2b.sourceforge.net/
 *
 * activate -> active=True (default)
 * frequency cut (Hz) -> fcut=300..2000
 * feed level (db) -> feed=1.0..15.0
 *
 * Presets:
 *  - DEFAULT: preset=0 <=> fcut=700 / feed=4.5 (This is the overal default)
 *  - CMOY: preset=1 <=> fcut=700 / feed=6.0
 *  - JMEIER: preset=2 <=> fcut=650 / feed=9.5
 */

/**
 * SECTION:element-crossfeed
 *
 * Improve headphone listening of stereo audio records using the bs2b library.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch -v filesrc location=sine.ogg ! oggdemux ! vorbisdec ! audioconvert ! crossfeed ! alsasink
 * ]| Play an Ogg/Vorbis file.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <gst/audio/gstaudiofilter.h>

#include <bs2b/bs2b.h>

typedef struct _GstCrossfeed GstCrossfeed;
typedef struct _GstCrossfeedClass GstCrossfeedClass;

#define GST_TYPE_CROSSFEED \
  (gst_crossfeed_get_type())
#define GST_CROSSFEED(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CROSSFEED,GstCrossfeed))
#define GST_CROSSFEED_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CROSSFEED,GstCrossfeedClass))
#define GST_IS_CROSSFEED(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CROSSFEED))
#define GST_IS_CROSSFEED_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CROSSFEED))

#define GST_CROSSFEED_BS2B_LOCK(obj) g_mutex_lock (obj->bs2b_lock)
#define GST_CROSSFEED_BS2B_UNLOCK(obj) g_mutex_unlock (obj->bs2b_lock)

#define GST_CROSSFEED_LOCK(obj) g_mutex_lock (obj->lock)
#define GST_CROSSFEED_UNLOCK(obj) g_mutex_unlock (obj->lock)

struct _GstCrossfeed {
  GstAudioFilter element;

  GMutex *bs2b_lock;
  t_bs2bdp bs2bdp;
  void (*func) ();

  GMutex *lock;
  gboolean active;
  gint divider;
};

struct _GstCrossfeedClass {
  GstAudioFilterClass parent_class;
};

static const GstElementDetails crossfeed_details = GST_ELEMENT_DETAILS (
    "Crossfeed effect",
    "Filter/Effect/Audio",
    "Improve headphone listening of stereo audio records"
        "using the bs2b library.",
    "Christoph Reiter <christoph.reiter@gmx.at>");

#define ALLOWED_CAPS \
    "audio/x-raw-int, " \
    "rate = (int) [ " \
        G_STRINGIFY (BS2B_MINSRATE) "," G_STRINGIFY (BS2B_MAXSRATE) \
    " ], " \
    "channels = (int) { 1, 2 }, " \
    "endianness = (int) { 1234, 4321 }, " \
    "width = (int) { 8, 16, 24, 32 }, " \
    "signed = (boolean) { true, false }; " \
    "audio/x-raw-float, " \
    "rate = (int) [ " \
        G_STRINGIFY (BS2B_MINSRATE) "," G_STRINGIFY (BS2B_MAXSRATE) \
    " ], " \
    "channels = (int) { 1, 2 }, " \
    "endianness = (int) { 1234, 4321 }, " \
    "width = (int) {32, 64} "

enum
{
  ARG_0,
  ARG_ACTIVE,
  ARG_FCUT,
  ARG_FEED,
  ARG_PRESET
};

typedef enum
{
  PRESET_DEFAULT,
  PRESET_CMOY,
  PRESET_JMEIER,
  PRESET_NONE
} GstBs2bPreset;

#define DEFAULT_FCUT (BS2B_DEFAULT_CLEVEL & 0xFFFF)
#define DEFAULT_FEED (BS2B_DEFAULT_CLEVEL >> 16)
#define FEED_FACTOR 10.0f

static void gst_crossfeed_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_crossfeed_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GType gst_crossfeed_preset_get_type (void);

static gboolean gst_crossfeed_setup (GstAudioFilter * self,
    GstRingBufferSpec * format);

static void gst_crossfeed_finalize (GObject * object);

static gboolean gst_crossfeed_sink_eventfunc (GstBaseTransform * trans,
    GstEvent * event);

static GstFlowReturn gst_crossfeed_transform_inplace (GstBaseTransform * base,
    GstBuffer * outbuf);

static void gst_crossfeed_update_passthrough (GstCrossfeed * crossfeed);

GST_BOILERPLATE (GstCrossfeed, gst_crossfeed, GstAudioFilter,
    GST_TYPE_AUDIO_FILTER);

static void
gst_crossfeed_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstCaps *caps;

  gst_element_class_set_details (element_class, &crossfeed_details);

  caps = gst_caps_from_string (ALLOWED_CAPS);
  gst_audio_filter_class_add_pad_templates (GST_AUDIO_FILTER_CLASS (g_class),
      caps);
  gst_caps_unref (caps);
}

static void
gst_crossfeed_class_init (GstCrossfeedClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBaseTransformClass *trans_class = (GstBaseTransformClass *) klass;
  GstAudioFilterClass *filter_class = (GstAudioFilterClass *) klass;

  gobject_class->set_property = gst_crossfeed_set_property;
  gobject_class->get_property = gst_crossfeed_get_property;
  gobject_class->finalize = gst_crossfeed_finalize;

  trans_class->transform_ip = gst_crossfeed_transform_inplace;
  trans_class->event = gst_crossfeed_sink_eventfunc;
  filter_class->setup = gst_crossfeed_setup;

  g_object_class_install_property (gobject_class, ARG_ACTIVE,
      g_param_spec_boolean ("active", "Active",
          "Specify whether the filter is active",
          TRUE, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
          G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, ARG_FCUT,
      g_param_spec_int ("fcut", "Frequency cut",
          "Lowpass filter cut frequency (Hz)",
          BS2B_MINFCUT, BS2B_MAXFCUT, DEFAULT_FCUT,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
          G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, ARG_FEED,
      g_param_spec_float ("feed", "Feed level", "Feed Level (db)",
          BS2B_MINFEED / FEED_FACTOR, BS2B_MAXFEED / FEED_FACTOR,
          DEFAULT_FEED / FEED_FACTOR,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
          G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, ARG_PRESET,
      g_param_spec_enum ("preset", "Preset", "Bs2b filter preset",
          gst_crossfeed_preset_get_type (),
          PRESET_DEFAULT, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
          G_PARAM_STATIC_STRINGS));
}

static void
gst_crossfeed_init (GstCrossfeed * crossfeed, GstCrossfeedClass * klass)
{
  gst_base_transform_set_gap_aware (GST_BASE_TRANSFORM (crossfeed), TRUE);

  crossfeed->lock = g_mutex_new();
  crossfeed->bs2b_lock = g_mutex_new();
  crossfeed->active = TRUE;

  GST_CROSSFEED_BS2B_LOCK (crossfeed);
  crossfeed->bs2bdp = bs2b_open ();
  GST_CROSSFEED_BS2B_UNLOCK (crossfeed);
}

static gboolean gst_crossfeed_setup (GstAudioFilter * filter,
  GstRingBufferSpec * format)
{
  GstCrossfeed *crossfeed = GST_CROSSFEED (filter);

  crossfeed->func = NULL;

  if (format->type == GST_BUFTYPE_LINEAR) {
    if (format->width == 8) {
      if (format->sign)
        crossfeed->func = &bs2b_cross_feed_s8;
      else
        crossfeed->func = &bs2b_cross_feed_u8;
    }
    else if (format->width == 16) {
      if (format->bigend && format->sign)
        crossfeed->func = &bs2b_cross_feed_s16be;
      else if(format->bigend)
        crossfeed->func = &bs2b_cross_feed_u16be;
      else if (format->sign)
        crossfeed->func = &bs2b_cross_feed_s16le;
      else
        crossfeed->func = &bs2b_cross_feed_u16le;
    }
    else if (format->width == 24) {
      if (format->bigend && format->sign)
        crossfeed->func = &bs2b_cross_feed_s24be;
      else if(format->bigend)
        crossfeed->func = &bs2b_cross_feed_u24be;
      else if (format->sign)
        crossfeed->func = &bs2b_cross_feed_s24le;
      else
        crossfeed->func = &bs2b_cross_feed_u24le;
    }
    else if (format->width == 32) {
      if (format->bigend && format->sign)
        crossfeed->func = &bs2b_cross_feed_s32be;
      else if(format->bigend)
        crossfeed->func = &bs2b_cross_feed_u32be;
      else if (format->sign)
        crossfeed->func = &bs2b_cross_feed_s32le;
      else
        crossfeed->func = &bs2b_cross_feed_u32le;
    }
  }
  else if (format->type == GST_BUFTYPE_FLOAT) {
    if (format->width == 32) {
      if (format->bigend)
        crossfeed->func = &bs2b_cross_feed_fbe;
      else
        crossfeed->func = &bs2b_cross_feed_fle;
    }
    else if (format->width == 64) {
      if (format->bigend)
        crossfeed->func = &bs2b_cross_feed_dbe;
      else
        crossfeed->func = &bs2b_cross_feed_dle;
    }
  }

  if (crossfeed->func == NULL)
    return FALSE;

  GST_CROSSFEED_LOCK (crossfeed);
  gst_crossfeed_update_passthrough (crossfeed);
  GST_CROSSFEED_UNLOCK (crossfeed);

  crossfeed->divider = format->width / 4;

  /* set_rate calls clear, so no need to reset the filter here */
  GST_CROSSFEED_BS2B_LOCK (crossfeed);
  bs2b_set_srate (crossfeed->bs2bdp, format->rate);
  GST_CROSSFEED_BS2B_UNLOCK (crossfeed);

  return TRUE;
}

static void
gst_crossfeed_finalize (GObject * object)
{
  GstCrossfeed *crossfeed = GST_CROSSFEED (object);

  bs2b_close(crossfeed->bs2bdp);
  crossfeed->bs2bdp = NULL;

  g_mutex_free (crossfeed->bs2b_lock);
  g_mutex_free (crossfeed->lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean gst_crossfeed_sink_eventfunc (GstBaseTransform * trans,
    GstEvent * event)
{
  GstCrossfeed *crossfeed = GST_CROSSFEED (trans);

  if (GST_EVENT_TYPE (event) == GST_EVENT_NEWSEGMENT) {
    GST_CROSSFEED_BS2B_LOCK (crossfeed);
    bs2b_clear (crossfeed->bs2bdp);
    GST_CROSSFEED_BS2B_UNLOCK (crossfeed);
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->event (trans, event);
}

static GstFlowReturn
gst_crossfeed_transform_inplace (GstBaseTransform * base, GstBuffer * outbuf)
{
  GstCrossfeed *crossfeed = GST_CROSSFEED (base);
  void *data = GST_BUFFER_DATA (outbuf);
  gint samples = GST_BUFFER_SIZE (outbuf);

  if(gst_base_transform_is_passthrough (base) ||
      G_UNLIKELY (GST_BUFFER_FLAG_IS_SET (outbuf, GST_BUFFER_FLAG_GAP)))
    return GST_FLOW_OK;

  crossfeed->func (crossfeed->bs2bdp, data, samples / crossfeed->divider);

  return GST_FLOW_OK;
}

static void
gst_crossfeed_update_passthrough (GstCrossfeed * crossfeed)
{
  GstAudioFilter *filter = GST_AUDIO_FILTER (crossfeed);
  GstBaseTransform *trans = GST_BASE_TRANSFORM (crossfeed);
  gboolean passthrough;

  passthrough = (
      filter->format.channels != 2 ||
      !crossfeed->active) ? TRUE : FALSE;

  gst_base_transform_set_passthrough (trans, passthrough);
}

static GType
gst_crossfeed_preset_get_type (void)
{
  static GType crossfeed_preset_type = 0;

  if (!crossfeed_preset_type) {
    static GEnumValue types[] = {
      {
        PRESET_DEFAULT,
        "Closest to virtual speaker placement (30°, 3 meter)   [700Hz, 4.5dB]",
        "default"
      },
      {
        PRESET_CMOY,
        "Close to Chu Moy's crossfeeder (popular)              [700Hz, 6.0dB]",
        "cmoy"
      },
      {
        PRESET_JMEIER,
        "Close to Jan Meier's CORDA amplifiers (little change) [650Hz, 9.0dB]",
        "jmeier"
      },
      {
        PRESET_NONE,
        "No preset",
        "none"
      },
      {0, NULL, NULL},
    };

    crossfeed_preset_type = g_enum_register_static ("GstBs2bPreset", types);
  }

  return crossfeed_preset_type;
}

static void
gst_crossfeed_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCrossfeed *crossfeed = GST_CROSSFEED (object);

  switch (prop_id) {
    case ARG_ACTIVE:
      GST_CROSSFEED_LOCK (crossfeed);
      crossfeed->active = g_value_get_boolean (value);
      gst_crossfeed_update_passthrough (crossfeed);
      /* Clear the filter buffer if it gets set inactive, so we have
       * a fresh start when it gets activated again. */
      if (!crossfeed->active) {
        GST_CROSSFEED_UNLOCK (crossfeed);
        GST_CROSSFEED_BS2B_LOCK (crossfeed);
        bs2b_clear (crossfeed->bs2bdp);
        GST_CROSSFEED_BS2B_UNLOCK (crossfeed);
      } else {
        GST_CROSSFEED_UNLOCK (crossfeed);
      }
      break;
    case ARG_FCUT:
      GST_CROSSFEED_BS2B_LOCK (crossfeed);
      bs2b_set_level_fcut (crossfeed->bs2bdp, g_value_get_int (value));
      GST_CROSSFEED_BS2B_UNLOCK (crossfeed);
      break;
    case ARG_FEED:
      GST_CROSSFEED_BS2B_LOCK (crossfeed);
      bs2b_set_level_feed (crossfeed->bs2bdp,
          g_value_get_float (value) * FEED_FACTOR);
      GST_CROSSFEED_BS2B_UNLOCK (crossfeed);
      break;
    case ARG_PRESET:
      switch (g_value_get_enum (value)) {
        case PRESET_DEFAULT:
          GST_CROSSFEED_BS2B_LOCK (crossfeed);
          bs2b_set_level (crossfeed->bs2bdp, BS2B_DEFAULT_CLEVEL);
          GST_CROSSFEED_BS2B_UNLOCK (crossfeed);
          break;
        case PRESET_CMOY:
          GST_CROSSFEED_BS2B_LOCK (crossfeed);
          bs2b_set_level (crossfeed->bs2bdp, BS2B_CMOY_CLEVEL);
          GST_CROSSFEED_BS2B_UNLOCK (crossfeed);
          break;
        case PRESET_JMEIER:
          GST_CROSSFEED_BS2B_LOCK (crossfeed);
          bs2b_set_level (crossfeed->bs2bdp, BS2B_JMEIER_CLEVEL);
          GST_CROSSFEED_BS2B_UNLOCK (crossfeed);
          break;
        default:
          G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
          break;
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_crossfeed_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstCrossfeed *crossfeed = GST_CROSSFEED (object);

  switch (prop_id) {
    case ARG_ACTIVE:
      g_value_set_boolean (value, crossfeed->active);
      break;
    case ARG_FCUT:
      GST_CROSSFEED_BS2B_LOCK (crossfeed);
      g_value_set_int (value, bs2b_get_level_fcut (crossfeed->bs2bdp));
      GST_CROSSFEED_BS2B_UNLOCK (crossfeed);
      break;
    case ARG_FEED:
      GST_CROSSFEED_BS2B_LOCK (crossfeed);
      g_value_set_float (value,
          bs2b_get_level_feed (crossfeed->bs2bdp) / FEED_FACTOR);
      GST_CROSSFEED_BS2B_UNLOCK (crossfeed);
      break;
    case ARG_PRESET:
      GST_CROSSFEED_BS2B_LOCK (crossfeed);
      switch (bs2b_get_level (crossfeed->bs2bdp)) {
        case BS2B_DEFAULT_CLEVEL:
          g_value_set_enum (value, PRESET_DEFAULT);
          break;
        case BS2B_CMOY_CLEVEL:
          g_value_set_enum (value, PRESET_CMOY);
          break;
        case BS2B_JMEIER_CLEVEL:
          g_value_set_enum (value, PRESET_JMEIER);
          break;
        default:
          g_value_set_enum (value, PRESET_NONE);
          break;
      }
      GST_CROSSFEED_BS2B_UNLOCK (crossfeed);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "crossfeed", GST_RANK_NONE,
      GST_TYPE_CROSSFEED);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "crossfeed",
    "Improve headphone listening of stereo audio records"
        "using the bs2b library.",
    plugin_init,
    VERSION, "LGPL",
    "gstreamer0.10-bs2b",
    "https://github.com/lazka/gst-bs2b"
);
