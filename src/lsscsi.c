/* This is a utility program for listing storage devices and hosts (HBAs)
 * that use the SCSI subsystems in the Linux operating system. It is
 * applicable to kernel versions 2.6.1 and greater. In lsscsi version 0.30
 * support was added to additionally list NVMe devices and controllers.
 *
 *  Copyright (C) 2003-2018 D. Gilbert
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
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <libgen.h>
#include <sys/sysmacros.h>
#ifndef major
#include <sys/types.h>
#endif
#include <linux/major.h>
#include <linux/limits.h>
#include <time.h>
#define __STDC_FORMAT_MACROS 1
#include <inttypes.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "sg_unaligned.h"


static const char * version_str = "0.30  2018/06/12 [svn: r154]";

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
#define TRANSPORT_SRP 11
#define TRANSPORT_PCIE 12       /* most likely NVMe */

#define NVME_HOST_NUM 0x7fff    /* 32767, high to avoid SCSI host numbers */

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

#define UINT64_LAST ((uint64_t)~0)

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
static const char * srp_host = "/class/srp_host/";
static const char * dev_dir = "/dev";
static const char * dev_disk_byid_dir = "/dev/disk/by-id";
#if (HAVE_NVME && (! IGNORE_NVME))
/* static const char * bus_pci_prefix = "/bus/pci"; */
/* static const char * bus_pcie_devs = "/bus/pci_express/devices"; */
static const char * class_nvme = "/class/nvme/";
/* static const char * class_nvme_subsys = "/class/nvme-subsystem/"; */
#endif


/* For SCSI 'h' is host_num, 'c' is channel, 't' is target, 'l' is LUN is
 * uint64_t and lun_arr[8] is LUN as 8 byte array. For NVMe, h=0x7fff
 * (NVME_HOST_NUM) and displayed as 'N'; 'c' is Linux's NVMe controller
 * number, 't' is NVMe Identify controller CTNLID field, and 'l' is
 * namespace id (1 to (2**32)-1) rendered as a little endian 4 byte sequence
 * in lun_arr, last 4 bytes are zeros. invalidate_hctl() puts -1 in
 * integers, 0xff in bytes */
struct addr_hctl {
        int h;                 /* if h==0x7fff, display as 'N' for NVMe */
        int c;
        int t;
        uint64_t l;           /* SCSI: Linux word flipped; NVME: uint32_t */
        uint8_t lun_arr[8];   /* T10, SAM-5 order; NVME: little endian */
};

struct addr_hctl filter;
static bool filter_active = false;

struct lsscsi_opts {
        bool brief;
        bool classic;
        bool dev_maj_min;        /* --device */
        bool generic;
        bool kname;
        bool no_nvme;
        bool pdt;               /* (-D) peripheral device type in hex */
        bool protection;        /* data integrity */
        bool protmode;          /* data integrity */
        bool scsi_id;           /* udev derived from /dev/disk/by-id/scsi* */
        bool transport_info;
        bool wwn;
        int long_opt;           /* --long */
        int lunhex;
        int ssize;              /* show storage size, once->base 10 (e.g. 3 GB
                                 * twice ->base 2 (e.g. 3.1 GiB)
                                 * thrice for number of logical blocks */
        int unit;               /* logical unit (LU) name: from vpd_pg83 */
        int verbose;
};

static void tag_lun(const uint8_t * lunp, int * tag_arr);


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
        "Security manager",
        "Zoned Block",
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
        "adi    ", "sec man", "zbc    ", "(0x15) ", "(0x16) ", "(0x17) ",
        "(0x18) ", "(0x19) ", "(0x1a) ", "(0x1b) ", "(0x1c) ", "(0x1e) ",
        "wlun   ", "no dev ",
};

/* '--name' ('-n') option removed in version 0.11 and can now be reused */
static struct option long_options[] = {
        {"brief", no_argument, 0, 'b'},
        {"classic", no_argument, 0, 'c'},
        {"controllers", no_argument, 0, 'C'},
        {"device", no_argument, 0, 'd'},
        {"generic", no_argument, 0, 'g'},
        {"help", no_argument, 0, 'h'},
        {"hosts", no_argument, 0, 'H'},
        {"kname", no_argument, 0, 'k'},
        {"long", no_argument, 0, 'l'},
        {"list", no_argument, 0, 'L'},
        {"lunhex", no_argument, 0, 'x'},
        {"no-nvme", no_argument, 0, 'N'},       /* allow both '-' and '_' */
        {"no_nvme", no_argument, 0, 'N'},
        {"pdt", no_argument, 0, 'D'},
        {"protection", no_argument, 0, 'p'},
        {"protmode", no_argument, 0, 'P'},
        {"scsi_id", no_argument, 0, 'i'},
        {"scsi-id", no_argument, 0, 'i'}, /* convenience, not documented */
        {"size", no_argument, 0, 's'},
        {"sz-lbs", no_argument, 0, 'S'},
        {"sz_lbs", no_argument, 0, 'S'},  /* convenience, not documented */
        {"sysfsroot", required_argument, 0, 'y'},
        {"transport", no_argument, 0, 't'},
        {"unit", no_argument, 0, 'u'},
        {"long_unit", no_argument, 0, 'U'},
        {"long-unit", no_argument, 0, 'U'},
        {"verbose", no_argument, 0, 'v'},
        {"version", no_argument, 0, 'V'},
        {"wwn", no_argument, 0, 'w'},
        {0, 0, 0, 0}
};


/* Device node list: contains the information needed to match a node with a
 * sysfs class device. */
#define DEV_NODE_LIST_ENTRIES 16
enum dev_type {BLK_DEV, CHR_DEV};

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

/* WWN here is extracted from /dev/disk/by-id/wwn-<WWN> which is
 * created by udev 60-persistent-storage.rules using ID_WWN_WITH_EXTENSION.
 * The udev ID_WWN_WITH_EXTENSION is the combination of char wwn[17] and
 * char wwn_vendor_extension[17] from struct scsi_id_device. This macro
 * defines the maximum length of char-array needed to store this wwn including
 * the null-terminator.
 */
#define DISK_WWN_MAX_LEN 35

struct disk_wwn_node_entry {
        char wwn[DISK_WWN_MAX_LEN]; /* '0x' + wwn<128-bit> + */
                                    /* <null-terminator> */
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

/* Code analyzer states that the following two pointers may reference local
 * (auto or stack based) locations and thus may be dangling. However they
 * are only use by iscsi_target_scan() (plus functions it * calls) which is
 * invoked only in transport_tport(). And the local (auto or stack based)
 * locations flagged by the analyzer are defined in the function scope of
 * transport_tport(). Hence there is no problem.  */
static const char * iscsi_dir_name;
static const struct addr_hctl * iscsi_target_hct;

static int iscsi_tsession_num;

static char errpath[LMAX_PATH];


static const char * usage_message1 =
"Usage: lsscsi   [--brief] [--classic] [--controllers] [--device] "
            "[--generic]\n"
            "\t\t[--help] [--hosts] [--kname] [--list] [--long] "
            "[--long-unit]\n"
            "\t\t[--lunhex] [--no-nvme] [--pdt] [--protection] [--prot-mode]\n"
            "\t\t[--scsi_id] [--size] [--sz-lbs] [--sysfsroot=PATH] "
            "[--transport]\n"
            "\t\t[--unit] [--verbose] [--version] [--wwn]  [<h:c:t:l>]\n"
"  where:\n"
"    --brief|-b        tuple and device name only\n"
"    --classic|-c      alternate output similar to 'cat /proc/scsi/scsi'\n"
"    --controllers|-C   synonym for --hosts since NVMe controllers treated\n"
"                       like SCSI hosts\n"
"    --device|-d       show device node's major + minor numbers\n"
"    --generic|-g      show scsi generic device name\n"
"    --help|-h         this usage information\n"
"    --hosts|-H        lists scsi hosts rather than scsi devices\n"
"    --kname|-k        show kernel name instead of device node name\n"
"    --list|-L         additional information output one\n"
"                      attribute=value per line\n"
"    --long|-l         additional information output\n"
"    --long-unit|-U    print LU name in full, use twice to prefix with\n"
"                      '.naa', 'eui.', 'uuid.' or 't10.'\n"
"    --lunhex|-x       show LUN part of tuple as hex number in T10 "
"format;\n";

static const char * usage_message2 =
"                      use twice to get full 16 digit hexadecimal LUN\n"
"    --no-nvme|-N      exclude NVMe devices from output\n"
"    --pdt|-D          show the peripheral device type in hex\n"
"    --protection|-p   show target and initiator protection information\n"
"    --protmode|-P     show negotiated protection information mode\n"
"    --scsi_id|-i      show udev derived /dev/disk/by-id/scsi* entry\n"
"    --size|-s         show disk size, (once for decimal (e.g. 3 GB),\n"
"                      twice for power of two (e.g. 2.7 GiB),\n"
"                      thrice for number of blocks))\n"
"    --sysfsroot=PATH|-y PATH    set sysfs mount point to PATH (def: /sys)\n"
"    --sz-lbs|-S       show size as a number of logical blocks; if used "
"twice\n"
"                      adds comma followed by logical block size in bytes\n"
"    --transport|-t    transport information for target or, if '--hosts'\n"
"                      given, for initiator\n"
"    --unit|-u         logical unit (LU) name (aka WWN for ATA/SATA)\n"
"    --verbose|-v      output path names where data is found\n"
"    --version|-V      output version string and exit\n"
"    --wwn|-w          output WWN for disks (from /dev/disk/by-id/wwn*)\n"
"    <h:c:t:l>         filter output list (def: '*:*:*:*' (all)). Meaning:\n"
"                      <host_num:controller:target:lun> or for NVMe:\n"
"                      <'N':ctl_num:cntlid:namespace_id>\n\n"
"List SCSI devices or hosts, followed by NVMe namespaces or controllers.\n"
"Many storage devices (e.g. SATA disks and USB attached storage) use SCSI\n"
"command sets and hence are also listed by this utility. Hyphenated long\n"
"options can also take underscore (and vice versa).\n";


#ifdef __GNUC__
static int pr2serr(const char * fmt, ...)
        __attribute__ ((format (printf, 1, 2)));
#else
static int pr2serr(const char * fmt, ...);
#endif


static int
pr2serr(const char * fmt, ...)
{
        va_list args;
        int n;

        va_start(args, fmt);
        n = vfprintf(stderr, fmt, args);
        va_end(args);
        return n;
}

#ifdef __GNUC__
static int scnpr(char * cp, int cp_max_len, const char * fmt, ...)
        __attribute__ ((format (printf, 3, 4)));
#else
static int scnpr(char * cp, int cp_max_len, const char * fmt, ...);
#endif

/* Want safe, 'n += snprintf(b + n, blen - n, ...)' style sequence of
 * functions. Returns number number of chars placed in cp excluding the
 * trailing null char. So for cp_max_len > 0 the return value is always
 * < cp_max_len; for cp_max_len <= 1 the return value is 0 and no chars
 * are written to cp. Note this means that when cp_max_len = 1, this
 * function assumes that cp[0] is the null character and does nothing
 * (and returns 0).  */
static int
scnpr(char * cp, int cp_max_len, const char * fmt, ...)
{
        va_list args;
        int n;

        if (cp_max_len < 2)
                return 0;
        va_start(args, fmt);
        n = vsnprintf(cp, cp_max_len, fmt, args);
        va_end(args);
        return (n < cp_max_len) ? n : (cp_max_len - 1);
}

#if 0
static bool
all_zeros(const uint8_t * bp, int b_len)
{
        if ((NULL == bp) || (b_len <= 0))
        return false;
        for (--b_len; b_len >= 0; --b_len) {
                if (0x0 != bp[b_len])
                        return false;
        }
        return true;
}

static bool
all_ffs(const uint8_t * bp, int b_len)
{
        if ((NULL == bp) || (b_len <= 0))
                return false;
        for (--b_len; b_len >= 0; --b_len) {
                if (0xff != bp[b_len])
                        return false;
        }
        return true;
}
#endif

#if (HAVE_NVME && (! IGNORE_NVME))

/* trims leading whitespaces, if trim_leading is true; and trims trailing
 * whitespaces, if trim_trailing is true. Edits s in place. If s is NULL
 * or empty (or both bools are false) it does nothing. Returns length of
 * processed string (or 0 if s is NULL). */
static int
trim_lead_trail(char * s, bool trim_leading, bool trim_trailing) {
        int n;
        char * p = s;

        if ((NULL == s) || (0 == ((n = (int)strlen(p)))) ||
            (! (trim_leading && trim_trailing))) /* sanity checks */
                return s ? (int)strlen(s) : 0;

        if (trim_trailing) {
                while (isspace((uint8_t)p[n - 1]))
                        p[--n] = 0;
        }
        if (trim_leading) {
                while (*p && isspace((uint8_t)*p)) {
                        ++p;
                        --n;
                }
                memmove(s, p, n + 1);
        }
        return (int)strlen(s);
}

/* Truncate or pad string to length n, plus adds null byte to str assumed to
 * be at least n+1 bytes long. If shorter than n, pads with spaces to right.
 * If truncated and trailing__on_trunc is true and last character (after
 * truncate) is not whitespace, then places "_" in last character position. */
static void
trunc_pad2n(char * str, int n, bool trailing__on_trunc)
{
    int slen = strlen(str);

    if (slen < n) {
        memset(str + slen, ' ', n - slen);
        str[n] = '\0';
    } else if (slen > n) {
        str[n] = '\0';
        if ((n > 0) && trailing__on_trunc && (! isspace((uint8_t)str[n - 1])))
                str[n - 1] = '_';
    }
}

static const char * bad_arg = "Bad_argument";

/* Opens the file 'dirp/fname' and searches for 'name'=, the first one found
 * has its value (rest of line after "=") returned in 'b'. The 'name' is
 * typically in upper case. Example: 'MAJOR=253' if name is 'MAJOR' returns
 * pointer to string containing '253'. */
static char *
name_eq2value(const char * dirp, const char * fname, const char * name,
              int b_len, char * b)
{
        bool ok = false;
        int k;
        size_t len = 0;
        size_t n;
        char * full_name;
        FILE * fp = NULL;
        char line[132];

        if (b_len > 0)
                b[0] = '\0';
        if (b_len < 2)
                return b;
        if (dirp)
                len = strlen(dirp);
        if (fname)
                len += strlen(fname);
        if (len < 1) {
                snprintf(b, b_len, "%s", bad_arg);
                return b;
        }
        len += 20;
        full_name = (char *)calloc(1, len);
        if (NULL == full_name)
                goto clean_up;
        if (dirp && fname)
                snprintf(full_name, len - 2, "%s/%s", dirp, fname);
        else if (dirp)
                snprintf(full_name, len - 2, "%s", dirp);
        else    /* fname must be nz (if zero(null) then len==0 above) */
                snprintf(full_name, len - 2, "%s", fname);

        fp = fopen(full_name, "r");
        if (NULL == fp) {
#if 0
            pr2serr("%s: unable to open %s\n", __func__, full_name);
#endif
            goto clean_up;
        }
        if (strlen(name) >= (len - 2)) {
                snprintf(b, b_len, "%s", bad_arg);
                goto clean_up;
        }
        /* Re-use full_name as filename no longer needed */
        snprintf(full_name, len - 1, "%s=", name);
        n = strlen(full_name);

        for (k = 0; k < 1024; ++k) {    /* shouldn't be that many lines */
                if (NULL == fgets(line, sizeof(line), fp))
                        break;
                if (0 == strncmp(line, full_name, n)) {
                        ok = true;
                        break;
                }
        }
        if (ok) {
                snprintf(b, b_len, "%s", line + n);
                n = strlen(b);
                if ((n > 0) && ('\n' == b[n - 1]))
                        b[n - 1] = '\0';        /* remove trailing LF */
        }
clean_up:
        free(full_name);
        if (fp)
                fclose(fp);
        return b;
}

#endif          /* (HAVE_NVME && (! IGNORE_NVME)) */

/* Returns true if dirent entry is either a symlink or a directory
 * starting_with given name. If starting_with is NULL choose all that are
 * either symlinks or directories other than . or .. (own directory or
 * parent) . Can be tricked cause symlink could point to .. (parent), for
 * example. Otherwise return false. */
static bool
dir_or_link(const struct dirent * s, const char * starting_with)
{
        if (DT_LNK == s->d_type) {
                if (starting_with)
                        return 0 == strncmp(s->d_name, starting_with,
                                            strlen(starting_with));
                return true;
        } else if (DT_DIR != s->d_type)
                return false;
        else {  /* Assume can't have zero length directory name */
                size_t len = strlen(s->d_name);

                if (starting_with)
                        return 0 == strncmp(s->d_name, starting_with,
                                            strlen(starting_with));
                if (len > 2)
                        return true;
                if ('.' == s->d_name[0]) {
                        if (1 == len)
                                return false;   /* this directory: '.' */
                        else if ('.' == s->d_name[1])
                                return false;   /* parent: '..' */
                }
                return true;
        }
}

static void
usage(void)
{
        pr2serr("%s%s", usage_message1, usage_message2);
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
                memcpy(dest, src, (lp  - src) + 1);
}

static uint64_t
lun_word_flip(uint64_t in)
{
        int k;
        uint64_t res = 0;

        for (k = 0; ; ++k) {
                res |= (in & 0xffff);
                if (k > 2)
                        break;
                res <<= 16;
                in >>= 16;
        }
        return res;
}

/* Bits 3, 2, 1, 0 in sel_mask select the h, c, t, l components respectively.
 * Bits 4+5 of sel_mask convey the --lunhex option selecting l (LUN) in
 * hex. Generates string of the form %d:%d:%d with a colon between
 * components, returns 4th argument. */
static char *
tuple2string(const struct addr_hctl * tp, int sel_mask, int blen, char * b)
{
        bool got1 = false;
        bool is_nvme = (NVME_HOST_NUM == tp->h);
        int n = 0;

        if (0x8 & sel_mask) {
                if (is_nvme)
                        n += scnpr(b + n, blen - n, "N");
                else
                        n += scnpr(b + n, blen - n, "%d", tp->h);
                got1 = true;
        }
        if (0x4 & sel_mask) {
                n += scnpr(b + n, blen - n, "%s%d", got1 ? ":" : "", tp->c);
                got1 = true;
        }
        if (0x2 & sel_mask) {
                n += scnpr(b + n, blen - n, "%s%d", got1 ? ":" : "", tp->t);
                got1 = true;
        }
        if ((! is_nvme ) && (0x1 & sel_mask)) {
                int lunhex = (sel_mask >> 4) & 0x3;

                if (1 == lunhex) {  /* -x (--lunhex) format */
                        int ta, k;
                        int tag_arr[16];

                        n += scnpr(b + n, blen - n, "%s0x", got1 ? ":" : "");
                        tag_lun(tp->lun_arr, tag_arr);
                        for (k = 0; k < 8; ++k) {
                                ta = tag_arr[k];
                                if (ta <= 0)
                                        break;
                                n += scnpr(b + n, blen - n, "%s%02x",
                                           ((ta > 1) ? "_" : ""),
                                           tp->lun_arr[k]);
                        }
                } else if (lunhex > 1) /* -xx (--lunhex twice) */
                        n += scnpr(b + n, blen - n, "%s0x%016" PRIx64,
                                   got1 ? ":" : "",
                                   lun_word_flip(tp->l));
                else if (UINT64_LAST == tp->l)
                        n += scnpr(b + n, blen - n, "%s",
                                   got1 ? ":-1" : "-1");
                else
                        n += scnpr(b + n, blen - n, "%s%" PRIu64,
                                   got1 ? ":" : "", tp->l);
        } else if (0x1 & sel_mask) {    /* now must be NVMe */
                int lunhex = (sel_mask >> 4) & 0x3;

                if (1 == lunhex) {  /* -x (--lunhex) format */
                        n += scnpr(b + n, blen - n, "%s0x", got1 ? ":" : "");
                        n += scnpr(b + n, blen - n, "%04" PRIx32,
                                   (uint32_t)tp->l);
                } else if (lunhex > 1) { /* -xx (--lunhex twice) */
                        n += scnpr(b + n, blen - n, "%s0x", got1 ? ":" : "");
                        n += scnpr(b + n, blen - n, "%08" PRIx32,
                                   (uint32_t)tp->l);
                } else if (UINT32_MAX == tp->l)
                        n += scnpr(b + n, blen - n, "%s",
                                   got1 ? ":-1" : "-1");
                else
                        n += scnpr(b + n, blen - n, "%s%" PRIu32,
                                   got1 ? ":" : "", (uint32_t)tp->l);
        }
        return b;
}

#if (HAVE_NVME && (! IGNORE_NVME))

static void
mk_nvme_tuple(struct addr_hctl * tp, int cdev_minor, int cntlid,
              uint32_t nsid)
{
        tp->h = NVME_HOST_NUM;
        tp->c = cdev_minor;
        tp->t = cntlid;
        // tp->l = nsid;
        sg_put_unaligned_le32(nsid, tp->lun_arr);
        memset(tp->lun_arr + 4, 0, 4);
        tp->l = nsid;
}

#endif

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
        STRING_UNITS_10 = 0,    /* use powers of 10^3 (standard SI) */
        STRING_UNITS_2,         /* use binary powers of 2^10 */
};

/**
 * size2string - get the size in the specified units
 * @size:       The size to be converted
 * @units:      units to use (powers of 1000 or 1024)
 * @buf:        buffer to format to
 * @len:        length of buffer
 *
 * This function yields a string formatted to 3 significant figures
 * giving the size in the required units.  Returns true on success or
 * false on failure.  @buf is always zero terminated.
 */
static bool
size2string(uint64_t size, const enum string_size_units units, char *buf,
            int len)
{
        int i, j;
        unsigned int res;
        uint64_t sf_cap;
        uint64_t remainder = 0;
        char tmp[8];
        const char *units_10[] = { "B", "kB", "MB", "GB", "TB", "PB",
                                   "EB", "ZB", "YB", NULL};
        const char *units_2[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB",
                                 "EiB", "ZiB", "YiB", NULL };
        /* designated initializer are C99 but not yet C++; g++ and clang++
         * accept them (with noise) */
#ifdef __cplusplus
        const char **units_str[] = {units_10, units_2, };
        const unsigned int divisor[] = {1000, 1024, };
#else
        const char **units_str[] = {
                [STRING_UNITS_10] =  units_10,
                [STRING_UNITS_2] = units_2,
        };
        const unsigned int divisor[] = {
                [STRING_UNITS_10] = 1000,
                [STRING_UNITS_2] = 1024,
        };
#endif

        tmp[0] = '\0';
        i = 0;
        if (size >= divisor[units]) {
                while ((size >= divisor[units]) && units_str[units][i]) {
                        remainder = do_div_rem(&size, divisor[units]);
                        i++;
                }

                sf_cap = size;
                for (j = 0; (sf_cap * 10) < 1000; ++j)
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

        return true;
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
        if (p) {
                p->h = -1;
                p->c = -1;
                p->t = -1;
                p->l = UINT64_LAST;
                /* le or be, it matters not; writing 0xff bytes */
                sg_put_unaligned_le64(p->l, p->lun_arr);
        }
}

/* Return 1 for directory entry that is link or directory (other than
 * a directory name starting with dot). Else return 0.  */
static int
first_dir_scan_select(const struct dirent * s)
{
        if (FT_OTHER != aa_first.ft)
                return 0;
        if (! dir_or_link(s, NULL))
                return 0;
        my_strcopy(aa_first.name, s->d_name, LMAX_NAME);
        aa_first.ft = FT_CHAR;  /* dummy */
        aa_first.d_type =  s->d_type;
        return 1;
}

/* Selects symlinks and directories that don't start with "." */
static int
sub_dir_scan_select(const struct dirent * s)
{
        return (dir_or_link(s, NULL)) ? 1 : 0;
}

/* Selects symlinks and directories that don't start with "." as long as
 * they contain the string "scsi_disk". */
static int
sd_dir_scan_select(const struct dirent * s)
{
        return (dir_or_link(s, "scsi_disk")) ? 1 : 0;
}

/* Return 1 for directory entry that is link or directory (other than a
 * directory name starting with dot) that contains "block". Else return 0.
 */
static int
block_dir_scan_select(const struct dirent * s)
{
        return (dir_or_link(s, "block")) ? 1 : 0;
}

typedef int (* dirent_select_fn) (const struct dirent *);

/* Scans directory dir_name, selecting elements on the basis of fn (NULL
 * select all), into an unsorted list. The first item is assumed to be
 * directories (or symlinks to) and it is appended, after a '/' to dir_name.
 * Then if sub_str is found in that dir_name, it selects items that are
 * directories or symlinks, the first of which is appended, after a '/',
 * to dir_name. If conditions are met true is return, elae false.
 */
static bool
sub_scan(char * dir_name, const char * sub_str, dirent_select_fn fn)
{
        int num, k, len;
        struct dirent ** namelist;

        num = scandir(dir_name, &namelist, fn, NULL);
        if (num <= 0)
                return false;
        len = strlen(dir_name);
        if (len >= LMAX_PATH)
                return false;
        snprintf(dir_name + len, LMAX_PATH - len, "/%s", namelist[0]->d_name);

        for (k = 0; k < num; ++k)
                free(namelist[k]);
        free(namelist);

        if (strstr(dir_name, sub_str) == 0) {
                num = scandir(dir_name, &namelist, sub_dir_scan_select, NULL);
                if (num <= 0)
                        return false;
                len = strlen(dir_name);
                if (len >= LMAX_PATH)
                        return false;
                snprintf(dir_name + len, LMAX_PATH - len, "/%s",
                         namelist[0]->d_name);

                for (k = 0; k < num; ++k)
                        free(namelist[k]);
                free(namelist);
        }
        return true;
}

/* Scan for block:sdN or block/sdN directory in
 * /sys/bus/scsi/devices/h:c:i:l  */
static bool
block_scan(char * dir_name)
{
        return sub_scan(dir_name, "block:", block_dir_scan_select);
}

/* Scan for scsi_disk:h:c:i:l or scsi_disk/h:c:i:l directory in
 * /sys/bus/scsi/devices/h:c:i:l  */
static bool
sd_scan(char * dir_name)
{
        return sub_scan(dir_name, "scsi_disk:", sd_dir_scan_select);
}

static int
enclosure_device_dir_scan_select(const struct dirent * s)
{
        if (dir_or_link(s, "enclosure_device")) {
                my_strcopy(enclosure_device.name, s->d_name,
                           LMAX_NAME);
                enclosure_device.ft = FT_CHAR;  /* dummy */
                enclosure_device.d_type =  s->d_type;
                return 1;
        }
        return 0;
}

/* Return true for directory entry that is link or directory (other than a
 * directory name starting with dot) that contains "enclosure_device".
 * Else return false.  */
static bool
enclosure_device_scan(const char * dir_name, const struct lsscsi_opts * op)
{
        int num, k;
        struct dirent ** namelist;

        num = scandir(dir_name, &namelist, enclosure_device_dir_scan_select,
                      NULL);
        if (num < 0) {
                if (op->verbose > 0) {
                        snprintf(errpath, LMAX_PATH, "%s: scandir: %s",
                                 __func__, dir_name);
                        perror(errpath);
                }
                return false;
        }
        for (k = 0; k < num; ++k)
                free(namelist[k]);
        free(namelist);
        return !! num;
}

/* scan for directory entry that is either a symlink or a directory. Returns
 * number found or -1 for error. */
static int
scan_for_first(const char * dir_name, const struct lsscsi_opts * op)
{
        int num, k;
        struct dirent ** namelist;

        aa_first.ft = FT_OTHER;
        num = scandir(dir_name, &namelist, first_dir_scan_select, NULL);
        if (num < 0) {
                if (op->verbose > 0) {
                        snprintf(errpath, LMAX_PATH, "%s: scandir: %s",
                                 __func__, dir_name);
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
non_sg_dir_scan_select(const struct dirent * s)
{
        int len;

        if (FT_OTHER != non_sg.ft)
                return 0;
        if (! dir_or_link(s, NULL))
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

/* Returns number found or -1 for error */
static int
non_sg_scan(const char * dir_name, const struct lsscsi_opts * op)
{
        int num, k;
        struct dirent ** namelist;

        non_sg.ft = FT_OTHER;
        num = scandir(dir_name, &namelist, non_sg_dir_scan_select, NULL);
        if (num < 0) {
                if (op->verbose > 0) {
                        snprintf(errpath, LMAX_PATH, "%s: scandir: %s",
                                 __func__, dir_name);
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
sg_dir_scan_select(const struct dirent * s)
{
        if (FT_OTHER != aa_sg.ft)
                return 0;
        if (dir_or_link(s, "scsi_generic")) {
                my_strcopy(aa_sg.name, s->d_name, LMAX_NAME);
                aa_sg.ft = FT_CHAR;
                aa_sg.d_type =  s->d_type;
                return 1;
        } else
                return 0;
}

/* Returns number of directories or links starting with "scsi_generic"
 * found or -1 for error. */
static int
sg_scan(const char * dir_name)
{
        int num, k;
        struct dirent ** namelist;

        aa_sg.ft = FT_OTHER;
        num = scandir(dir_name, &namelist, sg_dir_scan_select, NULL);
        if (num < 0)
                return -1;
        for (k = 0; k < num; ++k)
                free(namelist[k]);
        free(namelist);
        return num;
}


static int
sas_port_dir_scan_select(const struct dirent * s)
{
        return (dir_or_link(s, "port-")) ? 1 : 0;
}

static int
sas_port_scan(const char * dir_name, struct dirent ***port_list)
{
        int num;
        struct dirent ** namelist;

        namelist = NULL;
        num = scandir(dir_name, &namelist, sas_port_dir_scan_select, NULL);
        if (num < 0) {
                *port_list = NULL;
                return -1;
        }
        *port_list = namelist;
        return num;
}


static int
sas_low_phy_dir_scan_select(const struct dirent * s)
{
        int n, m;
        char * cp;

        if (dir_or_link(s, "phy")) {
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
        int num, k;
        struct dirent ** namelist=NULL;

        memset(sas_low_phy, 0, sizeof(sas_low_phy));
        num = scandir(dir_name, &namelist, sas_low_phy_dir_scan_select, NULL);
        if (num < 0)
                return -1;
        if (! phy_list) {
                for (k = 0; k < num; ++k)
                        free(namelist[k]);
                free(namelist);
        }
        else
                *phy_list = namelist;
        return num;
}

static int
iscsi_target_dir_scan_select(const struct dirent * s)
{
        int off;
        char buff[LMAX_PATH];
        struct stat a_stat;

        if (dir_or_link(s, "session")) {
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
        int num, k;
        struct dirent ** namelist;

        iscsi_dir_name = dir_name;
        iscsi_target_hct = hctl;
        iscsi_tsession_num = -1;
        num = scandir(dir_name, &namelist, iscsi_target_dir_scan_select,
                      NULL);
        if (num < 0)
                return -1;
        for (k = 0; k < num; ++k)
                free(namelist[k]);
        free(namelist);
        return num;
}


/* If 'dir_name'/'base_name' is a directory chdir to it. If that is successful
   return true, else false */
static bool
if_directory_chdir(const char * dir_name, const char * base_name)
{
        char b[LMAX_PATH];
        struct stat a_stat;

        snprintf(b, sizeof(b), "%s/%s", dir_name, base_name);
        if (stat(b, &a_stat) < 0)
                return false;
        if (S_ISDIR(a_stat.st_mode)) {
                if (chdir(b) < 0)
                        return false;
                return true;
        }
        return false;
}

/* If 'dir_name'/generic is a directory chdir to it. If that is successful
   return true. Otherwise look a directory of the form
   'dir_name'/scsi_generic:sg<n> and if found chdir to it and return true.
   Otherwise return false. */
static bool
if_directory_ch2generic(const char * dir_name)
{
        const char * old_name = "generic";
        char b[LMAX_PATH];
        struct stat a_stat;

        snprintf(b, sizeof(b), "%s/%s", dir_name, old_name);
        if ((stat(b, &a_stat) >= 0) && S_ISDIR(a_stat.st_mode)) {
                if (chdir(b) < 0)
                        return false;
                return true;
        }
        /* No "generic", so now look for "scsi_generic:sg<n>" */
        if (1 != sg_scan(dir_name))
                return false;
        snprintf(b, sizeof(b), "%s/%s", dir_name, aa_sg.name);
        if (stat(b, &a_stat) < 0)
                return false;
        if (S_ISDIR(a_stat.st_mode)) {
                if (chdir(b) < 0)
                        return false;
                return true;
        }
        return false;
}

/* If 'dir_name'/'base_name' is found places corresponding value in 'value'
 * and returns true . Else returns false.
 */
static bool
get_value(const char * dir_name, const char * base_name, char * value,
          int max_value_len)
{
        int len;
        FILE * f;
        char b[LMAX_PATH];

        snprintf(b, sizeof(b), "%s/%s", dir_name, base_name);
        if (NULL == (f = fopen(b, "r"))) {
                return false;
        }
        if (NULL == fgets(value, max_value_len, f)) {
                /* assume empty */
                value[0] = '\0';
                fclose(f);
                return true;
        }
        len = strlen(value);
        if ((len > 0) && (value[len - 1] == '\n'))
                value[len - 1] = '\0';
        fclose(f);
        return true;
}

/* Allocate dev_node_list and collect info on every char and block devices
 * in /dev but not its subdirectories. This list excludes symlinks, even if
 * they are to devices. */
static void
collect_dev_nodes(void)
{
        size_t dnl_sz = sizeof(struct dev_node_list);
        struct dirent *dep;
        DIR *dirp;
        struct dev_node_list *cur_list, *prev_list;
        struct dev_node_entry *cur_ent;
        char device_path[LMAX_DEVPATH];
        struct stat stats;

        if (dev_node_listhead)
                return; /* already collected nodes */

        dev_node_listhead = (struct dev_node_list *)calloc(1, dnl_sz);
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

                /* This will bypass all symlinks in /dev */
                if (lstat(device_path, &stats))
                        continue;

                /* Skip non-block/char files. */
                if ( (!S_ISBLK(stats.st_mode)) && (!S_ISCHR(stats.st_mode)) )
                        continue;

                /* Add to the list. */
                if (cur_list->count >= DEV_NODE_LIST_ENTRIES) {
                        prev_list = cur_list;
                        cur_list = (struct dev_node_list *)calloc(1, dnl_sz);
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
 * matching major/minor and type. Outputs to node which is assumed to be at
 * least LMAX_NAME bytes long. Returns true if match found, false
 * otherwise. */
static bool
get_dev_node(const char * wd, char * node, enum dev_type type)
{
        bool match_found = false;
        unsigned int k = 0;
        unsigned int maj, min;
        time_t newest_mtime = 0;
        struct dev_node_entry *cur_ent;
        struct dev_node_list *cur_list;
        char value[LMAX_NAME];

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
                        if ((! match_found) ||
                            (difftime(cur_ent->mtime,newest_mtime) > 0)) {
                                newest_mtime = cur_ent->mtime;
                                my_strcopy(node, cur_ent->name, LMAX_NAME);
                        }
                        match_found = true;
                }
        }

exit:
        return match_found;
}

/* Allocate disk_wwn_node_list and collect info on every node in
 * /dev/disk/by-id/scsi-* that does not contain "part" . Returns
 * number of wwn nodes collected, 0 for already collected and
 * -1 for error. */
static int
collect_disk_wwn_nodes(void)
{
        int k;
        int num = 0;
        size_t dwnl_sz = sizeof(struct disk_wwn_node_list);
        struct disk_wwn_node_list *cur_list, *prev_list;
        struct disk_wwn_node_entry *cur_ent;
        DIR *dirp;
        struct dirent *dep;
        char device_path[PATH_MAX + 1];
        char symlink_path[PATH_MAX + 1];
        struct stat stats;

        if (disk_wwn_node_listhead)
                return num; /* already collected nodes */

        disk_wwn_node_listhead =
                         (struct disk_wwn_node_list *)calloc(1, dwnl_sz);
        if (! disk_wwn_node_listhead)
                return -1;

        cur_list = disk_wwn_node_listhead;

        dirp = opendir(dev_disk_byid_dir);
        if (dirp == NULL)
                return -1;

        while (1) {
                dep = readdir(dirp);
                if (dep == NULL)
                        break;
                if (memcmp("scsi-", dep->d_name, 5))
                        continue;       /* needs to start with "scsi-" */
                if (strstr(dep->d_name, "part"))
                        continue;       /* skip if contains "part" */
                if (dep->d_name[5] != '3' &&
                    dep->d_name[5] != '2' &&
                    dep->d_name[5] != '8')
                        continue;       /* skip for invalid identifier */

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
                                                        calloc(1, dwnl_sz);
                        if (! cur_list)
                                break;
                        prev_list->next = cur_list;
                }

                cur_ent = &cur_list->nodes[cur_list->count];
                my_strcopy(cur_ent->wwn, "0x", 2);
                my_strcopy(cur_ent->wwn + 2, dep->d_name + 5,
                           sizeof(cur_ent->wwn) - 2);
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
   matching major/minor. Returns true if match found, false otherwise. */
static bool
get_disk_wwn(const char *wd, char * wwn_str, int max_wwn_str_len)
{
        unsigned int k = 0;
        char * bn;
        struct disk_wwn_node_list *cur_list;
        struct disk_wwn_node_entry *cur_ent;
        char name[LMAX_PATH];

        my_strcopy(name, wd, sizeof(name));
        name[sizeof(name) - 1] = '\0';
        bn = basename(name);
        if (disk_wwn_node_listhead == NULL) {
                collect_disk_wwn_nodes();
                if (disk_wwn_node_listhead == NULL)
                        return false;
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
                        return true;
                }
        }
        return false;
}

/*
 * Look up a device node in a directory with symlinks to device nodes.
 * @dir: Directory to examine, e.g. "/dev/disk/by-id".
 * @pfx: Prefix of the symlink, e.g. "scsi-".
 * @dev: Device node to look up, e.g. "/dev/sda".
 * Returns a pointer to the name of the symlink without the prefix if a match
 * has been found.
 * Side effect: changes the working directory to @dir.
 * Note: The caller must free the pointer returned by this function.
 */
static char *
lookup_dev(const char *dir, const char *pfx, const char *dev)
{
        unsigned st_rdev;
        DIR *dirp;
        struct dirent *entry;
        char *result = NULL;
        struct stat stats;

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
 * Note: The caller must free the returned buffer with free().
 */
static char *
get_disk_scsi_id(const char *dev_node)
{
        char *scsi_id = NULL;
        DIR *dir;
        struct dirent *entry;
        char holder[LMAX_PATH + 6];
        char sys_block[LMAX_PATH];

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
        int len;
        char * c2p;
        char * cp;
        const char * np;
        char bf2[LMAX_PATH];
        char buff[LMAX_DEVPATH];

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

#define VPD_DEVICE_ID 0x83
#define VPD_ASSOC_LU 0
#define VPD_ASSOC_TPORT 1
#define TPROTO_ISCSI 5

/* Iterates to next designation descriptor in the device identification
 * VPD page. The 'initial_desig_desc' should point to start of first
 * descriptor with 'page_len' being the number of valid bytes in that
 * and following descriptors. To start, 'off' should point to a negative
 * value, thereafter it should point to the value yielded by the previous
 * call. If 0 returned then 'initial_desig_desc + *off' should be a valid
 * descriptor; returns -1 if normal end condition and -2 for an abnormal
 * termination. Matches association, designator_type and/or code_set when
 * any of those values are greater than or equal to zero. */
int
sg_vpd_dev_id_iter(const uint8_t * initial_desig_desc, int page_len,
                   int * off, int m_assoc, int m_desig_type, int m_code_set)
{
        const uint8_t * bp;
        int k, c_set, assoc, desig_type;

        for (k = *off, bp = initial_desig_desc ; (k + 3) < page_len; ) {
                k = (k < 0) ? 0 : (k + bp[k + 3] + 4);
                if ((k + 4) > page_len)
                        break;
                c_set = (bp[k] & 0xf);
                if ((m_code_set >= 0) && (m_code_set != c_set))
                        continue;
                assoc = ((bp[k + 1] >> 4) & 0x3);
                if ((m_assoc >= 0) && (m_assoc != assoc))
                        continue;
                desig_type = (bp[k + 1] & 0xf);
                if ((m_desig_type >= 0) && (m_desig_type != desig_type))
                        continue;
                *off = k;
                return 0;
        }
        return (k == page_len) ? -1 : -2;
}

/* Fetch logical unit (LU) name given the device name in the form:
 * h:c:t:l tuple string (e.g. "2:0:1:0"). This is fetched via sysfs (lk 3.15
 * and later) in vpd_pg83. For later ATA and SATA devices this may be its
 * WWN. Normally take the first found in this order: NAA, EUI-64 * then SCSI
 * name string. However if a SCSI name string is present and the protocol is
 * iSCSI (target port checked) then the SCSI name string is preferred. If
 * none of the above are present then check for T10 Vendor ID
 * (designator_type=1) and use if available. */
static char *
get_lu_name(const char * devname, char * b, int b_len, bool want_prefix)
{
        int fd, res, len, dlen, sns_dlen, off, k, n;
        uint8_t *bp;
        char *cp;
        char buff[LMAX_DEVPATH];
        uint8_t u[512];
        uint8_t u_sns[512];
        struct stat a_stat;

        if ((NULL == b) || (b_len < 1))
                return b;
        b[0] = '\0';
        snprintf(buff, sizeof(buff), "%s%s%s/device/vpd_pg83",
                 sysfsroot, class_scsi_dev, devname);
        if (! ((stat(buff, &a_stat) >= 0) && S_ISREG(a_stat.st_mode)))
                return b;
        if ((fd = open(buff, O_RDONLY)) < 0)
                return b;
        res = read(fd, u, sizeof(u));
        if (res <= 8) {
                close(fd);
                return b;
        }
        close(fd);
        if (VPD_DEVICE_ID != u[1])
                return b;
        len = sg_get_unaligned_be16(u + 2);
        if ((len + 4) != res)
                return b;
        bp = u + 4;
        cp = b;
        off = -1;
        if (0 == sg_vpd_dev_id_iter(bp, len, &off, VPD_ASSOC_LU,
                                    8 /* SCSI name string (sns) */,
                                    3 /* UTF-8 */)) {
                sns_dlen = bp[off + 3];
                memcpy(u_sns, bp + off + 4, sns_dlen);
                /* now want to check if this is iSCSI */
                off = -1;
                if (0 == sg_vpd_dev_id_iter(bp, len, &off, VPD_ASSOC_TPORT,
                                            8 /* SCSI name string (sns) */,
                                            3 /* UTF-8 */)) {
                        if ((0x80 & bp[1]) &&
                            (TPROTO_ISCSI == (bp[0] >> 4))) {
                                snprintf(b, b_len, "%.*s", sns_dlen, u_sns);
                                return b;
                        }
                }
        } else
                sns_dlen = 0;

        if (0 == sg_vpd_dev_id_iter(bp, len, &off, VPD_ASSOC_LU,
                                    3 /* NAA */, 1 /* binary */)) {
                dlen = bp[off + 3];
                if (! ((8 == dlen) || (16 ==dlen)))
                        return b;
                if (want_prefix) {
                        if ((n = snprintf(cp, b_len, "naa.")) >= b_len)
                            n = b_len - 1;
                        cp += n;
                        b_len -= n;
                }
                for (k = 0; ((k < dlen) && (b_len > 1)); ++k) {
                        snprintf(cp, b_len, "%02x", bp[off + 4 + k]);
                        cp += 2;
                        b_len -= 2;
                }
        } else if (0 == sg_vpd_dev_id_iter(bp, len, &off, VPD_ASSOC_LU,
                                           2 /* EUI */, 1 /* binary */)) {
                dlen = bp[off + 3];
                if (! ((8 == dlen) || (12 == dlen) || (16 ==dlen)))
                        return b;
                if (want_prefix) {
                        if ((n = snprintf(cp, b_len, "eui.")) >= b_len)
                            n = b_len - 1;
                        cp += n;
                        b_len -= n;
                }
                for (k = 0; ((k < dlen) && (b_len > 1)); ++k) {
                        snprintf(cp, b_len, "%02x", bp[off + 4 + k]);
                        cp += 2;
                        b_len -= 2;
                }
        } else if (0 == sg_vpd_dev_id_iter(bp, len, &off, VPD_ASSOC_LU,
                                           0xa /* UUID */,  1 /* binary */)) {
                dlen = bp[off + 3];
                if ((1 != ((bp[off + 4] >> 4) & 0xf)) || (18 != dlen)) {
                        snprintf(cp, b_len, "??");
                        /* cp += 2; */
                        /* b_len -= 2; */
                } else {
                        if (want_prefix) {
                                if ((n = snprintf(cp, b_len, "uuid.")) >=
                                    b_len)
                                    n = b_len - 1;
                                cp += n;
                                b_len -= n;
                        }
                        for (k = 0; (k < 16) && (b_len > 1); ++k) {
                                if ((4 == k) || (6 == k) || (8 == k) ||
                                    (10 == k)) {
                                        snprintf(cp, b_len, "-");
                                        ++cp;
                                        --b_len;
                                }
                                snprintf(cp, b_len, "%02x",
                                         (unsigned int)bp[off + 6 + k]);
                                cp += 2;
                                b_len -= 2;
                        }
                }
        } else if (sns_dlen > 0)
                snprintf(b, b_len, "%.*s", sns_dlen, u_sns);
        else if ((0 == sg_vpd_dev_id_iter(bp, len, &off, VPD_ASSOC_LU,
                                          0x1 /* T10 vendor ID */,  -1)) &&
                 ((bp[off] & 0xf) > 1 /* ASCII or UTF */)) {
                dlen = bp[off + 3];
                if (dlen < 8)
                        return b;       /* must have 8 byte T10 vendor id */
                if (want_prefix) {
                        if ((n = snprintf(cp, b_len, "t10.")) >= b_len)
                            n = b_len - 1;
                        cp += n;
                        b_len -= n;
                }
                snprintf(cp, b_len, "%.*s", dlen, bp + off + 4);
        }
        return b;
}

/* Parse colon_list into host/channel/target/lun ("hctl") array, return true
 * if successful, else false. colon_list should point at first character of
 * hctl (i.e. a digit) and yields a new value in *outp when true returned. */
static bool
parse_colon_list(const char * colon_list, struct addr_hctl * outp)
{
        int k;
        uint64_t z;
        const char * elem_end;

        if ((! colon_list) || (! outp))
                return false;
#if (HAVE_NVME && (! IGNORE_NVME))
        if ('N' == toupper((uint8_t)*colon_list))
                outp->h = NVME_HOST_NUM;
        else
#endif
        if (1 != sscanf(colon_list, "%d", &outp->h))
                return false;
        if (NULL == (elem_end = strchr(colon_list, ':')))
                return false;
        colon_list = elem_end + 1;
        if (1 != sscanf(colon_list, "%d", &outp->c))
                return false;
        if (NULL == (elem_end = strchr(colon_list, ':')))
                return false;
        colon_list = elem_end + 1;
        if (1 != sscanf(colon_list, "%d", &outp->t))
                return false;
        if (NULL == (elem_end = strchr(colon_list, ':')))
                return false;
        colon_list = elem_end + 1;
        if (1 != sscanf(colon_list, "%" SCNu64 , &outp->l))
                return false;
        z = outp->l;
        for (k = 0; k < 8; k += 2, z >>= 16)
                sg_put_unaligned_be16((uint16_t)z, outp->lun_arr + k);
        return true;
}

/* Print enclosure device link from the rport- or end_device- */
static void
print_enclosure_device(const char *devname, const char *path,
                       const struct lsscsi_opts * op)
{
        char b[LMAX_PATH];
        struct addr_hctl hctl;

        if (parse_colon_list(devname, &hctl)) {
                snprintf(b, sizeof(b),
                         "%s/device/target%d:%d:%d/%d:%d:%d:%" PRIu64,
                         path, hctl.h, hctl.c, hctl.t,
                         hctl.h, hctl.c, hctl.t, hctl.l);
                if (enclosure_device_scan(b, op) > 0)
                        printf("  %s\n",enclosure_device.name);
        }
}

/*
 * Obtain the GUID of the InfiniBand port associated with SCSI host number h
 * by stripping prefix fe80:0000:0000:0000: from GID 0. An example:
 * 0002:c903:00a0:5de2.
 */
static void
get_local_srp_gid(const int h, char *b, int b_len)
{
        int port;
        char buff[LMAX_DEVPATH];
        char value[LMAX_NAME];

        snprintf(buff, sizeof(buff), "%s%shost%d", sysfsroot, scsi_host, h);
        if (!get_value(buff, "local_ib_port", value, sizeof(value)))
                return;
        if (sscanf(value, "%d", &port) != 1)
                return;
        if (!get_value(buff, "local_ib_device", value, sizeof(value)))
                return;
        snprintf(buff, sizeof(buff), "%s/class/infiniband/%s/ports/%d/gids",
                 sysfsroot, value, port);
        if (!get_value(buff, "0", value, sizeof(value)))
                return;
        if (strlen(value) > 20)
                snprintf(b, b_len, "%s", value + 20);
}

/*
 * Obtain the original GUID of the remote InfiniBand port associated with a
 * SCSI host by stripping prefix fe80:0000:0000:0000: from its GID. An
 * example: 0002:c903:00a0:5de2. Returns true on success, else false.
 */
static bool
get_srp_orig_dgid(const int h, char *b, int b_len)
{
        char buff[LMAX_DEVPATH];
        char value[LMAX_NAME];

        snprintf(buff, sizeof(buff), "%s%shost%d", sysfsroot, scsi_host, h);
        if (get_value(buff, "orig_dgid", value, sizeof(value)) &&
            strlen(value) > 20) {
                snprintf(b, b_len, "%s", value + 20);
                return true;
        }
        return false;
}

/*
 * Obtain the GUID of the remote InfiniBand port associated with a SCSI host
 * by stripping prefix fe80:0000:0000:0000: from its GID. An example:
 * 0002:c903:00a0:5de2. Returns true on success else false.
 */
static bool
get_srp_dgid(const int h, char *b, int b_len)
{
        char buff[LMAX_DEVPATH];
        char value[LMAX_NAME];

        snprintf(buff, sizeof(buff), "%s%shost%d", sysfsroot, scsi_host, h);
        if (get_value(buff, "dgid", value, sizeof(value)) &&
            strlen(value) > 20) {
                snprintf(b, b_len, "%s", value + 20);
                return true;
        }
        return false;
}

/* Check host associated with 'devname' for known transport types. If so set
 * transport_id, place a string in 'b' and return true. Otherwise return
 * false. */
static bool
transport_init(const char * devname, /* const struct lsscsi_opts * op, */
               int b_len, char * b)
{
        int off;
        char * cp;
        char buff[LMAX_DEVPATH];
        char wd[LMAX_PATH];
        struct stat a_stat;

        /* SPI host */
        snprintf(buff, sizeof(buff), "%s%s%s", sysfsroot, spi_host, devname);
        if ((stat(buff, &a_stat) >= 0) && S_ISDIR(a_stat.st_mode)) {
                transport_id = TRANSPORT_SPI;
                snprintf(b, b_len, "spi:");
                return true;
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
                        my_strcopy(b + off, ",", b_len - off);
                        off = strlen(b);
                } else
                        return false;
                if (get_value(buff, "port_id", b + off, b_len - off))
                        return true;
                else
                        return false;
        }

        /* SRP host */
        snprintf(buff, sizeof(buff), "%s%s%s", sysfsroot, srp_host, devname);
        if (stat(buff, &a_stat) >= 0 && S_ISDIR(a_stat.st_mode)) {
                int h;

                transport_id = TRANSPORT_SRP;
                snprintf(b, b_len, "srp:");
                if (sscanf(devname, "host%d", &h) == 1)
                        get_local_srp_gid(h, b + strlen(b), b_len - strlen(b));
                return true;
        }

        /* SAS host */
        /* SAS transport layer representation */
        snprintf(buff, sizeof(buff), "%s%s%s", sysfsroot, sas_host, devname);
        if ((stat(buff, &a_stat) >= 0) && S_ISDIR(a_stat.st_mode)) {
                transport_id = TRANSPORT_SAS;
                off = strlen(buff);
                snprintf(buff + off, sizeof(buff) - off, "/device");
                if (sas_low_phy_scan(buff, NULL) < 1)
                        return false;
                snprintf(buff, sizeof(buff), "%s%s%s", sysfsroot, sas_phy,
                         sas_low_phy);
                snprintf(b, b_len, "sas:");
                off = strlen(b);
                if (get_value(buff, "sas_address", b + off, b_len - off))
                        return true;
                else
                        pr2serr("_init: no sas_address, wd=%s\n", buff);
        }

        /* SAS class representation */
        snprintf(buff, sizeof(buff), "%s%s%s%s", sysfsroot, scsi_host,
                 devname, "/device/sas/ha");
        if ((stat(buff, &a_stat) >= 0) && S_ISDIR(a_stat.st_mode)) {
                transport_id = TRANSPORT_SAS_CLASS;
                snprintf(b, b_len, "sas:");
                off = strlen(b);
                if (get_value(buff, "device_name", b + off, b_len - off))
                        return true;
                else
                        pr2serr("_init: no device_name, wd=%s\n", buff);
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
                return true;
        } while (0);

        /* iSCSI host */
        snprintf(buff, sizeof(buff), "%s%s%s", sysfsroot, iscsi_host, devname);
        if ((stat(buff, &a_stat) >= 0) && S_ISDIR(a_stat.st_mode)) {
                transport_id = TRANSPORT_ISCSI;
                snprintf(b, b_len, "iscsi:");
// >>>       Can anything useful be placed after "iscsi:" in single line
//           host output?
//           Hmmm, probably would like SAM-4 ",i,0x" notation here.
                return true;
        }

        /* USB host? */
        cp = get_usb_devname(devname, NULL, wd, sizeof(wd) - 1);
        if (cp) {
                transport_id = TRANSPORT_USB;
                snprintf(b, b_len, "usb:%s", cp);
                return true;
        }

        /* ATA or SATA host, crude check: driver name */
        snprintf(buff, sizeof(buff), "%s%s%s", sysfsroot, scsi_host, devname);
        if (get_value(buff, "proc_name", wd, sizeof(wd))) {
                if (0 == strcmp("ahci", wd)) {
                        transport_id = TRANSPORT_SATA;
                        snprintf(b, b_len, "sata:");
                        return true;
                } else if (strstr(wd, "ata")) {
                        if (0 == memcmp("sata", wd, 4)) {
                                transport_id = TRANSPORT_SATA;
                                snprintf(b, b_len, "sata:");
                                return true;
                        }
                        transport_id = TRANSPORT_ATA;
                        snprintf(b, b_len, "ata:");
                        return true;
                }
        }
        return false;
}

/* Given the transport_id of a SCSI host (initiator) associated with
 * 'path_name' output additional information.
 */
static void
transport_init_longer(const char * path_name, const struct lsscsi_opts * op)
{
        int k, j, len;
        int phynum;
        int portnum;
        char * cp;
        struct stat a_stat;
        struct dirent ** phylist;
        struct dirent ** portlist;
        char buff[LMAX_PATH];
        char bname[LMAX_NAME];
        char value[LMAX_NAME];

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
        case TRANSPORT_SRP:
                printf("  transport=srp\n");
                {
                        int h;

                        if (sscanf(path_name, "host%d", &h) != 1)
                                break;
                        if (get_srp_orig_dgid(h, value, sizeof(value)))
                                printf("  orig_dgid=%s\n", value);
                        if (get_srp_dgid(h, value, sizeof(value)))
                                printf("  dgid=%s\n", value);
                }
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
                        for (k = 0; k < phynum; ++k) {
                                /* emit something potentially useful */
                                snprintf(buff, sizeof(buff), "%s%s%s",
                                         sysfsroot, sas_phy,
                                         phylist[k]->d_name);
                                printf("  %s\n",phylist[k]->d_name);
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
                for (k = 0; k < portnum; ++k) {     /* for each host port */
                        snprintf(buff, sizeof(buff), "%s%s%s", path_name,
                                 "/device/", portlist[k]->d_name);
                        if ((phynum = sas_low_phy_scan(buff, &phylist)) < 1) {
                                printf("  %s: phy list not available\n",
                                       portlist[k]->d_name);
                                free(portlist[k]);
                                continue;
                        }

                        snprintf(buff, sizeof(buff), "%s%s%s", sysfsroot,
                                 sas_port, portlist[k]->d_name);
                        if (get_value(buff, "num_phys", value,
                                      sizeof(value))) {
                                printf("  %s: num_phys=%s,",
                                       portlist[k]->d_name, value);
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

                        free(portlist[k]);

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
        case TRANSPORT_PCIE:
                printf("  transport=pcie\n");
                break;
        default:
                if (op->verbose > 1)
                        pr2serr("No transport information\n");
                break;
        }
}

/* Attempt to determine the transport type of the SCSI device (LU) associated
 * with 'devname'. If found set transport_id, place string in 'b' and return
 * true. Otherwise return false. */
static bool
transport_tport(const char * devname, const struct lsscsi_opts * op,
                int b_len, char * b)
{
        bool ata_dev;
        int off, n;
        char * cp;
        char buff[LMAX_DEVPATH];
        char wd[LMAX_PATH];
        char nm[LMAX_NAME];
        char tpgt[LMAX_NAME];
        struct addr_hctl hctl;
        struct stat a_stat;

        if (! parse_colon_list(devname, &hctl))
                return false;

        /* check for SAS host */
        snprintf(buff, sizeof(buff), "%s%shost%d", sysfsroot, sas_host,
                 hctl.h);
        if ((stat(buff, &a_stat) >= 0) && S_ISDIR(a_stat.st_mode)) {
                /* SAS transport layer representation */
                transport_id = TRANSPORT_SAS;
                snprintf(buff, sizeof(buff), "%s%s%s", sysfsroot,
                         class_scsi_dev, devname);
                if (if_directory_chdir(buff, "device")) {
                        if (NULL == getcwd(wd, sizeof(wd)))
                                return false;
                        cp = strrchr(wd, '/');
                        if (NULL == cp)
                                return false;
                        *cp = '\0';
                        cp = strrchr(wd, '/');
                        if (NULL == cp)
                                return false;
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
                                return true;
                        else {  /* non-SAS device in SAS domain */
                                snprintf(b + off, b_len - off,
                                         "0x0000000000000000");
                                if (op->verbose > 1)
                                        pr2serr("%s: no sas_address, wd=%s\n",
                                                __func__, buff);
                                return true;
                        }
                } else
                        pr2serr("%s: down FAILED: %s\n", __func__, buff);
                return false;
        }

        /* not SAS, so check for SPI host */
        snprintf(buff, sizeof(buff), "%s%shost%d", sysfsroot, spi_host,
                 hctl.h);
        if ((stat(buff, &a_stat) >= 0) && S_ISDIR(a_stat.st_mode)) {
                transport_id = TRANSPORT_SPI;
                snprintf(b, b_len, "spi:%d", hctl.t);
                return true;
        }

        /* no, so check for FC host */
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
                        my_strcopy(b + off, ",", b_len - off);
                        off = strlen(b);
                } else
                        return false;
                if (get_value(buff, "port_id", b + off, b_len - off))
                        return true;
                else
                        return false;
        }

        /* no, so check for SRP host */
        snprintf(buff, sizeof(buff), "%s%shost%d", sysfsroot, srp_host,
                 hctl.h);
        if (stat(buff, &a_stat) >= 0 && S_ISDIR(a_stat.st_mode)) {
                transport_id = TRANSPORT_SRP;
                snprintf(b, b_len, "srp:");
                get_local_srp_gid(hctl.h, b + strlen(b), b_len - strlen(b));
                return true;
        }

        /* SAS class representation or SBP? */
        snprintf(buff, sizeof(buff), "%s%s/%s", sysfsroot, bus_scsi_devs,
                 devname);
        if (if_directory_chdir(buff, "sas_device")) {
                transport_id = TRANSPORT_SAS_CLASS;
                snprintf(b, b_len, "sas:");
                off = strlen(b);
                if (get_value(".", "sas_addr", b + off, b_len - off))
                        return true;
                else
                        pr2serr("%s: no sas_addr, wd=%s\n", __func__, buff);
        } else if (get_value(buff, "ieee1394_id", wd, sizeof(wd))) {
                /* IEEE1394 SBP device */
                transport_id = TRANSPORT_SBP;
                snprintf(b, b_len, "sbp:%s", wd);
                return true;
        }

        /* iSCSI device? */
        snprintf(buff, sizeof(buff), "%s%shost%d/device", sysfsroot,
                 iscsi_host, hctl.h);
        if ((stat(buff, &a_stat) >= 0) && S_ISDIR(a_stat.st_mode)) {
                if (1 != iscsi_target_scan(buff, &hctl))
                        return false;
                transport_id = TRANSPORT_ISCSI;
                snprintf(buff, sizeof(buff), "%s%ssession%d", sysfsroot,
                         iscsi_session, iscsi_tsession_num);
                if (! get_value(buff, "targetname", nm, sizeof(nm)))
                        return false;
                if (! get_value(buff, "tpgt", tpgt, sizeof(tpgt)))
                        return false;
                n = atoi(tpgt);
                // output target port name as per sam4r08, annex A, table A.3
                snprintf(b, b_len, "%s,t,0x%x", nm, n);
// >>>       That reference says maximum length of targetname is 223 bytes
//           (UTF-8) excluding trailing null.
                return true;
        }

        /* USB device? */
        cp = get_usb_devname(NULL, devname, wd, sizeof(wd) - 1);
        if (cp) {
                transport_id = TRANSPORT_USB;
                snprintf(b, b_len, "usb:%s", cp);
                return true;
        }

        /* ATA or SATA device, crude check: driver name */
        snprintf(buff, sizeof(buff), "%s%shost%d", sysfsroot, scsi_host,
                 hctl.h);
        if (get_value(buff, "proc_name", wd, sizeof(wd))) {
                ata_dev = false;
                if (0 == strcmp("ahci", wd)) {
                        transport_id = TRANSPORT_SATA;
                        snprintf(b, b_len, "sata:");
                        ata_dev = true;
                } else if (strstr(wd, "ata")) {
                        if (0 == memcmp("sata", wd, 4)) {
                                transport_id = TRANSPORT_SATA;
                                snprintf(b, b_len, "sata:");
                        } else {
                                transport_id = TRANSPORT_ATA;
                                snprintf(b, b_len, "ata:");
                        }
                        ata_dev = true;
                }
                if (ata_dev) {
                        off = strlen(b);
                        snprintf(b + off, b_len - off, "%s",
                                 get_lu_name(devname, wd, sizeof(wd), false));
                        return true;
                }
        }
        return false;
}

/* Given the transport_id of the SCSI device (LU) associated with 'devname'
 * output additional information. */
static void
transport_tport_longer(const char * devname, const struct lsscsi_opts * op)
{
        char * cp;
        char path_name[LMAX_DEVPATH];
        char buff[LMAX_DEVPATH];
        char b2[LMAX_DEVPATH];
        char wd[LMAX_PATH];
        char value[LMAX_NAME];
        struct addr_hctl hctl;

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
// xxxxxxxxxxxx  following call to print_enclosure_device fails since b2 is
// inappropriate, comment out since might be useless (check with FCP folks)
                // print_enclosure_device(devname, b2, op);
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
        case TRANSPORT_SRP:
                printf("  transport=srp\n");
                if (! parse_colon_list(devname, &hctl))
                        break;
                if (get_srp_orig_dgid(hctl.h, value, sizeof(value)))
                        printf("  orig_dgid=%s\n", value);
                if (get_srp_dgid(hctl.h, value, sizeof(value)))
                        printf("  dgid=%s\n", value);
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
                cp = get_lu_name(devname, b2, sizeof(b2), false);
                if (strlen(cp) > 0)
                        printf("  wwn=%s\n", cp);
                break;
        case TRANSPORT_SATA:
                printf("  transport=sata\n");
                cp = get_lu_name(devname, b2, sizeof(b2), false);
                if (strlen(cp) > 0)
                        printf("  wwn=%s\n", cp);
                break;
        default:
                if (op->verbose > 1)
                        pr2serr("No transport information\n");
                break;
        }
}

static void
longer_d_entry(const char * path_name, const char * devname,
               const struct lsscsi_opts * op)
{
        char value[LMAX_NAME];

        if (op->transport_info) {
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

#if (HAVE_NVME && (! IGNORE_NVME))

/* NVMe longer data for namespace listing */
static void
longer_nd_entry(const char * path_name, const char * devname,
                const struct lsscsi_opts * op)
{
        char value[LMAX_NAME];
        const int vlen = sizeof(value);

        if (devname) { ; }      /* suppress warning */
        if (op->long_opt) {
                bool sing = (op->long_opt > 2);
                const char * sep = sing ? "\n" : "";

                if (get_value(path_name, "capability", value, vlen))
                        printf("  capability=%s%s", value, sep);
                else
                        printf("  capability=?%s", sep);
                if (get_value(path_name, "ext_range", value, vlen))
                        printf("  ext_range=%s%s", value, sep);
                else
                        printf("  ext_range=?%s", sep);
                if (get_value(path_name, "hidden", value, vlen))
                        printf("  hidden=%s%s", value, sep);
                else
                        printf("  hidden=?%s", sep);
                if (get_value(path_name, "nsid", value, vlen))
                        printf("  nsid=%s%s", value, sep);
                else
                        printf("  nsid=?%s", sep);
                if (get_value(path_name, "range", value, vlen))
                        printf("  range=%s%s", value, sep);
                else
                        printf("  range=?%s", sep);
                if (get_value(path_name, "removable", value, vlen))
                        printf("  removable=%s%s", value, sep);
                else
                        printf("  removable=?%s", sep);
                if (op->long_opt > 1) {
                        if (! sing)
                                printf("\n");
                        if (get_value(path_name, "queue/nr_requests", value,
                                      vlen))
                                printf("  nr_requests=%s%s", value, sep);
                        else
                                printf("  nr_requests=?%s", sep);
                        if (get_value(path_name, "queue/read_ahead_kb", value,
                                      vlen))
                                printf("  read_ahead_kb=%s%s", value, sep);
                        else
                                printf("  read_ahead_kb=?%s", sep);
                        if (get_value(path_name, "queue/write_cache", value,
                                      vlen))
                                printf("  write_cache=%s%s", value, sep);
                        else
                                printf("  write_cache=?%s", sep);
                        if (! sing)
                                printf("\n");
                        if (get_value(path_name, "queue/logical_block_size",
                                      value, vlen))
                                printf("  logical_block_size=%s%s", value,
                                       sep);
                        else
                                printf("  logical_block_size=?%s", sep);
                        if (get_value(path_name, "queue/physical_block_size",
                                      value, vlen))
                                printf("  physical_block_size=%s%s", value,
                                       sep);
                        else
                                printf("  physical_block_size=?%s", sep);
                }
                if (! sing)
                        printf("\n");
        }
}

#endif          /* (HAVE_NVME && (! IGNORE_NVME)) */

static void
one_classic_sdev_entry(const char * dir_name, const char * devname,
                       const struct lsscsi_opts * op)
{
        int type, scsi_level;
        char buff[LMAX_DEVPATH];
        char wd[LMAX_PATH];
        char dev_node[LMAX_NAME];
        char value[LMAX_NAME];
        struct addr_hctl hctl;

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
        } else if (scsi_level == 0) {
                printf("ANSI SCSI revision: none\n");
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
tag_lun(const uint8_t * lunp, int * tag_arr)
{
        bool next_level;
        int k, a_method, bus_id, len_fld, e_a_method;
        uint8_t not_spec[2] = {0xff, 0xff};

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
                next_level = false;
                a_method = (lunp[0] >> 6) & 0x3;
                switch (a_method) {
                case 0:         /* peripheral device addressing method */
                        bus_id = lunp[0] & 0x3f;
                        if (bus_id)
                            next_level = true;
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

/* List one SCSI device (LU) on a line. */
static void
one_sdev_entry(const char * dir_name, const char * devname,
               const struct lsscsi_opts * op)
{
        bool get_wwn = false;
        int type, n, vlen;
        int devname_len = 13;
        char buff[LMAX_DEVPATH];
        char extra[LMAX_DEVPATH];
        char value[LMAX_NAME];
        char wd[LMAX_PATH];
        struct addr_hctl hctl;

        if (op->classic) {
                one_classic_sdev_entry(dir_name, devname, op);
                return;
        }
        vlen = sizeof(value);
        snprintf(buff, sizeof(buff), "%s/%s", dir_name, devname);
        if (op->lunhex && parse_colon_list(devname, &hctl)) {
                int sel_mask = 0xf;
                char b[80];

                sel_mask |= (1 == op->lunhex) ? 0x10 : 0x20;
                snprintf(value, vlen, "[%s]",
                         tuple2string(&hctl, sel_mask, sizeof(b), b));
                devname_len = 28;
        } else
                snprintf(value, vlen, "[%s]", devname);

        if ((int)strlen(value) >= devname_len)
                 printf("%s ", value);  /* if very long, append a space */
        else /* left justified with field length of devname_len */
                printf("%-*s", devname_len, value);
        if (op->pdt) {
                char b[16];

                if (get_value(buff, "type", value, vlen) &&
                    (1 == sscanf(value, "%d", &type)) &&
                    (type >= 0) && (type < 32))
                        snprintf(b, sizeof(b), "0x%x", type);
                else
                        snprintf(b, sizeof(b), "-1");
                printf("%-8s", b);
        } else if (op->brief)
                ;
        else if (! get_value(buff, "type", value, vlen)) {
                printf("type?   ");
        } else if (1 != sscanf(value, "%d", &type)) {
                printf("type??  ");
        } else if ((type < 0) || (type > 31)) {
                printf("type??? ");
        } else
                printf("%s ", scsi_short_device_types[type]);

        if (op->wwn)
                get_wwn = true;
        if (op->transport_info) {
                if (transport_tport(devname, op, vlen, value))
                        printf("%-30s  ", value);
                else
                        printf("                                ");
        } else if (op->unit) {
                get_lu_name(devname, value, vlen, op->unit > 3);
                n = strlen(value);
                if (n < 1)      /* left justified "none" means no lu name */
                        printf("%-32s  ", "none");
                else if (1 == op->unit) {
                        if (n < 33)
                                printf("%-32s  ", value);
                        else {
                                value[32] = '_';
                                value[33] = ' ';
                                value[34] = '\0';
                                printf("%-34s", value);
                        }
                } else if (2 == op->unit) {
                        if (n < 33)
                                printf("%-32s  ", value);
                        else {
                                value[n - 32] = '_';
                                printf("%-32s  ", value + n - 32);
                        }
                } else     /* -uuu, output in full, append rest of line */
                        printf("%-s  ", value);
        } else if (! op->brief) {
                if (get_value(buff, "vendor", value, vlen))
                        printf("%-8s ", value);
                else
                        printf("vendor?  ");

                if (get_value(buff, "model", value, vlen))
                        printf("%-16s ", value);
                else
                        printf("model?           ");

                if (get_value(buff, "rev", value, vlen))
                        printf("%-4s  ", value);
                else
                        printf("rev?  ");
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
                        char wwn_str[DISK_WWN_MAX_LEN];
                        enum dev_type typ;

                        typ = (FT_BLOCK == non_sg.ft) ? BLK_DEV : CHR_DEV;
                        if (get_wwn) {
                                if ((BLK_DEV == typ) &&
                                    get_disk_wwn(wd, wwn_str, sizeof(wwn_str)))
                                        printf("%-*s  ", DISK_WWN_MAX_LEN - 1,
                                               wwn_str);
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
                                if (get_value(wd, "dev", value, vlen))
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

        if (op->ssize) {
                uint64_t blk512s;
                char blkdir[LMAX_DEVPATH];

                my_strcopy(blkdir, buff, sizeof(blkdir));
                value[0] = 0;
                if (! ((0 == type) && block_scan(blkdir) &&
                       if_directory_chdir(blkdir, ".") &&
                       get_value(".", "size", value, vlen)) ) {
                        printf("  %6s", "-");
                        goto fini_line;
                }
                blk512s = atoll(value);

                if (op->ssize > 2) {
                        int lbs = 0;
                        char bb[32];

                        if (get_value(".", "queue/logical_block_size", bb,
                                      sizeof(bb))) {
                                lbs = atoi(bb);
                                if (lbs < 1)
                                        printf("  %12s,[lbs<1 ?]", value);
                                else if (512 == lbs)
                                        printf("  %12s%s", value,
                                               (op->ssize > 3) ? ",512" : "");
                                else {
                                        int64_t byts = 512 * blk512s;

                                        snprintf(value, vlen, "%" PRId64,
                                                 (byts / lbs));
                                        if (op->ssize > 3)
                                                printf("  %12s,%d", value,
                                                       lbs);
                                        else
                                                printf("  %12s", value);
                                }
                        } else
                                printf("  %12s,512", value);
                } else {
                        enum string_size_units unit_val = (0x1 & op->ssize) ?
                                         STRING_UNITS_10 : STRING_UNITS_2;

                        blk512s <<= 9;
                        if (blk512s > 0 &&
                            size2string(blk512s, unit_val, value, vlen))
                                printf("  %6s", value);
                        else
                                printf("  %6s", "-");
                }
        }

fini_line:
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
sdev_dir_scan_select(const struct dirent * s)
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
                                pr2serr("%s: parse failed\n", __func__);
                                return 0;
                        }
                        if (((-1 == filter.h) || (s_hctl.h == filter.h)) &&
                            ((-1 == filter.c) || (s_hctl.c == filter.c)) &&
                            ((-1 == filter.t) || (s_hctl.t == filter.t)) &&
                            ((UINT64_LAST == filter.l) ||
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

#if (HAVE_NVME && (! IGNORE_NVME))

/* List one NVMe namespace (NS) on a line. */
static void
one_ndev_entry(const char * nvme_ctl_abs, const char * nvme_ns_rel,
               const struct lsscsi_opts * op)
{
        int n, m;
        int cdev_minor = 0;
        int cntlid = 0;
        int vb = op->verbose;
        int devname_len = 13;
        int sel_mask = 0xf;
        uint32_t nsid = 0;
        char * cp;
        char buff[LMAX_DEVPATH];
        char value[LMAX_NAME];
        char dev_node[LMAX_NAME + 16] = "";
        char wd[LMAX_PATH];
        char devname[64];
        char ctl_model[48];
        char b[80];
        const int vlen = sizeof(value);
        struct addr_hctl hctl;

        snprintf(buff, sizeof(buff), "%s/%s", nvme_ctl_abs, nvme_ns_rel);
        if ((0 == strncmp(nvme_ns_rel, "nvme", 4)) &&
            (1 == sscanf(nvme_ns_rel + 4, "%d", &cdev_minor)))
                ;
        else if (vb)
                pr2serr("%s: unable to find %s in %s\n", __func__,
                        "cdev_minor", nvme_ns_rel);

        if (get_value(nvme_ctl_abs, "cntlid", value, vlen)) {
                if (1 != sscanf(value, "%d", &cntlid)) {
                        if (vb)
                                pr2serr("%s: trying to decode: %s as "
                                        "cntlid\n", __func__, value);
                }
                if (filter_active && (-1 != filter.t) && (cntlid != filter.t))
                        return;         /* doesn't meet filter condition */
        } else if (vb)
                pr2serr("%s: unable to find %s under %s\n", __func__,
                        "cntlid", nvme_ctl_abs);

#ifdef __cplusplus
        cp = strrchr((char *)nvme_ns_rel, 'n');
#else
        cp = strrchr(nvme_ns_rel, 'n');
#endif
        if ((NULL == cp) || ('v' == *(cp + 1)) ||
            (1 != sscanf(cp + 1, "%u", &nsid))) {
                if (vb)
                        pr2serr("%s: unable to find nsid in %s\n", __func__,
                                nvme_ns_rel);
        }
        mk_nvme_tuple(&hctl, cdev_minor, cntlid, nsid);

        if (op->lunhex) {
                sel_mask |= (1 == op->lunhex) ? 0x10 : 0x20;
                devname_len = 28;
        }
        snprintf(value, vlen, "[%s]",
                 tuple2string(&hctl, sel_mask, sizeof(devname), devname));

        if ((int)strlen(value) >= devname_len)
                printf("%s ", value);  /* if very long, append a space */
        else /* left justified with field length of devname_len */
                printf("%-*s", devname_len, value);

        if (op->pdt)
                printf("%-8s", "0x0");
        else if (op->brief)
                ;
        else if (vb) /* NVMe namespace can only be NVM device */
                printf("dsk/nvm ");
        else
                printf("disk    ");


        if (op->wwn) {
                if (get_value(buff, "wwid", value, vlen))
                        printf("%-41s  ", value);
                else
                        printf("%-41s  ", "wwid?");
        } else if (op->transport_info) {
                if (get_value(buff, "device/transport", value, vlen)) {
                        const char * svp = "device/device/subsystem_vendor";
                        const char * sdp = "device/device/subsystem_device";
                        char bb[80];

                        if (0 == strcmp("pcie" , value)) {

                                if (get_value(buff, svp, b, sizeof(b)) &&
                                    get_value(buff, sdp, bb, sizeof(bb))) {
                                        snprintf(value , vlen, "pcie %s:%s",
                                                 b, bb);
                                        printf("%-41s  ", value);
                                } else
                                        printf("%-41s  ", "transport?");
                        } else
                                printf("%-41s  ", value);
                } else
                        printf("%-41s  ", "transport?");
        } else if (op->unit) {
                if (get_value(buff, "wwid", value, vlen)) {
                        if ((op->unit < 4) &&
                            (0 == strncmp("eui.", value, 4)))
                                printf("%-41s  ", value + 4);
                        else
                                printf("%-41s  ", value);
                } else
                        printf("%-41s  ", "wwid?");
        } else if (! op->brief) {
                if (! get_value(nvme_ctl_abs, "model", ctl_model,
                                sizeof(ctl_model)))
                        snprintf(ctl_model, sizeof(ctl_model), "-    ");
                n = trim_lead_trail(ctl_model, true, true);
                snprintf(b, sizeof(b), "__%u", nsid);
                m = strlen(b);
                if (n > (41 - m))
                        memcpy(ctl_model + 41 - m, b, m + 1);
                else
                        strcat(ctl_model, b);
                printf("%-41s  ", ctl_model);
        }

        if (op->kname)
                snprintf(dev_node, sizeof(dev_node), "%s/%s",
                         dev_dir, nvme_ns_rel);
        else if (! get_dev_node(buff, dev_node, BLK_DEV))
                snprintf(dev_node, sizeof(dev_node), "-       ");

        printf("%-9s", dev_node);
        if (op->dev_maj_min) {
                if (get_value(buff, "dev", value, vlen))
                        printf(" [%s]", value);
                else
                        printf(" [dev?]");
        }

        if (op->ssize) {
                uint64_t blk512s;

                if (! get_value(buff, "size", value, vlen)) {
                        printf("  %6s", "-");
                        goto fini_line;
                }
                blk512s = atoll(value);
                /* tabbing 8 should be banned, use goto to win some space */
                if (op->ssize > 2) {
                        int lbs = 0;
                        char bb[32];

                        if (get_value(buff, "queue/logical_block_size", bb,
                                      sizeof(bb))) {
                                lbs = atoi(bb);
                                if (lbs < 1)
                                        printf("  %12s,[lbs<1 ?]", value);
                                else if (512 == lbs)
                                        printf("  %12s%s", value,
                                               (op->ssize > 3) ? ",512" : "");
                                else {
                                        int64_t byts = 512 * blk512s;

                                        snprintf(value, vlen, "%" PRId64,
                                                 (byts / lbs));
                                        if (op->ssize > 3)
                                                printf("  %12s,%d", value,
                                                       lbs);
                                        else
                                                printf("  %12s", value);
                                }
                        } else
                                printf("  %12s,512", value);
                } else {
                        enum string_size_units unit_val = (0x1 & op->ssize) ?
                                         STRING_UNITS_10 : STRING_UNITS_2;

                        blk512s <<= 9;
                        if (blk512s > 0 &&
                            size2string(blk512s, unit_val, value, vlen))
                                printf("  %6s", value);
                        else
                                printf("  %6s", "-");
                }
        }

fini_line:
        printf("\n");
        if (op->long_opt > 0)
                longer_nd_entry(buff, devname, op);
        if (vb > 0) {
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
ndev_dir_scan_select(const struct dirent * s)
{
        int cdev_minor; /* /dev/nvme<n> char device minor */

        if ((0 == strncmp(s->d_name, "nvme", 4)) &&
            (1 == sscanf(s->d_name + 4, "%d", &cdev_minor))) {
                if (filter_active) {
                        if (((-1 == filter.h) ||
                             (NVME_HOST_NUM == filter.h)) &&
                            ((-1 == filter.c) || (cdev_minor == filter.c)))
                                return 1;
                        else
                                return 0;
                } else
                        return 1;
        }
        return 0;
}

static int
ndev_dir_scan_select2(const struct dirent * s)
{
        int cdev_minor;
        uint32_t nsid;
        char * cp;

        /* What to do about NVMe controller CNTLID field? */
        if (strncmp(s->d_name, "nvme", 4))
                return 0;
#ifdef __cplusplus
        cp = strchr((char *)s->d_name + 4, 'n');
#else
        cp = strchr(s->d_name + 4, 'n');
#endif
        if (NULL == cp)
                return 0;
        if ((1 == sscanf(s->d_name + 4, "%d", &cdev_minor)) &&
            (1 == sscanf(cp + 1, "%u", &nsid))) {
                if (filter_active) {    /* filter cntlid (.t) in caller */
                        if (((-1 == filter.h) ||
                             (NVME_HOST_NUM == filter.h)) &&
                            ((-1 == filter.c) || (cdev_minor == filter.c)) &&
                      /*    ((-1 == filter.t) || (s_hctl.t == filter.t)) && */
                            ((UINT64_LAST == filter.l) || (nsid == filter.l)))
                                return 1;
                        else
                                return 0;
                } else
                        return 1;
        }
        return 0;
}

static void
one_nhost_entry(const char * dir_name, const char * nvme_ctl_rel,
                const struct lsscsi_opts * op)
{
        int vlen;
        int vb = op->verbose;
        uint32_t cdev_minor;
        const char * nullname1 = "<NULL>";
        const char * nullname2 = "(null)";
        char buff[LMAX_DEVPATH];
        char value[LMAX_DEVPATH];
        char wd[LMAX_PATH];
        char b[80];
        char bb[80];

        vlen = sizeof(value);
        if (1 == sscanf(nvme_ctl_rel, "nvme%u", &cdev_minor))
                printf("[N:%u]  ", cdev_minor);
        else
                printf("[N:?]  ");
        snprintf(buff, sizeof(buff), "%s%s", dir_name, nvme_ctl_rel);

        if (op->kname)
                snprintf(value, vlen, "%s/%s", dev_dir, nvme_ctl_rel);
        else if (! get_dev_node(buff, value, CHR_DEV))
                snprintf(value, vlen, "-       ");
        printf("%-9s", value);
        if (op->dev_maj_min) {
                char * bp;

                bp = name_eq2value(buff, "uevent", "MAJOR", sizeof(b), b);
                if (strlen(bp) > 1)
                        printf(" [%s:%s]", bp, name_eq2value(buff, "uevent",
                                                 "MINOR", sizeof(bb), bb));
                else
                        printf(" [dev?]");
        }
        if (op->transport_info) {
                const char * svp = "device/subsystem_vendor";
                const char * sdp = "device/subsystem_device";

                printf("    ");
                if (get_value(buff, "transport", value, vlen)) {
                        if (0 == strcmp("pcie" , value)) {
                                if (get_value(buff, svp, b, sizeof(b)) &&
                                    get_value(buff, sdp, bb, sizeof(bb)))
                                        printf("pcie %s:%s", b, bb);
                                else
                                        printf("pcie ?:?");
                        } else
                                printf("%s%s\n", (vb ? "transport=" : ""),
                                       value);
                } else if (vb)
                        printf("transport=?\n");
                printf("\n");
        } else if (op->wwn) {
                if (get_value(buff, "subsysnqn", value, vlen))
                        printf("   %s%s\n", (vb ? "subsysnqn=" : ""),
                              value);
                else if (vb)
                        printf("subsysnqn=?\n");
        } else if (op->unit) {
                if (get_value(buff, "device/subsystem_vendor", value, vlen)) {
                        printf("   %s%s:", (vb ? "vin=" : ""), value);
                        if (get_value(buff, "device/subsystem_device", value,
                            vlen))
                                printf("%s\n", value);
                        else
                                printf("??\n");
                } else if (vb)
                        printf("subsystem_vendor=?\n");
        } else if (op->long_opt > 0) {
                bool sing = (op->long_opt > 2);
                const char * sep = sing ? "\n" : "";

                if (get_value(buff, "cntlid", value, vlen))
                        printf("%s  cntlid=%s%s", sep, value, sep);
                else if (vb)
                        printf("%s  cntlid=?%s", sep, sep);
                if (get_value(buff, "state", value, vlen))
                        printf("  state=%s%s", value, sep);
                else if (vb)
                        printf("  state=?%s", sep);
                if (get_value(buff, "device/current_link_width", value, vlen))
                        printf("  current_link_width=%s%s", value, sep);
                else if (vb)
                        printf("  current_link_width=?%s", sep);
                if (get_value(buff, "firmware_rev", value, vlen))
                        printf("  firmware_rev=%s%s", value, sep);
                else if (vb)
                        printf("  firmware_rev=?%s", sep);
                if (! sing)
                        printf("\n");
                if (op->long_opt > 1) {
                        if (get_value(buff, "device/current_link_speed",
                                      value, vlen))
                                printf("  current_link_speed=%s%s", value,
                                       sep);
                        else if (vb)
                                printf("  current_link_speed=?%s", sep);
                        if (get_value(buff, "model", value, vlen)) {
                                trim_lead_trail(value, true, true);
                                printf("  model=%s%s", value, sep);
                        } else if (vb)
                                printf("  model=?%s", sep);
                        if (get_value(buff, "serial", value, vlen)) {
                                trim_lead_trail(value, true, true);
                                printf("  serial=%s%s", value, sep);
                        } else if (vb)
                                printf("  serial=?%s", sep);
                        if (! sing)
                                printf("\n");
                }
        } else if (! op->brief) {
                if (get_value(buff, "model", value, vlen) &&
                    strncmp(value, nullname1, 6) &&
                    strncmp(value, nullname2, 6)) {
                        trim_lead_trail(value, true, true);
                        trunc_pad2n(value, 32, true);
                } else
                        strcpy(value, nullname1);
                printf("  %-32s ", value);

                if (get_value(buff, "serial", value, vlen) &&
                    strncmp(value, nullname1, 6) &&
                    strncmp(value, nullname2, 6)) {
                        trim_lead_trail(value, true, true);
                        trunc_pad2n(value, 18, true);
                } else
                        strcpy(value, nullname1);
                printf(" %-18s ", value);

                if (get_value(buff, "firmware_rev", value, vlen) &&
                    strncmp(value, nullname1, 6) &&
                    strncmp(value, nullname2, 6)) {
                        trim_lead_trail(value, true, true);
                        trunc_pad2n(value, 8, false);
                } else
                        strcpy(value, nullname1);
                printf(" %-8s\n", value);
        } else
                printf("\n");
        if (vb > 0) {
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

#endif          /* (HAVE_NVME && (! IGNORE_NVME)) */

/* This is a compare function for numeric sort based on hctl tuple.
 * Returns -1 if (a->d_name < b->d_name) ; 0 if they are equal
 * and 1 otherwise. */
static int
sdev_scandir_sort(const struct dirent ** a, const struct dirent ** b)
{
        const char * lnam = (*a)->d_name;
        const char * rnam = (*b)->d_name;
        struct addr_hctl left_hctl;
        struct addr_hctl right_hctl;

        if (! parse_colon_list(lnam, &left_hctl)) {
                pr2serr("%s: left parse failed: %.20s\n", __func__,
                        (lnam ? lnam : "<null>"));
                return -1;
        }
        if (! parse_colon_list(rnam, &right_hctl)) {
                pr2serr("%s: right parse failed: %.20s\n", __func__,
                        (rnam ? rnam : "<null>"));
                return 1;
        }
        return cmp_hctl(&left_hctl, &right_hctl);
}

#if (HAVE_NVME && (! IGNORE_NVME))

/* This is a compare function for numeric sort based on hctl tuple. Similar
 * to sdev_scandir_sort() but converts entries like "nvme2" into a hctl tuple.
 * Returns -1 if (a->d_name < b->d_name) ; 0 if they are equal
 * and 1 otherwise. */
static int
nhost_scandir_sort(const struct dirent ** a, const struct dirent ** b)
{
        const char * lnam = (*a)->d_name;
        const char * rnam = (*b)->d_name;
        struct addr_hctl left_hctl;
        struct addr_hctl right_hctl;

        if (strchr(lnam, ':')) {
                if (! parse_colon_list(lnam, &left_hctl)) {
                        pr2serr("%s: left parse failed: %.20s\n", __func__,
                                (lnam ? lnam : "<null>"));
                        return -1;
                }
        } else {
                if (1 == sscanf(lnam, "nvme%d", &left_hctl.c)) {
                        left_hctl.h = NVME_HOST_NUM;
                        left_hctl.t = 0;
                        left_hctl.l = 0;
                } else {
                        pr2serr("%s: left sscanf failed: %.20s\n", __func__,
                                (lnam ? lnam : "<null>"));
                        return -1;
                }
        }
        if (strchr(rnam, ':')) {
                if (! parse_colon_list(rnam, &right_hctl)) {
                        pr2serr("%s: right parse failed: %.20s\n", __func__,
                                (rnam ? rnam : "<null>"));
                        return 1;
                }
        } else {
                if (1 == sscanf(rnam, "nvme%d", &right_hctl.c)) {
                        right_hctl.h = NVME_HOST_NUM;
                        right_hctl.t = 0;
                        right_hctl.l = 0;
                } else {
                        pr2serr("%s: right sscanf failed: %.20s\n", __func__,
                                (rnam ? rnam : "<null>"));
                        return -1;
                }
        }
        return cmp_hctl(&left_hctl, &right_hctl);
}

#endif          /* (HAVE_NVME && (! IGNORE_NVME)) */

/* List SCSI devices (LUs). */
static void
list_sdevices(const struct lsscsi_opts * op)
{
        int num, k;
        struct dirent ** namelist;
        char buff[LMAX_DEVPATH];
        char name[LMAX_NAME];

        snprintf(buff, sizeof(buff), "%s%s", sysfsroot, bus_scsi_devs);

        num = scandir(buff, &namelist, sdev_dir_scan_select,
                      sdev_scandir_sort);
        if (num < 0) {  /* scsi mid level may not be loaded */
                if (op->verbose > 0) {
                        snprintf(name, sizeof(name), "%s: scandir: %s",
                                 __func__, buff);
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

#if (HAVE_NVME && (! IGNORE_NVME))

/* List NVME devices (namespaces). */
static void
list_ndevices(const struct lsscsi_opts * op)
{
        int num, num2, k, j;
        struct dirent ** name_list;
        struct dirent ** namelist2;
        char buff[LMAX_DEVPATH];
        char buff2[LMAX_DEVPATH];
        char ebuf[120];

        snprintf(buff, sizeof(buff), "%s%s", sysfsroot, class_nvme);

        num = scandir(buff, &name_list, ndev_dir_scan_select,
                      nhost_scandir_sort);
        if (num < 0) {  /* NVMe module may not be loaded */
                if (op->verbose > 0) {
                        snprintf(ebuf, sizeof(ebuf), "%s: scandir: %s",
                                 __func__, buff);
                        perror(ebuf);
                        printf("NVMe module may not be loaded\n");
                }
                return;
        }
        for (k = 0; k < num; ++k) {
                snprintf(buff2, sizeof(buff2), "%s%s", buff,
                         name_list[k]->d_name);
                free(name_list[k]);
                num2 = scandir(buff2, &namelist2, ndev_dir_scan_select2,
                               sdev_scandir_sort);
                if (num2 < 0) {
                        if (op->verbose > 0) {
                                snprintf(ebuf, sizeof(ebuf), "%s: scandir"
                                         "(2): %s", __func__, buff);
                                perror(ebuf);
                        }
                        /* already freed name_list[k] so move to next */
                        ++k;
                        break;
                }
                for (j = 0; j < num2; ++j) {
                        transport_id = TRANSPORT_UNKNOWN;
                        one_ndev_entry(buff2, namelist2[j]->d_name, op);
                        free(namelist2[j]);
                }
                free(namelist2);
        }
        for ( ; k < num; ++k) /* clean out rest of name_list[] */
                free(name_list[k]);

        free(name_list);
        if (op->wwn)
                free_disk_wwn_node_list();
}

#endif          /* (HAVE_NVME && (! IGNORE_NVME)) */

/* List host (initiator) attributes when --long given (one or more times). */
static void
longer_h_entry(const char * path_name, const struct lsscsi_opts * op)
{
        char value[LMAX_NAME];

        if (op->transport_info) {
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
               const struct lsscsi_opts * op)
{
        unsigned int host_id;
        const char * nullname1 = "<NULL>";
        const char * nullname2 = "(null)";
        char buff[LMAX_DEVPATH];
        char value[LMAX_NAME];
        char wd[LMAX_PATH];

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
        if (op->transport_info) {
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
host_dir_scan_select(const struct dirent * s)
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
 */
static int
shost_scandir_sort(const struct dirent ** a, const struct dirent ** b)
{
        unsigned int l, r;
        const char * lnam = (*a)->d_name;
        const char * rnam = (*b)->d_name;

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
list_shosts(const struct lsscsi_opts * op)
{
        int num, k;
        struct dirent ** namelist;
        char buff[LMAX_DEVPATH];
        char name[LMAX_NAME];

        snprintf(buff, sizeof(buff), "%s%s", sysfsroot, scsi_host);

        num = scandir(buff, &namelist, host_dir_scan_select,
                      shost_scandir_sort);
        if (num < 0) {
                snprintf(name, sizeof(name), "%s: scandir: %s",
                         __func__, buff);
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

#if (HAVE_NVME && (! IGNORE_NVME))

/* List NVME hosts (controllers). */
static void
list_nhosts(const struct lsscsi_opts * op)
{
        int num, k;
        struct dirent ** namelist;
        char buff[LMAX_DEVPATH];
        char ebuf[120];

        snprintf(buff, sizeof(buff), "%s%s", sysfsroot, class_nvme);

        num = scandir(buff, &namelist, ndev_dir_scan_select,
                      nhost_scandir_sort);
        if (num < 0) {  /* NVMe module may not be loaded */
                if (op->verbose > 0) {
                        snprintf(ebuf, sizeof(ebuf), "%s: scandir: %s",
                                 __func__, buff);
                        perror(ebuf);
                        printf("NVMe module may not be loaded\n");
                }
                return;
        }
        for (k = 0; k < num; ++k) {
                transport_id = TRANSPORT_UNKNOWN;
                one_nhost_entry(buff, namelist[k]->d_name, op);
                free(namelist[k]);
        }
        free(namelist);
        if (op->wwn)
                free_disk_wwn_node_list();
}

#endif          /* (HAVE_NVME && (! IGNORE_NVME)) */

/* Return true if able to decode, otherwise false */
static bool
one_filter_arg(const char * arg, struct addr_hctl * filtp)
{
        int val, k, n, res;
        uint64_t val64;
        const char * cp;
        const char * cpe;
        char buff[64];

        cp = arg;
        while ((*cp == ' ') || (*cp == '\t') || (*cp == '['))
                ++cp;
        if ('\0' == *cp)
                return true;
        for (k = 0; *cp; cp = cpe + 1, ++k) {
                cpe = strchr(cp, ':');
                if (cpe)
                        n = cpe - cp;
                else {
                        n = strlen(cp);
                        cpe = cp + n - 1;
                }
                val = -1;
                val64 = UINT64_LAST;
                if (n > ((int)sizeof(buff) - 1)) {
                        pr2serr("intermediate sting in %s too long (n=%d)\n",
                                arg, n);
                        return false;
                }
                if ((n > 0) && ('-' != *cp) && ('*' != *cp) && ('?' != *cp)) {
                        memcpy(buff, cp, n);
                        buff[n] = '\0';
                        if (3 == k) {
                                if (('0' == buff[0]) &&
                                    ('X' == toupper((uint8_t)buff[1])))
                                        res = sscanf(buff, "%" SCNx64 ,
                                                     &val64);
                                else
                                        res = sscanf(buff, "%" SCNu64 ,
                                                     &val64);
                        } else {
                                res = sscanf(buff, "%d", &val);
                                if ((0 == res) && (0 == k) && (1 == n) &&
                                    ('N' == toupper((uint8_t)buff[0]))) {
                                        /* take 'N' as NVMe indication */
                                        res = 1;
                                        val = NVME_HOST_NUM;
                                }
                        }
                        if ((1 != res) && (NULL == strchr(buff, ']'))) {
                                        ;
                                pr2serr("cannot decode %s as an integer\n",
                                        buff);
                                return false;
                        }
                }
                switch (k) {
                case 0: filtp->h = val; break;
                case 1: filtp->c = val; break;
                case 2: filtp->t = val; break;
                case 3: filtp->l = val64; break;
                default:
                        pr2serr("expect three colons at most in %s\n", arg);
                        return false;
                }
        }
        return true;
}

/* Return true if able to decode, otherwise false */
static bool
decode_filter_arg(const char * a1p, const char * a2p, const char * a3p,
                  const char * a4p, struct addr_hctl * filtp)
{
        int n, rem;
        char * b1p;
        char b1[256];

        if ((NULL == a1p) || (NULL == filtp)) {
                pr2serr("bad call to decode_filter\n");
                return false;
        }
        filtp->h = -1;
        filtp->c = -1;
        filtp->t = -1;
        filtp->l = UINT64_LAST;
        if ((0 == strncmp("host", a1p, 4)) &&
            (1 == sscanf(a1p, "host%d", &n)) && ( n >= 0)) {
                filtp->h = n;
                return true;
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
        pr2serr("filter arguments exceed internal buffer size (%d)\n",
                (int)sizeof(b1));
        return false;
}


int
main(int argc, char **argv)
{
        bool do_sdevices = true;
        bool do_hosts = false;  /* checked before do_sdevices */
        int c;
        int version_count = 0;
        const char * cp;
        struct lsscsi_opts * op;
        struct lsscsi_opts opts;

        op = &opts;
        cp = getenv("LSSCSI_LUNHEX_OPT");
        invalidate_hctl(&filter);
        memset(op, 0, sizeof(opts));
        while (1) {
                int option_index = 0;

                c = getopt_long(argc, argv, "bcCdDghHiklLNpPsStuUvVwxy:",
                                long_options, &option_index);
                if (c == -1)
                        break;

                switch (c) {
                case 'b':
                        op->brief = true;
                        break;
                case 'c':
                        op->classic = true;
                        break;
                case 'C':       /* synonym for --hosts, NVMe perspective */
                        do_hosts = true;
                        break;
                case 'd':
                        op->dev_maj_min = true;
                        break;
                case 'D':       /* --pdt */
                        op->pdt = true;
                        break;
                case 'g':
                        op->generic = true;
                        break;
                case 'h':
                        usage();
                        return 0;
                case 'H':
                        do_hosts = true;
                        break;
                case 'i':
                        op->scsi_id = true;
                        break;
                case 'k':
                        op->kname = true;
                        break;
                case 'l':
                        ++op->long_opt;
                        break;
                case 'L':
                        op->long_opt += 3;
                        break;
                case 'N':
                        op->no_nvme = true;
                        break;
                case 'p':
                        op->protection = true;
                        break;
                case 'P':
                        op->protmode = true;
                        break;
                case 's':
                        ++op->ssize;
                        break;
                case 'S':
                        op->ssize += 3;
                        break;
                case 't':
                        op->transport_info = true;
                        break;
                case 'u':
                        ++op->unit;
                        break;
                case 'U':
                        op->unit += 3;
                        break;
                case 'v':
                        ++op->verbose;
                        break;
                case 'V':
                        ++version_count;
                        break;
                case 'w':
                        op->wwn = true;
                        break;
                case 'x':
                        ++op->lunhex;
                        break;
                case 'y':       /* sysfsroot <dir> */
                        sysfsroot = optarg;
                        break;
                case '?':
                        usage();
                        return 1;
                default:
                        pr2serr("?? getopt returned character code 0x%x ??\n",
                                c);
                        usage();
                        return 1;
               }
        }
        if (version_count > 0) {
                int yr, mon, day;
                char * p;
                char b[64];

                if (1 == version_count) {
                        pr2serr("version: %s\n", version_str);
                        return 0;
                }
                cp = strchr(version_str, '/');
                if (cp && (3 == sscanf(cp - 4, "%d/%d/%d", &yr, &mon, &day)))
                    ;
                else {
                        pr2serr("version:: %s\n", version_str);
                        return 0;
                }
                strncpy(b, version_str, sizeof(b) - 1);
                p = (char *)strchr(b, '/');
                snprintf(p - 4, sizeof(b) - (p - 4 - b), "%d%02d%02d  ",
                         yr, mon, day);
                b[strlen(b)] = ' ';
                printf("%s\n", b);
                return 0;
        }

        if (optind < argc) {
                const char * a1p = NULL;
                const char * a2p = NULL;
                const char * a3p = NULL;
                const char * a4p = NULL;

                if ((optind + 4) < argc) {
                        pr2serr("unexpected non-option arguments: ");
                        while (optind < argc)
                                pr2serr("%s ", argv[optind++]);
                        pr2serr("\n");
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
                if ((0 == memcmp("host", a1p, 4)) ||
                    (0 == memcmp("HOST", a1p, 4))) {
                        if (! decode_filter_arg(a1p + 4, a2p, a3p, a4p,
                                                &filter))
                                return 1;
                } else {
                        if (! decode_filter_arg(a1p, a2p, a3p, a4p, &filter))
                                return 1;
                }
                if ((filter.h != -1) || (filter.c != -1) ||
                    (filter.t != -1) || (filter.l != UINT64_LAST))
                        filter_active = true;
        }
        if ((0 == op->lunhex) && cp) {
                if (1 == sscanf(cp, "%d", &c))
                        op->lunhex = c;
        }
        if (op->transport_info && op->unit) {
                pr2serr("use '--transport' or '--unit' but not both\n");
                return 1;
        }
        if (op->transport_info &&
            ((1 == op->long_opt) || (2 == op->long_opt))) {
                pr2serr("please use '--list' (rather than '--long') with "
                        "--transport\n");
                return 1;
        }
        if (op->unit) {
                if (do_hosts)
                        pr2serr("--unit ignored when --hosts given\n");
                if ((1 == op->long_opt) || (2 == op->long_opt)) {
                        pr2serr("please use '--list' (rather than '--long') "
                                "with " "--unit\n");
                        return 1;
                }
        }
        if (op->verbose > 1) {
                printf(" sysfsroot: %s\n", sysfsroot);
        }
        if (do_hosts) {
                list_shosts(op);
#if (HAVE_NVME && (! IGNORE_NVME))
                if (! op->no_nvme)
                        list_nhosts(op);
#endif
        } else if (do_sdevices) {
                list_sdevices(op);
#if (HAVE_NVME && (! IGNORE_NVME))
                if (! op->no_nvme)
                        list_ndevices(op);
#endif
        }

        free_dev_node_list();

        return 0;
}
