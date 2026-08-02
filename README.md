# GTK4-slow-launch-speed-test

Two minimal C applications that use **GTK 3** and **GTK 4** that load and display an image. Used for comparing application **launch speed**.

Each binary prints launch-time milestones to **stderr** so the variants can be measured side by side.

## Build

```sh
./build-and-run.sh
```

# Results

<img width="460" height="269" alt="image" src="https://github.com/user-attachments/assets/cf8c61bc-0e01-47f1-ab9d-1b015c38b97b" />

**GTK4** takes almost 10x to launch on my system

# Why

The reason is that GTK4 takes too much time reading `XDG_DATA_DIRS` on my system, setting `XDG_DATA_DIRS` to a non existing location cuts the time down to 60ms: 

<img width="635" height="145" alt="image" src="https://github.com/user-attachments/assets/3878877c-fb8c-4f9b-a348-85c1ba846089" />

---

This is still a significant degradation, it is still almost 2x the launch time of GTK3.

The reason for this other delay is that GTK4 is hardware accelerated, forcing the software renderer (which is what GTK3 uses) fixes that problem: 

```sh
GDK_RENDERER=cairo GDK_DISABLE=gl,vulkan
```

<img width="823" height="96" alt="image" src="https://github.com/user-attachments/assets/5ddb5295-549e-4e80-9e82-73623263ea5d" />

---

# Deeper Investigation

**Disclaimer: The following is an analysis conducted by DeepSeek V4 Flash**

# Why GTK4 apps take ~10× longer to launch than the same GTK3 app

A root-cause investigation of the `GTK4-slow-launch-speed-test` repo, using
GTK **4.22.4**, the built-in `GTK_DEBUG=icontheme` tracing, `strace`, and
`perf`. The problem reproduces even with a clean `XDG_DATA_DIRS=/usr/share`.

---

## 1. TL;DR

* The delay happens **before** the app's `activate` callback — inside
  `GApplication::startup`, triggered by `gtk_application_set_window_icon()`,
  which performs the **first icon-theme lookup**.
* That lookup forces GTK4 to load the **active icon theme** from disk
  (here `Papirus-Dark` and its inherited themes). Loading is done by parsing
  the pre-built `icon-theme.cache` files.
* The cache parser is **quadratic**: for *each* of the theme's ~102
  sub-directories it walks **the entire cache** (all hash buckets / all
  ~297,000 image entries) to pick out the icons that belong to that one
  sub-directory. 102 × 297,000 ≈ **30 million** entry inspections.
* During startup GTK loads the theme **twice** (initial load, then again
  when the `gtk-icon-theme-name` setting settles), roughly doubling the cost.
* The `strace` evidence matches: no slow I/O syscalls at all; ~86 % of
  syscall time is `futex` — the main thread parked waiting for the
  icon-theme worker thread that does the (userspace, CPU-bound) cache parse.

Measured on this machine: GTK4 `startup` ≈ **360–390 ms**, the identical
GTK3 app ≈ **31 ms**.

---

## 2. Environment and method

| | |
|---|---|
| GTK version | 4.22.4 (`gtk+-3.0` for the GTK3 build) |
| Display | X11 |
| Active icon theme | `Papirus-Dark` (from `gsettings`, `gtk-icon-theme-name`) |
| Active CSS theme | `Dark-darose` |
| `XDG_DATA_DIRS` used for the runs | `/usr/share` (clean, real filesystem) |
| Apps | the two minimal image-viewer binaries from this repo |

Both binaries print two milestones to stderr:

* `startup` — start of the `activate` callback (i.e. *after* all of
  `GApplication::startup` / `gtk_init()` has already run),
* `displayed` — after `gtk_window_present()`.

`t0` is captured at the very first line of `main()`, so every number below is
the true wall time from process start.

---

## 3. Measurements

### 3.1 GTK4 vs GTK3 (the 10× gap)

`XDG_DATA_DIRS=/usr/share`, three runs each:

```
[GTK4] startup   383.01 ms   displayed   397.00 ms
[GTK4] startup   379.82 ms   displayed   390.19 ms
[GTK4] startup   356.88 ms   displayed   365.19 ms

[GTK3] startup    30.87 ms   displayed    41.61 ms
[GTK3] startup    33.59 ms   displayed    43.31 ms
[GTK3] startup    31.10 ms   displayed    40.01 ms
```

The gap is ~12×. Crucially, `startup` is *already* ~360–380 ms, so **all of
the delay is inside `GApplication::startup`, before the app's own code runs**,
and the two milestones are only ~10 ms apart.

### 3.2 Isolating the trigger (`XDG_DATA_DIRS` sweep)

| `XDG_DATA_DIRS` | GTK4 `startup` | Comment |
|---|---|---|
| `/usr/share` | 357–383 ms | real icon theme is scanned |
| `/nonexistent` | ~52–61 ms | nothing exists, all probes fail fast |
| `/usr/share` + `GDK_RENDERER=cairo` | ~335 ms | renderer is **not** the cause |

The only slow configuration is the one where a real `/usr/share/icons`
directory ends up on the icon-theme search path. Disabling the hardware
renderer changes almost nothing (~20 ms), so rendering/GL init is not the
bottleneck.

### 3.3 What GTK actually loads (`GTK_DEBUG=icontheme`)

```
look for icon cache in /usr/share/icons/Papirus-Dark
found icon cache for /usr/share/icons/Papirus-Dark
look for icon cache in /usr/share/icons/breeze-dark
found icon cache for /usr/share/icons/breeze-dark
look for icon cache in /usr/share/icons/breeze
found icon cache for /usr/share/icons/breeze
look for icon cache in /usr/share/icons/hicolor
found icon cache for /usr/share/icons/hicolor
...
Current icon themes Papirus-Dark breeze-dark breeze hicolor
change to icon theme "Papirus-Dark"
unmapping icon cache
...
Current icon themes Papirus-Dark breeze-dark breeze hicolor
```

Two important facts:

1. The active theme is **`Papirus-Dark`** with inherited themes
   `breeze-dark → breeze → hicolor`. Every theme ships a valid
   `icon-theme.cache` (nothing falls back to raw `readdir`).
2. The whole chain is loaded **twice** (`Current icon themes …` appears
   twice, with `unmapping icon cache` in between) — the initial load and a
   re-load that happens when the `gtk-icon-theme-name` setting change settles.

### 3.4 Size of what gets parsed

| theme | sub-dirs in `index.theme` | cache size | hash buckets | cache entries |
|---|---|---|---|---|
| `Papirus-Dark` | **102** | 3.07 MB | 6,247 | **~297,000** |
| `breeze` | ~55 | 724 KB | 2,777 | ~54,000 |
| `hicolor` | 649 | 49 KB | 367 | ~1,100 |

`Papirus-Dark` dominates: ~297 k cache entries, listed once per sub-directory.

---

## 4. `strace` analysis

Full command:

```
strace -f -c env XDG_DATA_DIRS=/usr/share ./gtk4-image-viewer
```

Syscall time summary (all threads):

```
% time     seconds  usecs/call     calls   errors syscall
------ ----------- ----------- --------- --------- ------
 86.09    0.343998         446       770      31 futex
  9.47    0.037846         225       168       0 ppoll
  0.71    0.002855           4       640       0 mmap
  0.50    0.002017          13       150       2 ioctl
  0.42    0.001698           2       805     277 openat
  0.34    0.001345           1       805     510 recvmsg
  0.29    0.001176           1       857       0 read
  0.26    0.001039           1       659      34 newfstatat
  0.24    0.000961           2       327     242 readlink
  0.22    0.000860           1       541       0 close
```

### 4.1 What the summary tells us

* **No slow syscall at all.** The most expensive individual syscall in the
  whole trace is ~2 ms. `newfstatat` (659 calls) and `openat` (805 calls) are
  ~1–2 µs each. There is no disk I/O stall to find here.
* **86 % of syscall time is `futex`** — 770 calls, ~446 µs average, ~0.34 s
  total. This is the main thread *blocked waiting for a thread to be scheduled
  / for a lock*, not doing I/O.
* `ppoll` (0.038 s) is the main loop waking for the 1-second quit timer and
  dbus traffic.

### 4.2 Why `futex` dominates

GTK4's icon theme loads its data on a **worker thread** (`GTask` run in the
`pool-0` thread), while holding the icon-theme mutex
(`gtk_icon_theme_load_in_thread()`, `gtk/gtkicontheme.c:795-804`). The main
thread, inside its first icon lookup, calls `gtk_icon_theme_has_icon()`
(`gtk/gtkicontheme.c:2421`) which tries to take the same mutex and simply
waits — appearing in `strace` as `futex`.

The ~0.34 s of `futex` therefore **measures the wall time the main thread
spends waiting for the icon-theme worker** — which is busy in userspace
parsing the cache. `strace` cannot see that CPU time (it only times
syscalls), which is why the file I/O numbers look innocent.

### 4.3 `perf` confirms it is CPU-bound cache parsing

`perf record` while launching shows the hot time inside libgtk-4 on the
`pool-0` worker thread, in icon-name string handling and hash lookups
(`g_hash_table_lookup`, `gtk_string_set_add` / string interning) — exactly
the code in `gtk_icon_cache_list_icons_in_directory()`. There is no
significant time in the kernel, the GPU, or filesystem code.

---

## 5. The root cause in the GTK4 source

### 5.1 Why the icon theme is touched at all during startup

`GtkApplication` sets a default themed window icon for the app id during
startup, before any `activate` handler runs:

```
gtk/gtkapplication.c:334   gtk_init ();
gtk/gtkapplication.c:342   gtk_application_set_window_icon (application);
```

`gtk_application_set_window_icon()` (`gtk/gtkapplication.c:300-315`) calls
`gtk_icon_theme_has_icon()` (`gtkicontheme.c:2421`), which calls
`ensure_valid_themes()` (`gtkicontheme.c:1951`) → `load_themes()`
(`gtkicontheme.c:1858`). This is the **first** forced, synchronous load of the
whole icon theme — before the window (and your `startup` milestone) exists.

The GTK3 test app never takes this path (no themed icons are needed to load a
plain image), which is why GTK3 stays at ~30 ms.

### 5.2 The quadratic cache parser

`load_themes()` walks the active theme's `index.theme` and calls
`theme_subdir_load()` for **every sub-directory** it declares:

```
gtk/gtkicontheme.c:1735   for (i = 0; dirs[i] != NULL; i++)
gtk/gtkicontheme.c:1736     theme_subdir_load (self, theme, theme_file, dirs[i]);
```

`theme_subdir_load()` (`gtkicontheme.c:3130`) then resolves each sub-directory
against the icon cache:

```
gtk/gtkicontheme.c:3217   if (dir_mtime->cache != NULL)
gtk/gtkicontheme.c:3218     icons = gtk_icon_cache_list_icons_in_directory (cache, subdir, &self->icons);
```

And this is where the quadratic behaviour lives, `gtk/gtkiconcache.c:179`:

```
gtk_icon_cache_list_icons_in_directory (cache, directory, set)
{
  directory_index = get_directory_index (cache, directory);   /* (a) linear scan */

  for (i = 0; i < n_buckets; i++)                             /* (b) whole cache */
    {
      chain_offset = first chain of bucket i;
      while (chain_offset != 0xffffffff)
        {
          n_images = image list of chain;
          for (j = 0; j < n_images; j++)                      /* (c) every image */
            if (image_directory_index == directory_index) break;
          if (match) intern icon name into `set`;
          chain_offset = next chain;
        }
    }
}
```

(a) `get_directory_index()` (`gtkiconcache.c:156`) does a linear `strcmp`
scan of the cache's directory list.

(b)+(c) The body walks **every hash bucket and every image entry of the
entire cache** for each sub-directory. It must, because the cache is indexed
by icon-name hash, not by directory — so answering "which icons are in
subdir X?" requires a full sweep.

### 5.3 The cost in numbers

* Sub-directories to resolve: **102** (`Papirus-Dark` `index.theme`).
* Cache entries swept per sub-directory: **~297,000**.
* Total: **~30,000,000** entry inspections, each doing endian-safe u16/u32
  reads plus a `strcmp` / string-interning for every match.
* The whole theme chain (`Papirus-Dark → breeze-dark → breeze → hicolor`) is
  loaded, and the debug trace shows it is loaded **twice** during startup
  (`gtkicontheme.c:841` queues the initial load; `gtkicontheme.c:1104` queues
  another when `gtk-icon-theme-name` notifies).

This is the ~300–350 ms. It scales with (theme sub-dirs × theme cache
entries); a small theme (e.g. `hicolor`, ~1,100 entries) is negligible, which
is why `XDG_DATA_DIRS=/usr/share/icons`-style configurations that skip the
real theme appear instant.

---

## 6. Why GTK3 is not affected

* The GTK3 test app only loads an image file (`gtk_image_new_from_file`); it
  never looks up a themed icon, so the icon theme is never loaded.
* GTK3's `GtkApplication` does not perform the same forced themed-icon lookup
  for the window icon during startup.
* The measured GTK3 number (≈ 31 ms) is essentially the GTK3 initialization
  floor — the GTK4 floor is also ~50 ms (see `/nonexistent` run) once the icon
  theme is *not* loaded; the gap to ~360 ms is exactly the theme load.

---

## 7. Conclusion

GTK4's slow launch is caused by the icon-theme machinery, not by the
hardware renderer and not by the data directories themselves:

1. `GApplication::startup` performs a themed window-icon lookup
   (`gtk_application_set_window_icon`, `gtk/gtkapplication.c:342`) that forces
   the icon theme to load synchronously before `activate`.
2. `gtk_icon_cache_list_icons_in_directory()` (`gtk/gtkiconcache.c:179`)
   resolves each sub-directory by sweeping the *entire* icon cache, making
   theme loading **O(sub-directories × cache entries)**.
3. With `Papirus-Dark` (~102 sub-dirs, ~297 k entries) that is ~30 M
   operations, repeated twice during startup → ~300 ms.
4. `strace` shows the main thread merely parked in `futex` (~86 % of syscall
   time) waiting for the worker thread that does that CPU-bound parse; `perf`
   localizes the CPU time to libgtk's cache/string code.

### Practical workarounds / fixes

* A smaller, cached icon theme (e.g. a tuned `hicolor`) makes launch time
  drop to the ~50 ms GTK4 floor.
* Upstream, the loader could index the cache by directory once (instead of
  re-sweeping it per sub-directory), eliminating the quadratic blow-up, and
  the duplicated load triggered by the theme-name notification could be
  avoided.
