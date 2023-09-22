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

#include <stdlib.h>
#include <string.h>

#include <gst/pbutils/pbutils.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <gst/video/video.h>

#include "gstrtpatlasdepay.h"

#include "gstbuffermemory.h"
#include "gstrtpatlaspay.h"
#include "gstrtputils.h"
#include "utils.h"

#define AP_NUT 56
#define FU_NUT 57

GST_DEBUG_CATEGORY_STATIC(rtpatlaspay_debug);
#define GST_CAT_DEFAULT (rtpatlaspay_debug)

#define GST_TYPE_RTP_ATLAS_AGGREGATE_MODE                                      \
  (gst_rtp_atlas_aggregate_mode_get_type())

static GType gst_rtp_atlas_aggregate_mode_get_type(void) {
  static GType type = 0;
  static const GEnumValue values[] = {
      {GST_RTP_ATLAS_AGGREGATE_NONE, "Do not aggregate NAL units", "none"},
      {GST_RTP_ATLAS_AGGREGATE_ZERO_LATENCY,
       "Aggregate NAL units until a ACL or suffix unit is included",
       "zero-latency"},
      {GST_RTP_ATLAS_AGGREGATE_MAX,
       "Aggregate all NAL units with the same timestamp (adds one frame of"
       " latency)",
       "max"},
      {0, NULL, NULL},
  };

  if (!type) {
    type = g_enum_register_static("GstRtpAtlasAggregateMode", values);
  }
  return type;
}

/* From ISO/IEC 23090-10
* When the V3C bitstream contains a single atlas,
  a V3C atlas track with sample entry 'v3c1' or 'v3cg' shall be used.
* Under the 'v3cg' and 'v3cb' sample entry, the parameter sets and SEI
  messages may be present in the setup_unit array, or in the
  samples of V3C atlas track.
*/
static GstStaticPadTemplate gst_rtp_atlas_pay_sink_template =
    GST_STATIC_PAD_TEMPLATE(
        "sink", GST_PAD_SINK, GST_PAD_ALWAYS,
        GST_STATIC_CAPS("video/x-atlas, stream-format = (string) { v3cg, v3ag },"
                        "alignment = (string)au; ")
        //                "codec_data=(string)ANY; "
        /* optional parameters */
        /* vuh_data = (string) ANY,*/
    );

static GstStaticPadTemplate gst_rtp_atlas_pay_src_template =
    GST_STATIC_PAD_TEMPLATE(
        "src", GST_PAD_SRC, GST_PAD_ALWAYS,
        GST_STATIC_CAPS("application/x-rtp, "
                        "media = (string) \"application\", "
                        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
                        "clock-rate = (int) 90000, "
                        "encoding-name = (string) \"v3c\"")
        /* optional parameters */
        /* v3c-unit-header = (string) ANY,*/
        /* v3c-unit-type = (int) [ 0, 31], */
        /* v3c-vps-id = (int) [ 0, 15],*/
        /* v3c-atlas-id = (int) [ 0, 65],*/
        /* v3c-parameter-set = (string) ANY, */
        /* v3c-tile-id = (int) [ 0, 1027], */
        /* v3c-tile-id-pres = (int) [ 0, 1]*/
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

#define DEFAULT_CONFIG_INTERVAL 0
#define DEFAULT_AGGREGATE_MODE GST_RTP_ATLAS_AGGREGATE_NONE

enum {
  PROP_0,
  PROP_CONFIG_INTERVAL,
  PROP_AGGREGATE_MODE,
};

static void gst_rtp_atlas_pay_finalize(GObject *object);

static void gst_rtp_atlas_pay_set_property(GObject *object, guint prop_id,
                                           const GValue *value,
                                           GParamSpec *pspec);
static void gst_rtp_atlas_pay_get_property(GObject *object, guint prop_id,
                                           GValue *value, GParamSpec *pspec);

static GstCaps *gst_rtp_atlas_pay_getcaps(GstRTPBasePayload *payload,
                                          GstPad *pad, GstCaps *filter);
static gboolean gst_rtp_atlas_pay_setcaps(GstRTPBasePayload *basepayload,
                                          GstCaps *caps);
static GstFlowReturn gst_rtp_atlas_pay_handle_buffer(GstRTPBasePayload *pad,
                                                     GstBuffer *buffer);
static gboolean gst_rtp_atlas_pay_sink_event(GstRTPBasePayload *payload,
                                             GstEvent *event);
static GstStateChangeReturn
gst_rtp_atlas_pay_change_state(GstElement *element, GstStateChange transition);
static gboolean gst_rtp_atlas_pay_src_query(GstPad *pad, GstObject *parent,
                                            GstQuery *query);

static void gst_rtp_atlas_pay_reset_bundle(GstRtpAtlasPay *rtpatlaspay);

#define gst_rtp_atlas_pay_parent_class parent_class
G_DEFINE_TYPE(GstRtpAtlasPay, gst_rtp_atlas_pay, GST_TYPE_RTP_BASE_PAYLOAD);

static void gst_rtp_atlas_pay_class_init(GstRtpAtlasPayClass *klass) {
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstRTPBasePayloadClass *gstrtpbasepayload_class;

  gobject_class = (GObjectClass *)klass;
  gstelement_class = (GstElementClass *)klass;
  gstrtpbasepayload_class = (GstRTPBasePayloadClass *)klass;

  gobject_class->set_property = gst_rtp_atlas_pay_set_property;
  gobject_class->get_property = gst_rtp_atlas_pay_get_property;

  g_object_class_install_property(
      G_OBJECT_CLASS(klass), PROP_CONFIG_INTERVAL,
      g_param_spec_int("config-interval", "ASPS AFPS AAPS Send Interval",
                       "Send ASPS, AFPS and AAPS Insertion Interval in seconds "
                       "(0 = disabled, -1 = send with every IDR frame)",
                       -1, 3600, DEFAULT_CONFIG_INTERVAL,
                       G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      G_OBJECT_CLASS(klass), PROP_AGGREGATE_MODE,
      g_param_spec_enum(
          "aggregate-mode", "Attempt to use aggregate packets",
          "Bundle suitable ASPS/AFPS/AAPS NAL units into aggregate packets.",
          GST_TYPE_RTP_ATLAS_AGGREGATE_MODE, DEFAULT_AGGREGATE_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gobject_class->finalize = gst_rtp_atlas_pay_finalize;

  gst_element_class_add_static_pad_template(gstelement_class,
                                            &gst_rtp_atlas_pay_src_template);
  gst_element_class_add_static_pad_template(gstelement_class,
                                            &gst_rtp_atlas_pay_sink_template);

  gst_element_class_set_static_metadata(
      gstelement_class, "RTP Atlas payloader", "Codec/Payloader/Network/RTP",
      "RTP Payload Format for Visual Volumetric Video-based Coding (V3C)",
      "Lukasz Kondrad <lukasz.kondrad@nokia.com");

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR(gst_rtp_atlas_pay_change_state);

  gstrtpbasepayload_class->get_caps = gst_rtp_atlas_pay_getcaps;
  gstrtpbasepayload_class->set_caps = gst_rtp_atlas_pay_setcaps;
  gstrtpbasepayload_class->handle_buffer = gst_rtp_atlas_pay_handle_buffer;
  gstrtpbasepayload_class->sink_event = gst_rtp_atlas_pay_sink_event;

  GST_DEBUG_CATEGORY_INIT(rtpatlaspay_debug, "rtpatlaspay", 0,
                          "ATLAS RTP Payloader");

  gst_type_mark_as_plugin_api(GST_TYPE_RTP_ATLAS_AGGREGATE_MODE, 0);
}

static void gst_rtp_atlas_pay_init(GstRtpAtlasPay *rtpatlaspay) {
  rtpatlaspay->queue = g_array_new(FALSE, FALSE, sizeof(guint));
  rtpatlaspay->vps =
      g_ptr_array_new_with_free_func((GDestroyNotify)gst_buffer_unref);
  rtpatlaspay->asps =
      g_ptr_array_new_with_free_func((GDestroyNotify)gst_buffer_unref);
  rtpatlaspay->afps =
      g_ptr_array_new_with_free_func((GDestroyNotify)gst_buffer_unref);
  rtpatlaspay->aaps =
      g_ptr_array_new_with_free_func((GDestroyNotify)gst_buffer_unref);
  rtpatlaspay->last_asps_afps_aaps = -1;
  rtpatlaspay->asps_afps_aaps_interval = DEFAULT_CONFIG_INTERVAL;
  rtpatlaspay->aggregate_mode = DEFAULT_AGGREGATE_MODE;

  gst_pad_set_query_function(GST_RTP_BASE_PAYLOAD_SRCPAD(rtpatlaspay),
                             gst_rtp_atlas_pay_src_query);
}

static void
gst_rtp_atlas_pay_clear_asps_afps_aaps(GstRtpAtlasPay *rtpatlaspay) {
  g_ptr_array_set_size(rtpatlaspay->vps, 0);
  g_ptr_array_set_size(rtpatlaspay->asps, 0);
  g_ptr_array_set_size(rtpatlaspay->afps, 0);
  g_ptr_array_set_size(rtpatlaspay->aaps, 0);
}

static void gst_rtp_atlas_pay_finalize(GObject *object) {
  GstRtpAtlasPay *rtpatlaspay;

  rtpatlaspay = GST_RTP_ATLAS_PAY(object);

  g_array_free(rtpatlaspay->queue, TRUE);

  g_ptr_array_free(rtpatlaspay->afps, TRUE);
  g_ptr_array_free(rtpatlaspay->aaps, TRUE);
  g_ptr_array_free(rtpatlaspay->asps, TRUE);
  g_ptr_array_free(rtpatlaspay->vps, TRUE);

  gst_rtp_atlas_pay_reset_bundle(rtpatlaspay);

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static gboolean gst_rtp_atlas_pay_src_query(GstPad *pad, GstObject *parent,
                                            GstQuery *query) {
  GstRtpAtlasPay *rtpatlaspay = GST_RTP_ATLAS_PAY(parent);

  if (GST_QUERY_TYPE(query) == GST_QUERY_LATENCY) {
    gboolean retval;
    gboolean live;
    GstClockTime min_latency, max_latency;

    retval = gst_pad_query_default(pad, parent, query);
    if (!retval)
      return retval;

    if (rtpatlaspay->stream_format == GST_ATLAS_STREAM_FORMAT_UNKNOWN ||
        rtpatlaspay->alignment == GST_ATLAS_ALIGNMENT_UNKNOWN)
      return FALSE;

    gst_query_parse_latency(query, &live, &min_latency, &max_latency);

    if (rtpatlaspay->aggregate_mode == GST_RTP_ATLAS_AGGREGATE_MAX &&
        rtpatlaspay->alignment != GST_ATLAS_ALIGNMENT_AU &&
        rtpatlaspay->fps_num) {
      GstClockTime one_frame = gst_util_uint64_scale_int(
          GST_SECOND, rtpatlaspay->fps_denum, rtpatlaspay->fps_num);

      min_latency += one_frame;
      max_latency += one_frame;
      gst_query_set_latency(query, live, min_latency, max_latency);
    }
    return TRUE;
  }

  return gst_pad_query_default(pad, parent, query);
}

static GstCaps *gst_rtp_atlas_pay_getcaps(GstRTPBasePayload *payload,
                                          GstPad *pad, GstCaps *filter) {
  GstCaps *template_caps;
  GstCaps *allowed_caps;
  GstCaps *caps;
  GstCaps *icaps;

  allowed_caps =
      gst_pad_peer_query_caps(GST_RTP_BASE_PAYLOAD_SRCPAD(payload), NULL);

  if (allowed_caps == NULL)
    return NULL;

  template_caps =
      gst_static_pad_template_get_caps(&gst_rtp_atlas_pay_sink_template);

  if (gst_caps_is_any(allowed_caps)) {
    caps = gst_caps_ref(template_caps);
    goto done;
  }

  if (gst_caps_is_empty(allowed_caps)) {
    caps = gst_caps_ref(allowed_caps);
    goto done;
  }

  caps = gst_caps_new_empty();
  icaps = gst_caps_intersect(caps, template_caps);
  gst_caps_unref(caps);
  caps = icaps;

done:

  if (filter) {
    GstCaps *tmp;

    GST_DEBUG_OBJECT(
        payload, "Intersect %" GST_PTR_FORMAT " and filter %" GST_PTR_FORMAT,
        caps, filter);
    tmp = gst_caps_intersect_full(filter, caps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref(caps);
    caps = tmp;
  }

  gst_caps_unref(template_caps);
  gst_caps_unref(allowed_caps);

  GST_LOG_OBJECT(payload, "returning caps %" GST_PTR_FORMAT, caps);
  return caps;
}

static gboolean
gst_rtp_atlas_pay_setcaps_optional_parameters(GstRTPBasePayload *basepayload) {
  GstRtpAtlasPay *payloader = GST_RTP_ATLAS_PAY(basepayload);
  gchar *set;
  GString *atlas_data_string = g_string_new("");
  GString *vps_string = g_string_new("");
  GString *vuh_string = g_string_new("");
  guint count = 0;
  guint8 v3c_parameter_set_id = 0;
  guint8 atlas_id = 0;
  guint8 unit_type = 0;
  gboolean res;
  GstMapInfo map;
  guint i;

  if (payloader->vps->len == 0) {
    res = gst_rtp_base_payload_set_outcaps(basepayload, NULL);
    g_string_free(vps_string, TRUE);
    return res;
  }

  GstBuffer *vps_buffer = GST_BUFFER_CAST(g_ptr_array_index(payloader->vps, 0));

  gst_buffer_map(vps_buffer, &map, GST_MAP_READ);
  set = g_base64_encode(map.data, map.size);
  gst_buffer_unmap(vps_buffer, &map);
  g_string_append_printf(vps_string, "%s", set);
  g_free(set);

  guint8 ptl_tier_flag = gst_vps_data_get_ptl_tier_flag(vps_buffer);
  guint8 ptl_codec_idc = gst_vps_data_get_ptl_codec_idc(vps_buffer);
  guint8 ptl_toolset_idc = gst_vps_data_get_ptl_toolset_idc(vps_buffer);
  guint8 ptl_rec_idc = gst_vps_data_get_ptl_rec_idc(vps_buffer);
  guint8 ptl_level_idc = gst_vps_data_get_ptl_level_idc(vps_buffer);

  res = gst_rtp_base_payload_set_outcaps(basepayload,  NULL);

  for (i = 0; i < payloader->asps->len; i++) {
    GstBuffer *asps_buf =
        GST_BUFFER_CAST(g_ptr_array_index(payloader->asps, i));

    gst_buffer_map(asps_buf, &map, GST_MAP_READ);
    set = g_base64_encode(map.data, map.size);
    gst_buffer_unmap(asps_buf, &map);

    g_string_append_printf(atlas_data_string, "%s%s", i ? "," : "", set);
    g_free(set);
    count++;
  }
  for (i = 0; i < payloader->afps->len; i++) {
    GstBuffer *afps_buf =
        GST_BUFFER_CAST(g_ptr_array_index(payloader->afps, i));

    gst_buffer_map(afps_buf, &map, GST_MAP_READ);
    set = g_base64_encode(map.data, map.size);
    gst_buffer_unmap(afps_buf, &map);

    g_string_append_printf(atlas_data_string, "%s%s", i ? "," : "", set);
    g_free(set);
    count++;
  }
  for (i = 0; i < payloader->aaps->len; i++) {
    GstBuffer *aaps_buf =
        GST_BUFFER_CAST(g_ptr_array_index(payloader->aaps, i));

    gst_buffer_map(aaps_buf, &map, GST_MAP_READ);
    set = g_base64_encode(map.data, map.size);
    gst_buffer_unmap(aaps_buf, &map);

    g_string_append_printf(atlas_data_string, "%s%s", i ? "," : "", set);
    g_free(set);
    count++;
  }

  gst_buffer_map(payloader->vuh, &map, GST_MAP_READ);

  set = g_base64_encode(map.data, map.size);
  g_string_append_printf(vuh_string, "%s", set);

  v3c_parameter_set_id = gst_vuh_data_get_v3c_parameter_set_id(payloader->vuh);
  atlas_id = gst_vuh_data_get_atlas_id(payloader->vuh);
  unit_type = gst_vuh_data_get_unit_type(payloader->vuh);

  if (G_LIKELY(count)) {
    res = gst_rtp_base_payload_set_outcaps(
        basepayload, "v3c-atlas-data", G_TYPE_STRING, atlas_data_string->str,
        "v3c-parameter-set", G_TYPE_STRING, vps_string->str, "v3c-vps-id",
        G_TYPE_INT, v3c_parameter_set_id, "v3c-atlas-id", G_TYPE_INT, atlas_id,
        "v3c-unit-type", G_TYPE_INT, unit_type, "v3c-unit-header",
        G_TYPE_STRING, vuh_string->str, "v3c-ptl-tier-flag", G_TYPE_INT,
        ptl_tier_flag, "v3c-ptl-codec-idc", G_TYPE_INT, ptl_codec_idc,
        "v3c-ptl-toolset-idc", G_TYPE_INT, ptl_toolset_idc, "v3c-ptl-rec-idc",
        G_TYPE_INT, ptl_rec_idc, "v3c-ptl-level-idc", G_TYPE_INT, ptl_level_idc,
        NULL);
  } else {
    res = gst_rtp_base_payload_set_outcaps(
        basepayload, "v3c-parameter-set", G_TYPE_STRING, vps_string->str,
        "v3c-vps-id", G_TYPE_INT, v3c_parameter_set_id, "v3c-atlas-id",
        G_TYPE_INT, atlas_id, "v3c-unit-type", G_TYPE_INT, unit_type,
        "v3c-unit-header", G_TYPE_STRING, vuh_string->str, "v3c-ptl-tier-flag",
        G_TYPE_INT, ptl_tier_flag, "v3c-ptl-codec-idc", G_TYPE_INT,
        ptl_codec_idc, "v3c-ptl-toolset-idc", G_TYPE_INT, ptl_toolset_idc,
        "v3c-ptl-rec-idc", G_TYPE_INT, ptl_rec_idc, "v3c-ptl-level-idc",
        G_TYPE_INT, ptl_level_idc, NULL);
  }

  g_free(set);
  g_string_free(vuh_string, TRUE);
  gst_buffer_unmap(payloader->vuh, &map);

  g_string_free(atlas_data_string, TRUE);
  g_string_free(vps_string, TRUE);

  return res;
}

static gboolean gst_rtp_atlas_pay_setcaps(GstRTPBasePayload *basepayload,
                                          GstCaps *caps) {
  GstRtpAtlasPay *rtpatlaspay = NULL;
  GstStructure *str = NULL;
  const GValue *value = NULL;
  GstMapInfo map;
  GstBuffer *buffer = NULL;
  const gchar *alignment = NULL;
  const gchar *stream_format = NULL;

  rtpatlaspay = GST_RTP_ATLAS_PAY(basepayload);

  str = gst_caps_get_structure(caps, 0);

  gst_rtp_base_payload_set_options(basepayload, "application", TRUE, "v3c",
                                   90000);

  rtpatlaspay->alignment = GST_ATLAS_ALIGNMENT_UNKNOWN;
  alignment = gst_structure_get_string(str, "alignment");
  if (alignment) {
    if (g_str_equal(alignment, "au"))
      rtpatlaspay->alignment = GST_ATLAS_ALIGNMENT_AU;
  }

  rtpatlaspay->stream_format = GST_ATLAS_PAY_STREAM_FORMAT_UNKNOWN;
  stream_format = gst_structure_get_string(str, "stream-format");
  if (stream_format) {
    if (g_str_equal(stream_format, "v3cg"))
      rtpatlaspay->stream_format = GST_ATLAS_PAY_STREAM_FORMAT_V3CG;
  }

  if (!gst_structure_get_fraction(str, "framerate", &rtpatlaspay->fps_num,
                                  &rtpatlaspay->fps_denum)) {
    rtpatlaspay->fps_num = 0;
    rtpatlaspay->fps_denum = 0;
  }

  if ((value = gst_structure_get_value(str, "codec_data"))) {
    guint8 unit_size_precision_bytes_minus1 = 0;
    buffer = gst_value_get_buffer(value);

    gst_buffer_map(buffer, &map, GST_MAP_READ);

    /* get info from the codec_data,
       i.e. V3CDecoderConfigurationRecord from ISO/IEC 23090-10 
      codec_data shall have at least one VPS
    */
    if (map.size < 3) {
      goto v3cdcr_too_small;
    }

    unit_size_precision_bytes_minus1 =
        gst_codec_data_get_unit_size_precision_bytes_minus1(buffer);
    rtpatlaspay->nal_length_size = unit_size_precision_bytes_minus1 + 1;
    GST_DEBUG_OBJECT(rtpatlaspay, "nal length %u",
                     rtpatlaspay->nal_length_size);

    GstBuffer *vps_buffer = gst_codec_data_get_vps_unit(buffer);
    if (vps_buffer)
      g_ptr_array_add(rtpatlaspay->vps, vps_buffer);

    gst_buffer_unmap(buffer, &map);
  } else {
    goto no_codec_data;
  }

  if ((value = gst_structure_get_value(str, "vuh_data"))) {
    rtpatlaspay->vuh = gst_value_get_buffer(value);
    gst_buffer_map(rtpatlaspay->vuh, &map, GST_MAP_READ);
    /* get info from the vuh_data,
   i.e. v3c_unit_header from ISO/IEC 23090-5*/
    if (map.size != 4) {
      gst_buffer_unmap(rtpatlaspay->vuh, &map);
      goto vuhd_wrong_size;
    }
    gst_buffer_unmap(rtpatlaspay->vuh, &map);

  } else {
    goto no_vuh_data;
  }

  gst_rtp_atlas_pay_setcaps_optional_parameters(basepayload);

  return TRUE;

no_codec_data : {
  GST_ERROR_OBJECT(rtpatlaspay,
                   "no codec_data, that should contain "
                   "V3CDecoderConfigurationRecord from ISO/IEC 23090-10");
  goto error;
}
no_vuh_data : {
  GST_ERROR_OBJECT(
      rtpatlaspay,
      "no vuh_data, that should contain v3c_unit_header from ISO/IEC 23090-5");
  goto error;
}
vuhd_wrong_size : {
  GST_ERROR_OBJECT(rtpatlaspay,
                   "V3C unit header size %" G_GSIZE_FORMAT "!= 4",
                   map.size);
  goto error;
}
v3cdcr_too_small : {
  GST_ERROR_OBJECT(rtpatlaspay,
        "V3CDecoderConfigurationRecord size %" G_GSIZE_FORMAT " < 2", map.size);
  goto error;
}
error : {
  gst_buffer_unmap(buffer, &map);
  return FALSE;
}
}

static GstFlowReturn
gst_rtp_atlas_pay_payload_nal(GstRTPBasePayload *basepayload,
                              GPtrArray *paybufs, GstClockTime dts,
                              GstClockTime pts);
static GstFlowReturn
gst_rtp_atlas_pay_payload_nal_single(GstRTPBasePayload *basepayload,
                                     GstBuffer *paybuf, GstClockTime dts,
                                     GstClockTime pts, gboolean marker);
static GstFlowReturn gst_rtp_atlas_pay_payload_nal_fragment(
    GstRTPBasePayload *basepayload, GstBuffer *paybuf, GstClockTime dts,
    GstClockTime pts, gboolean marker, guint mtu, guint8 nal_type,
    const guint8 *nal_header, int size);
static GstFlowReturn gst_rtp_atlas_pay_payload_nal_bundle(
    GstRTPBasePayload *basepayload, GstBuffer *paybuf, GstClockTime dts,
    GstClockTime pts, gboolean marker, guint8 nal_type,
    const guint8 *nal_header, int size);

static GstFlowReturn
gst_rtp_atlas_pay_send_asps_afps_aaps(GstRTPBasePayload *basepayload,
                                      GstRtpAtlasPay *rtpatlaspay,
                                      GstClockTime dts, GstClockTime pts) {
  GstFlowReturn ret = GST_FLOW_OK;
  gboolean sent_all_asps_afps_aaps = TRUE;
  guint i;
  GPtrArray *bufs;

  bufs = g_ptr_array_new();

  for (i = 0; i < rtpatlaspay->asps->len; i++) {
    GstBuffer *asps_buf =
        GST_BUFFER_CAST(g_ptr_array_index(rtpatlaspay->asps, i));

    GST_DEBUG_OBJECT(rtpatlaspay, "inserting ASPS in the stream");
    g_ptr_array_add(bufs, gst_buffer_ref(asps_buf));
  }
  for (i = 0; i < rtpatlaspay->afps->len; i++) {
    GstBuffer *afps_buf =
        GST_BUFFER_CAST(g_ptr_array_index(rtpatlaspay->afps, i));

    GST_DEBUG_OBJECT(rtpatlaspay, "inserting AFPS in the stream");
    g_ptr_array_add(bufs, gst_buffer_ref(afps_buf));
  }
  for (i = 0; i < rtpatlaspay->aaps->len; i++) {
    GstBuffer *aaps_buf =
        GST_BUFFER_CAST(g_ptr_array_index(rtpatlaspay->aaps, i));

    GST_DEBUG_OBJECT(rtpatlaspay, "inserting AAPS in the stream");
    g_ptr_array_add(bufs, gst_buffer_ref(aaps_buf));
  }

  ret = gst_rtp_atlas_pay_payload_nal(basepayload, bufs, dts, pts);
  if (ret != GST_FLOW_OK) {
    /* not critical but warn */
    GST_WARNING_OBJECT(basepayload, "failed pushing ASPS/AFPS/AAPS");

    sent_all_asps_afps_aaps = FALSE;
  }

  if (pts != -1 && sent_all_asps_afps_aaps)
    rtpatlaspay->last_asps_afps_aaps = gst_segment_to_running_time(
        &basepayload->segment, GST_FORMAT_TIME, pts);

  return ret;
}

static void gst_rtp_atlas_pay_reset_bundle(GstRtpAtlasPay *rtpatlaspay) {
  g_clear_pointer(&rtpatlaspay->bundle, gst_buffer_list_unref);
  rtpatlaspay->bundle_size = 0;
  rtpatlaspay->bundle_contains_acl_or_suffix = FALSE;
}

static GstFlowReturn
gst_rtp_atlas_pay_payload_nal(GstRTPBasePayload *basepayload,
                              GPtrArray *paybufs, GstClockTime dts,
                              GstClockTime pts) {
  GstRtpAtlasPay *rtpatlaspay;
  guint mtu;
  GstFlowReturn ret;
  gint i;
  gboolean sent_ps;

  rtpatlaspay = GST_RTP_ATLAS_PAY(basepayload);
  mtu = GST_RTP_BASE_PAYLOAD_MTU(rtpatlaspay);

  /* should set src caps before pushing stuff,
   * and if we did not see enough ASPS/AFPS/AAPS, that may not be the case */
  if (G_UNLIKELY(
          !gst_pad_has_current_caps(GST_RTP_BASE_PAYLOAD_SRCPAD(basepayload))))
    gst_rtp_atlas_pay_setcaps_optional_parameters(basepayload);

  ret = GST_FLOW_OK;
  sent_ps = FALSE;
  for (i = 0; i < paybufs->len; i++) {
    guint8 nal_header[2];
    guint8 nal_type;
    GstBuffer *paybuf;
    gboolean send_ps;
    guint size;
    gboolean marker;

    paybuf = g_ptr_array_index(paybufs, i);

    if (ret != GST_FLOW_OK) {
      /* unref buffers that will not be payloaded after a flow error */
      gst_buffer_unref(paybuf);
      continue;
    }

    marker = GST_BUFFER_FLAG_IS_SET(paybuf, GST_BUFFER_FLAG_MARKER);

    size = gst_buffer_get_size(paybuf);
    gst_buffer_extract(paybuf, 0, nal_header, 2);
    nal_type = (nal_header[0] >> 1) & 0x3f;

    GST_DEBUG_OBJECT(rtpatlaspay,
                     "payloading NAL Unit: datasize=%u type=%d"
                     " pts=%" GST_TIME_FORMAT,
                     size, nal_type, GST_TIME_ARGS(pts));

    send_ps = FALSE;

    /* check if we need to emit an ASPS/AFPS/AAPS now */
    if ((nal_type == GST_ATLAS_NAL_TRAIL_N) ||
        (nal_type == GST_ATLAS_NAL_TRAIL_R) ||
        (nal_type == GST_ATLAS_NAL_TSA_N) ||
        (nal_type == GST_ATLAS_NAL_TSA_R) ||
        (nal_type == GST_ATLAS_NAL_STSA_N) ||
        (nal_type == GST_ATLAS_NAL_STSA_R) ||
        (nal_type == GST_ATLAS_NAL_RASL_N) ||
        (nal_type == GST_ATLAS_NAL_RASL_R) ||
        (nal_type == GST_ATLAS_NAL_BLA_W_LP) ||
        (nal_type == GST_ATLAS_NAL_BLA_W_RADL) ||
        (nal_type == GST_ATLAS_NAL_BLA_N_LP) ||
        (nal_type == GST_ATLAS_NAL_IDR_W_RADL) ||
        (nal_type == GST_ATLAS_NAL_IDR_N_LP) ||
        (nal_type == GST_ATLAS_NAL_CRA)) {
      if (rtpatlaspay->asps_afps_aaps_interval > 0) {
        if (rtpatlaspay->last_asps_afps_aaps != -1) {
          guint64 diff;
          GstClockTime running_time = gst_segment_to_running_time(
              &basepayload->segment, GST_FORMAT_TIME, pts);

          GST_LOG_OBJECT(rtpatlaspay,
                         "now %" GST_TIME_FORMAT
                         ", last ASPS/AFPS/AAPS %" GST_TIME_FORMAT,
                         GST_TIME_ARGS(running_time),
                         GST_TIME_ARGS(rtpatlaspay->last_asps_afps_aaps));

          /* calculate diff between last ASPS/AFPS in milliseconds */
          if (running_time > rtpatlaspay->last_asps_afps_aaps)
            diff = running_time - rtpatlaspay->last_asps_afps_aaps;
          else
            diff = 0;

          GST_DEBUG_OBJECT(
              rtpatlaspay,
              "interval since last ASPS/AFPS/AAPS %" GST_TIME_FORMAT,
              GST_TIME_ARGS(diff));

          /* bigger than interval, queue ASPS/AFPS */
          if (GST_TIME_AS_SECONDS(diff) >=
              rtpatlaspay->asps_afps_aaps_interval) {
            GST_DEBUG_OBJECT(rtpatlaspay, "time to send ASPS/AFPS/AAPS");
            send_ps = TRUE;
          }
        } else {
          /* no known previous ASPS/AFPS time, send now */
          GST_DEBUG_OBJECT(rtpatlaspay,
                           "no previous ASPS/AFPS/AAPS time, send now");
          send_ps = TRUE;
        }
      } else if (rtpatlaspay->asps_afps_aaps_interval == -1 &&
                 (nal_type == GST_ATLAS_NAL_IDR_W_RADL ||
                  nal_type == GST_ATLAS_NAL_IDR_N_LP)) {
        /* send ASPS/AFPS/AAPS before every IDR frame */
        send_ps = TRUE;
      }
    }

    if (!sent_ps && (send_ps || rtpatlaspay->send_asps_afps_aaps)) {
      /* we need to send ASPS/AFPS now first. */
      rtpatlaspay->send_asps_afps_aaps = FALSE;
      sent_ps = TRUE;
      GST_DEBUG_OBJECT(rtpatlaspay,
                       "sending ASPS/AFPS/AAPS before current atlas frame");
      ret = gst_rtp_atlas_pay_send_asps_afps_aaps(basepayload, rtpatlaspay, dts,
                                                  pts);
      if (ret != GST_FLOW_OK) {
        gst_buffer_unref(paybuf);
        continue;
      }
    }

    if (rtpatlaspay->aggregate_mode != GST_RTP_ATLAS_AGGREGATE_NONE)
      ret = gst_rtp_atlas_pay_payload_nal_bundle(
          basepayload, paybuf, dts, pts, marker, nal_type, nal_header, size);
    else
      ret = gst_rtp_atlas_pay_payload_nal_fragment(basepayload, paybuf, dts,
                                                   pts, marker, mtu, nal_type,
                                                   nal_header, size);
  }

  g_ptr_array_free(paybufs, TRUE);

  return ret;
}

static GstFlowReturn
gst_rtp_atlas_pay_payload_nal_single(GstRTPBasePayload *basepayload,
                                     GstBuffer *paybuf, GstClockTime dts,
                                     GstClockTime pts, gboolean marker) {
  GstBufferList *outlist;
  GstBuffer *outbuf;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;

  /* use buffer lists
   * create buffer without payload containing only the RTP header
   * (memory block at index 0) */
  outbuf = gst_rtp_buffer_new_allocate(0, 0, 0);

  gst_rtp_buffer_map(outbuf, GST_MAP_WRITE, &rtp);

  /* Mark the end of a frame */
  gst_rtp_buffer_set_marker(&rtp, marker);

  /* timestamp the outbuffer */
  GST_BUFFER_PTS(outbuf) = pts;
  GST_BUFFER_DTS(outbuf) = dts;

  /* insert payload memory block */
  gst_rtp_copy_video_meta(basepayload, outbuf, paybuf);
  outbuf = gst_buffer_append(outbuf, paybuf);

  outlist = gst_buffer_list_new();

  /* add the buffer to the buffer list */
  gst_buffer_list_add(outlist, outbuf);

  gst_rtp_buffer_unmap(&rtp);

  /* push the list to the next element in the pipe */
  return gst_rtp_base_payload_push_list(basepayload, outlist);
}

static GstFlowReturn gst_rtp_atlas_pay_payload_nal_fragment(
    GstRTPBasePayload *basepayload, GstBuffer *paybuf, GstClockTime dts,
    GstClockTime pts, gboolean marker, guint mtu, guint8 nal_type,
    const guint8 *nal_header, int size) {
  GstRtpAtlasPay *rtpatlaspay = (GstRtpAtlasPay *)basepayload;
  GstFlowReturn ret;
  guint max_fragment_size, ii, pos;
  GstBuffer *outbuf;
  GstBufferList *outlist = NULL;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  guint8 *payload;

  if (gst_rtp_buffer_calc_packet_len(size, 0, 0) < mtu) {
    GST_DEBUG_OBJECT(rtpatlaspay,
                     "NAL Unit fit in one packet datasize=%d mtu=%d", size,
                     mtu);
    /* will fit in one packet */
    return gst_rtp_atlas_pay_payload_nal_single(basepayload, paybuf, dts, pts,
                                                marker);
  }

  GST_DEBUG_OBJECT(basepayload,
                   "NAL Unit DOES NOT fit in one packet datasize=%d mtu=%d",
                   size, mtu);

  GST_DEBUG_OBJECT(basepayload, "Using FU fragmentation for data size=%d",
                   size - 2);

  /* We keep 3 bytes for RTP payload header (NUT=57) and FU Header */
  max_fragment_size = gst_rtp_buffer_calc_payload_len(mtu - 3, 0, 0);

  outlist = gst_buffer_list_new();

  for (pos = 2, ii = 0; pos < size; pos += max_fragment_size, ii++) {
    guint remaining, fragment_size;
    gboolean first_fragment, last_fragment;

    remaining = size - pos;
    fragment_size = MIN(remaining, max_fragment_size);
    first_fragment = (pos == 2);
    last_fragment = (remaining <= max_fragment_size);

    GST_DEBUG_OBJECT(
        basepayload,
        "Inside  FU fragmentation fragment_size=%u iteration=%d %s%s",
        fragment_size, ii, first_fragment ? "first" : "",
        last_fragment ? "last" : "");

    /* use buffer lists
     * create buffer without payload containing only the RTP header
     * (memory block at index 0), and with space for PayloadHdr and FU header */
    outbuf = gst_rtp_buffer_new_allocate(3, 0, 0);

    gst_rtp_buffer_map(outbuf, GST_MAP_WRITE, &rtp);

    GST_BUFFER_DTS(outbuf) = dts;
    GST_BUFFER_PTS(outbuf) = pts;
    payload = gst_rtp_buffer_get_payload(&rtp);

    /* RTP payload header (type = FU_NUT (57)) */
    payload[0] = (nal_header[0] & 0x81) | (FU_NUT << 1);
    payload[1] = nal_header[1];

    /* If it's the last fragment and the end of this au, mark the end of
     * atlas tile */
    gst_rtp_buffer_set_marker(&rtp, last_fragment && marker);

    /* FU Header */
    payload[2] =
        (first_fragment << 7) | (last_fragment << 6) | (nal_type & 0x3f);

    gst_rtp_buffer_unmap(&rtp);

    /* insert payload memory block */
    gst_rtp_copy_video_meta(rtpatlaspay, outbuf, paybuf);
    gst_buffer_copy_into(outbuf, paybuf, GST_BUFFER_COPY_MEMORY, pos,
                         fragment_size);
    /* add the buffer to the buffer list */
    gst_buffer_list_add(outlist, outbuf);
  }

  ret = gst_rtp_base_payload_push_list(basepayload, outlist);
  gst_buffer_unref(paybuf);

  return ret;
}

static GstFlowReturn gst_rtp_atlas_pay_send_bundle(GstRtpAtlasPay *rtpatlaspay,
                                                   gboolean marker) {
  GstRTPBasePayload *basepayload;
  GstBufferList *bundle;
  guint length, bundle_size;
  GstBuffer *first, *outbuf;
  GstClockTime dts, pts;

  bundle_size = rtpatlaspay->bundle_size;

  if (bundle_size == 0) {
    GST_DEBUG_OBJECT(rtpatlaspay, "no bundle, nothing to send");
    return GST_FLOW_OK;
  }

  basepayload = GST_RTP_BASE_PAYLOAD(rtpatlaspay);
  bundle = rtpatlaspay->bundle;
  length = gst_buffer_list_length(bundle);

  first = gst_buffer_list_get(bundle, 0);
  dts = GST_BUFFER_DTS(first);
  pts = GST_BUFFER_PTS(first);

  if (length == 1) {
    /* Push unaggregated NALU */
    outbuf = gst_buffer_ref(first);

    GST_DEBUG_OBJECT(rtpatlaspay, "sending NAL Unit unaggregated: datasize=%u",
                     bundle_size - 2);
  } else {
    guint8 ap_header[2];
    guint i;
    guint8 layer_id = 0xFF;
    guint8 temporal_id = 0xFF;

    outbuf = gst_buffer_new_allocate(NULL, sizeof ap_header, NULL);

    for (i = 0; i < length; i++) {
      GstBuffer *buf = gst_buffer_list_get(bundle, i);
      guint8 nal_header[2];
      GstMemory *size_header;
      GstMapInfo map;
      guint8 nal_layer_id;
      guint8 nal_temporal_id;

      gst_buffer_extract(buf, 0, &nal_header, sizeof nal_header);

      /* Propagate F bit */
      if ((nal_header[0] & 0x80))
        ap_header[0] |= 0x80;

      /* Select lowest layer_id & temporal_id */
      nal_layer_id =
          ((nal_header[0] & 0x01) << 5) | ((nal_header[1] >> 3) & 0x1F);
      nal_temporal_id = nal_header[1] & 0x7;
      layer_id = MIN(layer_id, nal_layer_id);
      temporal_id = MIN(temporal_id, nal_temporal_id);

      /* append NALU size */
      size_header = gst_allocator_alloc(NULL, 2, NULL);
      gst_memory_map(size_header, &map, GST_MAP_WRITE);
      GST_WRITE_UINT16_BE(map.data, gst_buffer_get_size(buf));
      gst_memory_unmap(size_header, &map);
      gst_buffer_append_memory(outbuf, size_header);

      /* append NALU data */
      outbuf = gst_buffer_append(outbuf, gst_buffer_ref(buf));
    }

    ap_header[0] = (AP_NUT << 1) | (layer_id & 0x20);
    ap_header[1] = ((layer_id & 0x1F) << 3) | (temporal_id & 0x07);

    gst_buffer_fill(outbuf, 0, &ap_header, sizeof ap_header);

    GST_DEBUG_OBJECT(rtpatlaspay,
                     "sending AP bundle: n=%u header=%02x%02x datasize=%u",
                     length, ap_header[0], ap_header[1], bundle_size);
  }

  gst_rtp_atlas_pay_reset_bundle(rtpatlaspay);
  return gst_rtp_atlas_pay_payload_nal_single(basepayload, outbuf, dts, pts,
                                              marker);
}

static gboolean gst_rtp_atlas_pay_payload_nal_bundle(
    GstRTPBasePayload *basepayload, GstBuffer *paybuf, GstClockTime dts,
    GstClockTime pts, gboolean marker, guint8 nal_type,
    const guint8 *nal_header, int size) {
  GstRtpAtlasPay *rtpatlaspay;
  GstFlowReturn ret;
  guint pay_size, bundle_size;
  GstBufferList *bundle;
  gboolean start_of_au;
  guint mtu;

  rtpatlaspay = GST_RTP_ATLAS_PAY(basepayload);
  mtu = GST_RTP_BASE_PAYLOAD_MTU(rtpatlaspay);
  pay_size = 2 + gst_buffer_get_size(paybuf);
  bundle = rtpatlaspay->bundle;
  start_of_au = FALSE;

  if (bundle) {
    GstBuffer *first = gst_buffer_list_get(bundle, 0);

    if (nal_type == GST_ATLAS_NAL_AUD) {
      GST_DEBUG_OBJECT(rtpatlaspay, "found access delimiter");
      start_of_au = TRUE;
    } else if (GST_BUFFER_IS_DISCONT(paybuf)) {
      GST_DEBUG_OBJECT(rtpatlaspay, "found discont");
      start_of_au = TRUE;
    } else if (GST_BUFFER_PTS(first) != pts || GST_BUFFER_DTS(first) != dts) {
      GST_DEBUG_OBJECT(rtpatlaspay, "found timestamp mismatch");
      start_of_au = TRUE;
    }
  }

  if (start_of_au) {
    GST_DEBUG_OBJECT(rtpatlaspay, "sending bundle before start of AU");

    ret = gst_rtp_atlas_pay_send_bundle(rtpatlaspay, TRUE);
    if (ret != GST_FLOW_OK)
      goto out;

    bundle = NULL;
  }

  bundle_size = 2 + pay_size;

  if (gst_rtp_buffer_calc_packet_len(bundle_size, 0, 0) > mtu) {
    GST_DEBUG_OBJECT(rtpatlaspay, "NAL Unit cannot fit in a bundle");

    ret = gst_rtp_atlas_pay_send_bundle(rtpatlaspay, FALSE);
    if (ret != GST_FLOW_OK)
      goto out;

    return gst_rtp_atlas_pay_payload_nal_fragment(
        basepayload, paybuf, dts, pts, marker, mtu, nal_type, nal_header, size);
  }

  bundle_size = rtpatlaspay->bundle_size + pay_size;

  if (gst_rtp_buffer_calc_packet_len(bundle_size, 0, 0) > mtu) {
    GST_DEBUG_OBJECT(
        rtpatlaspay,
        "bundle overflows, sending: bundlesize=%u datasize=2+%u mtu=%u",
        rtpatlaspay->bundle_size, pay_size - 2, mtu);

    ret = gst_rtp_atlas_pay_send_bundle(rtpatlaspay, FALSE);
    if (ret != GST_FLOW_OK)
      goto out;

    bundle = NULL;
  }

  if (!bundle) {
    GST_DEBUG_OBJECT(rtpatlaspay, "creating new AP aggregate");
    bundle = rtpatlaspay->bundle = gst_buffer_list_new();
    bundle_size = rtpatlaspay->bundle_size = 2;
    rtpatlaspay->bundle_contains_acl_or_suffix = FALSE;
  }

  GST_DEBUG_OBJECT(rtpatlaspay,
                   "bundling NAL Unit: bundlesize=%u datasize=2+%u mtu=%u",
                   rtpatlaspay->bundle_size, pay_size - 2, mtu);

  paybuf = gst_buffer_make_writable(paybuf);
  GST_BUFFER_PTS(paybuf) = pts;
  GST_BUFFER_DTS(paybuf) = dts;

  gst_buffer_list_add(bundle, gst_buffer_ref(paybuf));
  rtpatlaspay->bundle_size += pay_size;
  ret = GST_FLOW_OK;

  /* in atlas, all ACL NAL units are < 35 */
  if (nal_type < 35 || nal_type == GST_ATLAS_NAL_EOS ||
      nal_type == GST_ATLAS_NAL_EOB || nal_type == GST_ATLAS_NAL_SUFFIX_NSEI ||
      nal_type == GST_ATLAS_NAL_SUFFIX_ESEI ){
        rtpatlaspay->bundle_contains_acl_or_suffix = TRUE;
  }

  if (marker) {
    GST_DEBUG_OBJECT(rtpatlaspay, "sending bundle at marker");
    ret = gst_rtp_atlas_pay_send_bundle(rtpatlaspay, TRUE);
  }

out:
  gst_buffer_unref(paybuf);
  return ret;
}

static GstFlowReturn
gst_rtp_atlas_pay_handle_buffer(GstRTPBasePayload *basepayload,
                                GstBuffer *buffer) {
  GstRtpAtlasPay *rtpatlaspay;
  GstFlowReturn ret;
  guint nal_len;
  GstClockTime dts, pts;
  GstBuffer *paybuf = NULL;
  gboolean marker = FALSE;
  gboolean discont = FALSE;

  if (buffer == NULL)
    return GST_FLOW_OK;

  rtpatlaspay = GST_RTP_ATLAS_PAY(basepayload);

  ret = GST_FLOW_OK;

  /* now loop over all NAL units and put them in a packet */
  GstBufferMemoryMap memory;
  gsize remaining_buffer_size;
  guint nal_length_size;
  gsize offset = 0;
  GPtrArray *paybufs;

  paybufs = g_ptr_array_new();
  nal_length_size = rtpatlaspay->nal_length_size;

  gst_buffer_memory_map(buffer, &memory);
  remaining_buffer_size = gst_buffer_get_size(buffer);

  pts = GST_BUFFER_PTS(buffer);
  dts = GST_BUFFER_DTS(buffer);
  marker = GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_MARKER);
  GST_DEBUG_OBJECT(basepayload, "got %" G_GSIZE_FORMAT " bytes",
                   remaining_buffer_size);

  while (remaining_buffer_size > nal_length_size) {
    gint i;

    nal_len = 0;
    for (i = 0; i < nal_length_size; i++) {
      nal_len = (nal_len << 8) + *memory.data;
      if (!gst_buffer_memory_advance_bytes(&memory, 1))
        break;
    }

    offset += nal_length_size;
    remaining_buffer_size -= nal_length_size;

    if (remaining_buffer_size >= nal_len) {
      GST_DEBUG_OBJECT(basepayload, "got NAL of size %u", nal_len);
    } else {
      nal_len = remaining_buffer_size;
      GST_DEBUG_OBJECT(basepayload, "got incomplete NAL of size %u", nal_len);
    }

    paybuf =
        gst_buffer_copy_region(buffer, GST_BUFFER_COPY_ALL, offset, nal_len);
    g_ptr_array_add(paybufs, paybuf);

    /* If we're at the end of the buffer, then we're at the end of the
     * access unit
     */
    GST_BUFFER_FLAG_UNSET(paybuf, GST_BUFFER_FLAG_MARKER);
    if (remaining_buffer_size - nal_len <= nal_length_size) {
      if (rtpatlaspay->alignment == GST_ATLAS_ALIGNMENT_AU || marker)
        GST_BUFFER_FLAG_SET(paybuf, GST_BUFFER_FLAG_MARKER);
    }

    GST_BUFFER_FLAG_UNSET(paybuf, GST_BUFFER_FLAG_DISCONT);
    if (discont) {
      GST_BUFFER_FLAG_SET(paybuf, GST_BUFFER_FLAG_DISCONT);
      discont = FALSE;
    }

    /* Skip current nal. If it is split over multiple GstMemory
     * advance_bytes () will switch to the correct GstMemory. The payloader
     * does not access those bytes directly but uses gst_buffer_copy_region ()
     * to create a sub-buffer referencing the nal instead */
    if (!gst_buffer_memory_advance_bytes(&memory, nal_len))
      break;
    offset += nal_len;
    remaining_buffer_size -= nal_len;
  }

  ret = gst_rtp_atlas_pay_payload_nal(basepayload, paybufs, dts, pts);

  gst_buffer_memory_unmap(&memory);
  gst_buffer_unref(buffer);

  if (ret == GST_FLOW_OK && rtpatlaspay->bundle_size > 0 &&
      rtpatlaspay->aggregate_mode == GST_RTP_ATLAS_AGGREGATE_ZERO_LATENCY &&
      rtpatlaspay->bundle_contains_acl_or_suffix)
  {
    GST_DEBUG_OBJECT(rtpatlaspay, "sending bundle at end incoming packet");
    ret = gst_rtp_atlas_pay_send_bundle(rtpatlaspay, FALSE);
  }

  return ret;
}

static gboolean gst_rtp_atlas_pay_sink_event(GstRTPBasePayload *payload,
                                             GstEvent *event) {
  gboolean res;
  const GstStructure *s;
  GstRtpAtlasPay *rtpatlaspay = GST_RTP_ATLAS_PAY(payload);
  GstFlowReturn ret = GST_FLOW_OK;

  switch (GST_EVENT_TYPE(event)) {
  case GST_EVENT_FLUSH_STOP:
    gst_rtp_atlas_pay_reset_bundle(rtpatlaspay);
    break;
  case GST_EVENT_CUSTOM_DOWNSTREAM:
    s = gst_event_get_structure(event);
    if (gst_structure_has_name(s, "GstForceKeyUnit")) {
      gboolean resend_codec_data;

      if (gst_structure_get_boolean(s, "all-headers", &resend_codec_data) &&
          resend_codec_data)
        rtpatlaspay->send_asps_afps_aaps = TRUE;
    }
    break;
  case GST_EVENT_EOS: {
    /* call handle_buffer with NULL to flush last NAL from adapter
     * in byte-stream mode
     */
    gst_rtp_atlas_pay_handle_buffer(payload, NULL);
    ret = gst_rtp_atlas_pay_send_bundle(rtpatlaspay, TRUE);

    break;
  }
  case GST_EVENT_STREAM_START:
    GST_DEBUG_OBJECT(rtpatlaspay,
                     "New stream detected => Clear ASPS, AFPS and AAPS");
    gst_rtp_atlas_pay_clear_asps_afps_aaps(rtpatlaspay);
    break;
  default:
    break;
  }

  if (ret != GST_FLOW_OK)
    return FALSE;

  res = GST_RTP_BASE_PAYLOAD_CLASS(parent_class)->sink_event(payload, event);

  return res;
}

static GstStateChangeReturn
gst_rtp_atlas_pay_change_state(GstElement *element, GstStateChange transition) {
  GstStateChangeReturn ret;
  GstRtpAtlasPay *rtpatlaspay = GST_RTP_ATLAS_PAY(element);

  switch (transition) {
  case GST_STATE_CHANGE_READY_TO_PAUSED:
    rtpatlaspay->send_asps_afps_aaps = FALSE;
    gst_rtp_atlas_pay_reset_bundle(rtpatlaspay);
    break;
  default:
    break;
  }

  ret = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);

  switch (transition) {
  case GST_STATE_CHANGE_PAUSED_TO_READY:
    rtpatlaspay->last_asps_afps_aaps = -1;
    gst_rtp_atlas_pay_clear_asps_afps_aaps(rtpatlaspay);
    break;
  default:
    break;
  }

  return ret;
}

static void gst_rtp_atlas_pay_set_property(GObject *object, guint prop_id,
                                           const GValue *value,
                                           GParamSpec *pspec) {
  GstRtpAtlasPay *rtpatlaspay;

  rtpatlaspay = GST_RTP_ATLAS_PAY(object);

  switch (prop_id) {
  case PROP_CONFIG_INTERVAL:
    rtpatlaspay->asps_afps_aaps_interval = g_value_get_int(value);
    break;
  case PROP_AGGREGATE_MODE:
    rtpatlaspay->aggregate_mode = g_value_get_enum(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void gst_rtp_atlas_pay_get_property(GObject *object, guint prop_id,
                                           GValue *value, GParamSpec *pspec) {
  GstRtpAtlasPay *rtpatlaspay;

  rtpatlaspay = GST_RTP_ATLAS_PAY(object);

  switch (prop_id) {
  case PROP_CONFIG_INTERVAL:
    g_value_set_int(value, rtpatlaspay->asps_afps_aaps_interval);
    break;
  case PROP_AGGREGATE_MODE:
    g_value_set_enum(value, rtpatlaspay->aggregate_mode);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

gboolean gst_rtp_atlas_pay_plugin_init(GstPlugin *plugin) {
  return gst_element_register(plugin, "rtpatlaspay", GST_RANK_SECONDARY,
                              GST_TYPE_RTP_ATLAS_PAY);
}
