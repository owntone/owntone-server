Summary: A multi-threaded implementation of Apple's DAAP server
Name: mt-daapd
Version: 0.1.0
Release: 1
License: GPL
Group: Development/Networking
URL: http://www.pedde.com/downloads/%{name}-%{version}.tar.gz
Source0: %{name}-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-buildroot
Requires: howl libid3tag
BuildRequires: libid3tag-devel

%description
A multi-threaded implementation of Apple's DAAP server, mt-daapd
allows a Linux machine to advertise MP3 files to to used by 
Windows or Mac iTunes clients.
%prep
%setup -q

%build
./configure --enable-howl --prefix=$RPM_BUILD_ROOT/usr
make RPM_OPT_FLAGS="$RPM_OPT_FLAGS"

%install
rm -rf $RPM_BUILD_ROOT
make install
mkdir -p $RPM_BUILD_ROOT/etc/init.d
cp contrib/mt-daapd $RPM_BUILD_ROOT/etc/init.d
cp contrib/mt-daapd.conf $RPM_BUILD_ROOT/etc
cp contrib/mt-daapd.playlist $RPM_BUILD_ROOT/etc

%post
/sbin/chkconfig --add mt-daapd

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root,-)
/etc/*
/usr/sbin/*
/usr/share/mt-daapd/*

%doc


%changelog
* Fri Nov 14 2003 root <root@hafnium.corbey.com> 
- Initial build.


