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

#ifndef __UTILS_H__
#define __UTILS_H__

#include <gst/base/gstbytereader.h>
#include <gst/gst.h>

G_BEGIN_DECLS
typedef enum {
  GST_ATLAS_NAL_TRAIL_N = 0,
  GST_ATLAS_NAL_TRAIL_R = 1,
  GST_ATLAS_NAL_TSA_N = 2,
  GST_ATLAS_NAL_TSA_R = 3,
  GST_ATLAS_NAL_STSA_N = 4,
  GST_ATLAS_NAL_STSA_R = 5,
  GST_ATLAS_NAL_RADL_N = 6,
  GST_ATLAS_NAL_RADL_R = 7,
  GST_ATLAS_NAL_RASL_N = 8,
  GST_ATLAS_NAL_RASL_R = 9,
  GST_ATLAS_NAL_SKIP_N = 10,
  GST_ATLAS_NAL_SKIP_R = 11,
  GST_ATLAS_NAL_BLA_W_LP = 16,
  GST_ATLAS_NAL_BLA_W_RADL = 17,
  GST_ATLAS_NAL_BLA_N_LP = 18,
  GST_ATLAS_NAL_GBLA_W_LP = 19,
  GST_ATLAS_NAL_GBLA_W_RADL = 20,
  GST_ATLAS_NAL_GBLA_N_LP = 21,
  GST_ATLAS_NAL_IDR_W_RADL = 22,
  GST_ATLAS_NAL_IDR_N_LP = 23,
  GST_ATLAS_NAL_GIDR_W_RADL = 24,
  GST_ATLAS_NAL_GIDR_N_LP = 25,
  GST_ATLAS_NAL_CRA = 26,
  GST_ATLAS_NAL_GCRA = 27,
  GST_ATLAS_NAL_RSV_IRAP_ACL_29 = 29,
  GST_ATLAS_NAL_ASPS = 36,
  GST_ATLAS_NAL_AFPS = 37,
  GST_ATLAS_NAL_AUD = 38,
  GST_ATLAS_NAL_V3C_AUD = 39,
  GST_ATLAS_NAL_EOS = 40,
  GST_ATLAS_NAL_EOB = 41,
  GST_ATLAS_NAL_FD = 42,
  GST_ATLAS_NAL_PREFIX_NSEI = 43,
  GST_ATLAS_NAL_SUFFIX_NSEI = 44,
  GST_ATLAS_NAL_PREFIX_ESEI = 45,
  GST_ATLAS_NAL_SUFFIX_ESEI = 46,
  GST_ATLAS_NAL_AAPS = 47,
  GST_ATLAS_NAL_CASPS = 48,
  GST_ATLAS_NAL_CAF_IDR = 49,
  GST_ATLAS_NAL_CAF_TRAIL = 50
} GstAtlasNalUnitType;

guint8 gst_codec_data_get_unit_size_precision_bytes_minus1(GstBuffer *buffer);
GstBuffer* gst_codec_data_get_vps_unit(GstBuffer *buffer);
guint8 gst_vuh_data_get_v3c_parameter_set_id(GstBuffer *buffer);
guint8 gst_vuh_data_get_atlas_id(GstBuffer *buffer);
guint8 gst_vuh_data_get_unit_type(GstBuffer *buffer);
guint8 gst_vps_data_get_ptl_tier_flag(GstBuffer *buffer);
guint8 gst_vps_data_get_ptl_codec_idc(GstBuffer *buffer);
guint8 gst_vps_data_get_ptl_toolset_idc(GstBuffer *buffer);
guint8 gst_vps_data_get_ptl_rec_idc(GstBuffer *buffer);
guint8 gst_vps_data_get_ptl_level_idc(GstBuffer *buffer);

#endif