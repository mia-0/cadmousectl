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

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hidapi.h>

int cadmouse_send_command(hid_device *mouse, int opt, int val1, int val2)
{
    unsigned char cmd[8] = { 0x0c, opt, val1, val2, 0x00, 0x00, 0x00, 0x00 };
    return hid_send_feature_report(mouse, cmd, 8);
}

int cadmouse_set_smartscroll(hid_device *mouse, int state)
{
    int result;

    if (state == 1 || state == 3)
        result = cadmouse_send_command(mouse, 0x03, 0x00, 0x00);
    else
        result = cadmouse_send_command(mouse, 0x03, 0x00, 0x01);

    if (result < 0)
        return result;

    if (state == 3)
        result = cadmouse_send_command(mouse, 0x04, 0xff, 0x00);
    else
        result = cadmouse_send_command(mouse, 0x04, state ? 0x00 : 0xff, 0x00);

    if (result < 0)
        return result;

    result = cadmouse_send_command(mouse, 0x05, 0x00, state ? 0x01 : 0x00);

    return result;
}

enum cadmouse_pollrate {
    POLL_125 = 0x08,
    POLL_250 = 0x04,
    POLL_500 = 0x02,
    POLL_1000 = 0x01
};

int cadmouse_set_pollrate(hid_device *mouse, enum cadmouse_pollrate rate)
{
    return cadmouse_send_command(mouse, 0x06, 0x00, rate);
}

int cadmouse_set_liftoff_detection(hid_device *mouse, int state)
{
    return cadmouse_send_command(mouse, 0x07, 0x00, state ? 0x00 : 0x1f);
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
    { NULL, 0 }
};

Button SWButtons[] = {
    { "left", 0x0a },
    { "right", 0x0b },
    { "middle", 0x0c },
    { "backward", 0x0d },
    { "forward", 0x0e },
    { "rm", 0x2e },
    { "extra", 0x2f },
    { NULL, 0 }
};

Button *get_button(const char *name, Button *type)
{
    for (; type->name != NULL; type++) {
        if (strcasecmp(type->name, name) == 0)
            break;
    }

    return type->name != NULL ? type : NULL;
}

int cadmouse_set_hwbutton(hid_device *mouse, Button *hw, Button *sw)
{
    return cadmouse_send_command(mouse, hw->id, 0x00, sw->id);
}

int cadmouse_set_speed(hid_device *mouse, int speed)
{
    return cadmouse_send_command(mouse, 0x01, 0x00, speed);
}

#define COMMAND(cmd, ...)                                          \
    do {                                                           \
        res = cmd(mouse, __VA_ARGS__);                             \
        if (res == -1) {                                           \
            fwprintf(stderr, L"%s: %s\n", #cmd, hid_error(mouse)); \
            goto error;                                            \
        }                                                          \
    } while (0)

int main(int argc, char **argv)
{
    int opt, res;

    hid_device *mouse = hid_open(0x256f, 0xc650, NULL);

    if (mouse == NULL) {
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

    if (mouse != NULL) {
        hid_close(mouse);
        hid_exit();
    }

    return 0;
}

