/* This is a utility program for listing SCSI devices and hosts (HBAs)
 * in the Linux operating system. It is applicable to kernel versions
 * 2.6.1 and greater.
 *  Copyright (C) 2002-2004 D. Gilbert
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
#include <libgen.h>

#include <sysfs/libsysfs.h>

// This utility has been ported to use the libsysfs library with help
// from Ananth Narayan <ananth at in dot ibm dot com>.

// Following define assumes dlist_sort_custom() which became available in
// sysfsutils-1.2.0
#define HAVE_DLIST_SORT_CUSTOM

static const char * version_str = "0.14  2004/9/20";
static const char * scsi_bus_name = "scsi";
static const char * scsi_host_name = "scsi_host";

#define MASK_CLASSIC 1
#define MASK_LONG 2
/* #define MASK_NAME 4 */
#define MASK_GENERIC 8
#define MASK_DEVICE 0x10


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
        "storage", "enclosu", "s. disk", "opti rd", "expande", "osd    ",
	"adi    ", "(0x13) ", "(0x14) ", "(0x15) ", "(0x16) ", "(0x17) ", 
	"(0x18) ", "(0x19) ", "(0x1a) ", "(0x1b) ", "(0x1c) ", "(0x1e) ", 
	"know LU", "no dev ", 
};

static struct option long_options[] = {
	{"classic", 0, 0, 'c'},
	{"device", 0, 0, 'd'},
	{"generic", 0, 0, 'g'},
	{"help", 0, 0, 'h'},
	{"hosts", 0, 0, 'H'},
	{"long", 0, 0, 'l'},
	{"verbose", 0, 0, 'v'},
	{"version", 0, 0, 'V'},
	{0, 0, 0, 0}
};

struct addr_hcil {
	int h;
	int c;
	int i;
	int l;
};

#ifdef HAVE_DLIST_SORT_CUSTOM
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
#endif

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
	fprintf(stderr, "Usage: lsscsi   [--classic|-c] [--device|-d]"
			" [--generic|-g] [--help|-h]\n\t\t[--hosts|-H] "
			"[--long|-l] [--verbose|-v] [--version|-V]\n");
	fprintf(stderr, "\t--classic  alternate output that is similar "
			"to 'cat /proc/scsi/scsi'\n");
	fprintf(stderr, "\t--device   show device node's major + minor"
			" numbers\n");
	fprintf(stderr, "\t--generic  show scsi generic device name\n");
	fprintf(stderr, "\t--help     this usage information\n");
	fprintf(stderr, "\t--hosts    lists scsi hosts rather than scsi "
			"devices\n");
	fprintf(stderr, "\t--long     additional information output\n");
	fprintf(stderr, "\t--verbose  output path names were data "
			"is found\n");
	fprintf(stderr, "\t--version  output version string and exit\n");
}

static struct sysfs_link * open_link(const char * dir_name,
				     const char * base_name)
{
	char buff[SYSFS_PATH_MAX];

	strcpy(buff, dir_name);
	strcat(buff, "/");
	strcat(buff, base_name);
	return sysfs_open_link(buff);
}

/* Return 1 if found, else 0 if problems */
static int get_sdev_value(struct sysfs_device * sdevp, const char * search,
		          char * value, int max_value_len)
{
	int len;
	struct sysfs_attribute * sattrp;

	sattrp = sysfs_get_device_attr(sdevp, search);
	if (NULL == sattrp)
		return 0;
	if (max_value_len <= 0)
		return 1;
	strncpy(value, sattrp->value, max_value_len);
	value[max_value_len - 1] = '\0';
	len = strlen(value);
	if ((len > 0) && (value[len - 1] == '\n'))
		value[len - 1] = '\0';
	return 1;
}

/* Return 1 if found, else 0 if problems */
static int get_cdev_value(struct sysfs_class_device * cdevp, 
		const char * search, char * value, int max_value_len)
{
	int len;
	struct sysfs_attribute * sattrp;

	sattrp = sysfs_get_classdev_attr(cdevp, (char *)search);
	if (NULL == sattrp)
		return 0;
	if (max_value_len <= 0)
		return 1;
	strncpy(value, sattrp->value, max_value_len);
	value[max_value_len - 1] = '\0';
	len = strlen(value);
	if ((len > 0) && (value[len - 1] == '\n'))
		value[len - 1] = '\0';
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

static void llonger_entry(struct sysfs_device * sdevp)
{
	char value[SYSFS_NAME_LEN];

	if (get_sdev_value(sdevp, "state", value, SYSFS_NAME_LEN))
		printf("  state=%s", value);
	else
		printf(" state=?");
	if (get_sdev_value(sdevp, "queue_depth", value, SYSFS_NAME_LEN))
		printf(" queue_depth=%s", value);
	else
		printf(" queue_depth=?");
	if (get_sdev_value(sdevp, "scsi_level", value, SYSFS_NAME_LEN))
		printf(" scsi_level=%s", value);
	else
		printf(" scsi_level=?");
	if (get_sdev_value(sdevp, "type", value, SYSFS_NAME_LEN))
		printf(" type=%s", value);
	else
		printf(" type=?");
	if (get_sdev_value(sdevp, "device_blocked", value, SYSFS_NAME_LEN))
		printf(" device_blocked=%s", value);
	else
		printf(" device_blocked=?");
	if (get_sdev_value(sdevp, "timeout", value, SYSFS_NAME_LEN))
		printf(" timeout=%s", value);
	else
		printf(" timeout=?");
	printf("\n");
}

static void one_classic_sdev_entry(struct sysfs_device * sdevp,
				   int do_verbose, int out_mask)
{
	struct addr_hcil hcil;
	char value[SYSFS_NAME_LEN];
	int type, scsi_level;

	if (! parse_colon_list(sdevp->name, &hcil))
       		invalidate_hcil(&hcil);
	printf("Host: scsi%d Channel: %02d Id: %02d Lun: %02d\n",
	       hcil.h, hcil.c, hcil.i, hcil.l);

	if (get_sdev_value(sdevp, "vendor", value, SYSFS_NAME_LEN))
		printf("  Vendor: %-8s", value);
	else
		printf("  Vendor: ?       ");
	if (get_sdev_value(sdevp, "model", value, SYSFS_NAME_LEN))
		printf(" Model: %-16s", value);
	else
		printf(" Model: ?               ");
	if (get_sdev_value(sdevp, "rev", value, SYSFS_NAME_LEN))
		printf(" Rev: %-4s", value);
	else
		printf(" Rev: ?   ");
	printf("\n");
	if (! get_sdev_value(sdevp, "type", value, SYSFS_NAME_LEN)) {
		printf("  Type:   %-33s", "?");
	} else if (1 != sscanf(value, "%d", &type)) {
		printf("  Type:   %-33s", "??");
	} else if ((type < 0) || (type > 31)) {
		printf("  Type:   %-33s", "???");
	} else
		printf("  Type:   %-33s", scsi_device_types[type]);
	if (! get_sdev_value(sdevp, "scsi_level", value, SYSFS_NAME_LEN)) {
		printf("ANSI SCSI revision: ?\n");
	} else if (1 != sscanf(value, "%d", &scsi_level)) {
		printf("ANSI SCSI revision: ??\n");
	} else
		printf("ANSI SCSI revision: %02x\n", (scsi_level - 1) ?
		                            scsi_level - 1 : 1);
	if (out_mask & MASK_GENERIC) {
		struct sysfs_link * slinkp;

		if ((slinkp = open_link(sdevp->path, "generic"))) {
			char * tp = slinkp->target;

			printf("  /dev/%s\n", basename(tp));
			sysfs_close_link(slinkp);
		} else
			printf("-\n");
	}
	if (out_mask & MASK_LONG)
		llonger_entry(sdevp);
	if (do_verbose)
		printf("  dir: %s\n", sdevp->path);
}

static void one_sdev_entry(struct sysfs_device * sdevp, int do_verbose,
			   int out_mask)
{
	char value[SYSFS_NAME_LEN];
	int type;
	struct sysfs_attribute * sattrp;
	struct sysfs_link * slinkp;

	if (out_mask & MASK_CLASSIC) {
		one_classic_sdev_entry(sdevp, do_verbose, out_mask);
		return;
	}
	snprintf(value, SYSFS_NAME_LEN, "[%s]", sdevp->name);
	printf("%-13s", value);
	sattrp = sysfs_get_device_attr(sdevp, "type");
	if (NULL == sattrp) {
		printf("type?   ");
	} else if (1 != sscanf(sattrp->value, "%d", &type)) {
		printf("type??  ");
	} else if ((type < 0) || (type > 31)) {
		printf("type??? ");
	} else
		printf("%s ", scsi_short_device_types[type]);


	if (get_sdev_value(sdevp, "vendor", value, SYSFS_NAME_LEN))
		printf("%-8s ", value);
	else
		printf("vendor?  ");

	if (get_sdev_value(sdevp, "model", value, SYSFS_NAME_LEN))
		printf("%-16s ", value);
	else
		printf("model?           ");

	if (get_sdev_value(sdevp, "rev", value, SYSFS_NAME_LEN))
		printf("%-4s  ", value);
	else
		printf("rev?  ");

	if ((slinkp = open_link(sdevp->path, "block"))) { 
		/* look for block device */
		char * tp = slinkp->target;

		printf("/dev/%s", basename(tp));
		if (out_mask & MASK_DEVICE) {
			struct sysfs_class_device * cdevp;

			if ((cdevp = sysfs_open_class_device_path(tp))) {
				if (get_cdev_value(cdevp, "dev", value,
						   SYSFS_NAME_LEN))
					printf("[%s]", value);
				else
					printf("[dev?]");
				sysfs_close_class_device(cdevp);
			} else
				printf("[dev??]");
		}
		sysfs_close_link(slinkp);
	} else { /* look for tape device */
		if ((slinkp = open_link(sdevp->path, "tape"))) {
			char * tp = slinkp->target;
                        char s[SYSFS_NAME_LEN];
                        char * cp;

			strcpy(s, basename(tp));
			if ((cp = strchr(s, 'm')))
				s[cp - s] = '\0';
			printf("/dev/%s", s);
			if (out_mask & MASK_DEVICE) {
				struct sysfs_class_device * cdevp;

				if ((cdevp = sysfs_open_class_device_path(tp))) {
					if (get_cdev_value(cdevp, "dev",
						 value, SYSFS_NAME_LEN))
						printf("[%s]", value);
					else
						printf("[dev?]");
					sysfs_close_class_device(cdevp);
				} else
					printf("[dev??]");
			}
			sysfs_close_link(slinkp);
		} else
			printf("-       ");
	}
	if (out_mask & MASK_GENERIC) {
		if ((slinkp = open_link(sdevp->path, "generic"))) {
			char * tp = slinkp->target;

			printf("  /dev/%s", basename(tp));
			if (out_mask & MASK_DEVICE) {
				struct sysfs_class_device * cdevp;

				if ((cdevp = sysfs_open_class_device_path(tp))) {
					if (get_cdev_value(cdevp, "dev", value,
						   	SYSFS_NAME_LEN))
						printf("[%s]", value);
					else
						printf("[dev?]");
					sysfs_close_class_device(cdevp);
				} else
					printf("[dev??]");
			}
			sysfs_close_link(slinkp);
		} else
			printf("  -");
	}
	printf("\n");
	if (out_mask & MASK_LONG)
		llonger_entry(sdevp);
	if (do_verbose)
		printf("  dir: %s\n", sdevp->path);
}

#ifdef HAVE_DLIST_SORT_CUSTOM
static int sdev_dlist_comp(void * a, void * b)
{
	const char * lnam = ((struct sysfs_device *)a)->name;
	const char * rnam = ((struct sysfs_device *)b)->name;
	struct addr_hcil left_hcil;
	struct addr_hcil right_hcil;

	if (! parse_colon_list(lnam, &left_hcil)) {
		fprintf(stderr, "sdev_dlist_comp: left parse failed \n");
		return -1;
	}
	if (! parse_colon_list(rnam, &right_hcil)) {
		fprintf(stderr, "sdev_dlist_comp: right parse failed \n");
		return 1;
	}
	return cmp_hcil(&left_hcil, &right_hcil);
}
#endif

static void list_sdevices(int do_verbose, int out_mask)
{
	struct sysfs_bus * sbusp;
	struct sysfs_device * sdevp;
	struct dlist * lp = NULL;
	int num = 0;

	sbusp = sysfs_open_bus(scsi_bus_name);
	if (NULL == sbusp) {
		if (do_verbose) {
			printf("SCSI mid level module may not be loaded\n");
		}
		if (out_mask & MASK_CLASSIC)
			printf("Attached devices: none\n");
		return;
	}
	lp = sysfs_get_bus_devices(sbusp);
	if (NULL == lp) {
		if (out_mask & MASK_CLASSIC)
			printf("Attached devices: none\n");
		return;
	}
#ifdef HAVE_DLIST_SORT_CUSTOM
	dlist_sort_custom(lp, sdev_dlist_comp);
#endif
	if (out_mask & MASK_CLASSIC) {
		dlist_for_each_data(lp, sdevp, struct sysfs_device) {
			++num;
		}
		printf("Attached devices: %s\n", (num ? "" : "none"));
	}
	dlist_for_each_data(lp, sdevp, struct sysfs_device) {
		one_sdev_entry(sdevp, do_verbose, out_mask);
	}
	sysfs_close_bus(sbusp);
}

static void one_host_entry(struct sysfs_class_device * cdevp,
			    int do_verbose, int out_mask)
{
	char value[SYSFS_NAME_LEN];
	unsigned int host_id;

	if (out_mask & MASK_CLASSIC) {
		// one_classic_host_entry(dir_name, devname, do_verbose, 
				       // out_mask);
		printf("  <'--classic' not supported for hosts>\n");
		return;
	}
	if (1 == sscanf(cdevp->name, "host%u", &host_id))
		printf("[%u]  ", host_id);
	else
		printf("[?]  ");
	if (get_cdev_value(cdevp, "proc_name", value, SYSFS_NAME_LEN))
		printf("  %-12s\n", value);
	else
		printf("  proc_name=????\n");

	if (out_mask & MASK_LONG) {
		if (get_cdev_value(cdevp, "cmd_per_lun", value, SYSFS_NAME_LEN))
			printf("  cmd_per_lun=%-4s ", value);
		else
			printf("  cmd_per_lun=???? ");

		if (get_cdev_value(cdevp, "host_busy", value, SYSFS_NAME_LEN))
			printf("host_busy=%-4s ", value);
		else
			printf("host_busy=???? ");

		if (get_cdev_value(cdevp, "sg_tablesize", value, SYSFS_NAME_LEN))
			printf("sg_tablesize=%-4s ", value);
		else
			printf("sg_tablesize=???? ");

		if (get_cdev_value(cdevp, "unchecked_isa_dma", value,
				   SYSFS_NAME_LEN))
			printf("unchecked_isa_dma=%-2s ", value);
		else
			printf("unchecked_isa_dma=?? ");
		printf("\n");
	}
	if (do_verbose) {
		struct sysfs_device * sdevp;

		sdevp = sysfs_get_classdev_device(cdevp);
		if (sdevp) {
			printf("  device dir: %s\n", sdevp->path);
		}
	}
}

#ifdef HAVE_DLIST_SORT_CUSTOM
static int scdev_dlist_comp(void * a, void * b)
{
	const char * lnam = ((struct sysfs_class_device *)a)->name;
	const char * rnam = ((struct sysfs_class_device *)b)->name;
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
#endif

static void list_hosts(int do_verbose, int out_mask)
{
	struct sysfs_class * sclassp;
	struct sysfs_class_device * scdevp;
	struct dlist * lcdevp;

	sclassp = sysfs_open_class(scsi_host_name);
	if (NULL == sclassp) {
		if (out_mask & MASK_CLASSIC)
			printf("Attached hosts: none\n");
		else if (do_verbose)
			printf("No SCSI hosts found\n");
		return;
        }
	lcdevp = sysfs_get_class_devices(sclassp);
	if (NULL == lcdevp) {
		if (do_verbose)
			printf("Unexpected sysfs_get_class_devices failure\n");
		return;
	}
#ifdef HAVE_DLIST_SORT_CUSTOM
	dlist_sort_custom(lcdevp, scdev_dlist_comp);
#endif

	dlist_for_each_data(lcdevp, scdevp, struct sysfs_class_device) {
		one_host_entry(scdevp, do_verbose, out_mask);
	}

	sysfs_close_class(sclassp);
}


int main(int argc, char **argv)
{
	int c;
	int do_sdevices = 1;
	int do_hosts = 0;
	int out_mask = 0;
	int do_verbose = 0;
	char sysfsroot[SYSFS_PATH_MAX];

	while (1) {
		int option_index = 0;

		c = getopt_long(argc, argv, "cdghHlvV", long_options, 
				&option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'c':
			out_mask |= MASK_CLASSIC;
			break;
		case 'd':
			out_mask |= MASK_DEVICE;
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
		case 'v':
			++do_verbose;
			break;
		case 'V':
			fprintf(stderr, "version: %s\n", version_str);
			return 0;
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
		fprintf(stderr, "unexpected non-option arguments: ");
		while (optind < argc)
			fprintf(stderr, "%s ", argv[optind++]);
		fprintf(stderr, "\n");
		return 1;
	}
	if (sysfs_get_mnt_path(sysfsroot, SYSFS_PATH_MAX)) {
		fprintf(stderr, "Unable to locate sysfsroot. If "
			"kernel >= 2.6.0\n    Try something like"
			" 'mount -t sysfs none /sys'\n");
		return 1;
	}
	if (do_hosts) {
		list_hosts(do_verbose, out_mask);
	} else if (do_sdevices) {
		list_sdevices(do_verbose, out_mask);
	}
	return 0;
}
