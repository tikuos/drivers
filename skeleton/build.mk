# tikudrivers/skeleton/build.mk
#
# Per-driver Makefile fragment. Auto-included by the top-level
# Makefile when tikudrivers/ is present. Compiles the driver's
# sources only when its enable flag is set, so a kernel build
# without the flag pays zero footprint for this driver.
#
# Copy this file alongside the rest of the skeleton directory and
# rename the flag to match the new driver
# (TIKU_DRV_<CLASS>_<NAME>_ENABLE).

ifeq ($(TIKU_DRV_SKELETON_ENABLE),1)
SRCS   += $(wildcard tikudrivers/skeleton/*.c)
CFLAGS += -DTIKU_DRV_SKELETON_ENABLE=1
endif
