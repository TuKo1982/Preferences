# UniPrefs module SDK

UniPrefs is one preferences program for AmigaOS (MUI). It knows no
preferences by itself: everything it lists comes from the module files in
`SYS:Prefs/Modules` (more exactly `PROGDIR:Modules`), one
`<Name>.module` per module. Copy a file there and the module appears; delete
it and it is gone.

This SDK is everything needed to write such a module.

    README.md          this guide
    module.mk          make rules: include it from your Makefile
    include/           the headers (kit.h is the only one you include)
    lib/               modstart.S + glue.c, linked into every binary module
    tools/             make check: UniPrefs' key engine for the build machine
    templates/         Template.module (text), Template.c + Makefile (binary)
    examples/Editor/   a complete small module (environment variable, own page)
    examples/UniPrefs/ UniPrefs' own pages About and Updates (Modules), one drawer each
                       with its own Makefile

## Two kinds of module

**Text module.** A few lines that list an existing editor in UniPrefs. The page
shows a button that starts the editor. Nothing to compile.

**Binary module.** A small AmigaDOS load file built from one C file. It
describes the preferences as *keys*. From the keys alone UniPrefs gives:

- a CLI: `UniPrefs <Name> LIST`, `GET=key`, `SET key=value USE|SAVE`, `FROM=file`;
- ARexx: `MODULE <Name>` on port `UNIPREFS.1`;
- Save & Quit, Save, Use (uses, then quits), Reset and Quit, which write like the original editors;
- a plain page with one gadget per key.

The module may also build a page of its own with MUI and the UniPrefs kit,
for example to look like the original editor.

## Categories

Every module names the list headings it appears under: one to four
categories, comma-separated. A module with several categories appears under
each of them. It is one module, one page and one set of values.

    Categories=Menus & Desktop, Shell                (text module)
    .categories = AP_CATEGORY_INPUT ", " AP_CATEGORY_SYSTEM,    /* binary */

The usual categories come first in the list, in this order. The constants are
in module.h.

| Category            | Constant                 | For                                  |
|---------------------|--------------------------|--------------------------------------|
| `General`           | `AP_CATEGORY_GENERAL`       | the program's own pages (About, Modules): always listed first, before any other category (`AP_CATEGORY_PREFERENCES` is the same; before 25.11.2026 the category was `UniPrefs`, listed last) |
| (one per OS)        | `AP_CATEGORY_OS*`, `AP_CATEGORY_APOLLOOS` | the editors of an operating system itself, and only them: such a module is in that category alone (the constants are in module.h) |
| `System`            | `AP_CATEGORY_SYSTEM`     | boot, devices, serial, memory        |
| `Display`           | `AP_CATEGORY_DISPLAY`    | screen modes, palette, overscan, RTG |
| `Look`              | `AP_CATEGORY_LOOK`       | fonts, patterns, themes, pointer     |
| `Menus & Desktop`   | `AP_CATEGORY_INPUT`      | menus, windows, desktop tools        |
| `Requesters`        | `AP_CATEGORY_REQUESTERS` | file and other requesters            |
| `Locale and time`   | `AP_CATEGORY_LOCALE`     | language, country, clock             |
| `Sound`             | `AP_CATEGORY_SOUND`      | beeps, audio, AHI                    |
| `Printing`          | `AP_CATEGORY_PRINTING`   | printers                             |
| `Network`           | `AP_CATEGORY_NETWORK`    | TCP/IP, file sharing                 |
| `Datatypes`         | `AP_CATEGORY_DATATYPES`  | datatype settings                    |

Several OSes can have an editor of the same name: a module of an OS editor is
named `<editor>_<OS suffix>` (the suffixes are listed in module.h; `_`, not
`-`, which Updates 0.4 refuses). `UniPrefs <editor>` still finds
`<editor>_<suffix>` while it is the only one of that editor installed.

Any other name makes a heading of its own, listed after these in
alphabetical order (`General` is always first). Prefer a usual category, and add your own only when it
groups several modules. Upper and lower case do not matter.

Rules, checked when the module loads:

- at least one category and at most `AP_MAXCATS` (4);
- each category is 1 to 31 characters long and contains no comma;
- no category may appear twice.

## Text module

`templates/Template.module`:

    Name=MyTool                ; CLI name = file name without .module, no spaces
    Title=My Tool              ; list entry (default: Name)
    Editor=SYS:Prefs/MyTool    ; started with PUBSCREEN=<screen>
    Categories=Look
    Version=0.1
    Author=Your Name           ; optional: shown by the Modules page
    BasedOn=MyTool 1.2         ; optional: the original software ("-" for none)
    Description=Opens MyTool, which sets ...   ; optional: one or two sentences
    OS=<OS category>           ; optional: only for an editor of the OS itself

`Name`, `Editor`, `Categories` and `Version` are required. The entry stays hidden while
the editor is not installed. `Author`, `BasedOn`, `Description` and `OS` are only
read by the Modules page (see "Module info" below); UniPrefs ignores them.

## Binary module

### Building

You need the m68k-amigaos GCC 6.5 cross compiler with libnix (bebbo's
amiga-gcc or ApolloCrossDev); `m68k-amigaos-gcc` must be in the PATH or
named by `AP_CC`. Then write a Makefile:

    UNIPREFS_SDK = /path/to/UniPrefs-SDK
    MODULES      = MyTool                 # MyTool.c -> MyTool.module
    include $(UNIPREFS_SDK)/module.mk

| Command      | Result                                                     |
|--------------|------------------------------------------------------------|
| `make`       | `MyTool.module`                                            |
| `make check` | the checks UniPrefs makes at load time, run on your PC     |
| `make clean` | removes the build output                                   |

`module.mk` also reads `AP_CPU` (default `-m68020`), `AP_SRC`, `AP_OUT`,
`AP_OBJ` and `HOSTCC`, and, set before the include line:

| Setting            | Use                                                        |
|--------------------|------------------------------------------------------------|
| `AP_LIBS_<Name>`   | extra link libraries of one module (`AP_LIBS_AHI = -lm`)   |
| `AP_INCS`          | extra include directories, for the module and `make check` |
| `AP_EXTRA_<Name>`  | extra objects linked into one module, after its own        |

An extra object from a `.c` next to the module's is built like the module
(`AP_EXTRA_MyTool = $(AP_OBJ)/net.o` for `net.c`); one needing other flags
gets its own rule after the include line. `templates/Makefile` is a starting
point. Several modules can share one Makefile (`MODULES = One Two`), but
UniPrefs gives each module its own drawer and Makefile, so that each one
builds alone: in `examples/UniPrefs/`, `make -C About` (or `make` inside
`About/`) builds `About/About.module`, and `make` there builds them all. Their
Makefiles find this SDK three drawers up (`UNIPREFS_SDK ?= ../../..`) and
the compiler in `$(HOME)/ApolloCrossDev/Compilers/GCC-6.50-Dev`, else in the
PATH; both can be given on the command line. Updates needs the AmiSSL 5 SDK
(`AMISSL_SDK=...`). A module kept in a tree of its own finds the SDK the same
way (`UNIPREFS_SDK ?= <path to this SDK>`); headers shared by several modules
go in a drawer beside them (`AP_INCS = ../common`).

### The source

One C file. `kit.h` is its first and only include. It ends with the
descriptor and `AP_MODULE`:

    #include "kit.h"

    static const PChunkSel chunks[] = { { PF_ID('T','M','P','L'), 16, 0, 0, 0 } };
    static const Key keys[] = {
        { .name = "Delay", .label = "Delay (ms)", .type = KT_INT, .off = 2, .size = 2,
          .min = 0, .max = 5000 },
    };

    #ifndef AP_NO_GUI
    static Object *page(Page *p) { ... }
    #endif

    static const Module mod = {
        .name = "MyTool", .title = "My Tool", .kind = MK_IFF, .file = "mytool.prefs",
        .chunks = chunks, .nchunks = SIZEOF(chunks), .keys = keys, .nkeys = SIZEOF(keys),
        .categories = AP_CATEGORY_LOOK, .version = "0.1",
        .page = PAGE(page), .wintitle = "My Tool Preferences",
    };
    AP_INFO("Your Name", "MyTool 1.2 (its author)", "The delay and the name MyTool uses (ENV:Sys/mytool.prefs).")
    AP_MODULE(mod, MyTool, "0.1 (1.1.2027)")    /* also the $VER string */

The first argument of `AP_MODULE` is the descriptor, the second the module
name (a C identifier), the third the version shown by `version`. Every
module has its own version, `.version = "major.minor"`; the `$VER` string
starts with the same number (else the module refuses to start), and LIST
and CHECK show it. A text module gives it as `Version=0.1` (required).

### Module info (AP_INFO)

The Modules page has a panel that shows, for the module selected in its list,
the author, the original software the module is based on and a short
description. It reads them from the module file without loading it, the way
`version` reads `$VER`:

    AP_INFO("author", "based on", "description")

next to `AP_MODULE` puts one string in the file:
`"$APINFO:" author TAB based-on TAB description`, NUL-terminated. Rules:

- plain printable ASCII, no tab inside a field;
- author 1-63 characters, based on 1-127 (`"-"` when the module is based on
  no other program), description 1-255 (keep it to one or two sentences,
  about 200 characters);
- one `AP_INFO` per module.

A module that reproduces an editor of the operating system itself also says
which OS and version, as a fourth field (1-47 characters):

    AP_INFO_OS("author", "based on", "description", AP_CATEGORY_APOLLOOS)

The panel shows it on its OS line (`-` for the others).

It is data only: UniPrefs and the module ABI do not use it, and a module
without it still loads (the panel then shows `-`). `make check` prints the
info and refuses a malformed one. A text module gives the same values
as `Author=`, `BasedOn=`, `Description=` and `OS=` lines. `tools/make_release.py`
copies every file's info into the release's `info.txt`, so the Modules page
can describe a module that is not installed yet.

`templates/Template.c` is a full starting point. `examples/Editor/Editor.c`
is a working module of 50 lines.

### Module fields (module.h)

| Field          | Meaning                                                                   |
|----------------|---------------------------------------------------------------------------|
| `name`         | CLI name, the same as the file name; letters and digits, at most 30 characters |
| `title`        | list entry                                                                |
| `kind`         | `MK_IFF`: an IFF FORM PREF file; `MK_ENV`: one global variable per key; `MK_LAUNCH`: none |
| `file`         | `MK_IFF`: file name in `ENV:Sys/` and `ENVARC:Sys/`, no path              |
| `editor`       | optional: the original editor; when installed, the page gets a button to open it |
| `chunks`, `nchunks` | `MK_IFF`: the chunks the keys live in                                |
| `keys`, `nkeys`| the keys                                                                  |
| `init`         | `MK_IFF`, optional: sets the defaults of a new file, `void init(PFile *)` |
| `categories`   | required, see above                                                       |
| `page`         | optional, `PAGE(fn)`: builds the page; without it UniPrefs builds a plain page |
| `wintitle`     | window title while the page shows                                         |
| `after`        | optional, `AFTER(fn)`: called once the page shows its values              |
| `before_write` | optional, `BEFORE(fn)`: called before Save and Use; return 0 to cancel    |
| `exit`         | optional, `EXIT(fn)`: called after the window is gone; free what the page kept |

**IFF files.** UniPrefs keeps the whole file as bytes. A module only changes
the fields its keys describe. Unknown chunks and untouched bytes are written
back exactly as they were read. Use writes `ENV:Sys/`; Save writes `ENV:Sys/`
and `ENVARC:Sys/`, like the original editors.

A `PChunkSel` is an ID, the size of the data part when UniPrefs creates the
chunk, and an optional selector. The selector (byte offset, size 1, 2 or 4,
and value) picks one chunk among several with the same ID; font.prefs, for
example, has three FONT chunks.

**How a file is read.** The chunks are taken in file order, as the editors' IFF loops do:
of two chunks of one kind the last wins. A refused file changes nothing. `mflags` (module.h)
and, with `MF_EXT`, `xflags` and the hooks after it describe where an editor reads or
writes differently:

- `XF_PRHD_STOP`: a PRHD shorter than 6 bytes ends the read (some editors do so); without it
  such a PRHD is an IFF error, `XF_PRHD_SHORT`: not looked at.
- `XF_PRHD_OR_CHUNK`: a FORM PREF without PRHD is refused unless its last chunk was read
  (an empty FORM PREF is refused); `XF_NEED_PRHD`: refused always; `XF_CHUNK_FIRST`: the first
  of two chunks wins.
- `check_chunk(id, data, size)`: each chunk of `chunks[]` before it is taken: 0 take, 1 skip,
  -1 IFF error (a reader that checks sizes inside a chunk).
- `before_file(st, arc)`: Save and Use call it just before each file; the module may change
  `st->pf` for that file alone (Ghostbuster writes ENV: and ENVARC: differently).
- `XF_UNITS`: the editor takes `UNIT=n`; `UniPrefs <Module> UNIT=n` edits `<file><n>.prefs`.
- `cli(from, save, msg, len)`: `MK_ACTION` only, the editor's CLI: save 0 USE, 1 SAVE, 2 FROM alone; -1 = the
  editor opens its window (UniPrefs opens the page).
- `start_arg("USER")`, `start_arg("UNIT")` (host version 6): UniPrefs' own start arguments
  (CLI or tool types), for an editor that reads USER.
- `start_arg("INSTALLOS")` (host 0.19): the OS (an `AP_CATEGORY_*` text) whose modules the user chose to
  download at start-up, else NULL; read by the Modules page.

**Environment variables.** With `MK_ENV`, the key name is the variable name,
for example `ENV:EDITOR`. `def` is the text used while the variable is not
set. Only the keys that changed are written. ENV keys may be `KT_INT`,
`KT_HEX`, `KT_BOOL`, `KT_ENUM` or `KT_STR`; the other types need an IFF file.

### Keys (module.h)

| Type          | Shown as                         | Field (IFF)                                  |
|---------------|----------------------------------|----------------------------------------------|
| `KT_INT`      | decimal, range `min`..`max`      | 1, 2 or 4 bytes; `KF_SIGNED` if signed       |
| `KT_HEX`      | `0x%08X` (display IDs)           | 1, 2 or 4 bytes                              |
| `KT_BOOL`     | `Yes`/`No`                       | non-zero, or the bits of `mask`              |
| `KT_ENUM`     | one of `names[]`                 | `values[i]`, or the index i when `values` is NULL |
| `KT_STR`      | text                             | a `size`-byte buffer, NUL-terminated         |
| `KT_STRLIST`  | comma-separated                  | `count` strings of `size` bytes each         |
| `KT_MS`       | milliseconds                     | 8-byte timeval (seconds, microseconds)       |
| `KT_CHAR`     | one character                    | 1 byte                                       |
| `KT_DEC`      | decimal, e.g. `2.54`             | 1, 2 or 4 bytes; shown = raw x `values[0]` / `values[1]`, `values[2]` decimals |
| `KT_RGB`      | `#RRGGBB`                        | 3 bytes (8-bit R,G,B) or 6 (16-bit R,G,B)    |
| `KT_VSTR`     | text                             | from `off` to the end of the chunk, no NUL; the chunk grows and shrinks |
| `KT_BYTES`    | hex digits                       | `size` bytes, or with size 0 to the end of the chunk (resized) |

`KT_DEC` compares as shown: giving back the shown value keeps the exact raw
value (cm shown from millipoints, for example). `KT_VSTR` and variable
`KT_BYTES` with `count = 2` also keep their length in the UWORD just before
them (wbpattern.prefs). An ENV key may be `KT_INT`, `KT_HEX`, `KT_BOOL`,
`KT_ENUM`, `KT_DEC` or `KT_STR`.

Other key fields:

- `chunk`, `off`: the chunk index and byte offset in its data;
- `mask`: a bit field inside the field (shifted down to bit 0);
- `min > max`: no range, written `.min = NOR`;
- `flags`: `KF_SIGNED`; `KF_REBOOT` makes the CLI print "takes effect at the next boot";
- `help`: one line, shown as the gadget's bubble help;
- `hook`: `int hook(ModState *, const Key *, char *err, int errlen)`, called
  after a set. It may refuse the value: return non-zero and fill `err`.

All checks of a value happen in the key engine, so the CLI, ARexx and every
gadget accept and refuse exactly the same values.

### The page (gui_int.h)

`page(Page *p)` returns a MUI object. It *binds* gadgets to keys:

    Object *s = StringObject, StringFrame, End;
    bind(p, "Name", s, AD_STRING, NULL);

UniPrefs then fills the gadgets, reads them back on Save or Use and restores
them on Cancel. The page never reads or writes the file itself.

The adapters (`AD_*` in gui_int.h) are `AD_CHECK`, `AD_CYCLE`, `AD_RADIO`,
`AD_SLIDER`, `AD_STRING` (also the string of a popstring), `AD_TIME`,
`AD_LIST`, `AD_FONT` and a few special ones used by the shipped modules.

Kit functions that build and bind in one call:

| Function | Builds |
|----------|--------|
| `check_row(p, key, label)` | a checkmark with its label, like the system editors |
| `cycle(p, key, labels)` | a cycle gadget; labels in the order of `names[]` |
| `radio(p, key, title, labels, vals, horiz)` | radio buttons |
| `time_row(p, cols, key, label, reverse)` | a time slider row (ms) |

Kit functions that only build:

| Function | Builds |
|----------|--------|
| `slider(min, max, quiet)` | a slider |
| `int_string(maxlen)` | an integer string gadget |
| `stepper(str)` | an integer string with arrows |
| `str_list()`, `listview(l)` | a list of strings and its listview |
| `fill_dir(l, dir, suffix, capitalise)` | fills a list from a drawer |
| `text_fixed(widest)` | a right-aligned text, as wide as `widest` |
| `run_button(label, cmd)` | a button that runs a command |

Other calls:

- `notify_refresh(b, attr)` calls `bind_refresh(b)` whenever `attr` changes;
- `bind_text(b, buf, len)` gives the value a gadget shows, as key text;
- `bind(p, key, NULL, AD_VALUE, NULL)` binds a key to no gadget: the page
  shows `value_get(b)` in gadgets of its own and stores the user's choice
  with `value_set(b, text)` (one colour wheel for eight colours, for
  example). `b->init` holds the value read from the file;
- `pixels_new(PXA_Width, w, PXA_Height, h, PXA_Depth, d, ...)` is a bitplane
  image editor; `PXA_Data` is the planes as
  `KT_BYTES` text, notify `PXA_Changed` to follow the user's drawing.
  `PXA_Cell, n` fixes the cell size; `PXA_Preview` 1 shows the image
  repeated (with `PXA_Cell, 1`: just the image, a preset button), 2 on four
  backgrounds, 3 is a colour strip (a click sets `PXA_Pen`, notify it);
- `pixels_tools()` is a drawing tool bar (dots, freehand,
  rectangles, ellipses, line, fill, undo, redo, clear); `set(bar,
  PXA_Target, editor)` makes it act on that editor;
- `page_test(p)` is a Test button: it writes the page to ENV: now, and
  Cancel puts the old ENV: file back (IFF modules);
- `MK_ACTION` modules have no settings file (a module that sets the clock, for example): give
  `.write = WRITE(fn)`, called by Save (archive 1) and Use (0); it does
  nothing when the user changed nothing and returns NULL, or a message.
  `p->apply = "_Apply"` (in `page()`) adds an Apply button: `write(p, 0)`,
  the window stays (OpenURL). Their Open..., Save As..., Reset To Defaults,
  Last Saved and Restore are ghosted unless the page does them:
  `p->fileop[0..4]` = a hook each, in that order; Open/Save As hooks get the
  chosen path as message (`CallHookPkt(hook, p, path)`) and return 0 when
  done (Save As then makes an icon if Create Icons? is checked);
- `p->menu[i]` (label, key, hook; up to `AP_PAGEMENU` = 8, set in `page()`):
  the page's own Project menu entries, after Save As..., while it shows
  (DefBrowser: Restore original files..., Clean all browsers..., About...);
  a label `NM_BARLABEL` (hook NULL) is a separator. Leave out a key UniPrefs
  uses (O A E Q D L R I);
- windows a page opens are disposed with the application before the
  module's `exit()`: there, forget them (set the pointer NULL), never close
  or dispose them;
- a module may link several sources: DefBrowser.module is DefBrowser.c plus
  DefBrowser's own sources (`AP_EXTRA_DefBrowser`);
- `p->nosave = 1` (in `after`) ghosts Save while the page shows (a clock
  page without a battery-backed clock, for example);
- Catalogs: a module uses the catalog of the editor it replaces, the way
  that editor does: `#define CATALOG "Sys/prefs.catalog"` (the system editors')
  and `TR(id, "English")` for each text, with the editor's own number
  (`cat_str()` underneath); lists of labels: an array of texts and one of
  numbers ending in 0, `TRL(texts, ids)` once in `page()`; the window title:
  `p->wintitle = TR(id, ...)` in `page()`. UniPrefs' own buttons and menus
  use the numbers the system editors give them. The numbers come from the
  editor's load file (`tools/cat_table.py`, `tools/cat_pea.py`;
  `tools/catalogize.py` applies them to a module source); a module of your
  own uses your catalog and numbers. Test a language without changing the
  system's: `setenv UNIPREFS_LANGUAGE deutsch`;
- `orig_data(path, hunk, off, len, crc)` reads bytes of an original editor's
  load file at run time (its images, for example), when their CRC-32 is
  `crc`; NULL for another version. Never copy such data into a module;
- `pf_resize(pf, sel, size)` resizes a chunk (in `init`, for example).
- `key_get`/`key_set` read and set keys of `&p->st`.

The window objects are `app` and `win`; MUI macros (`VGroup`, `Child`,
`Label2`, ...) work as usual. Put all GUI code inside `#ifndef AP_NO_GUI`,
because `make check` builds the module without it.

### What a module may use

UniPrefs gives each module its library bases and C runtime (apmodule.h).

- Library bases: `SysBase`, `DOSBase`, `IntuitionBase`, `GfxBase`,
  `UtilityBase` and `MUIMasterBase`. `MUIMasterBase` and `IntuitionBase` are
  only set once the window opens, so the CLI and the key hooks must not use
  them.
- C runtime: `malloc`, `free`, `realloc`, `calloc`, `snprintf`, `vsnprintf`
  and the `string.h` functions. Memory comes from UniPrefs' allocator, so a
  buffer from `io_load` can be freed with `free`.
- Files: `io_load` and `io_exists`, or dos.library directly.
- Not available: stdio files, `printf`, `exit` and other start-up code.
- Other libraries: open them yourself with `OpenLibrary`, and close them in
  `.exit` (datatypes.library, for example).
- A module is loaded once and runs in UniPrefs' task. Its static data lives
  until UniPrefs quits.
- Your own MUI classes: in `MUIM_AskMinMax`, call the superclass first and
  only then read `msg->MinMaxInfo`. MUI 3.8 sets that pointer during the
  superclass call; read before it, it points anywhere and the machine
  crashes when the page opens.

### Testing

On the PC, `make check` runs the same descriptor checks as UniPrefs and
lists every key of a new file. The checker can also read a real file:

    obj/check/MyTool mytool.prefs                 every key as read from the file
    obj/check/MyTool mytool.prefs Delay=300       set keys, write mytool.prefs.out

On the Amiga, `CHECK` loads a module file from anywhere, without installing it:

    UniPrefs CHECK=RAM:MyTool.module              describes the module, or says why it is refused
    UniPrefs CHECK=RAM:MyTool.module MyTool       opens the window on it
    UniPrefs CHECK=RAM:MyTool.module MyTool LIST  the CLI on it

A module loaded with `CHECK` takes the place of an installed module of the
same name.

UniPrefs refuses a module, prints why and goes on without it, when:

- the file is not a UniPrefs module;
- it was built for another module format;
- its descriptor fails the checks, for example a key past the end of its
  chunk, a key or category given twice, or no category;
- a module of the same name is already loaded.

### Installing

Copy `MyTool.module` to `SYS:Prefs/Modules/`. It is listed the next
time UniPrefs starts.

## Compatibility

A binary module starts with `moveq #-1,d0; rts`, the magic `'ALPF'`, the
module format number `AP_ABI` and a pointer to `ap_init` (apabi.h,
modstart.S). UniPrefs loads only modules of its own format.

- `AP_ABI` changes when `Module`, `Key`, `ModState`, `Page` or `Bind` change
  layout or meaning. Modules must then be rebuilt with the new SDK.
- New UniPrefs functions are added at the end of `APHost` and raise
  `AP_HOST_VERSION`. A module that needs them checks `AP->version`. Older
  modules keep working.

This SDK is module format 4, host version 6. New in host version 2: `value_get`, `value_set`,
`pixels_new`, `pf_resize`, `pixels_tools`, `page_test` and `orig_data`; in 3: `st_save`,
`page_reload`, `page_collect`; in 4 (read-only): `version_string()` (the program's `$VER` string)
and `module_list()` (every loaded module in list order, NULL-terminated; used by About); in 5:
`Page.on_show`; in 6: `start_arg()` and the `MF_EXT` fields of `Module`.
A module built with this SDK refuses an older program (host version below 6).

module.h also names the module drawer `AP_MODDIR` (the one the program loads from) and the
program's file name `AP_PROGNAME` (used by Updates). The program was called Preferences before 0.21:
`AP_PROG_TARGET` ("Preferences") stays its name in the release manifests, which every Updates reads.

## What UniPrefs adds to every page

The window has the menus of the system editors for the page shown: Project
(Open..., Save As..., Replace Original Editor..., Quit), Edit (Reset To
Defaults, Last Saved, Restore) and Settings (Create Icons?). A module gets
them without code: Reset To Defaults uses `init` (IFF) or each key's `def`
(ENV), Last Saved reads ENVARC:, Save As writes the IFF file or Name=Value
lines that `FROM=` and Open... read back. `.after` runs again after each of
them, so a page that keeps gadgets of its own refreshes them there.

Replace Original Editor... deletes the module's `editor` file and writes in its
place an AmigaDOS script (protection bit s) that starts `UniPrefs <module>
ALONE` with the editor's arguments, and turns the editor's icon into a project
icon whose default tool is C:IconX. It is refused for launchers, for a page of
another system than the running one, and for a module with `XF_KEEP_EDITOR`
(MF_EXT): set it when the page leaves part of the editor's work to the editor.

## Limits

| Limit | Value |
|-------|-------|
| Modules | 128 |
| Categories | 48 in all, 4 per module, 31 characters each |
| Bound gadgets per page | 40 (`MAXBINDS`) |
| Module name | 30 characters |
| Environment variable text | 255 characters (`AP_ENVLEN`) |
