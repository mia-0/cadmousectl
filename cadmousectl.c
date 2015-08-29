/* Copyright © 2015, Martin Herkt <lachs0r@srsfckn.biz>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or  without fee is hereby granted,  provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES OF
 * MERCHANTABILITY AND FITNESS.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL,  DIRECT,  INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING  FROM LOSS OF USE,  DATA OR PROFITS,  WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <getopt.h>
#include <libusb.h>

int cadmouse_send_command(libusb_device_handle *device,
                          int opt, int val1, int val2)
{
    unsigned char cmd[8] = { 0x0c, opt, val1, val2, 0x00, 0x00, 0x00, 0x00 };
    int result = libusb_control_transfer(device,
                                         LIBUSB_ENDPOINT_OUT |
                                         LIBUSB_REQUEST_TYPE_CLASS |
                                         LIBUSB_RECIPIENT_INTERFACE,
                                         0x09, 0x030c, 0x0000, cmd, 8, 0);

    return result < 0 ? result : 0;
}

int cadmouse_set_smartscroll(libusb_device_handle *device, int state)
{
    int result;

    result = cadmouse_send_command(device, 0x03, 0x00, state ? 0x00 : 0x01);

    if (result < 0)
        return result;

    result = cadmouse_send_command(device, 0x04, state ? 0x00 : 0xff, 0x00);

    if (result < 0)
        return result;

    result = cadmouse_send_command(device, 0x05, 0x00, state ? 0x01 : 0x00);

    return result;
}

enum cadmouse_pollrate {
    POLL_125 = 0x08,
    POLL_250 = 0x04,
    POLL_500 = 0x02,
    POLL_1000 = 0x01
};

int cadmouse_set_pollrate(libusb_device_handle *device,
                          enum cadmouse_pollrate rate)
{
    return cadmouse_send_command(device, 0x06, 0x00, rate);
}

int cadmouse_set_liftoff_detection(libusb_device_handle *device, int state)
{
    return cadmouse_send_command(device, 0x07, 0x00, state ? 0x00 : 0x1f);
}

typedef struct Button {
    char *name;
    char id;
} Button;

Button HWButtons[] = {
    { "left", 0x0a },
    { "right", 0x0b },
    { "middle", 0x0c },
    { "wheel", 0x0d },
    { "forward", 0x0e },
    { "backward", 0x0f },
    { "rm", 0x10 },
    { NULL, NULL }
};

Button SWButtons[] = {
    { "left", 0x0a },
    { "right", 0x0b },
    { "middle", 0x0c },
    { "backward", 0x0d },
    { "forward", 0x0e },
    { "rm", 0x2f },
    { NULL, NULL }
};

Button *get_button(const char *name, Button *type)
{
    for (; type->name != NULL; type++) {
        if (strcasecmp(type->name, name) == 0)
            break;
    }

    return type->name != NULL ? type : NULL;
}

int cadmouse_set_hwbutton(libusb_device_handle *device,
                          Button *hw, Button *sw)
{
    return cadmouse_send_command(device, hw->id, 0x00, sw->id);
}

int cadmouse_set_speed(libusb_device_handle *device, int speed)
{
    return cadmouse_send_command(device, 0x01, 0x00, speed + 0x40);
}

libusb_device_handle *find_device(int vendor, int product, int *error)
{
    libusb_device **list;
    libusb_device *found = NULL;
    libusb_device_handle *handle = NULL;
    int err = 0;
    ssize_t i = 0;

    ssize_t cnt = libusb_get_device_list (NULL, &list);

    if (cnt < 0)
        goto out;

    for (i = 0; i < cnt; i++) {
        libusb_device *device = list[i];
        struct libusb_device_descriptor desc;

        if (libusb_get_device_descriptor(device, &desc))
            continue;

        if (desc.idVendor == vendor && desc.idProduct == product) {
            found = device;
            break;
        }
    }

    if (found) {
        err = libusb_open (found, &handle);

        if (err)
            goto out_free;
    }

out_free:
    libusb_free_device_list(list, 1);
out:
    if (error != NULL && err != 0)
        *error = err;
    return handle;
}

#define ERROR(label, ...)                       \
    do {                                        \
        fprintf(stderr, "Error: " __VA_ARGS__); \
        status = 1;                             \
        goto label;                             \
    } while (0)

#define COMMAND(cmd, ...)                               \
    do {                                                \
        result = cmd(device, __VA_ARGS__);              \
        if (result) {                                   \
            ERROR(error_4, "operation failed: %s\n",    \
                  libusb_error_name(result));           \
        }                                               \
    } while (0)

int main(int argc, char **argv)
{
    int result, opt, status = 0;

    result = libusb_init (NULL);
    if (result)
        ERROR(error_0, "libusb initialisation failed: %s\n",
              libusb_error_name (result));

    result = 0;
    libusb_device_handle *device = find_device(0x256f, 0xc650, &result);

    if (!device) {
        if (result) {
            ERROR(error_1, "couldn't open device: %s\n",
                  libusb_error_name (result));
        } else {
            ERROR(error_1, "no suitable device found\n");
        }
    }

    int reattach_driver = 0;

    result = libusb_kernel_driver_active(device, 0);
    switch (result) {
        case 0:
        case LIBUSB_ERROR_NOT_SUPPORTED:
            break;
        case 1:
            reattach_driver = 1;
            result = libusb_detach_kernel_driver(device, 0);

            if (result) {
                ERROR(error_2, "couldn't detach kernel driver: %s\n",
                      libusb_error_name (result));
            }

            break;
        default:
            ERROR(error_2, "coudn't detect kernel driver presence: %s\n",
                  libusb_error_name (result));
    }

    result = libusb_claim_interface(device, 0);
    if (result) {
        ERROR(error_3, "couldn't claim interface: %s\n",
              libusb_error_name (result));
    }

    extern char *optarg;
    extern int optind, opterr, optopt;
    while ((opt = getopt(argc, argv, "l:p:r:s:S:")) != -1) {
        switch(opt) {
            case 'l':
                {
                    long int liftdetect = strtol(optarg, NULL, 10);

                    if (liftdetect == 0)
                        COMMAND(cadmouse_set_liftoff_detection, 0);
                    else
                        COMMAND(cadmouse_set_liftoff_detection, 1);
                }
                break;
            case 'p':
                {
                    long int rate = strtol(optarg, NULL, 10);

                    if (rate == 125)
                        COMMAND(cadmouse_set_pollrate, POLL_125);
                    else if (rate == 250)
                        COMMAND(cadmouse_set_pollrate, POLL_250);
                    else if (rate == 500)
                        COMMAND(cadmouse_set_pollrate, POLL_500);
                    else if (rate == 1000)
                        COMMAND(cadmouse_set_pollrate, POLL_1000);
                    else
                        fputs("-p: Unsupported polling rate\n", stderr);
                }
                break;
            case 'r':
                {
                    Button *hw = NULL, *sw = NULL;
                    char *sep = strchr(optarg, ':');

                    if (sep != NULL) {
                        *sep = '\0';
                        sep++;
                        hw = get_button(optarg, HWButtons);
                        sw = get_button(sep, SWButtons);
                    }

                    if (hw == NULL || sw == NULL)
                        fputs("-r: invalid button mapping\n", stderr);
                    else
                        COMMAND(cadmouse_set_hwbutton, hw, sw);
                }
                break;
            case 's':
                {
                    long int speed = strtol(optarg, NULL, 10);

                    if (speed < 0 || speed > 100)
                        fputs("-s: Option value out of range\n", stderr);
                    else
                        COMMAND(cadmouse_set_speed, speed);
                }
                break;
            case 'S':
                {
                    long int smartscroll = strtol(optarg, NULL, 10);

                    if (smartscroll == 0)
                        COMMAND(cadmouse_set_smartscroll, 0);
                    else
                        COMMAND(cadmouse_set_smartscroll, 1);
                }
                break;
        }
    }

error_4:
    result = libusb_release_interface(device, 0);
    if (result) {
        ERROR(error_3, "couldn't release interface: %s\n",
              libusb_error_name (result));
    }

error_3:
    if (reattach_driver) {
        result = libusb_attach_kernel_driver(device, 0);
        if (result) {
            ERROR(error_2, "couldn't reattach kernel driver: %s\n",
                  libusb_error_name (result));
        }
    }

error_2:
    libusb_close (device);
error_1:
    libusb_exit (NULL);
error_0:
    return status;
}

