# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2020-2026 SEN Labs e.U.

NAME = sen_server
ARCH = $(shell getarch)

TARGET_DIR := ./bin
TYPE = APP
APP_MIME_SIG = application/x-vnd.sen-labs.sen-server

SRCS := src/relations/RelationHandler.cpp \
    	src/relations/RelationWrite.cpp \
	src/relations/RelationSticky.cpp \
    	src/relations/RelationSets.cpp \
    	src/relations/SelfRelationHandler.cpp \
	src/config/SenConfigHandler.cpp \
	src/server/Cli.cpp \
	src/deskbar/DeskbarView.cpp \
	src/ui/AboutWindow.cpp \
	src/ui/CheckWindow.cpp \
	src/ui/InfoWindow.cpp \
	src/ui/Knot.cpp \
	src/ui/QueryFiles.cpp \
	src/ui/SenUi.cpp \
	src/ui/SettingsWindow.cpp \
	src/ui/StatItem.cpp \
	src/stats/SenStats.cpp \
	src/server/SenServer.cpp

RDEFS = src/resources/sen_server.rdef

LIBS = be translation spdlog fmt $(STDCPPLIBS)
#	Specify the level of optimization that you want. Specify either NONE (O0),
#	SOME (O1), FULL (O2), or leave blank (for the default optimization level).

#	Specify additional paths to directories following the standard libXXX.so
#	or libXXX.a naming scheme. You can specify full paths or paths relative
#	to the Makefile. The paths included are not parsed recursively, so
#	include all of the paths where libraries must be found. Directories where
#	source files were specified are	automatically included.
LIBPATHS =

#	Additional paths to look for system headers. These use the form
#	"#include <header>". Directories that contain the files in SRCS are
#	NOT auto-included here.
SYSTEM_INCLUDE_PATHS = $(shell findpaths -e B_FIND_PATH_HEADERS_DIRECTORY)

#	Additional paths paths to look for local headers. These use the form
#	#include "header". Directories that contain the files in SRCS are
#	automatically included.
LOCAL_INCLUDE_PATHS =

OPTIMIZE =

# 	Specify the codes for languages you are going to support in this
# 	application. The default "en" one must be provided too. "make catkeys"
# 	will recreate only the "locales/en.catkeys" file. Use it as a template
# 	for creating catkeys for other languages. All localization files must be
# 	placed in the "locales" subdirectory.
LOCALES = en

#	Specify all the preprocessor symbols to be defined. The symbols will not
#	have their values set automatically; you must supply the value (if any) to
#	use. For example, setting DEFINES to "DEBUG=1" will cause the compiler
#	option "-DDEBUG=1" to be used. Setting DEFINES to "DEBUG" would pass
#	"-DDEBUG" on the compiler's command line.
DEFINES = HAIKU_TARGET_PLATFORM_HAIKU SPDLOG_COMPILED_LIB SPDLOG_FMT_EXTERNAL

#	Specify the warning level. Either NONE (suppress all warnings),
#	ALL (enable all warnings), or leave blank (enable default warnings).
WARNINGS =

#	With image symbols, stack crawls in the debugger are meaningful.
#	If set to "TRUE", symbols will be created.
SYMBOLS := TRUE

#	Includes debug information, which allows the binary to be debugged easily.
#	If set to "TRUE", debug info will be created.
DEBUGGER := TRUE

#	Specify any additional compiler flags to be used.
COMPILER_FLAGS =

#	Specify any additional linker flags to be used.
LINKER_FLAGS =

VERSION_RDEF = $(NAME).rdef
RDEFS += $(VERSION_RDEF)

## Include the Makefile-Engine
DEVEL_DIRECTORY = \
	$(shell findpaths -e B_FIND_PATH_DEVELOP_DIRECTORY etc/makefile-engine)
include $(DEVEL_DIRECTORY)

ifeq ($(DEBUGGER),TRUE)
	DEFINES += DEBUGGING TRACING
endif

distclean:
	-rm $(TARGET_DIR)/$(NAME)

# (the package is made by package.sh)

#$(VERSION_RDEF): Makefile
