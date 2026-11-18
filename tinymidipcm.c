
#define TSF_NO_STDIO
#define TSF_IMPLEMENTATION
#include "tsf.h"

#define TML_NO_STDIO
#define TML_IMPLEMENTATION
#include "tml.h"

tml_message *midi_render(tsf *soundfont, tml_message *midi_message, unsigned int channels, unsigned int sample_rate, unsigned char *buffer, unsigned int pcm_length, double *msecs)
{
	const unsigned int BLOCK_FRAMES = TSF_RENDER_EFFECTSAMPLEBLOCK;
	const unsigned int BLOCK_BYTES = BLOCK_FRAMES * sizeof(float) * channels;

	unsigned int bytes_written = 0;
	while (bytes_written + BLOCK_BYTES <= pcm_length)
	{
		*msecs += BLOCK_FRAMES * (1000.0 / (float)sample_rate);

		while (midi_message && *msecs >= midi_message->time)
		{
			switch (midi_message->type)
			{
				case TML_PROGRAM_CHANGE:
					tsf_channel_set_presetnumber(soundfont, midi_message->channel, midi_message->program, (midi_message->channel == 9));
					break;
				case TML_NOTE_ON:
					tsf_channel_note_on(soundfont, midi_message->channel, midi_message->key, midi_message->velocity / 127.0f);
					break;
				case TML_NOTE_OFF:
					tsf_channel_note_off(soundfont, midi_message->channel, midi_message->key);
					break;
				case TML_KEY_PRESSURE:
					tsf_channel_set_keypressure(soundfont, midi_message->channel, midi_message->key, midi_message->key_pressure);
					break;
				case TML_CHANNEL_PRESSURE:
					tsf_channel_set_pressure(soundfont, midi_message->channel, midi_message->channel_pressure);
					break;
				case TML_PITCH_BEND:
					tsf_channel_set_pitchwheel(soundfont, midi_message->channel, midi_message->pitch_bend);
					break;
				case TML_CONTROL_CHANGE:
					tsf_channel_midi_control(soundfont, midi_message->channel, midi_message->control, midi_message->control_value);
					break;
				case TML_GM_RESET:
					tsf_reset(soundfont);
					tsf_channel_set_bank_preset(soundfont, 9, 128, 0);
					break;
				case TML_GM2_RESET:
				case TML_GS_RESET:
				case TML_XG_RESET:
					tsf_reset(soundfont);
					tsf_channel_set_bank_preset(soundfont, 9, 128, 0);
					for (int ch = 0; ch < 16; ch++)
						tsf_channel_midi_control(soundfont, ch, 91, 40); // CC91=40
					break;
			}

			midi_message = midi_message->next;
		}

		/* tsf_render_short(soundfont, (short *)(buffer + bytes_written), BLOCK_FRAMES, 0); */
		tsf_render_float(soundfont, (float *)(buffer + bytes_written), BLOCK_FRAMES, 0);

		bytes_written += BLOCK_BYTES;
	}

	return midi_message;
}

#if 0
#include <stdio.h>

#define SAMPLE_RATE 44100
#define CHANNELS 2

int main(int argc, char *argv[])
{
	FILE *soundfont_file = fopen("STDGMGS.SF2", "rb");
	fseek(soundfont_file, 0, SEEK_END);
	int soundfont_length = ftell(soundfont_file);
	unsigned char *soundfont_buffer = malloc(soundfont_length);
	fseek(soundfont_file, 0, SEEK_SET);
	fread(soundfont_buffer, 1, soundfont_length, soundfont_file);
	fclose(soundfont_file);

	FILE *midi_file = fopen("D_E1M1.MID", "rb");
	fseek(soundfont_file, 0, SEEK_END);
	int midi_length = ftell(midi_file);
	unsigned char *midi_buffer = malloc(midi_length);
	fseek(midi_file, 0, SEEK_SET);
	fread(midi_buffer, 1, midi_length, midi_file);
	fclose(midi_file);

	tsf *soundfont = tsf_load_memory(soundfont_buffer, soundfont_length);
	tsf_channel_set_bank_preset(soundfont, 9, 128, 0);
	tsf_set_output(soundfont, TSF_STEREO_INTERLEAVED, SAMPLE_RATE, -10.0f);
	//tsf_set_output(soundfont, TSF_MONO, SAMPLE_RATE, 0.0f);

	tml_message *midi_message = tml_load_memory(midi_buffer, midi_length);

	double msecs = 0;
	FILE *test = fopen("out.pcm", "a+b");

	int buffer_length = TSF_RENDER_EFFECTSAMPLEBLOCK * CHANNELS * sizeof(float);
	unsigned char *pcm_buffer = malloc(buffer_length);

	do
	{
		midi_message = midi_render(soundfont, midi_message, CHANNELS, SAMPLE_RATE, pcm_buffer, buffer_length, &msecs);
		fwrite(pcm_buffer, 1, buffer_length, test);
	} while (midi_message != NULL);

	// cat out.pcm | aplay -c2 -f FLOAT_LE -r 44100
	// ffmpeg -f f32le -ar 44100 -ac 2 -i out.pcm out.wav
}
#endif
