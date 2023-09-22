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

#ifndef __GST_RTP_ATLAS_DEPAY_H__
#define __GST_RTP_ATLAS_DEPAY_H__

#include "utils.h"
#include <gst/base/gstadapter.h>
#include <gst/gst.h>
#include <gst/rtp/gstrtpbasedepayload.h>

G_BEGIN_DECLS
#define GST_TYPE_RTP_ATLAS_DEPAY (gst_rtp_atlas_depay_get_type())
#define GST_RTP_ATLAS_DEPAY(obj)                                               \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_RTP_ATLAS_DEPAY,                 \
                              GstRtpAtlasDepay))
#define GST_RTP_ATLAS_DEPAY_CLASS(klass)                                       \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_RTP_ATLAS_DEPAY,                  \
                           GstRtpAtlasDepayClass))
#define GST_IS_RTP_ATLAS_DEPAY(obj)                                            \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_RTP_ATLAS_DEPAY))
#define GST_IS_RTP_ATLAS_DEPAY_CLASS(klass)                                    \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_RTP_ATLAS_DEPAY))
typedef struct _GstRtpAtlasDepay GstRtpAtlasDepay;
typedef struct _GstRtpAtlasDepayClass GstRtpAtlasDepayClass;

typedef enum {
  GST_ATLAS_STREAM_FORMAT_UNKNOWN,
  GST_ATLAS_STREAM_FORMAT_V3CG
} GstAtlasStreamFormat;

struct _GstRtpAtlasDepay {
  GstRTPBaseDepayload depayload;

  const gchar *stream_format;
  GstAtlasStreamFormat output_format;

  GstBuffer *vuh;
  GstBuffer *vps;
  GstBuffer *codec_data;
  GstAdapter *adapter;
  gboolean wait_start;

  /* NAL Aggregation Units*/
  GstAdapter *atlas_frame_adapter;
  gboolean atlas_frame_start;
  GstClockTime last_ts;
  gboolean last_keyframe;

  /* NAL Fragmentation Units */
  guint8 current_fu_type;
  guint16 last_fu_seqnum;
  GstClockTime fu_timestamp;
  gboolean fu_marker;

  GPtrArray *asps;
  GPtrArray *afps;
  GPtrArray *aaps;
  gboolean new_codec_data;

  GstAllocator *allocator;
  GstAllocationParams params;
};

struct _GstRtpAtlasDepayClass {
  GstRTPBaseDepayloadClass parent_class;
};

typedef struct {
  GstElement *element;
  GstBuffer *outbuf;
  GQuark copy_tag;
} CopyMetaData;

typedef struct {
  GstElement *element;
  GQuark keep_tag;
} DropMetaData;

GType gst_rtp_atlas_depay_get_type(void);

gboolean gst_rtp_atlas_depay_plugin_init(GstPlugin *plugin);

gboolean gst_rtp_atlas_add_asps_afps_aaps(GstElement *rtpatlas, GPtrArray *asps,
                                       GPtrArray *afps, GPtrArray *aaps,
                                       GstBuffer *nal);

G_END_DECLS
#endif /* __GST_RTP_ATLAS_DEPAY_H__ */
