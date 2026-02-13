/*
 * SPDX-FileCopyrightText: 2025 SiFli Technologies(Nanjing) Co, Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _USB_DISPLAY_H
#define _USB_DISPLAY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Display Configuration */
#define USB_DISP_WIDTH   720
#define USB_DISP_HEIGHT  720

/*
 * Function Declaration
 */
void usb_display_init(uint8_t busid, uint32_t reg_base);
void usb_display_task(uint8_t busid);

#ifdef __cplusplus
}
#endif

#endif /* _USB_DISPLAY_H */