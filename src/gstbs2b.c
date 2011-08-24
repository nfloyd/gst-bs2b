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

#include <string.h>

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

struct _GstCrossfeed {
  GstAudioFilter element;

  gboolean active;
  guint32 level;
  gint fcut;
  gint feed;

  gint samplerate;
  gboolean is_int;
  gboolean little_endian;
  gint width;
  gint depth;
  gboolean sign;

  t_bs2bdp bs2bdp;
  void (*func) ();
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

static GstStaticPadTemplate sink_template_factory =
    GST_STATIC_PAD_TEMPLATE ("sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (
            "audio/x-raw-int, "
            "rate = (int) [ "
                G_STRINGIFY (BS2B_MINSRATE) "," G_STRINGIFY (BS2B_MAXSRATE)
            " ], "
            "channels = (int) 2, "
            "endianness = (int) { 1234, 4321 }, "
            "width = (int) { 8, 16, 32 }, "
            "depth = (int) { 8, 16, 32 }, "
            "signed = (boolean) { true, false }; "

            "audio/x-raw-float, "
            "rate = (int) [ "
                G_STRINGIFY (BS2B_MINSRATE) "," G_STRINGIFY (BS2B_MAXSRATE)
            " ], "
            "channels = (int) 2, "
            "endianness = (int) { 1234, 4321 }, "
            "width = (int) {32, 64} ")
    );

static GstStaticPadTemplate src_template_factory =
  GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
      "audio/x-raw-int, "
      "rate = (int) [ "
          G_STRINGIFY (BS2B_MINSRATE) "," G_STRINGIFY (BS2B_MAXSRATE)
      " ], "
      "channels = (int) 2, "
      "endianness = (int) { 1234, 4321 }, "
      "width = (int) { 8, 16, 32 }, "
      "depth = (int) { 8, 16, 32 }, "
      "signed = (boolean) { true, false }; "

      "audio/x-raw-float, "
      "rate = (int) [ "
          G_STRINGIFY (BS2B_MINSRATE) "," G_STRINGIFY (BS2B_MAXSRATE)
      " ], "
      "channels = (int) 2, "
      "endianness = (int) { 1234, 4321 }, "
      "width = (int) {32, 64} ")
  );

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
#define FEED_FACTOR 10.0

static void gst_crossfeed_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_crossfeed_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GType gst_crossfeed_preset_get_type (void);

static GstFlowReturn gst_crossfeed_transform_inplace (GstBaseTransform * base,
    GstBuffer * outbuf);

static void set_bs2b_filter_function (GstCrossfeed *crossfeed);

GST_BOILERPLATE (GstCrossfeed, gst_crossfeed, GstAudioFilter,
    GST_TYPE_AUDIO_FILTER);

static gboolean
gst_crossfeed_setcaps (GstBaseTransform * trans, GstCaps * in, GstCaps * out)
{
  GstCrossfeed *crossfeed = GST_CROSSFEED(trans);
  GstStructure *s = gst_caps_get_structure (in, 0);

  crossfeed->is_int = !strcmp (gst_structure_get_name (s), "audio/x-raw-int");

  gst_structure_get_int (s, "rate", &crossfeed->samplerate);
  bs2b_set_srate (crossfeed->bs2bdp, crossfeed->samplerate);

  crossfeed->little_endian =
      (g_value_get_int (gst_structure_get_value (s, "endianness")) == 1234);

  gst_structure_get_int (s, "width", &crossfeed->width);

  gst_structure_get_int (s, "depth", &crossfeed->depth);

  gst_structure_get_boolean (s, "signed", &crossfeed->sign);

  set_bs2b_filter_function (crossfeed);

  return TRUE;
}

static void
gst_crossfeed_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template_factory));

  gst_element_class_set_details (element_class, &crossfeed_details);
}

static void
gst_crossfeed_class_init (GstCrossfeedClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *trans_class;

  gobject_class = (GObjectClass *) klass;
  trans_class = (GstBaseTransformClass *) klass;

  gobject_class->set_property = gst_crossfeed_set_property;
  gobject_class->get_property = gst_crossfeed_get_property;

  g_object_class_install_property (gobject_class, ARG_ACTIVE,
      g_param_spec_boolean ("active", "Active",
          "Specify whether the filter is active",
          TRUE, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  g_object_class_install_property (gobject_class, ARG_FCUT,
      g_param_spec_int ("fcut", "Frequency cut",
          "Lowpass filter cut frequency (Hz)",
          BS2B_MINFCUT, BS2B_MAXFCUT, DEFAULT_FCUT,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  g_object_class_install_property (gobject_class, ARG_FEED,
      g_param_spec_float ("feed", "Feed level", "Feed Level (db)",
          BS2B_MINFEED / FEED_FACTOR, BS2B_MAXFEED / FEED_FACTOR,
          DEFAULT_FEED / FEED_FACTOR,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  g_object_class_install_property (gobject_class, ARG_PRESET,
      g_param_spec_enum ("preset", "Preset", "Bs2b filter preset",
          gst_crossfeed_preset_get_type (),
          PRESET_DEFAULT, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  trans_class->transform_ip = gst_crossfeed_transform_inplace;
  trans_class->set_caps = gst_crossfeed_setcaps;
}

static void
gst_crossfeed_init (GstCrossfeed * crossfeed, GstCrossfeedClass * klass)
{
  crossfeed->bs2bdp = bs2b_open ();
  crossfeed->active = TRUE;
}

static GstFlowReturn
gst_crossfeed_transform_inplace (GstBaseTransform * base, GstBuffer * outbuf)
{
  GstCrossfeed *crossfeed = GST_CROSSFEED (base);
  void *data = GST_BUFFER_DATA (outbuf);
  gint samples = GST_BUFFER_SIZE (outbuf);

  if (G_LIKELY (crossfeed->func != NULL) && crossfeed->active)
    crossfeed->func (crossfeed->bs2bdp, data, samples / crossfeed->divider);

  return GST_FLOW_OK;
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
  GstCrossfeed *crossfeed;

  g_return_if_fail (GST_IS_CROSSFEED (object));
  crossfeed = GST_CROSSFEED (object);

  switch (prop_id) {
    case ARG_ACTIVE:
      crossfeed->active = g_value_get_boolean (value);
      break;
    case ARG_FCUT:
      crossfeed->fcut = g_value_get_int (value);
      bs2b_set_level_fcut (crossfeed->bs2bdp, crossfeed->fcut);
      crossfeed->level = bs2b_get_level (crossfeed->bs2bdp);
      break;
    case ARG_FEED:
      crossfeed->feed = (gint) (g_value_get_float (value) * FEED_FACTOR);
      bs2b_set_level_feed (crossfeed->bs2bdp, crossfeed->feed);
      crossfeed->level = bs2b_get_level (crossfeed->bs2bdp);
      break;
    case ARG_PRESET:
      switch (g_value_get_enum (value)) {
        case PRESET_DEFAULT:
          bs2b_set_level (crossfeed->bs2bdp, BS2B_DEFAULT_CLEVEL);
          break;
        case PRESET_CMOY:
          bs2b_set_level (crossfeed->bs2bdp, BS2B_CMOY_CLEVEL);
          break;
        case PRESET_JMEIER:
          bs2b_set_level (crossfeed->bs2bdp, BS2B_JMEIER_CLEVEL);
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
  GstCrossfeed *crossfeed;

  g_return_if_fail (GST_IS_CROSSFEED (object));
  crossfeed = GST_CROSSFEED (object);

  switch (prop_id) {
    case ARG_ACTIVE:
      g_value_set_boolean (value, crossfeed->active);
      break;
    case ARG_FCUT:
      g_value_set_int (value, crossfeed->fcut);
      break;
    case ARG_FEED:
      g_value_set_float (value, (crossfeed->feed / FEED_FACTOR));
      break;
    case ARG_PRESET:
      switch (crossfeed->level) {
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
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
set_bs2b_filter_function (GstCrossfeed *crossfeed)
{
  crossfeed->divider = crossfeed->width / 4;

  if (crossfeed->is_int) {
    if (crossfeed->width == 8) {
      if (crossfeed->sign)
        crossfeed->func = &bs2b_cross_feed_s8;
      else
        crossfeed->func = &bs2b_cross_feed_u8;
    }
    else if (crossfeed->width == 16) {
      if (crossfeed->little_endian) {
        if (crossfeed->sign)
          crossfeed->func = &bs2b_cross_feed_s16le;
        else
          crossfeed->func = &bs2b_cross_feed_u16le;
      }
      else {
        if (crossfeed->sign)
          crossfeed->func = &bs2b_cross_feed_s16be;
        else
          crossfeed->func = &bs2b_cross_feed_u16be;
      }
    }
    else if (crossfeed->width == 32) {
      if (crossfeed->little_endian) {
        if (crossfeed->sign)
          crossfeed->func = &bs2b_cross_feed_s32le;
        else
          crossfeed->func = &bs2b_cross_feed_u32le;
      }
      else {
        if (crossfeed->sign)
          crossfeed->func = &bs2b_cross_feed_s32be;
        else
          crossfeed->func = &bs2b_cross_feed_u32be;
      }
    }
  }
  else {
    if (crossfeed->width == 32) {
      if (crossfeed->little_endian)
        crossfeed->func = &bs2b_cross_feed_fle;
      else
        crossfeed->func = &bs2b_cross_feed_fbe;
    }
    else if (crossfeed->width == 64) {
      if (crossfeed->little_endian)
        crossfeed->func = &bs2b_cross_feed_dle;
      else
        crossfeed->func = &bs2b_cross_feed_dbe;
    }
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
