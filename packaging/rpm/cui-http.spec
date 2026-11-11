Name:           cui-http
Version:        0.1.0
Release:        1%{?dist}
Summary:        Native API client (Postman/Insomnia alternative)

License:        GPL-3.0-or-later
URL:            https://github.com/pdenapo/cui-http
Source0:        https://github.com/pdenapo/cui-http/releases/download/v%{version}/cui-http-%{version}.tar.gz

BuildRequires:  cmake, ninja-build, gcc, sqlite-devel, libcurl-devel, glfw-devel, mesa-libGL-devel, libX11-devel
Requires:       sqlite, curl, glfw, mesa-libGL

%description
Blazing-fast native API client built with CUI and a C backend.
Includes the cui_http GUI and the cui-http-cli command line tool.

%prep
%autosetup -n %{name}-%{version}

%build
%cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCUI_PLATFORM=glfw
%cmake_build

%install
%cmake_install

%files
%license LICENSE
%doc README.md
%{_bindir}/cui-http
%{_bindir}/cui-http-cli