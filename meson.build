project('gst-plugins-atlas', 'c', version : '1.0.0', license : 'BSD-3-Clause-Clear')
plugins_install_dir = join_paths(get_option('libdir'), 'nokia')
cc = meson.get_compiler('c')
gst_atlas_version = meson.project_version()
api_version = '1.0'
gst_dep = dependency('gstreamer-1.0',
    fallback : ['gstreamer', 'gst_dep'])
gst_rtp_dep = dependency('gstreamer-rtp-1.0')
gst_base_dep = dependency('gstreamer-base-1.0')

plugin_c_args = ['-DHAVE_CONFIG_H']

cdata = configuration_data()
cdata.set_quoted('PACKAGE_VERSION', gst_atlas_version)
cdata.set_quoted('PACKAGE', 'gst-plugins-atlas')
cdata.set_quoted('GST_LICENSE', 'BSD-3-Clause-Clear')
cdata.set_quoted('GST_API_VERSION', api_version)
cdata.set_quoted('GST_PACKAGE_NAME', 'GStreamer RTP V3C Atlas Plug-ins')
cdata.set_quoted('GST_PACKAGE_ORIGIN', 'https://github.com/nokiatech')
configure_file(output : 'config.h', configuration : cdata)

gst_plugins_good_rtp_path = get_option('gst_plugins_good_rtp')
gst_plugins_good_rtp_path_inc = include_directories(gst_plugins_good_rtp_path)

atlas_sources = [
  'src/plugin.c',
  'src/gstrtpatlasdepay.c',
  'src/gstrtpatlaspay.c',
  'src/utils.c',
  gst_plugins_good_rtp_path+'/gstbuffermemory.c',
  gst_plugins_good_rtp_path+'/gstrtputils.c',
]

gstatlas = library('gstatlas',
  atlas_sources,
  c_args: plugin_c_args,
  dependencies : [gst_dep, gst_base_dep, gst_rtp_dep],
  install : true,
  install_dir : plugins_install_dir,
  include_directories : [gst_plugins_good_rtp_path_inc],
)