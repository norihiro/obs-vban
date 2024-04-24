Name: @PLUGIN_NAME_FEDORA@
Version: @VERSION@
Release: @RELEASE@%{?dist}
Summary: VBAN plugin for OBS Studio
License: GPLv3+

Source0: %{name}-%{version}.tar.bz2
BuildRequires: cmake, gcc, gcc-c++
BuildRequires: obs-studio-devel

%description
VBAN plugin will add a VBAN source, VBAN output, and VBAN output filter to OBS Studio.

%prep
%autosetup -p1

%build
%{cmake} -DLINUX_PORTABLE=OFF -DLINUX_RPATH=OFF
%{cmake_build}

%install
%{cmake_install}

%files
%{_libdir}/obs-plugins/@PLUGIN_NAME@.so
%{_datadir}/obs/obs-plugins/@PLUGIN_NAME@/
