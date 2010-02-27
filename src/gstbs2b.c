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
 * Improve headphone listening of stereo audio records using the bs2b library
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
};

struct _GstCrossfeedClass {
  GstAudioFilterClass parent_class;
};


#define ALLOWED_CAPS \
    "audio/x-raw-int,"                                                \
    " depth = (int) 16, "                                             \
    " width = (int) 16, "                                             \
    " endianness = (int) BYTE_ORDER,"                                 \
    " rate = (int) [ 1, MAX ],"                                       \
    " channels = (int) 2, "                                           \
    " signed = (boolean) TRUE"

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

  gst_structure_get_int (s, "rate", &crossfeed->samplerate);
  bs2b_set_srate(_bs2bdp, crossfeed->samplerate);

  return TRUE;
}

static void
gst_crossfeed_base_init (gpointer g_class)
{
  GstAudioFilterClass *audiofilter_class = GST_AUDIO_FILTER_CLASS (g_class);
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstCaps *caps;

  gst_element_class_set_details_simple (element_class,
    "Crossfeed effect",
    "Filter/Effect/Audio",
    "Improve headphone listening of stereo audio records",
    "Christoph Reiter <christoph.reiter@gmx.at>");

  caps = gst_caps_from_string (ALLOWED_CAPS);
  gst_audio_filter_class_add_pad_templates (audiofilter_class, caps);
  gst_caps_unref (caps);
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
  gint16 *data = (gint16 *) GST_BUFFER_DATA (outbuf);
  gint samples = GST_BUFFER_SIZE (outbuf) / 4;

  if (crossfeed->active) {
    bs2b_cross_feed_s16 (_bs2bdp, data, samples);
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
    "Improve headphone listening of stereo audio records",
    plugin_init,
    VERSION, "LGPL",
    "GStreamer",
    "http://gstreamer.net/"
);