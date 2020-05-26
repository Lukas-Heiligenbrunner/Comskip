//
// Created by lukas on 23.05.20.
//

#ifndef COMSKIP_SETTINGS_H
#define COMSKIP_SETTINGS_H

// Define detection methods
#define BLACK_FRAME       1
#define LOGO              2
#define SCENE_CHANGE      4
#define RESOLUTION_CHANGE 8
#define CC                16
#define AR                32
#define SILENCE           64
#define CUTSCENE          128

/**
 *  enable cc detection method
 */
#define PROCESS_CC 1

/**
 * a struct containing all relevant settings
 * should be read only
 */
struct Settings {
    /**
     * the Threads to be used for decoding
     */
    int thread_count;

    /**
     * allow hardware decoding?
     */
    int hardware_decode;

    /**
     * use special decoding?
     */
    int use_cuvid;
    int use_vdpau;
    int use_dxva2;

    /**
     * slows down decoding if enabled
     */
    int play_nice;

    /**
     * Demux the input into elementary streams
     */
    int output_demux;

    /**
     * The PID of the video in the TS
     */
    int demux_pid;

    /**
     * detection Method
     */
    int commDetectMethod;
};

/**
 * object containg the settings
 */
extern struct Settings Settings;


/**
 * initialize all default values to the settings
 */
void sett_initDefaults();


/**
 * sets the Threads to be used for decoding
 */
void sett_setThreadCount(int threads);

/**
 * Activate Hardware Assisted video decoding
 */
void sett_enableHardwareDecoding(int enable);

/**
 * Use NVIDIA Video Decoder (CUVID), if available
 */
void sett_useCuvid(int enable);

/**
 * Use NVIDIA Video Decode and Presentation API (VDPAU), if available
 */
void sett_usevdpau(int enable);

/**
 * Use DXVA2 Video Decode and Presentation API (DXVA2), if available
 */
void sett_usedxva2(int enable);

/**
 * slow down encoding speed
 */
void sett_enableplayNice(int enable);

/**
 * Demux the input into elementary streams
 */
void sett_output_demux(int enable);

/**
 * set the PID of the video in the TS
 */
void sett_setDemuxPID(int PID);

/**
 * sets PID to 1 if ts
 */
void sett_isTSStream();

/**
 * An integer sum of the detection methods to use
 *
 * Detection methods available:
 * 1   - Black Frame
 * 2   - Logo
 * 4   - Scene Change
 * 8   - Resolution Change
 * 16  - Closed Captions
 * 32  - Aspect Ratio
 * 64  - Silence
 * 128 - CutScenes
 * 255 - USE ALL AVAILABLE
 */
void sett_setDetectionMethod(int Method);

#endif //COMSKIP_SETTINGS_H
