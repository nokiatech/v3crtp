/*
 * Copyright (c) 2023 Nokia
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted (subject to the limitations in the disclaimer
 * below) provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * * Neither the name of Nokia nor the names of its contributors may be used to
 * endorse or promote products derived from this software without specific prior
 * written permission.
 *
 * NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE GRANTED BY
 * THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT
 * NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>

#include "gstrtpatlasdepay.h"
#include "gstrtputils.h"
#include <gst/base/gstbitreader.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <gst/video/video.h>

#define AP_NUT 56
#define FU_NUT 57

GST_DEBUG_CATEGORY_STATIC(rtpatlasdepay_debug);
#define GST_CAT_DEFAULT (rtpatlasdepay_debug)

#define DEFAULT_STREAM_FORMAT GST_ATLAS_STREAM_FORMAT_V3CG

static GstStaticPadTemplate gst_rtp_atlas_depay_src_template =
    GST_STATIC_PAD_TEMPLATE(
        "src", GST_PAD_SRC, GST_PAD_ALWAYS,
        GST_STATIC_CAPS("video/x-atlas, stream-format=(string) { v3cg, v3ag }, "
                        "alignment=(string)au, "
                        "codec_data=(string)ANY; "));

static GstStaticPadTemplate gst_rtp_atlas_depay_sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
                            GST_STATIC_CAPS("application/x-rtp, "
                                            "media = (string) \"application\", "
                                            "clock-rate = (int) 90000, "
                                            "encoding-name = (string) \"v3c\"")
                            /* optional parameters */
                            /* v3c-unit-header = (string) ANY,*/
                            /* v3c-unit-type = (int) [ 0, 31], */
                            /* v3c-vps-id = (int) [ 0, 15],*/
                            /* v3c-atlas-id = (int) [ 0, 65],*/
                            /* v3c-parameter-set = (string) ANY, */
                            /* v3c-tile-id = (int) [ 0, 1027], */
                            /* v3c-tile-id-pres = (int) [ 0, 1],*/
                            /* v3c-atlas-data = (string) ANY,*/
                            /* v3c-common-atlas-data = (string) ANY, */
                            /* v3c-sei = (string) ANY, */
                            /* v3c-ptl-level-idc = (int) [ 0, 65],*/
                            /* v3c-ptl-tier-flag = (int) [ 0, 1],*/
                            /* v3c-ptl-codec-idc = (int) [0, 127],*/
                            /* v3c-ptl-toolset-idc = (int) [0, 255], */
                            /* v3c-ptl-rec-idc = (int) [0, 255],*/
                            /* tx-mode = (string) {MRST , SRST},*/
                            /* sprop-max-don-diff */
    );

#define gst_rtp_atlas_depay_parent_class parent_class
G_DEFINE_TYPE(GstRtpAtlasDepay, gst_rtp_atlas_depay,
              GST_TYPE_RTP_BASE_DEPAYLOAD);

static void gst_rtp_atlas_depay_finalize(GObject *object);

static GstStateChangeReturn
gst_rtp_atlas_depay_change_state(GstElement *element,
                                 GstStateChange transition);

static GstBuffer *gst_rtp_atlas_depay_process(GstRTPBaseDepayload *depayload,
                                              GstRTPBuffer *rtp);
static gboolean gst_rtp_atlas_depay_setcaps(GstRTPBaseDepayload *filter,
                                            GstCaps *caps);
static gboolean gst_rtp_atlas_depay_handle_event(GstRTPBaseDepayload *depay,
                                                 GstEvent *event);
static GstBuffer *gst_rtp_atlas_complete_au(GstRtpAtlasDepay *rtpatlasdepay,
                                            GstClockTime *out_timestamp,
                                            gboolean *out_keyframe);
static void gst_rtp_atlas_depay_push(GstRtpAtlasDepay *rtpatlasdepay,
                                     GstBuffer *outbuf, gboolean keyframe,
                                     GstClockTime timestamp, gboolean marker);

static void gst_rtp_atlas_depay_class_init(GstRtpAtlasDepayClass *klass) {
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstRTPBaseDepayloadClass *gstrtpbasedepayload_class;

  gobject_class = (GObjectClass *)klass;
  gstelement_class = (GstElementClass *)klass;
  gstrtpbasedepayload_class = (GstRTPBaseDepayloadClass *)klass;

  gobject_class->finalize = gst_rtp_atlas_depay_finalize;

  gst_element_class_add_static_pad_template(gstelement_class,
                                            &gst_rtp_atlas_depay_src_template);
  gst_element_class_add_static_pad_template(gstelement_class,
                                            &gst_rtp_atlas_depay_sink_template);

  gst_element_class_set_static_metadata(
      gstelement_class, "RTP Atlas depayloader",
      "Codec/Depayloader/Network/RTP",
      "RTP Payload Format for Visual Volumetric Video-based Coding (V3C)",
      "Lukasz Kondrad <lukasz.kondrad@nokia.com>");
  gstelement_class->change_state = gst_rtp_atlas_depay_change_state;

  gstrtpbasedepayload_class->process_rtp_packet = gst_rtp_atlas_depay_process;
  gstrtpbasedepayload_class->set_caps = gst_rtp_atlas_depay_setcaps;
  gstrtpbasedepayload_class->handle_event = gst_rtp_atlas_depay_handle_event;

  GST_DEBUG_CATEGORY_INIT(rtpatlasdepay_debug, "rtpatlasdepay", 0,
                          "Atlas RTP Depayloader");
}

static void gst_rtp_atlas_depay_init(GstRtpAtlasDepay *rtpatlasdepay) {
  rtpatlasdepay->adapter = gst_adapter_new();
  rtpatlasdepay->atlas_frame_adapter = gst_adapter_new();
  rtpatlasdepay->output_format = DEFAULT_STREAM_FORMAT;
  rtpatlasdepay->stream_format = NULL;
  rtpatlasdepay->asps =
      g_ptr_array_new_with_free_func((GDestroyNotify)gst_buffer_unref);
  rtpatlasdepay->afps =
      g_ptr_array_new_with_free_func((GDestroyNotify)gst_buffer_unref);
  rtpatlasdepay->aaps =
      g_ptr_array_new_with_free_func((GDestroyNotify)gst_buffer_unref);
}

static void gst_rtp_atlas_depay_reset(GstRtpAtlasDepay *rtpatlasdepay,
                                      gboolean hard) {
  gst_adapter_clear(rtpatlasdepay->adapter);
  rtpatlasdepay->wait_start = TRUE;
  gst_adapter_clear(rtpatlasdepay->atlas_frame_adapter);
  rtpatlasdepay->atlas_frame_start = FALSE;
  rtpatlasdepay->last_keyframe = FALSE;
  rtpatlasdepay->last_ts = 0;
  rtpatlasdepay->current_fu_type = 0;
  rtpatlasdepay->new_codec_data = TRUE;
  g_ptr_array_set_size(rtpatlasdepay->asps, 0);
  g_ptr_array_set_size(rtpatlasdepay->afps, 0);
  g_ptr_array_set_size(rtpatlasdepay->aaps, 0);

  if (hard) {
    if (rtpatlasdepay->allocator != NULL) {
      gst_object_unref(rtpatlasdepay->allocator);
      rtpatlasdepay->allocator = NULL;
    }
    gst_allocation_params_init(&rtpatlasdepay->params);
  }
}

static void gst_rtp_atlas_depay_drain(GstRtpAtlasDepay *rtpatlasdepay) {
  GstClockTime timestamp;
  gboolean keyframe;
  GstBuffer *outbuf;

  if (!rtpatlasdepay->atlas_frame_start)
    return;

  outbuf = gst_rtp_atlas_complete_au(rtpatlasdepay, &timestamp, &keyframe);
  if (outbuf)
    gst_rtp_atlas_depay_push(rtpatlasdepay, outbuf, keyframe, timestamp, FALSE);
}

static void gst_rtp_atlas_depay_finalize(GObject *object) {
  GstRtpAtlasDepay *rtpatlasdepay;

  rtpatlasdepay = GST_RTP_ATLAS_DEPAY(object);

  if (rtpatlasdepay->codec_data)
    gst_buffer_unref(rtpatlasdepay->codec_data);

  g_object_unref(rtpatlasdepay->adapter);
  g_object_unref(rtpatlasdepay->atlas_frame_adapter);

  g_ptr_array_free(rtpatlasdepay->asps, TRUE);
  g_ptr_array_free(rtpatlasdepay->afps, TRUE);
  g_ptr_array_free(rtpatlasdepay->aaps, TRUE);

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static inline const gchar *stream_format_get_nick(GstAtlasStreamFormat fmt) {
  switch (fmt) {
  case GST_ATLAS_STREAM_FORMAT_V3CG:
    return "v3cg";
  default:
    break;
  }
  return "unknown";
}

static void gst_rtp_atlas_depay_negotiate(GstRtpAtlasDepay *rtpatlasdepay) {
  GstAtlasStreamFormat stream_format = GST_ATLAS_STREAM_FORMAT_UNKNOWN;
  GstCaps *caps;

  caps = gst_pad_get_allowed_caps(GST_RTP_BASE_DEPAYLOAD_SRCPAD(rtpatlasdepay));

  GST_DEBUG_OBJECT(rtpatlasdepay, "gst_rtp_atlas_depay_negotiate");
  GST_DEBUG_OBJECT(rtpatlasdepay, "allowed caps: %" GST_PTR_FORMAT, caps);

  if (caps) {
    if (gst_caps_get_size(caps) > 0) {
      GstStructure *s = gst_caps_get_structure(caps, 0);
      const gchar *str = NULL;

      if ((str = gst_structure_get_string(s, "stream-format"))) {
        rtpatlasdepay->stream_format = g_intern_string(str);

        if (strcmp(str, "v3cg") == 0) {
          stream_format = GST_ATLAS_STREAM_FORMAT_V3CG;
        } else {
          if (str == NULL)
            GST_DEBUG_OBJECT(rtpatlasdepay, "no stream-format ");
          else
            GST_DEBUG_OBJECT(rtpatlasdepay, "unknown stream-format: %s", str);
        }
      }

      if ((str = gst_structure_get_string(s, "alignment"))) {
        if (strcmp(str, "au") != 0) {
          GST_DEBUG_OBJECT(rtpatlasdepay, "unknown alignment: %s", str);
        }
      }
    }
    gst_caps_unref(caps);
  }

  if (stream_format != GST_ATLAS_STREAM_FORMAT_UNKNOWN) {
    GST_DEBUG_OBJECT(rtpatlasdepay, "downstream wants stream-format %s",
                     stream_format_get_nick(stream_format));
    rtpatlasdepay->output_format = stream_format;
  } else {
    GST_DEBUG_OBJECT(rtpatlasdepay, "defaulting to output stream-format %s",
                     stream_format_get_nick(DEFAULT_STREAM_FORMAT));
    rtpatlasdepay->stream_format =
        stream_format_get_nick(DEFAULT_STREAM_FORMAT);
    rtpatlasdepay->output_format = DEFAULT_STREAM_FORMAT;
  }
}

static gboolean
gst_rtp_atlas_depay_set_output_caps(GstRtpAtlasDepay *rtpatlasdepay,
                                    GstCaps *caps) {
  GstAllocationParams params;
  GstAllocator *allocator = NULL;
  GstPad *srcpad;
  gboolean res;

  gst_allocation_params_init(&params);

  srcpad = GST_RTP_BASE_DEPAYLOAD_SRCPAD(rtpatlasdepay);

  res = gst_pad_set_caps(srcpad, caps);

  if (res) {
    GstQuery *query;

    query = gst_query_new_allocation(caps, TRUE);
    if (!gst_pad_peer_query(srcpad, query)) {
      GST_DEBUG_OBJECT(rtpatlasdepay, "downstream ALLOCATION query failed");
    }

    if (gst_query_get_n_allocation_params(query) > 0) {
      gst_query_parse_nth_allocation_param(query, 0, &allocator, &params);
    }

    gst_query_unref(query);
  }

  if (rtpatlasdepay->allocator)
    gst_object_unref(rtpatlasdepay->allocator);

  rtpatlasdepay->allocator = allocator;
  rtpatlasdepay->params = params;

  return res;
}

static gboolean gst_rtp_atlas_set_src_caps(GstRtpAtlasDepay *rtpatlasdepay) {
  gboolean res, update_caps;
  GstCaps *old_caps;
  GstCaps *srccaps;
  GstPad *srcpad;

  GST_DEBUG_OBJECT(rtpatlasdepay, "rtpatlasdepay->new_codec_data %d",
                   rtpatlasdepay->new_codec_data);

  if (!rtpatlasdepay->new_codec_data){
    return TRUE;
  }

  srccaps = gst_caps_new_simple("video/x-atlas", "stream-format", G_TYPE_STRING,
                                rtpatlasdepay->stream_format, "alignment",
                                G_TYPE_STRING, "au", NULL);
  GstBuffer *codec_data;
  gint i = 0;
  gint len;
  guint num_asps = rtpatlasdepay->asps->len;
  guint num_afps = rtpatlasdepay->afps->len;
  guint num_aaps = rtpatlasdepay->aaps->len;
  GstMapInfo map, vpsmap;
  guint8 *data;
  guint8 num_arrays = 0;
  guint8 unit_size_precision_bytes_minus1 = 3;

  /* start with 1 bytes unit_size_precision_bytes_minus1,
   * num_of_v3c_parameter_sets = 0 */
  len = 1;

  if (rtpatlasdepay->vps){
    gst_buffer_map(rtpatlasdepay->vps, &map, GST_MAP_WRITE);
    /* 2 bytes for v3c_parameter_set_length*/
    len += 2;
    /* v3c_parameter_set size */
    len += map.size;
    gst_buffer_unmap(rtpatlasdepay->vps, &map);
  }

  /* 1 byte for num_of_setup_unit_arrays */
  len += 1;

  num_arrays = (num_asps > 0) + (num_afps > 0) + (num_aaps > 0);
  /* each array will have at least 2 bytes
    array_completeness, nal_unit_type, num_nal_units*/
  len += 2 * num_arrays;

  /* add size of asps, afps & aaps */
  for (i = 0; i < num_asps; i++) {
    /* 2 bytes for setup_unit_length and the unit itself */
    len += 2 + gst_buffer_get_size(g_ptr_array_index(rtpatlasdepay->asps, i));
  }
  for (i = 0; i < num_afps; i++) {
    /* 2 bytes for setup_unit_length and the unit itself */
    len += 2 + gst_buffer_get_size(g_ptr_array_index(rtpatlasdepay->afps, i));
  }
  for (i = 0; i < num_aaps; i++) {
    /* 2 bytes for setup_unit_length and the unit itself */
    len += 2 + gst_buffer_get_size(g_ptr_array_index(rtpatlasdepay->aaps, i));
  }

  GST_DEBUG_OBJECT(rtpatlasdepay, "codec_data length %u", (guint)len);

  codec_data = gst_buffer_new_and_alloc(len);
  gst_buffer_map(codec_data, &map, GST_MAP_READWRITE);
  data = map.data;

  memset(data, 0, map.size);
  data[0] = unit_size_precision_bytes_minus1 << 5;
  
  if (rtpatlasdepay->vps) {
    /* num_of_v3c_parameter_sets */
    data[0] = data[0] | 0x01;
    data++;
    gst_buffer_map(rtpatlasdepay->vps, &vpsmap, GST_MAP_READWRITE);

    /* v3c_parameter_set_length */
    GST_WRITE_UINT16_BE(data, vpsmap.size);
    data += 2;
    memcpy(data, vpsmap.data, vpsmap.size);
    data += vpsmap.size;

    GST_DEBUG_OBJECT(rtpatlasdepay, "Copied VPS of length %u",
                     (guint)vpsmap.size);

    gst_buffer_unmap(rtpatlasdepay->vps, &vpsmap);
  }

  /* num_of_setup_unit_arrays */
  data[0] = num_arrays;

  GST_DEBUG_OBJECT(rtpatlasdepay, "num of arrays %d ", num_arrays);

  if (num_asps > 0) {
    /* array_completeness | reserved_zero bit | nal_unit_type */
    data[0] = 0x00 | 0x24;
    data++;

    data[0] = num_asps;
    data += 1;

    for (i = 0; i < num_asps; i++) {
      gsize nal_size =
          gst_buffer_get_size(g_ptr_array_index(rtpatlasdepay->asps, i));
      GST_WRITE_UINT16_BE(data, nal_size);
      gst_buffer_extract(g_ptr_array_index(rtpatlasdepay->asps, i), 0, data + 2,
                         nal_size);
      data += 2 + nal_size;
      GST_DEBUG_OBJECT(rtpatlasdepay, "Copied ASPS %d of length %u", i,
                       (guint)nal_size);
    }
  }

  if (num_afps > 0) {
    /* array_completeness | reserved_zero bit | nal_unit_type */
    data[0] = 0x00 | 0x25;
    data++;

    data[0] = num_afps;
    data += 1;

    for (i = 0; i < num_afps; i++) {
      gsize nal_size =
          gst_buffer_get_size(g_ptr_array_index(rtpatlasdepay->afps, i));
      GST_WRITE_UINT16_BE(data, nal_size);
      gst_buffer_extract(g_ptr_array_index(rtpatlasdepay->afps, i), 0, data + 2,
                         nal_size);
      data += 2 + nal_size;
      GST_DEBUG_OBJECT(rtpatlasdepay, "Copied AFPS %d of length %u", i,
                       (guint)nal_size);
    }
  }

  if (num_aaps > 0) {
    /* array_completeness | reserved_zero bit | nal_unit_type */
    data[0] = 0x00 | 0x2F;
    data++;

    data[0] = num_aaps;
    data += 1;

    for (i = 0; i < num_aaps; i++) {
      gsize nal_size =
          gst_buffer_get_size(g_ptr_array_index(rtpatlasdepay->aaps, i));
      GST_WRITE_UINT16_BE(data, nal_size);
      gst_buffer_extract(g_ptr_array_index(rtpatlasdepay->aaps, i), 0,
                         data + 2, nal_size);
      data += 2 + nal_size;
      GST_DEBUG_OBJECT(rtpatlasdepay, "Copied AAPS %d of length %u", i,
                       (guint)nal_size);
    }
  }

  if (rtpatlasdepay->vuh != NULL) {
    gst_caps_set_simple(srccaps, "vuh_data", GST_TYPE_BUFFER,
                        rtpatlasdepay->vuh, NULL);
  }

  gst_caps_set_simple(srccaps, "codec_data", GST_TYPE_BUFFER, codec_data, NULL);
  gst_buffer_unmap(codec_data, &map);
  gst_buffer_unref(codec_data);

  srcpad = GST_RTP_BASE_DEPAYLOAD_SRCPAD(rtpatlasdepay);

  old_caps = gst_pad_get_current_caps(srcpad);
  if (old_caps != NULL) {

    GstCaps *tmp_caps = gst_caps_copy(srccaps);
    GstStructure *old_s, *tmp_s;

    old_s = gst_caps_get_structure(old_caps, 0);
    tmp_s = gst_caps_get_structure(tmp_caps, 0);
    if (gst_structure_has_field(old_s, "codec_data"))
      gst_structure_set_value(tmp_s, "codec_data",
                              gst_structure_get_value(old_s, "codec_data"));

    update_caps = !gst_caps_is_equal(old_caps, tmp_caps);
    gst_caps_unref(tmp_caps);
    gst_caps_unref(old_caps);
  } else {
    update_caps = TRUE;
  }

  if (update_caps) {
    res = gst_rtp_atlas_depay_set_output_caps(rtpatlasdepay, srccaps);
  } else {
    res = TRUE;
  }

  gst_caps_unref(srccaps);

  if (res)
    rtpatlasdepay->new_codec_data = FALSE;

  return res;
}

static gboolean gst_rtp_atlas_depay_setcaps(GstRTPBaseDepayload *depayload,
                                            GstCaps *caps) {
  gint clock_rate;
  GstStructure *structure = gst_caps_get_structure(caps, 0);
  GstRtpAtlasDepay *rtpatlasdepay;
  const gchar *ad_base64 = NULL;
  const gchar *vps_base64 = NULL;
  const gchar *vuh_base64 = NULL;
  GstMapInfo map;

  rtpatlasdepay = GST_RTP_ATLAS_DEPAY(depayload);

  if (!gst_structure_get_int(structure, "clock-rate", &clock_rate))
    clock_rate = 90000;
  depayload->clock_rate = clock_rate;

  vps_base64 = gst_structure_get_string(structure, "v3c-parameter-set");
  if (vps_base64) {
    gsize size;
    guchar *vps = g_base64_decode(vps_base64, &size);

    rtpatlasdepay->vps = gst_buffer_new_allocate(NULL, size, NULL);
    gst_buffer_map(rtpatlasdepay->vps, &map, GST_MAP_WRITE);
    memcpy(map.data, vps, size);
    gst_buffer_unmap(rtpatlasdepay->vps, &map);
  }

  /* Base64 encoded, comma separated config NALs */
  ad_base64 = gst_structure_get_string(structure, "v3c-atlas-data");
  if (ad_base64) {
    gchar **params_base64;
    gint i = 0;
    gsize size;

    params_base64 = g_strsplit(ad_base64, ",", 0);

    for (i = 0; params_base64[i]; i++) {
      gchar param_type = 0;
      guchar *param = g_base64_decode(params_base64[i], &size);
      param_type = (param[0] & 0x7E) >> 1;
      if(param_type == GST_ATLAS_NAL_ASPS) {
        GST_DEBUG_OBJECT(rtpatlasdepay, "got ASPS of size %ld on v3c-atlas-data",
                         size);
        GstBuffer *asps = gst_buffer_new_allocate(NULL, size, NULL);
        gst_buffer_map(asps, &map, GST_MAP_WRITE);
        memcpy(map.data, param, size);
        gst_buffer_unmap(asps, &map);
        g_ptr_array_add(rtpatlasdepay->asps, asps);

      } else if (param_type == GST_ATLAS_NAL_AFPS) {
        GST_DEBUG_OBJECT(rtpatlasdepay, "got AFPS of size %ld on v3c-atlas-data",
                         size);
        GstBuffer *afps = gst_buffer_new_allocate(NULL, size, NULL);
        gst_buffer_map(afps, &map, GST_MAP_WRITE);
        memcpy(map.data, param, size);
        gst_buffer_unmap(afps, &map);
        g_ptr_array_add(rtpatlasdepay->afps, afps);
      } else {
        GST_WARNING_OBJECT(rtpatlasdepay, "got Setup Unit on v3c-atlas-data that is not implemented");
      }
    }
  }

  vuh_base64 = gst_structure_get_string(structure, "v3c-unit-header");
  if (vuh_base64) {
    gsize size;
    guchar *vuh = g_base64_decode(vuh_base64, &size);
    if (size != 4) {
      GST_ERROR_OBJECT(rtpatlasdepay,
                       "V3C unit header size %" G_GSIZE_FORMAT "!= 4", size);
      return TRUE;
    }
    rtpatlasdepay->vuh = gst_buffer_new_allocate(NULL, size, NULL);
    gst_buffer_map(rtpatlasdepay->vuh, &map, GST_MAP_WRITE);
    memcpy(map.data, vuh, size);
    gst_buffer_unmap(rtpatlasdepay->vuh, &map);
  }

  /* negotiate with downstream w.r.t. output format and alignment */
  gst_rtp_atlas_depay_negotiate(rtpatlasdepay);

  return gst_rtp_atlas_set_src_caps(rtpatlasdepay);
}

static GstBuffer *
gst_rtp_atlas_depay_allocate_output_buffer(GstRtpAtlasDepay *depay,
                                           gsize size) {
  GstBuffer *buffer = NULL;

  g_return_val_if_fail(size > 0, NULL);

  GST_LOG_OBJECT(depay, "want output buffer of %u bytes", (guint)size);

  buffer = gst_buffer_new_allocate(depay->allocator, size, &depay->params);
  if (buffer == NULL) {
    GST_INFO_OBJECT(depay, "couldn't allocate output buffer");
    buffer = gst_buffer_new_allocate(NULL, size, NULL);
  }

  return buffer;
}

static GstBuffer *gst_rtp_atlas_complete_au(GstRtpAtlasDepay *rtpatlasdepay,
                                            GstClockTime *out_timestamp,
                                            gboolean *out_keyframe) {
  GstBufferList *list;
  GstMapInfo outmap;
  GstBuffer *outbuf;
  guint outsize, offset = 0;
  gint b, n_bufs, m, n_mem;

  /* we had a atlas frame in the adapter and we completed it */
  GST_DEBUG_OBJECT(rtpatlasdepay, "taking completed AU");
  outsize = gst_adapter_available(rtpatlasdepay->atlas_frame_adapter);

  outbuf = gst_rtp_atlas_depay_allocate_output_buffer(rtpatlasdepay, outsize);

  if (outbuf == NULL)
    return NULL;

  if (!gst_buffer_map(outbuf, &outmap, GST_MAP_WRITE))
    return NULL;

  list =
      gst_adapter_take_buffer_list(rtpatlasdepay->atlas_frame_adapter, outsize);

  n_bufs = gst_buffer_list_length(list);
  for (b = 0; b < n_bufs; ++b) {
    GstBuffer *buf = gst_buffer_list_get(list, b);

    n_mem = gst_buffer_n_memory(buf);
    for (m = 0; m < n_mem; ++m) {
      GstMemory *mem = gst_buffer_peek_memory(buf, m);
      gsize mem_size = gst_memory_get_sizes(mem, NULL, NULL);
      GstMapInfo mem_map;

      if (gst_memory_map(mem, &mem_map, GST_MAP_READ)) {
        memcpy(outmap.data + offset, mem_map.data, mem_size);
        gst_memory_unmap(mem, &mem_map);
      } else {
        memset(outmap.data + offset, 0, mem_size);
      }
      offset += mem_size;
    }

    gst_rtp_copy_video_meta(rtpatlasdepay, outbuf, buf);
  }
  gst_buffer_list_unref(list);
  gst_buffer_unmap(outbuf, &outmap);

  *out_timestamp = rtpatlasdepay->last_ts;
  *out_keyframe = rtpatlasdepay->last_keyframe;

  rtpatlasdepay->last_keyframe = FALSE;
  rtpatlasdepay->atlas_frame_start = FALSE;

  return outbuf;
}

/* ASPS/AFPS/AAPS/RADL/TSA/RASL/IDR/CRA is considered key, all others DELTA;
 * so downstream waiting for keyframe can pick up at ASPS/AFPS/AAPS/IDR */

#define NAL_TYPE_IS_PARAMETER_SET(nt)                                          \
  (((nt) == GST_ATLAS_NAL_ASPS) || ((nt) == GST_ATLAS_NAL_AFPS) ||             \
   ((nt) == GST_ATLAS_NAL_AAPS))

#define NAL_TYPE_IS_CODED_ATLAS_TILE_SEGMENT(nt)                               \
  (((nt) == GST_ATLAS_NAL_TRAIL_N) || ((nt) == GST_ATLAS_NAL_TRAIL_R) ||       \
   ((nt) == GST_ATLAS_NAL_TSA_N) || ((nt) == GST_ATLAS_NAL_TSA_R) ||           \
   ((nt) == GST_ATLAS_NAL_STSA_N) || ((nt) == GST_ATLAS_NAL_STSA_R) ||         \
   ((nt) == GST_ATLAS_NAL_RASL_N) || ((nt) == GST_ATLAS_NAL_RASL_R) ||         \
   ((nt) == GST_ATLAS_NAL_SKIP_N) || ((nt) == GST_ATLAS_NAL_SKIP_R) ||         \
   ((nt) == GST_ATLAS_NAL_BLA_W_LP) || ((nt) == GST_ATLAS_NAL_BLA_W_RADL) ||   \
   ((nt) == GST_ATLAS_NAL_BLA_N_LP) || ((nt) == GST_ATLAS_NAL_GBLA_W_LP) ||    \
   ((nt) == GST_ATLAS_NAL_GBLA_W_RADL) || ((nt) == GST_ATLAS_NAL_GBLA_N_LP) || \
   ((nt) == GST_ATLAS_NAL_GIDR_W_RADL) || ((nt) == GST_ATLAS_NAL_GIDR_N_LP) || \
   ((nt) == GST_ATLAS_NAL_GCRA) || ((nt) == GST_ATLAS_NAL_IDR_W_RADL) ||       \
   ((nt) == GST_ATLAS_NAL_IDR_N_LP) || ((nt) == GST_ATLAS_NAL_CRA))

/* Intra random access point */
#define NAL_TYPE_IS_IRAP(nt)                                                   \
  (((nt) == GST_ATLAS_NAL_BLA_W_LP) || ((nt) == GST_ATLAS_NAL_BLA_W_RADL) ||   \
   ((nt) == GST_ATLAS_NAL_BLA_N_LP) || ((nt) == GST_ATLAS_NAL_IDR_W_RADL) ||   \
   ((nt) == GST_ATLAS_NAL_IDR_N_LP) || ((nt) == GST_ATLAS_NAL_CRA))

#define NAL_TYPE_IS_KEY(nt)                                                    \
  (NAL_TYPE_IS_PARAMETER_SET(nt) || NAL_TYPE_IS_IRAP(nt))

static void gst_rtp_atlas_depay_push(GstRtpAtlasDepay *rtpatlasdepay,
                                     GstBuffer *outbuf, gboolean keyframe,
                                     GstClockTime timestamp, gboolean marker) {
  /* prepend codec_data */
  if (rtpatlasdepay->codec_data) {
    GST_DEBUG_OBJECT(rtpatlasdepay, "prepending codec_data");
    gst_rtp_copy_video_meta(rtpatlasdepay, rtpatlasdepay->codec_data, outbuf);
    outbuf = gst_buffer_append(rtpatlasdepay->codec_data, outbuf);
    rtpatlasdepay->codec_data = NULL;
    keyframe = TRUE;
  }
  outbuf = gst_buffer_make_writable(outbuf);

  gst_rtp_drop_non_video_meta(rtpatlasdepay, outbuf);

  GST_BUFFER_PTS(outbuf) = timestamp;

  if (keyframe)
    GST_BUFFER_FLAG_UNSET(outbuf, GST_BUFFER_FLAG_DELTA_UNIT);
  else
    GST_BUFFER_FLAG_SET(outbuf, GST_BUFFER_FLAG_DELTA_UNIT);

  if (marker)
    GST_BUFFER_FLAG_SET(outbuf, GST_BUFFER_FLAG_MARKER);

  gst_rtp_base_depayload_push(GST_RTP_BASE_DEPAYLOAD(rtpatlasdepay), outbuf);
}

static void gst_rtp_atlas_depay_handle_nal(GstRtpAtlasDepay *rtpatlasdepay,
                                           GstBuffer *nal,
                                           GstClockTime in_timestamp,
                                           gboolean marker) {
  GstRTPBaseDepayload *depayload = GST_RTP_BASE_DEPAYLOAD(rtpatlasdepay);
  gint nal_type;
  GstMapInfo map;
  GstBuffer *outbuf = NULL;
  GstClockTime out_timestamp;
  gboolean keyframe, out_keyframe;

  gst_buffer_map(nal, &map, GST_MAP_READ);
  if (G_UNLIKELY(map.size < 5))
    goto short_nal;

  nal_type = (map.data[4] >> 1) & 0x3f;
  GST_DEBUG_OBJECT(rtpatlasdepay, "handle NAL type %d (RTP marker bit %d)",
                   nal_type, marker);

  keyframe = NAL_TYPE_IS_KEY(nal_type);

  out_keyframe = keyframe;
  out_timestamp = in_timestamp;

  gboolean start = FALSE, complete = FALSE;

  /* detect an AU boundary (see ISO/IEC 23090-5 section 8.4.5.2) */
  if (!marker) {
    if (NAL_TYPE_IS_CODED_ATLAS_TILE_SEGMENT(nal_type)) {
      /* A NAL unit (X) ends an access unit if the next-occurring ACL NAL unit
       * (Y) has the high-order bit of the first byte after its NAL unit
       * header equal to 1 */
      start = TRUE;
      if (((map.data[6] >> 7) & 0x01) == 1) {
        complete = TRUE;
      }
    } else if ((nal_type >= GST_ATLAS_NAL_ASPS &&
                nal_type <= GST_ATLAS_NAL_AUD) ||
               nal_type == GST_ATLAS_NAL_PREFIX_NSEI ||
               nal_type == GST_ATLAS_NAL_PREFIX_ESEI ||
               nal_type == GST_ATLAS_NAL_AAPS ) {
      /* ASPS, AFPS, AAPS, SEI, ... terminate an access unit */
      complete = TRUE;
    }
    GST_DEBUG_OBJECT(depayload, "start %d, complete %d", start, complete);

    if (complete && rtpatlasdepay->atlas_frame_start)
      outbuf = gst_rtp_atlas_complete_au(rtpatlasdepay, &out_timestamp,
                                         &out_keyframe);
  }
  /* add to adapter */
  gst_buffer_unmap(nal, &map);

  GST_DEBUG_OBJECT(depayload, "adding NAL to atlas frame adapter");
  gst_adapter_push(rtpatlasdepay->atlas_frame_adapter, nal);
  rtpatlasdepay->last_ts = in_timestamp;
  rtpatlasdepay->last_keyframe |= keyframe;
  rtpatlasdepay->atlas_frame_start |= start;

  if (marker)
    outbuf =
        gst_rtp_atlas_complete_au(rtpatlasdepay, &out_timestamp, &out_keyframe);
  if (outbuf) {
    gst_rtp_atlas_depay_push(rtpatlasdepay, outbuf, out_keyframe, out_timestamp,
                             marker);
  }

  return;

  /* ERRORS */
short_nal : {
  GST_WARNING_OBJECT(depayload, "dropping short NAL");
  gst_buffer_unmap(nal, &map);
  gst_buffer_unref(nal);
  return;
}
}

static void
gst_rtp_atlas_finish_fragmentation_unit(GstRtpAtlasDepay *rtpatlasdepay) {
  guint outsize;
  GstMapInfo map;
  GstBuffer *outbuf;

  outsize = gst_adapter_available(rtpatlasdepay->adapter);
  g_assert(outsize >= 4);

  outbuf = gst_adapter_take_buffer(rtpatlasdepay->adapter, outsize);

  gst_buffer_map(outbuf, &map, GST_MAP_WRITE);
  GST_DEBUG_OBJECT(rtpatlasdepay, "output %d bytes", outsize);
  GST_WRITE_UINT32_BE(map.data, outsize - 4);
  gst_buffer_unmap(outbuf, &map);

  rtpatlasdepay->current_fu_type = 0;

  gst_rtp_atlas_depay_handle_nal(rtpatlasdepay, outbuf,
                                 rtpatlasdepay->fu_timestamp,
                                 rtpatlasdepay->fu_marker);
}

static GstBuffer *gst_rtp_atlas_depay_process(GstRTPBaseDepayload *depayload,
                                              GstRTPBuffer *rtp) {
  GstRtpAtlasDepay *rtpatlasdepay;
  GstBuffer *outbuf = NULL;
  guint8 nal_unit_type;

  rtpatlasdepay = GST_RTP_ATLAS_DEPAY(depayload);

  GST_DEBUG_OBJECT(rtpatlasdepay, "gst_rtp_atlas_depay_process");

  /* flush remaining data on discont */
  if (GST_BUFFER_IS_DISCONT(rtp->buffer)) {
    gst_adapter_clear(rtpatlasdepay->adapter);
    rtpatlasdepay->wait_start = TRUE;
    rtpatlasdepay->current_fu_type = 0;
    rtpatlasdepay->last_fu_seqnum = 0;
  }

  {
    gint payload_len;
    guint8 *payload;
    guint header_len;
    GstMapInfo map;
    guint outsize, nalu_size;
    GstClockTime timestamp;
    gboolean marker;
    guint8 nal_layer_id, nal_temporal_id_plus1;
    guint8 S, E;
    guint16 nal_header;
    timestamp = GST_BUFFER_PTS(rtp->buffer);

    payload_len = gst_rtp_buffer_get_payload_len(rtp);
    payload = gst_rtp_buffer_get_payload(rtp);
    marker = gst_rtp_buffer_get_marker(rtp);

    GST_DEBUG_OBJECT(rtpatlasdepay, "receiving %d bytes", payload_len);

    if (payload_len == 0)
      goto empty_packet;

    /* +---------------+---------------+
     * |0|1|2|3|4|5|6|7|0|1|2|3|4|5|6|7|
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     * |F|   Type    |  LayerId  | TID |
     * +-------------+-----------------+
     *
     * F must be 0.
     *
     */
    nal_unit_type = (payload[0] >> 1) & 0x3f;
    nal_layer_id = ((payload[0] & 0x01) << 5) |
                   (payload[1] >> 3); /* should be zero for now but this could
                                         change in future HEVC extensions */
    nal_temporal_id_plus1 = payload[1] & 0x03;

    /* At least two byte header with type */
    header_len = 2;

    GST_DEBUG_OBJECT(rtpatlasdepay,
                     "NAL header nal_unit_type %d, nal_temporal_id_plus1 %d",
                     nal_unit_type, nal_temporal_id_plus1);

    GST_FIXME_OBJECT(rtpatlasdepay, "Assuming DONL field is not present");

    /* FIXME - assuming DONL field is not present for now */
    /*(tx-mode == "SRST" sprop-max-don-diff = 0 */

    /* FIXME - assuming v3c-tile-id field is not present for now */
    /*v3c-tile-id-pres = 0 */

    /* If FU unit was being processed, but the current nal is of a different
     * type.  Assume that the remote payloader is buggy (didn't set the end bit
     * when the FU ended) and send out what we gathered thusfar */
    if (G_UNLIKELY(rtpatlasdepay->current_fu_type != 0 &&
                   nal_unit_type != rtpatlasdepay->current_fu_type))
      gst_rtp_atlas_finish_fragmentation_unit(rtpatlasdepay);

    switch (nal_unit_type) {
    case AP_NUT: {
      GST_DEBUG_OBJECT(rtpatlasdepay, "Processing aggregation packet");

      /* Aggregation packet (section 5.5.3) */

      /*  An example of an AP packet containing two aggregation units
         without the DONL and DOND fields and without v3c-tile-id

         0                   1                   2                   3
         0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
         +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         |  RTP payload header (NUT=56)  |         NALU 1 Size           |
         +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         |          NALU 1 HDR           |                               |
         +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+         NALU 1 Data           |
         |                   . . .                                       |
         |                                                               |
         +               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         |  . . .        | NALU 2 Size                   | NALU 2 HDR    |
         +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         | NALU 2 HDR    |                                               |
         +-+-+-+-+-+-+-+-+              NALU 2 Data                      |
         |                   . . .                                       |
         |                               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         |                               :...OPTIONAL RTP padding        |
         +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       */

      /* strip headers */
      payload += header_len;
      payload_len -= header_len;

      rtpatlasdepay->wait_start = FALSE;

      while (payload_len > 2) {
        gboolean last = FALSE;

        nalu_size = (payload[0] << 8) | payload[1];

        /* don't include nalu_size two bytes from the packet */
        if (nalu_size > (payload_len - 2))
          nalu_size = payload_len - 2;

        /* but reserve 4 bytes for the nalu_size value */
        outsize = nalu_size + 4;
        outbuf = gst_buffer_new_and_alloc(outsize);

        gst_buffer_map(outbuf, &map, GST_MAP_WRITE);
        GST_WRITE_UINT32_BE(map.data, nalu_size);

        /* strip NALU size */
        payload += 2;
        payload_len -= 2;

        memcpy(map.data + 4, payload, nalu_size);
        gst_buffer_unmap(outbuf, &map);

        gst_rtp_copy_video_meta(rtpatlasdepay, outbuf, rtp->buffer);

        if (payload_len - nalu_size <= 2)
          last = TRUE;

        gst_rtp_atlas_depay_handle_nal(rtpatlasdepay, outbuf, timestamp,
                                       marker && last);

        payload += nalu_size;
        payload_len -= nalu_size;
      }
      break;
    }
    case FU_NUT: {
      GST_DEBUG_OBJECT(rtpatlasdepay, "Processing Fragmentation Unit");

      /* Fragmentation units (FUs)  Section 5.5.4 */

      /*    The structure of a Fragmentation Unit (FU)
       0                   1                   2                   3
       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |  RTP payload header (NUT=56)   |   FU header   | DONL (cond)  |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-|
       |  DONL (cond)  |    v3c-tile-id (cond)         |               |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ |
       |                         FU payload                            |
       |                                                               |
       |                               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                               :...OPTIONAL RTP padding        |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       */

      /* strip headers */
      payload += header_len;
      payload_len -= header_len;

      /* processing FU header */
      S = (payload[0] & 0x80) == 0x80;
      E = (payload[0] & 0x40) == 0x40;

      GST_DEBUG_OBJECT(rtpatlasdepay,
                       "FU header with S %d, E %d, nal_unit_type %d", S, E,
                       payload[0] & 0x3f);

      if (rtpatlasdepay->wait_start && !S)
        goto waiting_start;

      if (S) {

        GST_DEBUG_OBJECT(rtpatlasdepay, "Start of Fragmentation Unit");

        /* If a new FU unit started, while still processing an older one.
         * Assume that the remote payloader is buggy (doesn't set the end
         * bit) and send out what we've gathered thusfar */
        if (G_UNLIKELY(rtpatlasdepay->current_fu_type != 0))
          gst_rtp_atlas_finish_fragmentation_unit(rtpatlasdepay);

        rtpatlasdepay->current_fu_type = nal_unit_type;
        rtpatlasdepay->fu_timestamp = timestamp;
        rtpatlasdepay->last_fu_seqnum = gst_rtp_buffer_get_seq(rtp);

        rtpatlasdepay->wait_start = FALSE;

        /* reconstruct NAL header */
        nal_header = ((payload[0] & 0x3f) << 9) | (nal_layer_id << 3) |
                     nal_temporal_id_plus1;

        /* go back one byte so we can copy the payload + two bytes more in the
         * front, the extra two bytes will be overwritten by the nal_header
         */
        payload -= 1;
        payload_len += 1;

        nalu_size = payload_len;
        outsize = nalu_size + 4;
        outbuf = gst_buffer_new_and_alloc(outsize);

        gst_buffer_map(outbuf, &map, GST_MAP_WRITE);
        // dummy value that will be overiten later on by the real size
        GST_WRITE_UINT32_BE(map.data, 0xffffffff);

        memcpy(map.data + 4, payload, nalu_size);
        map.data[4] = nal_header >> 8;
        map.data[5] = nal_header & 0xff;
        gst_buffer_unmap(outbuf, &map);

        gst_rtp_copy_video_meta(rtpatlasdepay, outbuf, rtp->buffer);

        GST_DEBUG_OBJECT(rtpatlasdepay, "queueing %d bytes", outsize);

        /* and assemble in the adapter */
        gst_adapter_push(rtpatlasdepay->adapter, outbuf);
      } else {
        if (rtpatlasdepay->current_fu_type == 0) {
          /* previous FU packet missing start bit? */
          GST_WARNING_OBJECT(rtpatlasdepay, "missing FU start bit on an "
                                            "earlier packet. Dropping.");
          gst_adapter_clear(rtpatlasdepay->adapter);
          return NULL;
        }
        if (gst_rtp_buffer_compare_seqnum(rtpatlasdepay->last_fu_seqnum,
                                          gst_rtp_buffer_get_seq(rtp)) != 1) {
          /* jump in sequence numbers within an FU is cause for discarding */
          GST_WARNING_OBJECT(
              rtpatlasdepay,
              "Jump in sequence numbers from "
              "%u to %u within Fragmentation Unit. Data was lost, dropping "
              "stored.",
              rtpatlasdepay->last_fu_seqnum, gst_rtp_buffer_get_seq(rtp));
          gst_adapter_clear(rtpatlasdepay->adapter);
          return NULL;
        }
        rtpatlasdepay->last_fu_seqnum = gst_rtp_buffer_get_seq(rtp);

        GST_DEBUG_OBJECT(rtpatlasdepay, "Following part of Fragmentation Unit");

        /* strip off FU header byte */
        payload += 1;
        payload_len -= 1;

        outsize = payload_len;
        outbuf = gst_buffer_new_and_alloc(outsize);
        gst_buffer_fill(outbuf, 0, payload, outsize);

        gst_rtp_copy_video_meta(rtpatlasdepay, outbuf, rtp->buffer);

        GST_DEBUG_OBJECT(rtpatlasdepay, "queueing %d bytes", outsize);

        /* and assemble in the adapter */
        gst_adapter_push(rtpatlasdepay->adapter, outbuf);
      }

      outbuf = NULL;
      rtpatlasdepay->fu_marker = marker;

      /* if NAL unit ends, flush the adapter */
      if (E) {
        gst_rtp_atlas_finish_fragmentation_unit(rtpatlasdepay);
        GST_DEBUG_OBJECT(rtpatlasdepay, "End of Fragmentation Unit");
      }
      break;
    }
    default: {
      rtpatlasdepay->wait_start = FALSE;
      /* 5.5.2. Single NAL unit packet*/
      /* the entire payload is the output buffer */

      /*
          0                   1                   2                   3
      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |      RTP payload header       |      DONL (conditional)       |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-|
      |      v3c-tile-id (cond)       |                               |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+                               |
      |                                                               |
      |                        NAL unit data                          |
      |                                                               |
      |                               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                               :...OPTIONAL RTP padding        |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      */

      nalu_size = payload_len;
      outsize = nalu_size + 4;
      outbuf = gst_buffer_new_and_alloc(outsize);

      gst_buffer_map(outbuf, &map, GST_MAP_WRITE);
      GST_WRITE_UINT32_BE(map.data, nalu_size);
      memcpy(map.data + 4, payload, nalu_size);
      gst_buffer_unmap(outbuf, &map);

      gst_rtp_copy_video_meta(rtpatlasdepay, outbuf, rtp->buffer);

      gst_rtp_atlas_depay_handle_nal(rtpatlasdepay, outbuf, timestamp, marker);
      break;
    }
    }
  }
  return NULL;

  /* ERRORS */
empty_packet : {
  GST_DEBUG_OBJECT(rtpatlasdepay, "empty packet");
  return NULL;
}
waiting_start : {
  GST_DEBUG_OBJECT(rtpatlasdepay, "waiting for start");
  return NULL;
}
}

static gboolean gst_rtp_atlas_depay_handle_event(GstRTPBaseDepayload *depay,
                                                 GstEvent *event) {
  GstRtpAtlasDepay *rtpatlasdepay;

  rtpatlasdepay = GST_RTP_ATLAS_DEPAY(depay);

  switch (GST_EVENT_TYPE(event)) {
  case GST_EVENT_FLUSH_STOP:
    gst_rtp_atlas_depay_reset(rtpatlasdepay, FALSE);
    break;
  case GST_EVENT_EOS:
    gst_rtp_atlas_depay_drain(rtpatlasdepay);
    break;
  default:
    break;
  }

  return GST_RTP_BASE_DEPAYLOAD_CLASS(parent_class)->handle_event(depay, event);
}

static GstStateChangeReturn
gst_rtp_atlas_depay_change_state(GstElement *element,
                                 GstStateChange transition) {
  GstRtpAtlasDepay *rtpatlasdepay;
  GstStateChangeReturn ret;

  rtpatlasdepay = GST_RTP_ATLAS_DEPAY(element);

  GST_DEBUG_OBJECT(rtpatlasdepay, "gst_rtp_atlas_depay_change_state");

  switch (transition) {
  case GST_STATE_CHANGE_NULL_TO_READY:
    break;
  case GST_STATE_CHANGE_READY_TO_PAUSED:
    gst_rtp_atlas_depay_reset(rtpatlasdepay, TRUE);
    break;
  default:
    break;
  }

  ret = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);

  switch (transition) {
  case GST_STATE_CHANGE_PAUSED_TO_READY:
    gst_rtp_atlas_depay_reset(rtpatlasdepay, TRUE);
    break;
  case GST_STATE_CHANGE_READY_TO_NULL:
    break;
  default:
    break;
  }
  return ret;
}

gboolean gst_rtp_atlas_depay_plugin_init(GstPlugin *plugin) {
  return gst_element_register(plugin, "rtpatlasdepay", GST_RANK_SECONDARY,
                              GST_TYPE_RTP_ATLAS_DEPAY);
}
