#include "../common.h"
#include "../config.h"
#include "../emu.h"
#include "osd.h"
#include <SDL3/SDL.h>
#include <string>

#define LOG(fmt, ...) do { fprintf(stderr, "[MAIN] " fmt "\n", ##__VA_ARGS__); fflush(stderr); } while(0)

// Local patch, not upstream: cross-emulator T-state diff (RELEASE_1_5_0_PLAN.md
// §9.1) needs a way to feed BubiC the same keystrokes as a Bubilator88
// .b88script, without mounting a disk (the argv-based disk mount this file
// used to have is gone from this checkout — see scripts/patches/README.md in
// the Bubilator88 repo). EMU::set_auto_key_list/start_auto_key already exist
// for exactly this (the ASCII "paste text as keystrokes" feature); this only
// wires them to two environment variables and adds a frame-count exit since
// there is no other way to end a headless run of this binary.
//
//   BUBIC_AUTO_KEY_TEXT        text to type, verbatim (CR is Enter; the auto-key
//                              table shifts letters/punctuation automatically)
//   BUBIC_AUTO_KEY_START_FRAME frame to start typing at (default 60)
//   BUBIC_EXIT_AFTER_FRAMES    exit after this many frames (0/unset = never)
static void maybe_start_auto_key(EMU *emu, int frame_count) {
  static bool started = false;
  static bool checked_env = false;
  static std::string text;
  static int start_frame = 60;
  if (started) return;
  if (!checked_env) {
    checked_env = true;
    const char *t = getenv("BUBIC_AUTO_KEY_TEXT");
    if (t) text = t;
    const char *f = getenv("BUBIC_AUTO_KEY_START_FRAME");
    if (f && *f) start_frame = atoi(f);
  }
  if (text.empty() || frame_count < start_frame) return;
  started = true;
  LOG("Auto-key: typing %zu chars at frame %d", text.size(), frame_count);
  emu->set_auto_key_list(const_cast<char *>(text.c_str()), (int)text.size());
  emu->start_auto_key();
}

// Entry point
int main(int argc, char *argv[]) {
  LOG("=== BubiC-8801MA starting ===");
  LOG("argc=%d", argc);
  for (int i = 0; i < argc; i++) {
    LOG("argv[%d]=%s", i, argv[i]);
  }

  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);

  LOG("Calling common_initialize()...");
  common_initialize();
  LOG("common_initialize() done");

  LOG("Application path: %s", tchar_to_char(get_application_path()));
  LOG("Initial current path: %s", tchar_to_char(get_initial_current_path()));

  LOG("Loading config...");
  load_config(create_local_path(_T("BubiC-8801MA.ini")));
  LOG("Config loaded");

  LOG("Creating EMU...");
  EMU *emu = new EMU();
  LOG("EMU created");

  LOG("Getting OSD...");
  OSD *osd = emu->get_osd();
  LOG("OSD obtained: %p", (void*)osd);

  const char *exit_after_env = getenv("BUBIC_EXIT_AFTER_FRAMES");
  const int exit_after_frames = (exit_after_env && *exit_after_env) ? atoi(exit_after_env) : 0;

  LOG("Entering main loop...");
  int frame_count = 0;
  while (!osd->is_terminated()) {
    if (frame_count < 5) {
      LOG("Frame %d: run()", frame_count);
    }
    maybe_start_auto_key(emu, frame_count);
    emu->run();

    if (frame_count < 5) {
      LOG("Frame %d: draw_screen()", frame_count);
    }
    emu->draw_screen();

    if (frame_count < 5) {
      LOG("Frame %d: completed", frame_count);
    }
    frame_count++;
    if (exit_after_frames > 0 && frame_count >= exit_after_frames) {
      LOG("BUBIC_EXIT_AFTER_FRAMES=%d reached, exiting", exit_after_frames);
      break;
    }
  }

  LOG("Main loop exited after %d frames", frame_count);

  LOG("Saving config...");
  save_config(create_local_path(_T("BubiC-8801MA.ini")));
  LOG("Config saved");

  LOG("Deleting EMU...");
  delete emu;
  LOG("EMU deleted");

  LOG("=== BubiC-8801MA exiting normally ===");
  return 0;
}
