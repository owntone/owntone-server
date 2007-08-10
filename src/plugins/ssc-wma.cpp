/*
 * $Id: $
 *
 * Win32-only transcoder for WMA using the Windows Media Format SDK.
 */

#define _WIN32_DCOM 

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>
#include <mmreg.h>
#include <wmsdk.h>


#include "ff-plugins.h"

#ifndef TRUE
# define TRUE 1
# define FALSE 0
#endif

typedef struct tag_ssc_handle {
    int state;
    IWMSyncReader *pReader;
    int errnum;

    int duration;
    char wav_header[44];
    int wav_offset;

    INSSBuffer *pBuffer;
    BYTE *pdata;
    DWORD data_len;
    int offset;

    DWORD channels;
    DWORD sample_rate;
    WORD bits_per_sample;
} SSCHANDLE;

#define STATE_DONE        0
#define STATE_OPEN        1
#define STATE_STREAMOPEN  2

#define SSC_WMA_E_SUCCESS      0
#define SSC_WMA_E_NOCOM        1
#define SSC_WMA_E_NOREADER     2
#define SSC_WMA_E_OPEN         3
#define SSC_WMA_E_READ         4

char *_ssc_wma_errors[] = {
    "Success",
    "Could not initialize COM",
    "Could not create WMA reader",
    "Could not open file",
    "Error while reading file"
};
    


/* Forwards */
void *ssc_wma_init(void);
void ssc_wma_deinit(void *pv);
int ssc_wma_open(void *pv, MP3FILE *pmp3);
int ssc_wma_close(void *pv);
int ssc_wma_read(void *pv, char *buffer, int len);
char *ssc_wma_error(void *pv);

/* Globals */
PLUGIN_TRANSCODE_FN _ptfn = {
    ssc_wma_init,
    ssc_wma_deinit,
    ssc_wma_open,
    ssc_wma_close,
    ssc_wma_read,
    ssc_wma_error
};

PLUGIN_INFO _pi = {
    PLUGIN_VERSION,        /* version */
    PLUGIN_TRANSCODE,      /* type */
    "ssc-wma/" VERSION,    /* server */
    NULL,                  /* output fns */
    NULL,                  /* event fns */
    &_ptfn,                /* fns */
    NULL,                  /* rend info */
    "wma,wmal,wmap,wmav"   /* codeclist */
};

/**
 * return the string representation of the last error
 */
char *ssc_wma_error(void *pv) {
    SSCHANDLE *handle = (SSCHANDLE*)pv;

    return _ssc_wma_errors[handle->errnum];
}

PLUGIN_INFO *plugin_info(void) {
    return &_pi;
}

void *ssc_wma_init(void) {
    SSCHANDLE *handle;
    HRESULT hr;

    hr = CoInitializeEx(NULL,COINIT_MULTITHREADED);
    if(FAILED(hr)) {
        pi_log(E_INF,"Could not initialize COM, Error code: 0x%08X\n",hr);
        return NULL;
    }

    handle=(SSCHANDLE *)malloc(sizeof(SSCHANDLE));
    if(handle) {
        memset(handle,0,sizeof(SSCHANDLE));
    }

    return (void*)handle;
}

void ssc_wma_deinit(void *vp) {
    SSCHANDLE *handle = (SSCHANDLE *)vp;
    ssc_wma_close(handle);
    
    if(handle) {
        free(handle);
        CoUninitialize();
    }

    return;
}

int ssc_wma_open(void *vp, MP3FILE *pmp3) {
    SSCHANDLE *handle = (SSCHANDLE*)vp;
    HRESULT hr = S_OK;
    WCHAR fname[PATH_MAX];
    DWORD byte_count;
    char *file;
    char *codec;
    int duration;

    file = pmp3->path;
    codec = pmp3->codectype;
    duration = pmp3->song_length;

    if(!handle)
        return FALSE;

    handle->state = STATE_DONE;
    handle->duration = duration;
    handle->errnum = SSC_WMA_E_OPEN;

    hr = WMCreateSyncReader(NULL,0,&handle->pReader);
    if(FAILED(hr)) {
        pi_log(E_INF,"Could not create WMA reader.  Error code: 0x%08X\n",hr);
        handle->errnum = SSC_WMA_E_NOREADER;
        return FALSE;
    }

    /* convert file name to wchar */
    MultiByteToWideChar(CP_UTF8,0,file,-1,fname,sizeof(fname)/sizeof(fname[0]));

    hr = handle->pReader->Open(fname);
    if(FAILED(hr)) {
        pi_log(E_INF,"Could not open file.  Error code: 0x%08X\n",hr);
        return FALSE;
    }
    handle->state=STATE_OPEN;

    hr = handle->pReader->SetRange(0,0);
    if(FAILED(hr)) {
        pi_log(E_INF,"Could not set range.  Error code: 0x%08X\n",hr);
        return FALSE;
    }

    hr = handle->pReader->SetReadStreamSamples(1,0);
    if(FAILED(hr)) {
        pi_log(E_INF,"Could not stream samples.  Error code: 0x%08X\n",hr);
        return FALSE;
    }

    handle->channels = 2;
    handle->bits_per_sample = 16;
    handle->sample_rate = 44100;

    IWMOutputMediaProps *pprops;
    hr = handle->pReader->GetOutputFormat(0,0,&pprops);
    if(FAILED(hr)) {
        pi_log(E_LOG,"Could not get output format for %s\n",file);
        return TRUE; /* we'll assume 44100/16/2 */
    }

    hr = pprops->GetMediaType(NULL,&byte_count);
    if(FAILED(hr)) {
        pi_log(E_LOG,"Could not get media type for %s\n",file);
        return TRUE;
    }

    WM_MEDIA_TYPE *ptype = (WM_MEDIA_TYPE*)calloc(byte_count,1);
    if(!ptype) {
        pi_log(E_FATAL,"ssc_wma_open: malloc\n");
    }

    hr = pprops->GetMediaType(ptype, &byte_count);
    if(FAILED(hr)) {
        free(ptype);
        return TRUE;
    }

    /* now get sample info */
    if(ptype->formattype == WMFORMAT_WaveFormatEx) {
        WAVEFORMATEX *pformat = (WAVEFORMATEX*)ptype->pbFormat;
        handle->channels = pformat->nChannels;
        handle->sample_rate = pformat->nSamplesPerSec;
        handle->bits_per_sample = pformat->wBitsPerSample;
    }

    free(ptype);

    return TRUE;
}

int ssc_wma_close(void *vp) {
    SSCHANDLE *handle = (SSCHANDLE *)vp;

    if(!handle)
        return TRUE;

    if(handle->state >= STATE_OPEN) {
        handle->pReader->Close();
    }

    if(handle->pReader)
        handle->pReader->Release();
    handle->pReader = NULL;

    handle->state = STATE_DONE;
    return TRUE;
}



int ssc_wma_read(void *vp, char *buffer, int len) {
    SSCHANDLE *handle = (SSCHANDLE *)vp;
    int bytes_returned = 0;
    int bytes_to_copy;
    HRESULT hr;

    unsigned int channels, sample_rate, bits_per_sample;
    unsigned int byte_rate, block_align, duration;

    QWORD sample_time=0, sample_duration=0;
    DWORD sample_len=0, flags=0, output_number=0;
    
    /* if we have not yet sent the header, let's do that first */
    if(handle->wav_offset != sizeof(handle->wav_header)) {
        /* still have some to send */
        if(!handle->wav_offset) {
            /* Should pull this from format info in the wma file */
            channels = handle->channels;
            sample_rate = handle->sample_rate;
            bits_per_sample = handle->bits_per_sample;

            if(handle->duration)
                duration = handle->duration;

            sample_len = ((bits_per_sample * sample_rate * channels / 8) * (duration/1000));
            byte_rate = sample_rate * channels * bits_per_sample / 8;
            block_align = channels * bits_per_sample / 8;

            pi_log(E_DBG,"Channels.......: %d\n",channels);
            pi_log(E_DBG,"Sample rate....: %d\n",sample_rate);
            pi_log(E_DBG,"Bits/Sample....: %d\n",bits_per_sample);

            memcpy(&handle->wav_header[0],"RIFF",4);
            *((unsigned int*)(&handle->wav_header[4])) = 36 + sample_len;
            memcpy(&handle->wav_header[8],"WAVE",4);
            memcpy(&handle->wav_header[12],"fmt ",4);
            *((unsigned int*)(&handle->wav_header[16])) = 16;
            *((unsigned short*)(&handle->wav_header[20])) = 1;
            *((unsigned short*)(&handle->wav_header[22])) = channels;
            *((unsigned int*)(&handle->wav_header[24])) = sample_rate;
            *((unsigned int*)(&handle->wav_header[28])) = byte_rate;
            *((unsigned short*)(&handle->wav_header[32])) = block_align;
            *((unsigned short*)(&handle->wav_header[34])) = bits_per_sample;
            memcpy(&handle->wav_header[36],"data",4);
            *((unsigned int*)(&handle->wav_header[40])) = sample_len;
        }
        
        bytes_to_copy = sizeof(handle->wav_header) - handle->wav_offset;
        if(len < bytes_to_copy)
            bytes_to_copy = len;

        memcpy(buffer,&handle->wav_header[handle->wav_offset],bytes_to_copy);
        handle->wav_offset += bytes_to_copy;
        return bytes_to_copy;
    }

    /* see if we have any leftover data */
    if(handle->data_len) {
        bytes_returned = handle->data_len;
        if(bytes_returned > len) {
            bytes_returned = len;
        }

        memcpy(buffer,handle->pdata + handle->offset,bytes_returned);
        handle->offset += bytes_returned;
        handle->data_len -= bytes_returned;

        if(!handle->data_len) {
            handle->pBuffer->Release();
            handle->pBuffer = NULL;
        }

        return bytes_returned;
    }

    handle->offset = 0;
    hr = handle->pReader->GetNextSample(1,&handle->pBuffer,&sample_time, &sample_duration, &flags, &output_number, NULL);
    if(SUCCEEDED(hr)) {
        hr = handle->pBuffer->GetBufferAndLength(&handle->pdata, &handle->data_len);
        if(FAILED(hr)) {
            pi_log(E_LOG,"Read error while transcoding file\n");
            handle->errnum = SSC_WMA_E_READ;
            return -1;
        }

//        pi_log(E_SPAM,"Read %d bytes\n",handle->data_len);

        bytes_returned = handle->data_len;
        if(bytes_returned > len)
            bytes_returned = len;

        memcpy(buffer,handle->pdata + handle->offset,bytes_returned);
        handle->offset += bytes_returned;
        handle->data_len -= bytes_returned;

        if(!handle->data_len) {
            handle->pBuffer->Release();
            handle->pBuffer = NULL;
        }
    } else {
        return 0;
    }
    
    return bytes_returned;
}
