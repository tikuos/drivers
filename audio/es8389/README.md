# ES8389 codec control

The driver identifies the ES8389 at seven-bit I2C address `0x10` and
configures stereo PCM16 I2S at 16 kHz with a 512 kHz BCLK input. The board
supplies no MCLK. ADC slot zero is the left differential microphone input;
slot one is the right differential input. DAC reference slots are disabled.
Microphone PGA gain is 0 dB. DAC setup is muted at -48 dB; the output API
accepts -48..0 dB and mute. It mutes before changing both channel volumes.

The caller initializes I2C, starts BCLK/WS, then opens the codec. Calls must
be serialized in kernel foreground with other I2C users. The driver leaves
the bus open for touch and other devices. Opening validates chip ID and
reads back clock and format registers. An initialization failure attempts
shutdown. An output failure latches a state that requires close/reopen;
the caller disables its amplifier on error. Close mutes both directions
and powers down the codec while clocks still run.

`TIKU_DRV_AUDIO_ES8389_ENABLE=1` registers a probe-only descriptor in class
`other`. Streaming setup is explicit. The implementation is enabled for
ESP32-S31; the `audio-test` port profile selects its source directly.

Register sequences and the 512 kHz/16 kHz coefficient entry derive from
[Espressif esp-audio-dev at 2fe2f0bae674](https://github.com/espressif/esp-audio-dev/blob/2fe2f0bae6748a5e44ec6673d92b2d42e75956f5/esp_codec_dev/device/es8389/es8389.c).
The implementation carries the [Espressif Modified MIT license](LICENSE),
which restricts its use to Espressif products. It remains in this optional
driver repository. Its public header carries Apache-2.0.

Qualification uses `TikuBench/tests/esp32s31/test_audio.c` and
`TikuBench/tests/host/python/test_esp32s31_audio.py`. Hardware checks cover
codec identity/configuration, digital I2S loopback, GPIO amplifier enable,
PSRAM capture and a host-decoded PCM excerpt. These checks do not establish
speaker acoustic output, microphone position, frequency response or noise.
