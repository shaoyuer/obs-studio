#include "audio-helpers.h"

#include <util/dstr.h>

static void audio_callback(void *param, obs_source_t *source,
			   const struct audio_data *audio_data, bool muted)
{
	UNUSED_PARAMETER(muted);
	UNUSED_PARAMETER(source);

	obs_source_t *parent = param;
	static uint32_t sample_rate = 0;

	/* The audio we get is resampled to OBS's sample rate, which cannot be changed
	 * at runtime, so this is safe. */
	if (!sample_rate) {
		struct obs_audio_info oai;
		obs_get_audio_info(&oai);
		sample_rate = oai.samples_per_sec;
	}

	struct obs_source_audio audio;

	int nr_channels = 0;
	for (int i = 0; i < MAX_AV_PLANES; i++) {
		if (audio_data->data[i])
			nr_channels++;
		audio.data[i] = audio_data->data[i];
	}

	audio.format = AUDIO_FORMAT_FLOAT_PLANAR;
	audio.frames = audio_data->frames;
	audio.timestamp = audio_data->timestamp;
	audio.speakers = nr_channels > MAX_AUDIO_CHANNELS ? MAX_AUDIO_CHANNELS
							  : nr_channels;
	audio.samples_per_sec = sample_rate;

	obs_source_output_audio(parent, &audio);
}

static inline bool settings_changed(obs_data_t *old_settings,
				    obs_data_t *new_settings)
{
	const char *old_window = obs_data_get_string(old_settings, "window");
	const char *new_window = obs_data_get_string(new_settings, "window");

	enum window_priority old_priority =
		obs_data_get_int(old_settings, "priority");
	enum window_priority new_priority =
		obs_data_get_int(new_settings, "priority");

	// Changes to priority only matter if a window is set
	return (old_priority != new_priority && *new_window) ||
	       strcmp(old_window, new_window) != 0;
}

void setup_audio_source(obs_source_t *parent, obs_source_t **child,
			const char *window, bool enabled,
			enum window_priority priority)
{
	if (enabled && audio_capture_available()) {
		obs_data_t *wasapi_settings = NULL;

		if (window) {
			wasapi_settings = obs_data_create();
			obs_data_set_string(wasapi_settings, "window", window);
			obs_data_set_int(wasapi_settings, "priority", priority);
		}

		if (!*child) {
			struct dstr name = {0};
			dstr_printf(&name, "%s (%s)",
				    obs_source_get_name(parent),
				    TEXT_CAPTURE_AUDIO_SUFFIX);

			*child = obs_source_create_private(
				AUDIO_SOURCE_TYPE, name.array, wasapi_settings);

			// Ensure child gets activated/deactivated properly
			obs_source_add_active_child(parent, *child);
			// Show source in mixer
			obs_source_set_audio_active(parent, true);
			// Set up callback for audio
			obs_source_add_audio_capture_callback(
				*child, audio_callback, parent);

			dstr_free(&name);
		} else if (wasapi_settings) {
			obs_data_t *old_settings =
				obs_source_get_settings(*child);
			// Only bother updating if settings changed
			if (settings_changed(old_settings, wasapi_settings))
				obs_source_update(*child, wasapi_settings);

			obs_data_release(old_settings);
		}

		obs_data_release(wasapi_settings);
	} else {
		obs_source_set_audio_active(parent, false);

		if (*child) {
			obs_source_remove_audio_capture_callback(
				*child, audio_callback, parent);
			obs_source_remove_active_child(parent, *child);
			obs_source_release(*child);
			*child = NULL;
		}
	}
}

static inline void encode_dstr(struct dstr *str)
{
	dstr_replace(str, "#", "#22");
	dstr_replace(str, ":", "#3A");
}

void reconfigure_audio_source(obs_source_t *source, HWND window)
{
	struct dstr title = {0};
	struct dstr class = {0};
	struct dstr exe = {0};
	struct dstr encoded = {0};

	ms_get_window_title(&title, window);
	ms_get_window_class(&class, window);
	ms_get_window_exe(&exe, window);

	encode_dstr(&title);
	encode_dstr(&class);
	encode_dstr(&exe);

	dstr_cat_dstr(&encoded, &title);
	dstr_cat(&encoded, ":");
	dstr_cat_dstr(&encoded, &class);
	dstr_cat(&encoded, ":");
	dstr_cat_dstr(&encoded, &exe);

	obs_data_t *audio_settings = obs_data_create();
	obs_data_set_string(audio_settings, "window", encoded.array);
	obs_data_set_int(audio_settings, "priority", WINDOW_PRIORITY_CLASS);

	obs_source_update(source, audio_settings);

	obs_data_release(audio_settings);
	dstr_free(&encoded);
	dstr_free(&title);
	dstr_free(&class);
	dstr_free(&exe);
}

void rename_audio_source(void *param, calldata_t *data)
{
	obs_source_t *src = *(obs_source_t **)param;
	if (!src)
		return;

	struct dstr name = {0};
	dstr_printf(&name, "%s (%s)", calldata_string(data, "new_name"),
		    TEXT_CAPTURE_AUDIO_SUFFIX);

	obs_source_set_name(src, name.array);
	dstr_free(&name);
}
