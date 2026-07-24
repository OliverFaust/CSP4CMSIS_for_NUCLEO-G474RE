# library/csp4cmsis/csp4cmsis.mk

# directory declaration
CSP4CMSIS_DIR = $(LIBRARIES_ROOT)/csp4cmsis

# source and include directories
CSP4CMSIS_ASMSRCDIR    = $(CSP4CMSIS_DIR)/src
CSP4CMSIS_CSRCDIR      = $(CSP4CMSIS_DIR)/src
CSP4CMSIS_CXXSRCSDIR   = $(CSP4CMSIS_DIR)/src
CSP4CMSIS_INCDIR       = $(CSP4CMSIS_DIR)/inc $(CSP4CMSIS_DIR)/inc/csp

# find all the source files in the target directories
CSP4CMSIS_CSRCS = $(call get_csrcs, $(CSP4CMSIS_CSRCDIR))
CSP4CMSIS_CXXSRCS = $(call get_cxxsrcs, $(CSP4CMSIS_CXXSRCSDIR))
CSP4CMSIS_ASMSRCS = $(call get_asmsrcs, $(CSP4CMSIS_ASMSRCDIR))

# get object files
CSP4CMSIS_COBJS = $(call get_relobjs, $(CSP4CMSIS_CSRCS))
CSP4CMSIS_CXXOBJS = $(call get_relobjs, $(CSP4CMSIS_CXXSRCS))
CSP4CMSIS_ASMOBJS = $(call get_relobjs, $(CSP4CMSIS_ASMSRCS))
CSP4CMSIS_OBJS = $(CSP4CMSIS_COBJS) $(CSP4CMSIS_ASMOBJS) $(CSP4CMSIS_CXXOBJS)

# get dependency files
CSP4CMSIS_DEPS = $(call get_deps, $(CSP4CMSIS_OBJS))

# extra macros to be defined
CSP4CMSIS_DEFINES = -DLIB_CSP4CMSIS

# genearte library
ifeq ($(CSP4CMSIS_LIB_FORCE_PREBUILT), y)
override CSP4CMSIS_OBJS:=
endif
CSP4CMSIS_LIB_NAME = libcsp4cmsis.a
LIB_LIB_CSP4CMSIS := $(subst /,$(PS), $(strip $(OUT_DIR)/$(CSP4CMSIS_LIB_NAME)))

# library generation rule
$(LIB_LIB_CSP4CMSIS): $(CSP4CMSIS_OBJS)
	$(TRACE_ARCHIVE)
ifeq "$(strip $(CSP4CMSIS_OBJS))" ""
	$(CP) $(PREBUILT_LIB)$(CSP4CMSIS_LIB_NAME) $(LIB_LIB_CSP4CMSIS)
else
	$(Q)$(AR) $(AR_OPT) $@ $(CSP4CMSIS_OBJS)
	$(CP) $(LIB_LIB_CSP4CMSIS) $(PREBUILT_LIB)$(CSP4CMSIS_LIB_NAME)
endif

# specific compile rules
# user can add rules to compile this middleware
# if not rules specified to this middleware, it will use default compiling rules

# Middleware Definitions
LIB_INCDIR += $(CSP4CMSIS_INCDIR)
LIB_CSRCDIR += $(CSP4CMSIS_CSRCDIR)
LIB_CXXSRCDIR += $(CSP4CMSIS_CXXSRCSDIR)
LIB_ASMSRCDIR += $(CSP4CMSIS_ASMSRCDIR)

LIB_CSRCS += $(CSP4CMSIS_CSRCS)
LIB_CXXSRCS += $(CSP4CMSIS_CXXSRCS)
LIB_ASMSRCS += $(CSP4CMSIS_ASMSRCS)
LIB_ALLSRCS += $(CSP4CMSIS_CSRCS) $(CSP4CMSIS_ASMSRCS)

LIB_COBJS += $(CSP4CMSIS_COBJS)
LIB_CXXOBJS += $(CSP4CMSIS_CXXOBJS)
LIB_ASMOBJS += $(CSP4CMSIS_ASMOBJS)
LIB_ALLOBJS += $(CSP4CMSIS_OBJS)

LIB_DEFINES += $(CSP4CMSIS_DEFINES)
LIB_DEPS += $(CSP4CMSIS_DEPS)
LIB_LIBS += $(LIB_LIB_CSP4CMSIS)
