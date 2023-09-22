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

#include "utils.h"
#include <stdio.h>

guint8 gst_codec_data_get_unit_size_precision_bytes_minus1(GstBuffer *buffer) {

  GstMapInfo map;
  guint8 *data = NULL;

  guint8 unit_size_precision_bytes_minus1 = 0;
  gst_buffer_map(buffer, &map, GST_MAP_READ);
  data = map.data;
  unit_size_precision_bytes_minus1 = (data[0] >> 5) & 0x07;
  gst_buffer_unmap(buffer, &map);

  return unit_size_precision_bytes_minus1;
}

GstBuffer *gst_codec_data_get_vps_unit(GstBuffer *buffer) {
  GstMapInfo map;
  guint8 *data = NULL;
  guint8 num_of_v3c_parameter_sets = 0;
  guint16 v3c_parameter_set_length = 0;
  
  gst_buffer_map(buffer, &map, GST_MAP_READ);
  data = map.data;
  num_of_v3c_parameter_sets = data[0] & 0x1F;
  
  if (num_of_v3c_parameter_sets != 1) {
    GST_ERROR("num_of_v3c_parameter_sets shall be equal to 1 according "
              "to ISO/IEC 23090-10");
    gst_buffer_unmap(buffer, &map);
    return NULL;
  }

  v3c_parameter_set_length = data[1];
  v3c_parameter_set_length = (v3c_parameter_set_length << 8) | data[2];
  gst_buffer_unmap(buffer, &map);

  return gst_buffer_copy_region(buffer, GST_BUFFER_COPY_ALL, 3,
                               v3c_parameter_set_length);
}

guint8 gst_vuh_data_get_v3c_parameter_set_id(GstBuffer *buffer) {

  GstMapInfo map;
  guint8 *data = NULL;

  guint8 v3c_parameter_set_id = 0;
  gst_buffer_map(buffer, &map, GST_MAP_READ);
  data = map.data;
  v3c_parameter_set_id = ((data[0] << 5) & 0xE0) | ((data[1] >> 7) & 0x01);
  gst_buffer_unmap(buffer, &map);

  return v3c_parameter_set_id;
}

guint8 gst_vuh_data_get_atlas_id(GstBuffer *buffer) {

  GstMapInfo map;
  guint8 *data = NULL;

  guint8 atlas_id = 0;
  gst_buffer_map(buffer, &map, GST_MAP_READ);
  data = map.data;
  atlas_id = (data[1] >> 1) & 0x3F;
  gst_buffer_unmap(buffer, &map);

  return atlas_id;
}

guint8 gst_vuh_data_get_unit_type(GstBuffer *buffer) {

  GstMapInfo map;
  guint8 *data = NULL;

  guint8 unit_type = 0;
  gst_buffer_map(buffer, &map, GST_MAP_READ);
  data = map.data;
  unit_type = (data[0] >> 3) & 0x1F;
  gst_buffer_unmap(buffer, &map);

  return unit_type;
}

guint8 gst_vps_data_get_ptl_tier_flag(GstBuffer *buffer) {
  GstMapInfo map;
  guint8 *data = NULL;

  guint8 ptl_tier_flag = 0;
  gst_buffer_map(buffer, &map, GST_MAP_READ);
  data = map.data;
  ptl_tier_flag = (data[0] >> 7) & 0x01;
  gst_buffer_unmap(buffer, &map);

  return ptl_tier_flag;
}

guint8 gst_vps_data_get_ptl_codec_idc(GstBuffer *buffer) {
  GstMapInfo map;
  guint8 *data = NULL;

  guint8 ptl_codec_idc = 0;
  gst_buffer_map(buffer, &map, GST_MAP_READ);
  data = map.data;
  ptl_codec_idc = data[0] & 0x7F;
  gst_buffer_unmap(buffer, &map);

  return ptl_codec_idc;
}

guint8 gst_vps_data_get_ptl_toolset_idc(GstBuffer *buffer) {
  GstMapInfo map;
  guint8 *data = NULL;

  guint8 ptl_toolset_idc = 0;
  gst_buffer_map(buffer, &map, GST_MAP_READ);
  data = map.data;
  ptl_toolset_idc = data[1];
  gst_buffer_unmap(buffer, &map);

  return ptl_toolset_idc;
}

guint8 gst_vps_data_get_ptl_rec_idc(GstBuffer *buffer) {
  GstMapInfo map;
  guint8 *data = NULL;

  guint8 ptl_rec_idc = 0;
  gst_buffer_map(buffer, &map, GST_MAP_READ);
  data = map.data;
  ptl_rec_idc = data[2];
  gst_buffer_unmap(buffer, &map);

  return ptl_rec_idc;
}

guint8 gst_vps_data_get_ptl_level_idc(GstBuffer *buffer) {
  GstMapInfo map;
  guint8 *data = NULL;

  guint8 ptl_level_idc = 0;
  gst_buffer_map(buffer, &map, GST_MAP_READ);
  data = map.data;
  ptl_level_idc = data[7];
  gst_buffer_unmap(buffer, &map);

  return ptl_level_idc;
}
