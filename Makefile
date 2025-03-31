NAME = sen_server
ARCH = $(shell getarch)

V_MAJOR = 0
V_MIDDLE = 4
V_MINOR = 2
V_VARIETY = B_APPV_DEVELOPMENT
V_BUILD = 0

TARGET_DIR := ./bin
PACKAGE = $(TARGET_DIR)/$(NAME)_$(VERSION)-$(ARCH).hpkg
TYPE = APP
APP_MIME_SIG = application/x-vnd.sen-labs.sen-server

SRCS := src/relations/RelationsHandler.cpp \
    	src/relations/SelfRelations.cpp \
    	src/relations/IceDustGenerator.cpp \
		src/config/SenConfigHandler.cpp \
		src/server/SenServer.cpp

RDEFS = src/resources/sen_server.rdef

LIBS = be $(STDCPPLIBS)
#	Specify the level of optimization that you want. Specify either NONE (O0),
#	SOME (O1), FULL (O2), or leave blank (for the default optimization level).
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
DEFINES = HAIKU_TARGET_PLATFORM_HAIKU

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

PACKAGE_DIR = $(OBJ_DIR)/package
ARCH_PACKAGE_INFO = $(OBJ_DIR)/PackageInfo_$(ARCH)

$(ARCH_PACKAGE_INFO): Makefile
	cat ./PackageInfo | sed 's/$$VERSION/$(VERSION)/' | sed 's/$$ARCH/$(ARCH)/' > $(ARCH_PACKAGE_INFO)

$(PACKAGE): $(TARGET_DIR)/$(NAME) $(ARCH_PACKAGE_INFO)
	mkdir -p $(PACKAGE_DIR)/apps
	mkdir -p $(PACKAGE_DIR)/data/deskbar/menu/Applications
	cp $(TARGET_DIR)/$(NAME) $(PACKAGE_DIR)/apps/
	-ln -s ../../../../apps/$(NAME) $(PACKAGE_DIR)/data/deskbar/menu/Applications/$(NAME)
	package create -C $(PACKAGE_DIR)/ -i $(OBJ_DIR)/PackageInfo_$(ARCH) $(PACKAGE)
	mimeset $(PACKAGE)

package: $(PACKAGE)

distclean:
	-rm $(TARGET_DIR)/$(NAME)

install:: package
	pkgman install $(PACKAGE)

#$(VERSION_RDEF): Makefile
