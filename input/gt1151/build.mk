# Polled Goodix touch reports on a board-initialized I2C bus.
ifeq ($(TIKU_DRV_INPUT_GT1151_ENABLE),1)
SRCS += drivers/input/gt1151/tiku_drv_input_gt1151.c
CFLAGS += -DTIKU_DRV_INPUT_GT1151_ENABLE=1
endif
