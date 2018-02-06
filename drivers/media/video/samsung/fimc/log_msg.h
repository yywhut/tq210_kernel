/* linux/drivers/media/video/samsung/jpeg/jpg_msg.h
 *
 * Driver header file for Samsung JPEG Encoder/Decoder
 *
 * Peter Oh, Copyright (c) 2009 Samsung Electronics
 * 	http://www.samsungsemi.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __SAMSUNG_SYSLSI_APDEV_log_msg_H__
#define __SAMSUNG_SYSLSI_APDEV_log_msg_H__

typedef enum log_level_t
{
	LOG_ERROR,
	LOG_WARNING,
	LOG_NOTE,
	LOG_DBG
} LOG_LEVEL;


#ifdef __cplusplus
extern "C" {
#endif

//#define log_msg(level,...) _log_msg(level, __func__, ##__VA_ARGS__)
#define log_msg(level,...)

void _log_msg(LOG_LEVEL level, const char *func_name, const char *msg, ...);

#ifdef __cplusplus
}
#endif

#endif /* __SAMSUNG_SYSLSI_APDEV_log_msg_H__ */
