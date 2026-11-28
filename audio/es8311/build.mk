# ES8311 control sequences for the ESP32-P4X Function EV board.
ifeq ($(TIKU_DRV_AUDIO_ES8311_ENABLE),1)
SRCS += drivers/audio/es8311/tiku_drv_audio_es8311.c
CFLAGS += -DTIKU_DRV_AUDIO_ES8311_ENABLE=1
endif
