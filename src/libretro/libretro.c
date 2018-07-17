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
#include "../plat_midi.h"
#include "../sound/sound.h"
#include "../video/video.h"

static uint32_t *frame_buf;
static struct retro_log_callback logging;
static retro_log_printf_t log_cb;

static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_environment_t environ_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

static uint32_t video_buffer[2048 * 2048];

/* forward declarations */
void pc_reset_hard_close(void);
int nvr_save(void);
void pc_thread(void *param);
void pc_reset_hard_init(void);

static void fallback_log(enum retro_log_level level, const char *fmt, ...)
{
   (void)level;
   va_list va;
   va_start(va, fmt);
   vfprintf(stderr, fmt, va);
   va_end(va);
}

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

void plat_midi_init(void)
{
}

void plat_midi_close(void)
{
}

void plat_midi_play_msg(uint8_t* val)
{
}

void plat_midi_play_sysex(uint8_t* data, unsigned int len)
{
}

int	plat_midi_write(uint8_t val)
{
    return 0;
}

int	plat_midi_get_num_devs()
{
    return 0;
}

void plat_midi_get_dev_name(int num, char *s)
{
}

FILE * plat_fopen(wchar_t *path, wchar_t *mode)
{
    return fopen(path, mode);
}

void initalmain(int argc, char *argv[])
{
}

void closeal()
{
}

void inital()
{
}

void check()
{
}

void givealbuffer_cd(void *buf)
{
}

void givealbuffer(void *buf)
{
}

uint64_t plat_timer_read()
{
        return 0;
}

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

void retro_deinit(void)
{
}

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

void retro_get_system_info(struct retro_system_info *info)
{
   memset(info, 0, sizeof(*info));
   info->library_name     = "86Box";
   info->library_version  = "v2.00 beta";
   info->need_fullpath    = false;
   info->valid_extensions = NULL; // Anything is fine, we don't care.
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   float aspect = 4.0f / 3.0f;
   float sampling_rate = 44100.0f;

   info->timing = (struct retro_system_timing) {
      .fps = 60.0,
      .sample_rate = sampling_rate,
   };

   info->geometry = (struct retro_game_geometry) {
      .base_width   = 320,
      .base_height  = 240,
      .max_width    = 2048,
      .max_height   = 2048,
      .aspect_ratio = aspect,
   };
}

void retro_set_environment(retro_environment_t cb)
{
   environ_cb = cb;

   bool no_content = true;
   cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_content);

   if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging))
      log_cb = logging.log;
   else
      log_cb = fallback_log;
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
   audio_cb = cb;
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
}

static void audio_callback(void)
{
}

void retro_run(void)
{
}

bool retro_load_game(const struct retro_game_info *info)
{
   enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
   if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
   {
      log_cb(RETRO_LOG_INFO, "XRGB8888 is not supported.\n");
      return false;
   }

   pc_reset_hard_init();

   (void)info;
   return true;
}

void retro_unload_game(void)
{
   pc_reset_hard_close();
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
{
}

void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
   (void)index;
   (void)enabled;
   (void)code;
}