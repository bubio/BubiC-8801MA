#include "../common.h"
#include "../config.h"
#include "../emu.h"
#include "osd.h"
#include <SDL3/SDL.h>

#define LOG(fmt, ...) do { fprintf(stderr, "[MAIN] " fmt "\n", ##__VA_ARGS__); fflush(stderr); } while(0)

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

  LOG("Entering main loop...");
  int frame_count = 0;
  while (!osd->is_terminated()) {
    if (frame_count < 5) {
      LOG("Frame %d: run()", frame_count);
    }
    emu->run();

    if (frame_count < 5) {
      LOG("Frame %d: draw_screen()", frame_count);
    }
    emu->draw_screen();

    if (frame_count < 5) {
      LOG("Frame %d: completed", frame_count);
    }
    frame_count++;
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
