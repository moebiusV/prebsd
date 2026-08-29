Name:           prebsd
Version:        0.1
Release:        1%{?dist}
Summary:        Fetch and run Research Unix V4-V7 and 32V on SIMH
License:        ISC
URL:            https://github.com/moebiusV/prebsd
Source0:        %{name}-%{version}.tar.gz

BuildArch:      noarch
Requires:       python3
Requires:       curl
Suggests:       simh
Suggests:       filsys
Suggests:       v7unix-toolchain

%description
Fetches and runs Research Unix V4, V5, V6, V7 and 32V on SIMH (the pdp11
and vax780 simulators), driving the console over telnet.

%prep
%setup -q

%build
%configure

%install
%make_install

%files
%{_libdir}/prebsd/
%{_bindir}/prebsd-fetch
%{_mandir}/man1/fetch.1
%{_mandir}/man1/boot.1

%changelog
* Thu Aug 28 2026 maintainer <email> - 0.1-1
- Initial package
