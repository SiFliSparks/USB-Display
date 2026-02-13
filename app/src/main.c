/*
 * SPDX-FileCopyrightText: 2025 SiFli Technologies(Nanjing) Co, Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * USB Display Device Main
 * Compatible with: chuanjinpang/win10_idd_xfz1986_usb_graphic_driver_display
 *
 * The MCU acts as a USB display device:
 *   PC (Windows with IDD driver) --USB--> MCU ---> LCD Screen
 */
#include "rtthread.h"
#include "bf0_hal.h"
#include "usb_display.h"

#define DISP_THREAD_STACK_SIZE  4096
#define DISP_THREAD_PRIORITY    3

static void display_thread_entry(void *parameter)
{
    (void)parameter;
    usb_display_task(0);
}

/**
 * @brief  Main program
 * @param  None
 * @retval 0 if success, otherwise failure number
 */
int main(void)
{
    rt_kprintf("\n");
    rt_kprintf("╔═══════════════════════════════════════════════════════╗\n");
    rt_kprintf("║     CherryUSB Display Device Demo                    ║\n");
    rt_kprintf("║     Compatible: Windows IDD Driver                    ║\n");
    rt_kprintf("║     Resolution: %d x %d RGB565                      ║\n", 
               USB_DISP_WIDTH, USB_DISP_HEIGHT);
    rt_kprintf("╚═══════════════════════════════════════════════════════╝\n");
    rt_kprintf("\n");
    
    /* Initialize USB display device stack */
    usb_display_init(0, (uintptr_t)USBC_BASE);
    
    /* Create display task thread to process received frames */
    rt_thread_t tid = rt_thread_create("usb_disp",
                                       display_thread_entry,
                                       RT_NULL,
                                       DISP_THREAD_STACK_SIZE,
                                       DISP_THREAD_PRIORITY,
                                       10);
    
    if (tid != RT_NULL) {
        rt_thread_startup(tid);
        rt_kprintf("[INFO] USB Display thread started\n");
    } else {
        rt_kprintf("[ERROR] Failed to create USB Display thread\n");
        return -1;
    }
    
    /* Main loop */
    while (1) {
        rt_thread_mdelay(1);
    }
    
    return 0;
}