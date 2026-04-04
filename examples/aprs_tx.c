#define _DEFAULT_SOURCE

/*
 * aprs_tx — Transmit APRS packets via an AIOC / CM108 USB audio device.
 *
 * Pipeline:
 *   libaprs packet builder → AX.25 encode → AFSK modulator → PortAudio → AIOC
 *   Serial DTR PTT ──────────────────────────────────────────────────────┘
 *
 * Usage:
 *   aprs_tx -C N0CALL-9 beacon -lat 35.21 -lon -97.50 -comment "Test"
 *   aprs_tx -C N0CALL-9 message -to W3ADO -text "Hello"
 *   aprs_tx -C N0CALL-9 status -text "On the air"
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <linux/hidraw.h>
#include <portaudio.h>
#include <libaprs/aprs.h>
#include <libaprs/ax25.h>
#include <libaprs/modem.h>

#define SAMPLE_RATE   48000
#define MAX_SAMPLES   (48000 * 4)
#define TX_DELAY_MS   1500
#define TX_TAIL_MS    500

/* ── PTT control ── */

/* Serial DTR PTT (e.g. /dev/ttyUSB0) */
static int serial_ptt_open(const char *path)
{
	int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (fd < 0) return -1;

	struct termios tio;
	memset(&tio, 0, sizeof(tio));
	tio.c_cflag = B9600 | CS8 | CLOCAL | CREAD;
	tio.c_iflag = IGNPAR;
	tcflush(fd, TCIFLUSH);
	tcsetattr(fd, TCSANOW, &tio);

	int bits = TIOCM_DTR;
	ioctl(fd, TIOCMBIC, &bits);
	return fd;
}

static int serial_ptt_set(int fd, int on)
{
	if (fd < 0) return -1;
	int bits = TIOCM_DTR;
	return ioctl(fd, on ? TIOCMBIS : TIOCMBIC, &bits);
}

/* CM108/AIOC HID GPIO PTT (e.g. /dev/hidraw0)
 * AIOC uses GPIO2 (bit 2, 0x04) for PTT via HID output report. */
#define AIOC_PTT_GPIO  0x04    /* GPIO2 */

static int cm108_ptt_open(const char *path)
{
	return open(path, O_WRONLY);
}

static int cm108_ptt_set(int fd, int on)
{
	if (fd < 0) return -1;
	uint8_t buf[5] = {
		0x00,                                   /* HID report ID */
		0x00,                                   /* reserved */
		on ? AIOC_PTT_GPIO : 0x00,             /* GPIO direction: output / input */
		on ? AIOC_PTT_GPIO : 0x00,             /* GPIO data */
		0x00                                    /* reserved */
	};
	return write(fd, buf, sizeof(buf)) == sizeof(buf) ? 0 : -1;
}

/* auto-detect PTT type from path */
static int is_hidraw(const char *path)
{
	return strstr(path, "hidraw") != NULL;
}

static int ptt_open(const char *path)
{
	return is_hidraw(path) ? cm108_ptt_open(path) : serial_ptt_open(path);
}

static int ptt_set(const char *path, int fd, int on)
{
	return is_hidraw(path) ? cm108_ptt_set(fd, on) : serial_ptt_set(fd, on);
}

/* ── PortAudio playback ── */

static PaStream *g_stream;

static int audio_open(const char *device_name)
{
	PaError err;
	PaDeviceIndex dev = paNoDevice;
	PaStreamParameters out_params;

	err = Pa_Initialize();
	if (err != paNoError) {
		fprintf(stderr, "PortAudio init failed: %s\n", Pa_GetErrorText(err));
		return -1;
	}

	int ndev = Pa_GetDeviceCount();
	for (int i = 0; i < ndev; i++) {
		const PaDeviceInfo *info = Pa_GetDeviceInfo(i);
		if (info && info->maxOutputChannels > 0 &&
		    strstr(info->name, device_name)) {
			dev = i;
			break;
		}
	}
	if (dev == paNoDevice) {
		fprintf(stderr, "Audio device '%s' not found.  Available:\n", device_name);
		for (int i = 0; i < ndev; i++) {
			const PaDeviceInfo *info = Pa_GetDeviceInfo(i);
			if (info && info->maxOutputChannels > 0)
				fprintf(stderr, "  [%d] %s\n", i, info->name);
		}
		Pa_Terminate();
		return -1;
	}

	fprintf(stderr, "Audio: %s\n", Pa_GetDeviceInfo(dev)->name);

	memset(&out_params, 0, sizeof(out_params));
	out_params.device = dev;
	out_params.channelCount = 1;
	out_params.sampleFormat = paInt16;
	out_params.suggestedLatency = Pa_GetDeviceInfo(dev)->defaultLowOutputLatency;

	err = Pa_OpenStream(&g_stream, NULL, &out_params,
	                    SAMPLE_RATE, 256, paClipOff, NULL, NULL);
	if (err != paNoError) {
		fprintf(stderr, "Pa_OpenStream: %s\n", Pa_GetErrorText(err));
		Pa_Terminate();
		return -1;
	}

	err = Pa_StartStream(g_stream);
	if (err != paNoError) {
		fprintf(stderr, "Pa_StartStream: %s\n", Pa_GetErrorText(err));
		Pa_CloseStream(g_stream); g_stream = NULL;
		Pa_Terminate();
		return -1;
	}
	return 0;
}

static int audio_write(const int16_t *samples, size_t nsamples)
{
	PaError err = Pa_WriteStream(g_stream, samples, (unsigned long)nsamples);
	if (err != paNoError && err != paOutputUnderflowed) {
		fprintf(stderr, "Pa_WriteStream: %s\n", Pa_GetErrorText(err));
		return -1;
	}
	return 0;
}

static void audio_close(void)
{
	if (g_stream) {
		Pa_StopStream(g_stream);
		Pa_CloseStream(g_stream);
		g_stream = NULL;
	}
	Pa_Terminate();
}

/* ── transmit a built packet ── */

static int transmit(const aprs_packet_t *pkt, const char *audio_dev,
                    const char *ptt_port, float level, int tx_delay,
                    int preemph, int verbose)
{
	ax25_ui_frame_t frame;
	uint8_t ax25_buf[1024];
	size_t ax25_len;
	aprs_err_t rc;

	rc = ax25_from_aprs(pkt, &frame);
	if (rc != APRS_OK) {
		fprintf(stderr, "ax25_from_aprs: %s\n", aprs_strerror(rc));
		return -1;
	}

	rc = ax25_encode_ui_frame(&frame, ax25_buf, sizeof(ax25_buf), &ax25_len);
	if (rc != APRS_OK) {
		fprintf(stderr, "ax25_encode: %s\n", aprs_strerror(rc));
		return -1;
	}

	afsk_mod_t *mod = afsk_mod_create(SAMPLE_RATE);
	if (!mod) { fprintf(stderr, "modulator create failed\n"); return -1; }
	if (!preemph) afsk_mod_set_preemph(mod, false);

	int16_t *samples = (int16_t *)malloc(MAX_SAMPLES * sizeof(int16_t));
	if (!samples) { afsk_mod_destroy(mod); return -1; }

	size_t nsamples;
	rc = afsk_mod_frame(mod, ax25_buf, ax25_len,
	                    samples, MAX_SAMPLES, &nsamples);
	afsk_mod_destroy(mod);
	if (rc != APRS_OK) {
		fprintf(stderr, "modulation failed: %s\n", aprs_strerror(rc));
		free(samples);
		return -1;
	}

	/* apply level scaling */
	for (size_t i = 0; i < nsamples; i++)
		samples[i] = (int16_t)((float)samples[i] * level);

	if (verbose)
		fprintf(stderr, "Modulated: %zu samples (%.2f sec)\n",
		        nsamples, (double)nsamples / SAMPLE_RATE);

	/* open audio (slow scan happens here, before PTT) */
	if (audio_open(audio_dev) < 0) { free(samples); return -1; }

	/* PTT on */
	int ptt_fd = ptt_open(ptt_port);
	if (ptt_fd < 0)
		fprintf(stderr, "Warning: cannot open %s for PTT\n", ptt_port);

	if (verbose) fprintf(stderr, "PTT ON (%s)\n",
	                     is_hidraw(ptt_port) ? "HID GPIO" : "serial DTR");
	ptt_set(ptt_port, ptt_fd, 1);
	usleep((unsigned)(tx_delay * 1000));

	/* play */
	if (verbose) fprintf(stderr, "Transmitting...\n");
	int ret = audio_write(samples, nsamples);

	/* drain then PTT off */
	audio_close();
	usleep(TX_TAIL_MS * 1000);
	ptt_set(ptt_port, ptt_fd, 0);
	if (verbose) fprintf(stderr, "PTT OFF\n");

	if (ptt_fd >= 0) close(ptt_fd);
	free(samples);
	return ret;
}

static void usage(const char *prog)
{
	fprintf(stderr,
	    "Usage: %s [options] -C <callsign> <command> [args]\n\n"
	    "Commands:\n"
	    "  beacon  -lat <deg> -lon <deg> [-sym <tbl><code>] [-comment <text>]\n"
	    "  message -to <call> -text <msg> [-msgno <num>]\n"
	    "  status  -text <text>\n"
	    "\nOptions:\n"
	    "  -C callsign   Source callsign with SSID (required)\n"
	    "  -D device      PortAudio device name (default: All-In-One)\n"
	    "  -P port        PTT device: /dev/hidrawN for AIOC/CM108,\n"
	    "                 /dev/ttyXXX for serial DTR (default: /dev/hidraw1)\n"
	    "  -l level       Audio level 0.0-1.0 (default: 0.15)\n"
	    "  -E             Flat audio — no pre-emphasis (use with radios\n"
	    "                 that apply their own FM pre-emphasis)\n"
	    "  -d ms          TX delay in ms (default: %d)\n"
	    "  -p path        Digipeater path (default: WIDE1-1)\n"
	    "  -v             Verbose\n",
	    prog, TX_DELAY_MS);
}

int main(int argc, char **argv)
{
	const char *callsign = NULL;
	const char *audio_dev = "All-In-One";
	const char *ptt_port = "/dev/hidraw1";
	const char *digi_path = "WIDE1-1";
	float    level = 0.15f;
	int      tx_delay = TX_DELAY_MS;
	int      preemph = 1;
	int      verbose = 0;

	/* parse global options before the command */
	int i;
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-C") == 0 && i + 1 < argc)
			callsign = argv[++i];
		else if (strcmp(argv[i], "-D") == 0 && i + 1 < argc)
			audio_dev = argv[++i];
		else if (strcmp(argv[i], "-P") == 0 && i + 1 < argc)
			ptt_port = argv[++i];
		else if (strcmp(argv[i], "-l") == 0 && i + 1 < argc)
			level = (float)atof(argv[++i]);
		else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc)
			tx_delay = atoi(argv[++i]);
		else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc)
			digi_path = argv[++i];
		else if (strcmp(argv[i], "-E") == 0)
			preemph = 0;
		else if (strcmp(argv[i], "-v") == 0)
			verbose = 1;
		else
			break;  /* first non-option = command */
	}

	if (!callsign || i >= argc) {
		usage(argv[0]);
		return 1;
	}

	const char *cmd = argv[i++];
	const char *path_arr[] = { digi_path };
	size_t path_len = 1;

	aprs_packet_t pkt;
	aprs_packet_init(&pkt);
	aprs_err_t rc;

	if (strcmp(cmd, "beacon") == 0) {
		double lat = 0, lon = 0;
		char sym_table = '/';
		char sym_code = '>';
		const char *comment = "";

		for (; i < argc; i++) {
			if (strcmp(argv[i], "-lat") == 0 && i + 1 < argc)
				lat = atof(argv[++i]);
			else if (strcmp(argv[i], "-lon") == 0 && i + 1 < argc)
				lon = atof(argv[++i]);
			else if (strcmp(argv[i], "-sym") == 0 && i + 1 < argc) {
				sym_table = argv[++i][0];
				sym_code = argv[i][1];
			}
			else if (strcmp(argv[i], "-comment") == 0 && i + 1 < argc)
				comment = argv[++i];
		}

		rc = aprs_build_position(&pkt, callsign, "APRS", path_arr, path_len,
		                          lat, lon, sym_table, sym_code, comment);
		if (rc != APRS_OK) {
			fprintf(stderr, "build_position: %s\n", aprs_strerror(rc));
			return 1;
		}

		char tnc2[256];
		aprs_format_tnc2(&pkt, tnc2, sizeof(tnc2));
		fprintf(stderr, "TX: %s\n", tnc2);

	} else if (strcmp(cmd, "message") == 0) {
		const char *to = NULL, *text = NULL, *msgno = NULL;

		for (; i < argc; i++) {
			if (strcmp(argv[i], "-to") == 0 && i + 1 < argc)
				to = argv[++i];
			else if (strcmp(argv[i], "-text") == 0 && i + 1 < argc)
				text = argv[++i];
			else if (strcmp(argv[i], "-msgno") == 0 && i + 1 < argc)
				msgno = argv[++i];
		}
		if (!to || !text) {
			fprintf(stderr, "message requires -to and -text\n");
			return 1;
		}

		rc = aprs_build_message(&pkt, callsign, "APRS", path_arr, path_len,
		                         to, text, msgno);
		if (rc != APRS_OK) {
			fprintf(stderr, "build_message: %s\n", aprs_strerror(rc));
			return 1;
		}

		char tnc2[256];
		aprs_format_tnc2(&pkt, tnc2, sizeof(tnc2));
		fprintf(stderr, "TX: %s\n", tnc2);

	} else if (strcmp(cmd, "status") == 0) {
		const char *text = NULL;

		for (; i < argc; i++) {
			if (strcmp(argv[i], "-text") == 0 && i + 1 < argc)
				text = argv[++i];
		}
		if (!text) {
			fprintf(stderr, "status requires -text\n");
			return 1;
		}

		rc = aprs_build_status(&pkt, callsign, "APRS", path_arr, path_len, text);
		if (rc != APRS_OK) {
			fprintf(stderr, "build_status: %s\n", aprs_strerror(rc));
			return 1;
		}

		char tnc2[256];
		aprs_format_tnc2(&pkt, tnc2, sizeof(tnc2));
		fprintf(stderr, "TX: %s\n", tnc2);

	} else {
		fprintf(stderr, "Unknown command: %s\n", cmd);
		usage(argv[0]);
		return 1;
	}

	return transmit(&pkt, audio_dev, ptt_port, level, tx_delay, preemph, verbose);
}
