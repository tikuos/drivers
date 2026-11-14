# ES8389 control sequences for the ESP32-S31 Korvo board.
ifeq ($(TIKU_DRV_AUDIO_ES8389_ENABLE),1)
SRCS += drivers/audio/es8389/tiku_drv_audio_es8389.c
CFLAGS += -DTIKU_DRV_AUDIO_ES8389_ENABLE=1
endif
