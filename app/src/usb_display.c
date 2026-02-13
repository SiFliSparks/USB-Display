/*
 * SPDX-FileCopyrightText: 2026 SiFli Technologies(Nanjing) Co, Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "rtthread.h"
#include "rtdevice.h"
#include "usbd_core.h"
#include "usbd_display.h"
#include "usb_display.h"
#include "mem_section.h"

/* ======================== USB Configuration ======================== */
#define USBD_VID           0x303A 
#define USBD_PID           0x2987  
#define USBD_MAX_POWER     100     /* 100mA */
#define USBD_LANGID_STRING 1033

#define DISPLAY_OUT_EP     0x02    
#define DISPLAY_IN_EP      0x81    

/* Endpoint Max Packet Size */
#ifdef CONFIG_USB_HS
#define DISPLAY_EP_MPS 512  /* High Speed */
#else
#define DISPLAY_EP_MPS 64   /* Full Speed */
#endif

/* Frame Buffer Configuration */
#define FRAME_BUFSIZE      (720 * 720 * 2)  /* RGB565 ,If you need to change the resolution, please modify it here: change (720 * 720 * 2) to (width * height * 2).*/
#define FRAME_COUNT        2                /* Double buffering */

/* Config descriptor size: Config(9) + Interface(9) + EP_OUT(7) + EP_IN(7) = 32 */
#define USB_CONFIG_SIZE 32

/* ======================== USB Descriptors ======================== */

static const uint8_t device_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0x00, 0x00, 0x00, 
                               USBD_VID, USBD_PID, 0x0101, 0x01)
};

static const uint8_t config_descriptor[] = {
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x01, 0x01, 
                               USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    
    /* Interface 0: Vendor-specific Display Class */
    USB_INTERFACE_DESCRIPTOR_INIT(0x00, 0x00, 0x02, 0xff, 0x00, 0x00, 0x00),
    
    /* Bulk OUT Endpoint (PC -> MCU) */
    USB_ENDPOINT_DESCRIPTOR_INIT(DISPLAY_OUT_EP, 0x02, DISPLAY_EP_MPS, 0x00),
    
    /* Bulk IN Endpoint (MCU -> PC) */
    USB_ENDPOINT_DESCRIPTOR_INIT(DISPLAY_IN_EP, 0x02, DISPLAY_EP_MPS, 0x00),
};

static const uint8_t device_quality_descriptor[] = {
    USB_DEVICE_QUALIFIER_DESCRIPTOR_INIT(USB_2_0, 0x00, 0x00, 0x00, 0x01),
};

/*
 * 
 * The Windows IDD driver parses this string to configure the display!
 * Format: "cherryusb_R<width>x<height>_E<encoding><quality>_Fps<fps>_Bl<buffer_kb>"
 * Change the resolution here to the resolution of the hardware you are using.
 * Example: "cherryusb_R640x480_Ergb16_Fps30_Bl128"
 * 
 * Fields:
 *   - R390x450   : Resolution 390x450
 *   - Ergb16     : Encoding RGB565 (16-bit)
 *   - Fps30      : Target 30 FPS
 *   - Bl256      : Buffer limit 256KB (actual frame is 351KB, but limit compression)
 */
static const char *string_descriptors[] = {
    (const char[]){ 0x09, 0x04 },                      /* Langid */
    "CherryUSB",                                        /* Manufacturer */
    "cherryusb_R720x720_Ergb16_Fps12",           /* Product  */
    "2025020800",                                       /* Serial Number */
};

static const uint8_t *device_descriptor_callback(uint8_t speed)
{
    (void)speed;
    return device_descriptor;
}

static const uint8_t *config_descriptor_callback(uint8_t speed)
{
    (void)speed;
    return config_descriptor;
}

static const uint8_t *device_quality_descriptor_callback(uint8_t speed)
{
    (void)speed;
    return device_quality_descriptor;
}

static const char *string_descriptor_callback(uint8_t speed, uint8_t index)
{
    (void)speed;
    if (index >= (sizeof(string_descriptors) / sizeof(char *))) {
        return NULL;
    }
    return string_descriptors[index];
}

const struct usb_descriptor usb_display_descriptor = {
    .device_descriptor_callback = device_descriptor_callback,
    .config_descriptor_callback = config_descriptor_callback,
    .device_quality_descriptor_callback = device_quality_descriptor_callback,
    .string_descriptor_callback = string_descriptor_callback,
    .msosv1_descriptor = NULL,
    .msosv2_descriptor = NULL,
    .webusb_url_descriptor = NULL,
    .bos_descriptor = NULL,
};

/* ======================== Frame Buffers (PSRAM) ======================== */

#define FRAME_POOL_SIZE (FRAME_COUNT * FRAME_BUFSIZE)

/* L2_RET_BSS_SECT is a single-argument macro that acts as a section attribute */
L2_RET_BSS_SECT(udisp_frames) static uint8_t psram_frame_pool[FRAME_POOL_SIZE];

static struct usbd_display_frame s_frames[FRAME_COUNT];

/* ======================== USB Event Handler ======================== */

static void usbd_event_handler(uint8_t busid, uint8_t event)
{
    (void)busid;
    
    switch (event) {
    case USBD_EVENT_RESET:
        rt_kprintf("[USB] RESET\n");
        break;
    case USBD_EVENT_CONNECTED:
        rt_kprintf("[USB] CONNECTED\n");
        break;
    case USBD_EVENT_DISCONNECTED:
        rt_kprintf("[USB] DISCONNECTED\n");
        break;
    case USBD_EVENT_CONFIGURED:
        rt_kprintf("[USB] CONFIGURED - Ready to receive frames\n");
        break;
    case USBD_EVENT_SUSPEND:
        rt_kprintf("[USB] SUSPEND\n");
        break;
    case USBD_EVENT_RESUME:
        rt_kprintf("[USB] RESUME\n");
        break;
    default:
        break;
    }
}

/* ======================== Initialization ======================== */

static struct usbd_interface s_display_intf;

void usb_display_init(uint8_t busid, uint32_t reg_base)
{   /* Initialize frame buffers using fixed offset in PSRAM pool */
    for (uint32_t i = 0; i < FRAME_COUNT; i++) {
        s_frames[i].frame_buf = &psram_frame_pool[i * FRAME_BUFSIZE];
        s_frames[i].frame_bufsize = FRAME_BUFSIZE;
        
        rt_kprintf("Frame[%d]: buf=0x%08X, size=%d KB\n",
                   i, (uint32_t)s_frames[i].frame_buf, FRAME_BUFSIZE / 1024);
    }
    
    /* Register USB descriptors */
    usbd_desc_register(busid, &usb_display_descriptor);
    
    /* Initialize display interface and add endpoints */
    usbd_add_interface(busid, usbd_display_init_intf(&s_display_intf,
                       DISPLAY_OUT_EP, DISPLAY_IN_EP,
                       s_frames, FRAME_COUNT));
    
    /* Initialize USB device stack */
    usbd_initialize(busid, reg_base, usbd_event_handler);
    
    rt_kprintf("USB Display initialized successfully!\n");
}

/* ======================== LCD Output Layer ======================== */

static rt_device_t s_lcd_device = RT_NULL;
static struct rt_semaphore lcd_sema;

/* LCD flush completion callback */
static rt_err_t lcd_flush_done(rt_device_t dev, void *buffer)
{
    (void)dev;
    (void)buffer;
    rt_sem_release(&lcd_sema);
    return RT_EOK;
}

/* Initialize LCD output */
static int lcd_output_init(void)
{
    /* Find and open LCD device */
    s_lcd_device = rt_device_find("lcd");
    if (!s_lcd_device) {
        rt_kprintf("[LCD] Device 'lcd' not found\n");
        return -1;
    }
    
    if (rt_device_open(s_lcd_device, RT_DEVICE_OFLAG_RDWR) != RT_EOK) {
        rt_kprintf("[LCD] Failed to open device\n");
        return -1;
    }
    
    /* Get LCD info */
    struct rt_device_graphic_info info;
    if (rt_device_control(s_lcd_device, RTGRAPHIC_CTRL_GET_INFO, &info) == RT_EOK) {
        rt_kprintf("[LCD] Info: %dx%d, %d bpp\n", 
                   info.width, info.height, info.bits_per_pixel);
        
        /* Verify resolution match */
        if (info.width != USB_DISP_WIDTH || info.height != USB_DISP_HEIGHT) {
            rt_kprintf("[LCD]Warning: Resolution mismatch! "
                       "LCD=%dx%d, USB=%dx%d\n",
                       info.width, info.height, 
                       USB_DISP_WIDTH, USB_DISP_HEIGHT);
        }
    }
    
    /* Initialize semaphore for LCD sync */
    rt_sem_init(&lcd_sema, "lcd_sem", 0, RT_IPC_FLAG_FIFO);
    
    rt_kprintf("[LCD] Initialized successfully\n");
    return 0;
}

/*
 * Flush pixel data to LCD
 * @param x, y: Top-left corner
 * @param width, height: Region size
 * @param data: RGB565 pixel data
 * @param size: Data size in bytes
 */
static void lcd_output_flush(uint16_t x, uint16_t y, 
                            uint16_t width, uint16_t height,
                            const uint8_t *data, uint32_t size)
{
    if (!s_lcd_device) {
        rt_kprintf("[LCD] Device not initialized\n");
        return;
    }
    
    /* Validate parameters */
    if (x + width > USB_DISP_WIDTH || y + height > USB_DISP_HEIGHT) {
        rt_kprintf("[LCD] Invalid region: (%d,%d) %dx%d\n", x, y, width, height);
        return;
    }
    
    uint32_t expected_size = (uint32_t)width * height * 2;  /* RGB565 */
    if (size < expected_size) {
        rt_kprintf("[LCD] Data size mismatch: %d < %d\n", size, expected_size);
        return;
    }
    
    /* Set buffer format to RGB565 */
    uint16_t color_format = RTGRAPHIC_PIXEL_FORMAT_RGB565;
    rt_device_control(s_lcd_device, RTGRAPHIC_CTRL_SET_BUF_FORMAT, &color_format);
    
    /* Set LCD window */
    rt_graphix_ops(s_lcd_device)->set_window(x, y, x + width - 1, y + height - 1);
    
    /* Register flush completion callback */
    rt_device_set_tx_complete(s_lcd_device, lcd_flush_done);
    
    /* Start asynchronous draw */
    rt_graphix_ops(s_lcd_device)->draw_rect_async((const char *)data, 
                                                   x, y, 
                                                   x + width - 1, 
                                                   y + height - 1);
    
    /* Wait for flush to complete */
    rt_sem_take(&lcd_sema, RT_WAITING_FOREVER);
}

/* ======================== Frame Processing ======================== */

/*
 * Frame Header Structure (must match usbd_display.c)
 * Total: 16 bytes
 */
struct usb_disp_frame_header {
    uint16_t crc16;              /* CRC16 checksum */
    uint8_t  type;               /* 0=RGB565, 3=JPEG */
    uint8_t  cmd;                /* Command field */
    uint16_t x;                  /* X coordinate */
    uint16_t y;                  /* Y coordinate */
    uint16_t width;              /* Width */
    uint16_t height;             /* Height */
    uint32_t frame_id      : 10; /* Frame ID (0-1023) */
    uint32_t payload_total : 22; /* Payload size (max 4MB) */
} __attribute__((packed));

#define FRAME_HEADER_SIZE 16

/* ======================== Display Task ======================== */

void usb_display_task(uint8_t busid)
{
    (void)busid;
    struct usbd_display_frame *frame = NULL;
    int ret;
    uint32_t frame_count = 0;
    uint32_t total_bytes = 0;
    
    /* Initialize LCD output */
    if (lcd_output_init() != 0) {
        rt_kprintf("[ERROR] LCD initialization failed, task exit\n");
        return;
    }
    
    rt_kprintf("[INFO] Waiting for frames from PC...\n\n");
    
    /* Main processing loop */
    while (1) {
        /* Block and wait for a frame from USB */
        ret = usbd_display_dequeue(&frame, RT_WAITING_FOREVER);
        if (ret != 0 || frame == NULL) {
            rt_thread_mdelay(1);
            continue;
        }
        
        frame_count++;
        
        /* Parse frame header */
        struct usb_disp_frame_header *header = 
            (struct usb_disp_frame_header *)frame->frame_buf;
        
        uint16_t x      = header->x;
        uint16_t y      = header->y;
        uint16_t width  = header->width;
        uint16_t height = header->height;
        uint8_t  type   = header->type;
        uint32_t payload_size = header->payload_total;
        uint32_t frame_id = header->frame_id;
        
        /* Validate frame */
        if (width == 0 || height == 0 ||
            x + width > USB_DISP_WIDTH || y + height > USB_DISP_HEIGHT) {
            rt_kprintf("[WARN] Frame #%u out of bounds: (%u,%u) %ux%u\n",
                       frame_count, x, y, width, height);
            goto recycle;
        }
        
        /* Calculate expected size */
        uint32_t expected_pixels = (uint32_t)width * height * 2;  /* RGB565 */
        
        if (payload_size < expected_pixels) {
            rt_kprintf("[WARN] Frame #%u payload too small: %u < %u\n",
                       frame_count, payload_size, expected_pixels);
            goto recycle;
        }
        
        /* Process based on format type */
        if (type == 0) {  /* RGB565 */
            /* Pixel data starts after header */
            uint8_t *pixel_data = frame->frame_buf + FRAME_HEADER_SIZE;
            
            /* Log frame info (every 30 frames to reduce spam) */
            if (frame_count % 30 == 1) {
                rt_kprintf("[Frame #%u] ID=%u, Region=(%u,%u) %ux%u, Size=%u bytes\n",
                           frame_count, frame_id, x, y, width, height, payload_size);
            }
            
            /* Flush to LCD */
            lcd_output_flush(x, y, width, height, pixel_data, payload_size);
            
            total_bytes += payload_size;
            
        } else if (type == 3) {  /* JPEG */
            rt_kprintf("[WARN] JPEG format not supported (frame #%u)\n", frame_count);
            /* TODO: Add JPEG decoder support if needed */
            
        } else {
            rt_kprintf("[WARN] Unknown format type: %u (frame #%u)\n", 
                       type, frame_count);
        }
        
recycle:
        /* Return frame buffer to pool for reuse */
        usbd_display_enqueue(frame);
        frame = NULL;
        
        /* Print statistics every 100 frames */
        if (frame_count % 100 == 0) {
            uint32_t avg_size = total_bytes / frame_count;
            rt_kprintf("\n[Stats] Frames=%u, Total=%u MB, Avg=%u KB/frame\n\n",
                       frame_count, total_bytes / (1024 * 1024), avg_size / 1024);
        }
    }
}