#
_XDCBUILDCOUNT = 
ifneq (,$(findstring path,$(_USEXDCENV_)))
override XDCPATH = C:/ti/simplelink_cc13x0_sdk_2_10_00_36/source;C:/ti/simplelink_cc13x0_sdk_2_10_00_36/kernel/tirtos/packages
override XDCROOT = C:/ti/xdctools_3_50_05_12_core
override XDCBUILDCFG = ./config.bld
endif
ifneq (,$(findstring args,$(_USEXDCENV_)))
override XDCARGS = 
override XDCTARGETS = 
endif
#
ifeq (0,1)
PKGPATH = C:/ti/simplelink_cc13x0_sdk_2_10_00_36/source;C:/ti/simplelink_cc13x0_sdk_2_10_00_36/kernel/tirtos/packages;C:/ti/xdctools_3_50_05_12_core/packages;..
HOSTOS = Windows
endif
