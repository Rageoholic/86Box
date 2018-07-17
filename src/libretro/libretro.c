#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

#include "libretro.h"

#include "../86box.h"
#include "../cpu/cpu.h"
#include "../machine/machine.h"
#include "../nvr.h"
#include "../plat.h"
#include "../sound/sound.h"
#include "../video/video.h"

static struct retro_log_callback logging;
static retro_log_printf_t log_cb;
static bool use_audio_cb;
static float last_aspect;
static float last_sample_rate;

int quited = 0;

int pcem_key[272];
int rawinputkey[272];

static retro_video_refresh_t video_cb;

/* forward declarations */
void closepc(void);
void savenvr(void);
int loadbios(void);
void initpc(int argc, char *argv[]);
void runpc(void);
void resetpchard(void);

wchar_t *
plat_get_extension(wchar_t *s)
{
    return L"";
}

void set_window_title(char *s)
{
}

void get_executable_name(char *s, int size)
{
}

static int key_convert[322] =
{
	  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
	0x0e,0x0f,  -1,  -1,  -1,0x1c,  -1,  -1,
	  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
	  -1,  -1,  -1,0x01,  -1,  -1,  -1,  -1,
	0x39,0x02,0x28,0x04,  -1,0x05,0x08,0x28,
	0x0a,0x0b,0x09,0x0d,0x33,0x0c,0x34,0x35,
	0x0b,0x02,0x03,0x04,0x05,0x06,0x07,0x08,
	0x09,0x0a,0x27,0x27,0x33,0x0d,0x34,0x35,
	0x03,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
	  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
	  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
	  -1,  -1,  -1,0x1a,0x2b,0x1b,0x07,0x0c,
	0x29,0x1e,0x30,0x2e,0x20,0x12,0x21,0x22,
	0x23,0x17,0x24,0x25,0x26,0x32,0x31,0x18,
	0x19,0x10,0x13,0x1f,0x14,0x16,0x2f,0x11,
	0x2d,0x15,0x2c,  -1,  -1,  -1,  -1,0xd3,
	  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
	  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
	  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
	  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
	  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
	  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
	  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
	  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
	  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
	  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
	  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
	  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
	  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
	  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
	  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
	  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
	0x52,0x4f,0x50,0x51,0x4b,0x4c,0x4d,0x47,
	0x48,0x49,0x53,0xb5,0x37,0x4a,0x4e,  -1,
	  -1,0xc8,0xd0,0xcb,0xcd,0xd2,0xc7,0xcf,
	0xc9,0xd1,0x3b,0x3c,0x3d,0x3e,0x3f,0x40,
	0x41,0x42,0x43,0x44,0x57,0x58,  -1,  -1,
	  -1,  -1,  -1,  -1,0x3a,  -1,0x45,  -1,
	  -1,0x9d,0x1d,0xb8,0x38,  -1,  -1,  -1,
	  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
};

static int ticks = 0;
static void timer_rout()
{
        ticks++;
}

uint64_t timer_freq;
uint64_t timer_read()
{
        return 0;
}

static void fallback_log(enum retro_log_level level, const char *fmt, ...)
{
   (void)level;
   va_list va;
   va_start(va, fmt);
   vfprintf(stderr, fmt, va);
   va_end(va);
}

static uint32_t video_buffer[2048 * 2048];

static void libretro_blit_memtoscreen(int x, int y, int y1, int y2, int w, int h)
{
   int xx, yy;
   uint32_t *p;

   if((w < 0) || (w > 2048) || (h < 0) || (h > 2048))
      return;

   memset(video_buffer, 0, 2048 * 2048 * sizeof(uint32_t));

   for (yy = y1; yy < y2; yy++)
   {
      if ((y + yy) >= 0 && (y + yy) < buffer32->h)
         memcpy(video_buffer + yy * 2048, &(((uint32_t *)buffer32->line[y + yy])[x]), w * 4);
   }

   video_cb(video_buffer, w, h, 2048 * 4);
}


void retro_deinit(void)
{
}

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
}

void retro_get_system_info(struct retro_system_info *info)
{
   memset(info, 0, sizeof(*info));
   info->library_name     = "86Box";
   info->library_version  = "v2.00 beta";
   info->need_fullpath    = false;
   info->valid_extensions = NULL; // Anything is fine, we don't care.
}

static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_environment_t environ_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

const char* retro_get_system_directory(void)
{
    const char* dir;
    environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir);

    return dir ? dir : ".";
}

void retro_init(void)
{
#ifdef _WIN32
   char slash = '\\';
#else
   char slash = '/';
#endif
   unsigned i;
   unsigned color_mode = RETRO_PIXEL_FORMAT_XRGB8888;
   const char *system_dir = NULL;

   environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &color_mode);

   system_dir = retro_get_system_directory();

   char pcempath[1024];

   sprintf(pcempath, "%s%c%s%c", system_dir, slash, "86box", slash);

   mbstowcs(exe_path, pcempath, sizeof_w(exe_path));

   /* video initialization */
   video_init();
   video_setblit(libretro_blit_memtoscreen);
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   info->timing.fps            = 60;
   info->timing.sample_rate    = 44100;
   info->geometry.base_width   = 320;
   info->geometry.base_height  = 240;
   info->geometry.max_width    = 2048;
   info->geometry.max_height   = 2048;
   info->geometry.aspect_ratio = (float)4/3;
}

static struct retro_rumble_interface rumble;

void retro_set_environment(retro_environment_t cb)
{
   environ_cb = cb;

   if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging))
      log_cb = logging.log;
   else
      log_cb = fallback_log;
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
   audio_cb = cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
   audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb)
{
   input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb)
{
   input_state_cb = cb;
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
   video_cb = cb;
}

void retro_reset(void)
{
   resetpchard();
}

static void check_variables(bool first_time_startup)
{
}

static void audio_set_state(bool enable)
{
   (void)enable;
}

void retro_run(void)
{
   static int ticks = 0;
   bool updated = false;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
      check_variables(false);

   input_poll_cb();

   if (quited)
      return;

   {
      runpc();
      frames++;
      if (frames >= 200 && nvr_dosave)
      {
         frames = 0;
         nvr_dosave = 0;
         savenvr();
      }
   }
   /* missing: audio_cb / video_cb */

}

static void keyboard_cb(bool down, unsigned keycode,
      uint32_t character, uint16_t mod)
{
   unsigned c;
   int key_idx;
   bool latch = down ? 1 : 0;


   log_cb(RETRO_LOG_INFO, "Down: %s, Code: %d, Char: %u, Mod: %u.\n",
         down ? "yes" : "no", keycode, character, mod);

   for(c = 0;c<272;c++)
      pcem_key[c] = 0;

   key_idx = key_convert[keycode];
   if (key_idx == -1)
      return;

   pcem_key[key_idx] = latch;
}

bool retro_load_game(const struct retro_game_info *info)
{
   int c, d;
   enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
   if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
   {
      log_cb(RETRO_LOG_INFO, "XRGB8888 is not supported.\n");
      return false;
   }

   struct retro_keyboard_callback cb = { keyboard_cb };
   environ_cb(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, &cb);

   check_variables(true);

   initpc(0, NULL);

	resetpchard();

   (void)info;
   return true;
}

void retro_unload_game(void)
{
   closepc();
}

unsigned retro_get_region(void)
{
   return RETRO_REGION_NTSC;
}

bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num)
{
   return retro_load_game(NULL);
}

size_t retro_serialize_size(void)
{
   return 0;
}

bool retro_serialize(void *data_, size_t size)
{
   return 0;
}

bool retro_unserialize(const void *data_, size_t size)
{
   return false;
}

void *retro_get_memory_data(unsigned id)
{
   (void)id;
   return NULL;
}

size_t retro_get_memory_size(unsigned id)
{
   (void)id;
   return 0;
}

void retro_cheat_reset(void)
{}

void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
   (void)index;
   (void)enabled;
   (void)code;
}

