#include <vorbis/vorbisfile.h>
#include <stdio.h>
#include <windows.h>
#include <stdint.h>

WAVEFORMATEX    plr_fmt;
HWAVEOUT        plr_hwo         = NULL;
OggVorbis_File  plr_vf;
HANDLE          plr_ev          = NULL;
int             plr_cnt         = 0;
int             plr_lvol        = 0xFFFF;
int             plr_rvol        = 0xFFFF;
WAVEHDR         *plr_buffers[3] = { NULL, NULL, NULL };

void plr_stop() {
    plr_cnt = 0;

    if (plr_vf.datasource) ov_clear(&plr_vf);

    if (plr_ev) {
        CloseHandle(plr_ev);
        plr_ev = NULL;
    }

    if (plr_hwo) {
        waveOutReset(plr_hwo);

        int i;
        for (i = 0; i < 3; i++) {
            if (plr_buffers[i] && plr_buffers[i]->dwFlags & WHDR_DONE) {
                waveOutUnprepareHeader(plr_hwo, plr_buffers[i], sizeof(WAVEHDR));
                free(plr_buffers[i]->lpData);
                free(plr_buffers[i]);
                plr_buffers[i] = NULL;
            }
        }

        waveOutClose(plr_hwo);
        plr_hwo = NULL;
    }
}

void plr_volume(uint16_t lVol, uint16_t rVol) {
    plr_lvol = lVol;
    plr_rvol = rVol;
}

int plr_length(const char *path) {
    OggVorbis_File  vf;

    if (ov_fopen(path, &vf) != 0)
        return 0;

    int ret = (int)ov_time_total(&vf, -1);

    ov_clear(&vf);

    return ret;
}

int plr_play(const char *path) {
    plr_stop();

    if (ov_fopen(path, &plr_vf) != 0) return 0;

    vorbis_info *vi = ov_info(&plr_vf, -1);

    if (!vi) {
        ov_clear(&plr_vf);
        return 0;
    }

    plr_fmt.wFormatTag      = WAVE_FORMAT_PCM;
    plr_fmt.nChannels       = vi->channels;
    plr_fmt.nSamplesPerSec  = vi->rate;
    plr_fmt.wBitsPerSample  = 16;
    plr_fmt.nBlockAlign     = plr_fmt.nChannels * (plr_fmt.wBitsPerSample / 8);
    plr_fmt.nAvgBytesPerSec = plr_fmt.nBlockAlign * plr_fmt.nSamplesPerSec;
    plr_fmt.cbSize          = 0;

    plr_ev = CreateEvent(NULL, 0, 1, NULL);

    if (waveOutOpen(&plr_hwo, WAVE_MAPPER, &plr_fmt, (DWORD_PTR)plr_ev, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        return 0;
    }

    return 1;
}

int plr_pump() {
    if (!plr_vf.datasource) return 0;

    int pos = 0;
    int bufsize = plr_fmt.nAvgBytesPerSec / 4; // 250ms (avg at 500ms) should be enough for everyone
    char *buf = malloc(bufsize);

    while (pos < bufsize) {
        long bytes = ov_read(&plr_vf, buf + pos, bufsize - pos, 0, 2, 1, NULL);

        if (bytes == OV_HOLE) {
            free(buf);
            continue;
        }

        if (bytes == OV_EBADLINK) {
            free(buf);
            return 0;
        }

        if (bytes == OV_EINVAL) {
            free(buf);
            return 0;
        }

        if (bytes == 0) {
            free(buf);

            int i, in_queue = 0;
            for (i = 0; i < 3; i++) {
                if (plr_buffers[i] && plr_buffers[i]->dwFlags & WHDR_DONE) {
                    waveOutUnprepareHeader(plr_hwo, plr_buffers[i], sizeof(WAVEHDR));
                    free(plr_buffers[i]->lpData);
                    free(plr_buffers[i]);
                    plr_buffers[i] = NULL;
                }

                if (plr_buffers[i]) in_queue++;
            }

            Sleep(100);

            return !(in_queue == 0);
        }

        pos += bytes;
    }

	int x, end = pos / (sizeof(short)*2);
    short *sbuf = (short *)buf;
	//Adjust volume
	for (x = 0; x < end; x++) {
		*sbuf = ((int32_t)*sbuf * plr_lvol) >> 16;
		sbuf++;
		*sbuf = ((int32_t)*sbuf * plr_rvol) >> 16;
		sbuf++;
	}
    //for (x = 0; x < end; x++) {
    //    sbuf[x+0] = ((int32_t)sbuf[x+0] * plr_lvol)>>16;
    //    sbuf[x+1] = ((int32_t)sbuf[x+1] * plr_rvol)>>16;
    //}

    WAVEHDR *header = malloc(sizeof(WAVEHDR));
    header->dwBufferLength   = pos;
    header->lpData           = buf;
    header->dwUser           = 0;
    header->dwFlags          = plr_cnt == 0 ? WHDR_BEGINLOOP : 0;
    header->dwLoops          = 0;
    header->lpNext           = NULL;
    header->reserved         = 0;

    waveOutPrepareHeader(plr_hwo, header, sizeof(WAVEHDR));

    if (plr_cnt > 1) {
        WaitForSingleObject(plr_ev, INFINITE);
    }

    int i, queued = 0;
    for (i = 0; i < 3; i++) {
        if (plr_buffers[i] && plr_buffers[i]->dwFlags & WHDR_DONE) {
            waveOutUnprepareHeader(plr_hwo, plr_buffers[i], sizeof(WAVEHDR));
            free(plr_buffers[i]->lpData);
            free(plr_buffers[i]);
            plr_buffers[i] = NULL;
        }

        if (!queued && plr_buffers[i] == NULL) {
            waveOutWrite(plr_hwo, header, sizeof(WAVEHDR));
            plr_buffers[i] = header;
            queued = 1;
        }
    }

    if (!queued) {
        free(header);
        free(buf);
    }

    plr_cnt++;

    return 1;
}
