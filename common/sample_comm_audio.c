/******************************************************************************
  A simple program of Hisilicon HI3516 audio input/output/encoder/decoder implementation.
  Copyright (C), 2010-2011, Hisilicon Tech. Co., Ltd.
 ******************************************************************************
    Modification:  2011-2 Created
******************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>

#include "sample_comm.h"


#ifdef __cplusplus
#if __cplusplus
extern "C"{
#endif
#endif /* End of #ifdef __cplusplus */

#define ACODEC_FILE     "/dev/acodec"

#define AUDIO_ADPCM_TYPE ADPCM_TYPE_DVI4/* ADPCM_TYPE_IMA, ADPCM_TYPE_DVI4*/
#define G726_BPS MEDIA_G726_40K         /* MEDIA_G726_16K, MEDIA_G726_24K ... */

typedef struct tagSAMPLE_AENC_S
{
    HI_BOOL bStart;
    pthread_t stAencPid;
    HI_S32  AeChn;
    HI_S32  AdChn;
    FILE    *pfd;
    HI_BOOL bSendAdChn;
} SAMPLE_AENC_S;

typedef struct tagSAMPLE_AI_S
{
    HI_BOOL bStart;
    HI_S32  AiDev;
    HI_S32  AiChn;
    HI_S32  AencChn;
    HI_S32  AoDev;
    HI_S32  AoChn;
    HI_BOOL bSendAenc;
    HI_BOOL bSendAo;
    pthread_t stAiPid;
} SAMPLE_AI_S;

typedef struct tagSAMPLE_ADEC_S
{
    HI_BOOL bStart;
    HI_S32 AdChn; 
    FILE *pfd;
    pthread_t stAdPid;
} SAMPLE_ADEC_S;

static SAMPLE_AI_S gs_stSampleAi[SIO_MAX_NUM*AIO_MAX_CHN_NUM];
static SAMPLE_AENC_S gs_stSampleAenc[AENC_MAX_CHN_NUM];
static SAMPLE_ADEC_S gs_stSampleAdec[ADEC_MAX_CHN_NUM];


HI_S32 SAMPLE_TW2865_SetFormat(AIO_MODE_E enWorkMode)
{
    int fd;
    tw2865_audio_format audio_farmat;
    
    fd = open(TW2865_FILE, O_RDWR);
    if (fd < 0)
    {
        printf("open %s fail\n", TW2865_FILE);
        return -1;
    }

    if (AIO_MODE_PCM_SLAVE_STD == enWorkMode
        || AIO_MODE_PCM_SLAVE_NSTD == enWorkMode
        || AIO_MODE_PCM_MASTER_STD == enWorkMode
        || AIO_MODE_PCM_MASTER_NSTD == enWorkMode)
    {
        audio_farmat.format = 1;
    }
    else
    {
        audio_farmat.format = 0;    
    }
    
    if (ioctl(fd, TW2865_SET_AUDIO_FORMAT, &audio_farmat))
    {
        printf("ioctl TW2865_SET_AUDIO_FORMAT err !!! \n");
        close(fd);
        return -1;
    }
    
    close(fd);
    return 0;
}

HI_S32 SAMPLE_TW2865_CfgAudio(AUDIO_SAMPLE_RATE_E enSample)
{
    int fd;
    tw2865_audio_samplerate samplerate;
        
    fd = open(TW2865_FILE, O_RDWR);
    if (fd < 0)
    {
        printf("open %s fail\n", TW2865_FILE);
        return -1;
    }
    
    if (AUDIO_SAMPLE_RATE_8000 == enSample)
    {
        samplerate = TW2865_SAMPLE_RATE_8000;
    }
    else if (AUDIO_SAMPLE_RATE_16000 == enSample)
    {
        samplerate = TW2865_SAMPLE_RATE_16000;
    }
    else if (AUDIO_SAMPLE_RATE_32000 == enSample)
    {
        samplerate = TW2865_SAMPLE_RATE_32000;
    }
    else if (AUDIO_SAMPLE_RATE_44100 == enSample)
    {
        samplerate = TW2865_SAMPLE_RATE_44100;
    }
    else if (AUDIO_SAMPLE_RATE_48000 == enSample)
    {
        samplerate = TW2865_SAMPLE_RATE_48000;
    }
    else 
    {
        printf("func(%s) line(%d): tw2865 not support enSample:%d\n",
                __FUNCTION__, __LINE__, enSample);
        return -1;
    }
        
    if (ioctl(fd, TW2865_SET_SAMPLE_RATE, &samplerate))
    {
        printf("ioctl TW2865_SET_SAMPLE_RATE err !!! \n");
        close(fd);
        return -1;
    }

    printf("func(%s) line(%d): tw2865 start enSample(%d) ok\n",
            __FUNCTION__, __LINE__, enSample);

    close(fd);
    return 0;
}

HI_S32 SAMPLE_TW2865_SetBitwidth(AUDIO_BIT_WIDTH_E enBitwidth)
{
    int fd;
    tw2865_audio_bitwidth enTw2865Bitwidth;
    
    switch (enBitwidth)
    {
        case AUDIO_BIT_WIDTH_8:
            enTw2865Bitwidth = TW2865_AUDIO_BITWIDTH_8;
            break;

        case AUDIO_BIT_WIDTH_16:
            enTw2865Bitwidth = TW2865_AUDIO_BITWIDTH_16;
            break;

        default:
            printf("func(%s) line(%d): tw2865 not support bitwidth %d!\n",
                    __FUNCTION__, __LINE__, enBitwidth);
            return HI_FAILURE;
    }
        
    fd = open(TW2865_FILE, O_RDWR);
    if (fd < 0)
    {
        printf("open %s fail\n", TW2865_FILE);
        return HI_FAILURE;
    }
    
    if (ioctl(fd, TW2865_SET_AUDIO_BITWIDTH, &enTw2865Bitwidth))
    {
        printf("func(%s) line(%d): ioctl TW2865_SET_AUDIO_BITWIDTH err!\n",
                    __FUNCTION__, __LINE__);
        close(fd);
        return HI_FAILURE;
    }
    close(fd);
    return HI_SUCCESS;
}

HI_S32 SAMPLE_Tlv320_CfgAudio(AIO_MODE_E enWorkmode,AUDIO_SAMPLE_RATE_E enSample)
{
     HI_S32 s32Samplerate;
     HI_S32 vol = 0x100;
     Audio_Ctrl audio_ctrl;
     int s_fdTlv = -1;
     HI_BOOL bPCMmode = HI_FALSE;
     HI_BOOL bMaster = HI_TRUE;      /* �������ģʽ�Ƕ���Tlv320aic31��˵�� */
     HI_BOOL bPCMStd = HI_FALSE;
 
     /* aic31�����һ��12.288M�ľ��񣬶���44.1kϵ�еĲ�������48kϵ�еĲ����ʣ�
         ��Ҫ��aic31���ò�ͬ��P��R��J��Dֵ��������������һ��־����¼ */
     HI_BOOL b44100HzSeries = HI_FALSE;         
     
     if (AUDIO_SAMPLE_RATE_8000 == enSample)
     {
         s32Samplerate = AC31_SET_8K_SAMPLERATE;
     }
     else if (AUDIO_SAMPLE_RATE_12000 == enSample)
     {
         s32Samplerate = AC31_SET_12K_SAMPLERATE;
     }
     else if (AUDIO_SAMPLE_RATE_11025 == enSample)
     {
         b44100HzSeries = HI_TRUE;
         s32Samplerate = AC31_SET_11_025K_SAMPLERATE;
     }
     else if (AUDIO_SAMPLE_RATE_16000 == enSample)
     {
         s32Samplerate = AC31_SET_16K_SAMPLERATE;
     }
     else if (AUDIO_SAMPLE_RATE_22050 == enSample)
     {
         b44100HzSeries = HI_TRUE;
         s32Samplerate = AC31_SET_22_05K_SAMPLERATE;
     }
     else if (AUDIO_SAMPLE_RATE_24000 == enSample)
     {
         s32Samplerate = AC31_SET_24K_SAMPLERATE;
     }
     else if (AUDIO_SAMPLE_RATE_32000 == enSample)
     {
         s32Samplerate = AC31_SET_32K_SAMPLERATE;
     }
     else if (AUDIO_SAMPLE_RATE_44100 == enSample)
     {
         b44100HzSeries = HI_TRUE;
         s32Samplerate = AC31_SET_44_1K_SAMPLERATE;
     }
     else if (AUDIO_SAMPLE_RATE_48000 == enSample)
     {
         s32Samplerate = AC31_SET_48K_SAMPLERATE;
     }
     else 
     {
         printf("SAMPLE_Tlv320_CfgAudio(), not support enSample:%d\n",enSample);
         return -1;
     }
 
     if(AIO_MODE_I2S_MASTER == enWorkmode) 
     {
         bPCMmode = HI_FALSE;
         bMaster = HI_FALSE;
     }
     else if(AIO_MODE_I2S_SLAVE == enWorkmode)
     {
         bPCMmode = HI_FALSE;
         bMaster = HI_TRUE;
     }
     else if((AIO_MODE_PCM_MASTER_NSTD == enWorkmode)||(AIO_MODE_PCM_MASTER_STD == enWorkmode))
     {
         bPCMmode = HI_TRUE;
         bMaster = HI_FALSE;
     }
     else if((AIO_MODE_PCM_SLAVE_NSTD == enWorkmode)||(AIO_MODE_PCM_SLAVE_STD == enWorkmode))
     {
         bPCMmode = HI_TRUE;
         bMaster = HI_TRUE;
     }
     else
     {
         printf("SAMPLE_Tlv320_CfgAudio(), not support workmode:%d\n\n",enWorkmode);
     }
 
     s_fdTlv = open(TLV320_FILE,O_RDWR);
     if (s_fdTlv < 0)
     {
         printf("can't open tlv320,%s\n", TLV320_FILE);
         return -1;   
     }   
 
     audio_ctrl.chip_num = 0;
     if (ioctl(s_fdTlv,SOFT_RESET,&audio_ctrl))
     {
         printf("[Func]:%s [Line]:%d [Info]:%s\n", __FUNCTION__, __LINE__, "tlv320aic31 reset failed");
     }

     /* ��������ģʽ 1Ϊ��ģʽ*/ 
     audio_ctrl.ctrl_mode = bMaster;
     audio_ctrl.if_44100hz_series = b44100HzSeries;
     audio_ctrl.sample = s32Samplerate;
     ioctl(s_fdTlv,SET_CTRL_MODE,&audio_ctrl); 
     
     /* set transfer mode 0:I2S 1:PCM */
     audio_ctrl.trans_mode = bPCMmode;
     if (ioctl(s_fdTlv,SET_TRANSFER_MODE,&audio_ctrl))
     {
         printf("set tlv320aic31 trans_mode err\n");
         close(s_fdTlv);
         return -1;
     }
         
     /*set sample of DAC and ADC */
     if (ioctl(s_fdTlv,SET_DAC_SAMPLE,&audio_ctrl))
     {
         printf("ioctl err1\n");
         close(s_fdTlv);
         return -1;
     }
     
     if (ioctl(s_fdTlv,SET_ADC_SAMPLE,&audio_ctrl))
     {
         printf("ioctl err2\n");
         close(s_fdTlv);
         return -1;
     }   
     
     /*set volume control of left and right DAC */
     audio_ctrl.if_mute_route = 0;
     audio_ctrl.input_level = 0;
     ioctl(s_fdTlv,LEFT_DAC_VOL_CTRL,&audio_ctrl);
     ioctl(s_fdTlv,RIGHT_DAC_VOL_CTRL,&audio_ctrl);
     
     /*Right/Left DAC Datapath Control */
     audio_ctrl.if_powerup = 1;/*Left/Right DAC datapath plays left/right channel input data*/
     ioctl(s_fdTlv,LEFT_DAC_POWER_SETUP,&audio_ctrl);
     ioctl(s_fdTlv,RIGHT_DAC_POWER_SETUP,&audio_ctrl);
 
     /* ����PCM��׼ģʽ�ͷǱ�׼ģʽ */
     if ((AIO_MODE_PCM_MASTER_STD == enWorkmode)||(AIO_MODE_PCM_SLAVE_STD == enWorkmode))
     {
         bPCMStd = HI_TRUE;
         audio_ctrl.data_offset = bPCMStd;
         ioctl(s_fdTlv,SET_SERIAL_DATA_OFFSET,&audio_ctrl);
     }
     else if ((AIO_MODE_PCM_MASTER_NSTD == enWorkmode)||(AIO_MODE_PCM_SLAVE_NSTD == enWorkmode))
     {
         bPCMStd = HI_FALSE;
         audio_ctrl.data_offset = bPCMStd;
         ioctl(s_fdTlv,SET_SERIAL_DATA_OFFSET,&audio_ctrl);
     }
     else
     {;}
     
     /* ����λ�� (0:16bit 1:20bit 2:24bit 3:32bit) */
     audio_ctrl.data_length = 0;
     ioctl(s_fdTlv,SET_DATA_LENGTH,&audio_ctrl);
   
     /*DACL1 TO LEFT_LOP/RIGHT_LOP VOLUME CONTROL 82 92*/
     audio_ctrl.if_mute_route = 1;/* route*/
     audio_ctrl.input_level = vol; /*level control*/
     ioctl(s_fdTlv,DACL1_2_LEFT_LOP_VOL_CTRL,&audio_ctrl);
     ioctl(s_fdTlv,DACR1_2_RIGHT_LOP_VOL_CTRL,&audio_ctrl);
 
     /* LEFT_LOP/RIGHT_LOP OUTPUT LEVEL CONTROL 86 93*/
     audio_ctrl.if_mute_route = 1;
     audio_ctrl.if_powerup = 1;
     audio_ctrl.input_level = 0;
     ioctl(s_fdTlv,LEFT_LOP_OUTPUT_LEVEL_CTRL,&audio_ctrl);
     ioctl(s_fdTlv,RIGHT_LOP_OUTPUT_LEVEL_CTRL,&audio_ctrl);
 
     /*����AD*/
     /* LEFT/RIGHT ADC PGA GAIN CONTROL 15 16*/    
     audio_ctrl.if_mute_route =0;    
     audio_ctrl.input_level = 0;    
     ioctl(s_fdTlv,LEFT_ADC_PGA_CTRL,&audio_ctrl);    
     ioctl(s_fdTlv,RIGHT_ADC_PGA_CTRL,&audio_ctrl); 
 
     /*INT2L TO LEFT/RIGTH ADCCONTROL 17 18*/    
     audio_ctrl.input_level = 0;    
     ioctl(s_fdTlv,IN2LR_2_LEFT_ADC_CTRL,&audio_ctrl);    
     ioctl(s_fdTlv,IN2LR_2_RIGTH_ADC_CTRL,&audio_ctrl); 
 
     /*IN1L_2_LEFT/RIGTH_ADC_CTRL 19 22*/    
     /*audio_ctrl.input_level = 0xf;    
     audio_ctrl.if_powerup = 1;
     printf("audio_ctrl.input_level=0x%x,audio_ctrl.if_powerup=0x%x\n",audio_ctrl.input_level,audio_ctrl.if_powerup);
     if (ioctl(s_fdTlv,IN1L_2_LEFT_ADC_CTRL,&audio_ctrl)==0)
         perror("ioctl err\n");    
     getchar();
     printf("audio_ctrl.input_level=0x%x,audio_ctrl.if_powerup=0x%x\n",audio_ctrl.input_level,audio_ctrl.if_powerup);
     ioctl(s_fdTlv,IN1R_2_RIGHT_ADC_CTRL,&audio_ctrl); 
     getchar();
     printf("set 19 22\n");*/
 
     close(s_fdTlv);
     printf("Set aic31 ok: bMaster = %d, enWorkmode = %d, enSamplerate = %d\n",
             bMaster, enWorkmode, enSample);
     return 0;
 }


HI_S32 SAMPLE_Tlv320_Disable()
{
    Audio_Ctrl audio_ctrl;
    int s_fdTlv = -1;
    HI_S32 s32Ret;
    
    s_fdTlv = open(TLV320_FILE,O_RDWR);
    if (s_fdTlv < 0)
    {
        printf("[Func]:%s [Line]:%d [Info]:%s\n", __FUNCTION__, __LINE__, "can't open /dev/tlv320aic31");
        return HI_FAILURE;   
    }   
    
    /* reset transfer mode 0:I2S 1:PCM */
    audio_ctrl.chip_num = 0;
    s32Ret = ioctl(s_fdTlv,SOFT_RESET,&audio_ctrl);
    if (HI_SUCCESS != s32Ret)
    {
        printf("[Func]:%s [Line]:%d [Info]:%s\n", __FUNCTION__, __LINE__, "tlv320aic31 reset failed");
    }
    close(s_fdTlv);
    
    return s32Ret;
}

/* config codec */ 
HI_S32 SAMPLE_COMM_AUDIO_CfgAcodec(AIO_ATTR_S *pstAioAttr, HI_BOOL bMacIn)
{
    HI_S32 s32Ret = HI_FAILURE;
    /*** ACODEC_TYPE_TW2865 ***/
    s32Ret = SAMPLE_TW2865_CfgAudio(pstAioAttr->enSamplerate);
    s32Ret |= SAMPLE_TW2865_SetFormat(pstAioAttr->enWorkmode);
    s32Ret |= SAMPLE_TW2865_SetBitwidth(pstAioAttr->enBitwidth);
    
    /*** ACODEC_TYPE_TLV320 ***/ 
    s32Ret = SAMPLE_Tlv320_CfgAudio(pstAioAttr->enWorkmode, pstAioAttr->enSamplerate);

    return s32Ret;
}

/******************************************************************************
* function : get frame from Ai, send it  to Aenc or Ao
******************************************************************************/
void *SAMPLE_COMM_AUDIO_AiProc(void *parg)
{
    HI_S32 s32Ret;
    HI_S32 AiFd;
    SAMPLE_AI_S *pstAiCtl = (SAMPLE_AI_S *)parg;
    AUDIO_FRAME_S stFrame; 
    fd_set read_fds;
    struct timeval TimeoutVal;
    AI_CHN_PARAM_S stAiChnPara;

    s32Ret = HI_MPI_AI_GetChnParam(pstAiCtl->AiDev, pstAiCtl->AiChn, &stAiChnPara);
    if (HI_SUCCESS != s32Ret)
    {
        printf("%s: Get ai chn param failed\n", __FUNCTION__);
        return NULL;
    }
    
    stAiChnPara.u32UsrFrmDepth = 30;
    
    s32Ret = HI_MPI_AI_SetChnParam(pstAiCtl->AiDev, pstAiCtl->AiChn, &stAiChnPara);
    if (HI_SUCCESS != s32Ret)
    {
        printf("%s: set ai chn param failed\n", __FUNCTION__);
        return NULL;
    }
    
    FD_ZERO(&read_fds);
    AiFd = HI_MPI_AI_GetFd(pstAiCtl->AiDev, pstAiCtl->AiChn);
    FD_SET(AiFd,&read_fds);

    while (pstAiCtl->bStart)
    {     
        TimeoutVal.tv_sec = 1;
        TimeoutVal.tv_usec = 0;
        
        FD_ZERO(&read_fds);
        FD_SET(AiFd,&read_fds);
        
        s32Ret = select(AiFd+1, &read_fds, NULL, NULL, &TimeoutVal);
        if (s32Ret < 0) 
        {
            break;
        }
        else if (0 == s32Ret) 
        {
            printf("%s: get ai frame select time out\n", __FUNCTION__);
            break;
        }        
        
        if (FD_ISSET(AiFd, &read_fds))
        {
            /* get frame from ai chn */
            s32Ret = HI_MPI_AI_GetFrame(pstAiCtl->AiDev, pstAiCtl->AiChn,
                &stFrame, NULL, HI_FALSE);
            if (HI_SUCCESS != s32Ret )
            {
                printf("%s: HI_MPI_AI_GetFrame(%d, %d), failed with %#x!\n",\
                       __FUNCTION__, pstAiCtl->AiDev, pstAiCtl->AiChn, s32Ret);
                pstAiCtl->bStart = HI_FALSE;
                return NULL;
            }

            /* send frame to encoder */
            if (HI_TRUE == pstAiCtl->bSendAenc)
            {
                HI_MPI_AENC_SendFrame(pstAiCtl->AencChn, &stFrame, NULL);
            }
            
            /* send frame to ao */
            if (HI_TRUE == pstAiCtl->bSendAo)
            {
                HI_MPI_AO_SendFrame(pstAiCtl->AoDev, pstAiCtl->AoChn,
                    &stFrame, HI_TRUE);
            }

            /* finally you must release the stream */
            HI_MPI_AI_ReleaseFrame(pstAiCtl->AiDev, pstAiCtl->AiChn,
                &stFrame, NULL);
        }
    }
    
    pstAiCtl->bStart = HI_FALSE;
    return NULL;
}

/******************************************************************************
* function : get stream from Aenc, send it  to Adec & save it to file
******************************************************************************/
void *SAMPLE_COMM_AUDIO_AencProc(void *parg)
{
    HI_S32 s32Ret;
    HI_S32 AencFd;
    SAMPLE_AENC_S *pstAencCtl = (SAMPLE_AENC_S *)parg;
    AUDIO_STREAM_S stStream;
    fd_set read_fds;
    struct timeval TimeoutVal;
    
    FD_ZERO(&read_fds);    
    AencFd = HI_MPI_AENC_GetFd(pstAencCtl->AeChn);
    FD_SET(AencFd, &read_fds);
    
    while (pstAencCtl->bStart)
    {     
        TimeoutVal.tv_sec = 1;
        TimeoutVal.tv_usec = 0;
        
        FD_ZERO(&read_fds);
        FD_SET(AencFd,&read_fds);
        
        s32Ret = select(AencFd+1, &read_fds, NULL, NULL, &TimeoutVal);
        if (s32Ret < 0) 
        {
            break;
        }
        else if (0 == s32Ret) 
        {
            printf("%s: get aenc stream select time out\n", __FUNCTION__);
            break;
        }
        
        if (FD_ISSET(AencFd, &read_fds))
        {
            /* get stream from aenc chn */
            s32Ret = HI_MPI_AENC_GetStream(pstAencCtl->AeChn, &stStream, HI_FALSE);
            if (HI_SUCCESS != s32Ret )
            {
                printf("%s: HI_MPI_AENC_GetStream(%d), failed with %#x!\n",\
                       __FUNCTION__, pstAencCtl->AeChn, s32Ret);
                pstAencCtl->bStart = HI_FALSE;
                return NULL;
            }

            /* send stream to decoder and play for testing */
            if (HI_TRUE == pstAencCtl->bSendAdChn)
            {
                HI_MPI_ADEC_SendStream(pstAencCtl->AdChn, &stStream, HI_TRUE);
            }
            
            /* save audio stream to file */
            fwrite(stStream.pStream,1,stStream.u32Len, pstAencCtl->pfd);

            /* finally you must release the stream */
            HI_MPI_AENC_ReleaseStream(pstAencCtl->AeChn, &stStream);
        }    
    }
    
    fclose(pstAencCtl->pfd);
    pstAencCtl->bStart = HI_FALSE;
    return NULL;
}

/******************************************************************************
* function : get stream from file, and send it  to Adec
******************************************************************************/
void *SAMPLE_COMM_AUDIO_AdecProc(void *parg)
{
    HI_S32 s32Ret;
    AUDIO_STREAM_S stAudioStream;    
    HI_U32 u32Len = 640;
    HI_U32 u32ReadLen;
    HI_S32 s32AdecChn;
    HI_U8 *pu8AudioStream = NULL;
    SAMPLE_ADEC_S *pstAdecCtl = (SAMPLE_ADEC_S *)parg;    
    FILE *pfd = pstAdecCtl->pfd;
    s32AdecChn = pstAdecCtl->AdChn;
    
    pu8AudioStream = (HI_U8*)malloc(sizeof(HI_U8)*MAX_AUDIO_STREAM_LEN);
    if (NULL == pu8AudioStream)
    {
        printf("%s: malloc failed!\n", __FUNCTION__);
        return NULL;
    }

    while (HI_TRUE == pstAdecCtl->bStart)
    {
        /* read from file */
        stAudioStream.pStream = pu8AudioStream;
        u32ReadLen = fread(stAudioStream.pStream, 1, u32Len, pfd);
        if (u32ReadLen <= 0)
        {            
            fseek(pfd, 0, SEEK_SET);/*read file again*/
            continue;
        }

        /* here only demo adec streaming sending mode, but pack sending mode is commended */
        stAudioStream.u32Len = u32ReadLen;
        s32Ret = HI_MPI_ADEC_SendStream(s32AdecChn, &stAudioStream, HI_TRUE);
        if (s32Ret)
        {
            printf("%s: HI_MPI_ADEC_SendStream(%d) failed with %#x!\n",\
                   __FUNCTION__, s32AdecChn, s32Ret);
            break;
        }
    }
    
    free(pu8AudioStream);
    pu8AudioStream = NULL;
    fclose(pfd);
    pstAdecCtl->bStart = HI_FALSE;
    return NULL;
}

/******************************************************************************
* function : Create the thread to get frame from ai and send to ao
******************************************************************************/
HI_S32 SAMPLE_COMM_AUDIO_CreatTrdAiAo(AUDIO_DEV AiDev, AI_CHN AiChn, AUDIO_DEV AoDev, AO_CHN AoChn)
{
    SAMPLE_AI_S *pstAi = NULL;
    
    pstAi = &gs_stSampleAi[AiDev*AIO_MAX_CHN_NUM + AiChn];
    pstAi->bSendAenc = HI_FALSE;
    pstAi->bSendAo = HI_TRUE;
    pstAi->bStart= HI_TRUE;
    pstAi->AiDev = AiDev;
    pstAi->AiChn = AiChn;
    pstAi->AoDev = AoDev;
    pstAi->AoChn = AoChn;

    pthread_create(&pstAi->stAiPid, 0, SAMPLE_COMM_AUDIO_AiProc, pstAi);
    
    return HI_SUCCESS;
}

/******************************************************************************
* function : Create the thread to get frame from ai and send to aenc
******************************************************************************/
HI_S32 SAMPLE_COMM_AUDIO_CreatTrdAiAenc(AUDIO_DEV AiDev, AI_CHN AiChn, AENC_CHN AeChn)
{
    SAMPLE_AI_S *pstAi = NULL;
    
    pstAi = &gs_stSampleAi[AiDev*AIO_MAX_CHN_NUM + AiChn];
    pstAi->bSendAenc = HI_TRUE;
    pstAi->bSendAo = HI_FALSE;
    pstAi->bStart= HI_TRUE;
    pstAi->AiDev = AiDev;
    pstAi->AiChn = AiChn;
    pstAi->AencChn = AeChn;
    pthread_create(&pstAi->stAiPid, 0, SAMPLE_COMM_AUDIO_AiProc, pstAi);
    
    return HI_SUCCESS;
}

/******************************************************************************
* function : Create the thread to get stream from aenc and send to adec
******************************************************************************/
HI_S32 SAMPLE_COMM_AUDIO_CreatTrdAencAdec(AENC_CHN AeChn, ADEC_CHN AdChn, FILE *pAecFd)
{
    SAMPLE_AENC_S *pstAenc = NULL;

    if (NULL == pAecFd)
    {
        return HI_FAILURE;
    }
    
    pstAenc = &gs_stSampleAenc[AeChn];
    pstAenc->AeChn = AeChn;
    pstAenc->AdChn = AdChn;
    pstAenc->bSendAdChn = HI_TRUE;
    pstAenc->pfd = pAecFd;    
    pstAenc->bStart = HI_TRUE;    
    pthread_create(&pstAenc->stAencPid, 0, SAMPLE_COMM_AUDIO_AencProc, pstAenc);
    
    return HI_SUCCESS;
}

/******************************************************************************
* function : Create the thread to get stream from file and send to adec
******************************************************************************/
HI_S32 SAMPLE_COMM_AUDIO_CreatTrdFileAdec(ADEC_CHN AdChn, FILE *pAdcFd)
{
    SAMPLE_ADEC_S *pstAdec = NULL;

    if (NULL == pAdcFd)
    {
        return HI_FAILURE;
    }

    pstAdec = &gs_stSampleAdec[AdChn];
    pstAdec->AdChn = AdChn;
    pstAdec->pfd = pAdcFd;
    pstAdec->bStart = HI_TRUE;
    pthread_create(&pstAdec->stAdPid, 0, SAMPLE_COMM_AUDIO_AdecProc, pstAdec);
    
    return HI_SUCCESS;
}

/******************************************************************************
* function : Destory the thread to get frame from ai and send to ao or aenc
******************************************************************************/
HI_S32 SAMPLE_COMM_AUDIO_DestoryTrdAi(AUDIO_DEV AiDev, AI_CHN AiChn)
{
    SAMPLE_AI_S *pstAi = NULL;
    
    pstAi = &gs_stSampleAi[AiDev*AIO_MAX_CHN_NUM + AiChn];
    pstAi->bStart= HI_FALSE;
    pthread_join(pstAi->stAiPid, 0);
    
    return HI_SUCCESS;
}

/******************************************************************************
* function : Destory the thread to get stream from aenc and send to adec
******************************************************************************/
HI_S32 SAMPLE_COMM_AUDIO_DestoryTrdAencAdec(AENC_CHN AeChn)
{
    SAMPLE_AENC_S *pstAenc = NULL;

    pstAenc = &gs_stSampleAenc[AeChn];
    pstAenc->bStart = HI_FALSE;
    pthread_join(pstAenc->stAencPid, 0);
    
    return HI_SUCCESS;
}

/******************************************************************************
* function : Destory the thread to get stream from file and send to adec
******************************************************************************/
HI_S32 SAMPLE_COMM_AUDIO_DestoryTrdFileAdec(ADEC_CHN AdChn)
{
    SAMPLE_ADEC_S *pstAdec = NULL;

    pstAdec = &gs_stSampleAdec[AdChn];
    pstAdec->bStart = HI_FALSE;
    pthread_join(pstAdec->stAdPid, 0);
    
    return HI_SUCCESS;
}

/******************************************************************************
* function : Ao bind Adec
******************************************************************************/
HI_S32 SAMPLE_COMM_AUDIO_AoBindAdec(AUDIO_DEV AoDev, AO_CHN AoChn, ADEC_CHN AdChn)
{
    MPP_CHN_S stSrcChn,stDestChn;

    stSrcChn.enModId = HI_ID_ADEC;
    stSrcChn.s32DevId = 0;
    stSrcChn.s32ChnId = AdChn;
    stDestChn.enModId = HI_ID_AO;
    stDestChn.s32DevId = AoDev;
    stDestChn.s32ChnId = AoChn;
    
    return HI_MPI_SYS_Bind(&stSrcChn, &stDestChn); 
}

/******************************************************************************
* function : Ao unbind Adec
******************************************************************************/
HI_S32 SAMPLE_COMM_AUDIO_AoUnbindAdec(AUDIO_DEV AoDev, AO_CHN AoChn, ADEC_CHN AdChn)
{
    MPP_CHN_S stSrcChn,stDestChn;

    stSrcChn.enModId = HI_ID_ADEC;
    stSrcChn.s32ChnId = AdChn;
    stSrcChn.s32DevId = 0;
    stDestChn.enModId = HI_ID_AO;
    stDestChn.s32DevId = AoDev;
    stDestChn.s32ChnId = AoChn;
    
    return HI_MPI_SYS_UnBind(&stSrcChn, &stDestChn); 
}

/******************************************************************************
* function : Ao bind Ai
******************************************************************************/
HI_S32 SAMPLE_COMM_AUDIO_AoBindAi(AUDIO_DEV AiDev, AI_CHN AiChn, AUDIO_DEV AoDev, AO_CHN AoChn)
{
    MPP_CHN_S stSrcChn,stDestChn;

    stSrcChn.enModId = HI_ID_AI;
    stSrcChn.s32ChnId = AiChn;
    stSrcChn.s32DevId = AiDev;
    stDestChn.enModId = HI_ID_AO;
    stDestChn.s32DevId = AoDev;
    stDestChn.s32ChnId = AoChn;
    
    return HI_MPI_SYS_Bind(&stSrcChn, &stDestChn); 
}

/******************************************************************************
* function : Ao unbind Ai
******************************************************************************/
HI_S32 SAMPLE_COMM_AUDIO_AoUnbindAi(AUDIO_DEV AiDev, AI_CHN AiChn, AUDIO_DEV AoDev, AO_CHN AoChn)
{
    MPP_CHN_S stSrcChn,stDestChn;

    stSrcChn.enModId = HI_ID_AI;
    stSrcChn.s32ChnId = AiChn;
    stSrcChn.s32DevId = AiDev;
    stDestChn.enModId = HI_ID_AO;
    stDestChn.s32DevId = AoDev;
    stDestChn.s32ChnId = AoChn;
    
    return HI_MPI_SYS_UnBind(&stSrcChn, &stDestChn); 
}

/******************************************************************************
* function : Aenc bind Ai
******************************************************************************/
HI_S32 SAMPLE_COMM_AUDIO_AencBindAi(AUDIO_DEV AiDev, AI_CHN AiChn, AENC_CHN AeChn)
{
    MPP_CHN_S stSrcChn,stDestChn;

    stSrcChn.enModId = HI_ID_AI;
    stSrcChn.s32DevId = AiDev;
    stSrcChn.s32ChnId = AiChn;
    stDestChn.enModId = HI_ID_AENC;
    stDestChn.s32DevId = 0;
    stDestChn.s32ChnId = AeChn;
    
    return HI_MPI_SYS_Bind(&stSrcChn, &stDestChn);
}

/******************************************************************************
* function : Aenc unbind Ai
******************************************************************************/
HI_S32 SAMPLE_COMM_AUDIO_AencUnbindAi(AUDIO_DEV AiDev, AI_CHN AiChn, AENC_CHN AeChn)
{
    MPP_CHN_S stSrcChn,stDestChn;

    stSrcChn.enModId = HI_ID_AI;
    stSrcChn.s32DevId = AiDev;
    stSrcChn.s32ChnId = AiChn;
    stDestChn.enModId = HI_ID_AENC;
    stDestChn.s32DevId = 0;
    stDestChn.s32ChnId = AeChn;
    
    return HI_MPI_SYS_UnBind(&stSrcChn, &stDestChn);      
}

#if 0
/******************************************************************************
* function : Acodec config [ s32Samplerate(0:8k, 1:16k ) ]
******************************************************************************/
HI_S32 SAMPLE_COMM_AUDIO_CfgAcodec(AUDIO_SAMPLE_RATE_E enSample, HI_BOOL bMicIn)
{
    HI_S32 fdAcodec = -1;
    ACODEC_CTRL stAudioctrl = {0};

    fdAcodec = open(ACODEC_FILE,O_RDWR);
    if (fdAcodec < 0)
    {
        printf("%s: can't open acodec,%s\n", __FUNCTION__, ACODEC_FILE);
        return HI_FAILURE;     
    }

    if ((AUDIO_SAMPLE_RATE_8000 == enSample)
        || (AUDIO_SAMPLE_RATE_11025 == enSample)
        || (AUDIO_SAMPLE_RATE_12000 == enSample))
    {
        stAudioctrl.i2s_fs_sel = 0x18;
    }
    else if ((AUDIO_SAMPLE_RATE_16000 == enSample)
        || (AUDIO_SAMPLE_RATE_22050 == enSample)
        || (AUDIO_SAMPLE_RATE_24000 == enSample))
    {
        stAudioctrl.i2s_fs_sel = 0x19;
    }
    else if ((AUDIO_SAMPLE_RATE_32000 == enSample)
        || (AUDIO_SAMPLE_RATE_44100 == enSample)
        || (AUDIO_SAMPLE_RATE_48000 == enSample))
    {
        stAudioctrl.i2s_fs_sel = 0x1a;
    }
    else 
    {
        printf("%s: not support enSample:%d\n", __FUNCTION__, enSample);
        return HI_FAILURE;
    }
    
    if (ioctl(fdAcodec, ACODEC_SET_I2S1_FS, &stAudioctrl))
    {
        printf("%s: set acodec sample rate failed\n", __FUNCTION__);
        return HI_FAILURE;
    }

    if (HI_TRUE == bMicIn)
    {
        stAudioctrl.mixer_mic_ctrl = ACODEC_MIXER_MICIN;
        if (ioctl(fdAcodec, ACODEC_SET_MIXER_MIC, &stAudioctrl))
        {
            printf("%s: set acodec micin failed\n", __FUNCTION__);
            return HI_FAILURE;
        }

        /* set volume plus (0~0x1f,default 0) */
        stAudioctrl.gain_mic = 0;
        if (ioctl(fdAcodec, ACODEC_SET_GAIN_MICL, &stAudioctrl))
        {
            printf("%s: set acodec micin volume failed\n", __FUNCTION__);
            return HI_FAILURE;
        }
        if (ioctl(fdAcodec, ACODEC_SET_GAIN_MICR, &stAudioctrl))
        {
            printf("%s: set acodec micin volume failed\n", __FUNCTION__);
            return HI_FAILURE;
        }
    }
    close(fdAcodec);    

    return HI_SUCCESS;
}

/******************************************************************************
* function : Disable Tlv320
******************************************************************************/
HI_S32 SAMPLE_COMM_AUDIO_DisableAcodec()
{
    return  SAMPLE_COMM_AUDIO_CfgAcodec(AUDIO_SAMPLE_RATE_48000, HI_FALSE);
}

#endif

/******************************************************************************
* function : Start Ai
******************************************************************************/
HI_S32 SAMPLE_COMM_AUDIO_StartAi(AUDIO_DEV AiDevId, HI_S32 s32AiChnCnt,
        AIO_ATTR_S *pstAioAttr, HI_BOOL bAnrEn, AUDIO_RESAMPLE_ATTR_S *pstAiReSmpAttr)
{
    HI_S32 i, s32Ret;
    
    s32Ret = HI_MPI_AI_SetPubAttr(AiDevId, pstAioAttr);
    if (s32Ret)
    {
        printf("%s: HI_MPI_AI_SetPubAttr(%d) failed with %#x\n", __FUNCTION__, AiDevId, s32Ret);
        return HI_FAILURE;
    }
    if (HI_MPI_AI_Enable(AiDevId))
    {
        printf("%s: HI_MPI_AI_Enable(%d) failed with %#x\n", __FUNCTION__, AiDevId, s32Ret);
        return HI_FAILURE;
    }                
    for (i=0; i<s32AiChnCnt; i++)
    {
        if (HI_MPI_AI_EnableChn(AiDevId, i))
        {
            printf("%s: HI_MPI_AI_EnableChn(%d,%d) failed with %#x\n", __FUNCTION__,\
                    AiDevId, i, s32Ret);
            return HI_FAILURE;
        }

        if (HI_TRUE == bAnrEn)
        {
            if (HI_MPI_AI_EnableAnr(AiDevId, i))
            {
                printf("%s: HI_MPI_AI_EnableAnr(%d,%d) failed with %#x\n", __FUNCTION__,\
                    AiDevId, i, s32Ret);
                return HI_FAILURE;
            }
        }

        if (NULL != pstAiReSmpAttr)
        {
            if (HI_MPI_AI_EnableReSmp(AiDevId, i, pstAiReSmpAttr))
            {
                printf("%s: HI_MPI_AI_EnableReSmp(%d,%d) failed with %#x\n",\
                        __FUNCTION__, AiDevId, i, s32Ret);
                return HI_FAILURE;
            }
        }
    }
    
    return HI_SUCCESS;
}

/******************************************************************************
* function : Stop Ai
******************************************************************************/
HI_S32 SAMPLE_COMM_AUDIO_StopAi(AUDIO_DEV AiDevId, HI_S32 s32AiChnCnt,
        HI_BOOL bAnrEn, HI_BOOL bResampleEn)
{
    HI_S32 i;    
    for (i=0; i<s32AiChnCnt; i++)
    {
        if (HI_TRUE == bResampleEn)
        {
            HI_MPI_AI_DisableReSmp(AiDevId, i);
        }
        if (HI_TRUE == bAnrEn)
        {
            HI_MPI_AI_DisableAnr(AiDevId, i);
        }
        HI_MPI_AI_DisableChn(AiDevId, i);
    }  
    HI_MPI_AI_Disable(AiDevId);
    return HI_SUCCESS;
}

HI_S32 SAMPLE_COMM_AUDIO_StartHdmi(AIO_ATTR_S *pstAioAttr)
{
    HI_S32 s32Ret;
    HI_HDMI_ATTR_S stHdmiAttr;
    HI_HDMI_ID_E enHdmi = HI_HDMI_ID_0;
    VO_PUB_ATTR_S stPubAttr;
    VO_DEV VoDev = 0;
    
    stPubAttr.u32BgColor = 0x000000ff;
    stPubAttr.enIntfType = VO_INTF_HDMI;
    stPubAttr.bDoubleFrame = HI_FALSE;
    stPubAttr.enIntfSync = VO_OUTPUT_1080P30;

    if(HI_SUCCESS != SAMPLE_COMM_VO_StartDevLayer(VoDev, &stPubAttr, 25))
    {
        printf("[Func]:%s [Line]:%d [Info]:%s\n", __FUNCTION__, __LINE__, "failed");
        return HI_FAILURE;
    }

    s32Ret = SAMPLE_COMM_VO_HdmiStart(stPubAttr.enIntfSync);
    if(HI_SUCCESS != s32Ret)
    {
        printf("[Func]:%s [Line]:%d [Info]:%s\n", __FUNCTION__, __LINE__, "failed");
        return HI_FAILURE;
    }

    HI_MPI_HDMI_SetAVMute(enHdmi, HI_TRUE);
    
    s32Ret = HI_MPI_HDMI_GetAttr(enHdmi, &stHdmiAttr);
    if(HI_SUCCESS != s32Ret)
    {
        printf("[Func]:%s [Line]:%d [Info]:%s\n", __FUNCTION__, __LINE__, "failed");
        return HI_FAILURE;
    }
    stHdmiAttr.bEnableAudio = HI_TRUE;        /**< �Ƿ�Enable��Ƶ */
    stHdmiAttr.enSoundIntf = HI_HDMI_SND_INTERFACE_I2S; /**< HDMI��Ƶ��Դ, ����HI_HDMI_SND_INTERFACE_I2S,�˲�����Ҫ��AO���뱣��һ�� */
    stHdmiAttr.enSampleRate = pstAioAttr->enSamplerate;        /**< PCM��Ƶ������,�˲�����Ҫ��AO�����ñ���һ�� */
    stHdmiAttr.u8DownSampleParm = HI_FALSE;    /**< PCM��Ƶ����downsample�����ʵĲ�����Ĭ��Ϊ0 */
    
    stHdmiAttr.enBitDepth = 8 * (pstAioAttr->enBitwidth+1);   /**< ��Ƶλ��Ĭ��Ϊ16,�˲�����Ҫ��AO�����ñ���һ�� */
    stHdmiAttr.u8I2SCtlVbit = 0;        /**< ������������Ϊ0, I2S control (0x7A:0x1D) */
    
    stHdmiAttr.bEnableAviInfoFrame = HI_TRUE; /**< �Ƿ�ʹ�� AVI InfoFrame������ʹ�� */
    stHdmiAttr.bEnableAudInfoFrame = HI_TRUE;; /**< �Ƿ�ʹ�� AUDIO InfoFrame������ʹ�� */

    s32Ret = HI_MPI_HDMI_SetAttr(enHdmi, &stHdmiAttr);
    if(HI_SUCCESS != s32Ret)
    {
        printf("[Func]:%s [Line]:%d [Info]:%s\n", __FUNCTION__, __LINE__, "failed");
        return HI_FAILURE;
    }
    HI_MPI_HDMI_SetAVMute(enHdmi, HI_FALSE);
    
    return HI_SUCCESS;
}

inline HI_S32 SAMPLE_COMM_AUDIO_StopHdmi(HI_VOID)
{
    HI_S32 s32Ret;
    VO_DEV VoDev = 0;
    
    s32Ret =  SAMPLE_COMM_VO_HdmiStop();
    s32Ret |= SAMPLE_COMM_VO_StopDevLayer(VoDev);
    return s32Ret;
}

/******************************************************************************
* function : Start Ao
******************************************************************************/
HI_S32 SAMPLE_COMM_AUDIO_StartAo(AUDIO_DEV AoDevId, AO_CHN AoChn,
        AIO_ATTR_S *pstAioAttr, AUDIO_RESAMPLE_ATTR_S *pstAoReSmpAttr)
{
    HI_S32 s32Ret;


    if (SAMPLE_AUDIO_HDMI_AO_DEV == AoDevId)
    {
        SAMPLE_COMM_AUDIO_StartHdmi(pstAioAttr);
    }

    s32Ret = HI_MPI_AO_SetPubAttr(AoDevId, pstAioAttr);
    if(HI_SUCCESS != s32Ret)
    {
        printf("%s: HI_MPI_AO_SetPubAttr(%d) failed with %#x!\n", __FUNCTION__, \
               AoDevId,s32Ret);
        return HI_FAILURE;
    }
    s32Ret = HI_MPI_AO_Enable(AoDevId);
    if(HI_SUCCESS != s32Ret)
    {
        printf("%s: HI_MPI_AO_Enable(%d) failed with %#x!\n", __FUNCTION__, \
               AoDevId, s32Ret);
        return HI_FAILURE;
    }
    s32Ret = HI_MPI_AO_EnableChn(AoDevId, AoChn);
    if(HI_SUCCESS != s32Ret)
    {
        printf("%s: HI_MPI_AO_EnableChn(%d) failed with %#x!\n", __FUNCTION__,\
               AoChn, s32Ret);
        return HI_FAILURE;
    }
    
    if (NULL != pstAoReSmpAttr)
    {
        s32Ret = HI_MPI_AO_DisableReSmp(AoDevId, AoChn);
        s32Ret |= HI_MPI_AO_EnableReSmp(AoDevId, AoChn, pstAoReSmpAttr);
        if(HI_SUCCESS != s32Ret)
        {
            printf("%s: HI_MPI_AO_EnableReSmp(%d,%d) failed with %#x!\n", \
                   __FUNCTION__, AoDevId, AoChn, s32Ret);
            return HI_FAILURE;
        }
    }

    return HI_SUCCESS;
}

/******************************************************************************
* function : Stop Ao
******************************************************************************/
HI_S32 SAMPLE_COMM_AUDIO_StopAo(AUDIO_DEV AoDevId, AO_CHN AoChn, HI_BOOL bResampleEn)
{
    if (HI_TRUE == bResampleEn)
    {
        HI_MPI_AO_DisableReSmp(AoDevId, AoChn);
    }
    HI_MPI_AO_DisableChn(AoDevId, AoChn);
    HI_MPI_AO_Disable(AoDevId);
    
#if (HICHIP == HI3531_V100 || HICHIP == HI3521_V100)
    if (SAMPLE_AUDIO_HDMI_AO_DEV == AoDevId)
    {
        SAMPLE_COMM_AUDIO_StopHdmi();
    }
#endif

    return HI_SUCCESS;
}

/******************************************************************************
* function : Start Aenc
******************************************************************************/
HI_S32 SAMPLE_COMM_AUDIO_StartAenc(HI_S32 s32AencChnCnt, PAYLOAD_TYPE_E enType)
{
    AENC_CHN AeChn;
    HI_S32 s32Ret, i;
    AENC_CHN_ATTR_S stAencAttr;
    
    /* set AENC chn attr */
    
    stAencAttr.enType = enType;
    stAencAttr.u32BufSize = 30;
    
    if (PT_ADPCMA == stAencAttr.enType)
    {
        AENC_ATTR_ADPCM_S stAdpcmAenc;
        stAencAttr.pValue       = &stAdpcmAenc;
        stAdpcmAenc.enADPCMType = AUDIO_ADPCM_TYPE;
    }
    else if (PT_G711A == stAencAttr.enType || PT_G711U == stAencAttr.enType)
    {
        AENC_ATTR_G711_S stAencG711;
        stAencAttr.pValue       = &stAencG711;
    }
    else if (PT_G726 == stAencAttr.enType)
    {
        AENC_ATTR_G726_S stAencG726;
        stAencAttr.pValue       = &stAencG726;
        stAencG726.enG726bps    = G726_BPS;
    }
    else if (PT_LPCM == stAencAttr.enType)
    {
        AENC_ATTR_LPCM_S stAencLpcm;
        stAencAttr.pValue = &stAencLpcm;
    }
    else
    {
        printf("%s: invalid aenc payload type:%d\n", __FUNCTION__, stAencAttr.enType);
        return HI_FAILURE;
    }    

    for (i=0; i<s32AencChnCnt; i++)
    {            
        AeChn = i;
        
        /* create aenc chn*/
        s32Ret = HI_MPI_AENC_CreateChn(AeChn, &stAencAttr);
        if (s32Ret != HI_SUCCESS)
        {
            printf("%s: HI_MPI_AENC_CreateChn(%d) failed with %#x!\n", __FUNCTION__,
                   AeChn, s32Ret);
            return HI_FAILURE;
        }        
    }
    
    return HI_SUCCESS;
}

/******************************************************************************
* function : Stop Aenc
******************************************************************************/
HI_S32 SAMPLE_COMM_AUDIO_StopAenc(HI_S32 s32AencChnCnt)
{
    HI_S32 i;
    for (i=0; i<s32AencChnCnt; i++)
    {
        HI_MPI_AENC_DestroyChn(i);
    }
    return HI_SUCCESS;
}

/******************************************************************************
* function : Start Adec
******************************************************************************/
HI_S32 SAMPLE_COMM_AUDIO_StartAdec(ADEC_CHN AdChn, PAYLOAD_TYPE_E enType)
{
    HI_S32 s32Ret;
    ADEC_CHN_ATTR_S stAdecAttr;
        
    stAdecAttr.enType = enType;
    stAdecAttr.u32BufSize = 20;
    stAdecAttr.enMode = ADEC_MODE_STREAM;/* propose use pack mode in your app */
        
    if (PT_ADPCMA == stAdecAttr.enType)
    {
        ADEC_ATTR_ADPCM_S stAdpcm;
        stAdecAttr.pValue = &stAdpcm;
        stAdpcm.enADPCMType = AUDIO_ADPCM_TYPE ;
    }
    else if (PT_G711A == stAdecAttr.enType || PT_G711U == stAdecAttr.enType)
    {
        ADEC_ATTR_G711_S stAdecG711;
        stAdecAttr.pValue = &stAdecG711;
    }
    else if (PT_G726 == stAdecAttr.enType)
    {
        ADEC_ATTR_G726_S stAdecG726;
        stAdecAttr.pValue = &stAdecG726;
        stAdecG726.enG726bps = G726_BPS ;      
    }
    else if (PT_LPCM == stAdecAttr.enType)
    {
        ADEC_ATTR_LPCM_S stAdecLpcm;
        stAdecAttr.pValue = &stAdecLpcm;
        stAdecAttr.enMode = ADEC_MODE_PACK;/* lpcm must use pack mode */
    }
    else
    {
        printf("%s: invalid aenc payload type:%d\n", __FUNCTION__, stAdecAttr.enType);
        return HI_FAILURE;
    }     
    
    /* create adec chn*/
    s32Ret = HI_MPI_ADEC_CreateChn(AdChn, &stAdecAttr);
    if (s32Ret)
    {
        printf("%s: HI_MPI_ADEC_CreateChn(%d) failed with %#x!\n", __FUNCTION__,\
               AdChn,s32Ret);
        return s32Ret;
    }
    return 0;
}

/******************************************************************************
* function : Stop Adec
******************************************************************************/
HI_S32 SAMPLE_COMM_AUDIO_StopAdec(ADEC_CHN AdChn)
{
    HI_MPI_ADEC_DestroyChn(AdChn);
    return HI_SUCCESS;
}

#ifdef __cplusplus
#if __cplusplus
}
#endif 
#endif /* End of #ifdef __cplusplus */

