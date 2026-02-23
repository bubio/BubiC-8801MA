#include "osd.h"
#include "../config.h"
#include "../emu.h"
#include "../fileio.h"
#include "../vm/event.h"
#include <SDL3/SDL.h>
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>

#define OSD_LOG(fmt, ...) do { fprintf(stderr, "[OSD] " fmt "\n", ##__VA_ARGS__); fflush(stderr); } while(0)

namespace fs = std::filesystem;

// Get user home directory
static std::string get_home_directory() {
#ifdef _WIN32
  const char* userprofile = getenv("USERPROFILE");
  if (userprofile && userprofile[0] != '\0') {
    return std::string(userprofile);
  }
  // Fallback
  const char* homedrive = getenv("HOMEDRIVE");
  const char* homepath = getenv("HOMEPATH");
  if (homedrive && homepath) {
    return std::string(homedrive) + std::string(homepath);
  }
  return "C:\\";
#else
  const char* home = getenv("HOME");
  if (home && home[0] != '\0') {
    return std::string(home);
  }
  return "/";
#endif
}

#ifdef _WIN32
// Get list of available drive letters on Windows
static std::vector<char> get_available_drives() {
  std::vector<char> drives;
  DWORD drive_mask = GetLogicalDrives();
  for (char letter = 'A'; letter <= 'Z'; letter++) {
    if (drive_mask & (1 << (letter - 'A'))) {
      drives.push_back(letter);
    }
  }
  return drives;
}

// Convert wide string to UTF-8 (for proper Japanese filename support)
static std::string wstring_to_utf8(const std::wstring& wstr) {
  if (wstr.empty()) return std::string();
  int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
  if (size_needed <= 0) return std::string();
  std::string utf8_str(size_needed - 1, 0);
  WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &utf8_str[0], size_needed, NULL, NULL);
  return utf8_str;
}

// Get UTF-8 encoded path string
static std::string path_to_utf8(const fs::path& p) {
  return wstring_to_utf8(p.wstring());
}
#else
// On non-Windows, just use string() (assumes UTF-8 locale)
static std::string path_to_utf8(const fs::path& p) {
  return p.string();
}
#endif

// Convert UTF-8 path text (UI side) to native TCHAR path (VM/FileIO side).
static const _TCHAR* utf8_path_to_tchar(const char* utf8) {
  if (!utf8) utf8 = "";
#if defined(_WIN32) && defined(_UNICODE) && defined(SUPPORT_TCHAR_TYPE)
  static thread_local wchar_t wbuf[_MAX_PATH];
  int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wbuf, _MAX_PATH);
  if (wlen <= 0) {
    wbuf[0] = L'\0';
  }
  return wbuf;
#elif defined(_WIN32)
  static thread_local char cbuf[_MAX_PATH];
  int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, NULL, 0);
  if (wlen > 0) {
    std::wstring wtmp((size_t)wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &wtmp[0], wlen);
    int alen = WideCharToMultiByte(CP_ACP, 0, wtmp.c_str(), -1, NULL, 0, NULL, NULL);
    if (alen > 0) {
      WideCharToMultiByte(CP_ACP, 0, wtmp.c_str(), -1, cbuf, _MAX_PATH, NULL, NULL);
      return cbuf;
    }
  }
  // Fallback: keep original bytes when input is already ACP/non-UTF8.
  my_strcpy_s(cbuf, _MAX_PATH, utf8);
  return cbuf;
#else
  return char_to_tchar(utf8);
#endif
}

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
static std::string nfd_to_nfc(const std::string& input) {
    CFStringRef cf_input = CFStringCreateWithCString(kCFAllocatorDefault, input.c_str(), kCFStringEncodingUTF8);
    if (!cf_input) return input;
    
    CFMutableStringRef cf_mutable = CFStringCreateMutableCopy(kCFAllocatorDefault, 0, cf_input);
    CFStringNormalize(cf_mutable, kCFStringNormalizationFormC);
    
    char buffer[1024];
    if (CFStringGetCString(cf_mutable, buffer, sizeof(buffer), kCFStringEncodingUTF8)) {
        std::string result(buffer);
        CFRelease(cf_input);
        CFRelease(cf_mutable);
        return result;
    }
    
    CFRelease(cf_input);
    CFRelease(cf_mutable);
    return input;
}
#endif

static const int kHostFrequencyTable[5] = {
    44100, 48000, 55467, 88200, 96000
};

static int normalize_sound_rate_hz(int rate) {
  if (rate >= 0 && rate < 5) {
    rate = kHostFrequencyTable[rate];
  } else {
    switch (rate) {
    case 5:
      rate = 44100;
      break;
    case 6:
      rate = 55467;
      break;
    case 7:
      rate = 96000;
      break;
    default:
      break;
    }
  }
  if (rate <= 0) {
    rate = 55467;
  }
  return rate;
}

static int sanitize_sound_samples_for_rate(int rate, int samples) {
  if (samples <= 0) {
    samples = rate / 100; // 10ms fallback
  }
  if (samples <= 0) {
    samples = 256;
  }
  return samples;
}

static int samples_to_latency_ms(int rate, int samples) {
  if (rate <= 0 || samples <= 0) {
    return 0;
  }
  return (int)((1000LL * (long long)samples + rate / 2) / rate);
}

static int get_disk_names(const char* path, int drv, EMU* emu) {
  if (!emu) return 0;
  FILEIO fio;
  if (!fio.Fopen(utf8_path_to_tchar(path), FILEIO_READ_BINARY)) return 0;

  int count = 0;
  long file_size = fio.FileLength();
  long offset = 0;

  while (offset < file_size && count < MAX_D88_BANKS) {
    // Read 17 bytes for disk name (D88 spec)
    char name_buf[18];
    fio.Fseek(offset, FILEIO_SEEK_SET);
    fio.Fread(name_buf, 17, 1);
    name_buf[17] = '\0';
    
    // Convert to wide char or keep as is depending on platform
    sjis_to_utf8(name_buf, emu->d88_file[drv].disk_name[count], sizeof(emu->d88_file[drv].disk_name[count]));

    fio.Fseek(offset + 0x1c, FILEIO_SEEK_SET);
    uint32_t disk_size = fio.FgetUint32_LE();
    if (disk_size == 0) break;
    
    offset += disk_size;
    count++;
  }
  fio.Fclose();
  emu->d88_file[drv].bank_num = count;
  my_tcscpy_s(emu->d88_file[drv].path, _MAX_PATH, utf8_path_to_tchar(path));
  return count;
}

OSD::OSD() {
  lock_count = 0;
  terminated = false;
  vm = NULL;
  emu = NULL;
  window = NULL;
  renderer = NULL;
  screen_texture = NULL;
  audio_stream = NULL;
  audio_speed_ratio = 1.0f;
  audio_src_rate = 0;
  audio_dst_rate = 0;
  requested_audio_rate = 0;
  requested_audio_latency_ms = 0;
  audio_paused_by_ui = false;
  joystick = NULL;
  vm_mutex = SDL_CreateMutex();
  last_fps_tick = 0;
  frame_count = 0;
  current_fps = 0.0f;
  last_emu_fps_tick = 0;
  last_emu_progress_tick = 0;
  emu_frames_accum = 0;
  emu_fps = 0.0f;
  key_shift_pressed = false;
  key_shift_released = false;
  key_caps_locked = false;
  show_menu = true;
  show_file_browser = false;
  show_save_browser = false;
  imgui_initialized = false;
  ui_interacting = false;
  ui_interacting_reason = UI_REASON_NONE;
  prev_ui_interacting = false;
  applied_vsync_mode = -1;
  requested_window_w = 0;
  requested_window_h = 0;
  last_ui_interaction_tick = SDL_GetTicks();
  fd1_path[0] = _T('\0');
  fd2_path[0] = _T('\0');
  pending_blank_type = 0;
  // Use last browser path from config if available, otherwise use home directory
  if (config.last_browser_path[0] != _T('\0')) {
    snprintf(current_browser_path, _MAX_PATH, "%s", tchar_to_char(config.last_browser_path));
  } else {
    std::string home_dir = get_home_directory();
    snprintf(current_browser_path, _MAX_PATH, "%s", home_dir.c_str());
  }
  vm_screen_buffer = NULL;
  vm_screen_width = 0;
  vm_screen_height = 0;
  memset(key_status, 0, sizeof(key_status));
  memset(joy_status, 0, sizeof(joy_status));
  memset(mouse_status, 0, sizeof(mouse_status));
}

OSD::~OSD() {
  release();
  if (vm_mutex) {
    SDL_DestroyMutex(vm_mutex);
    vm_mutex = NULL;
  }
}

static void add_recent_disk(const _TCHAR* path, int drv) {
  if (!path || path[0] == _T('\0') || drv >= USE_FLOPPY_DISK) return;
  
  // 入力ポインタが自身の配列内を指している場合に備え、一時バッファにコピー
  _TCHAR new_path[_MAX_PATH];
  my_tcscpy_s(new_path, _MAX_PATH, path);

  // 一旦、既存の重複をすべて詰めて消去する（古いバグで混入した重複も掃除）
  int write_idx = 0;
  for (int read_idx = 0; read_idx < MAX_HISTORY; read_idx++) {
    if (config.recent_floppy_disk_path[drv][read_idx][0] == _T('\0')) break;
    
    // 同じパスでなければ残す
    if (_tcsicmp(config.recent_floppy_disk_path[drv][read_idx], new_path) != 0) {
      if (write_idx != read_idx) {
        my_tcscpy_s(config.recent_floppy_disk_path[drv][write_idx], _MAX_PATH, config.recent_floppy_disk_path[drv][read_idx]);
      }
      write_idx++;
    }
  }
  // 残りの部分を空にする
  for (int i = write_idx; i < MAX_HISTORY; i++) {
    config.recent_floppy_disk_path[drv][i][0] = _T('\0');
  }

  // 全体を一つ後ろにずらして、先頭に新しいパスを入れる
  for (int i = MAX_HISTORY - 1; i > 0; i--) {
    my_tcscpy_s(config.recent_floppy_disk_path[drv][i], _MAX_PATH, config.recent_floppy_disk_path[drv][i-1]);
  }
  my_tcscpy_s(config.recent_floppy_disk_path[drv][0], _MAX_PATH, new_path);
}

void OSD::initialize(int rate, int samples) {
  OSD_LOG("initialize() called with rate=%d, samples=%d", rate, samples);

  OSD_LOG("Calling SDL_Init()...");
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS |
                SDL_INIT_JOYSTICK)) {
    OSD_LOG("SDL_Init FAILED: %s", SDL_GetError());
    return;
  }
  OSD_LOG("SDL_Init succeeded");

  // Open first available joystick
  OSD_LOG("Checking joysticks...");
  int num_joysticks;
  SDL_JoystickID *joysticks = SDL_GetJoysticks(&num_joysticks);
  if (joysticks) {
    OSD_LOG("Found %d joysticks", num_joysticks);
    if (num_joysticks > 0) {
      joystick = SDL_OpenJoystick(joysticks[0]);
    }
    SDL_free(joysticks);
  }

  // Base VM size is 640x400.
  // UI heights (Menu ~20px, Status ~24px) are constant regardless of VM scale.
  static const float scales[] = { 1.0f, 1.5f, 2.0f, 2.5f, 3.0f };
  int scale_idx = config.window_scale_idx;
  if (scale_idx < 0 || scale_idx > 4) scale_idx = 2;
  float scale = scales[scale_idx];

  window_width = (int)(640 * scale);
  window_height = (int)(400 * scale) + 20 + 24; // Approximation for initial window

  OSD_LOG("Creating window %dx%d...", window_width, window_height);
  std::string title = "BubiC-8801MA v" + std::string(APP_VERSION_STRING);
  window = SDL_CreateWindow(title.c_str(), window_width, window_height,
                            SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (!window) {
    OSD_LOG("SDL_CreateWindow FAILED: %s", SDL_GetError());
    return;
  }
  OSD_LOG("Window created: %p", (void*)window);

  OSD_LOG("Creating renderer...");
  renderer = SDL_CreateRenderer(window, NULL);
  if (!renderer) {
    OSD_LOG("SDL_CreateRenderer FAILED: %s", SDL_GetError());
    return;
  }
  OSD_LOG("Renderer created: %p", (void*)renderer);

  applied_vsync_mode = config.full_speed ? 0 : 1;
  SDL_SetRenderVSync(renderer, applied_vsync_mode);

  OSD_LOG("Calling initialize_sound()...");
  initialize_sound(rate, samples);
  OSD_LOG("initialize_sound() done");

  OSD_LOG("Calling initialize_imgui()...");
  initialize_imgui();
  OSD_LOG("initialize_imgui() done, imgui_initialized=%d", imgui_initialized);

  if (vm_screen_buffer) {
    set_vm_screen_size(640, 400, window_width, window_height, window_width, window_height);
  }
  OSD_LOG("initialize() completed");
}

void OSD::release() {
  release_sound();
  release_imgui();
  if (joystick) {
    SDL_CloseJoystick(joystick);
    joystick = NULL;
  }
  if (vm_screen_buffer) {
    free(vm_screen_buffer);
    vm_screen_buffer = NULL;
  }
  if (screen_texture) {
    SDL_DestroyTexture(screen_texture);
    screen_texture = NULL;
  }
  if (renderer) {
    SDL_DestroyRenderer(renderer);
    renderer = NULL;
  }
  if (window) {
    SDL_DestroyWindow(window);
    window = NULL;
  }
  SDL_Quit();
}

void OSD::initialize_sound(int rate, int samples) {
  rate = normalize_sound_rate_hz(rate);
  samples = sanitize_sound_samples_for_rate(rate, samples);
  requested_audio_rate = rate;
  requested_audio_latency_ms = samples_to_latency_ms(rate, samples);
  if (!reconfigure_sound(rate, samples)) {
    fprintf(stderr, "OSD: initialize_sound failed (%d Hz, %d samples): %s\n",
            rate, samples, SDL_GetError());
    fflush(stderr);
  }
}

void OSD::release_sound() {
  if (audio_stream) {
    SDL_DestroyAudioStream(audio_stream);
    audio_stream = NULL;
  }
  audio_src_rate = 0;
  audio_dst_rate = 0;
  audio_paused_by_ui = false;
}

void OSD::update_sound(int *extra_frames) {
  if (!audio_stream || !vm) {
    if (extra_frames) *extra_frames = 0;
    return;
  }
  if (audio_paused_by_ui) {
    if (extra_frames) *extra_frames = 0;
    return;
  }

  if (SDL_AudioStreamDevicePaused(audio_stream)) {
    if (!SDL_ResumeAudioStreamDevice(audio_stream)) {
      if (extra_frames) {
        *extra_frames = 0;
      }
      return;
    }
  }

  // Keep audio playback speed aligned with CPU multiplier.
  float desired_ratio = 1.0f;
  if (!config.full_speed) {
    int mul = config.cpu_power;
    if (mul < 1) mul = 1;
    if (mul > 16) mul = 16;
    desired_ratio = (float)mul;
  }

  if (audio_speed_ratio != desired_ratio) {
    if (SDL_SetAudioStreamFrequencyRatio(audio_stream, desired_ratio)) {
      // Clear backlog to prevent long stalls/glitches after speed changes.
      SDL_ClearAudioStream(audio_stream);
      audio_speed_ratio = desired_ratio;
    }
  }

  const int bytes_per_sample = 2 * (int)sizeof(uint16_t);
  const int block_bytes = sound_samples * bytes_per_sample;
  const int min_queued_bytes = block_bytes;
  const int target_queued_bytes = block_bytes * 2;
  const int max_queued_bytes = block_bytes * 3;
  int queued = SDL_GetAudioStreamQueued(audio_stream);
  if (queued < 0) {
    if (extra_frames) {
      *extra_frames = 0;
    }
    return;
  }
  int total_extra_frames = 0;

  // Keep sample timing stable on SDL3 path and let frequency-ratio handle speed.
  ((VM *)vm)->pc88event->set_sample_multi(0x1000);

  // If queue is already large enough, avoid generating more this turn.
  if (queued >= max_queued_bytes) {
    if (extra_frames) {
      *extra_frames = 0;
    }
    return;
  }

  auto push_audio_block = [&](int *produced_frames) -> bool {
    int local_frames = 0;
    uint16_t *buffer = vm->create_sound(&local_frames);
    if (!buffer) {
      return false;
    }
    if (!SDL_PutAudioStreamData(audio_stream, buffer, block_bytes)) {
      return false;
    }
    if (produced_frames) {
      *produced_frames = local_frames;
    }
    return true;
  };

  int refill_count = 0;
  while (queued < target_queued_bytes && refill_count < 3) {
    int produced = 0;
    if (!push_audio_block(&produced)) {
      break;
    }
    total_extra_frames += produced;
    queued = SDL_GetAudioStreamQueued(audio_stream);
    if (queued >= max_queued_bytes) {
      break;
    }
    refill_count++;
  }

  // Ensure minimum buffering at startup or after device hiccups.
  while (queued < min_queued_bytes && refill_count < 3) {
    int produced = 0;
    if (!push_audio_block(&produced)) {
      break;
    }
    total_extra_frames += produced;
    queued = SDL_GetAudioStreamQueued(audio_stream);
    refill_count++;
  }

  if (extra_frames) {
    *extra_frames = total_extra_frames;
  }
}

void OSD::stop_sound() {
  if (audio_stream) {
    SDL_ClearAudioStream(audio_stream);
  }
}

bool OSD::reconfigure_sound(int rate, int samples) {
  rate = normalize_sound_rate_hz(rate);
  samples = sanitize_sound_samples_for_rate(rate, samples);

  SDL_AudioSpec spec = {};
  spec.channels = 2;
  spec.format = SDL_AUDIO_S16;
  spec.freq = rate;

  SDL_AudioStream *new_stream =
      SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL,
                                NULL);
  if (!new_stream) {
    fprintf(stderr, "OSD: SDL_OpenAudioDeviceStream failed: %s\n",
            SDL_GetError());
    fflush(stderr);
    return false;
  }

  (void)SDL_SetAudioStreamFrequencyRatio(new_stream, 1.0f);
  if (!SDL_ResumeAudioStreamDevice(new_stream)) {
    SDL_DestroyAudioStream(new_stream);
    return false;
  }

  SDL_AudioSpec src_spec = {};
  SDL_AudioSpec dst_spec = {};
  int src_rate = rate;
  int dst_rate = rate;
  if (SDL_GetAudioStreamFormat(new_stream, &src_spec, &dst_spec)) {
    if (src_spec.freq > 0) {
      src_rate = src_spec.freq;
    }
    if (dst_spec.freq > 0) {
      dst_rate = dst_spec.freq;
    }
  }

  SDL_AudioStream *old_stream = audio_stream;
  audio_stream = new_stream;
  sound_rate = rate;
  sound_samples = samples;
  audio_speed_ratio = 1.0f;
  audio_src_rate = src_rate;
  audio_dst_rate = dst_rate;

  if (old_stream) {
    SDL_DestroyAudioStream(old_stream);
  }

  // Keep UI pause semantics across stream reconfiguration.
  if (audio_paused_by_ui) {
    (void)SDL_PauseAudioStreamDevice(audio_stream);
  }
  return true;
}

void OSD::set_audio_pause_for_ui(bool pause) {
  if (audio_paused_by_ui == pause) {
    return;
  }
  audio_paused_by_ui = pause;
  if (!audio_stream) {
    return;
  }
  if (pause) {
    (void)SDL_PauseAudioStreamDevice(audio_stream);
  } else {
    SDL_ClearAudioStream(audio_stream);
    (void)SDL_ResumeAudioStreamDevice(audio_stream);
  }
}

void OSD::update_input() {
  // Decay temporary key-hold frames (KEY_KEEP_FRAMES semantics).
  for (int i = 1; i < 256; i++) {
    if (key_status[i] & 0x7f) {
      key_status[i] = (key_status[i] & 0x80) | ((key_status[i] & 0x7f) - 1);
      if (key_status[i] == 0 && vm) {
        vm->key_up(i);
      }
    }
  }

  SDL_Event event;
  ImGuiIO &io = ImGui::GetIO();
  while (SDL_PollEvent(&event)) {
    ImGui_ImplSDL3_ProcessEvent(&event);
    if (event.type == SDL_EVENT_QUIT) {
      terminated = true;
    }
    const bool is_key_event =
        (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP);
    const bool block_vm_keydown =
        (show_file_browser || show_save_browser || ui_interacting);

    // Let ImGui capture non-key events, but keep key events flowing for
    // key-state synchronization (especially KEY_UP).
    if ((io.WantCaptureMouse || io.WantCaptureKeyboard) && !is_key_event) {
      continue;
    }
    handle_event(event, block_vm_keydown);
  }
}

void OSD::handle_event(const SDL_Event &event, bool block_vm_keydown) {
  if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
    bool down = (event.type == SDL_EVENT_KEY_DOWN);
    int vk = 0;
    switch (event.key.scancode) {
    case SDL_SCANCODE_ESCAPE: vk = 0x1B; break;
    case SDL_SCANCODE_RETURN: vk = 0x0D; break;
    case SDL_SCANCODE_SPACE: vk = 0x20; break;
    case SDL_SCANCODE_BACKSPACE: vk = 0x08; break;
    case SDL_SCANCODE_TAB: vk = 0x09; break;
    case SDL_SCANCODE_INSERT: vk = 0x2D; break;
    case SDL_SCANCODE_DELETE: vk = 0x2E; break;
    case SDL_SCANCODE_PAGEUP: vk = 0x70; break; // Map to correct F key or VK
    case SDL_SCANCODE_PAGEDOWN: vk = 0x71; break;
    case SDL_SCANCODE_END: vk = 0x23; break;
    case SDL_SCANCODE_HOME: vk = 0x24; break;
    case SDL_SCANCODE_LEFT: vk = config.cursor_as_numpad ? 0x64 : 0x25; break;
    case SDL_SCANCODE_UP: vk = config.cursor_as_numpad ? 0x68 : 0x26; break;
    case SDL_SCANCODE_RIGHT: vk = config.cursor_as_numpad ? 0x66 : 0x27; break;
    case SDL_SCANCODE_DOWN: vk = config.cursor_as_numpad ? 0x62 : 0x28; break;
    case SDL_SCANCODE_0: vk = config.digit_as_numpad ? 0x60 : '0'; break;
    case SDL_SCANCODE_1: vk = config.digit_as_numpad ? 0x61 : '1'; break;
    case SDL_SCANCODE_2: vk = config.digit_as_numpad ? 0x62 : '2'; break;
    case SDL_SCANCODE_3: vk = config.digit_as_numpad ? 0x63 : '3'; break;
    case SDL_SCANCODE_4: vk = config.digit_as_numpad ? 0x64 : '4'; break;
    case SDL_SCANCODE_5: vk = config.digit_as_numpad ? 0x65 : '5'; break;
    case SDL_SCANCODE_6: vk = config.digit_as_numpad ? 0x66 : '6'; break;
    case SDL_SCANCODE_7: vk = config.digit_as_numpad ? 0x67 : '7'; break;
    case SDL_SCANCODE_8: vk = config.digit_as_numpad ? 0x68 : '8'; break;
    case SDL_SCANCODE_9: vk = config.digit_as_numpad ? 0x69 : '9'; break;
    case SDL_SCANCODE_KP_0: vk = 0x60; break;
    case SDL_SCANCODE_KP_1: vk = 0x61; break;
    case SDL_SCANCODE_KP_2: vk = 0x62; break;
    case SDL_SCANCODE_KP_3: vk = 0x63; break;
    case SDL_SCANCODE_KP_4: vk = 0x64; break;
    case SDL_SCANCODE_KP_5: vk = 0x65; break;
    case SDL_SCANCODE_KP_6: vk = 0x66; break;
    case SDL_SCANCODE_KP_7: vk = 0x67; break;
    case SDL_SCANCODE_KP_8: vk = 0x68; break;
    case SDL_SCANCODE_KP_9: vk = 0x69; break;
    case SDL_SCANCODE_KP_MULTIPLY: vk = 0x6A; break;
    case SDL_SCANCODE_KP_PLUS: vk = 0x6B; break;
    case SDL_SCANCODE_KP_MINUS: vk = 0x6D; break;
    case SDL_SCANCODE_KP_PERIOD: vk = 0x6E; break;
    case SDL_SCANCODE_KP_DECIMAL: vk = 0x6E; break;
    case SDL_SCANCODE_KP_DIVIDE: vk = 0x6F; break;
    case SDL_SCANCODE_KP_ENTER: vk = 0x0D; break;
    case SDL_SCANCODE_KP_EQUALS: vk = 0x92; break;
    case SDL_SCANCODE_A: vk = 'A'; break;
    case SDL_SCANCODE_B: vk = 'B'; break;
    case SDL_SCANCODE_C: vk = 'C'; break;
    case SDL_SCANCODE_D: vk = 'D'; break;
    case SDL_SCANCODE_E: vk = 'E'; break;
    case SDL_SCANCODE_F: vk = 'F'; break;
    case SDL_SCANCODE_G: vk = 'G'; break;
    case SDL_SCANCODE_H: vk = 'H'; break;
    case SDL_SCANCODE_I: vk = 'I'; break;
    case SDL_SCANCODE_J: vk = 'J'; break;
    case SDL_SCANCODE_K: vk = 'K'; break;
    case SDL_SCANCODE_L: vk = 'L'; break;
    case SDL_SCANCODE_M: vk = 'M'; break;
    case SDL_SCANCODE_N: vk = 'N'; break;
    case SDL_SCANCODE_O: vk = 'O'; break;
    case SDL_SCANCODE_P: vk = 'P'; break;
    case SDL_SCANCODE_Q: vk = 'Q'; break;
    case SDL_SCANCODE_R: vk = 'R'; break;
    case SDL_SCANCODE_S: vk = 'S'; break;
    case SDL_SCANCODE_T: vk = 'T'; break;
    case SDL_SCANCODE_U: vk = 'U'; break;
    case SDL_SCANCODE_V: vk = 'V'; break;
    case SDL_SCANCODE_W: vk = 'W'; break;
    case SDL_SCANCODE_X: vk = 'X'; break;
    case SDL_SCANCODE_Y: vk = 'Y'; break;
    case SDL_SCANCODE_Z: vk = 'Z'; break;
    case SDL_SCANCODE_F1: vk = 0x70; break;
    case SDL_SCANCODE_F2: vk = 0x71; break;
    case SDL_SCANCODE_F3: vk = 0x72; break;
    case SDL_SCANCODE_F4: vk = 0x73; break;
    case SDL_SCANCODE_F5: vk = 0x74; break;
    case SDL_SCANCODE_F6: vk = 0x75; break;
    case SDL_SCANCODE_F7: vk = 0x76; break;
    case SDL_SCANCODE_F8: vk = 0x77; break;
    case SDL_SCANCODE_F9: vk = 0x78; break;
    case SDL_SCANCODE_F10: vk = 0x79; break;
    case SDL_SCANCODE_F11: vk = 0x7A; break;
    case SDL_SCANCODE_F12:
      if (down) show_menu = !show_menu;
      vk = 0x7B;
      break;
    case SDL_SCANCODE_LSHIFT: vk = 0x10; break;
    case SDL_SCANCODE_RSHIFT: vk = 0x10; break;
    case SDL_SCANCODE_LCTRL: vk = 0x11; break;
    case SDL_SCANCODE_RCTRL: vk = 0x11; break;
    case SDL_SCANCODE_LALT: vk = 0x12; break;
    case SDL_SCANCODE_RALT: vk = 0x12; break;
    case SDL_SCANCODE_SEMICOLON: vk = 0xBA; break;
    case SDL_SCANCODE_EQUALS: vk = 0xBB; break;
    case SDL_SCANCODE_COMMA: vk = 0xBC; break;
    case SDL_SCANCODE_MINUS: vk = 0xBD; break;
    case SDL_SCANCODE_PERIOD: vk = 0xBE; break;
    case SDL_SCANCODE_SLASH: vk = 0xBF; break;
    case SDL_SCANCODE_GRAVE: vk = 0xC0; break;
    case SDL_SCANCODE_LEFTBRACKET: vk = 0xDB; break;
    case SDL_SCANCODE_BACKSLASH: vk = 0xDC; break;
    case SDL_SCANCODE_RIGHTBRACKET: vk = 0xDD; break;
    case SDL_SCANCODE_APOSTROPHE: vk = 0xDE; break;
    default: break;
    }
    if (vk > 0 && vk < 256) {
      const bool was_down = ((key_status[vk] & 0x80) != 0);
      if (vm) {
        if (down) {
          key_status[vk] = 0x80;
          if (!block_vm_keydown && (!was_down || event.key.repeat != 0)) {
            vm->key_down(vk, event.key.repeat != 0);
          }
        } else {
          if (key_status[vk] == 0) {
            return;
          }
          if ((key_status[vk] &= 0x7f) != 0) {
            return;
          }
          vm->key_up(vk);
        }
      } else {
        if (down) {
          key_status[vk] = 0x80;
        } else {
          key_status[vk] &= 0x7f;
        }
      }
    }
  } else if (event.type == SDL_EVENT_JOYSTICK_AXIS_MOTION) {
    if (event.jaxis.axis < 2) {
      // Map axis to digital directions for now (common in retropc emus)
      int stick = 0; // Stick 0
      if (event.jaxis.axis == 0) { // X-axis
        if (event.jaxis.value < -16384) joy_status[stick] |= 0x04; // Left
        else joy_status[stick] &= ~0x04;
        if (event.jaxis.value > 16384) joy_status[stick] |= 0x08; // Right
        else joy_status[stick] &= ~0x08;
      } else if (event.jaxis.axis == 1) { // Y-axis
        if (event.jaxis.value < -16384) joy_status[stick] |= 0x01; // Up
        else joy_status[stick] &= ~0x01;
        if (event.jaxis.value > 16384) joy_status[stick] |= 0x02; // Down
        else joy_status[stick] &= ~0x02;
      }
    }
  } else if (event.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN || event.type == SDL_EVENT_JOYSTICK_BUTTON_UP) {
    int stick = 0;
    int button = event.jbutton.button;
    if (button < 12) { // Map buttons 0-11 to bits 4-15
      if (event.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN) {
        joy_status[stick] |= (1 << (button + 4));
      } else {
        joy_status[stick] &= ~(1 << (button + 4));
      }
    }
  } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
    mouse_status[0] += (int32_t)event.motion.xrel;
    mouse_status[1] += (int32_t)event.motion.yrel;
    last_ui_interaction_tick = SDL_GetTicks(); // Reset UI visibility timer
  } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
    bool down = (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
    if (event.button.button == SDL_BUTTON_LEFT) mouse_status[2] = down ? 1 : 0;
    if (event.button.button == SDL_BUTTON_RIGHT) mouse_status[3] = down ? 1 : 0;
    last_ui_interaction_tick = SDL_GetTicks(); // Reset UI visibility timer
  }
}

void OSD::clear_all_pressed_keys() {
  if (!vm) {
    memset(key_status, 0, sizeof(key_status));
    return;
  }
  for (int code = 1; code < 256; code++) {
    if (key_status[code] & 0x80) {
      vm->key_up(code);
    }
    key_status[code] = 0;
  }
}

void OSD::key_down(int code, bool extended, bool repeat) {
  if (code > 0 && code < 256) {
    bool was_down = ((key_status[code] & 0x80) != 0);
    key_status[code] = 0x80;
    if (vm && (!was_down || repeat)) {
      vm->key_down(code, repeat);
    }
  }
}

void OSD::key_up(int code, bool extended) {
  if (code > 0 && code < 256) {
    if (key_status[code] == 0) {
      return;
    }
    if ((key_status[code] &= 0x7f) != 0) {
      return;
    }
    if (vm) {
      vm->key_up(code);
    }
  }
}

void OSD::key_down_native(int code, bool extended) {
  if (code > 0 && code < 256) {
    key_status[code] = 0x80;
    if (vm) {
      vm->key_down(code, false);
    }
  }
}

void OSD::key_up_native(int code) {
  if (code > 0 && code < 256) {
    if (key_status[code] == 0) {
      return;
    }
    if ((key_status[code] &= 0x7f) != 0) {
      return;
    }
    if (vm) {
      vm->key_up(code);
    }
  }
}

void OSD::set_vm_screen_size(int screen_width, int screen_height,
                             int window_width, int window_height,
                             int window_width_aspect,
                             int window_height_aspect) {
  if (vm_screen_width == screen_width && vm_screen_height == screen_height &&
      vm_screen_buffer != NULL && screen_texture != NULL) {
    return;
  }

  if (vm_screen_width != screen_width || vm_screen_height != screen_height || vm_screen_buffer == NULL) {
    if (vm_screen_buffer) {
      free(vm_screen_buffer);
    }
    vm_screen_buffer =
        (scrntype_t *)malloc(screen_width * screen_height * sizeof(scrntype_t));
    vm_screen_width = screen_width;
    vm_screen_height = screen_height;
    memset(vm_screen_buffer, 0,
           screen_width * screen_height * sizeof(scrntype_t));
  }

  if (renderer) {
    if (screen_texture) {
      SDL_DestroyTexture(screen_texture);
    }
    screen_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888,
                                       SDL_TEXTUREACCESS_STREAMING, screen_width,
                                       screen_height);
  }
}

void OSD::set_host_window_size(int width, int height, bool window_mode) {
  // Set window size
}

void OSD::set_vm_screen_lines(int lines) {
  // PC-8801 always expects 400 lines output (either native or doubled 200 lines)
  int height = (lines <= 200) ? 400 : lines;
  set_vm_screen_size(640, height, 640, height, 640, height);
}

void OSD::update_window_scale() {
  if (!window) return;
  static const float scales[] = { 1.0f, 1.5f, 2.0f, 2.5f, 3.0f };
  int idx = config.window_scale_idx;
  if (idx < 0 || idx > 4) idx = 2;
  float scale = scales[idx];
  
  // Get current UI heights (they are not scaled)
  float menu_height = show_menu ? 20.0f : 0.0f; // Approx before frame
  float status_height = 24.0f;

  window_width = (int)(640 * scale);
  window_height = (int)(400 * scale + menu_height + status_height);
  
  SDL_SetWindowSize(window, window_width, window_height);
}

int OSD::draw_screen() {
  if (!renderer || !screen_texture || !vm_screen_buffer)
    return 0;

  uint64_t current_tick = SDL_GetTicks();

  // Update FPS calculation

    frame_count++;
    if (current_tick - last_fps_tick >= 1000) {
      current_fps = (float)frame_count * 1000.0f / (float)(current_tick - last_fps_tick);
      last_fps_tick = current_tick;
      frame_count = 0;
    }
  



  void *pixels;

  int pitch;

  if (SDL_LockTexture(screen_texture, NULL, &pixels, &pitch)) {

    for (int y = 0; y < vm_screen_height; y++) {

      memcpy((uint8_t *)pixels + y * pitch,

             vm_screen_buffer + y * vm_screen_width,

             vm_screen_width * sizeof(scrntype_t));

    }

    SDL_UnlockTexture(screen_texture);

  }



  // Speed control via VSync. Apply only when mode changes.
  const int desired_vsync_mode = config.full_speed ? 0 : 1;
  if (applied_vsync_mode != desired_vsync_mode) {
    SDL_SetRenderVSync(renderer, desired_vsync_mode);
    applied_vsync_mode = desired_vsync_mode;
  }



  // Background clear

  SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);

  SDL_RenderClear(renderer);

  

    if (!imgui_initialized) {
      static int warn_count = 0;
      if (warn_count < 5) {
        OSD_LOG("draw_screen called but imgui not initialized! (warn #%d)", warn_count);
        warn_count++;
      }
      SDL_RenderPresent(renderer);
      return 0;
    }

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    
      bool is_fullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN);
    
    ImGuiIO &io = ImGui::GetIO();
    float fb_scale_x = io.DisplayFramebufferScale.x;
    float fb_scale_y = io.DisplayFramebufferScale.y;
    if (fb_scale_x <= 0.0f) fb_scale_x = 1.0f;
    if (fb_scale_y <= 0.0f) fb_scale_y = 1.0f;
    SDL_SetRenderScale(renderer, fb_scale_x, fb_scale_y);
  
    // Determine if UI should be visible
    bool ui_visible = !is_fullscreen || 
                      (current_tick - last_ui_interaction_tick < 5000) || 
                      ImGui::IsPopupOpen((const char*)NULL, ImGuiPopupFlags_AnyPopupId) ||
                      show_file_browser || show_save_browser ||
                      io.WantCaptureKeyboard;
  
      if (ui_visible) {
        last_ui_interaction_tick = (ImGui::IsAnyItemActive() || ImGui::IsAnyItemHovered()) ? current_tick : last_ui_interaction_tick;
        SDL_ShowCursor();
      } else {
        if (is_fullscreen) SDL_HideCursor();
      }
        float menu_height = 0;
    bool menu_tree_open = false;
    if (show_menu && ui_visible && ImGui::BeginMainMenuBar()) {
      menu_tree_open = draw_menu_contents();
      menu_height = ImGui::GetFrameHeight();
      ImGui::EndMainMenuBar();
    }
  
    // Draw Status Bar at the bottom
    const float status_height = 24.0f;
    if (ui_visible) {
      draw_status_bar();
    }
  
    // VM Screen Scaling
    static const float scales[] = { 1.0f, 1.5f, 2.0f, 2.5f, 3.0f };
    int idx = config.window_scale_idx;
    if (idx < 0 || idx > 4) idx = 2;
    float scale = scales[idx];
  
      int current_w, current_h;
      SDL_GetWindowSize(window, &current_w, &current_h);
        if (is_fullscreen) {
      // フルスクリーン時は画面全体を使って最大拡大（UIは重畳するため高さを引かない）
      float max_scale_w = (float)current_w / 640.0f;
      float max_scale_h = (float)current_h / 400.0f;
      scale = (max_scale_w < max_scale_h) ? max_scale_w : max_scale_h;
    } else {
      // ウィンドウ時はUIの高さを確保してリサイズ
      int target_h_window = (int)(400.0f * scale + menu_height + status_height);
      int target_w_window = (int)(640.0f * scale);
      if (requested_window_w != target_w_window ||
          requested_window_h != target_h_window) {
        SDL_SetWindowSize(window, target_w_window, target_h_window);
        requested_window_w = target_w_window;
        requested_window_h = target_h_window;
        window_width = target_w_window;
        window_height = target_h_window;
      }

      // Follow actual client size (points) so Host->Screen resize is reflected reliably on HiDPI.
      SDL_GetWindowSize(window, &current_w, &current_h);
      float content_h = (float)current_h - menu_height - status_height;
      if (content_h < 1.0f) content_h = 1.0f;
      float max_scale_w = (float)current_w / 640.0f;
      float max_scale_h = content_h / 400.0f;
      scale = (max_scale_w < max_scale_h) ? max_scale_w : max_scale_h;
      if (scale < 0.1f) scale = 0.1f;
    }
  
    // VMの画面を描画（フルスクリーン時は画面中央に配置）
    float draw_w = 640.0f * scale;
    float draw_h = 400.0f * scale;
    float draw_x = ((float)current_w - draw_w) / 2.0f;
    float draw_y;
    if (is_fullscreen) {
      draw_y = ((float)current_h - draw_h) / 2.0f;
    } else {
      float content_h = (float)current_h - menu_height - status_height;
      if (content_h < draw_h) {
        draw_y = menu_height;
      } else {
        draw_y = menu_height + (content_h - draw_h) / 2.0f;
      }
    }
  
    SDL_FRect dest_rect = { draw_x, draw_y, draw_w, draw_h };
    SDL_RenderTexture(renderer, screen_texture, NULL, &dest_rect);
  
    draw_file_browser();
    draw_save_browser();
  
    // Pause emulation only when settings UI is actually open.
    // Hovering the menu bar should not pause the VM.
    uint32_t next_reason = UI_REASON_NONE;
    if (menu_tree_open) {
      next_reason |= UI_REASON_MENU_TREE;
    }
    if (show_file_browser) {
      next_reason |= UI_REASON_FILE_BROWSER;
    }
    if (show_save_browser) {
      next_reason |= UI_REASON_SAVE_BROWSER;
    }
    const bool next_ui_interacting = (next_reason != UI_REASON_NONE);
    if (!prev_ui_interacting && next_ui_interacting) {
      clear_all_pressed_keys();
    }
    if (prev_ui_interacting != next_ui_interacting) {
      set_audio_pause_for_ui(next_ui_interacting);
    }
    ui_interacting = next_ui_interacting;
    ui_interacting_reason = next_reason;
    prev_ui_interacting = next_ui_interacting;
  
    


  ImGui::Render();

  ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);



  SDL_RenderPresent(renderer);



  return 0;

}





// Methods
void OSD::power_off() { terminated = true; }
void OSD::suspend() {}
void OSD::restore() {}

void OSD::lock_vm() {
  if (vm_mutex) {
    SDL_LockMutex(vm_mutex);
  }
  lock_count++;
}

void OSD::unlock_vm() {
  lock_count--;
  if (vm_mutex) {
    SDL_UnlockMutex(vm_mutex);
  }
}

void OSD::draw_status_bar() {
  ImGuiViewport* viewport = ImGui::GetMainViewport();
  const float status_height = 24.0f;
  ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - status_height));
  ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, status_height));
  
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | 
                           ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse | 
                           ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | 
                           ImGuiWindowFlags_NoBringToFrontOnFocus;
  
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 2));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
  
  if (ImGui::Begin("StatusBar", NULL, flags)) {
    // Disk Access Lamps
    uint32_t accessed = 0;
    if (vm) {
      accessed = ((VM*)vm)->is_floppy_disk_accessed();
    }
    
    for (int i = 1; i >= 0; i--) {
      if (i != 1) {
        ImGui::SameLine(0.0f, 10.0f);
      }
      bool active = (accessed & (1 << i));
      ImGui::Text("FD%d:", i + 1);
      ImVec2 text_min = ImGui::GetItemRectMin();
      ImVec2 text_max = ImGui::GetItemRectMax();
      float text_center_y = (text_min.y + text_max.y) * 0.5f;
      ImGui::SameLine(0.0f, 4.0f);

      const float lamp_size = 10.0f;
      ImVec2 cursor = ImGui::GetCursorScreenPos();
      float lamp_y = text_center_y - (lamp_size * 0.5f);
      ImVec2 lamp_min(cursor.x, lamp_y);
      ImVec2 lamp_max(cursor.x + lamp_size, lamp_y + lamp_size);

      ImDrawList *dl = ImGui::GetWindowDrawList();
      ImU32 fill = active ? IM_COL32(255, 32, 32, 255) : IM_COL32(64, 16, 16, 255);
      dl->AddRectFilled(lamp_min, lamp_max, fill, 2.0f);
      dl->AddRect(lamp_min, lamp_max, IM_COL32(200, 200, 200, 96), 2.0f);

      // Reserve layout width/height in the same line after custom draw.
      ImGui::Dummy(ImVec2(lamp_size, text_max.y - text_min.y));
    }
    
    uint64_t now_tick = SDL_GetTicks();

    // Right-aligned metrics (avoid overlap regardless of text length).
    // Decay emu FPS when no VM frames are being advanced.
    if (last_emu_progress_tick != 0 &&
        (now_tick - last_emu_progress_tick) > 1000 &&
        emu_frames_accum == 0) {
      emu_fps = 0.0f;
    }

    char fps_text[128];
    snprintf(fps_text, sizeof(fps_text), "Render: %.1f  Emu: %.1f", current_fps,
             emu_fps);
    char speed_text[64];
    if (config.full_speed) {
      snprintf(speed_text, sizeof(speed_text), "FULL SPEED");
    } else if (config.cpu_power > 1) {
      snprintf(speed_text, sizeof(speed_text), "Speed: x%d", config.cpu_power);
    } else {
      speed_text[0] = '\0';
    }

    float right = ImGui::GetWindowWidth() - 8.0f;
    if (speed_text[0] != '\0') {
      float speed_w = ImGui::CalcTextSize(speed_text).x;
      ImGui::SameLine(right - speed_w);
      if (config.full_speed) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "%s", speed_text);
      } else {
        ImGui::Text("%s", speed_text);
      }
      right -= (speed_w + 24.0f);
    }

    float fps_w = ImGui::CalcTextSize(fps_text).x;
    ImGui::SameLine(right - fps_w);
    ImGui::Text("%s", fps_text);
    
    ImGui::End();
  }
  ImGui::PopStyleColor();
  ImGui::PopStyleVar();
}

void OSD::add_extra_frames(int frames) {
  if (frames <= 0) {
    return;
  }
  emu_frames_accum += frames;
  uint64_t now = SDL_GetTicks();
  last_emu_progress_tick = now;
  if (last_emu_fps_tick == 0) {
    last_emu_fps_tick = now;
    return;
  }
  uint64_t elapsed = now - last_emu_fps_tick;
  if (elapsed >= 1000) {
    emu_fps = (float)emu_frames_accum * 1000.0f / (float)elapsed;
    emu_frames_accum = 0;
    last_emu_fps_tick = now;
  }
}

void OSD::sleep(uint32_t ms) { SDL_Delay(ms); }

void OSD::open_message_box(const _TCHAR *text) {
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "BubiC-8801MA", text,
                           window);
}

void OSD::initialize_console() {}
void OSD::release_console() {}
void OSD::open_console(int width, int height, const char *title) {}
void OSD::close_console() {}
void OSD::write_console(const char *buffer, unsigned int length) {}
void OSD::write_console_char(const char *buffer, unsigned int length) {}
void OSD::set_console_text_attribute(unsigned short attr) {}
unsigned int OSD::get_console_code_page() { return 65001; } // UTF-8 or default
int OSD::read_console_input(char *buffer, unsigned int length) { return 0; }
bool OSD::is_console_closed() { return true; }

void OSD::initialize_imgui() {
  OSD_LOG("initialize_imgui() starting...");
  if (imgui_initialized) {
    OSD_LOG("Already initialized, skipping");
    return;
  }

  OSD_LOG("IMGUI_CHECKVERSION()...");
  IMGUI_CHECKVERSION();

  OSD_LOG("CreateContext()...");
  ImGui::CreateContext();

  ImGuiIO &io = ImGui::GetIO();
  static std::string ini_path = std::string(get_application_path()) + "imgui.ini";
  io.IniFilename = ini_path.c_str();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
  OSD_LOG("ImGui context created, ini_path=%s", ini_path.c_str());

  ImGui::StyleColorsDark();

  // ImGui SDL3 backend now provides framebuffer scale each frame.
  // Keep logical UI sizes unscaled here to avoid double-scaling on HiDPI.
  const float ui_scale = 1.0f;
  OSD_LOG("ImGui UI scale = %.2f", ui_scale);

  // Load Japanese font - Search in common locations across macOS, Windows, and Linux
  OSD_LOG("Starting font search...");
  ImFont* font = NULL;
  const float font_size = 18.0f;
  const std::vector<std::string> font_paths = {
    // macOS
    "/System/Library/Fonts/jp/Hiragino Sans GB.ttc",
    "/System/Library/Fonts/ヒラギノ角ゴシック W3.ttc",
    "/System/Library/Fonts/Hiragino Sans GB.ttc",
    "/System/Library/Fonts/Supplemental/Hiragino Sans GB.ttc",
    // Windows
    "C:\\Windows\\Fonts\\msgothic.ttc",
    "C:\\Windows\\Fonts\\msmincho.ttc",
    "C:\\Windows\\Fonts\\meiryo.ttc",
    // Linux
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/truetype/takao-gothic/TakaoPGothic.ttf",
    "/usr/share/fonts/truetype/vlgothic/VL-PGothic-Regular.ttf"
  };

  OSD_LOG("Will check %zu font paths", font_paths.size());
  int path_index = 0;
  for (const auto& path : font_paths) {
    OSD_LOG("[%d] Checking: %s", path_index, path.c_str());
    bool exists = false;
    try {
      exists = fs::exists(path);
      OSD_LOG("[%d] fs::exists returned %d", path_index, exists);
    } catch (const std::exception& e) {
      OSD_LOG("[%d] fs::exists threw std::exception: %s", path_index, e.what());
      path_index++;
      continue;
    } catch (...) {
      OSD_LOG("[%d] fs::exists threw unknown exception", path_index);
      path_index++;
      continue;
    }
    if (exists) {
      OSD_LOG("[%d] Loading font...", path_index);
      font = io.Fonts->AddFontFromFileTTF(path.c_str(), font_size, NULL, io.Fonts->GetGlyphRangesJapanese());
      if (font) {
        OSD_LOG("[%d] Font loaded successfully!", path_index);
        break;
      } else {
        OSD_LOG("[%d] AddFontFromFileTTF returned NULL", path_index);
      }
    }
    path_index++;
  }

  if (!font) {
    OSD_LOG("No Japanese font found, using default font");
    ImFontConfig cfg;
    cfg.SizePixels = 13.0f;
    io.Fonts->AddFontDefault(&cfg);
  }

  OSD_LOG("ImGui_ImplSDL3_InitForSDLRenderer(window=%p, renderer=%p)...", (void*)window, (void*)renderer);
  bool sdl3_init = ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
  OSD_LOG("ImGui_ImplSDL3_InitForSDLRenderer returned %d", sdl3_init);

  OSD_LOG("ImGui_ImplSDLRenderer3_Init(renderer=%p)...", (void*)renderer);
  bool renderer_init = ImGui_ImplSDLRenderer3_Init(renderer);
  OSD_LOG("ImGui_ImplSDLRenderer3_Init returned %d", renderer_init);

  if (sdl3_init && renderer_init) {
    imgui_initialized = true;
    OSD_LOG("ImGui initialized successfully!");
  } else {
    OSD_LOG("ImGui initialization FAILED! sdl3_init=%d, renderer_init=%d", sdl3_init, renderer_init);
  }
}

void OSD::release_imgui() {
  if (!imgui_initialized) return;

  ImGui_ImplSDLRenderer3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  imgui_initialized = false;
}

bool OSD::draw_menu_contents() {
    bool menu_tree_open = false;
    if (ImGui::BeginMenu("Control")) {
      menu_tree_open = true;
      if (ImGui::MenuItem("Reset")) {
        if (emu) emu->reset();
      }
      ImGui::Separator();
      if (ImGui::MenuItem("CPU x1", NULL, config.cpu_power == 1)) { config.cpu_power = 1; if(vm) vm->update_config(); }
      if (ImGui::MenuItem("CPU x2", NULL, config.cpu_power == 2)) { config.cpu_power = 2; if(vm) vm->update_config(); }
      if (ImGui::MenuItem("CPU x4", NULL, config.cpu_power == 4)) { config.cpu_power = 4; if(vm) vm->update_config(); }
      if (ImGui::MenuItem("CPU x8", NULL, config.cpu_power == 8)) { config.cpu_power = 8; if(vm) vm->update_config(); }
      if (ImGui::MenuItem("CPU x16", NULL, config.cpu_power == 16)) { config.cpu_power = 16; if(vm) vm->update_config(); }
      if (ImGui::MenuItem("Full Speed", NULL, config.full_speed)) { config.full_speed = !config.full_speed; }
      ImGui::Separator();
      if (ImGui::MenuItem("Romaji to Kana", NULL, config.romaji_to_kana)) { config.romaji_to_kana = !config.romaji_to_kana; }
      ImGui::Separator();
      if (ImGui::BeginMenu("Save State")) {
        for(int i=0; i<10; i++) {
          const _TCHAR* path = emu->state_file_path(i);
          char label[64];
          if (fs::exists(tchar_to_char(path))) {
            auto ftime = fs::last_write_time(tchar_to_char(path));
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
            std::time_t tt = std::chrono::system_clock::to_time_t(sctp);
            std::tm* local_t = std::localtime(&tt);
            char time_str[32];
            std::strftime(time_str, sizeof(time_str), "%Y/%m/%d %H:%M:%S", local_t);
            snprintf(label, sizeof(label), "%d: %s", i, time_str);
          } else {
            snprintf(label, sizeof(label), "%d: (No Data)", i);
          }
          if(ImGui::MenuItem(label)) { if(emu) emu->save_state(path); }
        }
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("Load State")) {
        for(int i=0; i<10; i++) {
          const _TCHAR* path = emu->state_file_path(i);
          char label[64];
          bool exists = fs::exists(tchar_to_char(path));
          if (exists) {
            auto ftime = fs::last_write_time(tchar_to_char(path));
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
            std::time_t tt = std::chrono::system_clock::to_time_t(sctp);
            std::tm* local_t = std::localtime(&tt);
            char time_str[32];
            std::strftime(time_str, sizeof(time_str), "%Y/%m/%d %H:%M:%S", local_t);
            snprintf(label, sizeof(label), "%d: %s", i, time_str);
          } else {
            snprintf(label, sizeof(label), "%d: (No Data)", i);
          }
          if(ImGui::MenuItem(label, NULL, false, exists)) { if(emu) emu->load_state(path); }
        }
        ImGui::EndMenu();
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Exit")) {
        terminated = true;
      }
      ImGui::EndMenu();
    }

    for (int drv = 0; drv < 2; drv++) {
      char menu_label[16];
      snprintf(menu_label, sizeof(menu_label), drv == 0 ? "FD1" : "FD2");
      
      if (ImGui::BeginMenu(menu_label)) {
        menu_tree_open = true;
        if (ImGui::MenuItem("Insert")) { select_file(drv); }
        if (ImGui::MenuItem("Eject")) { 
          if (vm) {
            vm->is_floppy_disk_protected(drv, false); // Clear write protect on eject
            vm->close_floppy_disk(drv); 
          }
          if (drv == 0) fd1_path[0] = '\0'; else fd2_path[0] = '\0';
          if (emu) {
            emu->floppy_disk_status[drv].path[0] = '\0';
            emu->d88_file[drv].path[0] = '\0';
          }
        }
        if (ImGui::MenuItem("Insert Blank 2D Disk")) { select_save_file(drv, 0x00); }
        if (ImGui::MenuItem("Insert Blank 2HD Disk")) { select_save_file(drv, 0x20); }
        ImGui::Separator(); // ----
        bool inserted = (vm && vm->is_floppy_disk_inserted(drv));
        if (ImGui::MenuItem("Write Protected", NULL, (vm && vm->is_floppy_disk_protected(drv)), inserted)) {
          if(vm) vm->is_floppy_disk_protected(drv, !vm->is_floppy_disk_protected(drv));
        }
        if (ImGui::MenuItem("Correct Timing", NULL, config.correct_disk_timing[drv])) {
          config.correct_disk_timing[drv] = !config.correct_disk_timing[drv];
          if(vm) vm->update_config();
        }
        if (ImGui::MenuItem("Ignore CRC Errors", NULL, config.ignore_disk_crc[drv])) {
          config.ignore_disk_crc[drv] = !config.ignore_disk_crc[drv];
          if(vm) vm->update_config();
        }
        ImGui::Separator(); // ----
        
        if (emu && emu->floppy_disk_status[drv].path[0] != '\0') {
          // D88ファイル名を表示
          fs::path p(tchar_to_char(emu->floppy_disk_status[drv].path));
          std::string filename_utf8 = path_to_utf8(p.filename());
          ImGui::TextDisabled("%s", filename_utf8.c_str());

          // イメージ（バンク）一覧をメインメニューに直接表示
          if (emu->d88_file[drv].bank_num > 1) {
            ImGui::Separator(); // ----
            for (int b = 0; b < emu->d88_file[drv].bank_num; b++) {
              const _TCHAR* b_name = emu->d88_file[drv].disk_name[b];
              char b_label[160];
              if (b_name[0] != '\0') {
                snprintf(b_label, sizeof(b_label), "%s", tchar_to_char(b_name));
              } else {
                snprintf(b_label, sizeof(b_label), "Image %d", b + 1);
              }
              if (ImGui::MenuItem(b_label, NULL, emu->d88_file[drv].cur_bank == b)) {
                if (vm) vm->open_floppy_disk(drv, emu->d88_file[drv].path, b);
                emu->floppy_disk_status[drv].bank = b;
                emu->d88_file[drv].cur_bank = b;
              }
            }
          }
        } else {
          ImGui::TextDisabled("(No disk inserted)");
        }

        ImGui::Separator(); // ----
        // 最近使ったファイル（ドライブごとに独立）
        if (ImGui::BeginMenu("Recent Disks")) {
          for (int i = 0; i < MAX_HISTORY; i++) {
            if (config.recent_floppy_disk_path[drv][i][0] == '\0') break;
            const _TCHAR* r_path = config.recent_floppy_disk_path[drv][i];
            
            // 表示名はD88ファイル名のみ、ID衝突回避のために##indexを付加
            fs::path p(tchar_to_char(r_path));
            std::string filename_utf8 = path_to_utf8(p.filename());
            char label[512];
            snprintf(label, sizeof(label), "%s##recent_%d", filename_utf8.c_str(), i);

            if (ImGui::MenuItem(label)) {
              if (vm) {
                vm->open_floppy_disk(drv, r_path, 0);
                if (drv == 0) my_tcscpy_s(fd1_path, _MAX_PATH, utf8_path_to_tchar(filename_utf8.c_str()));
                else my_tcscpy_s(fd2_path, _MAX_PATH, utf8_path_to_tchar(filename_utf8.c_str()));

                if (emu) {
                  my_tcscpy_s(emu->floppy_disk_status[drv].path, _MAX_PATH, r_path);
                  emu->floppy_disk_status[drv].bank = 0;
                  int banks = get_disk_names(tchar_to_char(r_path), drv, emu);
                  emu->d88_file[drv].cur_bank = 0;

                  // FD1選択時、FD2が空なら2番目のイメージを自動装填
                  if (drv == 0 && fd2_path[0] == '\0' && banks >= 2) {
                    vm->open_floppy_disk(1, r_path, 1);
                    my_tcscpy_s(fd2_path, _MAX_PATH, utf8_path_to_tchar(filename_utf8.c_str()));
                    my_tcscpy_s(emu->floppy_disk_status[1].path, _MAX_PATH, r_path);
                    emu->floppy_disk_status[1].bank = 1;
                    get_disk_names(tchar_to_char(r_path), 1, emu);
                    emu->d88_file[1].cur_bank = 1;
                    // 自動装填した分も履歴に追加
                    add_recent_disk(r_path, 1);
                  }
                }
              }
              add_recent_disk(r_path, drv);
            }
          }
          ImGui::EndMenu();
        }
        ImGui::EndMenu();
      }
    }

    if (ImGui::BeginMenu("Device")) {
      menu_tree_open = true;
      if (ImGui::BeginMenu("Boot")) {
        if (ImGui::MenuItem("N88-V1(S) mode", NULL, config.boot_mode == 0)) { config.boot_mode = 0; if(emu) emu->reset(); }
        if (ImGui::MenuItem("N88-V1(H) mode", NULL, config.boot_mode == 1)) { config.boot_mode = 1; if(emu) emu->reset(); }
        if (ImGui::MenuItem("N88-V2 mode", NULL, config.boot_mode == 2)) { config.boot_mode = 2; if(emu) emu->reset(); }
        if (ImGui::MenuItem("N mode", NULL, config.boot_mode == 3)) { config.boot_mode = 3; if(vm) vm->update_config(); } // Keep VM update if intended
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("CPU")) {
        if (ImGui::MenuItem("Z80 8MHz", NULL, config.cpu_type == 0)) { config.cpu_type = 0; if(vm) vm->update_config(); }
        if (ImGui::MenuItem("Z80 4MHz", NULL, config.cpu_type == 1)) { config.cpu_type = 1; if(vm) vm->update_config(); }
        if (ImGui::MenuItem("Z80 8MHz (FE2/MC)", NULL, config.cpu_type == 2)) { config.cpu_type = 2; if(vm) vm->update_config(); }
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("Sound")) {
        bool is_opna = (config.sound_type == 0 || config.sound_type == 4 || config.sound_type == 5);
        if (ImGui::MenuItem("SOUND BOARD II", NULL, is_opna)) {
          config.sound_type = is_opna ? 1 : 0; // Toggle between OPNA(0) and OPN(1)
          if(emu) emu->reset();
        }
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("Display")) {
        if (ImGui::MenuItem("High Resolution", NULL, config.monitor_type == 0)) { config.monitor_type = 0; if(vm) vm->update_config(); }
        if (ImGui::MenuItem("Standard", NULL, config.monitor_type == 1)) { config.monitor_type = 1; if(vm) vm->update_config(); }
        ImGui::Separator();
        if (ImGui::MenuItem("Set Scanline Automatically", NULL, config.scan_line_auto)) { config.scan_line_auto = !config.scan_line_auto; if(vm) vm->update_config(); }
        if (ImGui::MenuItem("Scanline", NULL, config.scan_line)) { config.scan_line = !config.scan_line; if(vm) vm->update_config(); }
        ImGui::Separator();
        if (ImGui::MenuItem("Ignore Palette Changed", NULL, (config.dipswitch & (1 << 5)) != 0)) { 
          config.dipswitch ^= (1 << 5); if(vm) vm->update_config(); 
        }
        ImGui::EndMenu();
      }
      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Host")) {
      menu_tree_open = true;
      if (ImGui::BeginMenu("Screen")) {
        if (ImGui::MenuItem("Fullscreen", NULL, (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN))) {
          if (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) SDL_SetWindowFullscreen(window, false);
          else SDL_SetWindowFullscreen(window, true);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("x1.0", NULL, config.window_scale_idx == 0)) { config.window_scale_idx = 0; update_window_scale(); }
        if (ImGui::MenuItem("x1.5", NULL, config.window_scale_idx == 1)) { config.window_scale_idx = 1; update_window_scale(); }
        if (ImGui::MenuItem("x2.0", NULL, config.window_scale_idx == 2)) { config.window_scale_idx = 2; update_window_scale(); }
        if (ImGui::MenuItem("x2.5", NULL, config.window_scale_idx == 3)) { config.window_scale_idx = 3; update_window_scale(); }
        if (ImGui::MenuItem("x3.0", NULL, config.window_scale_idx == 4)) { config.window_scale_idx = 4; update_window_scale(); }
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("Keyboard")) {
        if (ImGui::MenuItem("Map cursor keys to Numpad", NULL, config.cursor_as_numpad)) {
          config.cursor_as_numpad = !config.cursor_as_numpad;
        }
        if (ImGui::MenuItem("Map number keys to Numpad", NULL, config.digit_as_numpad)) {
          config.digit_as_numpad = !config.digit_as_numpad;
        }
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("Sound")) {
        static const int frequency_values[5] = {
            44100, 48000, 55467, 88200, 96000
        };
        static const char *frequency_labels[5] = {
            "44100 Hz", "48000 Hz", "55467 Hz", "88200 Hz", "96000 Hz"
        };
        static const int latency_values_ms[5] = {50, 100, 200, 300, 400};
        static const char *latency_labels[5] = {
            "50 ms", "100 ms", "200 ms", "300 ms", "400 ms"
        };

        int freq_index = config.sound_frequency;
        if (freq_index < 0 || freq_index >= 5) {
          freq_index = 2;
        }
        int latency_index = config.sound_latency;
        if (latency_index < 0 || latency_index >= 5) {
          latency_index = 1;
        }

        if (requested_audio_rate <= 0) {
          requested_audio_rate = frequency_values[freq_index];
        }
        if (requested_audio_latency_ms <= 0) {
          requested_audio_latency_ms = latency_values_ms[latency_index];
        }

        if (ImGui::BeginMenu("Sampling Frequency")) {
          for (int i = 0; i < 5; i++) {
            if (ImGui::MenuItem(frequency_labels[i], NULL,
                                config.sound_frequency == i)) {
              if (config.sound_frequency != i) {
                const int prev_freq = config.sound_frequency;
                const int prev_latency = config.sound_latency;
                const int requested_rate = frequency_values[i];
                const int requested_latency =
                    latency_values_ms[(config.sound_latency >= 0 &&
                                       config.sound_latency < 5)
                                          ? config.sound_latency
                                          : 1];
                requested_audio_rate = requested_rate;
                requested_audio_latency_ms = requested_latency;
                config.sound_frequency = i;
                if (emu) {
                  bool ok = emu->apply_host_sound_settings();
                  if (!ok) {
                    config.sound_frequency = prev_freq;
                    config.sound_latency = prev_latency;
                    continue;
                  }
                }
              }
            }
          }
          ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Audio Latency")) {
          for (int i = 0; i < 5; i++) {
            if (ImGui::MenuItem(latency_labels[i], NULL,
                                config.sound_latency == i)) {
              if (config.sound_latency != i) {
                const int prev_freq = config.sound_frequency;
                const int prev_latency = config.sound_latency;
                const int requested_rate =
                    frequency_values[(config.sound_frequency >= 0 &&
                                      config.sound_frequency < 5)
                                         ? config.sound_frequency
                                         : 2];
                const int requested_latency = latency_values_ms[i];
                requested_audio_rate = requested_rate;
                requested_audio_latency_ms = requested_latency;
                config.sound_latency = i;
                if (emu) {
                  bool ok = emu->apply_host_sound_settings();
                  if (!ok) {
                    config.sound_frequency = prev_freq;
                    config.sound_latency = prev_latency;
                    continue;
                  }
                }
              }
            }
          }
          ImGui::EndMenu();
        }
        ImGui::EndMenu();
      }
      ImGui::EndMenu();
    }
    return menu_tree_open;
}

void OSD::draw_menu() {
  if (!show_menu) return;

  ImGui_ImplSDLRenderer3_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();

  if (ImGui::BeginMainMenuBar()) {
    (void)draw_menu_contents();
    ImGui::EndMainMenuBar();
  }

  draw_file_browser();
  draw_save_browser();

  ImGui::Render();
  ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
}

void OSD::select_file(int drive) {
  pending_drive = drive;
  show_file_browser = true;
}

void OSD::draw_file_browser() {
  if (!show_file_browser) return;

  // Validate path and fall back to parent if it doesn't exist
  fs::path current_p(current_browser_path);
  while (!fs::exists(current_p) || !fs::is_directory(current_p)) {
    if (current_p.has_parent_path() && current_p != current_p.root_path()) {
      current_p = current_p.parent_path();
    } else {
      current_p = get_home_directory();
      break;
    }
    std::string path_utf8 = path_to_utf8(current_p);
    snprintf(current_browser_path, _MAX_PATH, "%s", path_utf8.c_str());
    my_tcscpy_s(config.last_browser_path, _MAX_PATH, utf8_path_to_tchar(current_browser_path));
  }

  static std::string popup_file_path;
  static int popup_num_banks = 0;
  static bool open_popup = false;

  if (ImGui::Begin("File Browser", &show_file_browser)) {
    ImGui::Text("Path: %s", current_browser_path);

#ifdef _WIN32
    // Drive selection for Windows
    ImGui::SameLine();
    ImGui::Text(" | Drive:");
    ImGui::SameLine();
    std::vector<char> drives = get_available_drives();
    for (char drive : drives) {
      ImGui::SameLine();
      char label[4] = {drive, ':', '\0'};
      if (ImGui::SmallButton(label)) {
        snprintf(current_browser_path, _MAX_PATH, "%c:\\", drive);
        my_tcscpy_s(config.last_browser_path, _MAX_PATH, utf8_path_to_tchar(current_browser_path));
      }
    }
#endif

    ImGui::Separator();

    try {
      if (fs::exists(current_browser_path) && fs::is_directory(current_browser_path)) {
        // Add Parent Directory option
        fs::path p(current_browser_path);
        if (p.has_parent_path() && p != p.root_path()) {
          if (ImGui::Button("[..] (Parent Directory)")) {
            std::string parent_path = path_to_utf8(p.parent_path());
            snprintf(current_browser_path, _MAX_PATH, "%s", parent_path.c_str());
            my_tcscpy_s(config.last_browser_path, _MAX_PATH, utf8_path_to_tchar(current_browser_path));
          }
        }

        int file_count = 0;
          for (const auto & entry : fs::directory_iterator(current_browser_path)) {
            std::string filename = path_to_utf8(entry.path().filename());
#ifdef __APPLE__
            filename = nfd_to_nfc(filename);
#endif
            if (entry.is_directory()) {
              if (ImGui::Button(("[D] " + filename).c_str())) {
                std::string dir_path = path_to_utf8(entry.path());
                snprintf(current_browser_path, _MAX_PATH, "%s", dir_path.c_str());
                my_tcscpy_s(config.last_browser_path, _MAX_PATH, utf8_path_to_tchar(current_browser_path));
              }
            } else {
            std::string ext = path_to_utf8(entry.path().extension());
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".d88" || ext == ".d77" || ext == ".2hd" || ext == ".2d") {
              if (ImGui::Button(filename.c_str())) {
                std::string full_path = path_to_utf8(entry.path());

                std::string display_name = filename;

                if (pending_drive == 0) {
                  // FD1: Always insert the first image (Bank 0)
                  if (vm) {
                    vm->open_floppy_disk(0, utf8_path_to_tchar(full_path.c_str()), 0);
                    my_tcscpy_s(fd1_path, _MAX_PATH, utf8_path_to_tchar(display_name.c_str()));
                    
                    if (emu) {
                      my_tcscpy_s(emu->floppy_disk_status[0].path, _MAX_PATH, utf8_path_to_tchar(full_path.c_str()));
                      emu->floppy_disk_status[0].bank = 0;
                      // ヘッダを解析して内部イメージ名を取得
                      int banks = get_disk_names(full_path.c_str(), 0, emu);
                      emu->d88_file[0].cur_bank = 0;

                      // If FD2 is empty and file has a second image, insert Bank 1 to FD2
                      if (fd2_path[0] == '\0' && banks >= 2) {
                        vm->open_floppy_disk(1, utf8_path_to_tchar(full_path.c_str()), 1);
                        my_tcscpy_s(fd2_path, _MAX_PATH, utf8_path_to_tchar(display_name.c_str()));
                        if (emu) {
                          my_tcscpy_s(emu->floppy_disk_status[1].path, _MAX_PATH, utf8_path_to_tchar(full_path.c_str()));
                          emu->floppy_disk_status[1].bank = 1;
                          get_disk_names(full_path.c_str(), 1, emu);
                          emu->d88_file[1].cur_bank = 1;
                          // 自動装填した分も履歴に追加
                          add_recent_disk(utf8_path_to_tchar(full_path.c_str()), 1);
                        }
                      }
                    }
                    add_recent_disk(utf8_path_to_tchar(full_path.c_str()), 0);
                  }
                } else {
                  // FD2: Always insert the first image (Bank 0)
                  if (vm) {
                    vm->open_floppy_disk(1, utf8_path_to_tchar(full_path.c_str()), 0);
                    my_tcscpy_s(fd2_path, _MAX_PATH, utf8_path_to_tchar(display_name.c_str()));
                    if (emu) {
                      my_tcscpy_s(emu->floppy_disk_status[1].path, _MAX_PATH, utf8_path_to_tchar(full_path.c_str()));
                      emu->floppy_disk_status[1].bank = 0;
                      get_disk_names(full_path.c_str(), 1, emu);
                      emu->d88_file[1].cur_bank = 0;
                    }
                    add_recent_disk(utf8_path_to_tchar(full_path.c_str()), 1);
                  }
                }
                show_file_browser = false;
              }
              file_count++;
            }
          }
        }
        if (file_count == 0) {
          ImGui::TextDisabled("(No supported disk images found in this directory)");
        }
      } else {
        ImGui::TextColored(ImVec4(1,0,0,1), "Invalid Path!");
      }
    } catch (const std::exception& e) {
      ImGui::Text("Error: %s", e.what());
    }

    if (ImGui::Button("Home")) {
      std::string home_dir = get_home_directory();
      snprintf(current_browser_path, _MAX_PATH, "%s", home_dir.c_str());
      my_tcscpy_s(config.last_browser_path, _MAX_PATH, utf8_path_to_tchar(current_browser_path));
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) {
      show_file_browser = false;
    }
    ImGui::End();
  }
}

void OSD::select_save_file(int drive, int type) {
  pending_drive = drive;
  pending_blank_type = type;
  show_save_browser = true;
}

void OSD::draw_save_browser() {
  if (!show_save_browser) return;

  // Validate path and fall back to parent if it doesn't exist
  fs::path current_p(current_browser_path);
  while (!fs::exists(current_p) || !fs::is_directory(current_p)) {
    if (current_p.has_parent_path() && current_p != current_p.root_path()) {
      current_p = current_p.parent_path();
    } else {
      current_p = get_home_directory();
      break;
    }
    std::string path_utf8 = path_to_utf8(current_p);
    snprintf(current_browser_path, _MAX_PATH, "%s", path_utf8.c_str());
    my_tcscpy_s(config.last_browser_path, _MAX_PATH, utf8_path_to_tchar(current_browser_path));
  }

  static char save_filename[256] = "blank.d88";

  if (ImGui::Begin("Create Blank Disk", &show_save_browser)) {
    ImGui::Text("Path: %s", current_browser_path);

#ifdef _WIN32
    // Drive selection for Windows
    ImGui::SameLine();
    ImGui::Text(" | Drive:");
    ImGui::SameLine();
    std::vector<char> drives = get_available_drives();
    for (char drive : drives) {
      ImGui::SameLine();
      char label[4] = {drive, ':', '\0'};
      if (ImGui::SmallButton(label)) {
        snprintf(current_browser_path, _MAX_PATH, "%c:\\", drive);
        my_tcscpy_s(config.last_browser_path, _MAX_PATH, utf8_path_to_tchar(current_browser_path));
      }
    }
#endif

    ImGui::Separator();

    // Reuse directory navigation from file browser
    try {
      if (fs::exists(current_browser_path) && fs::is_directory(current_browser_path)) {
        fs::path p(current_browser_path);
        if (p.has_parent_path() && p != p.root_path()) {
          if (ImGui::Button("[..] (Parent Directory)")) {
            std::string parent_path = path_to_utf8(p.parent_path());
            snprintf(current_browser_path, _MAX_PATH, "%s", parent_path.c_str());
            my_tcscpy_s(config.last_browser_path, _MAX_PATH, utf8_path_to_tchar(current_browser_path));
          }
        }
        for (const auto & entry : fs::directory_iterator(current_browser_path)) {
          if (entry.is_directory()) {
            std::string dirname = path_to_utf8(entry.path().filename());
#ifdef __APPLE__
            dirname = nfd_to_nfc(dirname);
#endif
            if (ImGui::Button(("[D] " + dirname).c_str())) {
              std::string dir_path = path_to_utf8(entry.path());
              snprintf(current_browser_path, _MAX_PATH, "%s", dir_path.c_str());
              my_tcscpy_s(config.last_browser_path, _MAX_PATH, utf8_path_to_tchar(current_browser_path));
            }
          }
        }
      }
    } catch (...) {}

    ImGui::Separator();
    ImGui::InputText("Filename", save_filename, sizeof(save_filename));
    
    if (ImGui::Button("Create and Insert")) {
      fs::path full_save_path = fs::path(current_browser_path) / save_filename;
      std::string path_str = path_to_utf8(full_save_path);
      std::string filename_str = path_to_utf8(full_save_path.filename());

      if (emu && emu->create_blank_floppy_disk(utf8_path_to_tchar(path_str.c_str()), (uint8_t)pending_blank_type)) {
        if (vm) {
          vm->open_floppy_disk(pending_drive, utf8_path_to_tchar(path_str.c_str()), 0);
          if (pending_drive == 0) {
            my_tcscpy_s(fd1_path, _MAX_PATH, utf8_path_to_tchar(filename_str.c_str()));
          } else {
            my_tcscpy_s(fd2_path, _MAX_PATH, utf8_path_to_tchar(filename_str.c_str()));
          }
          
          if (emu) {
            my_tcscpy_s(emu->floppy_disk_status[pending_drive].path, _MAX_PATH, utf8_path_to_tchar(path_str.c_str()));
            emu->floppy_disk_status[pending_drive].bank = 0;
            get_disk_names(path_str.c_str(), pending_drive, emu);
            emu->d88_file[pending_drive].cur_bank = 0;
          }
          add_recent_disk(utf8_path_to_tchar(path_str.c_str()), pending_drive);
        }
        show_save_browser = false;
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      show_save_browser = false;
    }
    ImGui::End();
  }
}
