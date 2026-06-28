// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
//
// amy_platform_stubs.c — no-op implementations of AMY's platform back-end hooks.
//
// AMY's core (api.c) references device-backend functions that are normally
// provided by the platform files we deliberately exclude from libamy (the
// miniaudio desktop driver, the MCU i2s/usb drivers — see cmake/amy.cmake). In
// "library mode" (audio = AMY_AUDIO_IS_NONE, midi = AMY_MIDI_IS_NONE) AMY never
// opens a device, so these are either never called or harmless no-ops:
//
//   * amy_platform_init / run_midi          — called once by amy_start()
//   * stop_midi / amy_platform_deinit       — called once by amy_stop()
//   * amy_update_tasks / amy_render_audio /
//     amy_i2s_write                         — only reachable via amy_update(),
//                                             which we never call (we pull audio
//                                             with amy_simple_fill_buffer())
//   * miniaudio_start / miniaudio_stop      — only called when audio == MINIAUDIO
//
// This mirrors AMY's own godot/src/amy_platform_stubs.c. We never edit AMY itself;
// we satisfy these symbols from our side of the seam instead.

#include <stddef.h>
#include <stdint.h>

void   amy_platform_init(void)   {}
void   amy_platform_deinit(void) {}
void   amy_update_tasks(void)    {}
int16_t *amy_render_audio(void)  { return NULL; }
size_t amy_i2s_write(const uint8_t *buffer, size_t nbytes) { (void) buffer; (void) nbytes; return 0; }

void   run_midi(void)            {}
void   stop_midi(void)           {}

void   miniaudio_start(void)     {}
void   miniaudio_stop(void)      {}
