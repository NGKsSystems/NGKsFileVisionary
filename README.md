# NGKsFileVisionary

## Overview

NGKsFileVisionary is a Windows desktop C++ application (Qt6 Widgets) for all-seeing filesystem and archive exploration. It provides recursive filesystem viewing in a single hierarchy and archive exploration backed by 7-Zip (`7za.exe`).

## Features

- Filesystem explorer with root selector, rescan, hidden/system toggles, extension filter, and search.
- Multiple filesystem view modes: `Standard`, `Full Hierarchy`, and `Flat Files`.
- Context-aware right-click menus for folder/file/archive/multi-selection/background operations.
- Archive detection for `.7z`, `.zip`, `.tar`, `.tar.gz`, `.tar.xz`, `.tgz`, `.gz`, `.xz`.
- Archive Explorer window with Back/Forward navigation, breadcrumb path, and search.
- Archive operations via 7-Zip process backend: list, extract all, extract selected, create archive.
- Background scanning and non-blocking archive process execution with cancellation.

## Context Menus

The app provides distinct menus for:

- Folder items (navigation, file operations, archive creation, favorites, tree snapshot, terminal/code actions)
- File items (open/open with, hash, preview, copy/move/delete/rename/duplicate, properties)
- Archive items (explore archive, extract here/to/all, open externally)
- Multi-selection (bulk copy/move/delete/archive/compress)
- Empty background area (refresh/rescan/new folder/new text file/tree snapshot/open tools/properties)

Folder navigation actions stay internal; `.lnk` is rejected as a navigable folder.

## Tree Snapshot

`Tree Snapshot...` is available only on real filesystem folders.

Snapshot options:

- Snapshot Type: `Full Recursive Tree` or `Visible View Tree`
- Output Format: `Plain Text (.txt)` or `Markdown (.md)`
- Options: include files/folders/hidden, names-only vs full-paths, max depth, unicode/ascii tree branches
- Output Action: copy to clipboard, save to file, or preview dialog

Generation behavior:

- Full recursive mode runs off the UI thread and supports cancellation.
- Visible-view mode exports current UI model state.
- Markdown output is fenced (` ```text `) for printable/exportable docs.

## Archive Creation / Extraction

Archive actions use bundled `7za.exe` (`third_party/7zip/7za.exe`) with asynchronous `QProcess` execution.

Supported UI actions:

- Create archive from selected paths
- Compress directly to `.zip`, `.7z`, or `.tar`
- Extract archive here or to a selected destination
- Explore archive contents internally

Common command patterns:

- List: `7za l -slt <archive>`
- Extract: `7za x <archive> -o<dest> -y -bsp1 -bb1`
- Create: `7za a <archive> <inputs...> -y -mx=5 -bsp1 -bb1`

## Archive Explorer

Archive Explorer supports:

- Internal tree view of archive paths
- Back/forward path navigation and breadcrumb
- Search within archive entries
- Extract selected, extract all, copy internal path, open entry by temp extraction

Double-clicking an archive file in filesystem view opens Archive Explorer by default.

## Build

DevFabric-only build flow (no CMake):

```powershell
python -m ngksdevfabric doctor
cmd /c debug\run_devfabric_build.cmd
build\debug\bin\app.exe
```

`debug/run_devfabric_build.cmd` always regenerates `build_graph/debug/ngksbuildcore_plan.json` before invoking BuildCore, which prevents stale-plan build failures after source/graph changes.

Build graph configuration is in `graph/filevisionary.graph.json`.

## Usage

1. Launch the app and choose a root folder.
2. Click **Rescan** to start incremental recursive scan.
3. Use extension filter (`.png;.mp3;`) and search for substring filtering.
4. Right-click archive files and choose **Explore Archive…**.
5. In Archive Explorer, use Back/Forward and breadcrumb path for internal navigation.

## Third-Party Licenses

- Bundled 7-Zip engine: `third_party/7zip/7za.exe`
- License: `third_party/7zip/LICENSE.txt`
- Attribution/notice: `third_party/7zip/NOTICE.txt`

## Known Limitations

- `third_party/7zip/7za.exe` must exist and be a valid binary for archive actions.
- Exact DevFabric graph schema may vary by installed `ngksdevfabric` version.
- Folder copy/duplicate operations are intentionally conservative in this pass (file-focused for large-operation safety).
- `Open in New Tab` is present as an architecture-ready placeholder until tabbed view is implemented.
- Some archive formats may not support all selective-extract flows uniformly; `Extract All` is the universal fallback.

## Third-Party 7-Zip Usage

- Runtime binary: `third_party/7zip/7za.exe`
- License: `third_party/7zip/LICENSE.txt`
- Notice: `third_party/7zip/NOTICE.txt`
- NGKsFileVisionary invokes 7-Zip through `QProcess`; command output is surfaced to operation status and proof logs.
