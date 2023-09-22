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

#ifndef __GST_RTP_ATLAS_PAY_H__
#define __GST_RTP_ATLAS_PAY_H__

#include "utils.h"
#include <gst/base/gstadapter.h>
#include <gst/gst.h>
#include <gst/rtp/gstrtpbasepayload.h>

G_BEGIN_DECLS
#define GST_TYPE_RTP_ATLAS_PAY (gst_rtp_atlas_pay_get_type())
#define GST_RTP_ATLAS_PAY(obj)                                                 \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_RTP_ATLAS_PAY, GstRtpAtlasPay))
#define GST_RTP_ATLAS_PAY_CLASS(klass)                                         \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_RTP_ATLAS_PAY,                    \
                           GstRtpAtlasPayClass))
#define GST_IS_RTP_ATLAS_PAY(obj)                                              \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_RTP_ATLAS_PAY))
#define GST_IS_RTP_ATLAS_PAY_CLASS(klass)                                      \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_RTP_ATLAS_PAY))
typedef struct _GstRtpAtlasPay GstRtpAtlasPay;
typedef struct _GstRtpAtlasPayClass GstRtpAtlasPayClass;

typedef enum {
  GST_ATLAS_ALIGNMENT_UNKNOWN,
  GST_ATLAS_ALIGNMENT_AU
} GstAtlasAlignment;

typedef enum {
  GST_RTP_ATLAS_AGGREGATE_NONE,
  GST_RTP_ATLAS_AGGREGATE_ZERO_LATENCY,
  GST_RTP_ATLAS_AGGREGATE_MAX,
} GstRTPAtlasAggregateMode;

typedef enum {
  GST_ATLAS_PAY_STREAM_FORMAT_UNKNOWN,
  GST_ATLAS_PAY_STREAM_FORMAT_V3CG
} GstAtlasPayStreamFormat;

struct _GstRtpAtlasPay {
  GstRTPBasePayload payload;

  GPtrArray *vps, *asps, *afps, *aaps;
  GstBuffer *vuh;

  GstAtlasPayStreamFormat stream_format;
  GstAtlasAlignment alignment;
  gint fps_num;
  gint fps_denum;
  guint8 nal_length_size;
  GArray *queue;

  gint asps_afps_aaps_interval;
  gboolean send_asps_afps_aaps;
  GstClockTime last_asps_afps_aaps;

  /* aggregate buffers with AP */
  GstBufferList *bundle;
  guint bundle_size;
  gboolean bundle_contains_acl_or_suffix;
  GstRTPAtlasAggregateMode aggregate_mode;
};

struct _GstRtpAtlasPayClass {
  GstRTPBasePayloadClass parent_class;
};

GType gst_rtp_atlas_pay_get_type(void);

gboolean gst_rtp_atlas_pay_plugin_init(GstPlugin *plugin);

G_END_DECLS
#endif /* __GST_RTP_ATLAS_PAY_H__ */
