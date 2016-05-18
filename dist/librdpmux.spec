Name:           librdpmux
Version:        0.3.0
Release:        1%{?dist}
Summary:        Library to provide low-level VM guest interaction capabilties outside the hypervisor
License:        MIT
URL:            https://github.com/datto/librdpmux

Source0:        https://github.com/datto/librdpmux/archive/v%{version}/%{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.2
BuildRequires:  nanomsg0.6-devel
BuildRequires:  glib2-devel
BuildRequires:  pixman-devel

%description
This library provides a defined interface to interact with
virtual machine guests on a very low level. It provides access
to the current framebuffer of the VM, and to programmatically
send and receive keyboard and mouse events.

%package        devel
Summary:        Development files for %{name}
Requires:       %{name} = %{version}-%{release}

%description    devel
The %{name}-devel package contains configuration and header
files for developing applications that use %{name}.

%prep
%setup -q

%build
%cmake .
%make_build V=1

%install
%make_install

%post -p /sbin/ldconfig
%postun -p /sbin/ldconfig

%files
%license LICENSE
%defattr(-,root,root,-)
%{_libdir}/*.so.*

%files devel
%defattr(-,root,root,-)
%doc doc/html
%{_includedir}/*.h
%{_libdir}/*.so
%{_libdir}/pkgconfig/*.pc

%changelog
* Tue May 17 2016 Sri Ramanujam <sramanujam@datto.com> - 0.3.0-1
- Implement version 2 of the RDPMux registration protocol

* Mon Mar 14 2016 Sri Ramanujam <sramanujam@datto.com> - 0.2.0-1
- Initial packaging
