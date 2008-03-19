#
# spec file for lsscsi
# 
# please send bugfixes or comments to dgilbert at interlog dot com
#

Summary: List all SCSI devices (or hosts) and associated information
Name: lsscsi
Version: 0.13
Release: 1
Packager: dgilbert at interlog dot com
License: GPL
Group: Utilities/System
Source: ftp://www.torque.net/scsi/lsscsi-0.13.tgz
Url: http://www.torque.net/scsi/lsscsi.html
Provides: lsscsi
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root/

%description
Uses information provided by the sysfs pseudo file system in Linux kernel
2.6 series to list all SCSI devices or all SCSI hosts. Includes a "classic"
option to mimic the output of "cat /proc/scsi/scsi" that has been widely
used prior to the lk 2.6 series.

Author:
--------
    Doug Gilbert <dgilbert at interlog dot com>

%prep
%setup

%build
make

%install
if [ "$RPM_BUILD_ROOT" != "/" ]; then
        rm -rf $RPM_BUILD_ROOT
fi
make install INSTDIR=$RPM_BUILD_ROOT/usr/bin MANDIR=$RPM_BUILD_ROOT/usr/share/man

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root)
%attr(-,root,root) %doc README CHANGELOG INSTALL
%attr(755,root,root) %{_bindir}/lsscsi
# Mandrake compresses man pages with bzip2, RedHat with gzip
%attr(-,root,root) %doc %{_mandir}/man8/lsscsi.8*
 

%changelog
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
