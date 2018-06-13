%define name    lsscsi
%define version 0.30
%define release 1

Summary: 	List SCSI devices (or hosts) plus NVMe namespaces and ctls
Name: 		%{name}
Version: 	%{version}
Release: 	%{release}
License:	GPL
Group:		Utilities/System
Source0:	http://sg.danny.cz/scsi/%{name}-%{version}.tgz
Url:		http://sg.danny.cz/scsi/lsscsi.html
BuildRoot:	%{_tmppath}/%{name}-%{version}-root/
Packager:	dgilbert at interlog dot com

%description
Uses information provided by the sysfs pseudo file system in the Linux
kernel 2.6 series, and later, to list SCSI devices (Logical
Units (e.g. disks)) plus NVMe namespaces (SSDs). It can list transport
identifiers (e.g. SAS address of a SAS disk), protection information
configuration and size for storage devices. Alternatively it can be used
to list SCSI hosts (e.g. HBAs) or NVMe controllers. By default one line
of information is output per device (or host).

Author:
--------
    Doug Gilbert <dgilbert at interlog dot com>

%prep

%setup -q

%build
%configure

%install
if [ "$RPM_BUILD_ROOT" != "/" ]; then
        rm -rf $RPM_BUILD_ROOT
fi

make install \
        DESTDIR=$RPM_BUILD_ROOT

%clean
if [ "$RPM_BUILD_ROOT" != "/" ]; then
        rm -rf $RPM_BUILD_ROOT
fi

%files
%defattr(-,root,root)
%doc ChangeLog INSTALL README CREDITS AUTHORS COPYING
%attr(0755,root,root) %{_bindir}/*
%{_mandir}/man8/*


%changelog
* Tue Jun 12 2018 - dgilbert at interlog dot com
- add NVMe support, minor tweaks
  * lsscsi-0.30
* Fri May 13 2016 - dgilbert at interlog dot com
- minor tweaks
  * lsscsi-0.29
* Tue Sep 23 2014 - dgilbert at interlog dot com
- add --unit to find LU names
  * lsscsi-0.28
* Sat Mar 16 2013 - dgilbert at interlog dot com
- rework buffers for large systems, add --lunhex and --scsi_id
  * lsscsi-0.27
* Tue Jan 31 2012 - dgilbert at interlog dot com
- add fcoe transport indicator; add --wwn option
  * lsscsi-0.26
* Mon May 09 2011 - dgilbert at interlog dot com
- add sas_port and fc_remore_ports infoR; '--size' option
  * lsscsi-0.25
* Thu Dec 23 2010 - dgilbert at interlog dot com
- FC transport syntax change
  * lsscsi-0.24
* Thu Dec 03 2009 - dgilbert at interlog dot com
- remove /proc/mounts scan for sysfs mount point, assume /sys
  * lsscsi-0.23
* Fri Dec 26 2008 - dgilbert at interlog dot com
- protection (T10-DIF) information, USB, ATA + SATA transports
  * lsscsi-0.22
* Tue Jul 29 2008 - dgilbert at interlog dot com
- more changes  for lk 2.6.26 (SCSI sysfs)
  * lsscsi-0.21
* Wed Jul 9 2008 - dgilbert at interlog dot com
- changes for lk 2.6.25/26 SCSI midlayer rework
  * lsscsi-0.20
* Thu Jan 25 2007 - dgilbert at interlog dot com
- add transport information (target+initiator)
  * lsscsi-0.19
* Fri Mar 24 2006 - dgilbert at interlog dot com
- cope with dropping of 'generic' symlink post lk 2.6.16
  * lsscsi-0.18
* Mon Feb 06 2006 - dgilbert at interlog dot com
- fix disappearance of block device names in lk 2.6.16-rc1
  * lsscsi-0.17
* Fri Dec 30 2005 - dgilbert at interlog dot com
- wlun naming, osst and changer devices
  * lsscsi-0.16
* Tue Jul 19 2005 - dgilbert at interlog dot com
- does not use libsysfs, add filter argument, /dev scanning
  * lsscsi-0.15
* Fri Aug 20 2004 - dgilbert at interlog dot com
- add 'timeout'
  * lsscsi-0.13
* Sun May 9 2004 - dgilbert at interlog dot com
- rework for lk 2.6.6, device state, host name, '-d' for major+minor
  * lsscsi-0.12
* Fri Jan 09 2004 - dgilbert at interlog dot com
- rework for lk 2.6.1
  * lsscsi-0.11
* Tue May 06 2003 - dgilbert at interlog dot com
- adjust HBA listing for lk > 2.5.69
  * lsscsi-0.10
* Fri Apr 04 2003 - dgilbert at interlog dot com
- fix up sorting, GPL + copyright notice
  * lsscsi-0.09
* Sun Mar 2 2003 - dgilbert at interlog dot com
- start to add host listing support (lk >= 2.5.63)
  * lsscsi-0.08
* Fri Feb 14 2003 - dgilbert at interlog dot com
- queue_depth name change in sysfs (lk 2.5.60)
  * lsscsi-0.07
* Mon Jan 20 2003 - dgilbert at interlog dot com
- osst device file names fix
  * lsscsi-0.06
* Sat Jan 18 2003 - dgilbert at interlog dot com
- output st and osst device file names (rather than "-")
  * lsscsi-0.05
* Thu Jan 14 2003 - dgilbert at interlog dot com
- fix multiple listings of st devices (needed for lk 2.5.57)
  * lsscsi-0.04
* Thu Jan 09 2003 - dgilbert at interlog dot com
- add --generic option (list sg devices), scsi_level output
  * lsscsi-0.03
* Wed Dec 18 2002 - dgilbert at interlog dot com
- add more options including classic mode
  * lsscsi-0.02
* Fri Dec 13 2002 - dgilbert at interlog dot com
- original
  * lsscsi-0.01
