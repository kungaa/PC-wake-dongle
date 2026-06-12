/* Empty static file table: every URL is served by fs_open_custom() in
 * usb_net.cpp. Included by lwIP's fs.c via HTTPD_FSDATA_FILE (lwipopts.h). */

#define FS_ROOT     ((const struct fsdata_file *) NULL)
#define FS_NUMFILES 0
