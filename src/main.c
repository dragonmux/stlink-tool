/*
 * Copyright (c) 2018 Jean THOMAS.
 * Copyright (c) 2024 1BitSquared <info@1bitsquared.com>
 * Modified by Rachel Mant <git@dragonmux.network>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software
 * is furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_MEAN_AND_LEAN
#include <windows.h>
#define mssleep(ms) Sleep(ms)
#else
#include <unistd.h>
#define mssleep(ms) usleep(ms * 1000U)
#endif
#include <libusb.h>
#include <getopt.h>
#include <string.h>

#include "stlink.h"

#define VENDOR_ID_STLINK           0x0483U
#define PRODUCT_ID_STLINK_MASK     0xffe0U
#define PRODUCT_ID_STLINK_GROUP    0x3740U
#define PRODUCT_ID_STLINKV2        0x3748U
#define PRODUCT_ID_STLINKV21       0x374bU
#define PRODUCT_ID_STLINKV21_MSD   0x3752U
#define PRODUCT_ID_STLINKV3_NO_MSD 0x3754U
#define PRODUCT_ID_STLINKV3_BL     0x374dU
#define PRODUCT_ID_STLINKV3        0x374fU
#define PRODUCT_ID_STLINKV3E       0x374eU

#define OPENMOKO_VID 0x1d50
#define BMP_APPL_PID 0x6018
#define BMP_DFU_IF   4

void print_help(char *argv[])
{
	printf("Usage: %s [options] [firmware.bin]\n", argv[0]);
	printf("Options:\n");
	printf("\t-p\tProbe the ST-Link adapter\n");
	printf("\t-h\tShow help\n\n");
	printf("\tApplication is started when called without argument or after firmware load\n\n");
}

int main(int argc, char **argv)
{
	int opt = -1;
	bool probe = false;

	while ((opt = getopt(argc, argv, "hp")) != -1) {
		switch (opt) {
		case 'p': /* Probe mode */
			probe = true;
			break;
		case 'h': /* Help */
			print_help(argv);
			return EXIT_SUCCESS;
			break;
		default:
			print_help(argv);
			return EXIT_FAILURE;
			break;
		}
	}

	const bool do_load = optind < argc;

	stlink_info_s info;
	const int res = libusb_init(&info.stinfo_usb_ctx);
	if (res != LIBUSB_SUCCESS) {
		fprintf(stderr, "Failed to initialise libusb: %d (%s)\n", res, libusb_strerror(res));
		return 2;
	}
rescan:
	info.stinfo_dev_handle = NULL;
	libusb_device **devs;
	ssize_t n_devs = libusb_get_device_list(info.stinfo_usb_ctx, &devs);
	if (n_devs < 0)
		goto exit_libusb;

	for (size_t i = 0U; devs[i]; i++) {
		libusb_device *dev = devs[i];
		struct libusb_device_descriptor desc;
		int res = libusb_get_device_descriptor(dev, &desc);
		if (res < 0)
			continue;
		if ((desc.idVendor == OPENMOKO_VID) && (desc.idProduct == BMP_APPL_PID)) {
			fprintf(stderr, "Trying to switch BMP/Application to bootloader\n");
			res = libusb_open(dev, &info.stinfo_dev_handle);
			if (res < 0) {
				fprintf(stderr, "Can not open BMP/Application!\n");
				continue;
			}
			libusb_claim_interface(info.stinfo_dev_handle, BMP_DFU_IF);
			res = libusb_control_transfer(info.stinfo_dev_handle,
				/* bmRequestType */ LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
				/* bRequest      */ 0, /*DFU_DETACH,*/
				/* wValue        */ 1000,
				/* wIndex        */ BMP_DFU_IF,
				/* Data          */ NULL,
				/* wLength       */ 0, 5000);
			libusb_release_interface(info.stinfo_dev_handle, 0);
			if (res < 0) {
				fprintf(stderr, "BMP Switch failed\n");
				continue;
			}
			libusb_free_device_list(devs, n_devs);
			mssleep(2000);
			goto rescan;
			break;
		}
		if (desc.idVendor != VENDOR_ID_STLINK || (desc.idProduct & PRODUCT_ID_STLINK_MASK) != PRODUCT_ID_STLINK_GROUP)
			continue;
		switch (desc.idProduct) {
		case PRODUCT_ID_STLINKV2:
			res = libusb_open(dev, &info.stinfo_dev_handle);
			if (res < 0) {
				fprintf(stderr, "Can not open ST-Link v2/Bootloader!\n");
				continue;
			}
			info.stinfo_ep_in = 1 | LIBUSB_ENDPOINT_IN;
			info.stinfo_ep_out = 2 | LIBUSB_ENDPOINT_OUT;
			info.stinfo_bl_type = STLINK_BL_V2;
			fprintf(stderr, "ST-Link v2/v2.1 Bootloader found\n");
			break;
		case PRODUCT_ID_STLINKV3_BL:
			res = libusb_open(dev, &info.stinfo_dev_handle);
			if (res < 0) {
				fprintf(stderr, "Can not open ST-Link v3 Bootloader!\n");
				continue;
			}
			info.stinfo_ep_in = 1 | LIBUSB_ENDPOINT_IN;
			info.stinfo_ep_out = 1 | LIBUSB_ENDPOINT_OUT;
			info.stinfo_bl_type = STLINK_BL_V3;
			fprintf(stderr, "ST-Link v3 Bootloader found\n");
			break;
		case PRODUCT_ID_STLINKV21:
		case PRODUCT_ID_STLINKV21_MSD:
		case PRODUCT_ID_STLINKV3:
		case PRODUCT_ID_STLINKV3_NO_MSD:
		case PRODUCT_ID_STLINKV3E:
			fprintf(stderr, "Trying to switch ST-Link/Application to bootloader\n");
			res = libusb_open(dev, &info.stinfo_dev_handle);
			if (res < 0) {
				fprintf(stderr, "Can not open ST-Link/Application!\n");
				continue;
			}
			if (libusb_claim_interface(info.stinfo_dev_handle, 0)) {
				fprintf(stderr,
					"Unable to claim USB interface. Please close all programs that "
					"may communicate with an ST-Link dongle.\n");
				continue;
			}
			const uint16_t mode = stlink_dfu_mode(info.stinfo_dev_handle, false);
			if (mode != 0x8000U) {
				libusb_release_interface(info.stinfo_dev_handle, 0);
				return 0;
			}
			stlink_dfu_mode(info.stinfo_dev_handle, true);
			libusb_release_interface(info.stinfo_dev_handle, 0);
			libusb_free_device_list(devs, n_devs);
			mssleep(2000);
			goto rescan;
			break;
		default:
			fprintf(stderr, "Unknown STM PID %x, please report\n", desc.idProduct);
		}
		if (info.stinfo_dev_handle)
			break;
	}
	libusb_free_device_list(devs, n_devs);
	if (!info.stinfo_dev_handle) {
		fprintf(stderr, "No ST-Link in DFU mode found. Replug ST-Link to flash!\n");
		return EXIT_FAILURE;
	}

	if (libusb_claim_interface(info.stinfo_dev_handle, 0)) {
		fprintf(stderr,
			"Unable to claim USB interface ! Please close all programs that "
			"may communicate with an ST-Link dongle.\n");
		return EXIT_FAILURE;
	}

	if (!stlink_read_info(&info)) {
		libusb_release_interface(info.stinfo_dev_handle, 0);
		return EXIT_FAILURE;
	}
	printf("Firmware version : V%dJ%dS%d\n", info.stlink_version, info.jtag_version, info.swim_version);
	printf("Loader version : %d\n", info.loader_version);
	printf("ST-Link ID : ");
	for (size_t i = 0; i < 12U; i += 4U)
		printf("%02X%02X%02X%02X", info.id[i + 3U], info.id[i + 2U], info.id[i + 1U], info.id[i + 0U]);
	printf("\n");
	printf("Firmware encryption key : ");
	for (size_t i = 0U; i < 16U; ++i)
		printf("%02X", info.firmware_key[i]);
	printf("\n");

	const uint16_t mode = stlink_current_mode(&info);
	if (mode == UINT16_MAX) {
		libusb_release_interface(info.stinfo_dev_handle, 0);
		return EXIT_FAILURE;
	}
	printf("Current mode : %u\n", mode);

	if (mode & ~3U) {
		printf("ST-Link dongle is not in the correct mode. Please unplug and plug the dongle again.\n");
		libusb_release_interface(info.stinfo_dev_handle, 0);
		return EXIT_SUCCESS;
	}

	if (!probe) {
		if (do_load)
			stlink_flash(&info, argv[optind]);
		stlink_exit_dfu(&info);
	}
	libusb_release_interface(info.stinfo_dev_handle, 0);
exit_libusb:
	libusb_exit(info.stinfo_usb_ctx);

	return EXIT_SUCCESS;
}
