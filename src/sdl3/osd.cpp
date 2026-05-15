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

#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#include "../vm/vm.h"

#define OSD_LOG(fmt, ...) do { fprintf(stderr, "[OSD] " fmt "\n", ##__VA_ARGS__); fflush(stderr); } while(0)

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <vector>
static std::string nfd_to_nfc(const std::string& input) {
    if (input.empty()) return input;
    CFStringRef cf_input = CFStringCreateWithCString(kCFAllocatorDefault, input.c_str(), kCFStringEncodingUTF8);
    if (!cf_input) return input;

    CFMutableStringRef cf_mutable = CFStringCreateMutableCopy(kCFAllocatorDefault, 0, cf_input);
    CFStringNormalize(cf_mutable, kCFStringNormalizationFormC);

    CFIndex length = CFStringGetMaximumSizeForEncoding(CFStringGetLength(cf_mutable), kCFStringEncodingUTF8) + 1;
    std::vector<char> buffer(length);
    if (CFStringGetCString(cf_mutable, buffer.data(), length, kCFStringEncodingUTF8)) {
        std::string result(buffer.data());
        CFRelease(cf_input);
        CFRelease(cf_mutable);
        return result;
    }

    CFRelease(cf_input);
    CFRelease(cf_mutable);
    return input;
}
#endif

namespace fs = std::filesystem;

// Localization
enum class Language {
  ENGLISH,
  JAPANESE,
  CHINESE,
  KOREAN,
  SPANISH,
  FRENCH
};

static Language current_lang = Language::ENGLISH;

struct Msg {
  const char* en;
  const char* jp;
  const char* zh;
  const char* ko;
  const char* es;
  const char* fr;
  operator const char*() const {
    switch (current_lang) {
      case Language::JAPANESE: return jp;
      case Language::CHINESE:  return zh;
      case Language::KOREAN:   return ko;
      case Language::SPANISH:  return es;
      case Language::FRENCH:   return fr;
      default:                 return en;
    }
  }
};

namespace Lang {
  static constexpr Msg Control = {"Control", "コントロール", "控制", "컨트롤", "Control", "Contrôle"};
  static constexpr Msg Reset = {"Reset", "リセット", "重置", "초기화", "Reiniciar", "Réinitialiser"};
  static constexpr Msg FullSpeed = {"Full Speed", "フルスピード", "全速", "최고 속도", "Velocidad máxima", "Vitesse maximale"};
  static constexpr Msg RomajiToKana = {"Romaji to Kana", "ローマ字かな変換", "罗马字转假名", "로마자 카나 변환", "Romaji a Kana", "Romaji vers Kana"};
  static constexpr Msg System = {"System", "システム", "系统", "시스템", "Sistema", "Système"};
  static constexpr Msg ResetOnDD = {"Reset on D&D", "D&D時にリセット", "拖放时重置", "D&D시 초기화", "Restablecer en D&D", "Réinitialiser sur D&D"};
  static constexpr Msg SaveState = {"Save State", "状態保存", "保存存档", "상태 저장", "Guardar estado", "Sauvegarder l'état"};
  static constexpr Msg LoadState = {"Load State", "状態復元", "读取存档", "상태 불러오기", "Cargar estado", "Charger l'état"};
  static constexpr Msg NoData = {"(No Data)", "(データなし)", "(无数据)", "(데이터 없음)", "(Sin datos)", "(Aucune donnée)"};
  static constexpr Msg StateDialogMenu = {"Save / Load State...", "状態保存・復元...", "保存／读取存档...", "상태 저장 · 불러오기...", "Guardar / cargar estado...", "Sauvegarder / charger l'état..."};
  static constexpr Msg StateDialogTitle = {"Save / Load State", "状態保存・復元", "保存／读取存档", "상태 저장 · 불러오기", "Guardar / cargar estado", "Sauvegarder / charger l'état"};
  static constexpr Msg Slot = {"Slot %d", "スロット %d", "槽 %d", "슬롯 %d", "Ranura %d", "Emplacement %d"};
  static constexpr Msg SaveBtn = {"Save", "保存", "保存", "저장", "Guardar", "Sauvegarder"};
  static constexpr Msg LoadBtn = {"Load", "復元", "读取", "불러오기", "Cargar", "Charger"};
  static constexpr Msg CloseBtn = {"Close", "閉じる", "关闭", "닫기", "Cerrar", "Fermer"};
  static constexpr Msg DeleteBtn = {"Delete", "削除", "删除", "삭제", "Eliminar", "Supprimer"};
  static constexpr Msg Exit = {"Exit", "終了", "退出", "종료", "Salir", "Quitter"};
  static constexpr Msg Insert = {"Insert", "挿入", "插入", "삽입", "Insertar", "Insérer"};
  static constexpr Msg Eject = {"Eject", "取り出し", "弹出", "꺼내기", "Expulsar", "Éjecter"};
  static constexpr Msg InsertBlank2D = {"Insert Blank 2D Disk", "空の2Dディスクを挿入", "插入空白2D磁盘", "빈 2D 디스크 삽입", "Insertar disco 2D en blanco", "Insérer un disque 2D vierge"};
  static constexpr Msg InsertBlank2HD = {"Insert Blank 2HD Disk", "空の2HDディスクを挿入", "插入空白2HD磁盘", "빈 2HD 디스크 삽입", "Insertar disco 2HD en blanco", "Insérer un disque 2HD vierge"};
  static constexpr Msg WriteProtected = {"Write Protected", "書き込み禁止", "写保护", "쓰기 금지", "Protección contra escritura", "Protégé en écriture"};
  static constexpr Msg CorrectTiming = {"Correct Timing", "正確なタイミング", "精确时序", "정확한 타이밍", "Temporización correcta", "Synchronisation précise"};
  static constexpr Msg IgnoreCRC = {"Ignore CRC Errors", "CRCエラーを無視", "忽略CRC错误", "CRC 오류 무시", "Ignorar errores CRC", "Ignorer les erreurs CRC"};
  static constexpr Msg ImageN = {"Image %d", "イメージ %d", "镜像 %d", "이미지 %d", "Imagen %d", "Image %d"};
  static constexpr Msg NoDiskInserted = {"(No disk inserted)", "(未挿入)", "(未插入磁盘)", "(디스크 없음)", "(Sin disco)", "(Aucun disque)"};
  static constexpr Msg RecentDisks = {"Recent Disks", "最近使ったディスク", "最近使用的磁盘", "최근 사용한 디스크", "Discos recientes", "Disques récents"};
  static constexpr Msg Device = {"Device", "デバイス", "设备", "장치", "Dispositivo", "Appareil"};
  static constexpr Msg Boot = {"Boot", "起動モード", "启动模式", "부팅 모드", "Modo de inicio", "Mode de démarrage"};
  static constexpr Msg CPU = {"CPU", "CPU", "处理器", "CPU", "CPU", "Processeur"};
  static constexpr Msg Sound = {"Sound", "サウンド", "声音", "사운드", "Sonido", "Son"};
  static constexpr Msg Display = {"Display", "表示", "显示", "디ス플레이", "Pantalla", "Affichage"};
  static constexpr Msg HighResolution = {"High Resolution", "高解像度", "高分辨率", "고해상度", "Alta resolución", "Haute résolution"};
  static constexpr Msg Standard = {"Standard", "標準", "标准", "표준", "Estándar", "Standard"};
  static constexpr Msg ScanlineAuto = {"Set Scanline Automatically", "スキャンライン自動設定", "自动设置扫描线", "스캔라인 자동 설정", "Ajustar líneas de escaneo automáticamente", "Régler les lignes de balayage automatiquement"};
  static constexpr Msg Scanline = {"Scanline", "スキャンライン", "扫描线", "スキャンライン", "Líneas de escaneo", "Lignes de balayage"};
  static constexpr Msg IgnorePalette = {"Ignore Palette Changed", "パレット変更を無視", "忽略调色板更改", "팔레트 변경 무시", "Ignorar cambios de paleta", "Ignorer les changements de palette"};
  static constexpr Msg Host = {"Host", "ホスト", "主机", "호스트", "Host", "Hôte"};
  static constexpr Msg Screen = {"Screen", "画面", "屏幕", "화면", "Pantalla", "Écran"};
  static constexpr Msg Fullscreen = {"Fullscreen", "フルスクリーン", "全屏", "전체 화면", "Pantalla completa", "Plein écran"};
  static constexpr Msg SaveScreenshot = {"Save Screenshot...", "スクリーンショット保存...", "保存截图...", "스크린샷 저장...", "Guardar captura de pantalla...", "Enregistrer une capture d'écran..."};
  static constexpr Msg Keyboard = {"Keyboard", "キーボード", "键盘", "키보드", "Teclado", "Clavier"};
  static constexpr Msg MapCursorToNumpad = {"Map cursor keys to Numpad", "カーソルキーをテンキーに割当", "映射方向键到数字键盘", "방향키를 숫자 키패드에 할당", "Mapear cursores al teclado numérico", "Mapper les flèches sur le pavé numérique"};
  static constexpr Msg MapDigitToNumpad = {"Map number keys to Numpad", "数字キーをテンキーに割当", "映射数字键到数字键盘", "숫자 키를 숫자 키패드に 할당", "Mapear números al teclado numérico", "Mapper les chiffres sur le pavé numérique"};
  static constexpr Msg SamplingFrequency = {"Sampling Frequency", "サンプリング周波数", "采样率", "샘플링 주파수", "Frecuencia de muestreo", "Fréquence d'échantillonnage"};
  static constexpr Msg AudioLatency = {"Audio Latency", "オーディオレイテンシ", "音频延迟", "오디오 지연", "Latencia de audio", "Latence audio"};
  static constexpr Msg MuteFM = {"Mute FM", "FM消音", "FM静音", "FM 음소거", "Silenciar FM", "Couper le son FM"};
  static constexpr Msg MuteSSG = {"Mute SSG", "SSG消音", "SSG静音", "SSG 음소거", "Silenciar SSG", "Couper le son SSG"};
  static constexpr Msg MuteADPCM = {"Mute ADPCM", "ADPCM消音", "ADPCM静音", "ADPCM 음소거", "Silenciar ADPCM", "Couper le son ADPCM"};
  static constexpr Msg MuteRhythm = {"Mute Rhythm", "リズム消音", "节奏音静音", "리듬 음소거", "Silenciar ritmo", "Couper le son du rythme"};
  static constexpr Msg DumpMemory = {"Dump Memory...", "メモリダンプ...", "导出内存...", "메모리 덤프...", "Volcar memoria...", "Vider la mémoire..."};
  static constexpr Msg Debug = {"Debug", "デバッグ", "调试", "디버그", "Depuración", "Débogage"};
  static constexpr Msg LanguageLabel = {"Language", "言語", "语言", "언어", "Idioma", "Langue"};
  static constexpr Msg LangEn = {"English", "英語", "英语", "영어", "Inglés", "Anglais"};
  static constexpr Msg LangJp = {"Japanese", "日本語", "中文(日语)", "일본어", "Japonés", "Japonais"};
  static constexpr Msg LangZh = {"Chinese (Simplified)", "中国語 (簡体字)", "简体中文", "중국어 (간체)", "Chino (Simplificado)", "Chinois (Simplifié)"};
  static constexpr Msg LangKo = {"Korean", "韓国語", "韩语", "한국어", "Coreano", "Coréen"};
  static constexpr Msg LangEs = {"Spanish", "スペイン語", "西班牙语", "스페인어", "Español", "Espagnol"};
  static constexpr Msg LangFr = {"French", "フランス語", "法语", "프랑스어", "Francés", "Français"};
  static constexpr Msg SpeedLabel = {"Speed: x%.2g", "速度: x%.2g", "速度: x%.2g", "속도: x%.2g", "Velocidad: x%.2g", "Vitesse: x%.2g"};
  static constexpr Msg SpeedLabelInt = {"Speed: x%d", "速度: x%d", "速度: x%d", "속도: x%d", "Velocidad: x%d", "Vitesse: x%d"};
  static constexpr Msg FullSpeedLabel = {"FULL SPEED", "フルスピード", "全速", "최고 속도", "VELOCIDAD MÁXIMA", "VITESSE MAXIMALE"};
  static constexpr Msg VolumeLabel = {"Vol:", "音量:", "音量:", "음량:", "Vol:", "Vol:"};
  static constexpr Msg FPSView = {"FPS: %.1f", "表示: %.1f", "帧率: %.1f", "표시: %.1f", "FPS: %.1f", "IPS: %.1f"};
  static constexpr Msg FPSCore = {"Core: %.1f", "実行: %.1f", "核心: %.1f", "실행: %.1f", "Núcleo: %.1f", "Cœur: %.1f"};
}

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

// Convert UTF-8 path text to filesystem path on Windows via UTF-16.
static fs::path utf8_to_fs_path(const char* utf8) {
  if (!utf8) utf8 = "";
  int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, NULL, 0);
  if (wlen <= 0) {
    return fs::path();
  }
  std::wstring wpath((size_t)wlen - 1, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &wpath[0], wlen);
  return fs::path(wpath);
}

// Convert native TCHAR path (config/VM side) to UTF-8 (UI side).
static std::string tchar_path_to_utf8(const _TCHAR* path) {
  if (!path) return std::string();
#if defined(_UNICODE) && defined(SUPPORT_TCHAR_TYPE)
  return wstring_to_utf8(path);
#else
  int wlen = MultiByteToWideChar(CP_ACP, 0, path, -1, NULL, 0);
  if (wlen <= 0) return std::string(path);
  std::wstring wpath((size_t)wlen - 1, L'\0');
  MultiByteToWideChar(CP_ACP, 0, path, -1, &wpath[0], wlen);
  return wstring_to_utf8(wpath);
#endif
}
#else
// On non-Windows, just use string() (assumes UTF-8 locale)
static std::string path_to_utf8(const fs::path& p) {
#ifdef __APPLE__
  return nfd_to_nfc(p.string());
#else
  return p.string();
#endif
}

static fs::path utf8_to_fs_path(const char* utf8) {
  return fs::path(utf8 ? utf8 : "");
}

static std::string tchar_path_to_utf8(const _TCHAR* path) {
  const char* cpath = tchar_to_char(path);
  std::string result = std::string(cpath ? cpath : "");
#ifdef __APPLE__
  return nfd_to_nfc(result);
#else
  return result;
#endif
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
  pending_memdump = false;
  native_dialog_open = false;
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
  vm_screen_buffer = NULL;
  vm_screen_width = 0;
  vm_screen_height = 0;
  show_state_dialog = false;
  state_dialog_selected = 0;
  for (int i = 0; i < 10; i++) {
    state_thumb_tex[i] = NULL;
    state_thumb_w[i] = 0;
    state_thumb_h[i] = 0;
  }
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

  // Detect system language
  int locale_count = 0;
  SDL_Locale **locales = SDL_GetPreferredLocales(&locale_count);
  if (locales) {
    for (int i = 0; i < locale_count; i++) {
      if (!locales[i]->language) continue;
      std::string lang = locales[i]->language;
      OSD_LOG("Preferred locale: %s", lang.c_str());
      if (lang == "ja") {
        current_lang = Language::JAPANESE;
        OSD_LOG("Switching UI language to Japanese");
        break;
      } else if (lang == "zh") {
        current_lang = Language::CHINESE;
        OSD_LOG("Switching UI language to Chinese");
        break;
      } else if (lang == "ko") {
        current_lang = Language::KOREAN;
        OSD_LOG("Switching UI language to Korean");
        break;
      } else if (lang == "es") {
        current_lang = Language::SPANISH;
        OSD_LOG("Switching UI language to Spanish");
        break;
      } else if (lang == "fr") {
        current_lang = Language::FRENCH;
        OSD_LOG("Switching UI language to French");
        break;
      }
    }
    SDL_free(locales);
  }

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
  release_state_thumbnails();
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
    float mul = config.cpu_power;
    if (mul < 0.25f) mul = 1.0f;
    if (mul > 16.0f) mul = 16.0f;
    desired_ratio = mul;
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
  (void)SDL_SetAudioStreamGain(new_stream, config.master_volume / 100.0f);
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
    if (event.type == SDL_EVENT_DROP_FILE) {
      if (event.drop.data) {
        pending_dd_path = event.drop.data;
      }
    }
    const bool is_key_event =
        (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP);
    const bool block_vm_keydown =
        ui_interacting;

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

    // Process pending memory dump from folder dialog callback
    if (pending_memdump && !pending_memdump_dir.empty()) {
      std::time_t t = std::time(nullptr);
      std::tm tm_local;
#if defined(_WIN32)
      localtime_s(&tm_local, &t);
#else
      localtime_r(&t, &tm_local);
#endif
      char stamp[32];
      std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &tm_local);
      std::string dir = pending_memdump_dir + "/BubiC_memdump_" + stamp;
      bool ok = vm ? vm->dump_memory(dir.c_str()) : false;
      fprintf(stderr, "%s %s\n",
              ok ? "Memory dump written to"
                 : "Memory dump FAILED at",
              dir.c_str());
      pending_memdump = false;
      pending_memdump_dir.clear();
    }

    // Draw Status Bar at the bottom
    const float status_height = 24.0f;
    if (ui_visible) {
      draw_status_bar();
    }

    if (show_state_dialog) {
      draw_state_dialog();
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

    process_pending_insert();
    process_pending_dd();
    process_pending_save();
    process_pending_screenshot();

    // Pause emulation only when settings UI is actually open.
    // Hovering the menu bar should not pause the VM.
    uint32_t next_reason = UI_REASON_NONE;
    if (menu_tree_open) {
      next_reason |= UI_REASON_MENU_TREE;
    }
    if (native_dialog_open) {
      next_reason |= UI_REASON_NATIVE_DIALOG;
    }
    if (show_state_dialog) {
      next_reason |= UI_REASON_MENU_TREE;
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

  ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                           ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse |
                           ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                           ImGuiWindowFlags_NoBringToFrontOnFocus;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 2));
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
      ImGui::AlignTextToFramePadding();
      ImGui::Text("FD%d:", i + 1);
      ImVec2 text_min = ImGui::GetItemRectMin();
      ImVec2 text_max = ImGui::GetItemRectMax();
      float text_center_y = (text_min.y + text_max.y) * 0.5f + 1;
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

    // Volume slider (right of FD1).
    ImGui::SameLine(0.0f, 16.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted((const char*)Lang::VolumeLabel);
    ImGui::SameLine(0.0f, 4.0f);
    ImGui::PushItemWidth(100.0f);
    int vol = config.master_volume;
    if (ImGui::SliderInt("##master_volume", &vol, 0, 100, "%d%%")) {
      if (vol < 0) vol = 0;
      if (vol > 100) vol = 100;
      config.master_volume = vol;
      if (audio_stream) {
        (void)SDL_SetAudioStreamGain(audio_stream, config.master_volume / 100.0f);
      }
    }
    ImGui::PopItemWidth();

    uint64_t now_tick = SDL_GetTicks();

    // Right-aligned metrics (avoid overlap regardless of text length).
    // Decay emu FPS when no VM frames are being advanced.
    if (last_emu_progress_tick != 0 &&
        (now_tick - last_emu_progress_tick) > 1000 &&
        emu_frames_accum == 0) {
      emu_fps = 0.0f;
    }

    char fps_text[128];
    char view_text[64], core_text[64];
    snprintf(view_text, sizeof(view_text), (const char*)Lang::FPSView, current_fps);
    snprintf(core_text, sizeof(core_text), (const char*)Lang::FPSCore, emu_fps);
    snprintf(fps_text, sizeof(fps_text), "%s  %s", view_text, core_text);

    char clock_text[64] = "";
    const char *boot_str = "";
#if defined(PC8001_VARIANT)
    if (config.boot_mode == 0) boot_str = "V1";
    else if (config.boot_mode == 1) boot_str = "V2";
    else if (config.boot_mode == 2) boot_str = "N";
#else
    if (config.boot_mode == 0) boot_str = "V1S";
    else if (config.boot_mode == 1) boot_str = "V1H";
    else if (config.boot_mode == 2) boot_str = "V2";
    else if (config.boot_mode == 3) boot_str = "N";
    else if (config.boot_mode == 4) boot_str = "V2CD";
#endif

    const char *cpu_str = "4MHz";
#ifdef SUPPORT_PC88_HIGH_CLOCK
    if (config.cpu_type == 0 || config.cpu_type == 2) {
      cpu_str = "8MHz";
    }
#endif
    char speed_suffix[32] = "";
    if (config.cpu_power != 1.0f && !config.full_speed) {
      if (config.cpu_power < 1.0f)
        snprintf(speed_suffix, sizeof(speed_suffix), " x%.2g", config.cpu_power);
      else
        snprintf(speed_suffix, sizeof(speed_suffix), " x%d", (int)config.cpu_power);
    }
    snprintf(clock_text, sizeof(clock_text), "[%s] %s%s", boot_str, cpu_str, speed_suffix);

    char speed_text[64];
    if (config.full_speed) {
      snprintf(speed_text, sizeof(speed_text), "%s", (const char*)Lang::FullSpeedLabel);
    } else {
      speed_text[0] = '\0';
    }

    float right = ImGui::GetWindowWidth() - 12.0f;

    float fps_w = ImGui::CalcTextSize(fps_text).x;
    ImGui::SameLine(right - fps_w);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", fps_text);
    right -= (fps_w + 16.0f);

    if (clock_text[0] != '\0') {
      float clock_w = ImGui::CalcTextSize(clock_text).x;
      ImGui::SameLine(right - clock_w);
      ImGui::AlignTextToFramePadding();
      ImGui::Text("%s", clock_text);
      right -= (clock_w + 16.0f);
    }

    if (speed_text[0] != '\0') {
      float speed_w = ImGui::CalcTextSize(speed_text).x;
      ImGui::SameLine(right - speed_w);
      ImGui::AlignTextToFramePadding();
      if (config.full_speed) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "%s", speed_text);
      } else {
        ImGui::Text("%s", speed_text);
      }
      right -= (speed_w + 24.0f);
    }

    ImGui::End();
  }
  ImGui::PopStyleColor();
  ImGui::PopStyleVar();
}

std::string OSD::thumbnail_path_for_state(const _TCHAR *state_path) {
  std::string s(tchar_to_char(state_path));
  s += ".bmp";
  return s;
}

void OSD::release_state_thumbnails() {
  for (int i = 0; i < 10; i++) {
    if (state_thumb_tex[i]) {
      SDL_DestroyTexture(state_thumb_tex[i]);
      state_thumb_tex[i] = NULL;
    }
    state_thumb_w[i] = 0;
    state_thumb_h[i] = 0;
  }
}

void OSD::refresh_state_thumbnails() {
  release_state_thumbnails();
  if (!emu || !renderer) return;
  for (int i = 0; i < 10; i++) {
    _TCHAR state_path_copy[_MAX_PATH];
    my_tcscpy_s(state_path_copy, _MAX_PATH, emu->state_file_path(i));
    std::string thumb = thumbnail_path_for_state(state_path_copy);
    SDL_Surface *surf = SDL_LoadBMP(thumb.c_str());
    if (!surf) continue;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
    if (tex) {
      SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
      state_thumb_tex[i] = tex;
      state_thumb_w[i] = surf->w;
      state_thumb_h[i] = surf->h;
    }
    SDL_DestroySurface(surf);
  }
}

void OSD::save_state_thumbnail_for_slot(int slot) {
  if (!emu || !vm_screen_buffer || vm_screen_width <= 0 || vm_screen_height <= 0) return;
  _TCHAR state_path_copy[_MAX_PATH];
  my_tcscpy_s(state_path_copy, _MAX_PATH, emu->state_file_path(slot));
  std::string thumb = thumbnail_path_for_state(state_path_copy);

  SDL_Surface *src = SDL_CreateSurfaceFrom(
      vm_screen_width, vm_screen_height, SDL_PIXELFORMAT_XRGB8888,
      vm_screen_buffer, vm_screen_width * (int)sizeof(scrntype_t));
  if (!src) return;

  const int tw = 256;
  const int th = 160;
  SDL_Surface *dst = SDL_CreateSurface(tw, th, SDL_PIXELFORMAT_XRGB8888);
  if (!dst) { SDL_DestroySurface(src); return; }
  SDL_Rect src_rect = { 0, 0, vm_screen_width, vm_screen_height };
  SDL_Rect dst_rect = { 0, 0, tw, th };
  SDL_BlitSurfaceScaled(src, &src_rect, dst, &dst_rect, SDL_SCALEMODE_LINEAR);
  SDL_SaveBMP(dst, thumb.c_str());
  SDL_DestroySurface(dst);
  SDL_DestroySurface(src);
}

void OSD::open_state_dialog() {
  show_state_dialog = true;
  state_dialog_selected = 0;
  refresh_state_thumbnails();
}

void OSD::close_state_dialog() {
  show_state_dialog = false;
  release_state_thumbnails();
}

void OSD::draw_state_dialog() {
  ImGuiViewport *viewport = ImGui::GetMainViewport();
  const float dlg_w = 480.0f;
  const float dlg_h = 360.0f;
  ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f,
                                 viewport->Pos.y + viewport->Size.y * 0.5f),
                          ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(dlg_w, dlg_h), ImGuiCond_Appearing);

  bool open = true;
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
  ImGui::SetNextWindowSizeConstraints(ImVec2(dlg_w, dlg_h), ImVec2(FLT_MAX, FLT_MAX));
  if (!ImGui::Begin((const char *)Lang::StateDialogTitle, &open, flags)) {
    ImGui::End();
    if (!open) close_state_dialog();
    return;
  }

  const float btn_panel_w = 100.0f;
  const float thumb_w = 256.0f;
  const float thumb_h = 160.0f;
  const float row_h = thumb_h + 6.0f;

  // Compute dimensions for inner regions
  ImVec2 avail = ImGui::GetContentRegionAvail();
  float reserve_bottom = ImGui::GetFrameHeightWithSpacing();
  float list_w = avail.x - btn_panel_w - 12.0f;
  float list_h = avail.y - reserve_bottom;

  // Collect slot info
  bool slot_exists[10];
  char slot_time[10][40];
  _TCHAR slot_path[10][_MAX_PATH];
  for (int i = 0; i < 10; i++) {
    my_tcscpy_s(slot_path[i], _MAX_PATH, emu->state_file_path(i));
    slot_exists[i] = fs::exists(tchar_to_char(slot_path[i]));
    slot_time[i][0] = '\0';
    if (slot_exists[i]) {
      auto ftime = fs::last_write_time(tchar_to_char(slot_path[i]));
      auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
          ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
      std::time_t tt = std::chrono::system_clock::to_time_t(sctp);
      std::tm *local_t = std::localtime(&tt);
      if (local_t) std::strftime(slot_time[i], sizeof(slot_time[i]),
                                 "%Y/%m/%d %H:%M:%S", local_t);
    }
  }

  // Left: scrollable slot list
  ImGui::BeginChild("##state_list", ImVec2(list_w, list_h), true);
  for (int i = 0; i < 10; i++) {
    ImGui::PushID(i);
    bool selected = (state_dialog_selected == i);

    ImVec2 row_pos = ImGui::GetCursorScreenPos();
    if (ImGui::Selectable("##row", selected, ImGuiSelectableFlags_AllowOverlap,
                          ImVec2(0, row_h))) {
      state_dialog_selected = i;
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
      state_dialog_selected = i;
      if (slot_exists[i]) {
        emu->load_state(slot_path[i]);
        ImGui::PopID();
        ImGui::EndChild();
        ImGui::End();
        close_state_dialog();
        return;
      }
    }

    ImDrawList *dl = ImGui::GetWindowDrawList();
    float thumb_x = row_pos.x + 4.0f;
    float thumb_y = row_pos.y + 3.0f;
    ImVec2 tmin(thumb_x, thumb_y);
    ImVec2 tmax(thumb_x + thumb_w, thumb_y + thumb_h);
    if (state_thumb_tex[i]) {
      dl->AddImage((ImTextureID)(uintptr_t)state_thumb_tex[i], tmin, tmax);
    } else {
      dl->AddRectFilled(tmin, tmax, IM_COL32(40, 40, 40, 255));
    }
    dl->AddRect(tmin, tmax, IM_COL32(160, 160, 160, 255));

    // Slot number overlay (top-left)
    char slot_label[32];
    snprintf(slot_label, sizeof(slot_label), (const char *)Lang::Slot, i);
    ImVec2 ls = ImGui::CalcTextSize(slot_label);
    dl->AddRectFilled(
        ImVec2(tmin.x, tmin.y),
        ImVec2(tmin.x + ls.x + 12.0f, tmin.y + ls.y + 6.0f),
        IM_COL32(0, 0, 0, 180));
    dl->AddText(ImVec2(tmin.x + 6.0f, tmin.y + 3.0f),
                IM_COL32(255, 255, 255, 255), slot_label);

    // Datetime overlay (bottom strip)
    const char *time_text = slot_exists[i] ? slot_time[i] : (const char *)Lang::NoData;
    ImVec2 ts = ImGui::CalcTextSize(time_text);
    float strip_h = ts.y + 6.0f;
    dl->AddRectFilled(
        ImVec2(tmin.x, tmax.y - strip_h),
        ImVec2(tmax.x, tmax.y),
        IM_COL32(0, 0, 0, 180));
    dl->AddText(
        ImVec2(tmax.x - ts.x - 6.0f, tmax.y - strip_h + 3.0f),
        IM_COL32(255, 255, 255, 255), time_text);

    ImGui::PopID();
  }
  ImGui::EndChild();

  // Right: action buttons
  ImGui::SameLine();
  ImGui::BeginChild("##state_actions", ImVec2(btn_panel_w, list_h), false);
  int sel = state_dialog_selected;
  bool sel_exists = (sel >= 0 && sel < 10) ? slot_exists[sel] : false;

  if (ImGui::Button((const char *)Lang::SaveBtn, ImVec2(-FLT_MIN, 0))) {
    emu->save_state(slot_path[sel]);
    save_state_thumbnail_for_slot(sel);
    refresh_state_thumbnails();
  }
  ImGui::Spacing();
  ImGui::BeginDisabled(!sel_exists);
  if (ImGui::Button((const char *)Lang::LoadBtn, ImVec2(-FLT_MIN, 0))) {
    emu->load_state(slot_path[sel]);
    ImGui::EndDisabled();
    ImGui::EndChild();
    ImGui::End();
    close_state_dialog();
    return;
  }
  ImGui::EndDisabled();
  ImGui::Spacing();
  ImGui::BeginDisabled(!sel_exists);
  if (ImGui::Button((const char *)Lang::DeleteBtn, ImVec2(-FLT_MIN, 0))) {
    FILEIO::RemoveFile(slot_path[sel]);
    std::string thumb = thumbnail_path_for_state(slot_path[sel]);
    SDL_RemovePath(thumb.c_str());
    refresh_state_thumbnails();
  }
  ImGui::EndDisabled();
  ImGui::EndChild();

  // Close button at bottom
  ImGui::Separator();
  if (ImGui::Button((const char *)Lang::CloseBtn, ImVec2(100, 0))) {
    open = false;
  }

  ImGui::End();
  if (!open) close_state_dialog();
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

  load_font();

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

void OSD::load_font() {
  ImGuiIO &io = ImGui::GetIO();
  io.Fonts->Clear();

  const float font_size = 15.0f;

  // Helper to find first existing font from a list.
  // Use u8path + error_code: path strings are UTF-8 (some contain Japanese
  // characters for macOS fonts) and the default narrow path ctor on Windows
  // would interpret them via the system code page (CP932), throwing on
  // invalid sequences and aborting the process.
  auto find_font = [&](const std::vector<std::string>& paths) -> std::string {
    for (const auto& p : paths) {
      std::error_code ec;
      if (fs::exists(fs::u8path(p), ec)) return p;
    }
    return "";
  };

  // Font lists
  const std::vector<std::string> jp_fonts = {
    "/System/Library/Fonts/ヒラギノ角ゴシック W3.ttc",
    "C:\\Windows\\Fonts\\meiryo.ttc",
    "C:\\Windows\\Fonts\\msgothic.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"
  };
  const std::vector<std::string> zh_fonts = {
    "/System/Library/Fonts/PingFang.ttc",
    "/System/Library/Fonts/Hiragino Sans GB.ttc",
    "C:\\Windows\\Fonts\\simsun.ttc",
    "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc"
  };
  const std::vector<std::string> ko_fonts = {
    "/System/Library/Fonts/AppleSDGothicNeo.ttc",
    "C:\\Windows\\Fonts\\malgun.ttf",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"
  };

  ImFont* primary_font = nullptr;

  // 1. Load Primary Language Font
  std::string primary_path = "";
  const ImWchar* primary_ranges = io.Fonts->GetGlyphRangesDefault();

  if (current_lang == Language::JAPANESE) {
    primary_path = find_font(jp_fonts);
    primary_ranges = io.Fonts->GetGlyphRangesJapanese();
  } else if (current_lang == Language::CHINESE) {
    primary_path = find_font(zh_fonts);
    primary_ranges = io.Fonts->GetGlyphRangesChineseFull();
  } else if (current_lang == Language::KOREAN) {
    primary_path = find_font(ko_fonts);
    primary_ranges = io.Fonts->GetGlyphRangesKorean();
  }

  if (!primary_path.empty()) {
    OSD_LOG("Loading primary font: %s", primary_path.c_str());
    primary_font = io.Fonts->AddFontFromFileTTF(primary_path.c_str(), font_size, NULL, primary_ranges);
  }

  // 2. Merge Japanese Font for disk names if current language is NOT Japanese
  if (current_lang != Language::JAPANESE) {
    std::string jp_path = find_font(jp_fonts);
    if (!jp_path.empty()) {
      ImFontConfig font_cfg;
      // If no primary font was loaded, this Japanese font becomes the primary font (MergeMode = false)
      // If a primary font exists, we merge into it (MergeMode = true)
      font_cfg.MergeMode = (io.Fonts->Fonts.Size > 0);

      OSD_LOG("%s Japanese font for fallback: %s", font_cfg.MergeMode ? "Merging" : "Loading", jp_path.c_str());
      io.Fonts->AddFontFromFileTTF(jp_path.c_str(), font_size, &font_cfg, io.Fonts->GetGlyphRangesJapanese());
    }
  }

  // Fallback if nothing loaded
  if (!primary_font && io.Fonts->Fonts.Size == 0) {
    OSD_LOG("No CJK fonts found, using default font");
    ImFontConfig cfg;
    cfg.SizePixels = font_size;
    io.Fonts->AddFontDefault(&cfg);
  }

  // ImGui 1.92+ with ImGuiBackendFlags_RendererHasTextures handles atlas
  // building and texture upload incrementally — no explicit Build() or
  // DestroyDeviceObjects/CreateDeviceObjects is needed here. The renderer
  // backend will pick up the new fonts on the next frame.
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
    if (ImGui::BeginMenu(Lang::Control)) {
      menu_tree_open = true;
      if (ImGui::MenuItem(Lang::Reset)) {
        if (emu) emu->reset();
      }
      ImGui::Separator();
      if (ImGui::MenuItem("CPU x0.25", NULL, config.cpu_power == 0.25f)) { config.cpu_power = 0.25f; if(vm) vm->update_config(); }
      if (ImGui::MenuItem("CPU x0.5", NULL, config.cpu_power == 0.5f)) { config.cpu_power = 0.5f; if(vm) vm->update_config(); }
      if (ImGui::MenuItem("CPU x1", NULL, config.cpu_power == 1.0f)) { config.cpu_power = 1.0f; if(vm) vm->update_config(); }
      if (ImGui::MenuItem("CPU x2", NULL, config.cpu_power == 2.0f)) { config.cpu_power = 2.0f; if(vm) vm->update_config(); }
      if (ImGui::MenuItem("CPU x4", NULL, config.cpu_power == 4.0f)) { config.cpu_power = 4.0f; if(vm) vm->update_config(); }
      if (ImGui::MenuItem("CPU x8", NULL, config.cpu_power == 8.0f)) { config.cpu_power = 8.0f; if(vm) vm->update_config(); }
      if (ImGui::MenuItem("CPU x16", NULL, config.cpu_power == 16.0f)) { config.cpu_power = 16.0f; if(vm) vm->update_config(); }
      if (ImGui::MenuItem(Lang::FullSpeed, NULL, config.full_speed)) { config.full_speed = !config.full_speed; }
      ImGui::Separator();
      if (ImGui::MenuItem(Lang::RomajiToKana, NULL, config.romaji_to_kana)) { config.romaji_to_kana = !config.romaji_to_kana; }
      ImGui::Separator();
      if (ImGui::MenuItem(Lang::StateDialogMenu)) {
        open_state_dialog();
      }
      ImGui::Separator();
      if (ImGui::MenuItem(Lang::Exit)) {
        terminated = true;
      }
      ImGui::EndMenu();
    }

    for (int drv = 0; drv < 2; drv++) {
      char menu_label[16];
      snprintf(menu_label, sizeof(menu_label), drv == 0 ? "FD1" : "FD2");

      if (ImGui::BeginMenu(menu_label)) {
        menu_tree_open = true;
        if (ImGui::MenuItem(Lang::Insert)) { select_file(drv); }
        if (ImGui::MenuItem(Lang::Eject)) {
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
        if (ImGui::MenuItem(Lang::InsertBlank2D)) { select_save_file(drv, 0x00); }
        if (ImGui::MenuItem(Lang::InsertBlank2HD)) { select_save_file(drv, 0x20); }
        ImGui::Separator(); // ----
        bool inserted = (vm && vm->is_floppy_disk_inserted(drv));
        if (ImGui::MenuItem(Lang::WriteProtected, NULL, (vm && vm->is_floppy_disk_protected(drv)), inserted)) {
          if(vm) vm->is_floppy_disk_protected(drv, !vm->is_floppy_disk_protected(drv));
        }
        if (ImGui::MenuItem(Lang::CorrectTiming, NULL, config.correct_disk_timing[drv])) {
          config.correct_disk_timing[drv] = !config.correct_disk_timing[drv];
          if(vm) vm->update_config();
        }
        if (ImGui::MenuItem(Lang::IgnoreCRC, NULL, config.ignore_disk_crc[drv])) {
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
                snprintf(b_label, sizeof(b_label), (const char*)Lang::ImageN, b + 1);
              }
              if (ImGui::MenuItem(b_label, NULL, emu->d88_file[drv].cur_bank == b)) {
                if (vm) vm->open_floppy_disk(drv, emu->d88_file[drv].path, b);
                emu->floppy_disk_status[drv].bank = b;
                emu->d88_file[drv].cur_bank = b;
              }
            }
          }
        } else {
          ImGui::TextDisabled("%s", (const char*)Lang::NoDiskInserted);
        }

        ImGui::Separator(); // ----
        // 最近使ったファイル（ドライブごとに独立）
        if (ImGui::BeginMenu(Lang::RecentDisks)) {
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

    if (ImGui::BeginMenu(Lang::Device)) {
      menu_tree_open = true;
      if (ImGui::BeginMenu(Lang::Boot)) {
        if (ImGui::MenuItem("N88-V1(S) mode", NULL, config.boot_mode == 0)) { config.boot_mode = 0; if(emu) emu->reset(); }
        if (ImGui::MenuItem("N88-V1(H) mode", NULL, config.boot_mode == 1)) { config.boot_mode = 1; if(emu) emu->reset(); }
        if (ImGui::MenuItem("N88-V2 mode", NULL, config.boot_mode == 2)) { config.boot_mode = 2; if(emu) emu->reset(); }
        if (ImGui::MenuItem("N mode", NULL, config.boot_mode == 3)) { config.boot_mode = 3; if(vm) vm->update_config(); } // Keep VM update if intended
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu(Lang::CPU)) {
        if (ImGui::MenuItem("Z80 8MHz", NULL, config.cpu_type == 0)) { config.cpu_type = 0; if(vm) vm->update_config(); }
        if (ImGui::MenuItem("Z80 4MHz", NULL, config.cpu_type == 1)) { config.cpu_type = 1; if(vm) vm->update_config(); }
        if (ImGui::MenuItem("Z80 8MHz (FE2/MC)", NULL, config.cpu_type == 2)) { config.cpu_type = 2; if(vm) vm->update_config(); }
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu(Lang::Sound)) {
        bool is_opna = (config.sound_type == 0 || config.sound_type == 4 || config.sound_type == 5);
        if (ImGui::MenuItem("SOUND BOARD II", NULL, is_opna)) {
          config.sound_type = is_opna ? 1 : 0; // Toggle between OPNA(0) and OPN(1)
          if(emu) emu->reset();
        }
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu(Lang::Display)) {
        if (ImGui::MenuItem(Lang::HighResolution, NULL, config.monitor_type == 0)) { config.monitor_type = 0; if(vm) vm->update_config(); }
        if (ImGui::MenuItem(Lang::Standard, NULL, config.monitor_type == 1)) { config.monitor_type = 1; if(vm) vm->update_config(); }
        ImGui::Separator();
        if (ImGui::MenuItem(Lang::ScanlineAuto, NULL, config.scan_line_auto)) { config.scan_line_auto = !config.scan_line_auto; if(vm) vm->update_config(); }
        if (ImGui::MenuItem(Lang::Scanline, NULL, config.scan_line)) { config.scan_line = !config.scan_line; if(vm) vm->update_config(); }
        ImGui::Separator();
        if (ImGui::MenuItem(Lang::IgnorePalette, NULL, (config.dipswitch & (1 << 5)) != 0)) {
          config.dipswitch ^= (1 << 5); if(vm) vm->update_config();
        }
        ImGui::EndMenu();
      }
      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(Lang::Host)) {
      menu_tree_open = true;
      if (ImGui::BeginMenu(Lang::Screen)) {
        if (ImGui::MenuItem(Lang::Fullscreen, NULL, (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN))) {
          if (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) SDL_SetWindowFullscreen(window, false);
          else SDL_SetWindowFullscreen(window, true);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("x1.0", NULL, config.window_scale_idx == 0)) { config.window_scale_idx = 0; update_window_scale(); }
        if (ImGui::MenuItem("x1.5", NULL, config.window_scale_idx == 1)) { config.window_scale_idx = 1; update_window_scale(); }
        if (ImGui::MenuItem("x2.0", NULL, config.window_scale_idx == 2)) { config.window_scale_idx = 2; update_window_scale(); }
        if (ImGui::MenuItem("x2.5", NULL, config.window_scale_idx == 3)) { config.window_scale_idx = 3; update_window_scale(); }
        if (ImGui::MenuItem("x3.0", NULL, config.window_scale_idx == 4)) { config.window_scale_idx = 4; update_window_scale(); }
        ImGui::Separator();
        if (ImGui::MenuItem(Lang::SaveScreenshot)) { show_screenshot_dialog(); }
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu(Lang::Keyboard)) {
        if (ImGui::MenuItem(Lang::MapCursorToNumpad, NULL, config.cursor_as_numpad)) {
          config.cursor_as_numpad = !config.cursor_as_numpad;
        }
        if (ImGui::MenuItem(Lang::MapDigitToNumpad, NULL, config.digit_as_numpad)) {
          config.digit_as_numpad = !config.digit_as_numpad;
        }
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu(Lang::Sound)) {
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

        if (ImGui::BeginMenu(Lang::SamplingFrequency)) {
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
        if (ImGui::BeginMenu(Lang::AudioLatency)) {
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
      if (ImGui::BeginMenu(Lang::System)) {
        ImGui::MenuItem(Lang::ResetOnDD, NULL, &config.reset_on_dd);
        ImGui::EndMenu();
      }
      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(Lang::Debug)) {
      menu_tree_open = true;
      if (ImGui::MenuItem(Lang::DumpMemory)) {
        if (vm) {
          std::string home = get_home_directory();
          pending_memdump = true;
          native_dialog_open = true;
          SDL_ShowOpenFolderDialog([](void *userdata, const char * const *filelist, int filter) {
            OSD *osd = static_cast<OSD*>(userdata);
            if (filelist && filelist[0]) {
              osd->pending_memdump_dir = filelist[0];
            } else {
              osd->pending_memdump = false;
            }
            osd->native_dialog_open = false;
          }, this, window, home.c_str(), false);
        }
      }
      ImGui::Separator();
      if (ImGui::MenuItem(Lang::MuteFM, NULL, config.sound_mute_fm)) {
        config.sound_mute_fm = !config.sound_mute_fm;
        if (vm)
          vm->update_mute();
      }
      if (ImGui::MenuItem(Lang::MuteSSG, NULL, config.sound_mute_ssg)) {
        config.sound_mute_ssg = !config.sound_mute_ssg;
        if (vm)
          vm->update_mute();
      }
      if (ImGui::MenuItem(Lang::MuteADPCM, NULL, config.sound_mute_adpcm)) {
        config.sound_mute_adpcm = !config.sound_mute_adpcm;
        if (vm)
          vm->update_mute();
      }
      if (ImGui::MenuItem(Lang::MuteRhythm, NULL, config.sound_mute_rhythm)) {
        config.sound_mute_rhythm = !config.sound_mute_rhythm;
        if (vm)
          vm->update_mute();
      }
      ImGui::Separator();
      if (ImGui::BeginMenu(Lang::LanguageLabel)) {
        if (ImGui::MenuItem(Lang::LangEn, NULL, current_lang == Language::ENGLISH)) { if (current_lang != Language::ENGLISH) { current_lang = Language::ENGLISH; load_font(); } }
        if (ImGui::MenuItem(Lang::LangJp, NULL, current_lang == Language::JAPANESE)) { if (current_lang != Language::JAPANESE) { current_lang = Language::JAPANESE; load_font(); } }
        if (ImGui::MenuItem(Lang::LangZh, NULL, current_lang == Language::CHINESE)) { if (current_lang != Language::CHINESE) { current_lang = Language::CHINESE; load_font(); } }
        if (ImGui::MenuItem(Lang::LangKo, NULL, current_lang == Language::KOREAN)) { if (current_lang != Language::KOREAN) { current_lang = Language::KOREAN; load_font(); } }
        if (ImGui::MenuItem(Lang::LangEs, NULL, current_lang == Language::SPANISH)) { if (current_lang != Language::SPANISH) { current_lang = Language::SPANISH; load_font(); } }
        if (ImGui::MenuItem(Lang::LangFr, NULL, current_lang == Language::FRENCH)) { if (current_lang != Language::FRENCH) { current_lang = Language::FRENCH; load_font(); } }
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

  ImGui::Render();
  ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
}

void OSD::select_file(int drive) {
  pending_drive = drive;
  static const SDL_DialogFileFilter filters[] = {
    { "Disk Images", "d88;D88;d77;D77;2hd;2HD;2d;2D" },
    { "All Files", "*" },
  };
  std::string default_loc = tchar_path_to_utf8(config.last_browser_path);
  if (default_loc.empty()) default_loc = get_home_directory();
  native_dialog_open = true;
  SDL_ShowOpenFileDialog([](void *userdata, const char * const *filelist, int filter) {
    OSD *osd = static_cast<OSD*>(userdata);
    if (filelist && filelist[0]) {
      osd->pending_insert_path = filelist[0];
    }
    osd->native_dialog_open = false;
  }, this, window, filters, 2, default_loc.c_str(), false);
}

void OSD::process_pending_insert() {
  if (pending_insert_path.empty()) return;

  std::string full_path = pending_insert_path;
  pending_insert_path.clear();

  // Update last_browser_path from the selected file's directory
  fs::path selected_dir = utf8_to_fs_path(full_path.c_str()).parent_path();
  std::string dir_utf8 = path_to_utf8(selected_dir);
  my_tcscpy_s(config.last_browser_path, _MAX_PATH, utf8_path_to_tchar(dir_utf8.c_str()));

  fs::path file_p = utf8_to_fs_path(full_path.c_str());
  std::string display_name = path_to_utf8(file_p.filename());

  if (pending_drive == 0) {
    // FD1: Always insert the first image (Bank 0)
    if (vm) {
      vm->open_floppy_disk(0, utf8_path_to_tchar(full_path.c_str()), 0);
      my_tcscpy_s(fd1_path, _MAX_PATH, utf8_path_to_tchar(display_name.c_str()));

      if (emu) {
        my_tcscpy_s(emu->floppy_disk_status[0].path, _MAX_PATH, utf8_path_to_tchar(full_path.c_str()));
        emu->floppy_disk_status[0].bank = 0;
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
}

void OSD::process_pending_dd() {
  if (pending_dd_path.empty()) return;

  std::string full_path = pending_dd_path;
  pending_dd_path.clear();

  // Update last_browser_path from the selected file's directory
  fs::path selected_dir = utf8_to_fs_path(full_path.c_str()).parent_path();
  std::string dir_utf8 = path_to_utf8(selected_dir);
  my_tcscpy_s(config.last_browser_path, _MAX_PATH, utf8_path_to_tchar(dir_utf8.c_str()));

  fs::path file_p = utf8_to_fs_path(full_path.c_str());
  std::string display_name = path_to_utf8(file_p.filename());

  if (vm && emu) {
    // FD1: Always insert the first image (Bank 0)
    vm->open_floppy_disk(0, utf8_path_to_tchar(full_path.c_str()), 0);
    my_tcscpy_s(fd1_path, _MAX_PATH, utf8_path_to_tchar(display_name.c_str()));
    my_tcscpy_s(emu->floppy_disk_status[0].path, _MAX_PATH, utf8_path_to_tchar(full_path.c_str()));
    emu->floppy_disk_status[0].bank = 0;
    int banks = get_disk_names(full_path.c_str(), 0, emu);
    emu->d88_file[0].cur_bank = 0;
    add_recent_disk(utf8_path_to_tchar(full_path.c_str()), 0);

    // If file has a second image, insert Bank 1 to FD2 (replacing existing)
    if (banks >= 2) {
      vm->open_floppy_disk(1, utf8_path_to_tchar(full_path.c_str()), 1);
      my_tcscpy_s(fd2_path, _MAX_PATH, utf8_path_to_tchar(display_name.c_str()));
      my_tcscpy_s(emu->floppy_disk_status[1].path, _MAX_PATH, utf8_path_to_tchar(full_path.c_str()));
      emu->floppy_disk_status[1].bank = 1;
      get_disk_names(full_path.c_str(), 1, emu);
      emu->d88_file[1].cur_bank = 1;
      add_recent_disk(utf8_path_to_tchar(full_path.c_str()), 1);
    }

    if (config.reset_on_dd) {
      emu->reset();
    }
  }
}

void OSD::select_save_file(int drive, int type) {
  pending_drive = drive;
  pending_blank_type = type;
  static const SDL_DialogFileFilter filters[] = {
    { "D88 Disk Image", "d88;D88" },
  };
  std::string default_loc = tchar_path_to_utf8(config.last_browser_path);
  if (default_loc.empty()) default_loc = get_home_directory();
  native_dialog_open = true;
  SDL_ShowSaveFileDialog([](void *userdata, const char * const *filelist, int filter) {
    OSD *osd = static_cast<OSD*>(userdata);
    if (filelist && filelist[0]) {
      osd->pending_save_path = filelist[0];
    }
    osd->native_dialog_open = false;
  }, this, window, filters, 1, default_loc.c_str());
}

void OSD::show_screenshot_dialog() {
  // Build default filename: AppName_DiskName_DateTime.bmp
  std::string basename = "BubiC-8801MA";
  if (emu && emu->floppy_disk_status[0].path[0] != '\0') {
    fs::path disk_p(tchar_to_char(emu->floppy_disk_status[0].path));
    std::string stem = path_to_utf8(disk_p.stem());
    if (!stem.empty()) {
      basename += "_" + stem;
    }
  }
  // Append timestamp: YYYYMMDD_HHMMSS
  {
    time_t now = time(NULL);
    struct tm lt;
#if defined(_WIN32)
    localtime_s(&lt, &now);
#else
    localtime_r(&now, &lt);
#endif
    char ts[20];
    snprintf(ts, sizeof(ts), "%04d%02d%02d_%02d%02d%02d",
             lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
             lt.tm_hour, lt.tm_min, lt.tm_sec);
    basename += "_";
    basename += ts;
  }
  basename += ".bmp";

  fs::path default_path = fs::path(get_home_directory()) / basename;
  std::string default_loc = path_to_utf8(default_path);

  static const SDL_DialogFileFilter filters[] = {
    { "BMP Image", "bmp" },
  };

  native_dialog_open = true;
  SDL_ShowSaveFileDialog([](void *userdata, const char * const *filelist, int filter) {
    OSD *osd = static_cast<OSD*>(userdata);
    if (filelist && filelist[0]) {
      std::lock_guard<std::mutex> lock(osd->screenshot_mutex);
      osd->pending_screenshot_path = filelist[0];
    }
    osd->native_dialog_open = false;
  }, this, window, filters, 1, default_loc.c_str());
}

void OSD::process_pending_screenshot() {
  std::string path_str;
  {
    std::lock_guard<std::mutex> lock(screenshot_mutex);
    if (pending_screenshot_path.empty()) return;
    path_str = pending_screenshot_path;
    pending_screenshot_path.clear();
  }

  // Ensure .bmp extension.
  fs::path save_p = utf8_to_fs_path(path_str.c_str());
  std::string ext = path_to_utf8(save_p.extension());
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  if (ext != ".bmp") {
    path_str += ".bmp";
  }

  // Build surface from the VM's internal screen buffer (no UI overlay).
  if (!vm_screen_buffer || vm_screen_width <= 0 || vm_screen_height <= 0) {
    OSD_LOG("Screenshot failed: no screen buffer");
    return;
  }

  SDL_Surface *surface = SDL_CreateSurfaceFrom(
      vm_screen_width, vm_screen_height, SDL_PIXELFORMAT_XRGB8888,
      vm_screen_buffer, vm_screen_width * (int)sizeof(scrntype_t));
  if (!surface) {
    OSD_LOG("Screenshot surface creation failed: %s", SDL_GetError());
    return;
  }

  if (SDL_SaveBMP(surface, path_str.c_str())) {
    OSD_LOG("Screenshot saved: %s", path_str.c_str());
  } else {
    OSD_LOG("Screenshot save failed: %s", SDL_GetError());
  }

  SDL_DestroySurface(surface);
}

void OSD::process_pending_save() {
  if (pending_save_path.empty()) return;

  std::string path_str = pending_save_path;
  pending_save_path.clear();

  // Ensure .d88 extension
  fs::path save_p = utf8_to_fs_path(path_str.c_str());
  std::string ext = path_to_utf8(save_p.extension());
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  if (ext != ".d88") {
    path_str += ".d88";
    save_p = utf8_to_fs_path(path_str.c_str());
  }

  // Update last_browser_path
  std::string dir_utf8 = path_to_utf8(save_p.parent_path());
  my_tcscpy_s(config.last_browser_path, _MAX_PATH, utf8_path_to_tchar(dir_utf8.c_str()));

  std::string filename_str = path_to_utf8(save_p.filename());

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
  }
}
