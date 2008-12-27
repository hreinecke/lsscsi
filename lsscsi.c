/* This is a utility program for listing SCSI devices and hosts (HBAs)
 * in the Linux operating system. It is applicable to kernel versions
 * 2.6.1 and greater.
 *  Copyright (C) 2003-2008 D. Gilbert
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 */

#define _XOPEN_SOURCE 500
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <libgen.h>
#include <linux/major.h>
#include <time.h>

static const char * version_str = "0.22  2008/12/26";

#define NAME_LEN_MAX 260
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

static int transport_id = TRANSPORT_UNKNOWN;


static char sysfsroot[NAME_LEN_MAX];
static const char * sysfs_name = "sysfs";
static const char * sysfs_test_dir = "/sys/class";
static const char * sysfs_test_top = "/sys";
static const char * proc_mounts = "/proc/mounts";
static const char * bus_scsi_devs = "/bus/scsi/devices";
static const char * class_scsi_dev = "/class/scsi_device/";
static const char * scsi_host = "/class/scsi_host/";
static const char * spi_host = "/class/spi_host/";
static const char * spi_transport = "/class/spi_transport/";
static const char * sas_host = "/class/sas_host/";
static const char * sas_phy = "/class/sas_phy/";
static const char * sas_device = "/class/sas_device/";
static const char * sas_end_device = "/class/sas_end_device/";
static const char * fc_host = "/class/fc_host/";
static const char * fc_transport = "/class/fc_transport/";
static const char * iscsi_host = "/class/iscsi_host/";
static const char * iscsi_session = "/class/iscsi_session/";
static const char * dev_dir = "/dev";


struct addr_hctl {
        int h;
        int c;
        int t;
        int l;
};

struct addr_hctl filter;
static int filter_active = 0;

struct lsscsi_opt_coll {
        int long_opt;           /* --long */
        int classic;
        int generic;
        int dev_maj_min;        /* --device */
        int kname;
        int transport;
        int verbose;
        int protection;         /* data integrity */
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

static struct option long_options[] = {
        {"classic", 0, 0, 'c'},
        {"device", 0, 0, 'd'},
        {"generic", 0, 0, 'g'},
        {"help", 0, 0, 'h'},
        {"hosts", 0, 0, 'H'},
        {"kname", 0, 0, 'k'},
        {"long", 0, 0, 'l'},
        {"list", 0, 0, 'L'},
/*      {"name", 0, 0, 'n'},    */
        {"protection", 0, 0, 'p'},
/*      {"sysfsroot", 1, 0, 'y'},       */
        {"transport", 0, 0, 't'},
        {"verbose", 0, 0, 'v'},
        {"version", 0, 0, 'V'},
        {0, 0, 0, 0}
};


/* Device node list: contains the information needed to match a node with a
   sysfs class device. */
#define DEV_NODE_LIST_ENTRIES 16
enum dev_type { BLK_DEV, CHR_DEV};

struct dev_node_list {
       struct dev_node_list *next;
       unsigned int count;
       struct dev_node_entry {
               unsigned int maj, min;
               enum dev_type type;
               time_t mtime;
               char name [ NAME_MAX + 1];
       } nodes[DEV_NODE_LIST_ENTRIES];
};
static struct dev_node_list* dev_node_listhead = NULL;

struct item_t {
        char name[NAME_LEN_MAX];
        int ft;
        int d_type;
};

static struct item_t non_sg;
static struct item_t aa_sg;
static struct item_t aa_first;
static struct item_t aa_sd;
static struct item_t aa_block;

static char sas_low_phy[NAME_LEN_MAX];
static char sas_hold_end_device[NAME_LEN_MAX];

static const char * iscsi_dir_name;
static const struct addr_hctl * iscsi_target_hct;
static int iscsi_tsession_num;


static void
usage()
{
        fprintf(stderr, "Usage: lsscsi   [--classic] [--device] [--generic]"
                        " [--help] [--hosts]\n"
                        "\t\t[--kname] [--list] [--long] [--protection] "
                        "[--transport]\n"
                        "\t\t[--verbose] [--version] [<h:c:t:l>]\n");
        fprintf(stderr, "  where:\n");
        fprintf(stderr, "    --classic|-c      alternate output similar "
                        "to 'cat /proc/scsi/scsi'\n");
        fprintf(stderr, "    --device|-d       show device node's major + "
                        "minor numbers\n");
        fprintf(stderr, "    --generic|-g      show scsi generic device "
                        "name\n");
        fprintf(stderr, "    --help|-h         this usage information\n");
        fprintf(stderr, "    --hosts|-H        lists scsi hosts rather than "
                        "scsi devices\n");
        fprintf(stderr, "    --kname|-k        show kernel name instead of "
                        "device node name\n");
        fprintf(stderr, "    --list|-L         additional information "
                        "output one\n");
        fprintf(stderr, "                      attribute=value per line\n");
        fprintf(stderr, "    --long|-l         additional information "
                        "output\n");
        fprintf(stderr, "    --protection|-p   show data integrity "
                        "(protection) information\n");
        fprintf(stderr, "    --transport|-t    transport information for "
                        "target or, if '--hosts'\n"
                        "                      given, for initiator\n");
        fprintf(stderr, "    --verbose|-v      output path names where data "
                        "is found\n");
        fprintf(stderr, "    --version|-V      output version string and "
                        "exit\n");
        fprintf(stderr, "    <h:c:t:l>         filter output list (def: "
                        "'- - - -' (all))\n\n");
        fprintf(stderr, "List SCSI devices or hosts, optionally with "
                "additional information\n");
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
                p->l = -1;
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
        strncpy(aa_first.name, s->d_name, NAME_LEN_MAX);
        aa_first.ft = FT_CHAR;  /* dummy */
        aa_first.d_type =  s->d_type;
        return 1;
}

/* Return 1 for directory entry that is link or directory (other than a
 * directory name starting with dot) that contains "block". Else return 0.
 */
static int
block_scandir_select(const struct dirent * s)
{
        if ((DT_LNK != s->d_type) &&
            ((DT_DIR != s->d_type) || ('.' == s->d_name[0])))
                return 0;
        if (strstr(s->d_name, "block")){
                strncpy(aa_block.name, s->d_name, NAME_LEN_MAX);
                aa_block.ft = FT_CHAR;  /* dummy */
                aa_block.d_type =  s->d_type;
        }
        return 1;
}

/* scan for scsi_disk directory in  /sys/bus/scsi/devices/<h:c:i:l> */
static int
block_scan(const char * dir_name, const struct lsscsi_opt_coll * opts)
{
        char name[NAME_LEN_MAX];
        struct dirent ** namelist;
        int num, k;

        num = scandir(dir_name, &namelist, block_scandir_select, NULL);
        if (num < 0) {
                if (opts->verbose > 0) {
                        snprintf(name, NAME_LEN_MAX, "scandir: %s", dir_name);
                        perror(name);
                }
                return -1;
        }
        for (k = 0; k < num; ++k)
                free(namelist[k]);
        free(namelist);
        return num;
}

static int
sd_scandir_select(const struct dirent * s)
{
        if ((DT_LNK != s->d_type) &&
            ((DT_DIR != s->d_type) || ('.' == s->d_name[0])))
                return 0;
        if (strstr(s->d_name, "scsi_disk")){
                strncpy(aa_sd.name, s->d_name, NAME_LEN_MAX);
                aa_sd.ft = FT_CHAR;  /* dummy */
                aa_sd.d_type =  s->d_type;
        }
        return 1;
}

static int
sd_scan(const char * dir_name, const struct lsscsi_opt_coll * opts)
{
        char name[NAME_LEN_MAX];
        struct dirent ** namelist;
        int num, k;

        num = scandir(dir_name, &namelist, sd_scandir_select, NULL);
        if (num < 0) {
                if (opts->verbose > 0) {
                        snprintf(name, NAME_LEN_MAX, "scandir: %s", dir_name);
                        perror(name);
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
scan_for_first(const char * dir_name, const struct lsscsi_opt_coll * opts)
{
        char name[NAME_LEN_MAX];
        struct dirent ** namelist;
        int num, k;

        aa_first.ft = FT_OTHER;
        num = scandir(dir_name, &namelist, first_scandir_select, NULL);
        if (num < 0) {
                if (opts->verbose > 0) {
                        snprintf(name, NAME_LEN_MAX, "scandir: %s", dir_name);
                        perror(name);
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
                strncpy(non_sg.name, s->d_name, NAME_LEN_MAX);
                non_sg.ft = FT_CHAR;
                non_sg.d_type =  s->d_type;
                return 1;
        } else if (0 == strncmp("block", s->d_name, 5)) {
                strncpy(non_sg.name, s->d_name, NAME_LEN_MAX);
                non_sg.ft = FT_BLOCK;
                non_sg.d_type =  s->d_type;
                return 1;
        } else if (0 == strcmp("tape", s->d_name)) {
                strcpy(non_sg.name, s->d_name);
                non_sg.ft = FT_CHAR;
                non_sg.d_type =  s->d_type;
                return 1;
        } else if (0 == strncmp("scsi_tape:st", s->d_name, 12)) {
                len = strlen(s->d_name);
                if (isdigit(s->d_name[len - 1])) {
                        /* want 'st<num>' symlink only */
                        strcpy(non_sg.name, s->d_name);
                        non_sg.ft = FT_CHAR;
                        non_sg.d_type =  s->d_type;
                        return 1;
                } else
                        return 0;
        } else if (0 == strncmp("onstream_tape:os", s->d_name, 16)) {
                strcpy(non_sg.name, s->d_name);
                non_sg.ft = FT_CHAR;
                non_sg.d_type =  s->d_type;
                return 1;
        } else
                return 0;
}

static int
non_sg_scan(const char * dir_name, const struct lsscsi_opt_coll * opts)
{
        char name[NAME_LEN_MAX];
        struct dirent ** namelist;
        int num, k;

        non_sg.ft = FT_OTHER;
        num = scandir(dir_name, &namelist, non_sg_scandir_select, NULL);
        if (num < 0) {
                if (opts->verbose > 0) {
                        snprintf(name, NAME_LEN_MAX, "scandir: %s", dir_name);
                        perror(name);
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
                strncpy(aa_sg.name, s->d_name, NAME_LEN_MAX);
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
sas_low_phy_scandir_select(const struct dirent * s)
{
        char * cp;
        int n, m;

        if ((DT_LNK != s->d_type) && (DT_DIR != s->d_type))
                return 0;
        if (0 == strncmp("phy", s->d_name, 3)) {
                if (0 == strlen(sas_low_phy))
                        strncpy(sas_low_phy, s->d_name, NAME_LEN_MAX);
                else {
                        cp = strrchr(s->d_name, ':');
                        if (NULL == cp)
                                return 0;
                        n = atoi(cp + 1);
                        cp = strrchr(sas_low_phy, ':');
                        if (NULL == cp)
                                return 0;
                        m = atoi(cp + 1);
                        if (n < m)
                                strncpy(sas_low_phy, s->d_name,
                                        NAME_LEN_MAX);
                }
                return 1;
        } else
                return 0;
}

static int
sas_low_phy_scan(const char * dir_name)
{
        struct dirent ** namelist;
        int num, k;

        memset(sas_low_phy, 0, sizeof(sas_low_phy));
        num = scandir(dir_name, &namelist, sas_low_phy_scandir_select, NULL);
        if (num < 0)
                return -1;
        for (k = 0; k < num; ++k)
                free(namelist[k]);
        free(namelist);
        return num;
}


static int
iscsi_target_scandir_select(const struct dirent * s)
{
        char buff[NAME_LEN_MAX];
        int off;
        struct stat a_stat;

        if ((DT_LNK != s->d_type) && (DT_DIR != s->d_type))
                return 0;
        if (0 == strncmp("session", s->d_name, 7)) {
                iscsi_tsession_num = atoi(s->d_name + 7);
                strcpy(buff, iscsi_dir_name);
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


/* Return 1 if found (in /proc/mounts or /sys/class directory exists),
   else 0 if problems */
static int
find_sysfsroot()
{
        char buff[NAME_LEN_MAX];
        char dev[34];
        char fs_type[34];
        FILE * f;
        int res = 0;
        int n;

        memset(buff, 0, sizeof(buff));
        memset(dev, 0, sizeof(dev));
        memset(fs_type, 0, sizeof(fs_type));
        if (NULL == (f = fopen(proc_mounts, "r"))) {
                DIR * dirp;

                dirp = opendir(sysfs_test_dir);
                if (dirp) {
                        closedir(dirp);
                        strcpy(sysfsroot, sysfs_test_top);
                        return 1;
                }
                fprintf(stderr, "Unable to open %s for reading",
                        proc_mounts);
                return 0;
        }
        while (fgets(buff, sizeof(buff) - 2, f)) {
                n = sscanf(buff, "%32s %256s %32s", dev, sysfsroot, fs_type);
                if (3 != n) {
                        fprintf(stderr, "unexpected short scan,n=%d\n", n);
                        break;
                }
                if (0 == strcmp(fs_type, sysfs_name)) {
                        res = 1;
                        break;
                }
        }
        fclose(f);
        return res;
}

/* If 'dir_name'/'base_name' is a directory chdir to it. If that is successful
   return 1, else 0 */
static int
if_directory_chdir(const char * dir_name, const char * base_name)
{
        char buff[NAME_LEN_MAX];
        struct stat a_stat;

        strcpy(buff, dir_name);
        strcat(buff, "/");
        strcat(buff, base_name);
        if (stat(buff, &a_stat) < 0)
                return 0;
        if (S_ISDIR(a_stat.st_mode)) {
                if (chdir(buff) < 0)
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
        char buff[NAME_LEN_MAX];
        struct stat a_stat;
        const char * old_name = "generic";

        strcpy(buff, dir_name);
        strcat(buff, "/");
        strcat(buff, old_name);
        if ((stat(buff, &a_stat) >= 0) && S_ISDIR(a_stat.st_mode)) {
                if (chdir(buff) < 0)
                        return 0;
                return 1;
        }
        /* No "generic", so now look for "scsi_generic:sg<n>" */
        if (1 != sg_scan(dir_name))
                return 0;
        strcpy(buff, dir_name);
        strcat(buff, "/");
        strcat(buff, aa_sg.name);
        if (stat(buff, &a_stat) < 0)
                return 0;
        if (S_ISDIR(a_stat.st_mode)) {
                if (chdir(buff) < 0)
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
        char buff[NAME_LEN_MAX];
        FILE * f;
        int len;

        strcpy(buff, dir_name);
        strcat(buff, "/");
        strcat(buff, base_name);
        if (NULL == (f = fopen(buff, "r"))) {
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

/* Allocate dev_node_list & collect info on every node in /dev. */
static void
collect_dev_nodes ()
{
        DIR *dirp;
        struct dirent *dep;
        char device_path[ PATH_MAX + 1];
        struct stat stats;
        struct dev_node_list *cur_list, *prev_list;
        struct dev_node_entry *cur_ent;

        if (dev_node_listhead) return; /* already collected nodes */

        dev_node_listhead = malloc (sizeof(struct dev_node_list));
        if (!dev_node_listhead) return;

        cur_list = dev_node_listhead;
        cur_list->next = NULL;
        cur_list->count = 0;

        dirp = opendir (dev_dir);
        if (dirp == NULL) return;

        while (1)
        {
                dep = readdir (dirp);
                if (dep == NULL) break;

                snprintf (device_path, PATH_MAX, "%s/%s",
                          dev_dir, dep->d_name);
                device_path [PATH_MAX] = '\0';

                if (lstat(device_path, &stats))
                        continue;

                /* Skip non-block/char files. */
                if ( (!S_ISBLK(stats.st_mode)) && (!S_ISCHR(stats.st_mode)) )
                        continue;

                /* Add to the list. */
                if (cur_list->count >= DEV_NODE_LIST_ENTRIES)
                {
                        prev_list = cur_list;
                        cur_list = malloc (sizeof(struct dev_node_list));
                        if (!cur_list) break;
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
                strncpy(cur_ent->name, device_path, NAME_MAX);

                cur_list->count++;
        }

        closedir(dirp);
}

/* Free dev_node_list. */
static void
free_dev_node_list ()
{
        if (dev_node_listhead)
        {
                struct dev_node_list *cur_list, *next_list;

                cur_list = dev_node_listhead;
                while (cur_list)
                {
                        next_list = cur_list->next;
                        free(cur_list);
                        cur_list = next_list;
                }

                dev_node_listhead = NULL;
        }
}

/* Given a path to a class device, find the most recent device node with
   matching major/minor. */
static int
get_dev_node (char *wd, char *node, enum dev_type type)
{
        struct dev_node_list *cur_list;
        struct dev_node_entry *cur_ent;
        char value[NAME_LEN_MAX];
        unsigned int maj, min;
        time_t newest_mtime = 0;
        int match_found = 0;
        unsigned int i;

        strcpy(node,"-");

        if (dev_node_listhead == NULL)
        {
                collect_dev_nodes();
                if (dev_node_listhead == NULL) goto exit;
        }

        /* Get the major/minor for this device. */
        if (!get_value(wd, "dev", value, NAME_LEN_MAX)) goto exit;
        sscanf(value, "%u:%u", &maj, &min);

        /* Search the node list for the newest match on this major/minor. */
        cur_list = dev_node_listhead;
        i = 0;

        while (1)
        {
                if (i >= cur_list->count)
                {
                        cur_list = cur_list->next;
                        if (!cur_list) break;
                        i = 0;
                }

                cur_ent = &cur_list->nodes[i];
                i++;

                if ( (maj == cur_ent->maj) &&
                     (min == cur_ent->min) &&
                     (type == cur_ent->type) )
                {
                        if ( (!match_found) ||
                             (difftime(cur_ent->mtime,newest_mtime) > 0) )
                        {
                                newest_mtime = cur_ent->mtime;
                                strncpy(node, cur_ent->name, NAME_MAX);
                        }
                        match_found = 1;
                }
        }

exit:
        return match_found;
}

/* Fetch USB device name string (form "<b>-<p1>[.<p2>]+:<c>.<i>") given
 * either a SCSI host name or devname (i.e. "h:c:t:l") string. If detected
 * return 'b' (pointer to start of USB device name string which is null
 * terminated), else return NULL.
 */
static char *
get_usb_devname(const char * hname, const char * devname, char * b, int b_len)
{
        char buff[NAME_LEN_MAX];
        char bf2[NAME_LEN_MAX];
        int len;
        const char * np;
        char * cp;
        char * c2p;

        strcpy(buff, sysfsroot);
        if (hname) {
                strcat(buff, scsi_host);
                np = hname;
        } else if (devname) {
                strcat(buff, class_scsi_dev);
                np = devname;
        } else
                return NULL;
        if (if_directory_chdir(buff, np) && getcwd(bf2, NAME_LEN_MAX) &&
            strstr(bf2, "usb")) {
                if (b_len > 0)
                        b[0] = '\0';
                if ((cp = strstr(bf2, "/host"))) {
                        len = (cp - bf2) - 1;
                        if ((len > 0) && ((c2p = memrchr(bf2, '/', len)))) {
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
        if (1 != sscanf(colon_list, "%d", &outp->l))
                return 0;
        return 1;
}

/* Check host associated with 'devname' for known transport types. If so set
   transport_id, place a string in 'b' and return 1. Otherwise return 0. */
static int
transport_init(const char * devname, /* const struct lsscsi_opt_coll * opts, */
               int b_len, char * b)
{
        char buff[NAME_LEN_MAX];
        char wd[NAME_LEN_MAX];
        int off;
        char * cp;
        struct stat a_stat;

        /* SPI host */
        strcpy(buff, sysfsroot);
        strcat(buff, spi_host);
        strcat(buff, devname);
        if ((stat(buff, &a_stat) >= 0) && S_ISDIR(a_stat.st_mode)) {
                transport_id = TRANSPORT_SPI;
                snprintf(b, b_len, "spi:");
                return 1;
        }

        /* FC host */
        strcpy(buff, sysfsroot);
        strcat(buff, fc_host);
        strcat(buff, devname);
        if ((stat(buff, &a_stat) >= 0) && S_ISDIR(a_stat.st_mode)) {
                transport_id = TRANSPORT_FC;
                snprintf(b, b_len, "fc:");
                off = strlen(b);
                if (get_value(buff, "port_name", b + off, b_len - off)) {
                        strcat(b, ",");
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
        strcpy(buff, sysfsroot);
        strcat(buff, sas_host);
        strcat(buff, devname);
        if ((stat(buff, &a_stat) >= 0) && S_ISDIR(a_stat.st_mode)) {
                transport_id = TRANSPORT_SAS;
                strcat(buff, "/device");
                if (sas_low_phy_scan(buff) < 1)
                        return 0;
                strcpy(buff, sysfsroot);
                strcat(buff, sas_phy);
                strcat(buff, sas_low_phy);
                snprintf(b, b_len, "sas:");
                off = strlen(b);
                if (get_value(buff, "sas_address", b + off, b_len - off))
                        return 1;
                else
                        fprintf(stderr, "_init: no sas_address, wd=%s\n",
                                buff);
        }

        /* SAS class representation */
        strcpy(buff, sysfsroot);
        strcat(buff, scsi_host);
        strcat(buff, devname);
        strcat(buff, "/device/sas/ha");
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
                char *t, buff2[NAME_LEN_MAX];

                /* resolve SCSI host device */
                strcpy(buff, sysfsroot);
                strcat(buff, scsi_host);
                strcat(buff, devname);
                strcat(buff, "/device");
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
                    > NAME_LEN_MAX)
                        break;
                strcat(buff, buff2);

                /* read the FireWire host's EUI-64 */
                if (!get_value(buff, "host_id/guid", buff2, sizeof(buff2)) ||
                    strlen(buff2) != 18)
                        break;
                snprintf(b, b_len, "sbp:%s", buff2 + 2);
                return 1;
        } while (0);

        /* iSCSI host */
        strcpy(buff, sysfsroot);
        strcat(buff, iscsi_host);
        strcat(buff, devname);
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
        strcpy(buff, sysfsroot);
        strcat(buff, scsi_host);
        strcat(buff, devname);
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
                      const struct lsscsi_opt_coll * opts)
{
        char buff[NAME_LEN_MAX];
        char wd[NAME_LEN_MAX];
        char value[NAME_LEN_MAX];
        char * cp;

        strcpy(buff, path_name);
        strcpy(wd, path_name);
        cp = basename(wd);
        switch (transport_id) {
        case TRANSPORT_SPI:
                printf("  transport=spi\n");
                strcpy(buff, sysfsroot);
                strcat(buff, spi_host);
                strcat(buff, cp);
                if (get_value(buff, "signalling", value, NAME_LEN_MAX))
                        printf("  signalling=%s\n", value);
                break;
        case TRANSPORT_FC:
                printf("  transport=fc\n");
                strcat(buff, "/device/fc_host:");
                strcat(buff, cp);
                if (get_value(buff, "node_name", value, NAME_LEN_MAX))
                        printf("  node_name=%s\n", value);
                if (get_value(buff, "port_name", value, NAME_LEN_MAX))
                        printf("  port_name=%s\n", value);
                if (get_value(buff, "port_id", value, NAME_LEN_MAX))
                        printf("  port_id=%s\n", value);
                if (get_value(buff, "port_type", value, NAME_LEN_MAX))
                        printf("  port_type=%s\n", value);
                if (get_value(buff, "speed", value, NAME_LEN_MAX))
                        printf("  speed=%s\n", value);
                if (get_value(buff, "supported_classes", value, NAME_LEN_MAX))
                        printf("  supported_classes=%s\n", value);
                if (get_value(buff, "tgtid_bind_type", value, NAME_LEN_MAX))
                        printf("  tgtid_bind_type=%s\n", value);
                if (opts->verbose > 2)
                        printf("fetched from directory: %s\n", buff);
                break;
        case TRANSPORT_SAS:
                printf("  transport=sas\n");
                strcat(buff, "/device");
                if (sas_low_phy_scan(buff) < 1)
                        return;
                strcpy(buff, sysfsroot);
                strcat(buff, sas_phy);
                strcat(buff, sas_low_phy);
                if (get_value(buff, "device_type", value, NAME_LEN_MAX))
                        printf("  device_type=%s\n", value);
                if (get_value(buff, "initiator_port_protocols", value,
                              NAME_LEN_MAX))
                        printf("  initiator_port_protocols=%s\n", value);
                if (get_value(buff, "invalid_dword_count", value,
                              NAME_LEN_MAX))
                        printf("  invalid_dword_count=%s\n", value);
                if (get_value(buff, "loss_of_dword_sync_count", value,
                              NAME_LEN_MAX))
                        printf("  loss_of_dword_sync_count=%s\n", value);
                if (get_value(buff, "maximum_linkrate", value, NAME_LEN_MAX))
                        printf("  maximum_linkrate=%s\n", value);
                if (get_value(buff, "maximum_linkrate_hw", value,
                              NAME_LEN_MAX))
                        printf("  maximum_linkrate_hw=%s\n", value);
                if (get_value(buff, "minimum_linkrate", value, NAME_LEN_MAX))
                        printf("  minimum_linkrate=%s\n", value);
                if (get_value(buff, "minimum_linkrate_hw", value,
                              NAME_LEN_MAX))
                        printf("  minimum_linkrate_hw=%s\n", value);
                if (get_value(buff, "negotiated_linkrate", value,
                              NAME_LEN_MAX))
                        printf("  negotiated_linkrate=%s\n", value);
                if (get_value(buff, "phy_identifier", value, NAME_LEN_MAX))
                        printf("  phy_identifier=%s\n", value);
                if (get_value(buff, "phy_reset_problem_count", value,
                              NAME_LEN_MAX))
                        printf("  phy_reset_problem_count=%s\n", value);
                if (get_value(buff, "running_disparity_error_count", value,
                              NAME_LEN_MAX))
                        printf("  running_disparity_error_count=%s\n", value);
                if (get_value(buff, "sas_address", value, NAME_LEN_MAX))
                        printf("  sas_address=%s\n", value);
                if (get_value(buff, "target_port_protocols", value,
                              NAME_LEN_MAX))
                        printf("  target_port_protocols=%s\n", value);
                if (opts->verbose > 2)
                        printf("fetched from directory: %s\n", buff);
                break;
        case TRANSPORT_SAS_CLASS:
                printf("  transport=sas\n");
                printf("  sub_transport=sas_class\n");
                strcat(buff, "/device/sas/ha");
                if (get_value(buff, "device_name", value, NAME_LEN_MAX))
                        printf("  device_name=%s\n", value);
                if (get_value(buff, "ha_name", value, NAME_LEN_MAX))
                        printf("  ha_name=%s\n", value);
                if (get_value(buff, "version_descriptor", value, NAME_LEN_MAX))
                        printf("  version_descriptor=%s\n", value);
                printf("  phy0:\n");
                strcat(buff, "/phys/0");
                if (get_value(buff, "class", value, NAME_LEN_MAX))
                        printf("    class=%s\n", value);
                if (get_value(buff, "enabled", value, NAME_LEN_MAX))
                        printf("    enabled=%s\n", value);
                if (get_value(buff, "id", value, NAME_LEN_MAX))
                        printf("    id=%s\n", value);
                if (get_value(buff, "iproto", value, NAME_LEN_MAX))
                        printf("    iproto=%s\n", value);
                if (get_value(buff, "linkrate", value, NAME_LEN_MAX))
                        printf("    linkrate=%s\n", value);
                if (get_value(buff, "oob_mode", value, NAME_LEN_MAX))
                        printf("    oob_mode=%s\n", value);
                if (get_value(buff, "role", value, NAME_LEN_MAX))
                        printf("    role=%s\n", value);
                if (get_value(buff, "sas_addr", value, NAME_LEN_MAX))
                        printf("    sas_addr=%s\n", value);
                if (get_value(buff, "tproto", value, NAME_LEN_MAX))
                        printf("    tproto=%s\n", value);
                if (get_value(buff, "type", value, NAME_LEN_MAX))
                        printf("    type=%s\n", value);
                if (opts->verbose > 2)
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
                       value, NAME_LEN_MAX));
                break;
        case TRANSPORT_ATA:
                printf("  transport=ata\n");
                break;
        case TRANSPORT_SATA:
                printf("  transport=sata\n");
                break;
        default:
                if (opts->verbose > 1)
                        fprintf(stderr, "No transport information\n");
                break;
        }
}

/* Attempt to determine the transport type of the SCSI device (LU) associated
   with 'devname'. If found set transport_id, place string in 'b' and return
   1. Otherwise return 0. */
static int
transport_tport(const char * devname,
                /* const struct lsscsi_opt_coll * opts, */ int b_len, char * b)
{
        char buff[NAME_LEN_MAX];
        char wd[NAME_LEN_MAX];
        char nm[NAME_LEN_MAX];
        char tpgt[NAME_LEN_MAX];
        char * cp;
        struct addr_hctl hctl;
        int len, off, n;
        struct stat a_stat;

        if (! parse_colon_list(devname, &hctl))
                return 0;

        /* SAS host? */
        strcpy(buff, sysfsroot);
        strcat(buff, sas_host);
        len = strlen(buff);
        snprintf(buff + len, NAME_LEN_MAX - len, "host%d", hctl.h);
        if ((stat(buff, &a_stat) >= 0) && S_ISDIR(a_stat.st_mode)) {
                /* SAS transport layer representation */
                transport_id = TRANSPORT_SAS;
                strcpy(buff, sysfsroot);
                strcat(buff, class_scsi_dev);
                strcat(buff, devname);
                if (if_directory_chdir(buff, "device")) {
                        if (NULL == getcwd(wd, NAME_LEN_MAX))
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
                        strcpy(sas_hold_end_device, cp);
                        strcpy(buff, sysfsroot);
                        strcat(buff, sas_device);
                        strcat(buff, cp);
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
        strcpy(buff, sysfsroot);
        strcat(buff, spi_host);
        len = strlen(buff);
        snprintf(buff + len, NAME_LEN_MAX - len, "host%d", hctl.h);
        if ((stat(buff, &a_stat) >= 0) && S_ISDIR(a_stat.st_mode)) {
                transport_id = TRANSPORT_SPI;
                snprintf(b, b_len, "spi:%d", hctl.t);
                return 1;
        }
        /* FC host? */
        strcpy(buff, sysfsroot);
        strcat(buff, fc_host);
        len = strlen(buff);
        snprintf(buff + len, NAME_LEN_MAX - len, "host%d", hctl.h);
        if ((stat(buff, &a_stat) >= 0) && S_ISDIR(a_stat.st_mode)) {
                transport_id = TRANSPORT_FC;
                strcpy(buff, sysfsroot);
                strcat(buff, fc_transport);
                len = strlen(buff);
                snprintf(buff + len, NAME_LEN_MAX - len, "target%d:%d:%d",
                         hctl.h, hctl.c, hctl.t);
                snprintf(b, b_len, "fc:");
                off = strlen(b);
                if (get_value(buff, "port_name", b + off, b_len - off)) {
                        strcat(b, ",");
                        off = strlen(b);
                } else
                        return 0;
                if (get_value(buff, "port_id", b + off, b_len - off))
                        return 1;
                else
                        return 0;
        }
        /* SAS class representation or SBP? */
        strcpy(buff, sysfsroot);
        strcat(buff, bus_scsi_devs);
        strcat(buff, "/");
        strcat(buff, devname);
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
        strcpy(buff, sysfsroot);
        strcat(buff, iscsi_host);
        off = strlen(buff);
        snprintf(buff + off, sizeof(buff) - off, "host%d/device", hctl.h);
        if ((stat(buff, &a_stat) >= 0) && S_ISDIR(a_stat.st_mode)) {
                if (1 != iscsi_target_scan(buff, &hctl))
                        return 0;
                transport_id = TRANSPORT_ISCSI;
                strcpy(buff, sysfsroot);
                strcat(buff, iscsi_session);
                off = strlen(buff);
                snprintf(buff + off, sizeof(buff) - off, "session%d",
                         iscsi_tsession_num);
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
        strcpy(buff, sysfsroot);
        strcat(buff, scsi_host);
        len = strlen(buff);
        snprintf(buff + len, NAME_LEN_MAX - len, "host%d", hctl.h);
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
                       const struct lsscsi_opt_coll * opts)
{
        char path_name[NAME_LEN_MAX];
        char buff[NAME_LEN_MAX];
        char b2[NAME_LEN_MAX];
        char wd[NAME_LEN_MAX];
        char value[NAME_LEN_MAX];
        struct addr_hctl hctl;
        int len, off;
        char * cp;

#if 0
        strcpy(buff, path_name);
        len = strlen(buff);
        snprintf(buff + len, NAME_LEN_MAX - len, "/scsi_device:%s", devname);
        if (! if_directory_chdir(buff, "device"))
                return;
        if (NULL == getcwd(wd, NAME_LEN_MAX))
                return;
#else
        strcpy(path_name, sysfsroot);
        strcat(path_name, class_scsi_dev);
        strcat(path_name, devname);
        strcat(buff, path_name);
#endif
        switch (transport_id) {
        case TRANSPORT_SPI:
                printf("  transport=spi\n");
                if (! parse_colon_list(devname, &hctl))
                        break;
                strcpy(buff, sysfsroot);
                strcat(buff, spi_transport);
                len = strlen(buff);
                snprintf(buff + len, NAME_LEN_MAX - len, "target%d:%d:%d",
                         hctl.h, hctl.c, hctl.t);
                printf("  target_id=%d\n", hctl.t);
                if (get_value(buff, "dt", value, NAME_LEN_MAX))
                        printf("  dt=%s\n", value);
                if (get_value(buff, "max_offset", value, NAME_LEN_MAX))
                        printf("  max_offset=%s\n", value);
                if (get_value(buff, "max_width", value, NAME_LEN_MAX))
                        printf("  max_width=%s\n", value);
                if (get_value(buff, "min_period", value, NAME_LEN_MAX))
                        printf("  min_period=%s\n", value);
                if (get_value(buff, "offset", value, NAME_LEN_MAX))
                        printf("  offset=%s\n", value);
                if (get_value(buff, "period", value, NAME_LEN_MAX))
                        printf("  period=%s\n", value);
                if (get_value(buff, "width", value, NAME_LEN_MAX))
                        printf("  width=%s\n", value);
                break;
        case TRANSPORT_FC:
                printf("  transport=fc\n");
                if (! if_directory_chdir(path_name, "device"))
                        return;
                if (NULL == getcwd(wd, NAME_LEN_MAX))
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
                strcpy(buff, "fc_remote_ports:");
                strcat(buff, cp);
                if (! if_directory_chdir(wd, buff))
                        return;
                if (NULL == getcwd(buff, NAME_LEN_MAX))
                        return;
                if (get_value(buff, "node_name", value, NAME_LEN_MAX))
                        printf("  node_name=%s\n", value);
                if (get_value(buff, "port_name", value, NAME_LEN_MAX))
                        printf("  port_name=%s\n", value);
                if (get_value(buff, "port_id", value, NAME_LEN_MAX))
                        printf("  port_id=%s\n", value);
                if (get_value(buff, "port_state", value, NAME_LEN_MAX))
                        printf("  port_state=%s\n", value);
                if (get_value(buff, "roles", value, NAME_LEN_MAX))
                        printf("  roles=%s\n", value);
                if (get_value(buff, "scsi_target_id", value, NAME_LEN_MAX))
                        printf("  scsi_target_id=%s\n", value);
                if (get_value(buff, "supported_classes", value, NAME_LEN_MAX))
                        printf("  supported_classes=%s\n", value);
                if (get_value(buff, "dev_loss_tmo", value, NAME_LEN_MAX))
                        printf("  dev_loss_tmo=%s\n", value);
                if (opts->verbose > 2)
                        printf("fetched from directory: %s\n", buff);
                break;
        case TRANSPORT_SAS:
                printf("  transport=sas\n");
                strcpy(buff, sysfsroot);
                strcat(buff, sas_device);
                strcat(buff, sas_hold_end_device);
                strcpy(b2, sysfsroot);
                strcat(b2, sas_end_device);
                strcat(b2, sas_hold_end_device);
                if (get_value(buff, "initiator_port_protocols", value,
                              NAME_LEN_MAX))
                        printf("  initiator_port_protocols=%s\n", value);
                if (get_value(b2, "initiator_response_timeout", value,
                              NAME_LEN_MAX))
                        printf("  initiator_response_timeout=%s\n", value);
                if (get_value(b2, "I_T_nexus_loss_timeout", value,
                              NAME_LEN_MAX))
                        printf("  I_T_nexus_loss_timeout=%s\n", value);
                if (get_value(buff, "phy_identifier", value, NAME_LEN_MAX))
                        printf("  phy_identifier=%s\n", value);
                if (get_value(b2, "ready_led_meaning", value, NAME_LEN_MAX))
                        printf("  ready_led_meaning=%s\n", value);
                if (get_value(buff, "sas_address", value, NAME_LEN_MAX))
                        printf("  sas_address=%s\n", value);
                if (get_value(buff, "target_port_protocols", value,
                              NAME_LEN_MAX))
                        printf("  target_port_protocols=%s\n", value);
                if (opts->verbose > 2) {
                        printf("fetched from directory: %s\n", buff);
                        printf("fetched from directory: %s\n", b2);
                }
                break;
        case TRANSPORT_SAS_CLASS:
                printf("  transport=sas\n");
                printf("  sub_transport=sas_class\n");
                strcpy(buff, path_name);
                strcat(buff, "/device/sas_device");
                if (get_value(buff, "device_name", value, NAME_LEN_MAX))
                        printf("  device_name=%s\n", value);
                if (get_value(buff, "dev_type", value, NAME_LEN_MAX))
                        printf("  dev_type=%s\n", value);
                if (get_value(buff, "iproto", value, NAME_LEN_MAX))
                        printf("  iproto=%s\n", value);
                if (get_value(buff, "iresp_timeout", value, NAME_LEN_MAX))
                        printf("  iresp_timeout=%s\n", value);
                if (get_value(buff, "itnl_timeout", value, NAME_LEN_MAX))
                        printf("  itnl_timeout=%s\n", value);
                if (get_value(buff, "linkrate", value, NAME_LEN_MAX))
                        printf("  linkrate=%s\n", value);
                if (get_value(buff, "max_linkrate", value, NAME_LEN_MAX))
                        printf("  max_linkrate=%s\n", value);
                if (get_value(buff, "max_pathways", value, NAME_LEN_MAX))
                        printf("  max_pathways=%s\n", value);
                if (get_value(buff, "min_linkrate", value, NAME_LEN_MAX))
                        printf("  min_linkrate=%s\n", value);
                if (get_value(buff, "pathways", value, NAME_LEN_MAX))
                        printf("  pathways=%s\n", value);
                if (get_value(buff, "ready_led_meaning", value, NAME_LEN_MAX))
                        printf("  ready_led_meaning=%s\n", value);
                if (get_value(buff, "rl_wlun", value, NAME_LEN_MAX))
                        printf("  rl_wlun=%s\n", value);
                if (get_value(buff, "sas_addr", value, NAME_LEN_MAX))
                        printf("  sas_addr=%s\n", value);
                if (get_value(buff, "tproto", value, NAME_LEN_MAX))
                        printf("  tproto=%s\n", value);
                if (get_value(buff, "transport_layer_retries", value,
                              NAME_LEN_MAX))
                        printf("  transport_layer_retries=%s\n", value);
                if (opts->verbose > 2)
                        printf("fetched from directory: %s\n", buff);
                break;
        case TRANSPORT_ISCSI:
                printf("  transport=iSCSI\n");
                strcpy(buff, sysfsroot);
                strcat(buff, iscsi_session);
                off = strlen(buff);
                snprintf(buff + off, sizeof(buff) - off, "session%d",
                         iscsi_tsession_num);
                if (get_value(buff, "targetname", value, NAME_LEN_MAX))
                        printf("  targetname=%s\n", value);
                if (get_value(buff, "tpgt", value, NAME_LEN_MAX))
                        printf("  tpgt=%s\n", value);
                if (get_value(buff, "data_pdu_in_order", value, NAME_LEN_MAX))
                        printf("  data_pdu_in_order=%s\n", value);
                if (get_value(buff, "data_seq_in_order", value, NAME_LEN_MAX))
                        printf("  data_seq_in_order=%s\n", value);
                if (get_value(buff, "erl", value, NAME_LEN_MAX))
                        printf("  erl=%s\n", value);
                if (get_value(buff, "first_burst_len", value, NAME_LEN_MAX))
                        printf("  first_burst_len=%s\n", value);
                if (get_value(buff, "initial_r2t", value, NAME_LEN_MAX))
                        printf("  initial_r2t=%s\n", value);
                if (get_value(buff, "max_burst_len", value, NAME_LEN_MAX))
                        printf("  max_burst_len=%s\n", value);
                if (get_value(buff, "max_outstanding_r2t", value, NAME_LEN_MAX))
                        printf("  max_outstanding_r2t=%s\n", value);
                if (get_value(buff, "recovery_tmo", value, NAME_LEN_MAX))
                        printf("  recovery_tmo=%s\n", value);
// >>>       Would like to see what are readable attributes in this directory.
//           Ignoring connections for the time being. Could add with an entry
//           for connection=<n> with normal two space indent followed by attributes
//           for that connection indented 4 spaces
                if (opts->verbose > 2)
                        printf("fetched from directory: %s\n", buff);
                break;
        case TRANSPORT_SBP:
                printf("  transport=sbp\n");
                if (! if_directory_chdir(path_name, "device"))
                        return;
                if (NULL == getcwd(buff, NAME_LEN_MAX))
                        return;
                if (get_value(buff, "ieee1394_id", value, NAME_LEN_MAX))
                        printf("  ieee1394_id=%s\n", value);
                if (opts->verbose > 2)
                        printf("fetched from directory: %s\n", buff);
                break;
        case TRANSPORT_USB:
                printf("  transport=usb\n");
                printf("  device_name=%s\n", get_usb_devname(NULL, devname,
                       value, NAME_LEN_MAX));
                break;
        case TRANSPORT_ATA:
                printf("  transport=ata\n");
                break;
        case TRANSPORT_SATA:
                printf("  transport=sata\n");
                break;
        default:
                if (opts->verbose > 1)
                        fprintf(stderr, "No transport information\n");
                break;
        }
}

static void
longer_d_entry(const char * path_name, const char * devname,
               const struct lsscsi_opt_coll * opts)
{
        char value[NAME_LEN_MAX];

        if (opts->transport > 0) {
                transport_tport_longer(devname, opts);
                return;
        }
        if (opts->long_opt >= 3) {
                if (get_value(path_name, "device_blocked", value,
                              NAME_LEN_MAX))
                        printf("  device_blocked=%s\n", value);
                else if (opts->verbose > 0)
                        printf("  device_blocked=?\n");
                if (get_value(path_name, "iocounterbits", value,
                              NAME_LEN_MAX))
                        printf("  iocounterbits=%s\n", value);
                else if (opts->verbose > 0)
                        printf("  iocounterbits=?\n");
                if (get_value(path_name, "iodone_cnt", value, NAME_LEN_MAX))
                        printf("  iodone_cnt=%s\n", value);
                else if (opts->verbose > 0)
                        printf("  iodone_cnt=?\n");
                if (get_value(path_name, "ioerr_cnt", value, NAME_LEN_MAX))
                        printf("  ioerr_cnt=%s\n", value);
                else if (opts->verbose > 0)
                        printf("  ioerr_cnt=?\n");
                if (get_value(path_name, "iorequest_cnt", value,
                              NAME_LEN_MAX))
                        printf("  iorequest_cnt=%s\n", value);
                else if (opts->verbose > 0)
                        printf("  iorequest_cnt=?\n");
                if (get_value(path_name, "queue_depth", value,
                              NAME_LEN_MAX))
                        printf("  queue_depth=%s\n", value);
                else if (opts->verbose > 0)
                        printf("  queue_depth=?\n");
                if (get_value(path_name, "queue_type", value,
                              NAME_LEN_MAX))
                        printf("  queue_type=%s\n", value);
                else if (opts->verbose > 0)
                        printf("  queue_type=?\n");
                if (get_value(path_name, "scsi_level", value,
                              NAME_LEN_MAX))
                        printf("  scsi_level=%s\n", value);
                else if (opts->verbose > 0)
                        printf("  scsi_level=?\n");
                if (get_value(path_name, "state", value,
                              NAME_LEN_MAX))
                        printf("  state=%s\n", value);
                else if (opts->verbose > 0)
                        printf("  state=?\n");
                if (get_value(path_name, "timeout", value,
                              NAME_LEN_MAX))
                        printf("  timeout=%s\n", value);
                else if (opts->verbose > 0)
                        printf("  timeout=?\n");
                if (get_value(path_name, "type", value,
                              NAME_LEN_MAX))
                        printf("  type=%s\n", value);
                else if (opts->verbose > 0)
                        printf("  type=?\n");
                return;
        }

        if (get_value(path_name, "state", value, NAME_LEN_MAX))
                printf("  state=%s", value);
        else
                printf("  state=?");
        if (get_value(path_name, "queue_depth", value, NAME_LEN_MAX))
                printf(" queue_depth=%s", value);
        else
                printf(" queue_depth=?");
        if (get_value(path_name, "scsi_level", value, NAME_LEN_MAX))
                printf(" scsi_level=%s", value);
        else
                printf(" scsi_level=?");
        if (get_value(path_name, "type", value, NAME_LEN_MAX))
                printf(" type=%s", value);
        else
                printf(" type=?");
        if (get_value(path_name, "device_blocked", value, NAME_LEN_MAX))
                printf(" device_blocked=%s", value);
        else
                printf(" device_blocked=?");
        if (get_value(path_name, "timeout", value, NAME_LEN_MAX))
                printf(" timeout=%s", value);
        else
                printf(" timeout=?");
        printf("\n");
        if (opts->long_opt == 2) {
                if (get_value(path_name, "iocounterbits", value,
                              NAME_LEN_MAX))
                        printf("  iocounterbits=%s", value);
                else
                        printf("  iocounterbits=?");
                if (get_value(path_name, "iodone_cnt", value,
                               NAME_LEN_MAX))
                        printf(" iodone_cnt=%s", value);
                else
                        printf(" iodone_cnt=?");
                if (get_value(path_name, "ioerr_cnt", value,
                               NAME_LEN_MAX))
                        printf(" ioerr_cnt=%s", value);
                else
                        printf(" ioerr_cnt=?");
                if (get_value(path_name, "iorequest_cnt", value,
                               NAME_LEN_MAX))
                        printf(" iorequest_cnt=%s", value);
                else
                        printf(" iorequest_cnt=?");
                printf("\n");
                if (get_value(path_name, "queue_type", value,
                               NAME_LEN_MAX))
                        printf("  queue_type=%s", value);
                else
                        printf("  queue_type=?");
                printf("\n");
        }
}

static void
one_classic_sdev_entry(const char * dir_name, const char * devname,
                       const struct lsscsi_opt_coll * opts)
{
        struct addr_hctl hctl;
        char buff[NAME_LEN_MAX];
        char value[NAME_LEN_MAX];
        int type, scsi_level;

        strcpy(buff, dir_name);
        strcat(buff, "/");
        strcat(buff, devname);
        if (! parse_colon_list(devname, &hctl))
                invalidate_hctl(&hctl);
        printf("Host: scsi%d Channel: %02d Target: %02d Lun: %02d\n",
               hctl.h, hctl.c, hctl.t, hctl.l);

        if (get_value(buff, "vendor", value, NAME_LEN_MAX))
                printf("  Vendor: %-8s", value);
        else
                printf("  Vendor: ?       ");
        if (get_value(buff, "model", value, NAME_LEN_MAX))
                printf(" Model: %-16s", value);
        else
                printf(" Model: ?               ");
        if (get_value(buff, "rev", value, NAME_LEN_MAX))
                printf(" Rev: %-4s", value);
        else
                printf(" Rev: ?   ");
        printf("\n");
        if (! get_value(buff, "type", value, NAME_LEN_MAX)) {
                printf("  Type:   %-33s", "?");
        } else if (1 != sscanf(value, "%d", &type)) {
                printf("  Type:   %-33s", "??");
        } else if ((type < 0) || (type > 31)) {
                printf("  Type:   %-33s", "???");
        } else
                printf("  Type:   %-33s", scsi_device_types[type]);
        if (! get_value(buff, "scsi_level", value, NAME_LEN_MAX)) {
                printf("ANSI SCSI revision: ?\n");
        } else if (1 != sscanf(value, "%d", &scsi_level)) {
                printf("ANSI SCSI revision: ??\n");
        } else
                printf("ANSI SCSI revision: %02x\n", (scsi_level - 1) ?
                                            scsi_level - 1 : 1);
        if (opts->generic) {
                if (if_directory_ch2generic(buff)) {
                        char wd[NAME_LEN_MAX];

                        if (NULL == getcwd(wd, NAME_LEN_MAX))
                                printf("generic_dev error\n");
                        else {
                                char dev_node[NAME_MAX + 1];

                                if (opts->kname)
                                        snprintf(dev_node, NAME_MAX, "%s/%s",
                                                 dev_dir, basename(wd));
                                else if (!get_dev_node(wd, dev_node, CHR_DEV))
                                        snprintf(dev_node, NAME_MAX, "-");

                                printf("%s\n", dev_node);
                        }
                }
                else
                        printf("-\n");
        }
        if (opts->long_opt > 0)
                longer_d_entry(buff, devname, opts);
        if (opts->verbose)
                printf("  dir: %s\n", buff);
}

static void
one_sdev_entry(const char * dir_name, const char * devname,
               const struct lsscsi_opt_coll * opts)
{
        char buff[NAME_LEN_MAX];
        char value[NAME_LEN_MAX];
        int type;

        if (opts->classic) {
                one_classic_sdev_entry(dir_name, devname, opts);
                return;
        }
        strcpy(buff, dir_name);
        strcat(buff, "/");
        strcat(buff, devname);
        snprintf(value, NAME_LEN_MAX, "[%s]", devname);
        printf("%-13s", value);
        if (! get_value(buff, "type", value, NAME_LEN_MAX)) {
                printf("type?   ");
        } else if (1 != sscanf(value, "%d", &type)) {
                printf("type??  ");
        } else if ((type < 0) || (type > 31)) {
                printf("type??? ");
        } else
                printf("%s ", scsi_short_device_types[type]);

        if (0 == opts->transport) {
                if (get_value(buff, "vendor", value, NAME_LEN_MAX))
                        printf("%-8s ", value);
                else
                        printf("vendor?  ");

                if (get_value(buff, "model", value, NAME_LEN_MAX))
                        printf("%-16s ", value);
                else
                        printf("model?           ");

                if (get_value(buff, "rev", value, NAME_LEN_MAX))
                        printf("%-4s  ", value);
                else
                        printf("rev?  ");
        } else {
                if (transport_tport(devname, /* opts, */
                                    sizeof(value), value))
                        printf("%-30s  ", value);
                else
                        printf("                                ");
        }

        if (1 == non_sg_scan(buff, opts)) {
                char wd[NAME_LEN_MAX];
                char extra[NAME_LEN_MAX];

                if (DT_DIR == non_sg.d_type) {
                        strcpy(wd, buff);
                        strcat(wd, "/");
                        strcat(wd, non_sg.name);
                        if (1 == scan_for_first(wd, opts))
                                strcpy(extra, aa_first.name);
                        else {
                                printf("unexpected scan_for_first error");
                                wd[0] = '\0';
                        }
                } else {
                        strcpy(wd, buff);
                        strcpy(extra, non_sg.name);
                }
                if (wd[0] && (if_directory_chdir(wd, extra))) {
                        if (NULL == getcwd(wd, NAME_LEN_MAX)) {
                                printf("getcwd error");
                                wd[0] = '\0';
                        }
                }
                if (wd[0]) {
                        char dev_node[NAME_MAX + 1] = "";
                        enum dev_type typ;

                        typ = (FT_BLOCK == non_sg.ft) ? BLK_DEV : CHR_DEV;
                        if (opts->kname)
                                snprintf(dev_node, NAME_MAX, "%s/%s",
                                        dev_dir, basename(wd));
                        else if (!get_dev_node(wd, dev_node, typ))
                                snprintf(dev_node, NAME_MAX, "-       ");

                        printf("%-9s", dev_node);
                        if (opts->dev_maj_min) {
                                if (get_value(wd, "dev", value, NAME_LEN_MAX))
                                        printf("[%s]", value);
                                else
                                        printf("[dev?]");
                        }
                }
        } else
                printf("-       ");

        if (opts->generic) {
                if (if_directory_ch2generic(buff)) {
                        char wd[NAME_LEN_MAX];

                        if (NULL == getcwd(wd, NAME_LEN_MAX))
                                printf("  generic_dev error");
                        else {
                                char dev_node[NAME_MAX + 1] = "";

                                if (opts->kname)
                                        snprintf(dev_node, NAME_MAX, "%s/%s",
                                                 dev_dir, basename(wd));
                                else if (!get_dev_node(wd, dev_node, CHR_DEV))
                                        snprintf(dev_node, NAME_MAX, "-");

                                printf("  %s", dev_node);
                                if (opts->dev_maj_min) {
                                        if (get_value(wd, "dev", value,
                                                      NAME_LEN_MAX))
                                                printf("[%s]", value);
                                        else
                                                printf("[dev?]");
                                }
                        }
                }
                else
                        printf("  -");
        }

        if (opts->protection) {
                int kernel_dif_support = 0;
                if (sd_scan(buff,opts)) {
                        if (if_directory_chdir(buff,aa_sd.name)) {
                                char value[NAME_LEN_MAX];
                                char sddir[NAME_LEN_MAX];
                                strncpy(sddir,buff,NAME_LEN_MAX);
                                strcat(sddir,"/");
                                strcat(sddir,aa_sd.name);
                                if (!get_value(sddir, "protection_type", value, 
                                        NAME_LEN_MAX)) {
                                        /* kernel < 2.6.27 */
                                        if (opts->verbose)
                                                printf(" No Data Integrity "
                                                                "Support\n");
                                } else {
                                        kernel_dif_support = 1;
                                        if (strncmp(value, "0", 1))
                                                printf("  DIF/Type%1s ",value);
                                        else
                                                printf("  -         ");
                                }
                        } else {
                                printf("  -         ");
                        }
                }

                if (kernel_dif_support && block_scan(buff,opts)) {
                        if (if_directory_chdir(buff,aa_block.name)) {
                                char value[NAME_LEN_MAX];
                                char blkdir[NAME_LEN_MAX];
                                strncpy(blkdir,buff,NAME_LEN_MAX);
                                strcat(blkdir,"/");
                                strcat(blkdir,aa_block.name);
                                if (if_directory_chdir(blkdir,"integrity")) {
                                        if (!get_value(".", "format", value, 
                                                                NAME_LEN_MAX)) {
                                                if (opts->verbose)
                                                        printf(" No Data "
                                                                "Integrity "
                                                                "Support\n");
                                        } else {
                                                printf(" %-17s",value);
                                        }
                                } else {
                                        printf(" %-17s","-   ");
                                }
                        }
                }
        }

        printf("\n");
        if (opts->long_opt > 0)
                longer_d_entry(buff, devname, opts);
        if (opts->verbose > 0) {
                printf("  dir: %s  [", buff);
                if (if_directory_chdir(buff, "")) {
                        char wd[NAME_LEN_MAX];

                        if (NULL == getcwd(wd, NAME_LEN_MAX))
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
                            ((-1 == filter.l) || (s_hctl.l == filter.l)))
                                return 1;
                        else
                                return 0;
                } else
                        return 1;
        }
        /* Still need to filter out "." and ".." */
        return 0;
}

static int
sdev_scandir_sort(const void * a, const void * b)
{
        const char * lnam = (*(struct dirent **)a)->d_name;
        const char * rnam = (*(struct dirent **)b)->d_name;
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
list_sdevices(const struct lsscsi_opt_coll * opts)
{
        char buff[NAME_LEN_MAX];
        char name[NAME_LEN_MAX];
        struct dirent ** namelist;
        int num, k;

        strcpy(buff, sysfsroot);
        strcat(buff, bus_scsi_devs);

        num = scandir(buff, &namelist, sdev_scandir_select,
                      sdev_scandir_sort);
        if (num < 0) {  /* scsi mid level may not be loaded */
                if (opts->verbose > 0) {
                        snprintf(name, NAME_LEN_MAX, "scandir: %s", buff);
                        perror(name);
                        printf("SCSI mid level module may not be loaded\n");
                }
                if (opts->classic)
                        printf("Attached devices: none\n");
                return;
        }
        if (opts->classic)
                printf("Attached devices: %s\n", (num ? "" : "none"));

        for (k = 0; k < num; ++k) {
                strncpy(name, namelist[k]->d_name, NAME_LEN_MAX);
                transport_id = TRANSPORT_UNKNOWN;
                one_sdev_entry(buff, name, opts);
                free(namelist[k]);
        }
        free(namelist);
}

/* List host (initiator) attributes when --long given (one or more times). */
static void
longer_h_entry(const char * path_name, const struct lsscsi_opt_coll * opts)
{
        char value[NAME_LEN_MAX];

        if (opts->transport > 0) {
                transport_init_longer(path_name, opts);
                return;
        }
        if (opts->long_opt >= 3) {
                if (get_value(path_name, "can_queue", value, NAME_LEN_MAX))
                        printf("  can_queue=%s\n", value);
                else if (opts->verbose)
                        printf("  can_queue=?\n");
                if (get_value(path_name, "cmd_per_lun", value, NAME_LEN_MAX))
                        printf("  cmd_per_lun=%s\n", value);
                else if (opts->verbose)
                        printf("  cmd_per_lun=?\n");
                if (get_value(path_name, "host_busy", value, NAME_LEN_MAX))
                        printf("  host_busy=%s\n", value);
                else if (opts->verbose)
                        printf("  host_busy=?\n");
                if (get_value(path_name, "sg_tablesize", value, NAME_LEN_MAX))
                        printf("  sg_tablesize=%s\n", value);
                else if (opts->verbose)
                        printf("  sg_tablesize=?\n");
                if (get_value(path_name, "state", value, NAME_LEN_MAX))
                        printf("  state=%s\n", value);
                else if (opts->verbose)
                        printf("  state=?\n");
                if (get_value(path_name, "unchecked_isa_dma", value,
                              NAME_LEN_MAX))
                        printf("  unchecked_isa_dma=%s\n", value);
                else if (opts->verbose)
                        printf("  unchecked_isa_dma=?\n");
                if (get_value(path_name, "unique_id", value, NAME_LEN_MAX))
                        printf("  unique_id=%s\n", value);
                else if (opts->verbose)
                        printf("  unique_id=?\n");
        } else if (opts->long_opt > 0) {
                if (get_value(path_name, "cmd_per_lun", value, NAME_LEN_MAX))
                        printf("  cmd_per_lun=%-4s ", value);
                else
                        printf("  cmd_per_lun=???? ");

                if (get_value(path_name, "host_busy", value, NAME_LEN_MAX))
                        printf("host_busy=%-4s ", value);
                else
                        printf("host_busy=???? ");

                if (get_value(path_name, "sg_tablesize", value, NAME_LEN_MAX))
                        printf("sg_tablesize=%-4s ", value);
                else
                        printf("sg_tablesize=???? ");

                if (get_value(path_name, "unchecked_isa_dma", value,
                              NAME_LEN_MAX))
                        printf("unchecked_isa_dma=%-2s ", value);
                else
                        printf("unchecked_isa_dma=?? ");
                printf("\n");
                if (2 == opts->long_opt) {
                        if (get_value(path_name, "can_queue", value,
                                      NAME_LEN_MAX))
                                printf("  can_queue=%-4s ", value);
                        if (get_value(path_name, "state", value, NAME_LEN_MAX))
                                printf("  state=%-8s ", value);
                        if (get_value(path_name, "unique_id", value,
                                      NAME_LEN_MAX))
                                printf("  unique_id=%-2s ", value);
                        printf("\n");
                }
        }
}

static void
one_host_entry(const char * dir_name, const char * devname,
               const struct lsscsi_opt_coll * opts)
{
        char buff[NAME_LEN_MAX];
        char value[NAME_LEN_MAX];
        char * nullname = "<NULL>";
        unsigned int host_id;

        if (opts->classic) {
                // one_classic_host_entry(dir_name, devname, opts);
                printf("  <'--classic' not supported for hosts>\n");
                return;
        }
        if (1 == sscanf(devname, "host%u", &host_id))
                printf("[%u]  ", host_id);
        else
                printf("[?]  ");
        strcpy(buff, dir_name);
        strcat(buff, "/");
        strcat(buff, devname);
        if ((get_value(buff, "proc_name", value, NAME_LEN_MAX)) &&
            (strncmp(value, nullname, 6)))
                printf("  %-12s  ", value);
        else if (if_directory_chdir(buff, "device/../driver")) {
                char wd[NAME_LEN_MAX];

                if (NULL == getcwd(wd, NAME_LEN_MAX))
                        printf("  %-12s  ", nullname);
                else
                        printf("  %-12s  ", basename(wd));

        } else
                printf("  proc_name=????  ");
        if (opts->transport > 0) {
                if (transport_init(devname, /* opts, */ sizeof(value), value))
                        printf("%s\n", value);
                else
                        printf("\n");
        } else
                printf("\n");

        if (opts->long_opt > 0)
                longer_h_entry(buff, opts);

        if (opts->verbose > 0) {
                printf("  dir: %s\n  device dir: ", buff);
                if (if_directory_chdir(buff, "device")) {
                        char wd[NAME_LEN_MAX];

                        if (NULL == getcwd(wd, NAME_LEN_MAX))
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

static int
host_scandir_sort(const void * a, const void * b)
{
        const char * lnam = (*(struct dirent **)a)->d_name;
        const char * rnam = (*(struct dirent **)b)->d_name;
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
list_hosts(const struct lsscsi_opt_coll * opts)
{
        char buff[NAME_LEN_MAX];
        char name[NAME_LEN_MAX];
        struct dirent ** namelist;
        int num, k;

        strcpy(buff, sysfsroot);
        strcat(buff, scsi_host);

        num = scandir(buff, &namelist, host_scandir_select,
                      host_scandir_sort);
        if (num < 0) {
                snprintf(name, NAME_LEN_MAX, "scandir: %s", buff);
                perror(name);
                return;
        }
        if (opts->classic)
                printf("Attached hosts: %s\n", (num ? "" : "none"));

        for (k = 0; k < num; ++k) {
                strncpy(name, namelist[k]->d_name, NAME_LEN_MAX);
                transport_id = TRANSPORT_UNKNOWN;
                one_host_entry(buff, name, opts);
                free(namelist[k]);
        }
        free(namelist);
}

/* Return 0 if able to decode, otheriwse 1 */
static int
one_filter_arg(const char * arg, struct addr_hctl * filtp)
{
        const char * cp;
        const char * cpe;
        char buff[64];
        int val, k, n, res;

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
                if (n > ((int)sizeof(buff) - 1)) {
                        fprintf(stderr, "intermediate sting in %s too long "
                                "(n=%d)\n", arg, n);
                        return 1;
                }
                if ((n > 0) && ('-' != *cp) && ('*' != *cp) && ('?' != *cp)) {
                        strncpy(buff, cp, n);
                        buff[n] = '\0';
                        res = sscanf(buff, "%d", &val);
                        if (1 != res) {
                                fprintf(stderr, "cannot decode %s as an "
                                        "integer\n", buff);
                                return 1;
                        }
                }
                switch (k) {
                case 0: filtp->h = val; break;
                case 1: filtp->c = val; break;
                case 2: filtp->t = val; break;
                case 3: filtp->l = val; break;
                default:
                        fprintf(stderr, "expect three colons at most in %s\n", arg);
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
        filtp->l = -1;
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
                strcpy(b1p, a1p);
                b1p += n;
                *b1p++ = ':';
                rem -= (n + 1);
                if ((n = strlen(a2p)) > rem)
                        goto err_out;
                strcpy(b1p, a2p);
                if (a3p) {
                        b1p += n;
                        *b1p++ = ':';
                        rem -= (n + 1);
                        if ((n = strlen(a3p)) > rem)
                                goto err_out;
                        strcpy(b1p, a3p);
                        if (a4p) {
                                b1p += n;
                                *b1p++ = ':';
                                rem -= (n + 1);
                                if ((n = strlen(a4p)) > rem)
                                        goto err_out;
                                strcpy(b1p, a4p);
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

        sysfsroot[0] = '\0';
        invalidate_hctl(&filter);
        memset(&opts, 0, sizeof(opts));
        while (1) {
                int option_index = 0;

                c = getopt_long(argc, argv, "cdghHklLptvV", long_options,
                                &option_index);
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
                case 't':
                        ++opts.transport;
                        break;
                case 'v':
                        ++opts.verbose;
                        break;
                case 'V':
                        fprintf(stderr, "version: %s\n", version_str);
                        return 0;
#if 0
                case 'y':       /* sysfsroot <dir> */
                        strncpy(sysfsroot, optarg, sizeof(sysfsroot));
                        break;
#endif
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
                    (filter.t != -1) || (filter.l != -1))
                        filter_active = 1;
        }
        if ((opts.transport > 0) &&
            ((1 == opts.long_opt) || (2 == opts.long_opt))) {
                fprintf(stderr, "please '--list' (rather than '--long') "
                                "with --transport\n");
                return 1;
        }
        if ('\0' == sysfsroot[0]) {
                if (! find_sysfsroot()) {
                        fprintf(stderr, "Unable to locate sysfsroot. If "
                                "kernel >= 2.6.0\n    Try something like"
                                " 'mount -t sysfs none /sys'\n");
                        return 1;
                }
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
