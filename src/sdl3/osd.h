/*
    SDL3 OSD Implementation for BubiC-8801MA
*/

#ifndef _SDL3_OSD_H_
#define _SDL3_OSD_H_

#include "../common.h"
#include "../vm/vm.h"
#include <SDL3/SDL.h>

// SDL3 specific definitions
#define OSD_CONSOLE_BLUE 1
#define OSD_CONSOLE_GREEN 2
#define OSD_CONSOLE_RED 4
#define OSD_CONSOLE_INTENSITY 8
#ifndef VK_ESCAPE
#define VK_ESCAPE 0x1B
#define VK_LSHIFT 0xA0
#define VK_RSHIFT 0xA1
#endif

typedef struct bitmap_s {
  bool initialized() { return texture != NULL; }
  int width, height;
  // SDL3 dependent
  SDL_Texture *texture;
  uint32_t *pixels;
  int pitch;
} bitmap_t;

// Dummy font/pen structs for now if printer is not used
typedef struct font_s {
  bool initialized() { return false; }
} font_t;

typedef struct pen_s {
  bool initialized() { return false; }
} pen_t;

class OSD {
private:
  int lock_count;
  bool terminated;

  // Console
  void initialize_console();
  void release_console();

  // Input
  void initialize_input();
  void release_input();
  uint8_t key_status[256];
  uint32_t joy_status[4];
  int32_t mouse_status[8];
  bool key_shift_pressed, key_shift_released;
  bool key_caps_locked;

  // Screen
  void initialize_screen();
  void release_screen();

  SDL_Window *window;
  SDL_Renderer *renderer;
  SDL_Texture *screen_texture;
  scrntype_t *vm_screen_buffer;
  int vm_screen_width, vm_screen_height;
  int window_width, window_height;

  // Sound
  void initialize_sound(int rate, int samples);
  void release_sound();
  SDL_AudioStream *audio_stream;
  int sound_rate;
  int sound_samples;
  float audio_speed_ratio;
  int audio_src_rate;
  int audio_dst_rate;
  int requested_audio_rate;
  int requested_audio_latency_ms;
  bool audio_paused_by_ui;

  SDL_Joystick *joystick;
  SDL_Mutex *vm_mutex;

  // ImGui
  void initialize_imgui();
  void release_imgui();
  void draw_menu();
  bool draw_menu_contents();
  void draw_file_browser();
  void draw_status_bar();

  // FPS tracking
  uint64_t last_fps_tick;
  int frame_count;
  float current_fps;
  uint64_t last_emu_fps_tick;
  uint64_t last_emu_progress_tick;
  int emu_frames_accum;
  float emu_fps;
  bool show_menu;
  bool show_file_browser;
  bool show_save_browser;
  bool imgui_initialized;
  uint64_t last_ui_interaction_tick;
  bool ui_interacting;
  uint32_t ui_interacting_reason;
  bool prev_ui_interacting;
  int applied_vsync_mode;
  int requested_window_w;
  int requested_window_h;
  void select_save_file(int drive, int type);
  void select_file(int drive);
  int pending_blank_type;
  int pending_drive;
  _TCHAR fd1_path[_MAX_PATH];
  _TCHAR fd2_path[_MAX_PATH];
  void draw_save_browser();
  void clear_all_pressed_keys();
  char current_browser_path[_MAX_PATH];

public:
  OSD();
  ~OSD();

  VM_TEMPLATE *vm;
  class EMU *emu;

  void initialize(int rate, int samples);
  void release();
  void power_off();
  void suspend();
  void restore();
  void lock_vm();
  void unlock_vm();
  bool is_vm_locked() { return lock_count != 0; }
  bool is_terminated() const { return terminated; }
  bool is_ui_interacting() const { return ui_interacting; }
  uint32_t get_ui_interacting_reason() const { return ui_interacting_reason; }
  enum UiInteractingReason : uint32_t {
    UI_REASON_NONE = 0,
    UI_REASON_MENU_TREE = 1u << 0,
    UI_REASON_FILE_BROWSER = 1u << 1,
    UI_REASON_SAVE_BROWSER = 1u << 2,
  };
  void force_unlock_vm() {}
  void sleep(uint32_t ms);

  // Console (Stub)
  void open_message_box(const _TCHAR *text);
  void open_console(int width, int height, const char *title);
  void close_console();
  void write_console(const char *buffer, unsigned int length);
  void write_console_char(const char *buffer, unsigned int length);
  void set_console_text_attribute(unsigned short attr);
  unsigned int get_console_code_page();
  int read_console_input(char *buffer, unsigned int length);
  bool is_console_closed();
  void close_debugger_console() {}
  void get_console_cursor_position(int *x, int *y) {
    if (x)
      *x = 0;
    if (y)
      *y = 0;
  }
  void set_console_cursor_position(int x, int y) {}
  void write_console_wchar(const wchar_t *buffer, unsigned int length) {}
  bool is_console_key_pressed(int vk) { return false; }
  // ... add more if needed

  // Input
  void update_input();
  void key_down(int code, bool extended, bool repeat);
  void key_up(int code, bool extended);
  uint8_t *get_key_buffer() { return key_status; }
  void key_lost_focus() {}
  void enable_mouse() {}
  void disable_mouse() {}
  void toggle_mouse() {}
  bool is_mouse_enabled() { return false; }
  int32_t *get_mouse_buffer() { return mouse_status; }
  uint32_t *get_joy_buffer() { return joy_status; }
  // Native key simulation
  void key_down_native(int vk, bool extended);
  void key_up_native(int vk);
  bool now_auto_key = false;

  // Screen
  int draw_screen();
  void set_vm_screen_size(int screen_width, int screen_height, int window_width,
                          int window_height, int window_width_aspect,
                          int window_height_aspect);
  void set_host_window_size(int width, int height, bool window_mode);
  void set_vm_screen_lines(int lines);
  void update_window_scale();
  // Getters
  double get_window_mode_power(int mode) { return 1.0; } // Stub
  int get_window_mode_width(int mode) { return 640; }    // Stub
  int get_window_mode_height(int mode) { return 400; }   // Stub

  int get_vm_window_width() { return window_width; }
  int get_vm_window_height() { return window_height; }
  int get_vm_window_width_aspect() { return window_width; }
  int get_vm_window_height_aspect() { return window_height; }

  // Sound
  void update_sound(int *extra_frames);
  void add_extra_frames(int frames);
  void stop_sound();
  bool reconfigure_sound(int rate, int samples);
  int get_audio_source_rate() const { return audio_src_rate; }
  int get_audio_device_rate() const { return audio_dst_rate; }
  void mute_sound() {}
  void start_record_sound() {}
  void stop_record_sound() {}
  void restart_record_sound() {}
  bool now_record_sound = false;

  // Debugger synchronization
  void start_waiting_in_debugger() {}
  void finish_waiting_in_debugger() {}
  void process_waiting_in_debugger() {}

  // Video (Stub)
  bool now_record_video = false;
  bool start_record_video(int fps) { return false; }
  void stop_record_video() {}
  void restart_record_video() {}
  void capture_screen() {}
  scrntype_t *get_vm_screen_buffer(int y) {
    if (vm_screen_buffer && y >= 0 && y < vm_screen_height) {
      return vm_screen_buffer + y * vm_screen_width;
    }
    return NULL;
  }
  bool screen_skip_line;

  // Printer (Stub)
  void create_bitmap(bitmap_t *bitmap, int width, int height) {}
  void release_bitmap(bitmap_t *bitmap) {}
  void create_font(font_t *font, const _TCHAR *family, int width, int height,
                   int rotate, bool bold, bool italic) {}
  void release_font(font_t *font) {}
  void create_pen(pen_t *pen, int width, uint8_t r, uint8_t g, uint8_t b) {}
  void release_pen(pen_t *pen) {}
  void clear_bitmap(bitmap_t *bitmap, uint8_t r, uint8_t g, uint8_t b) {}
  int get_text_width(bitmap_t *bitmap, font_t *font, const char *text) {
    return 0;
  }
  void draw_text_to_bitmap(bitmap_t *bitmap, font_t *font, int x, int y,
                           const char *text, uint8_t r, uint8_t g, uint8_t b) {}
  void draw_line_to_bitmap(bitmap_t *bitmap, pen_t *pen, int sx, int sy, int ex,
                           int ey) {}
  void draw_rectangle_to_bitmap(bitmap_t *bitmap, int x, int y, int width,
                                int height, uint8_t r, uint8_t g, uint8_t b) {}
  void draw_point_to_bitmap(bitmap_t *bitmap, int x, int y, uint8_t r,
                            uint8_t g, uint8_t b) {}
  void stretch_bitmap(bitmap_t *dest, int dest_x, int dest_y, int dest_width,
                      int dest_height, bitmap_t *source, int source_x,
                      int source_y, int source_width, int source_height) {}
  void write_bitmap_to_file(bitmap_t *bitmap, const char *path) {}

  // SDL specifics
  void handle_event(const SDL_Event &event, bool block_vm_keydown);
  void set_audio_pause_for_ui(bool pause);
};

#endif
