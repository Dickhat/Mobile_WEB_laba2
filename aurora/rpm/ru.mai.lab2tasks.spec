Name:       ru.mai.lab2tasks
Summary:    Offline REST tasks client for Aurora OS, lab 2
Version:    1.0.0
Release:    1
Group:      Applications
License:    MIT
Source0:    %{name}-%{version}.tar.gz

BuildRequires: cmake
BuildRequires: pkgconfig(auroraapp)
BuildRequires: pkgconfig(Qt5Core)
BuildRequires: pkgconfig(Qt5Network)
BuildRequires: pkgconfig(Qt5Qml)
BuildRequires: pkgconfig(Qt5Quick)
BuildRequires: pkgconfig(Qt5Sql)

# Qt5 SQL and SQLite plugin must be available on the target device.
# Runtime Qt libraries are resolved automatically from linked ELF dependencies.

%description
Native HTTPS REST task client with SQLite offline queue for Aurora OS.

%prep
%autosetup

%build
%cmake
%make_build

%install
%make_install

%files
%defattr(-,root,root,-)
%{_bindir}/%{name}
%defattr(644,root,root,-)
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png
