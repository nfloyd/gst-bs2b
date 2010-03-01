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
 * gst-launch -v filesrc location=sine.ogg ! oggdemux ! vorbisdec ! audioconvert ! crossfeed ! audioconvert ! audioresample ! alsasink
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
};

struct _GstCrossfeedClass {
  GstAudioFilterClass parent_class;
};

static const GstElementDetails crossfeed_details = GST_ELEMENT_DETAILS (
    "Crossfeed effect",
    "Filter/Effect/Audio",
    "Improve headphone listening of stereo audio records",
    "Christoph Reiter <christoph.reiter@gmx.at>");

static GstStaticPadTemplate sink_template_factory =
  GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
      "audio/x-raw-int, "
      "rate = (int) [ 1, MAX ], "
      "channels = (int) 2, "
      "endianness = (int) { 1234, 4321 }, "
      "width = (int) { 8, 16, 32 }, "
      "depth = (int) { 8, 16, 32 }, "
      "signed = (boolean) { true, false }; "

      "audio/x-raw-float, "
      "rate = (int) [ 1, MAX ], "
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
      "rate = (int) [ 1, MAX ], "
      "channels = (int) 2, "
      "endianness = (int) { 1234, 4321 }, "
      "width = (int) { 8, 16, 32 }, "
      "depth = (int) { 8, 16, 32 }, "
      "signed = (boolean) { true, false }; "

      "audio/x-raw-float, "
      "rate = (int) [ 1, MAX ], "
      "channels = (int) 2, "
      "endianness = (int) { 1234, 4321 }, "
      "width = (int) {32, 64} ")
  );

enum
{
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_ACTIVE,
  ARG_FCUT,
  ARG_FEED,
  ARG_PRESET
};

enum
{
  PRESET_DEFAULT,
  PRESET_CMOY,
  PRESET_JMEIER,
  PRESET_NONE
};

#define DEFAULT_FCUT ((BS2B_DEFAULT_CLEVEL) & 0xFFFF)
#define DEFAULT_FEED ((BS2B_DEFAULT_CLEVEL) >> 16)

t_bs2bdp _bs2bdp = NULL;

static void gst_crossfeed_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_crossfeed_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_crossfeed_transform_inplace (GstBaseTransform * base,
    GstBuffer * outbuf);

GST_BOILERPLATE (GstCrossfeed, gst_crossfeed, GstAudioFilter, GST_TYPE_AUDIO_FILTER);

static gboolean
gst_crossfeed_setcaps (GstBaseTransform * trans, GstCaps * incaps, GstCaps * outcaps)
{
  GstCrossfeed *crossfeed = GST_CROSSFEED(trans);
  GstStructure *s = gst_caps_get_structure (incaps, 0);
  const gchar *mimetype;
  gint endianness;

  mimetype = gst_structure_get_name(s);
  if (strcmp (mimetype, "audio/x-raw-int") == 0)
    crossfeed->is_int = TRUE;
  else
    crossfeed->is_int = FALSE;

  gst_structure_get_int (s, "rate", &crossfeed->samplerate);
  bs2b_set_srate(_bs2bdp, crossfeed->samplerate);

  gst_structure_get_int (s, "endianness", &endianness);
  if ( endianness == 1234)
    crossfeed->little_endian = TRUE;
  else
    crossfeed->little_endian = FALSE;

  gst_structure_get_int (s, "width", &crossfeed->width);

  gst_structure_get_int (s, "depth", &crossfeed->depth);

  gst_structure_get_boolean (s, "signed", &crossfeed->sign);

  printf("---------------------\n");
  printf("mime:       %s\n", mimetype);
  printf("rate:       %d\n", crossfeed->samplerate);
  printf("width:      %d\n", crossfeed->width);
  printf("depth:      %d\n", crossfeed->depth);
  printf("li. endian: %d\n", crossfeed->little_endian);
  printf("end. nativ: %d\n", BYTE_ORDER);
  printf("signed:     %d\n", crossfeed->sign);
  printf("---------------------\n");

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
  GstElementClass *gstelement_class;
  GstBaseTransformClass *trans_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  trans_class = (GstBaseTransformClass *) klass;

  gobject_class->set_property = gst_crossfeed_set_property;
  gobject_class->get_property = gst_crossfeed_get_property;

  g_object_class_install_property (gobject_class, ARG_ACTIVE,
      g_param_spec_boolean ("active", "active", "active",
          TRUE, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  g_object_class_install_property (gobject_class, ARG_FCUT,
      g_param_spec_int ("fcut", "fcut", "fcut",
          BS2B_MINFCUT, BS2B_MAXFCUT, DEFAULT_FCUT, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  g_object_class_install_property (gobject_class, ARG_FEED,
      g_param_spec_float ("feed", "feed", "feed",
          BS2B_MINFEED/10.0, BS2B_MAXFEED/10.0, DEFAULT_FEED/10.0, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  g_object_class_install_property (gobject_class, ARG_PRESET,
      g_param_spec_int ("preset", "preset", "preset",
          0, 2, PRESET_DEFAULT, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  trans_class->transform_ip = gst_crossfeed_transform_inplace;
  trans_class->set_caps = gst_crossfeed_setcaps;
}

static void
gst_crossfeed_init (GstCrossfeed * crossfeed, GstCrossfeedClass * klass)
{
  _bs2bdp = bs2b_open ();

  crossfeed->active = TRUE;
}

static GstFlowReturn
gst_crossfeed_transform_inplace (GstBaseTransform * base, GstBuffer * outbuf)
{
  GstCrossfeed *crossfeed = GST_CROSSFEED (base);
  void *data = GST_BUFFER_DATA (outbuf);
  gint samples = GST_BUFFER_SIZE (outbuf);

  if (crossfeed->active) {
    if (crossfeed->is_int)
    {
      if (crossfeed->width == 8)
      {
        if (crossfeed->sign)
          bs2b_cross_feed_s8 (_bs2bdp, (gint8 *) data, samples/2);
        else
          bs2b_cross_feed_u8 (_bs2bdp, (guint8 *) data, samples/2);
      }
      else if (crossfeed->width == 16)
      {
        if (crossfeed->little_endian)
        {
          if (crossfeed->sign)
            bs2b_cross_feed_s16le (_bs2bdp, (gint16 *) data, samples/4);
          else
            bs2b_cross_feed_u16le (_bs2bdp, (guint16 *) data, samples/4);
        }
        else
        {
          if (crossfeed->sign)
            bs2b_cross_feed_s16be (_bs2bdp, (gint16 *) data, samples/4);
          else
            bs2b_cross_feed_u16be (_bs2bdp, (guint16 *) data, samples/4);
        }
      }
      else if (crossfeed->width == 32)
      {
        if (crossfeed->little_endian)
        {
          if (crossfeed->sign)
            bs2b_cross_feed_s32le (_bs2bdp, (gint32 *) data, samples/8);
          else
            bs2b_cross_feed_u32le (_bs2bdp, (guint32 *) data, samples/8);
        }
        else
        {
          if (crossfeed->sign)
            bs2b_cross_feed_s32be (_bs2bdp, (gint32 *) data, samples/8);
          else
            bs2b_cross_feed_u32be (_bs2bdp, (guint32 *) data, samples/8);
        }
      }
    }
    else
    {
      if (crossfeed->width == 32)
      {
        if (crossfeed->little_endian)
          bs2b_cross_feed_fle (_bs2bdp, (gfloat *) data, samples/8);
        else
          bs2b_cross_feed_fbe (_bs2bdp, (gfloat *) data, samples/8);
      }
      else if (crossfeed->width == 64)
      {
        if (crossfeed->little_endian)
          bs2b_cross_feed_dle (_bs2bdp, (gdouble *) data, samples/16);
        else
          bs2b_cross_feed_dbe (_bs2bdp, (gdouble *) data, samples/16);
      }
    }
  }

  return GST_FLOW_OK;
}

static void
gst_crossfeed_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
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
      bs2b_set_level_fcut (_bs2bdp, crossfeed->fcut);
      crossfeed->level = bs2b_get_level (_bs2bdp);
      break;
    case ARG_FEED:
      crossfeed->feed = (gint) (g_value_get_float (value) * 10);
      bs2b_set_level_feed (_bs2bdp, crossfeed->feed);
      crossfeed->level = bs2b_get_level (_bs2bdp);
      break;
    case ARG_PRESET:
      switch (g_value_get_int (value)) {
        case PRESET_DEFAULT:
          bs2b_set_level (_bs2bdp, BS2B_DEFAULT_CLEVEL);
          break;
        case PRESET_CMOY:
          bs2b_set_level (_bs2bdp, BS2B_CMOY_CLEVEL);
          break;
        case PRESET_JMEIER:
          bs2b_set_level (_bs2bdp, BS2B_JMEIER_CLEVEL);
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
      g_value_set_float (value, ((gfloat)crossfeed->feed) / 10);
      break;
    case ARG_PRESET:
      switch (crossfeed->level) {
        case BS2B_DEFAULT_CLEVEL:
          g_value_set_int (value, PRESET_DEFAULT);
          break;
        case BS2B_CMOY_CLEVEL:
          g_value_set_int (value, PRESET_CMOY);
          break;
        case BS2B_JMEIER_CLEVEL:
          g_value_set_int (value, PRESET_JMEIER);
          break;
        default:
          g_value_set_int (value, PRESET_NONE);
          break;
      }
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
    "Improve headphone listening of stereo audio records using the bs2b library.",
    plugin_init,
    VERSION, "LGPL",
    "gstreamer0.10-bs2b",
    "http://bitbucket.org/lazka/gst-bs2b/"
);
