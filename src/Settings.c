#include <stdio.h>
#include "Settings.h"

// initial creation of the settings object
struct Settings Settings;

void sett_initDefaults(){
    // default 2 Threads
    Settings.thread_count = 2;

    // turn off hardware decode
    Settings.hardware_decode = 0;

    // no special encoding
    Settings.use_cuvid = 0;
    Settings.use_vdpau = 0;
    Settings.use_dxva2 = 0;

    // normal speed as default
    Settings.play_nice = 0;

    // disable split up of streams
    Settings.output_demux = 0;

    // default demux pid = 0
    Settings.demux_pid = 0;

    Settings.commDetectMethod = BLACK_FRAME + LOGO + RESOLUTION_CHANGE + AR + SILENCE + (PROCESS_CC ? CC : 0);
}

void sett_setThreadCount(int threads){
    Settings.thread_count = threads;
}

void sett_enableHardwareDecoding(int enable){
    Settings.hardware_decode = enable;
}

void sett_useCuvid(int enable){
    printf("Enabling use_cuvid\n");
    Settings.use_cuvid=enable;
}

void sett_usevdpau(int enable){
    printf("Enabling use_vdpau\n");
    Settings.use_vdpau = enable;
}

void sett_usedxva2(int enable){
    printf("Enabling use_dxva2\n");
    Settings.use_dxva2 = enable;
}

void sett_enableplayNice(int enable){
    printf("ComSkip playing nice due as per command line.\n");
    Settings.play_nice = enable;
}

void sett_output_demux(int enable){
    Settings.output_demux = enable;
}

void sett_setDemuxPID(int PID){
    printf("Selecting PID %x as per command line.\n", PID);
    Settings.demux_pid = PID;
}

void sett_isTSStream(){
    printf("Auto selecting the PID.\n");
    Settings.demux_pid = 1;
}

void sett_setDetectionMethod(int Method){
    printf("Setting detection methods to %i as per command line.\n", Method);
    Settings.commDetectMethod = Method;
}


