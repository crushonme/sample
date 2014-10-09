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
//#include "acodec.h"
#include "tw2865.h"
#include "tlv320aic31.h"


#ifdef __cplusplus
#if __cplusplus
extern "C"{
#endif
#endif /* End of #ifdef __cplusplus */

#define SAMPLE_AUDIO_PTNUMPERFRM   320

#define SAMPLE_AUDIO_AI_DEV 2

#define SAMPLE_AUDIO_AO_DEV 2

#define SAMPLE_AUDIO_CHN_NUM 2


static PAYLOAD_TYPE_E gs_enPayloadType = PT_ADPCMA;

static HI_BOOL gs_bMicIn = HI_FALSE;

static HI_BOOL gs_bAiAnr = HI_FALSE;
static HI_BOOL gs_bAioReSample = HI_FALSE;
static HI_BOOL gs_bUserGetMode = HI_TRUE;
static AUDIO_RESAMPLE_ATTR_S *gs_pstAiReSmpAttr = NULL;
static AUDIO_RESAMPLE_ATTR_S *gs_pstAoReSmpAttr = NULL;

#define SAMPLE_DBG(s32Ret)\
do{\
    printf("s32Ret=%#x,fuc:%s,line:%d\n", s32Ret, __FUNCTION__, __LINE__);\
}while(0)

/******************************************************************************
* function : PT Number to String
******************************************************************************/
static char* SAMPLE_AUDIO_Pt2Str(PAYLOAD_TYPE_E enType)
{
    if (PT_G711A == enType)  return "g711a";
    else if (PT_G711U == enType)  return "g711u";
    else if (PT_ADPCMA == enType)  return "adpcm";
    else if (PT_G726 == enType)  return "g726";
    else if (PT_LPCM == enType)  return "pcm";
    else return "data";
}


/******************************************************************************
* function : Ai -> Ao
******************************************************************************/
HI_S32 TEST_AUDIO_AiAo(AIO_ATTR_S *pstAioAttr,AI_CHN aich, AO_CHN aoch)
{
    HI_S32 s32Ret, s32AiChnCnt;
    AUDIO_DEV   AiDev = SAMPLE_AUDIO_AI_DEV;
    AI_CHN      AiChn = aich;
    AUDIO_DEV   AoDev = SAMPLE_AUDIO_AO_DEV;
    AO_CHN      AoChn = aoch;


	//printf("------TEST_AUDIO_AiAo-----\n");
    /* config aio dev attr */
    if (NULL == pstAioAttr)
    {
        printf("[Func]:%s [Line]:%d [Info]:%s\n", __FUNCTION__, __LINE__, "NULL pointer");
        return HI_FAILURE;
    }

	//printf("------TEST_AUDIO_AiAo--1---\n");
    /* config audio codec */
    s32Ret = SAMPLE_COMM_AUDIO_CfgAcodec(pstAioAttr, gs_bMicIn);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_DBG(s32Ret);
        return HI_FAILURE;
    }

	//printf("------TEST_AUDIO_AiAo--2---\n");
    /* enable AI channle */    
    s32AiChnCnt = pstAioAttr->u32ChnCnt;
    s32Ret = SAMPLE_COMM_AUDIO_StartAi(AiDev, s32AiChnCnt, pstAioAttr, gs_bAiAnr, gs_pstAiReSmpAttr);
    if (s32Ret != HI_SUCCESS)
    {
        SAMPLE_DBG(s32Ret);
        return HI_FAILURE;
    }

	//printf("------TEST_AUDIO_AiAo--3---\n");
    /* enable AO channle */
    pstAioAttr->u32ChnCnt = SAMPLE_AUDIO_CHN_NUM;//2->4
    s32Ret = SAMPLE_COMM_AUDIO_StartAo(AoDev, AoChn, pstAioAttr, gs_pstAoReSmpAttr);
    if (s32Ret != HI_SUCCESS)
    {
        SAMPLE_DBG(s32Ret);
        return HI_FAILURE;
    }

    /* bind AI to AO channle */
    if (HI_TRUE == gs_bUserGetMode)
    {
        s32Ret = SAMPLE_COMM_AUDIO_CreatTrdAiAo(AiDev, AiChn, AoDev, AoChn);
        if (s32Ret != HI_SUCCESS)
        {
            SAMPLE_DBG(s32Ret);
            return HI_FAILURE;
        }
    }
    else
    {   
        s32Ret = SAMPLE_COMM_AUDIO_AoBindAi(AiDev, AiChn, AoDev, AoChn);
        if (s32Ret != HI_SUCCESS)
        {
            SAMPLE_DBG(s32Ret);
            return HI_FAILURE;
        }
    }

    printf("ai(%d,%d) bind to ao(%d,%d) ok\n", AiDev, AiChn, AoDev, AoChn);

    printf("\nplease press twice ENTER to exit this sample\n");
    getchar();
    getchar();

    if (HI_TRUE == gs_bUserGetMode)
    {
        SAMPLE_COMM_AUDIO_DestoryTrdAi(AiDev, AiChn);
    }
    else
    {
        SAMPLE_COMM_AUDIO_AoUnbindAi(AiDev, AiChn, AoDev, AoChn);
    }
    SAMPLE_COMM_AUDIO_StopAi(AiDev, s32AiChnCnt, gs_bAiAnr, gs_bAioReSample);
    SAMPLE_COMM_AUDIO_StopAo(AoDev, AoChn, gs_bAioReSample);
    return HI_SUCCESS;
}


HI_VOID MYSAMPLE_AUDIO_Usage(void)
{
    printf("\n/************************************/\n");
    printf("press sample command as follows!\n");
    printf("1:  start AI ch0 to AO chn 0 loop\n");
	printf("2:  start AI ch1 to AO chn 1 loop\n");
	//printf("3:  start AI ch2 to AO chn 2 loop\n");
	//printf("4:  start AI ch3 to AO chn 3 loop\n");
    printf("q:  quit whole audio sample\n\n");
    printf("sample command:");
}

/******************************************************************************
* function : to process abnormal case
******************************************************************************/
void SAMPLE_AUDIO_HandleSig(HI_S32 signo)
{
    if (SIGINT == signo || SIGTSTP == signo)
    {
        SAMPLE_COMM_SYS_Exit();
        printf("\033[0;31mprogram exit abnormally!\033[0;39m\n");
    }

    exit(0);
}

/******************************************************************************
* function : main
******************************************************************************/
HI_S32 main(int argc, char *argv[])
{
    char ch;
    HI_S32 s32Ret= HI_SUCCESS;
    VB_CONF_S stVbConf;
    AIO_ATTR_S stAioAttr;
    AUDIO_RESAMPLE_ATTR_S stAiReSampleAttr;
    AUDIO_RESAMPLE_ATTR_S stAoReSampleAttr;
    AIO_ATTR_S stHdmiAoAttr;
        
    /* arg 1 is audio payload type */
    if (argc >= 2)
    {
        gs_enPayloadType = atoi(argv[1]);

        if (gs_enPayloadType != PT_G711A && gs_enPayloadType != PT_G711U &&\
            gs_enPayloadType != PT_ADPCMA && gs_enPayloadType != PT_G726 &&\
            gs_enPayloadType != PT_LPCM)
        {
            printf("payload type invalid!\n");
            printf("\nargv[1]:%d is payload type ID, suport such type:\n", gs_enPayloadType);
            printf("%d:g711a, %d:g711u, %d:adpcm, %d:g726, %d:lpcm\n",
            PT_G711A, PT_G711U, PT_ADPCMA, PT_G726, PT_LPCM);
            return HI_FAILURE;
        }
    }

    /* init stAio. all of cases will use it */
    stAioAttr.enSamplerate = AUDIO_SAMPLE_RATE_8000;
    stAioAttr.enBitwidth = AUDIO_BIT_WIDTH_16;
    stAioAttr.enWorkmode = AIO_MODE_I2S_SLAVE;
    stAioAttr.enSoundmode = AUDIO_SOUND_MODE_MONO;
    stAioAttr.u32EXFlag = 1;
    stAioAttr.u32FrmNum = 30;
    stAioAttr.u32PtNumPerFrm = SAMPLE_AUDIO_PTNUMPERFRM;
    stAioAttr.u32ChnCnt = 2;
    stAioAttr.u32ClkSel = 1;

    /* config ao resample attr if needed */
    if (HI_TRUE == gs_bAioReSample)
    {
        stAioAttr.enSamplerate = AUDIO_SAMPLE_RATE_32000;
        stAioAttr.u32PtNumPerFrm = SAMPLE_AUDIO_PTNUMPERFRM * 4;

        /* ai 32k -> 8k */
        stAiReSampleAttr.u32InPointNum = SAMPLE_AUDIO_PTNUMPERFRM * 4;
        stAiReSampleAttr.enInSampleRate = AUDIO_SAMPLE_RATE_32000;
        stAiReSampleAttr.enReSampleType = AUDIO_RESAMPLE_4X1;
        gs_pstAiReSmpAttr = &stAiReSampleAttr;

        /* ao 8k -> 32k */
        stAoReSampleAttr.u32InPointNum = SAMPLE_AUDIO_PTNUMPERFRM;
        stAoReSampleAttr.enInSampleRate = AUDIO_SAMPLE_RATE_8000;
        stAoReSampleAttr.enReSampleType = AUDIO_RESAMPLE_1X4;
        gs_pstAoReSmpAttr = &stAoReSampleAttr;
    }
    else
    {
        gs_pstAiReSmpAttr = NULL;
        gs_pstAoReSmpAttr = NULL;
    }

    /* resample and anr should be user get mode */
    //gs_bUserGetMode = (HI_TRUE == gs_bAioReSample || HI_TRUE == gs_bAiAnr) ? HI_TRUE : HI_FALSE;    
	gs_bUserGetMode = HI_TRUE;

	
    signal(SIGINT, SAMPLE_AUDIO_HandleSig);
    signal(SIGTERM, SAMPLE_AUDIO_HandleSig);
    
    memset(&stVbConf, 0, sizeof(VB_CONF_S));
    s32Ret = SAMPLE_COMM_SYS_Init(&stVbConf);
    if (HI_SUCCESS != s32Ret)
    {
        printf("%s: system init failed with %d!\n", __FUNCTION__, s32Ret);
        return HI_FAILURE;
    }

    MYSAMPLE_AUDIO_Usage();
    
    while ((ch = getchar()) != 'q')
    {
        switch (ch)
        {
            case '1':
            {
                s32Ret = TEST_AUDIO_AiAo(&stAioAttr,0,0);/* AI to AO*/
                break;
            }
            case '2':
            {
                s32Ret = TEST_AUDIO_AiAo(&stAioAttr,1,1);
                break;
            }
            case '3':
            {
                //s32Ret = TEST_AUDIO_AiAo(&stAioAttr,2,2);
                break;
            }
            
            case '4':
            {
                //s32Ret = TEST_AUDIO_AiAo(&stAioAttr,3,3);
                break;
            }
            
            default:
            {
                MYSAMPLE_AUDIO_Usage();
                break;
            }
        }
        if (s32Ret != HI_SUCCESS)
        {
        	printf("There was some error!\n");
            //break;
        }
    }

    SAMPLE_COMM_SYS_Exit();

    return HI_SUCCESS;
}

#ifdef __cplusplus
#if __cplusplus
}
#endif 
#endif /* End of #ifdef __cplusplus */
