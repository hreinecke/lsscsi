/* This is a utility program for listing SCSI devices and hosts (HBAs)
 * in the Linux operating system. It is applicable to kernel versions
 * 2.6.1 and greater.
 *  Copyright (C) 2003-2005 D. Gilbert
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 */

#define _XOPEN_SOURCE 500
#define _GNU_SOURCE

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

#define NAME_LEN_MAX 260

static const char * version_str = "0.16  2005/12/30";
static char sysfsroot[NAME_LEN_MAX];
static const char * sysfs_name = "sysfs";
static const char * sysfs_test_dir = "/sys/class";
static const char * sysfs_test_top = "/sys";
static const char * proc_mounts = "/proc/mounts";
static const char * scsi_devs = "/bus/scsi/devices";
static const char * scsi_hosts = "/class/scsi_host";
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
        int verbose;
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
/*      {"name", 0, 0, 'n'},    */
/*      {"sysfsroot", 1, 0, 'y'},       */
/*      {"transport", 0, 0, 't'},       */
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

static int cmp_hctl(const struct addr_hctl * le, const struct addr_hctl * ri)
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

static void invalidate_hctl(struct addr_hctl * p)
{
        if (p) {
                p->h = -1;
                p->c = -1;
                p->t = -1;
                p->l = -1;
        }
}

static void usage()
{
        fprintf(stderr, "Usage: lsscsi   [--classic] [--device] [--generic]"
                        " [--help] [--hosts]\n"
                        "\t\t[--kname] [--long] [--verbose]"
                        " [--version]\n"
                        "\t\t[<h:c:t:l>]\n");
        fprintf(stderr, "\t--classic|-c  alternate output similar "
                        "to 'cat /proc/scsi/scsi'\n");
        fprintf(stderr, "\t--device|-d   show device node's major + minor"
                        " numbers\n");
        fprintf(stderr, "\t--generic|-g  show scsi generic device name\n");
        fprintf(stderr, "\t--help|-h     this usage information\n");
        fprintf(stderr, "\t--hosts|-H    lists scsi hosts rather than scsi "
                        "devices\n");
        fprintf(stderr, "\t--kname|-k    show kernel name instead of device"
                        " node name\n");
        fprintf(stderr, "\t--long|-l     additional information output\n");
/*  fprintf(stderr, "\t--transport|-t  output transport information\n"); */
        fprintf(stderr, "\t--verbose|-v  output path names where data "
                        "is found\n");
        fprintf(stderr, "\t--version|-V  output version string and exit\n");
        fprintf(stderr, "\t<h:c:t:l>  filter output list (def: "
                        "'- - - -' (all))\n");
}


/* Return 1 if found (in /proc/mounts or /sys/class directory exists),
   else 0 if problems */
static int find_sysfsroot()
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

/* Return 1 if directory, else 0 */
static int if_directory_chdir(const char * dir_name, const char * base_name)
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

/* Return 1 if found, else 0 if problems */
static int get_value(const char * dir_name, const char * base_name,
                     char * value, int max_value_len)
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
                fclose(f);
                return 0;
        }
        len = strlen(value);
        if ((len > 0) && (value[len - 1] == '\n'))
                value[len - 1] = '\0';
        fclose(f);
        return 1;
}

/* Allocate dev_node_list & collect info on every node in /dev. */
static void collect_dev_nodes ()
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
static void free_dev_node_list ()
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
static int get_dev_node (char *wd, char *node, enum dev_type type)
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

/*  Parse colon_list into host/channel/target/lun ("hctl") array, 
 *  return 1 if successful, else 0 */
static int parse_colon_list(const char * colon_list, struct addr_hctl * outp)
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

static void longer_entry(const char * path_name,
                         const struct lsscsi_opt_coll * opts)
{
        char value[NAME_LEN_MAX];

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

static void one_classic_sdev_entry(const char * dir_name, 
                                   const char * devname,
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
                if (if_directory_chdir(buff, "generic")) { 
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
                longer_entry(buff, opts);
        if (opts->verbose)
                printf("  dir: %s\n", buff);
}

#define FT_OTHER 0
#define FT_BLOCK 1
#define FT_CHAR 2

struct non_sg_item {
        char name[NAME_LEN_MAX];
        int ft;
};

static struct non_sg_item non_sg;

static int non_sg_scandir_select(const struct dirent * s)
{
        if (FT_OTHER != non_sg.ft)
                return 0;
        if (DT_LNK != s->d_type)
                return 0;
        if (0 == strncmp("scsi_changer", s->d_name, 12)) {
                strncpy(non_sg.name, s->d_name, NAME_LEN_MAX);
                non_sg.ft = FT_CHAR;
                return 1;
        } else if (0 == strcmp("block", s->d_name)) {
                strcpy(non_sg.name, s->d_name);
                non_sg.ft = FT_BLOCK;
                return 1;
        } else if (0 == strcmp("tape", s->d_name)) {
                strcpy(non_sg.name, s->d_name);
                non_sg.ft = FT_CHAR;
                return 1;
        } else if (0 == strncmp("onstream_tape:os", s->d_name, 16)) {
                strcpy(non_sg.name, s->d_name);
                non_sg.ft = FT_CHAR;
                return 1;
        } else
                return 0;
}

static int non_sg_scan(const char * dir_name,
                       const struct lsscsi_opt_coll * opts)
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

static void one_sdev_entry(const char * dir_name, const char * devname,
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

        if ((1 == non_sg_scan(buff, opts)) &&
            (if_directory_chdir(buff, non_sg.name))) {
                char wd[NAME_LEN_MAX];

                if (NULL == getcwd(wd, NAME_LEN_MAX))
                        printf("getcwd error");
                else {
                        char dev_node[NAME_MAX + 1] = "";
                        enum dev_type typ;

                        typ = (FT_BLOCK == non_sg.ft) ? BLK_DEV : CHR_DEV;
                        if (opts->kname)
                                snprintf(dev_node, NAME_MAX, "%s/%s",
                                        dev_dir, basename(wd));
                        else if (!get_dev_node(wd, dev_node, typ))
                                snprintf(dev_node, NAME_MAX, "-       ");

                        printf("%s", dev_node);
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
                if (if_directory_chdir(buff, "generic")) { 
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
        printf("\n");
        if (opts->long_opt > 0)
                longer_entry(buff, opts);
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

static int sdev_scandir_select(const struct dirent * s)
{
/* Following no longer needed but leave for early lk 2.6 series */
        if (strstr(s->d_name, "mt"))
                return 0;       /* st auxiliary device names */
        if (strstr(s->d_name, "ot"))
                return 0;       /* osst auxiliary device names */
        if (strstr(s->d_name, "gen"))
                return 0;
/* Above no longer needed but leave for early lk 2.6 series */
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

static int sdev_scandir_sort(const void * a, const void * b)
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

static void list_sdevices(const struct lsscsi_opt_coll * opts)
{
        char buff[NAME_LEN_MAX];
        char name[NAME_LEN_MAX];
        struct dirent ** namelist;
        int num, k;

        strcpy(buff, sysfsroot);
        strcat(buff, scsi_devs);

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
                one_sdev_entry(buff, name, opts);
                free(namelist[k]);
        }
        free(namelist);
}

static void one_host_entry(const char * dir_name, const char * devname,
                           const struct lsscsi_opt_coll * opts)
{
        char buff[NAME_LEN_MAX];
        char value[NAME_LEN_MAX];
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
        if (get_value(buff, "proc_name", value, NAME_LEN_MAX))
                printf("  %-12s\n", value);
        else
                printf("  proc_name=????\n");

        if (opts->long_opt >= 3) {
                if (get_value(buff, "cmd_per_lun", value, NAME_LEN_MAX))
                        printf("  cmd_per_lun=%s\n", value);
                else if (opts->verbose)
                        printf("  cmd_per_lun=?\n");
                if (get_value(buff, "host_busy", value, NAME_LEN_MAX))
                        printf("  host_busy=%s\n", value);
                else if (opts->verbose)
                        printf("  host_busy=?\n");
                if (get_value(buff, "sg_tablesize", value, NAME_LEN_MAX))
                        printf("  sg_tablesize=%s\n", value);
                else if (opts->verbose)
                        printf("  sg_tablesize=?\n");
                if (get_value(buff, "unchecked_isa_dma", value, NAME_LEN_MAX))
                        printf("  unchecked_isa_dma=%s\n", value);
                else if (opts->verbose)
                        printf("  unchecked_isa_dma=?\n");
                if (get_value(buff, "unique_id", value, NAME_LEN_MAX))
                        printf("  unique_id=%s\n", value);
                else if (opts->verbose)
                        printf("  unique_id=?\n");
        } else if (opts->long_opt > 0) {
                if (get_value(buff, "cmd_per_lun", value, NAME_LEN_MAX))
                        printf("  cmd_per_lun=%-4s ", value);
                else
                        printf("  cmd_per_lun=???? ");

                if (get_value(buff, "host_busy", value, NAME_LEN_MAX))
                        printf("host_busy=%-4s ", value);
                else
                        printf("host_busy=???? ");

                if (get_value(buff, "sg_tablesize", value, NAME_LEN_MAX))
                        printf("sg_tablesize=%-4s ", value);
                else
                        printf("sg_tablesize=???? ");

                if (get_value(buff, "unchecked_isa_dma", value, NAME_LEN_MAX))
                        printf("unchecked_isa_dma=%-2s ", value);
                else
                        printf("unchecked_isa_dma=?? ");
                printf("\n");
        }
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

static int host_scandir_select(const struct dirent * s)
{
        if (0 == strncmp("host", s->d_name, 4))
                return 1;
        return 0;
}

static int host_scandir_sort(const void * a, const void * b)
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

static void list_hosts(const struct lsscsi_opt_coll * opts)
{
        char buff[NAME_LEN_MAX];
        char name[NAME_LEN_MAX];
        struct dirent ** namelist;
        int num, k;

        strcpy(buff, sysfsroot);
        strcat(buff, scsi_hosts);

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
                one_host_entry(buff, name, opts);
                free(namelist[k]);
        }
        free(namelist);
}


static int one_filter_arg(const char * arg, struct addr_hctl * filtp)
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

static int decode_filter_arg(const char * a1p, const char * a2p,
                             const char * a3p, const char * a4p,
                             struct addr_hctl * filtp)
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


int main(int argc, char **argv)
{
        int c;
        int do_sdevices = 1;
        int do_hosts = 0;
        /* int do_transport = 0; */
        struct lsscsi_opt_coll opts;

        sysfsroot[0] = '\0';
        invalidate_hctl(&filter);
        memset(&opts, 0, sizeof(opts));
        while (1) {
                int option_index = 0;

                c = getopt_long(argc, argv, "cdghHklvV", long_options, 
                                &option_index);
                if (c == -1)
                        break;

                switch (c) {
                case 'c':
                        opts.classic = 1;
                        break;
                case 'd':
                        opts.dev_maj_min = 1;
                        break;
                case 'g':
                        opts.generic = 1;
                        break;
                case 'h':
                        usage();
                        return 0;
                case 'H':
                        do_hosts = 1;
                        break;
                case 'k':
                        opts.kname = 1;
                        break;
                case 'l':
                        ++opts.long_opt;
                        break;
#if 0
                case 't':
                        do_transport = 1;
                        break;
#endif
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
