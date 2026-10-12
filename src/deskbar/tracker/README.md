<!--
SPDX-License-Identifier: MIT
SPDX-FileCopyrightText: 2026 SEN Labs e.U.
-->
# Headers of Tracker

`NavMenu.h` and `SlowMenu.h` are the headers of `BNavMenu`, the menu of Tracker that shows the files of a folder with icons and names (the menus of the Deskbar for the
Applications and Preferences are made with it), as Haiku installs them in `develop/headers/private/tracker` (`SlowMenu.h`, which `NavMenu.h` includes, is installed
by nobody: it is a copy from `src/kits/tracker`). They are under the Open Tracker License of Be, Inc. (in the files), which is the one that all of Tracker has.

The menus of SEN in the Deskbar (`../DeskbarView.cpp`) use `BNavMenu` from `libtracker.so`, so that they look like the other menus of the Deskbar, and so that
the files in them are what Tracker shows for them. The classes are private: a version of Haiku with another layout of them needs these headers from its source.
