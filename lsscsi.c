/* This is a utility program for listing SCSI devices and hosts (HBAs)
 * in the Linux operating system. It is applicable to kernel versions
 * greater than lk 2.5.50 .
 *  Copyright (C) 2002, 2003 D. Gilbert
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 */

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

#define NAME_LEN_MAX 260

static const char * version_str = "0.09  2003/4/4";
static char sysfsroot[NAME_LEN_MAX];
static const char * sysfs_name = "sysfs";
static const char * proc_mounts = "/proc/mounts";
static const char * scsi_devs = "/bus/scsi/devices";
static const char * scsi_hosts = "/class/scsi-host/devices";

#define MASK_CLASSIC 1
#define MASK_LONG 2
#define MASK_NAME 4
#define MASK_GENERIC 8


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
        "Unknown (expander)",
        "Object based storage",
	"Reserved (0x12)", "Reserved (0x13)", "Reserved (0x14)", 
	"Reserved (0x15)", "Reserved (0x16)", "Reserved (0x17)", 
	"Reserved (0x18)", "Reserved (0x19)", "Reserved (0x1a)", 
	"Reserved (0x1b)", "Reserved (0x1c)", "Reserved (0x1e)", 
	"Well known LU", 
	"No device", 
};

static const char * scsi_short_device_types[] =
{
        "disk   ", "tape   ", "printer", "process", "worm   ", "cd     ",
        "scanner", "optical", "mediumx", "comms  ", "(0xa)  ", "(0xb)  ",
        "storage", "enclosu", "s. disk", "opti rd", "expande", "obs    ",
	"(0x12) ", "(0x13) ", "(0x14) ", "(0x15) ", "(0x16) ", "(0x17) ", 
	"(0x18) ", "(0x19) ", "(0x1a) ", "(0x1b) ", "(0x1c) ", "(0x1e) ", 
	"know LU", "no dev ", 
};

static struct option long_options[] = {
	{"classic", 0, 0, 'c'},
	{"generic", 0, 0, 'g'},
	{"help", 0, 0, 'h'},
	{"hosts", 0, 0, 'H'},
	{"long", 0, 0, 'l'},
	{"name", 0, 0, 'n'},
	{"sysfsroot", 1, 0, 'y'},
	{"verbose", 0, 0, 'v'},
	{"version", 0, 0, 'z'},
	{0, 0, 0, 0}
};

struct addr_hcil {
	int h;
	int c;
	int i;
	int l;
};

static int cmp_hcil(const struct addr_hcil * le, const struct addr_hcil * ri)
{
	if (le->h == ri->h) {
		if (le->c == ri->c) {
			if (le->i == ri->i)
				return ((le->l == ri->l) ? 0 :
					((le->l < ri->l) ? -1 : 1));
			else
				return (le->i < ri->i) ? -1 : 1;
		} else
			return (le->c < ri->c) ? -1 : 1;
	} else
		return (le->h < ri->h) ? -1 : 1;
}

static void invalidate_hcil(struct addr_hcil * p)
{
	if (p) {
		p->h = -1;
		p->c = -1;
		p->i = -1;
		p->l = -1;
	}
}

static void usage()
{
	fprintf(stderr, "Usage: lsscsi   [--classic|-c] [--generic|-g]"
			" [--help|-h] [--hosts|-H]"
			"\n\t\t\[--long|-l] [--name|-n] [--sysfsroot=<dir>]"
			" [--verbose|-v]"
			"\n\t\t[--version|-V]\n");
	fprintf(stderr, "\t--classic  alternate output that is similar "
			"to 'cat /proc/scsi/scsi'\n");
	fprintf(stderr, "\t--generic  show scsi generic device name\n");
	fprintf(stderr, "\t--help  this usage information\n");
	fprintf(stderr, "\t--hosts  lists scsi hosts rather than scsi "
			"devices\n");
	fprintf(stderr, "\t--long  additional information output\n");
	fprintf(stderr, "\t--name  from INQUIRY VPD page 0x83 or "
			"manufactured name\n");
	fprintf(stderr, "\t--sysfsroot=<dir>  use /proc/mounts or <dir> "
			"for root of sysfs\n");
	fprintf(stderr, "\t--verbose  output path names were data "
			"is found\n");
	fprintf(stderr, "\t--version  output version string and exit\n");
}


/* Return 1 if found (in /proc/mounts), else 0 if problems */
static int find_sysfsroot()
{
	char buff[NAME_LEN_MAX];
	char dev[32];
	char fs_type[32];
	FILE * f;
	int res = 0;
	int n;

	if (NULL == (f = fopen(proc_mounts, "r"))) {
		snprintf(buff, sizeof(buff), "Unable to open %s for reading",
			 proc_mounts);
		perror(buff);
		return 0;
	}
	while (fgets(buff, sizeof(buff), f)) {
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

/*  Parse colon_list into host/channel/id/lun ("hcil") array, 
 *  return 1 if successful, else 0 */
static int parse_colon_list(const char * colon_list, struct addr_hcil * outp)
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
	if (1 != sscanf(colon_list, "%d", &outp->i))
		return 0;
	if (NULL == (elem_end = strchr(colon_list, ':')))
		return 0;
	colon_list = elem_end + 1;
	if (1 != sscanf(colon_list, "%d", &outp->l))
		return 0;
	return 1;
}

static void longer_entry(const char * path_name)
{
	char value[NAME_LEN_MAX];

	if (get_value(path_name, "online", value, NAME_LEN_MAX))
		printf("  online=%s", value);
	else
		printf(" online=?");
	if (get_value(path_name, "access_count", value, NAME_LEN_MAX))
		printf(" access_count=%s", value);
	else
		printf(" access_count=?");
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
	printf("\n");
}

static void one_classic_sdev_entry(const char * dir_name, 
				   const char * devname, int do_verbose, 
				   int out_mask)
{
	struct addr_hcil hcil;
	char buff[NAME_LEN_MAX];
	char value[NAME_LEN_MAX];
	int type, scsi_level;

	strcpy(buff, dir_name);
	strcat(buff, "/");
	strcat(buff, devname);
	if (! parse_colon_list(devname, &hcil))
       		invalidate_hcil(&hcil);
	printf("Host: scsi%d Channel: %02d Id: %02d Lun: %02d\n",
	       hcil.h, hcil.c, hcil.i, hcil.l);

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
	if (out_mask & MASK_GENERIC) {
		char extra[NAME_LEN_MAX];
		const char * bnp;

		bnp = basename(buff);
		strcpy(extra, bnp);
		strcat(extra, ":gen/kdev");
		if (get_value(buff, extra, value, NAME_LEN_MAX)) {
			int kd, majj, minn;

			if (1 == sscanf(value, "%x", &kd)) {
				majj = kd / 256;
				minn = kd % 256;
				if (SCSI_GENERIC_MAJOR == majj)
					printf("/dev/sg%d\n", minn);
				else
					printf("unexpected sg major\n");
			}
			else
				printf("unable to decode kdev\n");
		}
		else
			printf("-\n");
	}
	if (out_mask & MASK_NAME) {
		if (get_value(buff, "name", value, NAME_LEN_MAX))
			printf("  name: %s\n", value);
		else
			printf("  name: ??\n");
	}
	if (out_mask & MASK_LONG)
		longer_entry(buff);
	if (do_verbose)
		printf("  dir: %s\n", buff);
}

static void one_sdev_entry(const char * dir_name, const char * devname,
			   int do_verbose, int out_mask)
{
	char buff[NAME_LEN_MAX];
	char value[NAME_LEN_MAX];
	int type;

	if (out_mask & MASK_CLASSIC) {
		one_classic_sdev_entry(dir_name, devname, do_verbose, 
				       out_mask);
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

	if (if_directory_chdir(buff, "block")) { /* look for block device */
		char wd[NAME_LEN_MAX];

		if (NULL == getcwd(wd, NAME_LEN_MAX))
			printf("block_dev error");
		else
			printf("/dev/%s", basename(wd));
	}
	else { /* look for tape device */
		char extra[NAME_LEN_MAX];
		const char * bnp;
		int found = 0;

		bnp = basename(buff);
		strcpy(extra, bnp);
		strcat(extra, ":mt/kdev");
		if (get_value(buff, extra, value, NAME_LEN_MAX))
	       		found = 1; /* st device */
		else {
			strcpy(extra, bnp);
			strcat(extra, ":ot/kdev");
			if (get_value(buff, extra, value, NAME_LEN_MAX))
				found = 1;	/* osst device */
		}
		if (found) {
			int kd, majj, minn;

			if (1 == sscanf(value, "%x", &kd)) {
				majj = kd / 256;
				minn = kd % 256;
				/* for tape devices, 0 <= minn <= 31 */
				if (SCSI_TAPE_MAJOR == majj)
					printf("/dev/st%d", minn);
				else if (OSST_MAJOR == majj)
					printf("/dev/osst%d", minn);
				else
					printf("unexpected tape major");
			}
			else
				printf("unable to decode kdev");
		}
		else
			printf("-       ");
	}

	if (out_mask & MASK_GENERIC) {
		char extra[NAME_LEN_MAX];
		const char * bnp;

		bnp = basename(buff);
		strcpy(extra, bnp);
		strcat(extra, ":gen/kdev");
		if (get_value(buff, extra, value, NAME_LEN_MAX)) {
			int kd, majj, minn;

			if (1 == sscanf(value, "%x", &kd)) {
				majj = kd / 256;
				minn = kd % 256;
				if (SCSI_GENERIC_MAJOR == majj)
					printf("  /dev/sg%d", minn);
				else
					printf("  unexpected sg major");
			}
			else
				printf("  unable to decode kdev");
		}
		else
			printf("  -");
	}
	printf("\n");
	if (out_mask & MASK_NAME) {
		if (get_value(buff, "name", value, NAME_LEN_MAX))
			printf("  name: %s\n", value);
		else
			printf("  name: ??\n");
	}
	if (out_mask & MASK_LONG)
		longer_entry(buff);
	if (do_verbose) {
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
	if (strstr(s->d_name, "mt"))
		return 0;	/* st auxiliary device names */
	if (strstr(s->d_name, "ot"))
		return 0;	/* osst auxiliary device names */
	if (strstr(s->d_name, "gen"))
		return 0;
	if (strchr(s->d_name, ':'))
		return 1;
	return 0;
}

static int sdev_scandir_sort(const void * a, const void * b)
{
	const char * lnam = (*(struct dirent **)a)->d_name;
	const char * rnam = (*(struct dirent **)b)->d_name;
	struct addr_hcil left_hcil;
	struct addr_hcil right_hcil;

	if (! parse_colon_list(lnam, &left_hcil)) {
		fprintf(stderr, "sdev_scandir_sort: left parse failed \n");
		return -1;
	}
	if (! parse_colon_list(rnam, &right_hcil)) {
		fprintf(stderr, "sdev_scandir_sort: right parse failed \n");
		return 1;
	}
	return cmp_hcil(&left_hcil, &right_hcil);
}

static void list_sdevices(int do_verbose, int out_mask)
{
	char buff[NAME_LEN_MAX];
	char name[NAME_LEN_MAX];
        struct dirent ** namelist;
	int num, k;

	strcpy(buff, sysfsroot);
	strcat(buff, scsi_devs);

	num = scandir(buff, &namelist, sdev_scandir_select, 
		      sdev_scandir_sort);
	if (num < 0) {
		snprintf(name, NAME_LEN_MAX, "scandir: %s", buff);
		perror(name);
		return;
	}
	if (out_mask & MASK_CLASSIC)
		printf("Attached devices: %s\n", (num ? "" : "none"));

	for (k = 0; k < num; ++k) {
		strncpy(name, namelist[k]->d_name, NAME_LEN_MAX);
		one_sdev_entry(buff, name, do_verbose, out_mask);
		free(namelist[k]);
	}
	free(namelist);
}

static void one_host_entry(const char * dir_name, const char * devname,
			   int do_verbose, int out_mask)
{
	char buff[NAME_LEN_MAX];
	char value[NAME_LEN_MAX];
	unsigned int host_id;

	if (out_mask & MASK_CLASSIC) {
		// one_classic_host_entry(dir_name, devname, do_verbose, 
				       // out_mask);
		return;
	}
	strcpy(buff, dir_name);
	strcat(buff, "/");
	strcat(buff, devname);
	if (get_value(buff, "class_name", value, NAME_LEN_MAX)) {
		if (1 == sscanf(value, "scsi%u", &host_id))
			printf("[%u]  ", host_id);
		else
			printf("[?]  ");
	}
	else
		printf("[??] ");

	if (get_value(buff, "cmd_per_lun", value, NAME_LEN_MAX))
		printf("cmd_per_lun=%-4s ", value);
	else
		printf("cmd_per_lun=???? ");

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
	if (out_mask & MASK_NAME) {
		if (get_value(buff, "name", value, NAME_LEN_MAX))
			printf("  name: %s\n", value);
		else
			printf("  name: ??\n");
	}
	if (do_verbose) {
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

static int host_scandir_select(const struct dirent * s)
{
	if (isdigit(s->d_name[0]))
		return 1;
	return 0;
}

static int host_scandir_sort(const void * a, const void * b)
{
	const char * lnam = (*(struct dirent **)a)->d_name;
	const char * rnam = (*(struct dirent **)b)->d_name;
	unsigned int l, r;

	if (1 != sscanf(lnam, "%u", &l))
		return -1;
	if (1 != sscanf(rnam, "%u", &r))
		return 1;
	if (l < r)
		return -1;
	else if (r < l)
		return 1;
	return 0;
}

static void list_hosts(int do_verbose, int out_mask)
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
	if (out_mask & MASK_CLASSIC)
		printf("Attached hosts: %s\n", (num ? "" : "none"));

	for (k = 0; k < num; ++k) {
		strncpy(name, namelist[k]->d_name, NAME_LEN_MAX);
		one_host_entry(buff, name, do_verbose, out_mask);
		free(namelist[k]);
	}
	free(namelist);
}


int main(int argc, char **argv)
{
	int c;
	int do_sdevices = 1;
	int do_hosts = 0;
	int out_mask = 0;
	int do_verbose = 0;

	sysfsroot[0] = '\0';
	while (1) {
		int option_index = 0;

		c = getopt_long(argc, argv, "cghHlnvV", long_options, 
				&option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'c':
			out_mask |= MASK_CLASSIC;
			break;
		case 'g':
			out_mask |= MASK_GENERIC;
			break;
		case 'h':
			usage();
			return 0;
		case 'H':
			do_hosts = 1;
			break;
		case 'l':
			out_mask |= MASK_LONG;
			break;
		case 'n':
			out_mask |= MASK_NAME;
			break;
		case 'v':
			++do_verbose;
			break;
		case 'V':
			fprintf(stderr, "version: %s\n", version_str);
			return 0;
		case 'y':	/* sysfsroot=<dir> */
			strncpy(sysfsroot, optarg, sizeof(sysfsroot));
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
		fprintf(stderr, "unexpected non‐option arguments: ");
		while (optind < argc)
			fprintf(stderr, "%s ", argv[optind++]);
		fprintf(stderr, "\n");
	}
	if ('\0' == sysfsroot[0]) {
		if (! find_sysfsroot()) {
			fprintf(stderr, "Unable to locate sysfsroot. If "
				"kernel >= 2.5.48\n    Try something like"
				" 'mount -t sysfs none /sys'\n");
			return 1;
		}
	}
	if (do_verbose) {
		printf(" sysfsroot: %s\n", sysfsroot);
	}
	if (do_hosts)
		list_hosts(do_verbose, out_mask);
	else if (do_sdevices)
		list_sdevices(do_verbose, out_mask);
	return 0;
}
