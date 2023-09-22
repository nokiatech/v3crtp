# Atlas data payloader and depayloader for RTP Payload Format for Visual Volumetric Video-based Coding (V3C)

This repository provides [Gstreamer](https://gstreamer.freedesktop.org) plugins that implement atlas data payloader and depayloader for [RTP Payload Format for Visual Volumetric Video-based Coding (V3C)](https://datatracker.ietf.org/doc/draft-ietf-avtcore-rtp-v3c/03/).

## Prerequisites

 * Ninja (1.11.1), follow the [instructions](https://ninja-build.org/)

 * Meson ( > 0.55), follow the [instructions](https://mesonbuild.com/Getting-meson.html).

* GStreamer (1.22.5), runtime and development install, follow the [instruction](https://gstreamer.freedesktop.org/documentation/installing/index.html?gi-language=c).

* Gstreamer (1.22.5), [source](https://github.com/GStreamer/gstreamer.git) download

## Build

* for MacOS and Linux, run the following command lines in the terminal to compile the plugins. 

```
meson setup -Dbuildtype=release -Dgst_plugins_good_rtp=/path/to/gstreamer/subprojects/gst-plugins-good/gst/rtp build
ninja -C build
ninja -C build install
```

* for Windows, run the following command line in the terminal to create the solution for Microsoft Visual Studio (e.g., v2019, 64 bit). Once the solution is created, open the solution and compile the plugins.

```
meson setup -Dbuildtype=release -Dgst_plugins_good_rtp=/path/to/gstreamer/subprojects/gst-plugins-good/gst/rtp build --backend vs
```

Once the plugins are compiled append or add the environment variable *GST_PLUGIN_PATH* to point at the directory containing the compiled plugins (e.g, ./gst-plugins-atlas/build).

## Plugins description
The repository provides two plugins: 

* [RTP atlas payloader](#rtpatlaspay)
* [RTP atlas depayloader](#rtpatlasdepy)

### rtpatlaspay

RTP atlas payloader plugin takes as input stream-format according to [ISO/IEC 23090-10](<https://www.iso.org/standard/78991.html>).

* The input stream-format should be with fourCC code equal to 'v3cg' or 'v3ag', where each timed sample contain one coded atlas access unit as defined in [ISO/IEC 23090-5](<https://www.iso.org/standard/73025.html>).
 * SINK capabilities shall provide [codec_data](#codec_data) that at least signal the value of 'unit_size_precision_bytes_minus1' which is used to parse the samples. 
 * SINK capabilities may provide [vuh_data](#vuh_data).
 
The SINK pad capabilities are shown below.

```json
Pad Template:
  SINK template: 'sink'
    Availability: Always
    Capabilities:
      video/x-atlas
          stream-format: [ v3cg, v3ag ] 
              alignment: au
             codec_data: ANY
      /* optional parameters */
            /* vuh_data: ANY */
```
RTP atlas payloader outputs RTP packets that encapsulate atlas NAL units according to [RTP Payload Format for Visual Volumetric Video-based Coding (V3C)](https://datatracker.ietf.org/doc/draft-ietf-avtcore-rtp-v3c/03/). 

Based on the incoming data characteristics and settings of the plugin the following RTP payload types may be created: 
* Single NAL unit packet,
* Aggregation packet, or 
* Fragmentation unit.  

Depending on the content of codec_data and presence of vuh_data, the plugin may provide [optional parameters](https://www.ietf.org/archive/id/draft-ietf-avtcore-rtp-v3c-03.html#name-optional-parameters-definit) on the SRC pad. The optional parameter may be utilized by the application to create session description protocol (SDP) file.

The SRC pad capabilities are shown below.

```json
Pad Template:
  SRC template: 'src'
    Availability: Always
    Capabilities:
      application/x-rtp
                  media: application
                payload: [ 96, 127 ]
             clock-rate: 90000
          encoding-name: v3c
      /* optional parameters */
     /* v3c-unit-header: ANY,*/
       /* v3c-unit-type: [ 0, 31], */
        /* v3c-atlas-id: [ 0, 65], */
          /* v3c-vps-id: [ 0, 15], */
        /* v3c-parameter-set: ANY, */
      /* v3c-atlas-data: ANY, */
             /* v3c-sei: ANY, */
   /* v3c-ptl-level-idc: [ 0, 65],*/
   /* v3c-ptl-tier-flag: [ 0, 1], */
   /* v3c-ptl-codec-idc: [0, 127], */
 /* v3c-ptl-toolset-idc: [0, 255], */
     /* v3c-ptl-rec-idc: [0, 255], */
```


### rtpatlasdepay

RTP atlas depayloader plugin takes as input RTP stream with encoding-name 'v3c'. 
 * The SINK pad can provide [optional parameters](https://www.ietf.org/archive/id/draft-ietf-avtcore-rtp-v3c-03.html#name-optional-parameters-definit), e.g., provided to the client via SDP file. 

The SINK pad capabilities are shown below.

```json
Pad Templates:
  SINK template: 'sink'
    Availability: Always
    Capabilities:
      application/x-rtp
                  media: application
             clock-rate: 90000
          encoding-name: v3c
      /* optional parameters */
     /* v3c-unit-header: ANY,*/
        /* v3c-parameter-set: ANY, */
      /* v3c-atlas-data: ANY, */
             /* v3c-sei: ANY, */
       /* v3c-unit-type: [ 0, 31], */
          /* v3c-vps-id: [ 0, 15], */
        /* v3c-atlas-id: [ 0, 65], */
   /* v3c-ptl-level-idc: [ 0, 65],*/
   /* v3c-ptl-tier-flag: [ 0, 1], */
   /* v3c-ptl-codec-idc: [0, 127], */
 /* v3c-ptl-toolset-idc: [0, 255], */
     /* v3c-ptl-rec-idc: [0, 255], */
```
RTP atlas depayloader outputs stream-format according to [ISO/IEC 23090-10](<https://www.iso.org/standard/78991.html>) with fourCC code equal to 'v3cg' or 'v3ag', where each timed sample contain one coded atlas access unit as defined in [ISO/IEC [23090-5](<https://www.iso.org/standard/73025.html>).
 * The plugin creates [codec_data](#codec_data) based on the optional parameters provided on the SINK pad, with 'unit_size_precision_bytes_minus1' equal to 3. 
 * The plugin may also provide [vuh_data](#vuh_data) if the optional parameter v3c-unit-header is provided on SINK pad.

The SRC pad capabilities are shown below.

```json
Pad Template:
  SRC template: 'src'
    Availability: Always
    Capabilities:
      video/x-atlas
          stream-format: [ v3cg, v3ag ]
              alignment: au
             codec_data: ANY
      /* optional parameters */
            /* vuh_data: ANY */
```
### codec_data

The codec_data contains bytes representing V3CDecoderConfigurationRecord() syntax element defined in [ISO/IEC 23090-10](<https://www.iso.org/standard/78991.html>) and it is as follow:

```c
unsigned int(3) unit_size_precision_bytes_minus1;
unsigned int(5) num_of_v3c_parameter_sets;
for (int i=0; i < num_of_v3c_parameter_sets; i++) {
    unsigned int(16) v3c_parameter_set_length;
    v3c_parameter_set(v3c_parameter_set_length); 
}
unsigned int(8) num_of_setup_unit_arrays;
for (int j=0; j < num_of_setup_unit_arrays; j++) {
    unsigned int(1) array_completeness;
    unsigned int(1) reserved = 0;
    unsigned int(6) nal_unit_type;
    unsigned int(8) num_nal_units;
    for (int i=0; i < num_nal_units; i++) {
        unsigned int(16) setup_unit_length;
        setup_unit(setup_unit_length);  
	}
}
```

### vuh_data

The vuh_data contains four bytes representing v3c_unit_header() syntax element defined in [ISO/IEC 23090-5](https://www.iso.org/standard/73025.html) and it is as follow:

```c
unsigned int(5) vuh_unit_type
if( vuh_unit_type == 1 || vuh_unit_type == 2 || vuh_unit_type == 3 || 
    vuh_unit_type == 4 || vuh_unit_type == 5 || vuh_unit_type == 6 ) {
    unsigned int(4) vuh_v3c_parameter_set_id;
}
if( vuh_unit_type == 1 || vuh_unit_type == 2 || vuh_unit_type == 3 || 
    vuh_unit_type == 4 || vuh_unit_type == 5 ) {
    unsigned int(6) vuh_atlas_id;
}
if( vuh_unit_type == 4 ) {
    unsigned int(7) vuh_attribute_index;
    unsigned int(5) vuh_attribute_partition_index;
    unsigned int(4) vuh_map_index;
    unsigned int(1) vuh_auxiliary_video_flag;
} else if( vuh_unit_type == 3 ) {
    unsigned int(4)  vuh_map_index;
    unsigned int(1)  vuh_auxiliary_video_flag;
    unsigned int(12) vuh_reserved_zero_12bits;
} else if( vuh_unit_type == 1 || vuh_unit_type == 2 || vuh_unit_type == 5 ) {
    unsigned int(17) vuh_reserved_zero_17bits;
} else if( vuh_unit_type == 6 ) {
    unsigned int(23) vuh_reserved_zero_23bits;
} else {
    unsigned int(27) vuh_reserved_zero_27bits;
}
```

## Example pipelines 

An example of sender and receiver pipelines. For simplicity only one V3C video component is present. 

### Sender side

```
                          |-------------------|     V3C video component          |------------|                         |---------|
                          |                   |--------------------------------->| rtph265pay |------------------------>| udpsink |
                          |                   |    video/x-h265                  |------------|   application/x-rtp     |---------|
                          |                   |    stream-format: hvc1                            media: application
|-----------------|       |                   |    alignment: au                                  payload: 96
| filesrc         |       |      demux        |    codec_data: H265DecoderConfifurationRecord     encoding-name: H265
| (e.g., ISOBMFF) |------>| (e.g., ISOBMFF )  |  
|---------------- |       |                   | 
                          |                   |    V3 atlas component           |-------------|                         |---------|
                          |                   |-------------------------------->| rtpatlaspay |------------------------>| udpsink |
                          |-------------------|    video/x-atlas                |-------------|   application/x-rtp     |---------|
                                                   stream-format: v3c1                            media: application
                                                   alignment: au                                  payload: 97
                                                   codec_data: 21001f0102ff00000fff3c...          encoding-name: v3c
                                                   vuh_data: 08000000                             v3c-unit-header: "CAAAAA\=\="
                                                                                                  v3c-unit-type: 1
                                                                                                  v3c-atlas-id: 0
                                                                                                  v3c-vps-id: 0
                                                                                                  v3c-parameter-set: "AQL/AAAP/zwAAAAAACg... "
                                                                                                  v3c-ptl-level-idc: 60
                                                                                                  v3c-ptl-tier-flag: 0
                                                                                                  v3c-ptl-codec-idc: 1
                                                                                                  v3c-ptl-toolset-idc: 2
                                                                                                  v3c-ptl-rec-idc: 255
```

### Receiver side

``` 
|--------|                                 |---------------|                              |---------|       |-----------------|
| udpsrc |-------------------------------->|  rtph265depay | ---------------------------->| x265dec |------>|                 |
|--------|   application/x-rtp             |---------------|    video/x-h265              |---------|       |                 |
             media: application                                 stream-format: hvc1                         |                 |   
             payload: 96                                        alignment: au                               |   V3C renderer  |
             encoding-name: H265                                                                            |                 |
                                                                                                            |                 |                                                
|--------|                                 |----------------|                            |----------|      |                 |
| udpsrc |-------------------------------->|  rtpatlasdepay |--------------------------->| atlasdec |----->|                 |
|--------|   application/x-rtp             |----------------|   video/x-atlas            |----------|      |-----------------|
             media: application                                 stream-format: v3c1                         
             payload: 97                                        alignment: au                             
             encoding-name: v3c                                 codec_data: 21001f0...  
             v3c-unit-header: "CAAAAA\=\="                      vuh_data: 08000000    
             v3c-unit-type: 1
             v3c-atlas-id: 0
             v3c-vps-id: 0
             v3c-parameter-set: "AQL/AAAP/zwAAAAAACg... "
             v3c-ptl-level-idc: 60
             v3c-ptl-tier-flag: 0 
             v3c-ptl-codec-idc: 1
             v3c-ptl-toolset-idc: 2
             v3c-ptl-rec-idc: 255                                    
```

## Limitations
* In the implementation it is assumed DONL field is not present, i.e., tx-mode == "SRST" and sprop-max-don-diff = 0
* In the implementation it is assumed v3c-tile-id field is not present, i.e., v3c-tile-id-pres = 0
* In the implementation only atlas data is considered (no support for common atlas data, V3C_CAD). 

> **Note**
> When MIV bitstream contains static common atlas data for the duration of a sequence, the common atlas data can be provided out of band utilizing optional parameter [v3c-common-atlas-data](<https://www.ietf.org/archive/id/draft-ietf-avtcore-rtp-v3c-03.html#name-optional-parameters-definit>)


## License

Please see LICENSE.TXT file for the terms of use of the contents of this repository.

Copyright (c) 2023 Nokia Corporation and/or its subsidiary(ies).

All rights reserved.