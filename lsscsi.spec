#
# spec file for lsscsi
# 
# please send bugfixes or comments to dgilbert@interlog.com
#

Summary: List all SCSI devices (or hosts) and associated information
Name: lsscsi
Version: 0.03
Release: 1
Packager: dgilbert@interlog.com
License: GPL
Group: Utilities/System
Source: ftp://www.torque.net/scsi/lsscsi-0.03.tgz
Url: http://www.torque.net/scsi/lsscsi.html
Provides: lsscsi

%description
Uses information provided by sysfs in Linux kernel from around 2.5.50
to list all SCSI devices (or hosts).

Author:
--------
    Doug Gilbert <dgilbert@interlog.com>

%prep
%setup

%build
make

%install
make install INSTDIR=%{_bindir} MANDIR=%{_mandir}

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root)
%attr(-,root,root) %doc README CHANGELOG INSTALL
%attr(755,root,root) %{_bindir}/lsscsi
%attr(-,root,root) %doc %{_mandir}/man8/lsscsi.8.gz
 

%changelog
* Thu Jan 09 2003 - dgilbert@interlog.com
- add --generic option (list sg devices), scsi_level output
  * lsscsi-0.03
* Wed Dec 18 2002 - dgilbert@interlog.com
- add more options including classic mode
  * lsscsi-0.02
* Fri Dec 13 2002 - dgilbert@interlog.com
- original
  * lsscsi-0.01
