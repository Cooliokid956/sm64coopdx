#include "types.h"
#include "seq_ids.h"
#include "audio/external.h"
#include "game/camera.h"
#include "engine/math_util.h"
#include "pc/mods/mods.h"
#include "pc/mods/mod_fs.h"
#include "pc/lua/smlua.h"
#include "pc/lua/utils/smlua_audio_utils.h"
#include "pc/mods/mods_utils.h"
#include "pc/utils/misc.h"
#include "pc/debuglog.h"
#include "pc/pc_main.h"
#include "pc/fs/fmem.h"
#include "audio/load.h"

struct AudioOverride {
    bool enabled;
    bool loaded;
    const char* filename;
    u64 length;
    u8 bank;
    u8* buffer;
};

struct AudioOverride sAudioOverrides[MAX_AUDIO_OVERRIDE] = { 0 };

static void smlua_audio_utils_reset(struct AudioOverride* override) {
    if (override == NULL) { return; }

    override->enabled = false;
    override->loaded = false;

    if (override->filename) {
        free((char*)override->filename);
        override->filename = NULL;
    }

    override->length = 0;
    override->bank = 0;

    if (override->buffer != NULL) {
        free((u8*)override->buffer);
        override->buffer = NULL;
    }
}

void smlua_audio_utils_reset_all(void) {
    audio_init();
    for (s32 i = 0; i < MAX_AUDIO_OVERRIDE; i++) {
#ifdef VERSION_EU
        if (sAudioOverrides[i].enabled) {
            if (i >= SEQ_EVENT_CUTSCENE_LAKITU) {
                sBackgroundMusicDefaultVolume[i] = 75;
                return;
            }
            sBackgroundMusicDefaultVolume[i] = sBackgroundMusicDefaultVolumeDefault[i];
        }
#else
        if (sAudioOverrides[i].enabled) { sound_reset_background_music_default_volume(i); }
#endif
        smlua_audio_utils_reset(&sAudioOverrides[i]);
    }
}

bool smlua_audio_utils_override(u8 sequenceId, s32* bankId, void** seqData) {
    if (sequenceId >= MAX_AUDIO_OVERRIDE) { return false; }
    struct AudioOverride* override = &sAudioOverrides[sequenceId];
    if (!override->enabled) { return false; }

    if (gOverrideBank > -1) { override->bank = gOverrideBank; }

    if (override->loaded) {
        *seqData = override->buffer;
        *bankId = override->bank;
        return true;
    }

    u8* buffer = NULL;
    u32 length = 0;

    if (is_mod_fs_file(override->filename)) {
        if (!mod_fs_read_file_from_uri(override->filename, (void **) &buffer, &length)) {
            return false;
        }
    } else {
        FILE* fp = f_open_r(override->filename);
        if (!fp) { return false; }
        f_seek(fp, 0L, SEEK_END);
        length = f_tell(fp);

        buffer = malloc(length+1);
        if (buffer == NULL) {
            LOG_ERROR("Failed to malloc m64 sound file");
            f_close(fp);
            f_delete(fp);
            return false;
        }

        f_seek(fp, 0L, SEEK_SET);
        f_read(buffer, length, 1, fp);

        f_close(fp);
        f_delete(fp);
    }

    if (!buffer || !length) {
        return false;
    }

    // cache
    override->loaded = true;
    override->buffer = buffer;
    override->length = length;

    *seqData = buffer;
    *bankId = override->bank;
    return true;
}

static void smlua_audio_utils_create_audio_override(u8 sequenceId, u8 bankId, u8 defaultVolume, const char *filepath) {
    struct AudioOverride* override = &sAudioOverrides[sequenceId];
    if (override->enabled) { audio_init(); }
    smlua_audio_utils_reset(override);
    LOG_INFO("Loading audio: %s", filepath);
    override->filename = strdup(filepath);
    override->enabled = true;
    override->bank = bankId;
    sound_set_background_music_default_volume(sequenceId, defaultVolume);
}

void smlua_audio_utils_replace_sequence(u8 sequenceId, u8 bankId, u8 defaultVolume, const char* m64Name) {
    if (gLuaActiveMod == NULL) { return; }
    if (sequenceId >= MAX_AUDIO_OVERRIDE) {
        LOG_LUA_LINE("Invalid sequenceId given to smlua_audio_utils_replace_sequence(): %d", sequenceId);
        return;
    }

    if (bankId >= 64) {
        LOG_LUA_LINE("Invalid bankId given to smlua_audio_utils_replace_sequence(): %d", bankId);
        return;
    }

    if (is_mod_fs_file(m64Name)) {
        smlua_audio_utils_create_audio_override(sequenceId, bankId, defaultVolume, m64Name);
        return;
    }

    char m64path[SYS_MAX_PATH] = { 0 };
    if (snprintf(m64path, SYS_MAX_PATH-1, "sound/%s.m64", m64Name) < 0) {
        LOG_LUA_LINE("Could not concat m64path: %s", m64path);
        return;
    }
    normalize_path(m64path);

    for (s32 i = 0; i < gLuaActiveMod->fileCount; i++) {
        struct ModFile* file = &gLuaActiveMod->files[i];
        char relPath[SYS_MAX_PATH] = { 0 };
        snprintf(relPath, SYS_MAX_PATH-1, "%s", file->relativePath);
        normalize_path(relPath);
        if (path_ends_with(relPath, m64path)) {
            smlua_audio_utils_create_audio_override(sequenceId, bankId, defaultVolume, file->cachedPath);
            return;
        }
    }

    LOG_LUA_LINE("Could not find m64 at path: %s", m64path);
}

u8 smlua_audio_utils_allocate_sequence(void) {
    for (u8 seqId = SEQ_COUNT + 1; seqId < MAX_AUDIO_OVERRIDE; seqId++) {
        if (!sAudioOverrides[seqId].enabled) {
            return seqId;
        }
    }
    LOG_ERROR("Cannot allocate more custom sequences.");
    return MAX_AUDIO_OVERRIDE;
}

  ///////////////
 // mod audio //
///////////////

// Optimization: disable spatialization for everything as it's not used
#define MA_SOUND_FLAGS (MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_DEFAULT_ATTACHMENT | MA_SOUND_FLAG_NO_PITCH) // Avoid resampling if possible

static ma_engine sModAudioEngine;
static const char* sModAudioTypes[] = { "sample", "stream" };
#define GET_TYPE_NAME(audio) (sModAudioTypes[MA_GET_TYPE(audio)])
static ma_sound_group sModAudioChannels[3];
static struct DynamicPool *sModAudioPool;


// MA calls the end callback from its audio thread
// Use mutexes to be sure we don't try to delete the same memory at the same time
#include <pthread.h>
static pthread_mutex_t sSoundCopyMutex = PTHREAD_MUTEX_INITIALIZER;
static struct ModAudio *sSoundCopyFreeTail = NULL;

// Called whenever a sample copy finishes playback (called from the miniaudio thread)
// removes the copy from its linked list, and adds it to the pending list
static void audio_destroy_copy(struct ModAudio* copy) {
    pthread_mutex_lock(&sSoundCopyMutex);

    if (copy->next) { copy->next->prev = copy->prev; }
    if (copy->prev) { copy->prev->next = copy->next; }
    if (!copy->next && !copy->prev) {
        // This is the last copy of this audio, clear the pointer to it
        copy->parent->copiesTail = NULL;
    }
    copy->next = NULL;
    copy->prev = NULL;

    // add copy to list
    if (sSoundCopyFreeTail) {
        copy->prev = sSoundCopyFreeTail;
        sSoundCopyFreeTail->next = copy;
    }
    sSoundCopyFreeTail = copy;

    pthread_mutex_unlock(&sSoundCopyMutex);
}

static void audio_destroy_copy_callback(void* userData, UNUSED ma_sound* sound) {
    audio_destroy_copy((struct ModAudio *)userData);
}

void audio_destroy_copies(struct ModAudio* node) {
    while (node) {
        struct ModAudio* prev = node->prev;
        ma_sound_uninit(&node->sound);
        smlua_free_audio_copy(node);
        node = prev;
    }
}

// Called every frame in the main thread from smlua_update()
// Frees all audio sample copies that are in the pending list
void audio_destroy_pending_copies(void) {
    if (sSoundCopyFreeTail) {
        pthread_mutex_lock(&sSoundCopyMutex);
        audio_destroy_copies(sSoundCopyFreeTail);
        sSoundCopyFreeTail = NULL;
        pthread_mutex_unlock(&sSoundCopyMutex);
    }
}

static void audio_destroy_all_copies(struct ModAudio* audio) {
    pthread_mutex_lock(&sSoundCopyMutex);
    audio_destroy_copies(audio->copiesTail);
    audio->copiesTail = NULL;
    pthread_mutex_unlock(&sSoundCopyMutex);
}

struct ModAudio* audio_copy(struct ModAudio* audio) {
    if (audio->copy) { audio = audio->parent; }

    struct ModAudio* copy = calloc(1, sizeof(struct ModAudio)); copy->copy = true;
    ma_result result = ma_decoder_init_memory(audio->buffer, audio->bufferSize, NULL, &copy->decoder);
    if (result != MA_SUCCESS) { return NULL; }
    result = ma_sound_init_from_data_source(&sModAudioEngine, &copy->decoder, MA_SOUND_FLAGS, NULL, &copy->sound);
    if (result != MA_SUCCESS) { return NULL; }
    ma_sound_set_end_callback(&copy->sound, audio_destroy_copy_callback, copy);
    copy->parent = audio;
    copy->flags |= audio->flags;
    audio_set_volume_channel(copy, copy->channel);

    // Add to list
    if (audio->copiesTail) {
        copy->prev = audio->copiesTail;
        audio->copiesTail->next = copy;
    }
    audio->copiesTail = copy;

    return copy;
}

static void smlua_audio_custom_init(void) {
    sModAudioPool = dynamic_pool_init();

    ma_result result = ma_engine_init(NULL, &sModAudioEngine);
    if (result != MA_SUCCESS) {
        LOG_ERROR("failed to init Miniaudio: %d", result);
    }

    for (u8 i = 0; i < 3; i++) {
        ma_sound_group_init(&sModAudioEngine, MA_SOUND_FLAG_NO_SPATIALIZATION, NULL, &sModAudioChannels[i]);
    }

    f32 musicVolume = (f32)configMusicVolume / 127.0f * (f32)gLuaVolumeLevel / 127.0f;
    f32 sfxVolume = (f32)configSfxVolume / 127.0f * (f32)gLuaVolumeSfx / 127.0f;
    f32 envVolume = (f32)configEnvVolume / 127.0f * (f32)gLuaVolumeEnv / 127.0f;
    ma_sound_group_set_volume(&sModAudioChannels[MA_CHANNEL_MUSIC], musicVolume);
    ma_sound_group_set_volume(&sModAudioChannels[MA_CHANNEL_SFX], sfxVolume);
    ma_sound_group_set_volume(&sModAudioChannels[MA_CHANNEL_ENV], envVolume);
}

static struct ModAudio* find_mod_audio(const char *filepath) {
    struct DynamicPoolNode* node = sModAudioPool->tail;
    while (node) {
        struct DynamicPoolNode* prev = node->prev;
        struct ModAudio* audio = node->ptr;
        if (strcmp(filepath, audio->filepath) == 0) { return audio; }
        node = prev;
    }
    return NULL;
}

static bool audio_sanity_check(struct ModAudio* audio, u8 type, const char* action) {
    if (!audio || !audio->loaded) {
        LOG_LUA_LINE("Tried to %s an unloaded audio %s", action, audio ? GET_TYPE_NAME(audio) : "(NULL)");
        return false;
    }
    if (type != MA_GET_TYPE(audio)) {
        LOG_LUA_LINE("Tried to %s a %s as a %s", action,
            GET_TYPE_NAME(audio),
            sModAudioTypes[type]);
        return false;
    }
    return true;
}

struct ModAudio* audio_load_internal(const char* filename, enum ModAudioType type) {
    if (!sModAudioPool) { smlua_audio_custom_init(); }

    // check file type
    bool validFileType = false;
    const char* fileTypes[] = { ".mp3", ".aiff", ".ogg", NULL };
    const char** ft = fileTypes;
    while (*ft != NULL) {
        if (path_ends_with(filename, *ft)) {
            validFileType = true;
            break;
        }
        ft++;
    }
    if (!validFileType) {
        LOG_LUA_LINE("Tried to load audio file with invalid file type: %s", filename);
        return NULL;
    }

    const char *filepath = filename;
    if (!is_mod_fs_file(filename)) {

        // normalize filename
        char normPath[SYS_MAX_PATH] = { 0 };
        snprintf(normPath, SYS_MAX_PATH, "%s", filename);
        normalize_path(normPath);

        // find mod file in mod list
        bool foundModFile = false;
        struct ModFile* modFile = NULL;
        u16 fileCount = gLuaActiveMod->fileCount;
        for (u16 i = 0; i < fileCount; i++) {
            struct ModFile* file = &gLuaActiveMod->files[i];
            if (path_ends_with(file->relativePath, normPath)) {
                foundModFile = true;
                modFile = file;
                break;
            }
        }
        if (!foundModFile) {
            LOG_LUA_LINE("Could not find audio file: '%s'", filename);
            return NULL;
        }
        filepath = modFile->cachedPath;
    }

    // find stream in ModAudio list
    struct ModAudio* audio = find_mod_audio(filepath);
    if (audio) {
        if (type == MA_GET_TYPE(audio)) {
            return audio;
        } else {
            LOG_LUA_LINE("Tried to load a %s, when a %s already exists for '%s'", sModAudioTypes[type], GET_TYPE_NAME(audio), filename);
            return NULL;
        }
    }

    // allocate in ModAudio pool
    if (audio == NULL) {
        audio = dynamic_pool_alloc(sModAudioPool, sizeof(struct ModAudio));
        if (!audio) {
            LOG_LUA_LINE("Could not allocate space for new mod audio!");
            return NULL;
        }
    }

    // remember file
    audio->filepath = strdup(filepath);

    void *buffer = NULL;
    u32 size = 0;

    if (is_mod_fs_file(filepath)) {
        if (!mod_fs_read_file_from_uri(filepath, &buffer, &size)) {
            LOG_ERROR("failed to load audio file '%s': an error occurred with modfs", filename);
            return NULL;
        }
    } else {

        // load audio
        FILE *f = f_open_r(filepath);
        if (!f) {
            LOG_ERROR("failed to load audio file '%s': file not found", filename);
            return NULL;
        }

        f_seek(f, 0, SEEK_END);
        size = f_tell(f);
        f_rewind(f);
        buffer = calloc(size, 1);
        if (!buffer) {
            f_close(f);
            f_delete(f);
            LOG_ERROR("failed to load audio file '%s': cannot allocate buffer of size: %d", filename, size);
            return NULL;
        }

        // read the audio buffer
        if (f_read(buffer, 1, size, f) < size) {
            free(buffer);
            f_close(f);
            f_delete(f);
            LOG_ERROR("failed to load audio file '%s': cannot read audio buffer of size: %d", filename, size);
            return NULL;
        }
        f_close(f);
        f_delete(f);
    }

    if (!buffer || !size) {
        LOG_ERROR("failed to load audio file '%s': failed to read audio data", filename);
        return NULL;
    }

    // decode the audio buffer
    ma_result result = ma_decoder_init_memory(buffer, size, NULL, &audio->decoder);
    if (result != MA_SUCCESS) {
        free(buffer);
        LOG_ERROR("failed to load audio file '%s': failed to decode raw audio: %d", filename, result);
        return NULL;
    }

    result = ma_sound_init_from_data_source(&sModAudioEngine, &audio->decoder, MA_SOUND_FLAGS, NULL, &audio->sound);
    if (result != MA_SUCCESS) {
        free(buffer);
        LOG_ERROR("failed to load audio file '%s': %d", filename, result);
        return NULL;
    }

    audio->buffer = buffer;
    audio->bufferSize = size;
    audio->type = type;
    audio->loaded = true;
    audio_set_volume_channel(audio, type == MA_TYPE_STREAM ? MA_CHANNEL_MUSIC : MA_CHANNEL_SFX);
    printf("%X \n", audio->flags);
    printf("type %s, channel %i, loaded %i \n", GET_TYPE_NAME(audio), audio->channel, audio->loaded);
    return audio;
}

struct ModAudio* audio_stream_load(const char* filename) {
    return audio_load_internal(filename, MA_TYPE_STREAM);
}

struct ModAudio* audio_sample_load(const char* filename) {
    return audio_load_internal(filename, MA_TYPE_SAMPLE);
}

void audio_stream_play(struct ModAudio* audio, bool restart, f32 volume) {
    if (!audio_sanity_check(audio, MA_TYPE_STREAM, "play")) { return; }
    
    ma_sound_set_volume(&audio->sound, volume);
    if (restart) { ma_sound_seek_to_pcm_frame(&audio->sound, 0); }
    ma_sound_start(&audio->sound);
}

struct ModAudio* audio_sample_play(struct ModAudio* audio, Vec3f position, f32 volume) {
    if (!audio_sanity_check(audio, MA_TYPE_SAMPLE, "play")) { return NULL; }
    if (audio->copy) { audio = audio->parent; }

    ma_sound *sound = &audio->sound;
    if (ma_sound_is_playing(sound)) {
        audio = audio_copy(audio);
        if (!audio) { return NULL; }
        sound = &audio->sound;
    }

    f32 dist = 0;
    f32 pan = 0.5f;
    if (gCamera) {
        f32 dX = position[0] - gCamera->pos[0];
        f32 dY = position[1] - gCamera->pos[1];
        f32 dZ = position[2] - gCamera->pos[2];
        dist = sqrtf(dX * dX + dY * dY + dZ * dZ);

        Mat4 mtx;
        mtxf_translate(mtx, position);
        mtxf_mul(mtx, mtx, gCamera->mtx);
        f32 factor = 10;
        pan = (get_sound_pan(mtx[3][0] * factor, mtx[3][2] * factor) - 0.5f) * 2.0f;
    }

    f32 intensity = sound_get_level_intensity(dist);
    ma_sound_set_volume(sound, volume * intensity);
    ma_sound_set_pan(sound, pan);

    ma_sound_start(sound);
    return audio;
}

void audio_pause(struct ModAudio* audio) {
    ma_sound_stop(&audio->sound);
}

void audio_stop(struct ModAudio* audio) {
    if (audio->copy && audio->type == MA_TYPE_SAMPLE) {
        return audio_destroy_copy(audio);
    } else if (audio->copiesTail) {
        audio_destroy_all_copies(audio);
    }
    ma_sound_stop(&audio->sound);
    ma_sound_seek_to_pcm_frame(&audio->sound, 0);
}

void audio_destroy(struct ModAudio* audio) {
    if (audio->copy) {
        return audio_destroy_copy(audio);
    } else if (audio->copiesTail) {
        audio_destroy_all_copies(audio);
    }
    ma_sound_uninit(&audio->sound);
    audio->loaded = false;
}

u32 audio_get_sample_rate(struct ModAudio* audio) {
    return audio->sound.engineNode.sampleRate;
}

f32 audio_get_position(struct ModAudio* audio) {
    u64 cursor;
    ma_data_source_get_cursor_in_pcm_frames(&audio->decoder, &cursor);

    return (f32)cursor / audio->sound.engineNode.sampleRate;
}

void audio_set_position(struct ModAudio* audio, f32 pos) {
    ma_uint64 frame = (ma_uint64)(pos * audio->sound.engineNode.sampleRate);

    ma_sound_seek_to_pcm_frame(&audio->sound, frame);
}

bool audio_get_looping(struct ModAudio* audio) {
    return ma_sound_is_looping(&audio->sound);
}

void audio_set_looping(struct ModAudio* audio, bool looping) {
    ma_sound_set_looping(&audio->sound, looping);
}

void audio_get_loop_points(struct ModAudio* audio, RET u64 *loopStart, RET u64 *loopEnd) {
    ma_data_source_get_loop_point_in_pcm_frames(&audio->decoder, loopStart, loopEnd);
}

void audio_set_loop_points(struct ModAudio* audio, s64 loopStart, OPTIONAL s64 loopEnd) {
    if (!audio_sanity_check(audio, MA_TYPE_STREAM, "set stream loop points for")) { return; }
    
    u64 length; ma_data_source_get_length_in_pcm_frames(&audio->decoder, &length);
    if (loopStart < 0) loopStart = length + loopStart % length;
    if (loopEnd <= 0) loopEnd = length + loopEnd % length;

    ma_sound_set_looping(&audio->sound, true);
    ma_data_source_set_loop_point_in_pcm_frames(&audio->decoder, loopStart, loopEnd);
}

f32 audio_get_frequency(struct ModAudio* audio) {
    return ma_sound_get_pitch(&audio->sound);
}

void audio_set_frequency(struct ModAudio* audio, f32 freq) {
    ma_sound_set_pitch(&audio->sound, freq);
}

// f32 audio_stream_get_tempo(struct ModAudio* audio) {
//     if (!audio_sanity_check(audio, MA_TYPE_STREAM, "get stream tempo from")) { return 0; }
//
//     return bassh_get_tempo(audio->handle);
// }

// ? Possibly implement as a tempo node? https://source.chromium.org/chromium/chromium/src/+/main:media/base/audio_shifter.cc
// void audio_stream_set_tempo(struct ModAudio* audio, f32 tempo) {
//     if (!audio_sanity_check(audio, MA_TYPE_STREAM, "set stream tempo for")) { return; }
//
//     bassh_set_tempo(audio->handle, tempo);
// }

f32 audio_get_volume(struct ModAudio* audio) {
    return ma_sound_get_volume(&audio->sound);
}

void audio_set_volume(struct ModAudio* audio, f32 volume) {
    ma_sound_set_volume(&audio->sound, volume);
}

// void audio_stream_set_speed(struct ModAudio* audio, f32 initial_freq, f32 speed, bool pitch) {
//     if (!audio_sanity_check(audio, MA_TYPE_STREAM, "set stream speed for")) { return; }
//
//     bassh_set_speed(audio->handle, initial_freq, speed, pitch);
// }

u8 audio_get_volume_channel(struct ModAudio* audio) {
    return audio->channel;
}

void audio_set_volume_channel(struct ModAudio* audio, u8 channel) {
    if (channel > MA_CHANNEL_MASTER) {
        LOG_LUA_LINE("Tried to set volume channel to invalid value: %d", channel);
        return;
    }

    audio->channel = channel;
    if (channel == MA_CHANNEL_MASTER) {
        ma_node_attach_output_bus(&audio->sound, 0, ma_node_graph_get_endpoint(&sModAudioEngine.nodeGraph), 0);
    } else {
        ma_node_attach_output_bus(&audio->sound, 0, &sModAudioChannels[channel], 0);
    }
}

//////////////////////////////////////

void audio_custom_update_volume(void) {
    bool shouldMute = (configMuteFocusLoss && !WAPI.has_focus());

    // Update master volume
    f32 masterVolume = shouldMute ? 0 : ((f32)configMasterVolume / 127.0f * (f32)gLuaVolumeMaster / 127.0f);
    gMasterVolume = masterVolume;
    if (!sModAudioPool) { return; }
    if (ma_engine_get_volume(&sModAudioEngine) != masterVolume) {
        ma_engine_set_volume(&sModAudioEngine, masterVolume);
    }

    // Update music volume
    f32 musicVolume = (f32)configMusicVolume / 127.0f * (f32)gLuaVolumeLevel / 127.0f;
    if (ma_sound_group_get_volume(&sModAudioChannels[MA_CHANNEL_MUSIC]) != musicVolume) {
        ma_sound_group_set_volume(&sModAudioChannels[MA_CHANNEL_MUSIC], musicVolume);
    }

    // Update sound volume
    f32 sfxVolume = (f32)configSfxVolume / 127.0f * (f32)gLuaVolumeSfx / 127.0f;
    if (ma_sound_group_get_volume(&sModAudioChannels[MA_CHANNEL_SFX]) != sfxVolume) {
        ma_sound_group_set_volume(&sModAudioChannels[MA_CHANNEL_SFX], sfxVolume);
    }

    // Update env volume
    f32 envVolume = (f32)configEnvVolume / 127.0f * (f32)gLuaVolumeEnv / 127.0f;
    if (ma_sound_group_get_volume(&sModAudioChannels[MA_CHANNEL_ENV]) != envVolume) {
        ma_sound_group_set_volume(&sModAudioChannels[MA_CHANNEL_ENV], envVolume);
    }
}

void audio_custom_shutdown(void) {
    if (!sModAudioPool) { return; }
    struct DynamicPoolNode* node = sModAudioPool->tail;
    while (node) {
        struct DynamicPoolNode* prev = node->prev;
        struct ModAudio* audio = node->ptr;
        if (audio->loaded) {
            if (audio->copiesTail) {
                audio_destroy_all_copies(audio);
            }
            ma_sound_uninit(&audio->sound);
            free(audio->buffer);
            free((void *) audio->filepath);
        }
        dynamic_pool_free(sModAudioPool, audio);
        node = prev;
    }
    dynamic_pool_free_pool(sModAudioPool);
}

void smlua_audio_custom_deinit(void) {
    if (sModAudioPool) {
        audio_custom_shutdown();
        free(sModAudioPool);
        for (u8 i = 0; i < 3; i++) {
            ma_sound_group_uninit(&sModAudioChannels[i]);
        }
        ma_engine_uninit(&sModAudioEngine);
        sModAudioPool = NULL;
    }
}
