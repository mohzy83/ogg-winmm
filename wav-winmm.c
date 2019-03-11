/*
* Copyright (c) 2012 Toni Spets <toni.spets@iki.fi>
*
* Permission to use, copy, modify, and distribute this software for any
* purpose with or without fee is hereby granted, provided that the above
* copyright notice and this permission notice appear in all copies.
*
* THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
* WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
* MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
* ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
* WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
* ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
* OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#include <windows.h>
#include <stdio.h>
#include <ctype.h>
//#include <dirent.h>
#include "player.h"

#define MAGIC_DEVICEID 0xBEEF
#define MAX_TRACKS 99

#ifdef WIN32

#define snprintf _snprintf

#endif

struct track_info {
    char path[MAX_PATH];    // full path to ogg
    unsigned int length;    // seconds
    unsigned int position;  // seconds
};

static struct track_info tracks[MAX_TRACKS];

struct play_info {
    int first;
    int last;
};

//Writes a debug log file into windows temp dir
void writeToDebugLogFile(char* text) {
	FILE *pFile;
	char tempPath[261];
	GetTempPathA(261, tempPath);
	pFile = fopen(strcat(tempPath,"\\ogg-winmm-debug.log"), "a");
	fprintf(pFile, "%s", text);
	fclose(pFile);
}

#define DERROR(format, ...) {char zTemp[512]; snprintf(zTemp, 512, format, ##__VA_ARGS__); writeToDebugLogFile(zTemp);}
#ifdef _DEBUG
#define DVERBOSE(format, ...) {char zTemp[512]; snprintf(zTemp, 512, format, ##__VA_ARGS__); writeToDebugLogFile(zTemp);}
#else 
#define DVERBOSE(...)
#endif

int playing = 0;
int updateTrack = 0;
int closed = 0;
HANDLE player = NULL;
int firstTrack = -1;
int lastTrack = 0;
int numTracks = 0;
char music_path[2048];
int time_format = MCI_FORMAT_TMSF;
CRITICAL_SECTION cs;
static struct play_info info = { -1, -1 };

int player_main() {
    int first;
    int last;
    int current;
    
    while (!closed) {
        //set track info
        if (updateTrack) {
            first = info.first;
            last = info.last;
            current = first;
            updateTrack = 0;
        }

        //stop if at end of 'playlist'
        //note "last" track is NON-inclusive
        //if the "first" track equals "last" track then play that single track
        if ((first == last && current > last) || (first != last && current == last)) {
            playing = 0;
        } else { //try to play song
            DVERBOSE("Next track: %s\r\n", tracks[current].path);
            playing = plr_play(tracks[current].path);
        }

        while (1) {
            //check control signals

            if (closed) break; //MCI_CLOSE
            if (!playing) { //MCI_STOP
                plr_stop(); //end playback
                SuspendThread(player); //pause thread until next MCI_PLAY
            }

            if (plr_pump() == 0) break; //done playing song
            if (updateTrack) break; //MCI_PLAY
        }
        current++;
    }
    plr_stop();

    playing = 0;
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        GetModuleFileName(hinstDLL, music_path, sizeof music_path);

        memset(tracks, 0, sizeof tracks);

        InitializeCriticalSection(&cs);

		DVERBOSE("dll directory is %s\r\n", music_path);

        char *last = strrchr(music_path, '\\');
        if (last) *last = '\0';
        strncat(music_path, "\\MUSIC", sizeof music_path - 1);

        DVERBOSE("ogg-winmm music directory is %s\r\n", music_path);
        DVERBOSE("ogg-winmm searching tracks...\r\n");

        unsigned int position = 0;

        for (int i = 0; i < MAX_TRACKS; i++) {
            snprintf(tracks[i].path, sizeof tracks[i].path, "%s\\Track%02d.ogg", music_path, i);
            tracks[i].length = plr_length(tracks[i].path);
            tracks[i].position = position;

            if (tracks[i].length < 4) {
                tracks[i].path[0] = '\0';
                position += 4; // missing tracks are 4 second data tracks for us
            } else {
                if (firstTrack == -1) firstTrack = i;

                DVERBOSE("Track %02d: %02d:%02d @ %d seconds\r\n", i, tracks[i].length / 60, tracks[i].length % 60, tracks[i].position);
                numTracks++;
                lastTrack = i;
                position += tracks[i].length;
            }
        }

        DVERBOSE("Emulating total of %d CD tracks.\r\n\r\n", numTracks);
    }

    return TRUE;
}

MCIERROR WINAPI fake_mciSendCommandA(MCIDEVICEID IDDevice, UINT uMsg, DWORD_PTR fdwCommand, DWORD_PTR dwParam) {
    char cmdbuf[1024];

    DVERBOSE("mciSendCommandA(IDDevice=%p, uMsg=%p, fdwCommand=%p, dwParam=%p)\r\n", IDDevice, uMsg, fdwCommand, dwParam);

    if (fdwCommand & MCI_NOTIFY) {
        DVERBOSE("  MCI_NOTIFY\r\n");
    }

    if (fdwCommand & MCI_WAIT) {
        DVERBOSE("  MCI_WAIT\r\n");
    }

    if (uMsg == MCI_OPEN) {
        LPMCI_OPEN_PARMS parms = (LPVOID)dwParam;

        DVERBOSE("  MCI_OPEN\r\n");

        if (fdwCommand & MCI_OPEN_ALIAS) {
            DVERBOSE("    MCI_OPEN_ALIAS\r\n");
        }

        if (fdwCommand & MCI_OPEN_SHAREABLE) {
            DVERBOSE("    MCI_OPEN_SHAREABLE\r\n");
        }

        if (fdwCommand & MCI_OPEN_TYPE_ID) {
            DVERBOSE("    MCI_OPEN_TYPE_ID\r\n");

            if (LOWORD(parms->lpstrDeviceType) == MCI_DEVTYPE_CD_AUDIO) {
                DVERBOSE("  Returning magic device id for MCI_DEVTYPE_CD_AUDIO\r\n");
                parms->wDeviceID = MAGIC_DEVICEID;
                return 0;
            }
        }

        if (fdwCommand & MCI_OPEN_TYPE && !(fdwCommand & MCI_OPEN_TYPE_ID)) {
            DVERBOSE("    MCI_OPEN_TYPE\r\n");
            DVERBOSE("        -> %s\r\n", parms->lpstrDeviceType);

            if (strcmp(parms->lpstrDeviceType, "cdaudio") == 0) {
                DVERBOSE("  Returning magic device id for MCI_DEVTYPE_CD_AUDIO\r\n");
                parms->wDeviceID = MAGIC_DEVICEID;
                return 0;
            }
        }
    }

    if (IDDevice == MAGIC_DEVICEID || IDDevice == 0 || IDDevice == 0xFFFFFFFF) {
        if (uMsg == MCI_SET) {
            LPMCI_SET_PARMS parms = (LPVOID)dwParam;

            DVERBOSE("  MCI_SET\r\n");

            if (fdwCommand & MCI_SET_TIME_FORMAT) {
                DVERBOSE("    MCI_SET_TIME_FORMAT\r\n");

                time_format = parms->dwTimeFormat;

                if        (parms->dwTimeFormat == MCI_FORMAT_BYTES) {
                    DVERBOSE("      MCI_FORMAT_BYTES\r\n");
                } else if (parms->dwTimeFormat == MCI_FORMAT_FRAMES) {
                    DVERBOSE("      MCI_FORMAT_FRAMES\r\n");
                } else if (parms->dwTimeFormat == MCI_FORMAT_HMS) {
                    DVERBOSE("      MCI_FORMAT_HMS\r\n");
                } else if (parms->dwTimeFormat == MCI_FORMAT_MILLISECONDS) {
                    DVERBOSE("      MCI_FORMAT_MILLISECONDS\r\n");
                } else if (parms->dwTimeFormat == MCI_FORMAT_MSF) {
                    DVERBOSE("      MCI_FORMAT_MSF\r\n");
                } else if (parms->dwTimeFormat == MCI_FORMAT_SAMPLES) {
                    DVERBOSE("      MCI_FORMAT_SAMPLES\r\n");
                } else if (parms->dwTimeFormat == MCI_FORMAT_TMSF) {
                    DVERBOSE("      MCI_FORMAT_TMSF\r\n");
                }
            }
        }

        if (uMsg == MCI_CLOSE) {
            DVERBOSE("  MCI_CLOSE\r\n");

            if (player) {
                ResumeThread(player); //just in case it's suspended, else deadlock
                closed = 1;
                playing = 0;
            }

            playing = 0;
            player = NULL;
        }

        if (uMsg == MCI_PLAY) {
            LPMCI_PLAY_PARMS parms = (LPVOID)dwParam;

            DVERBOSE("  MCI_PLAY\r\n");

            if (fdwCommand & MCI_FROM) {
                DVERBOSE("    dwFrom: %d\r\n", parms->dwFrom);

                // FIXME: rounding to nearest track
                if (time_format == MCI_FORMAT_TMSF) {
                    info.first = MCI_TMSF_TRACK(parms->dwFrom);

                    DVERBOSE("      TRACK  %d\n", MCI_TMSF_TRACK(parms->dwFrom));
                    DVERBOSE("      MINUTE %d\n", MCI_TMSF_MINUTE(parms->dwFrom));
                    DVERBOSE("      SECOND %d\n", MCI_TMSF_SECOND(parms->dwFrom));
                    DVERBOSE("      FRAME  %d\n", MCI_TMSF_FRAME(parms->dwFrom));
                } else if (time_format == MCI_FORMAT_MILLISECONDS) {
                    info.first = 0;

                    for (int i = 0; i < MAX_TRACKS; i++) {
                        // FIXME: take closest instead of absolute
                        if (tracks[i].position == parms->dwFrom / 1000) {
                            info.first = i;
                        }
                    }

                    DVERBOSE("      mapped milliseconds to %d\n", info.first);
                } else {
                    // FIXME: not really
                    info.first = parms->dwFrom;
                }

                if (info.first < firstTrack) info.first = firstTrack;

                if (info.first > lastTrack) info.first = lastTrack;

                info.last = info.first;
            }

            if (fdwCommand & MCI_TO) {
                DVERBOSE("    dwTo:   %d\r\n", parms->dwTo);

                if (time_format == MCI_FORMAT_TMSF) {
                    info.last = MCI_TMSF_TRACK(parms->dwTo);

                    DVERBOSE("      TRACK  %d\n", MCI_TMSF_TRACK(parms->dwTo));
                    DVERBOSE("      MINUTE %d\n", MCI_TMSF_MINUTE(parms->dwTo));
                    DVERBOSE("      SECOND %d\n", MCI_TMSF_SECOND(parms->dwTo));
                    DVERBOSE("      FRAME  %d\n", MCI_TMSF_FRAME(parms->dwTo));
                } else if (time_format == MCI_FORMAT_MILLISECONDS) {
                    info.last = info.first;

                    for (int i = info.first; i < MAX_TRACKS; i++) {
                        // FIXME: use better matching
                        if (tracks[i].position + tracks[i].length > parms->dwFrom / 1000) {
                            info.last = i;
                            break;
                        }
                    }

                    DVERBOSE("      mapped milliseconds to %d\n", info.last);
                } else {
                    info.last = parms->dwTo;
                }

                if (info.last < info.first) info.last = info.first;

                if (info.last > lastTrack)  info.last = lastTrack;
            }

            if (info.first && (fdwCommand & MCI_FROM)) {
                updateTrack = 1;
                playing = 1;

                //track info is now a global variable for live updating
                if (player == NULL)
                    player = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)player_main, NULL, 0, NULL);
                else
                    ResumeThread(player);       
            }
        }

        if (uMsg == MCI_STOP) {
            DVERBOSE("  MCI_STOP\r\n");
            playing = 0;
        }

        if (uMsg == MCI_STATUS) {
            LPMCI_STATUS_PARMS parms = (LPVOID)dwParam;

            DVERBOSE("  MCI_STATUS\r\n");

            parms->dwReturn = 0;

            if (fdwCommand & MCI_TRACK) {
                DVERBOSE("    MCI_TRACK\r\n");
                DVERBOSE("      dwTrack = %d\r\n", parms->dwTrack);
            }

            if (fdwCommand & MCI_STATUS_ITEM) {
                DVERBOSE("    MCI_STATUS_ITEM\r\n");

                if (parms->dwItem == MCI_STATUS_CURRENT_TRACK) {
                    DVERBOSE("      MCI_STATUS_CURRENT_TRACK\r\n");
                }

                if (parms->dwItem == MCI_STATUS_LENGTH) {
                    DVERBOSE("      MCI_STATUS_LENGTH\r\n");

                    int seconds = tracks[parms->dwTrack].length;

                    if (seconds) {
                        if (time_format == MCI_FORMAT_MILLISECONDS) {
                            parms->dwReturn = seconds * 1000;
                        } else {
                            parms->dwReturn = MCI_MAKE_MSF(seconds / 60, seconds % 60, 0);
                        }
                    }
                }

                if (parms->dwItem == MCI_CDA_STATUS_TYPE_TRACK) {
                    DVERBOSE("      MCI_CDA_STATUS_TYPE_TRACK\r\n");
                }

                if (parms->dwItem == MCI_STATUS_MEDIA_PRESENT) {
                    DVERBOSE("      MCI_STATUS_MEDIA_PRESENT\r\n");
                    parms->dwReturn = lastTrack > 0;
                }

                if (parms->dwItem == MCI_STATUS_NUMBER_OF_TRACKS) {
                    DVERBOSE("      MCI_STATUS_NUMBER_OF_TRACKS\r\n");
                    parms->dwReturn = numTracks;
                }

                if (parms->dwItem == MCI_STATUS_POSITION) {
                    DVERBOSE("      MCI_STATUS_POSITION\r\n");

                    if (fdwCommand & MCI_TRACK) {
                        // FIXME: implying milliseconds
                        parms->dwReturn = tracks[parms->dwTrack].position * 1000;
                    }
                }

                if (parms->dwItem == MCI_STATUS_MODE) {
                    DVERBOSE("      MCI_STATUS_MODE\r\n");
                    DVERBOSE("        we are %s\r\n", playing ? "playing" : "NOT playing");

                    parms->dwReturn = playing ? MCI_MODE_PLAY : MCI_MODE_STOP;
                }

                if (parms->dwItem == MCI_STATUS_READY) {
                    DVERBOSE("      MCI_STATUS_READY\r\n");
                }

                if (parms->dwItem == MCI_STATUS_TIME_FORMAT) {
                    DVERBOSE("      MCI_STATUS_TIME_FORMAT\r\n");
                }

                if (parms->dwItem == MCI_STATUS_START) {
                    DVERBOSE("      MCI_STATUS_START\r\n");
                }
            }

            DVERBOSE("  dwReturn %d\n", parms->dwReturn);

        }

        return 0;
    }

    /* fallback */
    return MCIERR_UNRECOGNIZED_COMMAND;
}

/*  
# LIST OF ALL POSSIBLE mciSendString COMMANDS (mark with "-" partially or completely implemented functions)#
break
capability
capture
-close
configure
copy
cue
cut
delete
escape
freeze
index
info
list
load
mark
monitor
-open
paste
pause
-play
put
quality
realize
record
reserve
restore
resume
save
seek
-set
setaudio
settimecode
settuner
setvideo
signal
spin
status
step
-stop
sysinfo
undo
unfreeze
update
where
window
*/  

// this is really fugly but for christ sake why did anyone use it?!
MCIERROR WINAPI fake_mciSendStringA(LPCTSTR cmd, LPTSTR ret, UINT cchReturn, HANDLE hwndCallback) {
    DVERBOSE("MCI-SendStringA: %s\n", cmd);

    // Change string to lower-case
    char *cmdbuf = _strdup(cmd); // Prevents cmd readonly error
    for (int i = 0; cmdbuf[i]; i++) {
        cmdbuf[i] = tolower(cmdbuf[i]);
    }
    
    // Explode string into tokens
    DVERBOSE("Splitting string \"%s\" into tokens:\n", cmdbuf);
    char * com;
    com = strtok(cmdbuf, " ,.-");
    
    // -- Implement Commands --
    
    // OPEN
    if (com && strcmp(com, "open") == 0) { // TODO: 
        return 0;
    }
    
    // SET
    if (com && strcmp(com, "set") == 0) {
        com = strtok(NULL, " ,.-"); // Get next token
        if(com){ // TODO: FIX: Accept everything. This may bring unexpected behaviour
            com = strtok(NULL, " ,.-"); // Get next token
            
            // TIME
            if (com && strcmp(com, "time") == 0) { 
                com = strtok(NULL, " ,.-"); // Get next token
                
                // FORMAT
                if (com && strcmp(com, "format") == 0) { 
                    com = strtok(NULL, " ,.-"); // Get next token
                    static MCI_SET_PARMS parms;
                    
                    // MILLISECONDS
                    if (com && strcmp(com, "milliseconds") == 0) {
                        parms.dwTimeFormat = MCI_FORMAT_MILLISECONDS;
                        fake_mciSendCommandA(MAGIC_DEVICEID, MCI_SET, MCI_SET_TIME_FORMAT, (DWORD_PTR)&parms);
                        return 0;
                    }
                    
                    // MSF
                    if (com && strcmp(com, "msf") == 0) {
                        parms.dwTimeFormat = MCI_FORMAT_MSF;
                        fake_mciSendCommandA(MAGIC_DEVICEID, MCI_SET, MCI_SET_TIME_FORMAT, (DWORD_PTR)&parms);
                        return 0;
                    }
                    
                    // TMSF
                    if (com && strcmp(com, "tmsf") == 0) {
                        parms.dwTimeFormat = MCI_FORMAT_TMSF;
                        fake_mciSendCommandA(MAGIC_DEVICEID, MCI_SET, MCI_SET_TIME_FORMAT, (DWORD_PTR)&parms);
                        return 0;
                    }
                }
            }
        }
        
        // Accept all other commands
        return 0;
    }
    
    // STATUS
    if (com && strcmp(com, "status") == 0) {
        com = strtok(NULL, " ,.-"); // Get next token
        
        if (com) { // TODO: FIX: Accept everything. This may bring unexpected behaviour
            com = strtok(NULL, " ,.-"); // Get next token
            MCI_STATUS_PARMS parms;

            // LENGTH
            if (com && strcmp(com, "length") == 0) {
                parms.dwItem = MCI_STATUS_LENGTH;
                com = strtok(NULL, " ,.-"); // Get next token
                
                // TRACK
                if (com && strcmp(com, "track") == 0) {
                    com = strtok(NULL, " ,.-"); // Get next token (TRACK NUMBER)
                    
                    // (INT) TRACK NUMBER
                    if(com) { // TODO: Check if this is an INTEGER (Number)
                        parms.dwTrack = atoi(com);
                        fake_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&parms);
                        _itoa(parms.dwReturn, ret, 10); // Response
                        return 0;
                    }   
                }
                
                return 0;
            }
        
            // POSITION
            if (com && strcmp(com, "position") == 0) {
                parms.dwItem = MCI_STATUS_POSITION;
                com = strtok(NULL, " ,.-"); // Get next token
                
                // TRACK
                if (com && strcmp(com, "track") == 0) {
                    com = strtok(NULL, " ,.-"); // Get next token (TRACK NUMBER)
                    
                    // (INT) TRACK NUMBER
                    if(com){ // TODO: Check if this is an INTEGER (Number)
                        parms.dwTrack = atoi(com);
                        fake_mciSendCommandA(MAGIC_DEVICEID, MCI_STATUS, MCI_STATUS_ITEM|MCI_TRACK, (DWORD_PTR)&parms);
                        _itoa(parms.dwReturn, ret, 10); // Response
                        return 0;
                    }   
                }
                
                return 0;
            }
            
            // NUMBER
            if (com && strcmp(com, "number") == 0) {
                com = strtok(NULL, " ,.-"); // Get next token

                // OF
                if (com && strcmp(com, "of") == 0) {
                    com = strtok(NULL, " ,.-"); // Get next token

                    // TRACKS
                    if (com && strcmp(com, "tracks") == 0) {
                        _itoa(numTracks, ret, 10); // Response
                        return 0;
                    }   
                }

                return 0;
            }
        }
        
        // Accept all other commands
        return 0;
    }   
    
    // PLAY
    if (com && strcmp(com, "play") == 0) {
        com = strtok(NULL, " ,.-"); // Get next token
        
        if(com) { // TODO: FIX: Accept everything. This may bring unexpected behaviour
            com = strtok(NULL, " ,.-"); // Get next token

            // FROM
            if (com && strcmp(com, "from") == 0) {
                com = strtok(NULL, " ,.-"); // Get next token (FROM POS (INT))
                
                // (INT) From Time
                if(com) { // TODO: Check if number is INTEGER
                    
                    int posFrom = atoi(com);// Parse Integer
                    
                    com = strtok(NULL, " ,.-"); // Get next token
                    
                    // TO
                    if (com && strcmp(com, "to") == 0) {
                        com = strtok(NULL, " ,.-"); // Get next token (TO POS (INT)))
                            
                            // (INT) To Time
                            if(com) {
                                int posTo = atoi(com); // Parse Integer
                            
                                static MCI_PLAY_PARMS parms;
                                parms.dwFrom = posFrom;
                                parms.dwTo = posTo;
                                fake_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_FROM|MCI_TO, (DWORD_PTR)&parms);
                                //free(posFrom); // ???
                                //free(posTo); // ???
                                return 0;
                            }
                    } else {
                        // No TO position specified
                        static MCI_PLAY_PARMS parms;
                        parms.dwFrom = posFrom;
                        fake_mciSendCommandA(MAGIC_DEVICEID, MCI_PLAY, MCI_FROM, (DWORD_PTR)&parms);
                        return 0;
                    }
                }
            }
        }
        
        // Accept all other commands
        return 0;
    }
    
    // STOP
    if (com && strcmp(com, "stop") == 0) {
        // TODO: No support for ALIASES
        fake_mciSendCommandA(MAGIC_DEVICEID, MCI_STOP, 0, (DWORD_PTR)NULL);
        return 0;
    }
    
    // CLOSE
    if (com && strcmp(com, "close") == 0) {
        // TODO: No support for ALIASES
        fake_mciSendCommandA(MAGIC_DEVICEID, MCI_CLOSE, 0, (DWORD_PTR)NULL);
        return 0;
    }

    // TODO: Unfinished. Dunno what this does.. 
    if (strstr(cmd, "sysinfo")) {
        strcpy(ret, "cd");
        return 0;
    }
    
    /* This could be useful if this would be 100% implemented */
    // return MCIERR_UNRECOGNIZED_COMMAND;
    
    return 0;
}

UINT WINAPI fake_auxGetNumDevs() {
    DVERBOSE("fake_auxGetNumDevs()\r\n");
    return 1;
}

MMRESULT WINAPI fake_auxGetDevCapsA(UINT_PTR uDeviceID, LPAUXCAPS lpCaps, UINT cbCaps) {
    DVERBOSE("fake_auxGetDevCapsA(uDeviceID=%08X, lpCaps=%p, cbCaps=%08X\n", uDeviceID, lpCaps, cbCaps);

    lpCaps->wMid = 2 /*MM_CREATIVE*/;
    lpCaps->wPid = 401 /*MM_CREATIVE_AUX_CD*/;
    lpCaps->vDriverVersion = 1;
    strcpy(lpCaps->szPname, "ogg-winmm virtual CD");
    lpCaps->wTechnology = AUXCAPS_CDAUDIO;
    lpCaps->dwSupport = AUXCAPS_VOLUME;

    return MMSYSERR_NOERROR;
}


MMRESULT WINAPI fake_auxGetVolume(UINT uDeviceID, LPDWORD lpdwVolume) {
    DVERBOSE("fake_auxGetVolume(uDeviceId=%08X, lpdwVolume=%p)\r\n", uDeviceID, lpdwVolume);
    *lpdwVolume = 0x00000000;
    return MMSYSERR_NOERROR;
}

MMRESULT WINAPI fake_auxSetVolume(UINT uDeviceID, DWORD dwVolume) {
    static DWORD oldVolume = -1;
    char cmdbuf[256];

    DVERBOSE("fake_auxSetVolume(uDeviceId=%08X, dwVolume=%08X)\r\n", uDeviceID, dwVolume);

    if (dwVolume == oldVolume)
    {
        return MMSYSERR_NOERROR;
    }

    oldVolume = dwVolume;

    unsigned short left = LOWORD(dwVolume);
    unsigned short right = HIWORD(dwVolume);

    DVERBOSE("    left : %ud (%04X)\n", left, left);
    DVERBOSE("    right: %ud (%04X)\n", right, right);

    plr_volume(left, right);

    return MMSYSERR_NOERROR;
}

