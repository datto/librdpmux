Name:           librdpmux
Version:        0.2.0
Release:        1%{?dist}
Summary:        Library to provide low-level VM guest interaction capabilties outside the hypervisor.
License:        MIT
URL:            http://github-server.datto.lan/sramanujam/librdpmux

Source0:        librdpmux.tar.gz

BuildRequires:  cmake >= 3.4
BuildRequires:  nanomsg0.6-devel
BuildRequires:  glib2-devel
BuildRequires:  pixman-devel

%description
This library provides a defined interface to interact with virtual machine guests on a very low level. It provides access
to the current framebuffer of the VM, and to programmatically send and receive keyboard and mouse events.

%package        devel
Summary:        Development files for %{name}
Requires:       %{name} = %{version}-%{release}
Requires:       pkgconfig

%description    devel
The %{name}-devel package contains configuration and header files for developing applications that use %{name}.

%prep
%setup -qn librdpmux

%build
%cmake .

make %{?_smp_mflags} V=1

%install
%make_install

%post -p /sbin/ldconfig
%postun -p /sbin/ldconfig

%files
%{_libdir}/*.so.*

%files devel
%defattr(-,root,root)
%{_includedir}/*.h
%{_libdir}/*.so
%{_libdir}/pkgconfig/*.pc
