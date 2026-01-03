/*
 * This file is part of the TREZOR project, https://trezor.io/
 *
 * Copyright (c) SatoshiLabs
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>
#include <sys/types.h>

#include "common.h"
#include "mpu.h"
#include "image.h"
#include "flash.h"
#include "display.h"
#include "mini_printf.h"
#include "rng.h"
#include "secbool.h"
#include "touch.h"
#include "touch_calib.h"
#include "stmpe811.h"
#include "i2c_bus.h"
#include "usb.h"
#include "version.h"

#include "bootui.h"
#include "messages.h"

// Debug colors (RGB565 format)
#define COLOR_GREEN         0x07E0
#define COLOR_RED           0xF800
#define COLOR_GRAY          0x8410
// #include "mpu.h"

const uint8_t BOOTLOADER_KEY_M = 2;
const uint8_t BOOTLOADER_KEY_N = 3;
static const uint8_t * const BOOTLOADER_KEYS[] = {
    (const uint8_t *)"\xc2\xc8\x7a\x49\xc5\xa3\x46\x09\x77\xfb\xb2\xec\x9d\xfe\x60\xf0\x6b\xd6\x94\xdb\x82\x44\xbd\x49\x81\xfe\x3b\x7a\x26\x30\x7f\x3f",
    (const uint8_t *)"\x80\xd0\x36\xb0\x87\x39\xb8\x46\xf4\xcb\x77\x59\x30\x78\xde\xb2\x5d\xc9\x48\x7a\xed\xcf\x52\xe3\x0b\x4f\xb7\xcd\x70\x24\x17\x8a",
    (const uint8_t *)"\xb8\x30\x7a\x71\xf5\x52\xc6\x0a\x4c\xbb\x31\x7f\xf4\x8b\x82\xcd\xbf\x6b\x6b\xb5\xf0\x4c\x92\x0f\xec\x7b\xad\xf0\x17\x88\x37\x51",
// comment the lines above and uncomment the lines below to use a custom signed vendorheader
//    (const uint8_t *)"\xd7\x59\x79\x3b\xbc\x13\xa2\x81\x9a\x82\x7c\x76\xad\xb6\xfb\xa8\xa4\x9a\xee\x00\x7f\x49\xf2\xd0\x99\x2d\x99\xb8\x25\xad\x2c\x48",
//    (const uint8_t *)"\x63\x55\x69\x1c\x17\x8a\x8f\xf9\x10\x07\xa7\x47\x8a\xfb\x95\x5e\xf7\x35\x2c\x63\xe7\xb2\x57\x03\x98\x4c\xf7\x8b\x26\xe2\x1a\x56",
//    (const uint8_t *)"\xee\x93\xa4\xf6\x6f\x8d\x16\xb8\x19\xbb\x9b\xeb\x9f\xfc\xcd\xfc\xdc\x14\x12\xe8\x7f\xee\x6a\x32\x4c\x2a\x99\xa1\xe0\xe6\x71\x48",
};

#define USB_IFACE_NUM   0

static void usb_init_all(void) {

    static const usb_dev_info_t dev_info = {
        .device_class    = 0x00,
        .device_subclass = 0x00,
        .device_protocol = 0x00,
        .vendor_id       = 0x1209,
        .product_id      = 0x53C0,
        .release_num     = 0x0200,
        .manufacturer    = "SatoshiLabs",
        .product         = "TREZOR",
        .serial_number   = "000000000000000000000000",
        .interface       = "TREZOR Interface",
        .usb21_enabled   = sectrue,
        .usb21_landing   = sectrue,
    };

    static uint8_t rx_buffer[USB_PACKET_SIZE];

    static const usb_webusb_info_t webusb_info = {
        .iface_num        = USB_IFACE_NUM,
        .ep_in            = USB_EP_DIR_IN | 0x01,
        .ep_out           = USB_EP_DIR_OUT | 0x01,
        .subclass         = 0,
        .protocol         = 0,
        .max_packet_len   = sizeof(rx_buffer),
        .rx_buffer        = rx_buffer,
        .polling_interval = 1,
    };

    usb_init(&dev_info);

    ensure(usb_webusb_add(&webusb_info), NULL);

    usb_start();
}

static secbool bootloader_usb_loop(const vendor_header * const vhdr, const image_header * const hdr)
{
    usb_init_all();

    uint8_t buf[USB_PACKET_SIZE];

    for (;;) {
        int r = usb_webusb_read_blocking(USB_IFACE_NUM, buf, USB_PACKET_SIZE, USB_TIMEOUT);
        if (r != USB_PACKET_SIZE) {
            continue;
        }
        uint16_t msg_id;
        uint32_t msg_size;
        if (sectrue != msg_parse_header(buf, &msg_id, &msg_size)) {
            // invalid header -> discard
            continue;
        }
        switch (msg_id) {
            case 0: // Initialize
                process_msg_Initialize(USB_IFACE_NUM, msg_size, buf, vhdr, hdr);
                break;
            case 1: // Ping
                process_msg_Ping(USB_IFACE_NUM, msg_size, buf);
                break;
            case 5: // WipeDevice
                ui_fadeout();
                ui_screen_wipe_confirm();
                ui_fadein();
                int response = ui_user_input(INPUT_CONFIRM | INPUT_CANCEL);
                if (INPUT_CANCEL == response) {
                    ui_fadeout();
                    ui_screen_info(secfalse, vhdr, hdr);
                    ui_fadein();
                    send_user_abort(USB_IFACE_NUM, "Wipe cancelled");
                    break;
                }
                ui_fadeout();
                ui_screen_wipe();
                ui_fadein();
                r = process_msg_WipeDevice(USB_IFACE_NUM, msg_size, buf);
                if (r < 0) { // error
                    ui_fadeout();
                    ui_screen_fail();
                    ui_fadein();
                    usb_stop();
                    usb_deinit();
                    return secfalse; // shutdown
                } else { // success
                    ui_fadeout();
                    ui_screen_done(0, sectrue);
                    ui_fadein();
                    usb_stop();
                    usb_deinit();
                    return secfalse; // shutdown
                }
                break;
            case 6: // FirmwareErase
                process_msg_FirmwareErase(USB_IFACE_NUM, msg_size, buf);
                break;
            case 7: // FirmwareUpload
                r = process_msg_FirmwareUpload(USB_IFACE_NUM, msg_size, buf);
                if (r < 0 && r != -4) { // error, but not user abort (-4)
                    ui_fadeout();
                    ui_screen_fail();
                    ui_fadein();
                    usb_stop();
                    usb_deinit();
                    return secfalse; // shutdown
                } else
                if (r == 0) { // last chunk received
                    ui_screen_install_progress_upload(1000);
                    ui_fadeout();
                    ui_screen_done(4, sectrue);
                    ui_fadein();
                    ui_screen_done(3, secfalse);
                    hal_delay(1000);
                    ui_screen_done(2, secfalse);
                    hal_delay(1000);
                    ui_screen_done(1, secfalse);
                    hal_delay(1000);
                    usb_stop();
                    usb_deinit();
                    ui_fadeout();
                    return sectrue; // jump to firmware
                }
                break;
            case 55: // GetFeatures
                process_msg_GetFeatures(USB_IFACE_NUM, msg_size, buf, vhdr, hdr);
                break;
            default:
                process_msg_unknown(USB_IFACE_NUM, msg_size, buf);
                break;
        }
    }
}

secbool load_vendor_header_keys(const uint8_t * const data, vendor_header * const vhdr)
{
    return load_vendor_header(data, BOOTLOADER_KEY_M, BOOTLOADER_KEY_N, BOOTLOADER_KEYS, vhdr);
}

// protection against bootloader downgrade and vendor keys lock

#if PRODUCTION

static secbool check_vendor_keys_lock(const vendor_header * const vhdr) {
    uint8_t lock[FLASH_OTP_BLOCK_SIZE];
    ensure(flash_otp_read(FLASH_OTP_BLOCK_VENDOR_KEYS_LOCK, 0, lock, FLASH_OTP_BLOCK_SIZE), NULL);
    if (0 == memcmp(lock, "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF", FLASH_OTP_BLOCK_SIZE)) {
        return sectrue;
    }
    uint8_t hash[32];
    vendor_keys_hash(vhdr, hash);
    return sectrue * (0 == memcmp(lock, hash, 32));
}

static void check_bootloader_version(void)
{
    uint8_t bits[FLASH_OTP_BLOCK_SIZE];
    for (int i = 0; i < FLASH_OTP_BLOCK_SIZE * 8; i++) {
        if (i < VERSION_MONOTONIC) {
             bits[i / 8] &= ~(1 << (7 - (i % 8)));
        } else {
             bits[i / 8] |= (1 << (7 - (i % 8)));
        }
    }
    ensure(flash_otp_write(FLASH_OTP_BLOCK_BOOTLOADER_VERSION, 0, bits, FLASH_OTP_BLOCK_SIZE), NULL);

    uint8_t bits2[FLASH_OTP_BLOCK_SIZE];
    ensure(flash_otp_read(FLASH_OTP_BLOCK_BOOTLOADER_VERSION, 0, bits2, FLASH_OTP_BLOCK_SIZE), NULL);

    ensure(sectrue * (0 == memcmp(bits, bits2, FLASH_OTP_BLOCK_SIZE)), "Bootloader downgraded");
}

#endif

int main(void)
{
    // Initialize display first (required for LTDC + SDRAM framebuffer)
    display_init();

    touch_init();
    touch_power_on();

    // Give touch controller time to stabilize
    hal_delay(50);

    // Initialize touch calibration with defaults if not present
    if (touch_calib_is_valid() != sectrue) {
        touch_calib_write_defaults();
    }

    mpu_config_bootloader();

#if PRODUCTION
    check_bootloader_version();
#endif

main_start:

    display_clear();
    display_backlight(200);

    // Debug: Read and display STMPE811 chip ID with I2C status
    uint16_t chip_id = 0;
    uint8_t i2c_status_msb = 0, i2c_status_lsb = 0;
    i2c_status_t i2c_result = stmpe811_ReadID_Debug(&chip_id, &i2c_status_msb, &i2c_status_lsb);
    char dbg_buf[64];

    display_text(10, 25, "Touch Debug:", -1, FONT_NORMAL, COLOR_WHITE, COLOR_BLACK);

    // Show I2C status (0=OK, 1=TIMEOUT, 2=NACK, 3=ERROR)
    static const char* i2c_status_names[] = {"OK", "TIMEOUT", "NACK", "ERROR"};
    mini_snprintf(dbg_buf, sizeof(dbg_buf), "I2C: %s (MSB:%d LSB:%d)",
                  i2c_status_names[i2c_result < 4 ? i2c_result : 3],
                  i2c_status_msb, i2c_status_lsb);
    display_text(10, 45, dbg_buf, -1, FONT_NORMAL,
                 (i2c_result == I2C_STATUS_OK) ? COLOR_GREEN : COLOR_RED, COLOR_BLACK);

    mini_snprintf(dbg_buf, sizeof(dbg_buf), "Chip ID: 0x%04X", chip_id);
    display_text(10, 65, dbg_buf, -1, FONT_NORMAL,
                 (chip_id == 0x0811) ? COLOR_GREEN : COLOR_RED, COLOR_BLACK);

    if (chip_id == 0x0811) {
        display_text(150, 65, "(OK)", -1, FONT_NORMAL, COLOR_GREEN, COLOR_BLACK);
    } else if (chip_id == 0xFFFF) {
        display_text(150, 65, "(NO I2C)", -1, FONT_NORMAL, COLOR_RED, COLOR_BLACK);
    } else if (i2c_result == I2C_STATUS_NACK) {
        display_text(150, 65, "(NACK)", -1, FONT_NORMAL, COLOR_RED, COLOR_BLACK);
    } else if (i2c_result == I2C_STATUS_TIMEOUT) {
        display_text(150, 65, "(TIMEOUT)", -1, FONT_NORMAL, COLOR_RED, COLOR_BLACK);
    } else {
        display_text(150, 65, "(WRONG)", -1, FONT_NORMAL, COLOR_RED, COLOR_BLACK);
    }

    // Show calibration status
    if (touch_calib_is_valid() == sectrue) {
        display_text(10, 85, "Calib: LOADED from flash", -1, FONT_NORMAL, COLOR_GREEN, COLOR_BLACK);
    } else {
        display_text(10, 85, "Calib: DEFAULTS (no flash)", -1, FONT_NORMAL, COLOR_GRAY, COLOR_BLACK);
    }

    display_text(10, 110, "Touch screen to calibrate...", -1, FONT_NORMAL, COLOR_WHITE, COLOR_BLACK);
    display_text(10, 135, "Waiting 3 seconds...", -1, FONT_NORMAL, COLOR_GRAY, COLOR_BLACK);

    display_refresh();

    // Extended delay to detect touch (3 seconds) with live debug
    uint32_t touched = 0;
    uint32_t loop_count = 0;
    uint8_t last_tsc = 0xFF, last_fifo = 0xFF;
    uint32_t last_detected = 0xFF;
    (void)last_detected;  // May be used only in condition checks

    for (int i = 0; i < 3000; i++) {
        uint8_t tsc_ctrl = stmpe811_ReadTscCtrl();
        uint8_t fifo_size = stmpe811_ReadFifoSize();
        uint32_t is_detected = touch_is_detected();
        uint32_t read_val = touch_read();

        // Update display only when values change (reduce flicker)
        if (tsc_ctrl != last_tsc || fifo_size != last_fifo ||
            is_detected != last_detected || (loop_count % 500 == 0)) {

            // Clear debug area
            display_bar(10, 155, 230, 130, COLOR_BLACK);

            mini_snprintf(dbg_buf, sizeof(dbg_buf), "TSC_CTRL: 0x%02X", tsc_ctrl);
            display_text(10, 170, dbg_buf, -1, FONT_NORMAL,
                        (tsc_ctrl & 0x80) ? COLOR_GREEN : COLOR_GRAY, COLOR_BLACK);

            mini_snprintf(dbg_buf, sizeof(dbg_buf), "FIFO_SIZE: %d", fifo_size);
            display_text(10, 195, dbg_buf, -1, FONT_NORMAL,
                        (fifo_size > 0) ? COLOR_GREEN : COLOR_GRAY, COLOR_BLACK);

            mini_snprintf(dbg_buf, sizeof(dbg_buf), "is_detected: %lu", is_detected);
            display_text(10, 220, dbg_buf, -1, FONT_NORMAL,
                        is_detected ? COLOR_GREEN : COLOR_GRAY, COLOR_BLACK);

            mini_snprintf(dbg_buf, sizeof(dbg_buf), "touch_read: 0x%08lX", read_val);
            display_text(10, 245, dbg_buf, -1, FONT_NORMAL,
                        read_val ? COLOR_GREEN : COLOR_GRAY, COLOR_BLACK);

            mini_snprintf(dbg_buf, sizeof(dbg_buf), "Loop: %lu / 3000", loop_count);
            display_text(10, 275, dbg_buf, -1, FONT_NORMAL, COLOR_GRAY, COLOR_BLACK);

            display_refresh();

            last_tsc = tsc_ctrl;
            last_fifo = fifo_size;
            last_detected = is_detected;
        }

        touched = is_detected | read_val;
        if (touched) {
            // Show touch detected message
            display_bar(10, 295, 230, 30, COLOR_BLACK);
            display_text(10, 310, "TOUCH DETECTED!", -1, FONT_NORMAL, COLOR_GREEN, COLOR_BLACK);
            display_refresh();
            hal_delay(500);  // Show the message briefly
            break;
        }

        hal_delay(1);
        loop_count++;
    }

    // Clear the debug screen
    display_clear();

    vendor_header vhdr;
    image_header hdr;
    secbool firmware_present;

    // detect whether the devices contains a valid firmware

#if PRODUCTION
    firmware_present = load_vendor_header_keys((const uint8_t *)FIRMWARE_START, &vhdr);
    if (sectrue == firmware_present) {
        firmware_present = check_vendor_keys_lock(&vhdr);
    }
    if (sectrue == firmware_present) {
        firmware_present = load_image_header((const uint8_t *)(FIRMWARE_START + vhdr.hdrlen), FIRMWARE_IMAGE_MAGIC, FIRMWARE_IMAGE_MAXSIZE, vhdr.vsig_m, vhdr.vsig_n, vhdr.vpub, &hdr);
    }
    if (sectrue == firmware_present) {
        firmware_present = check_image_contents(&hdr, IMAGE_HEADER_SIZE + vhdr.hdrlen, FIRMWARE_SECTORS, FIRMWARE_SECTORS_COUNT);
    }
#else
    // Development mode: skip signature verification, just check magic
    firmware_present = secfalse;
    const uint32_t *fwmagic = (const uint32_t *)FIRMWARE_START;
    if (fwmagic[0] == 0x565A5254) {  // "TRZV" vendor header magic
        memcpy(&vhdr.hdrlen, (const uint8_t *)FIRMWARE_START + 4, 4);
        const uint32_t *imgmagic = (const uint32_t *)(FIRMWARE_START + vhdr.hdrlen);
        if (imgmagic[0] == 0x465A5254) {  // "TRZF" firmware magic
            memcpy(&hdr.hdrlen, (const uint8_t *)(FIRMWARE_START + vhdr.hdrlen) + 4, 4);
            firmware_present = sectrue;
        }
    }
#endif

    // start the bootloader if no or broken firmware found ...
    if (firmware_present != sectrue) {
        // show intro animation

        // no ui_fadeout(); - we already start from black screen
        ui_screen_first();
        ui_fadein();

        hal_delay(1000);

        ui_fadeout();
        ui_screen_second();
        ui_fadein();

        hal_delay(1000);

        ui_fadeout();
        ui_screen_third();
        ui_fadein();

        // erase storage
        ensure(flash_erase_sectors(STORAGE_SECTORS, STORAGE_SECTORS_COUNT, NULL), NULL);

        // and start the usb loop
        if (bootloader_usb_loop(NULL, NULL) != sectrue) {
            return 1;
        }
    } else
    // ... or if user touched the screen on start
    if (touched) {
        // First, offer touch calibration
        // no ui_fadeout(); - we already start from black screen
        ui_screen_calib_confirm();
        ui_fadein();

        int calib_response = ui_user_input(INPUT_CONFIRM | INPUT_CANCEL);
        ui_fadeout();

        if (INPUT_CONFIRM == calib_response) {
            // User wants to calibrate - run calibration
            if (touch_calib_run() == sectrue) {
                // Calibration successful - jump to firmware
                ui_fadeout();
                mpu_config_off();
                jump_to(FIRMWARE_START + vhdr.hdrlen + IMAGE_HEADER_SIZE);
            }
            // Calibration failed - restart
            goto main_start;
        }

        // show firmware info with connect buttons
        ui_screen_info(sectrue, &vhdr, &hdr);
        ui_fadein();

        for (;;) {
            int response = ui_user_input(INPUT_CONFIRM | INPUT_CANCEL | INPUT_INFO);
            ui_fadeout();

            // if cancel was pressed -> restart
            if (INPUT_CANCEL == response) {
                goto main_start;
            }

            // if confirm was pressed -> jump out
            if (INPUT_CONFIRM == response) {
                // show firmware info without connect buttons
                ui_screen_info(secfalse, &vhdr, &hdr);
                ui_fadein();
                break;
            }

            // if info icon was pressed -> show fingerprint
            if (INPUT_INFO == response) {
                // show fingerprint
                ui_screen_info_fingerprint(&hdr);
                ui_fadein();
                while (INPUT_LONG_CONFIRM != ui_user_input(INPUT_LONG_CONFIRM)) { }
                ui_fadeout();
                ui_screen_info(sectrue, &vhdr, &hdr);
                ui_fadein();
            }
        }

        // and start the usb loop
        if (bootloader_usb_loop(&vhdr, &hdr) != sectrue) {
            return 1;
        }
    }

#if PRODUCTION
    ensure(
        load_vendor_header_keys((const uint8_t *)FIRMWARE_START, &vhdr),
        "invalid vendor header");

    ensure(
        check_vendor_keys_lock(&vhdr),
        "unauthorized vendor keys");

    ensure(
        load_image_header((const uint8_t *)(FIRMWARE_START + vhdr.hdrlen), FIRMWARE_IMAGE_MAGIC, FIRMWARE_IMAGE_MAXSIZE, vhdr.vsig_m, vhdr.vsig_n, vhdr.vpub, &hdr),
        "invalid firmware header");

    ensure(
        check_image_contents(&hdr, IMAGE_HEADER_SIZE + vhdr.hdrlen, FIRMWARE_SECTORS, FIRMWARE_SECTORS_COUNT),
        "invalid firmware hash");

    // if all VTRUST flags are unset = ultimate trust => skip the procedure

    if ((vhdr.vtrust & VTRUST_ALL) != VTRUST_ALL) {

        // ui_fadeout();  // no fadeout - we start from black screen
        ui_screen_boot(&vhdr, &hdr);
        ui_fadein();

        int delay = (vhdr.vtrust & VTRUST_WAIT) ^ VTRUST_WAIT;
        if (delay > 1) {
            while (delay > 0) {
                ui_screen_boot_wait(delay);
                hal_delay(1000);
                delay--;
            }
        } else if (delay == 1) {
            hal_delay(1000);
        }

        if ((vhdr.vtrust & VTRUST_CLICK) == 0) {
            ui_screen_boot_click();
            touch_click();
        }

        ui_fadeout();
    }
#endif

    // mpu_config_firmware();
    // jump_to_unprivileged(FIRMWARE_START + vhdr.hdrlen + IMAGE_HEADER_SIZE);

    mpu_config_off();
    jump_to(FIRMWARE_START + vhdr.hdrlen + IMAGE_HEADER_SIZE);

    return 0;
}
