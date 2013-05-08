/* This is a utility program for listing SCSI devices and hosts (HBAs)
 * in the Linux operating system. It is applicable to kernel versions
 * 2.6.1 and greater.
 *
 *  Copyright (C) 2003-2013 D. Gilbert
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 */

#define _XOPEN_SOURCE 600
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <libgen.h>
#include <linux/major.h>
#include <linux/limits.h>
#include <time.h>
#define __STDC_FORMAT_MACROS 1
#include <inttypes.h>

static const char * version_str = "0.27  2013/05/08 [svn: r111]";

#define FT_OTHER 0
#define FT_BLOCK 1
#define FT_CHAR 2

#define TRANSPORT_UNKNOWN 0
#define TRANSPORT_SPI 1
#define TRANSPORT_FC 2
#define TRANSPORT_SAS 3
#define TRANSPORT_SAS_CLASS 4
#define TRANSPORT_ISCSI 5
#define TRANSPORT_SBP 6
#define TRANSPORT_USB 7
#define TRANSPORT_ATA 8         /* probably PATA, could be SATA */
#define TRANSPORT_SATA 9        /* most likely SATA */
#define TRANSPORT_FCOE 10

#ifdef PATH_MAX
#define LMAX_PATH PATH_MAX
#else
#define LMAX_PATH 2048
#endif

#ifdef NAME_MAX
#define LMAX_NAME (NAME_MAX + 1)
#else
#define LMAX_NAME 256
#endif

#define LMAX_DEVPATH (LMAX_NAME + 128)

static int transport_id = TRANSPORT_UNKNOWN;


static const char * sysfsroot = "/sys";
static const char * bus_scsi_devs = "/bus/scsi/devices";
static const char * class_scsi_dev = "/class/scsi_device/";
static const char * scsi_host = "/class/scsi_host/";
static const char * spi_host = "/class/spi_host/";
static const char * spi_transport = "/class/spi_transport/";
static const char * sas_host = "/class/sas_host/";
static const char * sas_phy = "/class/sas_phy/";
static const char * sas_port = "/class/sas_port/";
static const char * sas_device = "/class/sas_device/";
static const char * sas_end_device = "/class/sas_end_device/";
static const char * fc_host = "/class/fc_host/";
static const char * fc_transport = "/class/fc_transport/";
static const char * fc_remote_ports = "/class/fc_remote_ports/";
static const char * iscsi_host = "/class/iscsi_host/";
static const char * iscsi_session = "/class/iscsi_session/";
static const char * dev_dir = "/dev";
static const char * dev_disk_byid_dir = "/dev/disk/by-id";


struct addr_hctl {
        int h;
        int c;
        int t;
        uint64_t l;                     /* Linux word flipped */
        unsigned char lun_arr[8];       /* T10, SAM-5 order */
};

struct addr_hctl filter;
static int filter_active = 0;

struct lsscsi_opt_coll {
        int long_opt;           /* --long */
        int classic;
        int generic;
        int dev_maj_min;        /* --device */
        int kname;
        int lunhex;
        int protection;         /* data integrity */
        int protmode;           /* data integrity */
        int scsi_id;            /* udev derived from /dev/disk/by-id/scsi* */
        int size;
        int transport;
        int verbose;
        int wwn;
};


static const char * scsi_device_types[] =
{
        "Direct-Access",
        "Sequential-Access",
        "Printer",
        "Processor",
        "Write-once",
        "CD-ROM",
        "Scanner",
        "Optical memory",
        "Medium Changer",
        "Communications",
        "Unknown (0xa)",
        "Unknown (0xb)",
        "Storage array",
        "Enclosure",
        "Simplified direct-access",
        "Optical card read/writer",
        "Bridge controller",
        "Object based storage",
        "Automation Drive interface",
        "Reserved (0x13)", "Reserved (0x14)",
        "Reserved (0x15)", "Reserved (0x16)", "Reserved (0x17)",
        "Reserved (0x18)", "Reserved (0x19)", "Reserved (0x1a)",
        "Reserved (0x1b)", "Reserved (0x1c)", "Reserved (0x1e)",
        "Well known LU",
        "No device",
};

static const char * scsi_short_device_types[] =
{
        "disk   ", "tape   ", "printer", "process", "worm   ", "cd/dvd ",
        "scanner", "optical", "mediumx", "comms  ", "(0xa)  ", "(0xb)  ",
        "storage", "enclosu", "sim dsk", "opti rd", "bridge ", "osd    ",
        "adi    ", "(0x13) ", "(0x14) ", "(0x15) ", "(0x16) ", "(0x17) ",
        "(0x18) ", "(0x19) ", "(0x1a) ", "(0x1b) ", "(0x1c) ", "(0x1e) ",
        "wlun   ", "no dev ",
};

/* '--name' ('-n') option removed in version 0.11 and can now be reused */
static struct option long_options[] = {
        {"classic", 0, 0, 'c'},
        {"device", 0, 0, 'd'},
        {"generic", 0, 0, 'g'},
        {"help", 0, 0, 'h'},
        {"hosts", 0, 0, 'H'},
        {"kname", 0, 0, 'k'},
        {"long", 0, 0, 'l'},
        {"list", 0, 0, 'L'},
        {"lunhex", 0, 0, 'x'},
        {"protection", 0, 0, 'p'},
        {"protmode", 0, 0, 'P'},
        {"scsi_id", 0, 0, 'i'},
        {"scsi-id", 0, 0, 'i'}, /* convenience, not documented */
        {"size", 0, 0, 's'},
        {"sysfsroot", 1, 0, 'y'},
        {"transport", 0, 0, 't'},
        {"verbose", 0, 0, 'v'},
        {"version", 0, 0, 'V'},
        {"wwn", 0, 0, 'w'},
        {0, 0, 0, 0}
};


/* Device node list: contains the information needed to match a node with a
   sysfs class device. */
#define DEV_NODE_LIST_ENTRIES 16
enum dev_type { BLK_DEV, CHR_DEV};

struct dev_node_entry {
       unsigned int maj, min;
       enum dev_type type;
       time_t mtime;
       char name[LMAX_DEVPATH];
};

struct dev_node_list {
       struct dev_node_list *next;
       unsigned int count;
       struct dev_node_entry nodes[DEV_NODE_LIST_ENTRIES];
};
static struct dev_node_list* dev_node_listhead = NULL;

struct disk_wwn_node_entry {
       char wwn[32];
       char disk_bname[12];
};

#define DISK_WWN_NODE_LIST_ENTRIES 16
struct disk_wwn_node_list {
       struct disk_wwn_node_list *next;
       unsigned int count;
       struct disk_wwn_node_entry nodes[DISK_WWN_NODE_LIST_ENTRIES];
};
static struct disk_wwn_node_list * disk_wwn_node_listhead = NULL;

struct item_t {
        char name[LMAX_NAME];
        int ft;
        int d_type;
};

static struct item_t non_sg;
static struct item_t aa_sg;
static struct item_t aa_first;
static struct item_t enclosure_device;

static char sas_low_phy[LMAX_NAME];
static char sas_hold_end_device[LMAX_NAME];

static const char * iscsi_dir_name;
static const struct addr_hctl * iscsi_target_hct;
static int iscsi_tsession_num;

static char errpath[LMAX_PATH];


static const char * usage_message =
"Usage: lsscsi   [--classic] [--device] [--generic] [--help] [--hosts]\n"
            "\t\t[--kname] [--list] [--lunhex] [--long] [--protection]\n"
            "\t\t[--scsi_id] [--size] [--sysfsroot=PATH] [--transport]\n"
            "\t\t[--verbose] [--version] [--wwn] [<h:c:t:l>]\n"
"  where:\n"
"    --classic|-c      alternate output similar to 'cat /proc/scsi/scsi'\n"
"    --device|-d       show device node's major + minor numbers\n"
"    --generic|-g      show scsi generic device name\n"
"    --help|-h         this usage information\n"
"    --hosts|-H        lists scsi hosts rather than scsi devices\n"
"    --kname|-k        show kernel name instead of device node name\n"
"    --list|-L         additional information output one\n"
"                      attribute=value per line\n"
"    --long|-l         additional information output\n"
"    --lunhex|-x       show LUN part of tuple as hex number in T10 "
"format;\n"
"                      use twice to get full 16 digit hexadecimal LUN\n"
"    --protection|-p   show target and initiator protection information\n"
"    --protmode|-P     show negotiated protection information mode\n"
"    --scsi_id|-i      show udev derived /dev/disk/by-id/scsi* entry\n"
"    --size|-s         show disk size\n"
"    --sysfsroot=PATH|-y PATH    set sysfs mount point to PATH (def: /sys)\n"
"    --transport|-t    transport information for target or, if '--hosts'\n"
"                      given, for initiator\n"
"    --verbose|-v      output path names where data is found\n"
"    --version|-V      output version string and exit\n"
"    --wwn|-w          output WWN for disks (from /dev/disk/by-id/wwn*)\n"
"    <h:c:t:l>         filter output list (def: '*:*:*:*' (all))\n\n"
"List SCSI devices or hosts, optionally with additional information\n";

static void
usage(void)
{
        fprintf(stderr, "%s", usage_message);
}

/* Copies (dest_maxlen - 1) or less chars from src to dest. Less chars are
 * copied if '\0' char found in src. As long as dest_maxlen > 0 then dest
 * will be '\0' terminated on exit. If dest_maxlen < 1 then does nothing. */
static void
my_strcopy(char *dest, const char *src, int dest_maxlen)
{
        const char * lp;

        if (dest_maxlen < 1)
                return;
        lp = (const char *)memchr(src, 0, dest_maxlen);
        if (NULL == lp) {
                memcpy(dest, src, dest_maxlen - 1);
                dest[dest_maxlen - 1] = '\0';
        } else
                memcpy(dest, src, (lp - src) + 1);
}

/* Returns remainder (*np % base) and replaces *np with (*np / base).
 * base needs to be > 0 */
static unsigned int
do_div_rem(uint64_t * np, unsigned int base)
{
        unsigned int res;

        res = *np % base;
        *np /= base;
        return res;
}

enum string_size_units {
        STRING_UNITS_10,        /* use powers of 10^3 (standard SI) */
        STRING_UNITS_2,         /* use binary powers of 2^10 */
};

/**
 * string_get_size - get the size in the specified units
 * @size:       The size to be converted
 * @units:      units to use (powers of 1000 or 1024)
 * @buf:        buffer to format to
 * @len:        length of buffer
 *
 * This function returns a string formatted to 3 significant figures
 * giving the size in the required units.  Returns 0 on success or
 * error on failure.  @buf is always zero terminated.
 *
 */
static int
string_get_size(uint64_t size, const enum string_size_units units, char *buf,
                int len)
{
        const char *units_10[] = { "B", "kB", "MB", "GB", "TB", "PB",
                                   "EB", "ZB", "YB", NULL};
        const char *units_2[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB",
                                 "EiB", "ZiB", "YiB", NULL };
        const char **units_str[] = {
                [STRING_UNITS_10] =  units_10,
                [STRING_UNITS_2] = units_2,
        };
        const unsigned int divisor[] = {
                [STRING_UNITS_10] = 1000,
                [STRING_UNITS_2] = 1024,
        };
        int i, j;
        unsigned int res;
        uint64_t sf_cap;
        uint64_t remainder = 0;
        char tmp[8];

        tmp[0] = '\0';
        i = 0;
        if (size >= divisor[units]) {
                while ((size >= divisor[units]) && units_str[units][i]) {
                        remainder = do_div_rem(&size, divisor[units]);
                        i++;
                }

                sf_cap = size;
                for (j = 0; sf_cap*10 < 1000; j++)
                        sf_cap *= 10;

                if (j) {
                        remainder *= 1000;
                        do_div_rem(&remainder, divisor[units]);
                        res = remainder;
                        snprintf(tmp, sizeof(tmp), ".%03u", res);
                        tmp[j+1] = '\0';
                }
        }

        res = size;
        snprintf(buf, len, "%u%s%s", res, tmp, units_str[units][i]);

        return 0;
}


/* Compare <host:controller:target:lun> tuples (aka <h:c:t:l> or hctl) */
static int
cmp_hctl(const struct addr_hctl * le, const struct addr_hctl * ri)
{
        if (le->h == ri->h) {
                if (le->c == ri->c) {
                        if (le->t == ri->t)
                                return ((le->l == ri->l) ? 0 :
                                        ((le->l < ri->l) ? -1 : 1));
                        else
                                return (le->t < ri->t) ? -1 : 1;
                } else
                        return (le->c < ri->c) ? -1 : 1;
        } else
                return (le->h < ri->h) ? -1 : 1;
}

static void
invalidate_hctl(struct addr_hctl * p)
{
        int k;

        if (p) {
                p->h = -1;
                p->c = -1;
                p->t = -1;
                p->l = (uint64_t)~0;
                for (k = 0; k < 8; ++k)
                        p->lun_arr[k] = 0xff;
        }
}

/* Return 1 for directory entry that is link or directory (other than
 * a directory name starting with dot). Else return 0.
 */
static int
first_scandir_select(const struct dirent * s)
{
        if (FT_OTHER != aa_first.ft)
                return 0;
        if ((DT_LNK != s->d_type) &&
            ((DT_DIR != s->d_type) || ('.' == s->d_name[0])))
                return 0;
        my_strcopy(aa_first.name, s->d_name, LMAX_NAME);
        aa_first.ft = FT_CHAR;  /* dummy */
        aa_first.d_type =  s->d_type;
        return 1;
}

static int
sub_scandir_select(const struct dirent * s)
{
        if (s->d_type == DT_LNK)
                return 1;

        if (s->d_type == DT_DIR && s->d_name[0] != '.')
                return 1;

        return 0;
}

static int
sd_scandir_select(const struct dirent * s)
{
        if (s->d_type != DT_LNK && s->d_type != DT_DIR)
                return 0;

        if (s->d_name[0] == '.')
                return 0;

        if (strstr(s->d_name, "scsi_disk"))
                return 1;

        return 0;
}

/* Return 1 for directory entry that is link or directory (other than a
 * directory name starting with dot) that contains "block". Else return 0.
 */
static int
block_scandir_select(const struct dirent * s)
{
        if (s->d_type != DT_LNK && s->d_type != DT_DIR)
                return 0;

        if (s->d_name[0] == '.')
                return 0;

        if (strstr(s->d_name, "block"))
                return 1;

        return 0;
}

typedef int (* dirent_select_fn) (const struct dirent *);

static int
sub_scan(char * dir_name, const char * sub_str, dirent_select_fn fn)
{
        struct dirent ** namelist;
        int num, i, len;

        num = scandir(dir_name, &namelist, fn, NULL);
        if (num <= 0)
                return 0;
        len = strlen(dir_name);
        if (len >= LMAX_PATH)
                return 0;
        snprintf(dir_name + len, LMAX_PATH - len, "/%s", namelist[0]->d_name);

        for (i = 0; i < num; i++)
                free(namelist[i]);
        free(namelist);

        if (num && strstr(dir_name, sub_str) == 0) {
                num = scandir(dir_name, &namelist, sub_scandir_select, NULL);
                if (num <= 0)
                        return 0;
                len = strlen(dir_name);
                if (len >= LMAX_PATH)
                        return 0;
                snprintf(dir_name + len, LMAX_PATH - len, "/%s",
                         namelist[0]->d_name);

                for (i = 0; i < num; i++)
                        free(namelist[i]);
                free(namelist);
        }
        return 1;
}

/* Scan for block:sdN or block/sdN directory in
 * /sys/bus/scsi/devices/h:c:i:l
 */
static int
block_scan(char * dir_name)
{
        return sub_scan(dir_name, "block:", block_scandir_select);
}

/* Scan for scsi_disk:h:c:i:l or scsi_disk/h:c:i:l directory in
 * /sys/bus/scsi/devices/h:c:i:l
 */
static int
sd_scan(char * dir_name)
{
        return sub_scan(dir_name, "scsi_disk:", sd_scandir_select);
}

static int
enclosure_device_scandir_select(const struct dirent * s)
{
        if ((DT_LNK != s->d_type) &&
            ((DT_DIR != s->d_type) || ('.' == s->d_name[0])))
                return 0;
        if (strstr(s->d_name, "enclosure_device")){
                my_strcopy(enclosure_device.name, s->d_name, LMAX_NAME);
                enclosure_device.ft = FT_CHAR;  /* dummy */
                enclosure_device.d_type =  s->d_type;
                return 1;
        }
        return 0;
}

/* Return 1 for directory entry that is link or directory (other than a
 * directory name starting with dot) that contains "enclosure_device".
 * Else return 0.
 */
static int
enclosure_device_scan(const char * dir_name, const struct lsscsi_opt_coll * op)
{
        struct dirent ** namelist;
        int num, k;

        num = scandir(dir_name, &namelist, enclosure_device_scandir_select,
                      NULL);
        if (num < 0) {
                if (op->verbose > 0) {
                        snprintf(errpath, LMAX_PATH, "scandir: %s", dir_name);
                        perror(errpath);
                }
                return -1;
        }
        for (k = 0; k < num; ++k)
                free(namelist[k]);
        free(namelist);
        return num;
}

/* scan for directory entry that is either a symlink or a directory */
static int
scan_for_first(const char * dir_name, const struct lsscsi_opt_coll * op)
{
        struct dirent ** namelist;
        int num, k;

        aa_first.ft = FT_OTHER;
        num = scandir(dir_name, &namelist, first_scandir_select, NULL);
        if (num < 0) {
                if (op->verbose > 0) {
                        snprintf(errpath, LMAX_PATH, "scandir: %s", dir_name);
                        perror(errpath);
                }
                return -1;
        }
        for (k = 0; k < num; ++k)
                free(namelist[k]);
        free(namelist);
        return num;
}

static int
non_sg_scandir_select(const struct dirent * s)
{
        int len;

        if (FT_OTHER != non_sg.ft)
                return 0;
        if ((DT_LNK != s->d_type) &&
            ((DT_DIR != s->d_type) || ('.' == s->d_name[0])))
                return 0;
        if (0 == strncmp("scsi_changer", s->d_name, 12)) {
                my_strcopy(non_sg.name, s->d_name, LMAX_NAME);
                non_sg.ft = FT_CHAR;
                non_sg.d_type =  s->d_type;
                return 1;
        } else if (0 == strncmp("block", s->d_name, 5)) {
                my_strcopy(non_sg.name, s->d_name, LMAX_NAME);
                non_sg.ft = FT_BLOCK;
                non_sg.d_type =  s->d_type;
                return 1;
        } else if (0 == strcmp("tape", s->d_name)) {
                my_strcopy(non_sg.name, s->d_name, LMAX_NAME);
                non_sg.ft = FT_CHAR;
                non_sg.d_type =  s->d_type;
                return 1;
        } else if (0 == strncmp("scsi_tape:st", s->d_name, 12)) {
                len = strlen(s->d_name);
                if (isdigit(s->d_name[len - 1])) {
                        /* want 'st<num>' symlink only */
                        my_strcopy(non_sg.name, s->d_name, LMAX_NAME);
                        non_sg.ft = FT_CHAR;
                        non_sg.d_type =  s->d_type;
                        return 1;
                } else
                        return 0;
        } else if (0 == strncmp("onstream_tape:os", s->d_name, 16)) {
                my_strcopy(non_sg.name, s->d_name, LMAX_NAME);
                non_sg.ft = FT_CHAR;
                non_sg.d_type =  s->d_type;
                return 1;
        } else
                return 0;
}

static int
non_sg_scan(const char * dir_name, const struct lsscsi_opt_coll * op)
{
        struct dirent ** namelist;
        int num, k;

        non_sg.ft = FT_OTHER;
        num = scandir(dir_name, &namelist, non_sg_scandir_select, NULL);
        if (num < 0) {
                if (op->verbose > 0) {
                        snprintf(errpath, LMAX_PATH, "scandir: %s", dir_name);
                        perror(errpath);
                }
                return -1;
        }
        for (k = 0; k < num; ++k)
                free(namelist[k]);
        free(namelist);
        return num;
}


static int
sg_scandir_select(const struct dirent * s)
{
        if (FT_OTHER != aa_sg.ft)
                return 0;
        if ((DT_LNK != s->d_type) &&
            ((DT_DIR != s->d_type) || ('.' == s->d_name[0])))
                return 0;
        if (0 == strncmp("scsi_generic", s->d_name, 12)) {
                my_strcopy(aa_sg.name, s->d_name, LMAX_NAME);
                aa_sg.ft = FT_CHAR;
                aa_sg.d_type =  s->d_type;
                return 1;
        } else
                return 0;
}

static int
sg_scan(const char * dir_name)
{
        struct dirent ** namelist;
        int num, k;

        aa_sg.ft = FT_OTHER;
        num = scandir(dir_name, &namelist, sg_scandir_select, NULL);
        if (num < 0)
                return -1;
        for (k = 0; k < num; ++k)
                free(namelist[k]);
        free(namelist);
        return num;
}


static int
sas_port_scandir_select(const struct dirent * s)
{
        if ((DT_LNK != s->d_type) && (DT_DIR != s->d_type))
                return 0;
        if (0 == strncmp("port-", s->d_name, 5))
                return 1;
        return 0;
}

static int
sas_port_scan(const char * dir_name, struct dirent ***port_list)
{
        struct dirent ** namelist;
        int num;

        namelist = NULL;
        num = scandir(dir_name, &namelist, sas_port_scandir_select, NULL);
        if (num < 0) {
                *port_list = NULL;
                return -1;
        }
        *port_list = namelist;
        return num;
}


static int
sas_low_phy_scandir_select(const struct dirent * s)
{
        char * cp;
        int n, m;

        if ((DT_LNK != s->d_type) && (DT_DIR != s->d_type))
                return 0;
        if (0 == strncmp("phy", s->d_name, 3)) {
                if (0 == strlen(sas_low_phy))
                        my_strcopy(sas_low_phy, s->d_name, LMAX_NAME);
                else {
                        cp = (char *)strrchr(s->d_name, ':');
                        if (NULL == cp)
                                return 0;
                        n = atoi(cp + 1);
                        cp = strrchr(sas_low_phy, ':');
                        if (NULL == cp)
                                return 0;
                        m = atoi(cp + 1);
                        if (n < m)
                                my_strcopy(sas_low_phy, s->d_name, LMAX_NAME);
                }
                return 1;
        } else
                return 0;
}

static int
sas_low_phy_scan(const char * dir_name, struct dirent ***phy_list)
{
        struct dirent ** namelist=NULL;
        int num, k;

        memset(sas_low_phy, 0, sizeof(sas_low_phy));
        num = scandir(dir_name, &namelist, sas_low_phy_scandir_select, NULL);
        if (num < 0)
                return -1;
        if (!phy_list) {
                for (k=0; k<num; ++k)
                        free(namelist[k]);
                free(namelist);
        }
        else
                *phy_list = namelist;
        return num;
}

static int
iscsi_target_scandir_select(const struct dirent * s)
{
        char buff[LMAX_PATH];
        int off;
        struct stat a_stat;

        if ((DT_LNK != s->d_type) && (DT_DIR != s->d_type))
                return 0;
        if (0 == strncmp("session", s->d_name, 7)) {
                iscsi_tsession_num = atoi(s->d_name + 7);
                my_strcopy(buff, iscsi_dir_name, LMAX_PATH);
                off = strlen(buff);
                snprintf(buff + off, sizeof(buff) - off,
                         "/%s/target%d:%d:%d", s->d_name, iscsi_target_hct->h,
                         iscsi_target_hct->c, iscsi_target_hct->t);
                if ((stat(buff, &a_stat) >= 0) && S_ISDIR(a_stat.st_mode))
                        return 1;
                else
                        return 0;
        } else
                return 0;
}

static int
iscsi_target_scan(const char * dir_name, const struct addr_hctl * hctl)
{
        struct dirent ** namelist;
        int num, k;

        iscsi_dir_name = dir_name;
        iscsi_target_hct = hctl;
        iscsi_tsession_num = -1;
        num = scandir(dir_name, &namelist, iscsi_target_scandir_select, NULL);
        if (num < 0)
                return -1;
        for (k = 0; k < num; ++k)
                free(namelist[k]);
        free(namelist);
        return num;
}


/* If 'dir_name'/'base_name' is a directory chdir to it. If that is successful
   return 1, else 0 */
static int
if_directory_chdir(const char * dir_name, const char * base_name)
{
        char b[LMAX_PATH];
        struct stat a_stat;

        snprintf(b, sizeof(b), "%s/%s", dir_name, base_name);
        if (stat(b, &a_stat) < 0)
                return 0;
        if (S_ISDIR(a_stat.st_mode)) {
                if (chdir(b) < 0)
                        return 0;
                return 1;
        }
        return 0;
}

/* If 'dir_name'/generic is a directory chdir to it. If that is successful
   return 1. Otherwise look a directory of the form
   'dir_name'/scsi_generic:sg<n> and if found chdir to it and return 1.
   Otherwise return 0. */
static int
if_directory_ch2generic(const char * dir_name)
{
        char b[LMAX_PATH];
        struct stat a_stat;
        const char * old_name = "generic";

        snprintf(b, sizeof(b), "%s/%s", dir_name, old_name);
        if ((stat(b, &a_stat) >= 0) && S_ISDIR(a_stat.st_mode)) {
                if (chdir(b) < 0)
                        return 0;
                return 1;
        }
        /* No "generic", so now look for "scsi_generic:sg<n>" */
        if (1 != sg_scan(dir_name))
                return 0;
        snprintf(b, sizeof(b), "%s/%s", dir_name, aa_sg.name);
        if (stat(b, &a_stat) < 0)
                return 0;
        if (S_ISDIR(a_stat.st_mode)) {
                if (chdir(b) < 0)
                        return 0;
                return 1;
        }
        return 0;
}

/* If 'dir_name'/'base_name' is found places corresponding value in 'value'
 * and returns 1 . Else returns 0.
 */
static int
get_value(const char * dir_name, const char * base_name, char * value,
          int max_value_len)
{
        char b[LMAX_PATH];
        FILE * f;
        int len;

        snprintf(b, sizeof(b), "%s/%s", dir_name, base_name);
        if (NULL == (f = fopen(b, "r"))) {
                return 0;
        }
        if (NULL == fgets(value, max_value_len, f)) {
                /* assume empty */
                value[0] = '\0';
                fclose(f);
                return 1;
        }
        len = strlen(value);
        if ((len > 0) && (value[len - 1] == '\n'))
                value[len - 1] = '\0';
        fclose(f);
        return 1;
}

/* Allocate dev_node_list and collect info on every node in /dev. */
static void
collect_dev_nodes(void)
{
        DIR *dirp;
        struct dirent *dep;
        char device_path[LMAX_DEVPATH];
        struct stat stats;
        struct dev_node_list *cur_list, *prev_list;
        struct dev_node_entry *cur_ent;

        if (dev_node_listhead)
                return; /* already collected nodes */

        dev_node_listhead = (struct dev_node_list*)
                            malloc(sizeof(struct dev_node_list));
        if (! dev_node_listhead)
                return;

        cur_list = dev_node_listhead;
        cur_list->next = NULL;
        cur_list->count = 0;

        dirp = opendir(dev_dir);
        if (dirp == NULL)
                return;

        while (1) {
                dep = readdir(dirp);
                if (dep == NULL)
                        break;

                snprintf(device_path, sizeof(device_path), "%s/%s",
                         dev_dir, dep->d_name);
                /* device_path[LMAX_PATH] = '\0'; */

                if (lstat(device_path, &stats))
                        continue;

                /* Skip non-block/char files. */
                if ( (!S_ISBLK(stats.st_mode)) && (!S_ISCHR(stats.st_mode)) )
                        continue;

                /* Add to the list. */
                if (cur_list->count >= DEV_NODE_LIST_ENTRIES) {
                        prev_list = cur_list;
                        cur_list = (struct dev_node_list *)
                                   malloc(sizeof(struct dev_node_list));
                        if (! cur_list) break;
                        prev_list->next = cur_list;
                        cur_list->next = NULL;
                        cur_list->count = 0;
                }

                cur_ent = &cur_list->nodes[cur_list->count];
                cur_ent->maj = major(stats.st_rdev);
                cur_ent->min = minor(stats.st_rdev);
                if (S_ISBLK(stats.st_mode))
                        cur_ent->type = BLK_DEV;
                else if (S_ISCHR(stats.st_mode))
                        cur_ent->type = CHR_DEV;
                cur_ent->mtime = stats.st_mtime;
                my_strcopy(cur_ent->name, device_path, sizeof(cur_ent->name));

                cur_list->count++;
        }

        closedir(dirp);
}

/* Free dev_node_list. */
static void
free_dev_node_list(void)
{
        if (dev_node_listhead) {
                struct dev_node_list *cur_list, *next_list;

                cur_list = dev_node_listhead;
                while (cur_list) {
                        next_list = cur_list->next;
                        free(cur_list);
                        cur_list = next_list;
                }

                dev_node_listhead = NULL;
        }
}

/* Given a path to a class device, find the most recent device node with
 * matching major/minor. Outputs to node which is assumed to be at least
 * LMAX_NAME bytes long. Returns 1 if match found, 0 otherwise. */
static int
get_dev_node(const char * wd, char * node, enum dev_type type)
{
        struct dev_node_list *cur_list;
        struct dev_node_entry *cur_ent;
        char value[LMAX_NAME];
        unsigned int maj, min;
        time_t newest_mtime = 0;
        int match_found = 0;
        unsigned int k = 0;

        /* assume 'node' is at least 2 bytes long */
        memcpy(node, "-", 2);
        if (dev_node_listhead == NULL) {
                collect_dev_nodes();
                if (dev_node_listhead == NULL)
                        goto exit;
        }

        /* Get the major/minor for this device. */
        if (!get_value(wd, "dev", value, LMAX_NAME))
                goto exit;
        sscanf(value, "%u:%u", &maj, &min);

        /* Search the node list for the newest match on this major/minor. */
        cur_list = dev_node_listhead;

        while (1) {
                if (k >= cur_list->count) {
                        cur_list = cur_list->next;
                        if (! cur_list)
                                break;
                        k = 0;
                }

                cur_ent = &cur_list->nodes[k];
                k++;

                if ((maj == cur_ent->maj) &&
                    (min == cur_ent->min) &&
                    (type == cur_ent->type)) {
                        if ((!match_found) ||
                            (difftime(cur_ent->mtime,newest_mtime) > 0)) {
                                newest_mtime = cur_ent->mtime;
                                my_strcopy(node, cur_ent->name, LMAX_NAME);
                        }
                        match_found = 1;
                }
        }

exit:
        return match_found;
}

/* Allocate disk_wwn_node_list and collect info on every node in
 * /dev/disk/by-id/wwn* that does not contain "part" . Returns
 * number of wwn nodes collected, 0 for already collected and
 * -1 for error. */
static int
collect_disk_wwn_nodes(void)
{
        int k;
        int num = 0;
        DIR *dirp;
        struct dirent *dep;
        char device_path[PATH_MAX + 1];
        char symlink_path[PATH_MAX + 1];
        struct stat stats;
        struct disk_wwn_node_list *cur_list, *prev_list;
        struct disk_wwn_node_entry *cur_ent;

        if (disk_wwn_node_listhead)
                return num; /* already collected nodes */

        disk_wwn_node_listhead = (struct disk_wwn_node_list *)
                                 malloc(sizeof(struct disk_wwn_node_list));
        if (! disk_wwn_node_listhead)
                return -1;

        cur_list = disk_wwn_node_listhead;
        memset(cur_list, 0, sizeof(struct disk_wwn_node_list));

        dirp = opendir(dev_disk_byid_dir);
        if (dirp == NULL)
                return -1;

        while (1) {
                dep = readdir(dirp);
                if (dep == NULL)
                        break;
                if (memcmp("wwn-", dep->d_name, 4))
                        continue;       /* needs to start with "wwn-" */
                if (strstr(dep->d_name, "part"))
                        continue;       /* skip if contains "part" */

                snprintf(device_path, PATH_MAX, "%s/%s", dev_disk_byid_dir,
                         dep->d_name);
                device_path [PATH_MAX] = '\0';
                if (lstat(device_path, &stats))
                        continue;
                if (! S_ISLNK(stats.st_mode))
                        continue;       /* Skip non-symlinks */
                if ((k = readlink(device_path, symlink_path, PATH_MAX)) < 1)
                        continue;
                symlink_path[k] = '\0';

                /* Add to the list. */
                if (cur_list->count >= DISK_WWN_NODE_LIST_ENTRIES) {
                        prev_list = cur_list;
                        cur_list = (struct disk_wwn_node_list *)
                                   malloc(sizeof(struct disk_wwn_node_list));
                        if (! cur_list)
                                break;
                        memset(cur_list, 0, sizeof(struct disk_wwn_node_list));
                        prev_list->next = cur_list;
                }

                cur_ent = &cur_list->nodes[cur_list->count];
                my_strcopy(cur_ent->wwn, dep->d_name + 4,
                           sizeof(cur_ent->wwn));
                my_strcopy(cur_ent->disk_bname, basename(symlink_path),
                           sizeof(cur_ent->disk_bname));
                cur_list->count++;
                ++num;
        }
        closedir(dirp);
        return num;
}

/* Free disk_wwn_node_list. */
static void
free_disk_wwn_node_list(void)
{
        if (disk_wwn_node_listhead) {
                struct disk_wwn_node_list *cur_list, *next_list;

                cur_list = disk_wwn_node_listhead;
                while (cur_list) {
                        next_list = cur_list->next;
                        free(cur_list);
                        cur_list = next_list;
                }

                disk_wwn_node_listhead = NULL;
        }
}

/* Given a path to a class device, find the most recent device node with
   matching major/minor. Returns 1 if match found, 0 otherwise. */
static int
get_disk_wwn(const char *wd, char * wwn_str, int max_wwn_str_len)
{
        struct disk_wwn_node_list *cur_list;
        struct disk_wwn_node_entry *cur_ent;
        char name[LMAX_PATH];
        char * bn;
        unsigned int k = 0;

        my_strcopy(name, wd, sizeof(name));
        name[sizeof(name) - 1] = '\0';
        bn = basename(name);
        if (disk_wwn_node_listhead == NULL) {
                collect_disk_wwn_nodes();
                if (disk_wwn_node_listhead == NULL)
                        return 0;
        }
        cur_list = disk_wwn_node_listhead;
        while (1) {
                if (k >= cur_list->count) {
                        cur_list = cur_list->next;
                        if (! cur_list)
                                break;
                        k = 0;
                }
                cur_ent = &cur_list->nodes[k];
                k++;
                if (0 == strcmp(cur_ent->disk_bname, bn)) {
                        my_strcopy(wwn_str, cur_ent->wwn, max_wwn_str_len);
                        wwn_str[max_wwn_str_len - 1] = '\0';
                        return 1;
                }
        }
        return 0;
}

/*
 * Look up a device node in a directory with symlinks to device nodes.
 * @dir: Directory to examine, e.g. "/dev/disk/by-id".
 * @pfx: Prefix of the symlink, e.g. "scsi-".
 * @dev: Device node to look up, e.g. "/dev/sda".
 * Returns a pointer to the name of the symlink without the prefix if a match
 * has been found. The caller must free the pointer returned by this function.
 * Side effect: changes the working directory to @dir.
 */
static char *
lookup_dev(const char *dir, const char *pfx, const char *dev)
{
        struct stat stats;
        unsigned st_rdev;
        DIR *dirp;
        struct dirent *entry;
        char *result = NULL;

        if (stat(dev, &stats) < 0)
                goto out;
        st_rdev = stats.st_rdev;
        if (chdir(dir) < 0)
                goto out;
        dirp = opendir(dir);
        if (!dirp)
                goto out;
        while ((entry = readdir(dirp)) != NULL) {
                if (stat(entry->d_name, &stats) >= 0 &&
                    stats.st_rdev == st_rdev &&
                    strncmp(entry->d_name, pfx, strlen(pfx)) == 0) {
                        result = strdup(entry->d_name + strlen(pfx));
                        break;
                }
        }
        closedir(dirp);
out:
        return result;
}

/*
 * Obtain the SCSI ID of a disk.
 * @dev_node: Device node of the disk, e.g. "/dev/sda".
 * Return value: pointer to the SCSI ID if lookup succeeded or NULL if lookup
 * failed.
 * The caller must free the returned buffer with free().
 */
static char *
get_disk_scsi_id(const char *dev_node)
{
        char sys_block[64];
        char holder[16];
        char *scsi_id = NULL;
        DIR *dir;
        struct dirent *entry;

        scsi_id = lookup_dev(dev_disk_byid_dir, "scsi-", dev_node);
        if (scsi_id)
                goto out;
        scsi_id = lookup_dev(dev_disk_byid_dir, "dm-uuid-mpath-", dev_node);
        if (scsi_id)
                goto out;
        scsi_id = lookup_dev(dev_disk_byid_dir, "usb-", dev_node);
        if (scsi_id)
                goto out;
        snprintf(sys_block, sizeof(sys_block), "%s/class/block/%s/holders",
                 sysfsroot, dev_node + 5);
        dir = opendir(sys_block);
        if (!dir)
                goto out;
        while ((entry = readdir(dir)) != NULL) {
                snprintf(holder, sizeof(holder), "/dev/%s", entry->d_name);
                scsi_id = get_disk_scsi_id(holder);
                if (scsi_id)
                        break;
        }
        closedir(dir);
out:
        return scsi_id;
}

/* Fetch USB device name string (form "<b>-<p1>[.<p2>]+:<c>.<i>") given
 * either a SCSI host name or devname (i.e. "h:c:t:l") string. If detected
 * return 'b' (pointer to start of USB device name string which is null
 * terminated), else return NULL.
 */
static char *
get_usb_devname(const char * hname, const char * devname, char * b, int b_len)
{
        char buff[LMAX_DEVPATH];
        char bf2[LMAX_PATH];
        int len;
        const char * np;
        char * cp;
        char * c2p;

        if (hname) {
                snprintf(buff, sizeof(buff), "%s%s", sysfsroot, scsi_host);
                np = hname;
        } else if (devname) {
                snprintf(buff, sizeof(buff), "%s%s", sysfsroot,
                         class_scsi_dev);
                np = devname;
        } else
                return NULL;
        if (if_directory_chdir(buff, np) && getcwd(bf2, sizeof(bf2)) &&
            strstr(bf2, "usb")) {
                if (b_len > 0)
                        b[0] = '\0';
                if ((cp = strstr(bf2, "/host"))) {
                        len = (cp - bf2) - 1;
                        if ((len > 0) &&
                            ((c2p = (char *)memrchr(bf2, '/', len)))) {
                                len = cp - ++c2p;
                                snprintf(b, b_len, "%.*s", len, c2p);
                        }
                }
                return b;
        }
        return NULL;
}

/*  Parse colon_list into host/channel/target/lun ("hctl") array,
 *  return 1 if successful, else 0.
 */
static int
parse_colon_list(const char * colon_list, struct addr_hctl * outp)
{
        int k;
        unsigned short u;
        uint64_t z;
        const char * elem_end;

        if ((! colon_list) || (! outp))
                return 0;
        if (1 != sscanf(colon_list, "%d", &outp->h))
                return 0;
        if (NULL == (elem_end = strchr(colon_list, ':')))
                return 0;
        colon_list = elem_end + 1;
        if (1 != sscanf(colon_list, "%d", &outp->c))
                return 0;
        if (NULL == (elem_end = strchr(colon_list, ':')))
                return 0;
        colon_list = elem_end + 1;
        if (1 != sscanf(colon_list, "%d", &outp->t))
                return 0;
        if (NULL == (elem_end = strchr(colon_list, ':')))
                return 0;
        colon_list = elem_end + 1;
        if (1 != sscanf(colon_list, "%" SCNu64 , &outp->l))
                return 0;
        z = outp->l;
        for (k = 0; k < 4; ++k, z >>= 16) {
                u = z & 0xffff;
                outp->lun_arr[(2 * k) + 1] = u & 0xff;
                outp->lun_arr[2 * k] = (u >> 8) & 0xff;
        }
        return 1;
}

/* Print enclosure device link from the rport- or end_device- */
static void
print_enclosure_device(const char *devname, const char *path,
                        const struct lsscsi_opt_coll * op)
{
        struct addr_hctl hctl;
        char b[LMAX_PATH];

        if (parse_colon_list(devname, &hctl)) {
                snprintf(b, sizeof(b),
                         "%s/device/target%d:%d:%d/%d:%d:%d:%" PRIu64,
                         path, hctl.h, hctl.c, hctl.t,
                         hctl.h, hctl.c, hctl.t, hctl.l);
                if (enclosure_device_scan(b, op) > 0)
                        printf("  %s\n",enclosure_device.name);
        }
}


/* Check host associated with 'devname' for known transport types. If so set
   transport_id, place a string in 'b' and return 1. Otherwise return 0. */
static int
transport_init(const char * devname, /* const struct lsscsi_opt_coll * op, */
               int b_len, char * b)
{
        char buff[LMAX_DEVPATH];
        char wd[LMAX_PATH];
        int off;
        char * cp;
        struct stat a_stat;

        /* SPI host */
        snprintf(buff, sizeof(buff), "%s%s%s", sysfsroot, spi_host, devname);
        if ((stat(buff, &a_stat) >= 0) && S_ISDIR(a_stat.st_mode)) {
                transport_id = TRANSPORT_SPI;
                snprintf(b, b_len, "spi:");
                return 1;
        }

        /* FC host */
        snprintf(buff, sizeof(buff), "%s%s%s", sysfsroot, fc_host, devname);
        if ((stat(buff, &a_stat) >= 0) && S_ISDIR(a_stat.st_mode)) {
                if (get_value(buff, "symbolic_name", wd, sizeof(wd))) {
                        if (strstr(wd, " over ")) {
                                transport_id = TRANSPORT_FCOE;
                                snprintf(b, b_len, "fcoe:");
                        }
                }
                if (transport_id != TRANSPORT_FCOE) {
                        transport_id = TRANSPORT_FC;
                        snprintf(b, b_len, "fc:");
                }
                off = strlen(b);
                if (get_value(buff, "port_name", b + off, b_len - off)) {
                        off = strlen(b);
                        my_strcopy(b + off, ",", sizeof(b) - off);
                        off = strlen(b);
                } else
                        return 0;
                if (get_value(buff, "port_id", b + off, b_len - off))
                        return 1;
                else
                        return 0;
        }

        /* SAS host */
        /* SAS transport layer representation */
        snprintf(buff, sizeof(buff), "%s%s%s", sysfsroot, sas_host, devname);
        if ((stat(buff, &a_stat) >= 0) && S_ISDIR(a_stat.st_mode)) {
                transport_id = TRANSPORT_SAS;
                off = strlen(buff);
                snprintf(buff + off, sizeof(buff) - off, "/device");
                if (sas_low_phy_scan(buff, NULL) < 1)
                        return 0;
                snprintf(buff, sizeof(buff), "%s%s%s", sysfsroot, sas_phy,
                         sas_low_phy);
                snprintf(b, b_len, "sas:");
                off = strlen(b);
                if (get_value(buff, "sas_address", b + off, b_len - off))
                        return 1;
                else
                        fprintf(stderr, "_init: no sas_address, wd=%s\n",
                                buff);
        }

        /* SAS class representation */
        snprintf(buff, sizeof(buff), "%s%s%s%s", sysfsroot, scsi_host,
                 devname, "/device/sas/ha");
        if ((stat(buff, &a_stat) >= 0) && S_ISDIR(a_stat.st_mode)) {
                transport_id = TRANSPORT_SAS_CLASS;
                snprintf(b, b_len, "sas:");
                off = strlen(b);
                if (get_value(buff, "device_name", b + off, b_len - off))
                        return 1;
                else
                        fprintf(stderr, "_init: no device_name, wd=%s\n",
                                buff);
        }

        /* SBP (FireWire) host */
        do {
                char *t, buff2[LMAX_DEVPATH];

                /* resolve SCSI host device */
                snprintf(buff, sizeof(buff), "%s%s%s%s", sysfsroot, scsi_host,
                         devname, "/device");
                if (readlink(buff, buff2, sizeof(buff2)) <= 0)
                        break;

                /* check if the SCSI host has a FireWire host as ancestor */
                if (!(t = strstr(buff2, "/fw-host")))
                        break;
                transport_id = TRANSPORT_SBP;

                /* terminate buff2 after FireWire host */
                if (!(t = strchr(t+1, '/')))
                        break;
                *t = 0;

                /* resolve FireWire host device */
                buff[strlen(buff) - strlen("device")] = 0;
                if (strlen(buff) + strlen(buff2) + strlen("host_id/guid") + 2
                    > sizeof(buff))
                        break;
                my_strcopy(buff + strlen(buff), buff2, sizeof(buff));

                /* read the FireWire host's EUI-64 */
                if (!get_value(buff, "host_id/guid", buff2, sizeof(buff2)) ||
                    strlen(buff2) != 18)
                        break;
                snprintf(b, b_len, "sbp:%s", buff2 + 2);
                return 1;
        } while (0);

        /* iSCSI host */
        snprintf(buff, sizeof(buff), "%s%s%s", sysfsroot, iscsi_host, devname);
        if ((stat(buff, &a_stat) >= 0) && S_ISDIR(a_stat.st_mode)) {
                transport_id = TRANSPORT_ISCSI;
                snprintf(b, b_len, "iscsi:");
// >>>       Can anything useful be placed after "iscsi:" in single line
//           host output?
//           Hmmm, probably would like SAM-4 ",i,0x" notation here.
                return 1;
        }

        /* USB host? */
        cp = get_usb_devname(devname, NULL, wd, sizeof(wd) - 1);
        if (cp) {
                transport_id = TRANSPORT_USB;
                snprintf(b, b_len, "usb: %s", cp);
                return 1;
        }

        /* ATA or SATA host, crude check: driver name */
        snprintf(buff, sizeof(buff), "%s%s%s", sysfsroot, scsi_host, devname);
        if (get_value(buff, "proc_name", wd, sizeof(wd))) {
                if (0 == strcmp("ahci", wd)) {
                        transport_id = TRANSPORT_SATA;
                        snprintf(b, b_len, "sata:");
                        return 1;
                } else if (strstr(wd, "ata")) {
                        if (0 == memcmp("sata", wd, 4)) {
                                transport_id = TRANSPORT_SATA;
                                snprintf(b, b_len, "sata:");
                                return 1;
                        }
                        transport_id = TRANSPORT_ATA;
                        snprintf(b, b_len, "ata:");
                        return 1;
                }
        }
        return 0;
}

/* Given the transport_id of a SCSI host (initiator) associated with
 * 'path_name' output additional information.
 */
static void
transport_init_longer(const char * path_name,
                      const struct lsscsi_opt_coll * op)
{
        char buff[LMAX_PATH];
        char bname[LMAX_NAME];
        char value[LMAX_NAME];
        char * cp;
        struct stat a_stat;
        struct dirent ** phylist;
        struct dirent ** portlist;
        int phynum;
        int portnum;
        int i, j, len;

        my_strcopy(buff, path_name, sizeof(buff));
        cp = basename(buff);
        my_strcopy(bname, cp, sizeof(bname));
        bname[sizeof(bname) - 1] = '\0';
        cp = bname;
        switch (transport_id) {
        case TRANSPORT_SPI:
                printf("  transport=spi\n");
                snprintf(buff, sizeof(buff), "%s%s%s", sysfsroot, spi_host,
                         cp);
                if (get_value(buff, "signalling", value, sizeof(value)))
                        printf("  signalling=%s\n", value);
                break;
        case TRANSPORT_FC:
        case TRANSPORT_FCOE:
                printf("  transport=%s\n",
                       transport_id == TRANSPORT_FC ? "fc:" : "fcoe:");
                snprintf(buff, sizeof(buff), "%s%s%s", path_name,
                         "/device/fc_host/", cp);
                if (stat(buff, &a_stat) < 0) {
                        if (op->verbose > 2)
                                printf("no fc_host directory\n");
                        break;
                }
                if (get_value(buff, "active_fc4s", value, sizeof(value)))
                        printf("  active_fc4s=%s\n", value);
                if (get_value(buff, "supported_fc4s", value, sizeof(value)))
                        printf("  supported_fc4s=%s\n", value);
                if (get_value(buff, "fabric_name", value, sizeof(value)))
                        printf("  fabric_name=%s\n", value);
                if (get_value(buff, "maxframe_size", value, sizeof(value)))
                        printf("  maxframe_size=%s\n", value);
                if (get_value(buff, "max_npiv_vports", value, sizeof(value)))
                        printf("  max_npiv_vports=%s\n", value);
                if (get_value(buff, "npiv_vports_inuse", value, sizeof(value)))
                        printf("  npiv_vports_inuse=%s\n", value);
                if (get_value(buff, "node_name", value, sizeof(value)))
                        printf("  node_name=%s\n", value);
                if (get_value(buff, "port_name", value, sizeof(value)))
                        printf("  port_name=%s\n", value);
                if (get_value(buff, "port_id", value, sizeof(value)))
                        printf("  port_id=%s\n", value);
                if (get_value(buff, "port_state", value, sizeof(value)))
                        printf("  port_state=%s\n", value);
                if (get_value(buff, "port_type", value, sizeof(value)))
                        printf("  port_type=%s\n", value);
                if (get_value(buff, "speed", value, sizeof(value)))
                        printf("  speed=%s\n", value);
                if (get_value(buff, "supported_speeds", value, sizeof(value)))
                        printf("  supported_speeds=%s\n", value);
                if (get_value(buff, "supported_classes", value, sizeof(value)))
                        printf("  supported_classes=%s\n", value);
                if (get_value(buff, "tgtid_bind_type", value, sizeof(value)))
                        printf("  tgtid_bind_type=%s\n", value);
                if (op->verbose > 2)
                        printf("fetched from directory: %s\n", buff);
                break;
        case TRANSPORT_SAS:
                printf("  transport=sas\n");
                snprintf(buff, sizeof(buff), "%s%s", path_name, "/device");
                if ((portnum = sas_port_scan(buff, &portlist)) < 1) {
                        /* no configured ports */
                        printf("  no configured ports\n");
                        if ((phynum = sas_low_phy_scan(buff, &phylist)) < 1) {
                                printf("  no configured phys\n");
                                return;
                        }
                        for (i = 0; i < phynum; ++i) {
                                /* emit something potentially useful */
                                snprintf(buff, sizeof(buff), "%s%s%s",
                                         sysfsroot, sas_phy,
                                         phylist[i]->d_name);
                                printf("  %s\n",phylist[i]->d_name);
                                if (get_value(buff, "sas_address", value,
                                              sizeof(value)))
                                        printf("    sas_address=%s\n", value);
                                if (get_value(buff, "phy_identifier", value,
                                              sizeof(value)))
                                        printf("    phy_identifier=%s\n",
                                               value);
                                if (get_value(buff, "minimum_linkrate", value,
                                              sizeof(value)))
                                        printf("    minimum_linkrate=%s\n",
                                               value);
                                if (get_value(buff, "minimum_linkrate_hw",
                                              value, sizeof(value)))
                                        printf("    minimum_linkrate_hw=%s\n",
                                               value);
                                if (get_value(buff, "maximum_linkrate", value,
                                              sizeof(value)))
                                        printf("    maximum_linkrate=%s\n",
                                               value);
                                if (get_value(buff, "maximum_linkrate_hw",
                                              value, sizeof(value)))
                                        printf("    maximum_linkrate_hw=%s\n",
                                               value);
                                if (get_value(buff, "negotiated_linkrate",
                                              value, sizeof(value)))
                                        printf("    negotiated_linkrate=%s\n",
                                               value);
                        }
                        return;
                }
                for (i = 0; i < portnum; ++i) {     /* for each host port */
                        snprintf(buff, sizeof(buff), "%s%s%s", path_name,
                                 "/device/", portlist[i]->d_name);
                        if ((phynum = sas_low_phy_scan(buff, &phylist)) < 1) {
                                printf("  %s: phy list not available\n",
                                       portlist[i]->d_name);
                                free(portlist[i]);
                                continue;
                        }

                        snprintf(buff, sizeof(buff), "%s%s%s", sysfsroot,
                                 sas_port, portlist[i]->d_name);
                        if (get_value(buff, "num_phys", value,
                                      sizeof(value))) {
                                printf("  %s: num_phys=%s,",
                                       portlist[i]->d_name, value);
                                for (j = 0; j < phynum; ++j) {
                                        printf(" %s", phylist[j]->d_name);
                                        free(phylist[j]);
                                }
                                printf("\n");
                                if (op->verbose > 2)
                                        printf("  fetched from directory: "
                                               "%s\n", buff);
                                free(phylist);
                        }
                        snprintf(buff, sizeof(buff), "%s%s%s", sysfsroot,
                                 sas_phy, sas_low_phy);
                        if (get_value(buff, "device_type", value,
                                      sizeof(value)))
                                printf("    device_type=%s\n", value);
                        if (get_value(buff, "initiator_port_protocols", value,
                                      sizeof(value)))
                                printf("    initiator_port_protocols=%s\n",
                                       value);
                        if (get_value(buff, "invalid_dword_count", value,
                                      sizeof(value)))
                                printf("    invalid_dword_count=%s\n", value);
                        if (get_value(buff, "loss_of_dword_sync_count", value,
                                      sizeof(value)))
                                printf("    loss_of_dword_sync_count=%s\n",
                                       value);
                        if (get_value(buff, "minimum_linkrate", value,
                                      sizeof(value)))
                                printf("    minimum_linkrate=%s\n", value);
                        if (get_value(buff, "minimum_linkrate_hw", value,
                                      sizeof(value)))
                                printf("    minimum_linkrate_hw=%s\n", value);
                        if (get_value(buff, "maximum_linkrate", value,
                                      sizeof(value)))
                                printf("    maximum_linkrate=%s\n", value);
                        if (get_value(buff, "maximum_linkrate_hw", value,
                                      sizeof(value)))
                                printf("    maximum_linkrate_hw=%s\n", value);
                        if (get_value(buff, "negotiated_linkrate", value,
                                      sizeof(value)))
                                printf("    negotiated_linkrate=%s\n", value);
                        if (get_value(buff, "phy_identifier", value,
                                      sizeof(value)))
                                printf("    phy_identifier=%s\n", value);
                        if (get_value(buff, "phy_reset_problem_count", value,
                                      sizeof(value)))
                                printf("    phy_reset_problem_count=%s\n",
                                       value);
                        if (get_value(buff, "running_disparity_error_count",
                                      value, sizeof(value)))
                                printf("    running_disparity_error_count="
                                       "%s\n", value);
                        if (get_value(buff, "sas_address", value,
                                      sizeof(value)))
                                printf("    sas_address=%s\n", value);
                        if (get_value(buff, "target_port_protocols", value,
                                      sizeof(value)))
                                printf("    target_port_protocols=%s\n",
                                       value);
                        if (op->verbose > 2)
                                printf("  fetched from directory: %s\n", buff);

                        free(portlist[i]);

                }
                free(portlist);

                break;
        case TRANSPORT_SAS_CLASS:
                printf("  transport=sas\n");
                printf("  sub_transport=sas_class\n");
                snprintf(buff, sizeof(buff), "%s%s", path_name,
                         "/device/sas/ha");
                if (get_value(buff, "device_name", value, sizeof(value)))
                        printf("  device_name=%s\n", value);
                if (get_value(buff, "ha_name", value, sizeof(value)))
                        printf("  ha_name=%s\n", value);
                if (get_value(buff, "version_descriptor", value,
                              sizeof(value)))
                        printf("  version_descriptor=%s\n", value);
                printf("  phy0:\n");
                len = strlen(buff);
                snprintf(buff + len, sizeof(buff) - len, "%s", "/phys/0");
                if (get_value(buff, "class", value, sizeof(value)))
                        printf("    class=%s\n", value);
                if (get_value(buff, "enabled", value, sizeof(value)))
                        printf("    enabled=%s\n", value);
                if (get_value(buff, "id", value, sizeof(value)))
                        printf("    id=%s\n", value);
                if (get_value(buff, "iproto", value, sizeof(value)))
                        printf("    iproto=%s\n", value);
                if (get_value(buff, "linkrate", value, sizeof(value)))
                        printf("    linkrate=%s\n", value);
                if (get_value(buff, "oob_mode", value, sizeof(value)))
                        printf("    oob_mode=%s\n", value);
                if (get_value(buff, "role", value, sizeof(value)))
                        printf("    role=%s\n", value);
                if (get_value(buff, "sas_addr", value, sizeof(value)))
                        printf("    sas_addr=%s\n", value);
                if (get_value(buff, "tproto", value, sizeof(value)))
                        printf("    tproto=%s\n", value);
                if (get_value(buff, "type", value, sizeof(value)))
                        printf("    type=%s\n", value);
                if (op->verbose > 2)
                        printf("fetched from directory: %s\n", buff);
                break;
        case TRANSPORT_ISCSI:
                printf("  transport=iSCSI\n");
// >>>       This is the multi-line host output for iSCSI. Anymore to
//           add here? [From
//           /sys/class/scsi_host/hostN/device/iscsi_host:hostN directory]
                break;
        case TRANSPORT_SBP:
                printf("  transport=sbp\n");
                break;
        case TRANSPORT_USB:
                printf("  transport=usb\n");
                printf("  device_name=%s\n", get_usb_devname(cp, NULL,
                       value, sizeof(value)));
                break;
        case TRANSPORT_ATA:
                printf("  transport=ata\n");
                break;
        case TRANSPORT_SATA:
                printf("  transport=sata\n");
                break;
        default:
                if (op->verbose > 1)
                        fprintf(stderr, "No transport information\n");
                break;
        }
}

/* Attempt to determine the transport type of the SCSI device (LU) associated
   with 'devname'. If found set transport_id, place string in 'b' and return
   1. Otherwise return 0. */
static int
transport_tport(const char * devname,
                /* const struct lsscsi_opt_coll * op, */ int b_len, char * b)
{
        char buff[LMAX_DEVPATH];
        char wd[LMAX_PATH];
        char nm[LMAX_NAME];
        char tpgt[LMAX_NAME];
        char * cp;
        struct addr_hctl hctl;
        int off, n;
        struct stat a_stat;

        if (! parse_colon_list(devname, &hctl))
                return 0;

        /* SAS host? */
        snprintf(buff, sizeof(buff), "%s%shost%d", sysfsroot, sas_host,
                 hctl.h);
        if ((stat(buff, &a_stat) >= 0) && S_ISDIR(a_stat.st_mode)) {
                /* SAS transport layer representation */
                transport_id = TRANSPORT_SAS;
                snprintf(buff, sizeof(buff), "%s%s%s", sysfsroot,
                         class_scsi_dev, devname);
                if (if_directory_chdir(buff, "device")) {
                        if (NULL == getcwd(wd, sizeof(wd)))
                                return 0;
                        cp = strrchr(wd, '/');
                        if (NULL == cp)
                                return 0;
                        *cp = '\0';
                        cp = strrchr(wd, '/');
                        if (NULL == cp)
                                return 0;
                        *cp = '\0';
                        cp = basename(wd);
                        my_strcopy(sas_hold_end_device, cp,
                                   sizeof(sas_hold_end_device));
                        snprintf(buff, sizeof(buff), "%s%s%s", sysfsroot,
                                 sas_device, cp);

                        snprintf(b, b_len, "sas:");
                        off = strlen(b);
                        if (get_value(buff, "sas_address", b + off,
                                      b_len - off))
                                return 1;
                        else
                                fprintf(stderr, "_tport: no "
                                        "sas_address, wd=%s\n", buff);
                } else
                        fprintf(stderr, "_tport: down FAILED: %s\n", buff);
                return 0;
        }

        /* SPI host? */
        snprintf(buff, sizeof(buff), "%s%shost%d", sysfsroot, spi_host,
                 hctl.h);
        if ((stat(buff, &a_stat) >= 0) && S_ISDIR(a_stat.st_mode)) {
                transport_id = TRANSPORT_SPI;
                snprintf(b, b_len, "spi:%d", hctl.t);
                return 1;
        }

        /* FC host? */
        snprintf(buff, sizeof(buff), "%s%shost%d", sysfsroot, fc_host,
                 hctl.h);
        if ((stat(buff, &a_stat) >= 0) && S_ISDIR(a_stat.st_mode)) {
                if (get_value(buff, "symbolic_name", wd, sizeof(wd))) {
                        if (strstr(wd, " over ")) {
                                transport_id = TRANSPORT_FCOE;
                                snprintf(b, b_len, "fcoe:");
                        }
                }
                if (transport_id != TRANSPORT_FCOE) {
                        transport_id = TRANSPORT_FC;
                        snprintf(b, b_len, "fc:");
                }
                snprintf(buff, sizeof(buff), "%s%starget%d:%d:%d", sysfsroot,
                         fc_transport, hctl.h, hctl.c, hctl.t);
                off = strlen(b);
                if (get_value(buff, "port_name", b + off, b_len - off)) {
                        off = strlen(b);
                        my_strcopy(b + off, ",", sizeof(b) - off);
                        off = strlen(b);
                } else
                        return 0;
                if (get_value(buff, "port_id", b + off, b_len - off))
                        return 1;
                else
                        return 0;
        }

        /* SAS class representation or SBP? */
        snprintf(buff, sizeof(buff), "%s%s/%s", sysfsroot, bus_scsi_devs,
                 devname);
        if (if_directory_chdir(buff, "sas_device")) {
                transport_id = TRANSPORT_SAS_CLASS;
                snprintf(b, b_len, "sas:");
                off = strlen(b);
                if (get_value(".", "sas_addr", b + off, b_len - off))
                        return 1;
                else
                        fprintf(stderr, "_tport: no sas_addr, "
                                "wd=%s\n", buff);
        } else if (get_value(buff, "ieee1394_id", wd, sizeof(wd))) {
                /* IEEE1394 SBP device */
                transport_id = TRANSPORT_SBP;
                snprintf(b, b_len, "sbp:%s", wd);
                return 1;
        }

        /* iSCSI device? */
        snprintf(buff, sizeof(buff), "%s%shost%d/device", sysfsroot,
                 iscsi_host, hctl.h);
        if ((stat(buff, &a_stat) >= 0) && S_ISDIR(a_stat.st_mode)) {
                if (1 != iscsi_target_scan(buff, &hctl))
                        return 0;
                transport_id = TRANSPORT_ISCSI;
                snprintf(buff, sizeof(buff), "%s%ssession%d", sysfsroot,
                         iscsi_session, iscsi_tsession_num);
                if (! get_value(buff, "targetname", nm, sizeof(nm)))
                        return 0;
                if (! get_value(buff, "tpgt", tpgt, sizeof(tpgt)))
                        return 0;
                n = atoi(tpgt);
                // output target port name as per sam4r08, annex A, table A.3
                snprintf(b, b_len, "%s,t,0x%x", nm, n);
// >>>       That reference says maximum length of targetname is 223 bytes
//           (UTF-8) excluding trailing null.
                return 1;
        }

        /* USB device? */
        cp = get_usb_devname(NULL, devname, wd, sizeof(wd) - 1);
        if (cp) {
                transport_id = TRANSPORT_USB;
                snprintf(b, b_len, "usb: %s", cp);
                return 1;
        }

        /* ATA or SATA device, crude check: driver name */
        snprintf(buff, sizeof(buff), "%s%shost%d", sysfsroot, scsi_host,
                 hctl.h);
        if (get_value(buff, "proc_name", wd, sizeof(wd))) {
                if (0 == strcmp("ahci", wd)) {
                        transport_id = TRANSPORT_SATA;
                        snprintf(b, b_len, "sata:");
                        return 1;
                } else if (strstr(wd, "ata")) {
                        if (0 == memcmp("sata", wd, 4)) {
                                transport_id = TRANSPORT_SATA;
                                snprintf(b, b_len, "sata:");
                                return 1;
                        }
                        transport_id = TRANSPORT_ATA;
                        snprintf(b, b_len, "ata:");
                        return 1;
                }
        }
        return 0;
}

/* Given the transport_id of the SCSI device (LU) associated with 'devname'
   output additional information. */
static void
transport_tport_longer(const char * devname,
                       const struct lsscsi_opt_coll * op)
{
        char path_name[LMAX_DEVPATH];
        char buff[LMAX_DEVPATH];
        char b2[LMAX_DEVPATH];
        char wd[LMAX_PATH];
        char value[LMAX_NAME];
        struct addr_hctl hctl;
        char * cp;

#if 0
        snprintf(buff, sizeof(buff), "%s/scsi_device:%s", path_name, devname);
        if (! if_directory_chdir(buff, "device"))
                return;
        if (NULL == getcwd(wd, sizeof(wd)))
                return;
#else
        snprintf(path_name, sizeof(path_name), "%s%s%s", sysfsroot,
                 class_scsi_dev, devname);
        my_strcopy(buff, path_name, sizeof(buff));
#endif
        switch (transport_id) {
        case TRANSPORT_SPI:
                printf("  transport=spi\n");
                if (! parse_colon_list(devname, &hctl))
                        break;
                snprintf(buff, sizeof(buff), "%s%starget%d:%d:%d", sysfsroot,
                         spi_transport, hctl.h, hctl.c, hctl.t);
                printf("  target_id=%d\n", hctl.t);
                if (get_value(buff, "dt", value, sizeof(value)))
                        printf("  dt=%s\n", value);
                if (get_value(buff, "max_offset", value, sizeof(value)))
                        printf("  max_offset=%s\n", value);
                if (get_value(buff, "max_width", value, sizeof(value)))
                        printf("  max_width=%s\n", value);
                if (get_value(buff, "min_period", value, sizeof(value)))
                        printf("  min_period=%s\n", value);
                if (get_value(buff, "offset", value, sizeof(value)))
                        printf("  offset=%s\n", value);
                if (get_value(buff, "period", value, sizeof(value)))
                        printf("  period=%s\n", value);
                if (get_value(buff, "width", value, sizeof(value)))
                        printf("  width=%s\n", value);
                break;
        case TRANSPORT_FC:
        case TRANSPORT_FCOE:
                printf("  transport=%s\n",
                       transport_id == TRANSPORT_FC ? "fc:" : "fcoe:");
                if (! if_directory_chdir(path_name, "device"))
                        return;
                if (NULL == getcwd(wd, sizeof(wd)))
                        return;
                cp = strrchr(wd, '/');
                if (NULL == cp)
                        return;
                *cp = '\0';
                cp = strrchr(wd, '/');
                if (NULL == cp)
                        return;
                *cp = '\0';
                cp = basename(wd);
                snprintf(buff, sizeof(buff), "%s%s", "fc_remote_ports/", cp);
                if (if_directory_chdir(wd, buff)) {
                        if (NULL == getcwd(buff, sizeof(buff)))
                                return;
                } else {  /* newer transport */
                        /* /sys  /class/fc_remote_ports/  rport-x:y-z  / */
                        snprintf(buff, sizeof(buff), "%s%s%s/", sysfsroot,
                                 fc_remote_ports, cp);
                }
                snprintf(b2, sizeof(b2), "%s%s", path_name, "/device/");
                if (get_value(b2, "vendor", value, sizeof(value)))
                        printf("  vendor=%s\n", value);
                if (get_value(b2, "model", value, sizeof(value)))
                        printf("  model=%s\n", value);
                printf("  %s\n",cp);    /* rport */
                if (get_value(buff, "node_name", value, sizeof(value)))
                        printf("  node_name=%s\n", value);
                if (get_value(buff, "port_name", value, sizeof(value)))
                        printf("  port_name=%s\n", value);
                if (get_value(buff, "port_id", value, sizeof(value)))
                        printf("  port_id=%s\n", value);
                if (get_value(buff, "port_state", value, sizeof(value)))
                        printf("  port_state=%s\n", value);
                if (get_value(buff, "roles", value, sizeof(value)))
                        printf("  roles=%s\n", value);
                print_enclosure_device(devname, b2, op);
                if (get_value(buff, "scsi_target_id", value, sizeof(value)))
                        printf("  scsi_target_id=%s\n", value);
                if (get_value(buff, "supported_classes", value,
                              sizeof(value)))
                        printf("  supported_classes=%s\n", value);
                if (get_value(buff, "fast_io_fail_tmo", value, sizeof(value)))
                        printf("  fast_io_fail_tmo=%s\n", value);
                if (get_value(buff, "dev_loss_tmo", value, sizeof(value)))
                        printf("  dev_loss_tmo=%s\n", value);
                if (op->verbose > 2) {
                        printf("  fetched from directory: %s\n", buff);
                        printf("  fetched from directory: %s\n", b2);
                }
                break;
        case TRANSPORT_SAS:
                printf("  transport=sas\n");
                snprintf(buff, sizeof(buff), "%s%s%s", sysfsroot, sas_device,
                         sas_hold_end_device);

                snprintf(b2, sizeof(b2), "%s%s", path_name, "/device/");
                if (get_value(b2, "vendor", value, sizeof(value)))
                        printf("  vendor=%s\n", value);
                if (get_value(b2, "model", value, sizeof(value)))
                        printf("  model=%s\n", value);

                snprintf(b2, sizeof(b2), "%s%s%s", sysfsroot, sas_end_device,
                         sas_hold_end_device);
                if (get_value(buff, "bay_identifier", value, sizeof(value)))
                        printf("  bay_identifier=%s\n", value);
                print_enclosure_device(devname, b2, op);
                if (get_value(buff, "enclosure_identifier", value,
                              sizeof(value)))
                        printf("  enclosure_identifier=%s\n", value);
                if (get_value(buff, "initiator_port_protocols", value,
                              sizeof(value)))
                        printf("  initiator_port_protocols=%s\n", value);
                if (get_value(b2, "initiator_response_timeout", value,
                              sizeof(value)))
                        printf("  initiator_response_timeout=%s\n", value);
                if (get_value(b2, "I_T_nexus_loss_timeout", value,
                              sizeof(value)))
                        printf("  I_T_nexus_loss_timeout=%s\n", value);
                if (get_value(buff, "phy_identifier", value, sizeof(value)))
                        printf("  phy_identifier=%s\n", value);
                if (get_value(b2, "ready_led_meaning", value, sizeof(value)))
                        printf("  ready_led_meaning=%s\n", value);
                if (get_value(buff, "sas_address", value, sizeof(value)))
                        printf("  sas_address=%s\n", value);
                if (get_value(buff, "target_port_protocols", value,
                              sizeof(value)))
                        printf("  target_port_protocols=%s\n", value);
                if (get_value(b2, "tlr_enabled", value, sizeof(value)))
                        printf("  tlr_enabled=%s\n", value);
                if (get_value(b2, "tlr_supported", value, sizeof(value)))
                        printf("  tlr_supported=%s\n", value);
                if (op->verbose > 2) {
                        printf("fetched from directory: %s\n", buff);
                        printf("fetched from directory: %s\n", b2);
                }
                break;
        case TRANSPORT_SAS_CLASS:
                printf("  transport=sas\n");
                printf("  sub_transport=sas_class\n");
                snprintf(buff, sizeof(buff), "%s%s", path_name,
                         "/device/sas_device");
                if (get_value(buff, "device_name", value, sizeof(value)))
                        printf("  device_name=%s\n", value);
                if (get_value(buff, "dev_type", value, sizeof(value)))
                        printf("  dev_type=%s\n", value);
                if (get_value(buff, "iproto", value, sizeof(value)))
                        printf("  iproto=%s\n", value);
                if (get_value(buff, "iresp_timeout", value, sizeof(value)))
                        printf("  iresp_timeout=%s\n", value);
                if (get_value(buff, "itnl_timeout", value, sizeof(value)))
                        printf("  itnl_timeout=%s\n", value);
                if (get_value(buff, "linkrate", value, sizeof(value)))
                        printf("  linkrate=%s\n", value);
                if (get_value(buff, "max_linkrate", value, sizeof(value)))
                        printf("  max_linkrate=%s\n", value);
                if (get_value(buff, "max_pathways", value, sizeof(value)))
                        printf("  max_pathways=%s\n", value);
                if (get_value(buff, "min_linkrate", value, sizeof(value)))
                        printf("  min_linkrate=%s\n", value);
                if (get_value(buff, "pathways", value, sizeof(value)))
                        printf("  pathways=%s\n", value);
                if (get_value(buff, "ready_led_meaning", value,
                              sizeof(value)))
                        printf("  ready_led_meaning=%s\n", value);
                if (get_value(buff, "rl_wlun", value, sizeof(value)))
                        printf("  rl_wlun=%s\n", value);
                if (get_value(buff, "sas_addr", value, sizeof(value)))
                        printf("  sas_addr=%s\n", value);
                if (get_value(buff, "tproto", value, sizeof(value)))
                        printf("  tproto=%s\n", value);
                if (get_value(buff, "transport_layer_retries", value,
                              sizeof(value)))
                        printf("  transport_layer_retries=%s\n", value);
                if (op->verbose > 2)
                        printf("fetched from directory: %s\n", buff);
                break;
        case TRANSPORT_ISCSI:
                printf("  transport=iSCSI\n");
                snprintf(buff, sizeof(buff), "%s%ssession%d", sysfsroot,
                         iscsi_session, iscsi_tsession_num);
                if (get_value(buff, "targetname", value, sizeof(value)))
                        printf("  targetname=%s\n", value);
                if (get_value(buff, "tpgt", value, sizeof(value)))
                        printf("  tpgt=%s\n", value);
                if (get_value(buff, "data_pdu_in_order", value,
                              sizeof(value)))
                        printf("  data_pdu_in_order=%s\n", value);
                if (get_value(buff, "data_seq_in_order", value,
                              sizeof(value)))
                        printf("  data_seq_in_order=%s\n", value);
                if (get_value(buff, "erl", value, sizeof(value)))
                        printf("  erl=%s\n", value);
                if (get_value(buff, "first_burst_len", value, sizeof(value)))
                        printf("  first_burst_len=%s\n", value);
                if (get_value(buff, "initial_r2t", value, sizeof(value)))
                        printf("  initial_r2t=%s\n", value);
                if (get_value(buff, "max_burst_len", value, sizeof(value)))
                        printf("  max_burst_len=%s\n", value);
                if (get_value(buff, "max_outstanding_r2t", value,
                              sizeof(value)))
                        printf("  max_outstanding_r2t=%s\n", value);
                if (get_value(buff, "recovery_tmo", value, sizeof(value)))
                        printf("  recovery_tmo=%s\n", value);
// >>>       Would like to see what are readable attributes in this directory.
//           Ignoring connections for the time being. Could add with an entry
//           for connection=<n> with normal two space indent followed by
//           attributes for that connection indented 4 spaces
                if (op->verbose > 2)
                        printf("fetched from directory: %s\n", buff);
                break;
        case TRANSPORT_SBP:
                printf("  transport=sbp\n");
                if (! if_directory_chdir(path_name, "device"))
                        return;
                if (NULL == getcwd(wd, sizeof(wd)))
                        return;
                if (get_value(wd, "ieee1394_id", value, sizeof(value)))
                        printf("  ieee1394_id=%s\n", value);
                if (op->verbose > 2)
                        printf("fetched from directory: %s\n", buff);
                break;
        case TRANSPORT_USB:
                printf("  transport=usb\n");
                printf("  device_name=%s\n", get_usb_devname(NULL, devname,
                       value, sizeof(value)));
                break;
        case TRANSPORT_ATA:
                printf("  transport=ata\n");
                break;
        case TRANSPORT_SATA:
                printf("  transport=sata\n");
                break;
        default:
                if (op->verbose > 1)
                        fprintf(stderr, "No transport information\n");
                break;
        }
}

static void
longer_d_entry(const char * path_name, const char * devname,
               const struct lsscsi_opt_coll * op)
{
        char value[LMAX_NAME];

        if (op->transport > 0) {
                transport_tport_longer(devname, op);
                return;
        }
        if (op->long_opt >= 3) {
                if (get_value(path_name, "device_blocked", value,
                              sizeof(value)))
                        printf("  device_blocked=%s\n", value);
                else if (op->verbose > 0)
                        printf("  device_blocked=?\n");
                if (get_value(path_name, "iocounterbits", value,
                              sizeof(value)))
                        printf("  iocounterbits=%s\n", value);
                else if (op->verbose > 0)
                        printf("  iocounterbits=?\n");
                if (get_value(path_name, "iodone_cnt", value, sizeof(value)))
                        printf("  iodone_cnt=%s\n", value);
                else if (op->verbose > 0)
                        printf("  iodone_cnt=?\n");
                if (get_value(path_name, "ioerr_cnt", value, sizeof(value)))
                        printf("  ioerr_cnt=%s\n", value);
                else if (op->verbose > 0)
                        printf("  ioerr_cnt=?\n");
                if (get_value(path_name, "iorequest_cnt", value,
                              sizeof(value)))
                        printf("  iorequest_cnt=%s\n", value);
                else if (op->verbose > 0)
                        printf("  iorequest_cnt=?\n");
                if (get_value(path_name, "queue_depth", value,
                              sizeof(value)))
                        printf("  queue_depth=%s\n", value);
                else if (op->verbose > 0)
                        printf("  queue_depth=?\n");
                if (get_value(path_name, "queue_type", value,
                              sizeof(value)))
                        printf("  queue_type=%s\n", value);
                else if (op->verbose > 0)
                        printf("  queue_type=?\n");
                if (get_value(path_name, "scsi_level", value,
                              sizeof(value)))
                        printf("  scsi_level=%s\n", value);
                else if (op->verbose > 0)
                        printf("  scsi_level=?\n");
                if (get_value(path_name, "state", value,
                              sizeof(value)))
                        printf("  state=%s\n", value);
                else if (op->verbose > 0)
                        printf("  state=?\n");
                if (get_value(path_name, "timeout", value,
                              sizeof(value)))
                        printf("  timeout=%s\n", value);
                else if (op->verbose > 0)
                        printf("  timeout=?\n");
                if (get_value(path_name, "type", value,
                              sizeof(value)))
                        printf("  type=%s\n", value);
                else if (op->verbose > 0)
                        printf("  type=?\n");
                return;
        }

        if (get_value(path_name, "state", value, sizeof(value)))
                printf("  state=%s", value);
        else
                printf("  state=?");
        if (get_value(path_name, "queue_depth", value, sizeof(value)))
                printf(" queue_depth=%s", value);
        else
                printf(" queue_depth=?");
        if (get_value(path_name, "scsi_level", value, sizeof(value)))
                printf(" scsi_level=%s", value);
        else
                printf(" scsi_level=?");
        if (get_value(path_name, "type", value, sizeof(value)))
                printf(" type=%s", value);
        else
                printf(" type=?");
        if (get_value(path_name, "device_blocked", value, sizeof(value)))
                printf(" device_blocked=%s", value);
        else
                printf(" device_blocked=?");
        if (get_value(path_name, "timeout", value, sizeof(value)))
                printf(" timeout=%s", value);
        else
                printf(" timeout=?");
        printf("\n");
        if (op->long_opt == 2) {
                if (get_value(path_name, "iocounterbits", value,
                              sizeof(value)))
                        printf("  iocounterbits=%s", value);
                else
                        printf("  iocounterbits=?");
                if (get_value(path_name, "iodone_cnt", value,
                               sizeof(value)))
                        printf(" iodone_cnt=%s", value);
                else
                        printf(" iodone_cnt=?");
                if (get_value(path_name, "ioerr_cnt", value,
                               sizeof(value)))
                        printf(" ioerr_cnt=%s", value);
                else
                        printf(" ioerr_cnt=?");
                if (get_value(path_name, "iorequest_cnt", value,
                               sizeof(value)))
                        printf(" iorequest_cnt=%s", value);
                else
                        printf(" iorequest_cnt=?");
                printf("\n");
                if (get_value(path_name, "queue_type", value,
                               sizeof(value)))
                        printf("  queue_type=%s", value);
                else
                        printf("  queue_type=?");
                printf("\n");
        }
}

static void
one_classic_sdev_entry(const char * dir_name, const char * devname,
                       const struct lsscsi_opt_coll * op)
{
        struct addr_hctl hctl;
        char buff[LMAX_DEVPATH];
        char wd[LMAX_PATH];
        char dev_node[LMAX_NAME];
        char value[LMAX_NAME];
        int type, scsi_level;

        snprintf(buff, sizeof(buff), "%s/%s", dir_name, devname);
        if (! parse_colon_list(devname, &hctl))
                invalidate_hctl(&hctl);
        printf("Host: scsi%d Channel: %02d Target: %02d Lun: %02" PRIu64 "\n",
               hctl.h, hctl.c, hctl.t, hctl.l);

        if (get_value(buff, "vendor", value, sizeof(value)))
                printf("  Vendor: %-8s", value);
        else
                printf("  Vendor: ?       ");
        if (get_value(buff, "model", value, sizeof(value)))
                printf(" Model: %-16s", value);
        else
                printf(" Model: ?               ");
        if (get_value(buff, "rev", value, sizeof(value)))
                printf(" Rev: %-4s", value);
        else
                printf(" Rev: ?   ");
        printf("\n");
        if (! get_value(buff, "type", value, sizeof(value))) {
                printf("  Type:   %-33s", "?");
        } else if (1 != sscanf(value, "%d", &type)) {
                printf("  Type:   %-33s", "??");
        } else if ((type < 0) || (type > 31)) {
                printf("  Type:   %-33s", "???");
        } else
                printf("  Type:   %-33s", scsi_device_types[type]);
        if (! get_value(buff, "scsi_level", value, sizeof(value))) {
                printf("ANSI SCSI revision: ?\n");
        } else if (1 != sscanf(value, "%d", &scsi_level)) {
                printf("ANSI SCSI revision: ??\n");
        } else
                printf("ANSI SCSI revision: %02x\n", (scsi_level - 1) ?
                                            scsi_level - 1 : 1);
        if (op->generic) {
                if (if_directory_ch2generic(buff)) {
                        if (NULL == getcwd(wd, sizeof(wd)))
                                printf("generic_dev error\n");
                        else {
                                if (op->kname)
                                        snprintf(dev_node, sizeof(dev_node),
                                                 "%s/%s", dev_dir,
                                                 basename(wd));
                                else if (! get_dev_node(wd, dev_node,
                                                        CHR_DEV))
                                        snprintf(dev_node, sizeof(dev_node),
                                                 "-");
                                printf("%s\n", dev_node);
                        }
                }
                else
                        printf("-\n");
        }
        if (op->long_opt > 0)
                longer_d_entry(buff, devname, op);
        if (op->verbose)
                printf("  dir: %s\n", buff);
}

static void
tag_lun_helper(int * tag_arr, int kk, int num)
{
        int j;

        for (j = 0; j < num; ++j)
                tag_arr[(2 * kk) + j] = ((kk > 0) && (0 == j)) ? 2 : 1;
}

/* Tag lun bytes according to SAM-5 rev 10. Write output to tag_arr assumed
 * to have at least 8 ints. 0 in tag_arr means this position and higher can
 * be ignored; 1 means print as is; 2 means print with separator
 * prefixed. Example: lunp: 01 22 00 33 00 00 00 00 generates tag_arr
 * of 1, 1, 2, 1, 0 ... 0 and might be printed as 0x0122_0033 . */
static void
tag_lun(const unsigned char * lunp, int * tag_arr)
{
        int k, a_method, bus_id, len_fld, e_a_method, next_level;
        unsigned char not_spec[2] = {0xff, 0xff};

        if (NULL == tag_arr)
                return;
        for (k = 0; k < 8; ++k)
                tag_arr[k] = 0;
        if (NULL == lunp)
                return;
        if (0 == memcmp(lunp, not_spec, sizeof(not_spec))) {
                for (k = 0; k < 2; ++k)
                        tag_arr[k] = 1;
                return;
        }
        for (k = 0; k < 4; ++k, lunp += 2) {
                next_level = 0;
                a_method = (lunp[0] >> 6) & 0x3;
                switch (a_method) {
                case 0:         /* peripheral device addressing method */
                        bus_id = lunp[0] & 0x3f;
                        if (bus_id)
                            next_level = 1;
                        tag_lun_helper(tag_arr, k, 2);
                        break;
                case 1:         /* flat space addressing method */
                        tag_lun_helper(tag_arr, k, 2);
                        break;
                case 2:         /* logical unit addressing method */
                        tag_lun_helper(tag_arr, k, 2);
                        break;
                case 3:         /* extended logical unit addressing method */
                        len_fld = (lunp[0] & 0x30) >> 4;
                        e_a_method = lunp[0] & 0xf;
                        if ((0 == len_fld) && (1 == e_a_method))
                                tag_lun_helper(tag_arr, k, 2);
                        else if ((1 == len_fld) && (2 == e_a_method))
                                tag_lun_helper(tag_arr, k, 4);
                        else if ((2 == len_fld) && (2 == e_a_method))
                                tag_lun_helper(tag_arr, k, 6);
                        else if ((3 == len_fld) && (0xf == e_a_method))
                                tag_arr[2 * k] = (k > 0) ? 2 : 1;
                        else {
                                if (len_fld < 2)
                                        tag_lun_helper(tag_arr, k, 4);
                                else {
                                        tag_lun_helper(tag_arr, k, 6);
                                        if (3 == len_fld) {
                                                tag_arr[(2 * k) + 6] = 1;
                                                tag_arr[(2 * k) + 7] = 1;
                                        }
                                }
                        }
                        break;
                default:
                        tag_lun_helper(tag_arr, k, 2);
                        break;
                }
                if (! next_level)
                        break;
        }
}

static uint64_t
lun_word_flip(uint64_t in)
{
        uint64_t res = 0;
        int k;

        for (k = 0; ; ++k) {
                res |= (in & 0xffff);
                if (k > 2)
                        break;
                res <<= 16;
                in >>= 16;
        }
        return res;
}

/* List one SCSI device (LU) on a line. */
static void
one_sdev_entry(const char * dir_name, const char * devname,
               const struct lsscsi_opt_coll * op)
{
        char buff[LMAX_DEVPATH];
        char wd[LMAX_PATH];
        char extra[LMAX_DEVPATH];
        char value[LMAX_NAME];
        int type, k, n, len, ta;
        int devname_len = 13;
        int get_wwn = 0;
        struct addr_hctl hctl;
        int tag_arr[16];

        if (op->classic) {
                one_classic_sdev_entry(dir_name, devname, op);
                return;
        }
        len = sizeof(value);
        snprintf(buff, sizeof(buff), "%s/%s", dir_name, devname);
        if (op->lunhex && parse_colon_list(devname, &hctl)) {
                snprintf(value, len, "[%d:%d:%d:0x",
                         hctl.h, hctl.c, hctl.t);
                if (1 == op->lunhex) {
                        tag_lun(hctl.lun_arr, tag_arr);
                        for (k = 0; k < 8; ++k) {
                                ta = tag_arr[k];
                                if (ta <= 0)
                                        break;
                                n = strlen(value);
                                snprintf(value + n, len - n, "%s%02x",
                                         ((ta > 1) ? "_" : ""),
                                         hctl.lun_arr[k]);
                        }
                        n = strlen(value);
                        snprintf(value + n, len - n, "]");
                } else {
                        n = strlen(value);
                        snprintf(value + n, len - n, "%016" PRIx64 "]",
                                 lun_word_flip(hctl.l));
                }
                devname_len = 28;
        } else
                snprintf(value, sizeof(value), "[%s]", devname);

        printf("%-*s", devname_len, value);
        if (! get_value(buff, "type", value, sizeof(value))) {
                printf("type?   ");
        } else if (1 != sscanf(value, "%d", &type)) {
                printf("type??  ");
        } else if ((type < 0) || (type > 31)) {
                printf("type??? ");
        } else
                printf("%s ", scsi_short_device_types[type]);

        if (op->wwn)
                ++get_wwn;
        else if (0 == op->transport) {
                if (get_value(buff, "vendor", value, sizeof(value)))
                        printf("%-8s ", value);
                else
                        printf("vendor?  ");

                if (get_value(buff, "model", value, sizeof(value)))
                        printf("%-16s ", value);
                else
                        printf("model?           ");

                if (get_value(buff, "rev", value, sizeof(value)))
                        printf("%-4s  ", value);
                else
                        printf("rev?  ");
        } else {
                if (transport_tport(devname, /* op, */
                                    sizeof(value), value))
                        printf("%-30s  ", value);
                else
                        printf("                                ");
        }

        if (1 == non_sg_scan(buff, op)) {
                if (DT_DIR == non_sg.d_type) {
                        snprintf(wd, sizeof(wd), "%s/%s", buff, non_sg.name);
                        if (1 == scan_for_first(wd, op))
                                my_strcopy(extra, aa_first.name,
                                           sizeof(extra));
                        else {
                                printf("unexpected scan_for_first error");
                                wd[0] = '\0';
                        }
                } else {
                        my_strcopy(wd, buff, sizeof(wd));
                        my_strcopy(extra, non_sg.name, sizeof(extra));
                }
                if (wd[0] && (if_directory_chdir(wd, extra))) {
                        if (NULL == getcwd(wd, sizeof(wd))) {
                                printf("getcwd error");
                                wd[0] = '\0';
                        }
                }
                if (wd[0]) {
                        char dev_node[LMAX_NAME] = "";
                        char wwn_str[34];
                        enum dev_type typ;

                        typ = (FT_BLOCK == non_sg.ft) ? BLK_DEV : CHR_DEV;
                        if (get_wwn) {
                                if ((BLK_DEV == typ) &&
                                    get_disk_wwn(wd, wwn_str, sizeof(wwn_str)))
                                        printf("%-30s  ", wwn_str);
                                else
                                        printf("                          "
                                               "      ");

                        }
                        if (op->kname)
                                snprintf(dev_node, sizeof(dev_node), "%s/%s",
                                        dev_dir, basename(wd));
                        else if (! get_dev_node(wd, dev_node, typ))
                                snprintf(dev_node, sizeof(dev_node),
                                         "-       ");

                        printf("%-9s", dev_node);
                        if (op->dev_maj_min) {
                                if (get_value(wd, "dev", value,
                                              sizeof(value)))
                                        printf("[%s]", value);
                                else
                                        printf("[dev?]");
                        }

                        if (op->scsi_id) {
                                char *scsi_id;

                                scsi_id = get_disk_scsi_id(dev_node);
                                printf("  %s", scsi_id ? scsi_id : "-");
                                free(scsi_id);
                        }
                }
        } else {
                if (get_wwn)
                        printf("                                ");
                if (op->scsi_id)
                        printf("%-9s  -", "-");
                else
                        printf("%-9s", "-");
        }

        if (op->generic) {
                if (if_directory_ch2generic(buff)) {
                        if (NULL == getcwd(wd, sizeof(wd)))
                                printf("  generic_dev error");
                        else {
                                char dev_node[LMAX_NAME] = "";

                                if (op->kname)
                                        snprintf(dev_node, sizeof(dev_node),
                                                 "%s/%s", dev_dir,
                                                 basename(wd));
                                else if (! get_dev_node(wd, dev_node,
                                                        CHR_DEV))
                                        snprintf(dev_node, sizeof(dev_node),
                                                 "-");
                                printf("  %-9s", dev_node);
                                if (op->dev_maj_min) {
                                        if (get_value(wd, "dev", value,
                                                      sizeof(value)))
                                                printf("[%s]", value);
                                        else
                                                printf("[dev?]");
                                }
                        }
                }
                else
                        printf("  %-9s", "-");
        }

        if (op->protection) {
                char sddir[LMAX_DEVPATH];
                char blkdir[LMAX_DEVPATH];

                my_strcopy(sddir,  buff, sizeof(sddir));
                my_strcopy(blkdir, buff, sizeof(blkdir));

                if (sd_scan(sddir) &&
                    if_directory_chdir(sddir, ".") &&
                    get_value(".", "protection_type", value, sizeof(value))) {

                        if (!strncmp(value, "0", 1))
                                printf("  %-9s", "-");
                        else
                                printf("  DIF/Type%1s", value);

                } else
                        printf("  %-9s", "-");

                if (block_scan(blkdir) &&
                    if_directory_chdir(blkdir, "integrity") &&
                    get_value(".", "format", value, sizeof(value)))
                        printf("  %-16s", value);
                else
                        printf("  %-16s", "-");
        }

        if (op->protmode) {
                char sddir[LMAX_DEVPATH];

                my_strcopy(sddir, buff, sizeof(sddir));

                if (sd_scan(sddir) &&
                    if_directory_chdir(sddir, ".") &&
                    get_value(sddir, "protection_mode", value,
                              sizeof(value))) {

                        if (!strcmp(value, "none"))
                                printf("  %-4s", "-");
                        else
                                printf("  %-4s", value);
                } else
                        printf("  %-4s", "-");
        }

        if (op->size) {
                char blkdir[LMAX_DEVPATH];

                my_strcopy(blkdir, buff, sizeof(blkdir));

                value[0] = 0;
                if (type == 0 &&
                    block_scan(blkdir) &&
                    if_directory_chdir(blkdir, ".") &&
                    get_value(".", "size", value, sizeof(value))) {
                        uint64_t blocks = atoll(value);

                        blocks <<= 9;
                        if (blocks > 0 &&
                            !string_get_size(blocks, STRING_UNITS_10, value,
                                             sizeof(value))) {
                                printf("  %6s", value);
                        } else
                                printf("  %6s", "-");
                } else
                        printf("  %6s", "-");
        }

        printf("\n");
        if (op->long_opt > 0)
                longer_d_entry(buff, devname, op);
        if (op->verbose > 0) {
                printf("  dir: %s  [", buff);
                if (if_directory_chdir(buff, "")) {
                        if (NULL == getcwd(wd, sizeof(wd)))
                                printf("?");
                        else
                                printf("%s", wd);
                }
                printf("]\n");
        }
}

static int
sdev_scandir_select(const struct dirent * s)
{
/* Following no longer needed but leave for early lk 2.6 series */
        if (strstr(s->d_name, "mt"))
                return 0;       /* st auxiliary device names */
        if (strstr(s->d_name, "ot"))
                return 0;       /* osst auxiliary device names */
        if (strstr(s->d_name, "gen"))
                return 0;
/* Above no longer needed but leave for early lk 2.6 series */
        if (!strncmp(s->d_name, "host", 4)) /* SCSI host */
                return 0;
        if (!strncmp(s->d_name, "target", 6)) /* SCSI target */
                return 0;
        if (strchr(s->d_name, ':')) {
                if (filter_active) {
                        struct addr_hctl s_hctl;

                        if (! parse_colon_list(s->d_name, &s_hctl)) {
                                fprintf(stderr, "sdev_scandir_select: parse "
                                        "failed\n");
                                return 0;
                        }
                        if (((-1 == filter.h) || (s_hctl.h == filter.h)) &&
                            ((-1 == filter.c) || (s_hctl.c == filter.c)) &&
                            ((-1 == filter.t) || (s_hctl.t == filter.t)) &&
                            (((uint64_t)~0 == filter.l) ||
                             (s_hctl.l == filter.l)))
                                return 1;
                        else
                                return 0;
                } else
                        return 1;
        }
        /* Still need to filter out "." and ".." */
        return 0;
}

/* Returns -1 if (a->d_name < b->d_name) ; 0 if they are equal
 * and 1 otherwise.
 * Function signature was more generic before version 0.23 :
 * static int sdev_scandir_sort(const void * a, const void * b)
 */
static int
sdev_scandir_sort(const struct dirent ** a, const struct dirent ** b)
{
        const char * lnam = (*a)->d_name;
        const char * rnam = (*b)->d_name;
        struct addr_hctl left_hctl;
        struct addr_hctl right_hctl;

        if (! parse_colon_list(lnam, &left_hctl)) {
                fprintf(stderr, "sdev_scandir_sort: left parse failed\n");
                return -1;
        }
        if (! parse_colon_list(rnam, &right_hctl)) {
                fprintf(stderr, "sdev_scandir_sort: right parse failed\n");
                return 1;
        }
        return cmp_hctl(&left_hctl, &right_hctl);
}

/* List SCSI devices (LUs). */
static void
list_sdevices(const struct lsscsi_opt_coll * op)
{
        char buff[LMAX_DEVPATH];
        char name[LMAX_NAME];
        struct dirent ** namelist;
        int num, k;

        snprintf(buff, sizeof(buff), "%s%s", sysfsroot, bus_scsi_devs);

        num = scandir(buff, &namelist, sdev_scandir_select,
                      sdev_scandir_sort);
        if (num < 0) {  /* scsi mid level may not be loaded */
                if (op->verbose > 0) {
                        snprintf(name, sizeof(name), "scandir: %s", buff);
                        perror(name);
                        printf("SCSI mid level module may not be loaded\n");
                }
                if (op->classic)
                        printf("Attached devices: none\n");
                return;
        }
        if (op->classic)
                printf("Attached devices: %s\n", (num ? "" : "none"));

        for (k = 0; k < num; ++k) {
                my_strcopy(name, namelist[k]->d_name, sizeof(name));
                transport_id = TRANSPORT_UNKNOWN;
                one_sdev_entry(buff, name, op);
                free(namelist[k]);
        }
        free(namelist);
        if (op->wwn)
                free_disk_wwn_node_list();
}

/* List host (initiator) attributes when --long given (one or more times). */
static void
longer_h_entry(const char * path_name, const struct lsscsi_opt_coll * op)
{
        char value[LMAX_NAME];

        if (op->transport > 0) {
                transport_init_longer(path_name, op);
                return;
        }
        if (op->long_opt >= 3) {
                if (get_value(path_name, "can_queue", value, sizeof(value)))
                        printf("  can_queue=%s\n", value);
                else if (op->verbose)
                        printf("  can_queue=?\n");
                if (get_value(path_name, "cmd_per_lun", value, sizeof(value)))
                        printf("  cmd_per_lun=%s\n", value);
                else if (op->verbose)
                        printf("  cmd_per_lun=?\n");
                if (get_value(path_name, "host_busy", value, sizeof(value)))
                        printf("  host_busy=%s\n", value);
                else if (op->verbose)
                        printf("  host_busy=?\n");
                if (get_value(path_name, "sg_tablesize", value,
                              sizeof(value)))
                        printf("  sg_tablesize=%s\n", value);
                else if (op->verbose)
                        printf("  sg_tablesize=?\n");
                if (get_value(path_name, "state", value, sizeof(value)))
                        printf("  state=%s\n", value);
                else if (op->verbose)
                        printf("  state=?\n");
                if (get_value(path_name, "unchecked_isa_dma", value,
                              sizeof(value)))
                        printf("  unchecked_isa_dma=%s\n", value);
                else if (op->verbose)
                        printf("  unchecked_isa_dma=?\n");
                if (get_value(path_name, "unique_id", value, sizeof(value)))
                        printf("  unique_id=%s\n", value);
                else if (op->verbose)
                        printf("  unique_id=?\n");
        } else if (op->long_opt > 0) {
                if (get_value(path_name, "cmd_per_lun", value, sizeof(value)))
                        printf("  cmd_per_lun=%-4s ", value);
                else
                        printf("  cmd_per_lun=???? ");

                if (get_value(path_name, "host_busy", value, sizeof(value)))
                        printf("host_busy=%-4s ", value);
                else
                        printf("host_busy=???? ");

                if (get_value(path_name, "sg_tablesize", value,
                              sizeof(value)))
                        printf("sg_tablesize=%-4s ", value);
                else
                        printf("sg_tablesize=???? ");

                if (get_value(path_name, "unchecked_isa_dma", value,
                              sizeof(value)))
                        printf("unchecked_isa_dma=%-2s ", value);
                else
                        printf("unchecked_isa_dma=?? ");
                printf("\n");
                if (2 == op->long_opt) {
                        if (get_value(path_name, "can_queue", value,
                                      sizeof(value)))
                                printf("  can_queue=%-4s ", value);
                        if (get_value(path_name, "state", value,
                                      sizeof(value)))
                                printf("  state=%-8s ", value);
                        if (get_value(path_name, "unique_id", value,
                                      sizeof(value)))
                                printf("  unique_id=%-2s ", value);
                        printf("\n");
                }
        }
}

static void
one_host_entry(const char * dir_name, const char * devname,
               const struct lsscsi_opt_coll * op)
{
        char buff[LMAX_DEVPATH];
        char value[LMAX_NAME];
        char wd[LMAX_PATH];
        const char * nullname1 = "<NULL>";
        const char * nullname2 = "(null)";
        unsigned int host_id;

        if (op->classic) {
                // one_classic_host_entry(dir_name, devname, op);
                printf("  <'--classic' not supported for hosts>\n");
                return;
        }
        if (1 == sscanf(devname, "host%u", &host_id))
                printf("[%u]  ", host_id);
        else
                printf("[?]  ");
        snprintf(buff, sizeof(buff), "%s/%s", dir_name, devname);
        if ((get_value(buff, "proc_name", value, sizeof(value))) &&
            (strncmp(value, nullname1, 6)) && (strncmp(value, nullname2, 6)))
                printf("  %-12s  ", value);
        else if (if_directory_chdir(buff, "device/../driver")) {
                if (NULL == getcwd(wd, sizeof(wd)))
                        printf("  %-12s  ", nullname2);
                else
                        printf("  %-12s  ", basename(wd));

        } else
                printf("  proc_name=????  ");
        if (op->transport > 0) {
                if (transport_init(devname, /* op, */ sizeof(value), value))
                        printf("%s\n", value);
                else
                        printf("\n");
        } else
                printf("\n");

        if (op->long_opt > 0)
                longer_h_entry(buff, op);

        if (op->verbose > 0) {
                printf("  dir: %s\n  device dir: ", buff);
                if (if_directory_chdir(buff, "device")) {
                        if (NULL == getcwd(wd, sizeof(wd)))
                                printf("?");
                        else
                                printf("%s", wd);
                }
                printf("\n");
        }
}

static int
host_scandir_select(const struct dirent * s)
{
        int h;

        if (0 == strncmp("host", s->d_name, 4)) {
                if (filter_active) {
                        if (-1 == filter.h)
                                return 1;
                        else if ((1 == sscanf(s->d_name + 4, "%d", &h) &&
                                 (h == filter.h)))
                                return 1;
                        else
                                return 0;
                } else
                        return 1;
        }
        return 0;
}

/* Returns -1 if (a->d_name < b->d_name) ; 0 if they are equal
 * and 1 otherwise.
 * Function signature was more generic before version 0.23 :
 * static int host_scandir_sort(const void * a, const void * b)
 */
static int
host_scandir_sort(const struct dirent ** a, const struct dirent ** b)
{
        const char * lnam = (*a)->d_name;
        const char * rnam = (*b)->d_name;
        unsigned int l, r;

        if (1 != sscanf(lnam, "host%u", &l))
                return -1;
        if (1 != sscanf(rnam, "host%u", &r))
                return 1;
        if (l < r)
                return -1;
        else if (r < l)
                return 1;
        return 0;
}

static void
list_hosts(const struct lsscsi_opt_coll * op)
{
        char buff[LMAX_DEVPATH];
        char name[LMAX_NAME];
        struct dirent ** namelist;
        int num, k;

        snprintf(buff, sizeof(buff), "%s%s", sysfsroot, scsi_host);

        num = scandir(buff, &namelist, host_scandir_select,
                      host_scandir_sort);
        if (num < 0) {
                snprintf(name, sizeof(name), "scandir: %s", buff);
                perror(name);
                return;
        }
        if (op->classic)
                printf("Attached hosts: %s\n", (num ? "" : "none"));

        for (k = 0; k < num; ++k) {
                my_strcopy(name, namelist[k]->d_name, sizeof(name));
                transport_id = TRANSPORT_UNKNOWN;
                one_host_entry(buff, name, op);
                free(namelist[k]);
        }
        free(namelist);
}

/* Return 0 if able to decode, otherwise 1 */
static int
one_filter_arg(const char * arg, struct addr_hctl * filtp)
{
        const char * cp;
        const char * cpe;
        char buff[64];
        int val, k, n, res;
        uint64_t val64;

        cp = arg;
        while ((*cp == ' ') || (*cp == '\t') || (*cp == '['))
                ++cp;
        if ('\0' == *cp)
                return 0;
        for (k = 0; *cp; cp = cpe + 1, ++k) {
                cpe = strchr(cp, ':');
                if (cpe)
                        n = cpe - cp;
                else {
                        n = strlen(cp);
                        cpe = cp + n - 1;
                }
                val = -1;
                val64 = (uint64_t)~0;
                if (n > ((int)sizeof(buff) - 1)) {
                        fprintf(stderr, "intermediate sting in %s too long "
                                "(n=%d)\n", arg, n);
                        return 1;
                }
                if ((n > 0) && ('-' != *cp) && ('*' != *cp) && ('?' != *cp)) {
                        memcpy(buff, cp, n);
                        buff[n] = '\0';
                        if (3 == k) {
                                if (('0' == buff[0]) &&
                                    ('X' == toupper(buff[1])))
                                        res = sscanf(buff, "%" SCNx64 ,
                                                     &val64);
                                else
                                        res = sscanf(buff, "%" SCNu64 ,
                                                     &val64);
                        } else
                                res = sscanf(buff, "%d", &val);
                        if ((1 != res) && (NULL == strchr(buff, ']'))) {
                                        ;
                                fprintf(stderr, "cannot decode %s as an "
                                        "integer\n", buff);
                                return 1;
                        }
                }
                switch (k) {
                case 0: filtp->h = val; break;
                case 1: filtp->c = val; break;
                case 2: filtp->t = val; break;
                case 3: filtp->l = val64; break;
                default:
                        fprintf(stderr, "expect three colons at most in %s\n",
                                arg);
                        return 1;
                }
        }
        return 0;
}

/* Return 0 if able to decode, otherwise 1 */
static int
decode_filter_arg(const char * a1p, const char * a2p, const char * a3p,
                  const char * a4p, struct addr_hctl * filtp)
{
        char b1[256];
        char * b1p;
        int n, rem;

        if ((NULL == a1p) || (NULL == filtp)) {
                fprintf(stderr, "bad call to decode_filter\n");
                return 1;
        }
        filtp->h = -1;
        filtp->c = -1;
        filtp->t = -1;
        filtp->l = (uint64_t)~0;
        if ((0 == strncmp("host", a1p, 4)) &&
            (1 == sscanf(a1p, "host%d", &n)) && ( n >= 0)) {
                filtp->h = n;
                return 0;
        }
        if ((NULL == a2p) || strchr(a1p, ':'))
                return one_filter_arg(a1p, filtp);
        else {
                rem = sizeof(b1) - 5;
                b1p = b1;
                if ((n = strlen(a1p)) > rem)
                        goto err_out;
                my_strcopy(b1p, a1p, rem);
                b1p += n;
                *b1p++ = ':';
                rem -= (n + 1);
                if ((n = strlen(a2p)) > rem)
                        goto err_out;
                my_strcopy(b1p, a2p, rem);
                if (a3p) {
                        b1p += n;
                        *b1p++ = ':';
                        rem -= (n + 1);
                        if ((n = strlen(a3p)) > rem)
                                goto err_out;
                        my_strcopy(b1p, a3p, rem);
                        if (a4p) {
                                b1p += n;
                                *b1p++ = ':';
                                rem -= (n + 1);
                                if ((n = strlen(a4p)) > rem)
                                        goto err_out;
                                my_strcopy(b1p, a4p, rem);
                        }
                }
                return one_filter_arg(b1, filtp);
        }
err_out:
        fprintf(stderr, "filter arguments exceed internal buffer size "
                "(%d)\n", (int)sizeof(b1));
        return 1;
}


int
main(int argc, char **argv)
{
        int c;
        int do_sdevices = 1;
        int do_hosts = 0;
        struct lsscsi_opt_coll opts;
        const char * cp;

        cp = getenv("LSSCSI_LUNHEX_OPT");
        invalidate_hctl(&filter);
        memset(&opts, 0, sizeof(opts));
        while (1) {
                int option_index = 0;

                c = getopt_long(argc, argv, "cdghHiklLpPstvVwxy:",
                                long_options, &option_index);
                if (c == -1)
                        break;

                switch (c) {
                case 'c':
                        ++opts.classic;
                        break;
                case 'd':
                        ++opts.dev_maj_min;
                        break;
                case 'g':
                        ++opts.generic;
                        break;
                case 'h':
                        usage();
                        return 0;
                case 'H':
                        ++do_hosts;
                        break;
                case 'i':
                        ++opts.scsi_id;
                        break;
                case 'k':
                        ++opts.kname;
                        break;
                case 'l':
                        ++opts.long_opt;
                        break;
                case 'L':
                        opts.long_opt += 3;
                        break;
                case 'p':
                        ++opts.protection;
                        break;
                case 'P':
                        ++opts.protmode;
                        break;
                case 's':
                        ++opts.size;
                        break;
                case 't':
                        ++opts.transport;
                        break;
                case 'v':
                        ++opts.verbose;
                        break;
                case 'V':
                        fprintf(stderr, "version: %s\n", version_str);
                        return 0;
                case 'w':
                        ++opts.wwn;
                        break;
                case 'x':
                        ++opts.lunhex;
                        break;
                case 'y':       /* sysfsroot <dir> */
                        sysfsroot = optarg;
                        break;
                case '?':
                        usage();
                        return 1;
                default:
                        fprintf(stderr, "?? getopt returned character "
                                "code 0x%x ??\n", c);
                        usage();
                        return 1;
               }
        }

        if (optind < argc) {
                const char * a1p = NULL;
                const char * a2p = NULL;
                const char * a3p = NULL;
                const char * a4p = NULL;

                if ((optind + 4) < argc) {
                        fprintf(stderr, "unexpected non-option arguments: ");
                        while (optind < argc)
                                fprintf(stderr, "%s ", argv[optind++]);
                        fprintf(stderr, "\n");
                        return 1;
                }
                a1p = argv[optind++];
                if (optind < argc) {
                        a2p = argv[optind++];
                        if (optind < argc) {
                                a3p = argv[optind++];
                                if (optind < argc)
                                        a4p = argv[optind++];
                        }
                }
                if (decode_filter_arg(a1p, a2p, a3p, a4p, &filter))
                        return 1;
                if ((filter.h != -1) || (filter.c != -1) ||
                    (filter.t != -1) || (filter.l != (uint64_t)~0))
                        filter_active = 1;
        }
        if ((0 == opts.lunhex) && cp) {
                if (1 == sscanf(cp, "%d", &c))
                        opts.lunhex = c;
        }
        if ((opts.transport > 0) &&
            ((1 == opts.long_opt) || (2 == opts.long_opt))) {
                fprintf(stderr, "please '--list' (rather than '--long') "
                                "with --transport\n");
                return 1;
        }
        if (opts.verbose > 1) {
                printf(" sysfsroot: %s\n", sysfsroot);
        }
        if (do_hosts)
                list_hosts(&opts);
        else if (do_sdevices)
                list_sdevices(&opts);

        free_dev_node_list();

        return 0;
}
