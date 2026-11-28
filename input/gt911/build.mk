# Polled Goodix touch reports on a board-initialized I2C bus.
ifeq ($(TIKU_DRV_INPUT_GT911_ENABLE),1)
SRCS += drivers/input/gt911/tiku_drv_input_gt911.c
CFLAGS += -DTIKU_DRV_INPUT_GT911_ENABLE=1
endif
