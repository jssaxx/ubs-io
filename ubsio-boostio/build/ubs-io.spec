Name:           ubs-io-boostio
Version:        1.0.0
Release:        8
Summary:        ubs-io-boostio: High-performance cache acceleration library
License:        MulanPSL-2.0
Vendor:         Huawei Technologies Co., Ltd.
Source0:        ubs-io-%{version}.tar.gz
ExclusiveArch:  aarch64 x86_64

BuildRequires:  gcc gcc-c++ make cmake maven automake autoconf libtool dos2unix
BuildRequires:  libaio-devel openssl-devel
BuildRequires:  libboundscheck ubs-comm-devel
Requires:       libboundscheck ubs-comm-lib openssl-libs

%define debug_package %{nil}

%description
ubs-io-boostio is a high-performance cache acceleration library.

%package devel
Summary:        Development headers and static libraries for %{name}
Requires:       %{name}%{?_isa} = %{version}-%{release}

%description devel
Contains header files and development resources for ubs-io-boostio.

%prep
%setup -q -n ubs-io-%{version}

%build
pushd ubsio-boostio
dos2unix build.sh
bash -x build.sh -t release --cli
popd

%install
rm -rf %{buildroot}

dist_root=%{_builddir}/ubs-io-%{version}/ubsio-boostio/dist
src_core="${dist_root}/boostio"
src_test="${dist_root}/test_tools"

install -d %{buildroot}%{_bindir}
install -d %{buildroot}%{_includedir}/boostio
install -d %{buildroot}%{_libdir}
install -d %{buildroot}%{_sysconfdir}/boostio

cp -a "${src_core}"/lib/*.so* %{buildroot}%{_libdir}/
cp -a "${src_core}"/lib/libbio_sdk.a %{buildroot}%{_libdir}/
cp -a "${src_core}"/include/* %{buildroot}%{_includedir}/boostio/
cp -a "${src_core}"/bin/* %{buildroot}%{_bindir}/
cp -a "${src_core}"/conf/bio.conf %{buildroot}%{_sysconfdir}/boostio/

cp -a "${src_test}"/lib/* %{buildroot}%{_libdir}/
cp -a "${src_test}"/bin/* %{buildroot}%{_bindir}/
cp -a "${src_test}"/conf/bio_sdk_test.conf %{buildroot}%{_sysconfdir}/boostio/

find %{buildroot}%{_sysconfdir}/boostio -type d -exec chmod 750 {} \;
find %{buildroot}%{_sysconfdir}/boostio -type f -exec chmod 640 {} \;
find %{buildroot}%{_includedir}/boostio -type f -exec chmod 644 {} \;
find %{buildroot}%{_libdir} -type f -name "*.a" -exec chmod 644 {} \;
find %{buildroot}%{_libdir} -type f -name "*.so*" -exec chmod 755 {} \;
find %{buildroot}%{_bindir} -type f -exec chmod 755 {} \;

%post -p /sbin/ldconfig
%postun -p /sbin/ldconfig

%files
%{_libdir}/libbio_sdk.so.1
%{_libdir}/libbio_sdk.so.1.0.0
%{_libdir}/libzookeeper_mt.so.*
%{_libdir}/*.so
%exclude %{_libdir}/libbio_sdk.so
%{_bindir}/bio_daemon
%{_bindir}/bio_console
%{_bindir}/cli_client
%{_bindir}/cli_server
%attr(750,root,root) %dir %{_sysconfdir}/boostio
%config(noreplace) %attr(640,root,root) %{_sysconfdir}/boostio/bio.conf
%config(noreplace) %attr(640,root,root) %{_sysconfdir}/boostio/bio_sdk_test.conf

%files devel
%{_includedir}/boostio/
%{_libdir}/libbio_sdk.so
%{_libdir}/libbio_sdk.a

%changelog
* Wed May 20 2026 xxx <xxx@xxx.com> - 1.0.0-7
- Make the RPM spec compliant with packaging conventions

* Tue May 19 2026 xxx <xxx@xxx.com> - 1.0.0-6
- perf interceptor read and write

* Tue May 12 2026 xxx <xxx@xxx.com> - 1.0.0-5
- poc for interceptor to dts

* Tue Apr 28 2026 xxx <xxx@xxx.com> - 1.0.0-4
- poc for interceptor

* Sat Feb 28 2026 xxx <xxx@xxx.com> - 1.0.0-3
- Declare the public IP addresses used in the code

* Sat Feb 28 2026 xxx <xxx@xxx.com> - 1.0.0-2
- fix file permission

* Sat Feb 07 2026 xxx <xxx@xxx.com> - 1.0.0-1
- Initial RPM release
