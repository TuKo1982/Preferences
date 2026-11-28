# UniPrefs

*Called Preferences before version 0.21.*

One MUI program for the preferences of AmigaOS, ApolloOS and the programs installed with them. Each preference
is a **module**: a page of its own that reads and writes the same files as the original program, or an entry that
opens a program that stays external. The modules are installed from the Modules page; the list groups them by
heading.

## Install

Download `UniPrefs-0.33.lha` from the [latest release](../../releases/latest) and copy `UniPrefs`, `UniPrefs.info`
and the drawer `Modules` into `SYS:Prefs/` (`Icons/UniPrefs.info` is a second icon). The archive holds the program with its two own pages, About and
Modules: at the first start, when no module of the installed system is there, UniPrefs offers to download
them. Every other module is installed from the Modules page, from the modules repository entered there.

Requires AmigaOS (or ApolloOS) and MUI 3.8. The **Modules** page needs a TCP/IP stack and AmiSSL 5.

## Upgrading from Preferences

The Modules page of Preferences 0.20 and older updates to UniPrefs like any other update (*Check*, then *Update
all*). At its first start UniPrefs renames the drawer `Preferences-Modules` to `Modules`, copies the
settings of `ENVARC:Preferences/` to `ENVARC:UniPrefs/`, and when its window opens renames its own file
`Preferences` (and its icon) to `UniPrefs`. Scripts, menus and icons that start `SYS:Prefs/Preferences` must then
start `SYS:Prefs/UniPrefs`. The ARexx port is now `UNIPREFS.1`.

## Use

- `UniPrefs` opens the window with the list of modules; `UniPrefs <module>` opens it on that module.
- `UniPrefs <module> ALONE` opens that module alone, without the list (tool type `ALONE` from Workbench).
- `UniPrefs <module> GET=key`, `SET key=value USE|SAVE`, `LIST`, `FROM=file`: the command line of the
  modules that have keys.

- **Replace Original Editor...** (Project menu) moves the original editor of the page shown to
  `SYS:Prefs/Modules/Originals/` (no module needs it: they carry their own data) and writes in its place a
  script that starts `UniPrefs <module> ALONE` with the editor's arguments; the editor's icon then starts the script
  with IconX. Refused for a page of another system and for the modules that leave part of the editor's work to
  it. Preset icons whose default tool is that editor no longer open it.

## The General pages

- **About**: the program's version, its modules, the update repository, and the machine: processor (with the
  Apollo board and core on a 68080), FPU, chipset, chip and fast memory, exec and Workbench versions.
- **Modules** reads `manifest-all.txt` and `info-all.txt` from the latest releases of this repository and of the
  modules repository entered on the page. *Check online* lists every
  module of the release with its installed and available versions (rows with an update waiting in bold) and, in
  a side panel, its author, the original software it is based on, the OS it belongs to and a description.
  - *Show* (All / Installed / Not installed / Newer available) and *OS* (each operating system, No OS) filter
    the list; *Select all* chooses every row shown.
  - *Install selected* (or a double click) installs or updates the chosen modules, *Update all* every newer one
    shown, *Remove selected* deletes the chosen installed modules (never About and Modules).
  - Every file is checked against the size and SHA-256 given in the manifest before it replaces the installed
    one, the old file is kept as `.old`, and the connection is HTTPS with certificate verification.

## Modules and SDK

This repository holds the SDK that builds UniPrefs modules, with the sources of UniPrefs' own pages:

- `examples/UniPrefs/About/` and `examples/UniPrefs/Updates/`: the About and Modules pages, each with its own
  `Makefile` (`make` builds the module, `make check` checks it on the build machine).
- `include/`, `lib/`, `module.mk`, `templates/`, `tools/`: the SDK; how to write a module is in [SDK.md](SDK.md).

Cross compiler: GCC 6.5 for m68k-amigaos (ApolloCrossDev) with libnix.

## Author

Renaud Schweingruber
