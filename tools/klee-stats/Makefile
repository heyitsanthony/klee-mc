#===-- tools/klee-stats/Makefile -----------------------*- Makefile -*--===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#

LEVEL = ../..

SCRIPTFILES := klee-stats KleeStat.py
SCRIPTPATHS=$(SCRIPTFILES:%=$(PROJ_SRC_DIR)/%)

TOOLSCRIPTNAME := klee-stats

include $(LEVEL)/Makefile.common

# FIXME: Move this stuff (to "build" a script) into Makefile.rules.

ToolBuildPath := $(ToolDir)/$(TOOLSCRIPTNAME)

all-local:: $(ToolBuildPath)

$(ToolBuildPath): $(ToolDir)/.dir

$(ToolBuildPath): $(TOOLSCRIPTNAME)
	$(Echo) Copying $(BuildMode) script $(TOOLSCRIPTNAME)
	$(Verb) $(CP) -f $(SCRIPTPATHS) "$(ToolDir)"
	$(Verb) chmod 0755 "$@"

ifdef NO_INSTALL
install-local::
	$(Echo) Install circumvented with NO_INSTALL
uninstall-local::
	$(Echo) Uninstall circumvented with NO_INSTALL
else
DestTool = $(PROJ_bindir)/$(TOOLSCRIPTNAME)

install-local:: $(DestTool)

$(DestTool): $(ToolBuildPath) $(PROJ_bindir)
	$(Echo) Installing $(BuildMode) $(DestTool)
	$(Verb) $(ProgInstall) $(ToolBuildPath) $(DestTool)

uninstall-local::
	$(Echo) Uninstalling $(BuildMode) $(DestTool)
	-$(Verb) $(RM) -f $(DestTool)
endif
