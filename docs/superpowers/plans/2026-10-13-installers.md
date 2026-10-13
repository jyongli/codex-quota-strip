# Cross-platform installer delivery

Scope: publish v3.0.0-preview.5 with Windows per-user EXE setup, macOS universal DMG/PKG, Linux DEB/RPM, and portable ZIP/TGZ. TGZ includes a per-user install/uninstall entry point. Application features stay unchanged.

Acceptance:

- Windows: non-admin installation, Chinese/English wizard, shortcuts, isolated install/upgrade/uninstall checks preserving settings. Windows 10 1809+/11 x64; ARM64 is x64 emulation, not a native tested build.
- macOS: macOS 14+, arm64/x86_64; DMG with Applications link and PKG installing the same signed app to /Applications. Verify packaged signatures, architectures, minimum target, real PKG installation and contained licenses.
- Linux: Ubuntu 22.04 build baseline. Generate dependencies from binaries for DEB/RPM. Test package installation/removal and installed app UI smoke on Ubuntu 22.04/24.04, Debian 12, Fedora 43. TGZ user installer checks architecture/libraries, installs without root, preserves configuration on update/uninstall.
- Publish installers, portable packages, build provenance, checksum/third-party notices. Release body and separate Chinese/English docs map environment to filename, installation/upgrade/removal commands, and limitations.

Ownership: Windows installer worker owns packaging/windows/* and scripts/Build-WindowsInstaller.ps1, scripts/Test-WindowsInstaller.ps1. Main agent owns CMake, Linux/macOS packaging/tests, CI, documentation, release and verification. No global startup/proxy/security changes.

Verification: run isolated installer/package lifecycle tests; reuse unchanged application regression evidence, rerun hosted native/default tests on release commit. Inspect asset hashes, public metadata and Git tag. Fix failed checks before publishing.
