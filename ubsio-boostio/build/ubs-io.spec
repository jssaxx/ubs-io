%define custom_os %( \
    if [ -f "/etc/openEuler-release" ]; then \
        OS_RELEASE_CONTENT=$(cat /etc/openEuler-release); \
        if [[ $OS_RELEASE_CONTENT =~ ([0-9]+)\\.([0-9]+).*-?SP-?([0-9]+) ]]; then \
            printf "oe%%s%%ssp%%s" "${BASH_REMATCH[1]}" "${BASH_REMATCH[2]}" "${BASH_REMATCH[3]}"; \
        elif [[ $OS_RELEASE_CONTENT =~ ([0-9]+)\\.([0-9]+) ]]; then \
            printf "oe%%s%%s" "${BASH_REMATCH[1]}" "${BASH_REMATCH[2]}"; \
        fi; \
    else \
        echo "unknown_os"; \
    fi \
)

%global with_cli %{?_with_cli}%{!?_with_cli:1}
%{!?version_count: %define version_count 1}
%{!?build_type:    %define build_type release}

%define _os             %{custom_os}
%define _full_release   %{version_count}.%{_os}
%define _build_name_fmt %%{NAME}-%%{VERSION}-%%{RELEASE}.%%{ARCH}.rpm
%define __os_install_post %{nil}
%define debug_package     %{nil}

Name:           ubs_io-boostio
Version:        1.0.0
Release:        %{_full_release}
Summary:        BoostIO: High-performance cache acceleration library
License:        Mulan PSL v2
Vendor:         Huawei Technologies Co., Ltd
Source0:        ubs-io.tar.gz
BuildArch:      aarch64 x86_64

BuildRequires:  gcc gcc-c++ make cmake maven hostname fuse-devel automake
BuildRequires:  librados-devel libaio-devel libtool numactl-devel libcurl-devel
BuildRequires:  openssl-devel
BuildRequires:  libboundscheck ubs-comm-devel
Requires:       libboundscheck ubs-comm-lib openssl-libs librados2

%description
BoostIO is a high-performance cache acceleration library.

%if %{with_cli}
%package test-tools
Summary: BoostIO test tools and benchmarks
Requires: %{name} = %{version}-%{release}

%description test-tools
This package contains test tools, binaries, and configurations for BoostIO.
%endif

%package devel
Summary: Development headers and static libraries for BoostIO
Requires: %{name} = %{version}-%{release}

%description devel
Contains header files and development resources for BoostIO.

%prep
%setup -q -n ubs-io

%build
cd ubsio-boostio
%if %{with_cli}
    bash -x build.sh -t %{build_type} --cli
%else
    bash -x build.sh -t %{build_type}
%endif

%install
rm -rf %{buildroot}

DIST_ROOT=$RPM_BUILD_DIR/ubs-io/ubsio-boostio/dist

mkdir -p %{buildroot}%{_libdir}
mkdir -p %{buildroot}%{_includedir}/boostio
mkdir -p %{buildroot}%{_bindir}
mkdir -p %{buildroot}%{_sysconfdir}/boostio

SRC_CORE=$DIST_ROOT/boostio
cp -ad $SRC_CORE/lib/*.so* %{buildroot}%{_libdir}/
cp -ad $SRC_CORE/lib/libbio_sdk.a %{buildroot}%{_libdir}/
cp -ad $SRC_CORE/include/* %{buildroot}%{_includedir}/boostio/
cp -ad $SRC_CORE/bin/* %{buildroot}%{_bindir}/
cp -ad $SRC_CORE/conf/bio.conf %{buildroot}%{_sysconfdir}/boostio/

%if %{with_cli}
SRC_TEST=$DIST_ROOT/test_tools
if [ -d "$SRC_TEST" ]; then
    mkdir -p %{buildroot}%{_libdir}/boostio/test_tools
    mkdir -p %{buildroot}%{_bindir}/boostio/test_tools

    cp -ad $SRC_TEST/lib/* %{buildroot}%{_libdir}/boostio/test_tools/
    cp -ad $SRC_TEST/bin/* %{buildroot}%{_bindir}/boostio/test_tools/
    cp -ad $SRC_TEST/conf/bio_sdk_test.conf %{buildroot}%{_sysconfdir}/boostio/
fi
%endif

find %{buildroot}%{_libdir} -type f -name "*.so*" -exec chmod 755 {} \;
find %{buildroot}%{_libdir} -type f -name "*.a" -exec chmod 644 {} \;
find %{buildroot}%{_bindir} -type f -exec chmod 755 {} \;
find %{buildroot}%{_includedir} -type f -exec chmod 644 {} \;
find %{buildroot}%{_sysconfdir} -type f -exec chmod 644 {} \;

%post -p /sbin/ldconfig
%postun -p /sbin/ldconfig

%files
%defattr(-,root,root,-)
%{_libdir}/libbio_sdk.so.1
%{_libdir}/libbio_sdk.so.1.0.0
%{_libdir}/*.so
%exclude %{_libdir}/libbio_sdk.so
%{_bindir}/bio_daemon
%dir %{_sysconfdir}/boostio
%config(noreplace) %{_sysconfdir}/boostio/bio.conf

%if %{with_cli}
%files test-tools
%defattr(-,root,root,-)
%dir %{_libdir}/boostio
%dir %{_bindir}/boostio
%dir %{_libdir}/boostio/test_tools
%dir %{_bindir}/boostio/test_tools

%{_libdir}/boostio/test_tools/*
%{_bindir}/boostio/test_tools/bio_console
%config(noreplace) %{_sysconfdir}/boostio/bio_sdk_test.conf
%endif

%files devel
%defattr(-,root,root,-)
%{_includedir}/boostio/
%{_libdir}/libbio_sdk.so
%{_libdir}/libbio_sdk.a

%clean
rm -rf %{buildroot}

%changelog
* Sat Feb 07 2026 ljj929 <499109299@qq.com> - 1.0.0-1
- Initial RPM release