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

#include <fcntl.h>
#include <getopt.h>
#include <libudev.h>
#include <linux/hidraw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

int cadmouse_send_command(int fd, int opt, int val1, int val2)
{
    unsigned char cmd[8] = { 0x0c, opt, val1, val2, 0x00, 0x00, 0x00, 0x00 };
    int result = ioctl(fd, HIDIOCSFEATURE(8), cmd);

    return result < 0 ? result : 0;
}

int cadmouse_set_smartscroll(int fd, int state)
{
    int result;

    if (state == 1 || state == 3)
        result = cadmouse_send_command(fd, 0x03, 0x00, 0x00);
    else
        result = cadmouse_send_command(fd, 0x03, 0x00, 0x01);

    if (result < 0)
        return result;

    if (state == 3)
        result = cadmouse_send_command(fd, 0x04, 0xff, 0x00);
    else
        result = cadmouse_send_command(fd, 0x04, state ? 0x00 : 0xff, 0x00);

    if (result < 0)
        return result;

    result = cadmouse_send_command(fd, 0x05, 0x00, state ? 0x01 : 0x00);

    return result;
}

enum cadmouse_pollrate {
    POLL_125 = 0x08,
    POLL_250 = 0x04,
    POLL_500 = 0x02,
    POLL_1000 = 0x01
};

int cadmouse_set_pollrate(int fd, enum cadmouse_pollrate rate)
{
    return cadmouse_send_command(fd, 0x06, 0x00, rate);
}

int cadmouse_set_liftoff_detection(int fd, int state)
{
    return cadmouse_send_command(fd, 0x07, 0x00, state ? 0x00 : 0x1f);
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
    { "rm", 0x2e },
    { "extra", 0x2f },
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

int cadmouse_set_hwbutton(int fd, Button *hw, Button *sw)
{
    return cadmouse_send_command(fd, hw->id, 0x00, sw->id);
}

int cadmouse_set_speed(int fd, int speed)
{
    return cadmouse_send_command(fd, 0x01, 0x00, speed);
}

int find_device(const char *vendor, const char *product)
{
    int fd = -1;
    struct udev *udev;
    struct udev_enumerate *enumerate;
    struct udev_list_entry *devices, *dev_list_entry;
    struct udev_device *dev, *parent;

    udev = udev_new();
    if (udev == NULL) {
        fputs("Cannot create udev context\n", stderr);
        exit(1);
    }

    enumerate = udev_enumerate_new(udev);
    udev_enumerate_add_match_subsystem(enumerate, "hidraw");
    udev_enumerate_scan_devices(enumerate);

    devices = udev_enumerate_get_list_entry(enumerate);

    udev_list_entry_foreach(dev_list_entry, devices) {
        const char *path, *vid, *pid;

        path = udev_list_entry_get_name(dev_list_entry);
        dev = udev_device_new_from_syspath(udev, path);
        parent = udev_device_get_parent_with_subsystem_devtype(dev, "usb",
                                                               "usb_device");

        if (dev != NULL) {
            vid = udev_device_get_sysattr_value(parent, "idVendor");
            pid = udev_device_get_sysattr_value(parent, "idProduct");

            if (!strcmp(vid, vendor) && !strcmp(pid, product)) {
                path = udev_device_get_devnode(dev);
                fd = open(path, O_RDONLY|O_NONBLOCK);

                if (fd < 0)
                    perror("open");
            }

            udev_device_unref(parent);

            if (fd >= 0)
                break;
        }
    }

    udev_enumerate_unref(enumerate);
    udev_unref(udev);

    return fd;
}

#define COMMAND(cmd, ...)           \
    do {                            \
        res = cmd(fd, __VA_ARGS__); \
        if (res) {                  \
            perror(#cmd);           \
            goto error;             \
        }                           \
    } while (0)

int main(int argc, char **argv)
{
    int opt, res;

    int fd = find_device("256f", "c650");

    if (fd < 0) {
        fputs("Could not find/open a CadMouse\n", stderr);
        goto error;
    }

    extern char *optarg;
    extern int optind, opterr, optopt;
    int speedconflict = 0;
    while ((opt = getopt(argc, argv, "l:p:r:s:d:S:")) != -1) {
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
                    if (speedconflict != 0)
                        fputs("-s: -s cannot be used with -d or used more than once\n", stderr);
                    else
                    if (speed < 1 || speed > 164)
                        fputs("-s: Option value out of range\n", stderr);
                    else
                        speedconflict++;
                        COMMAND(cadmouse_set_speed, speed);
                }
                break;
             case 'd':
                {
                    long int dpi = strtol(optarg, NULL, 10);
                    long int speed = (dpi * 164)/8200;
                    if (speedconflict != 0)
                        fputs("-d: -d cannot be used with -s or used more than once\n", stderr);
                    if (dpi < 50 || dpi > 8200)
                        fputs("-d: Option value out of range\n", stderr);
                    else
                        speedconflict++;
                        COMMAND(cadmouse_set_speed, speed);
                }
                break;
            case 'S':
                {
                    long int smartscroll = strtol(optarg, NULL, 10);

                    if (smartscroll >= 0 && smartscroll < 4)
                        COMMAND(cadmouse_set_smartscroll, smartscroll);
                    else
                        fputs("-S: Option value out of range\n", stderr);
                }
                break;
        }
    }

error:

    if (fd >= 0)
        close(fd);

    return 0;
}

