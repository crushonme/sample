/******************************************************************************
  A simple program of Hisilicon HI3531 video decode implementation.
  Copyright (C), 2010-2011, Hisilicon Tech. Co., Ltd.
 ******************************************************************************
    Modification:  2011-12 Created
******************************************************************************/
#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* End of #ifdef __cplusplus */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <assert.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/mman.h>

#include "sample_comm.h"

#define SAMPLE_MAX_VDEC_CHN_CNT 16

typedef struct sample_vdec_sendparam
{
    pthread_t Pid;
    HI_BOOL bRun;
    VDEC_CHN VdChn;    
    PAYLOAD_TYPE_E enPayload;
	HI_S32 s32MinBufSize;
    VIDEO_MODE_E enVideoMode;
}SAMPLE_VDEC_SENDPARAM_S;
typedef enum sample_vdec_comp_E
{
    VDEC_ON_VDH = 0,/*ON VDH*/
    VDEC_ON_VEDU ,/*ON VEDU*/
    VDEC_ON_BUTT
}SAMPLE_VDEC_COMP_E;

//HI_S32 gs_VoDevCnt = 4;
VIDEO_NORM_E gs_enNorm = VIDEO_ENCODING_MODE_PAL;
HI_U32 gs_u32ViFrmRate = 0;
SAMPLE_VDEC_SENDPARAM_S gs_SendParam[SAMPLE_MAX_VDEC_CHN_CNT];
HI_S32 gs_s32Cnt;

/******************************************************************************
* function : send EOS stream to vdec
* WARNING : IN Hi3531, user needn't send EOS.
******************************************************************************/
static HI_VOID SAMPLE_VDEC_SendEos(VDEC_CHN VdChn)
{
    return;
}
    
/******************************************************************************
* function : send stream to vdec
******************************************************************************/
void* SAMPLE_VDEC_SendStream(void* p)
{
    VDEC_STREAM_S stStream;
    SAMPLE_VDEC_SENDPARAM_S *pstSendParam;
    char sFileName[50], sFilePostfix[20];
    FILE* fp = NULL;
    HI_S32 s32Ret;
    HI_S32 s32BlockMode = HI_IO_BLOCK;
    struct timeval stTime,*ptv; 
    HI_U8 *pu8Buf;
    HI_S32 s32LeftBytes,i;
    HI_BOOL bTimeFlag=HI_TRUE;
    HI_U64 pts= 0;
    HI_S32 s32IntervalTime = 40000;
    
    HI_U32 u32StartCode[4] = {0x41010000, 0x67010000, 0x01010000, 0x61010000};
    HI_U16 u16JpegStartCode = 0xD9FF;

    s32LeftBytes = 0;

    pstSendParam = (SAMPLE_VDEC_SENDPARAM_S *)p;

    /*open the stream file*/
    SAMPLE_COMM_SYS_Payload2FilePostfix(pstSendParam->enPayload, sFilePostfix);
    sprintf(sFileName, "stream_chn0%s", sFilePostfix);
    fp = fopen(sFileName, "r");
    if (HI_NULL == fp)
    {
        SAMPLE_PRT("open file %s err\n", sFileName);
        return NULL;
    }
    printf("open file [%s] ok!\n", sFileName);

    if(pstSendParam->s32MinBufSize!=0)
    {
        pu8Buf=malloc(pstSendParam->s32MinBufSize);
        if(pu8Buf==NULL)
        {
            SAMPLE_PRT("can't alloc %d in send stream thread:%d\n",pstSendParam->s32MinBufSize,pstSendParam->VdChn);
            fclose(fp);
            return (HI_VOID *)(HI_FAILURE);
        }
    }
    else
    {
    	SAMPLE_PRT("none buffer to operate in send stream thread:%d\n",pstSendParam->VdChn);
    	return (HI_VOID *)(HI_FAILURE);
    }
    ptv=(struct timeval *)&stStream.u64PTS;

    while (pstSendParam->bRun)
    {
        if(gettimeofday(&stTime,NULL))
        {
            if(bTimeFlag)
                SAMPLE_PRT("can't get time for pts in send stream thread %d\n",pstSendParam->VdChn);
            bTimeFlag=HI_FALSE;
        }
        stStream.u64PTS= 0;//((HI_U64)(stTime.tv_sec)<<32)|((HI_U64)stTime.tv_usec);
        stStream.pu8Addr=pu8Buf;
        stStream.u32Len=fread(pu8Buf+s32LeftBytes,1,pstSendParam->s32MinBufSize-s32LeftBytes,fp);
        // SAMPLE_PRT("bufsize:%d,readlen:%d,left:%d\n",pstVdecThreadParam->s32MinBufSize,stStream.u32Len,s32LeftBytes);
        s32LeftBytes=stStream.u32Len+s32LeftBytes;
       
        if((pstSendParam->enVideoMode==VIDEO_MODE_FRAME)&&(pstSendParam->enPayload== PT_H264))
        {
            HI_U8 *pFramePtr;
            HI_U32 u32StreamVal;
            HI_BOOL bFindStartCode = HI_FALSE;
            pFramePtr=pu8Buf+4;
            for(i=0;i<(s32LeftBytes-4);i++)
            {
                u32StreamVal=(pFramePtr[0]);
                u32StreamVal=u32StreamVal|((HI_U32)pFramePtr[1]<<8);
                u32StreamVal=u32StreamVal|((HI_U32)pFramePtr[2]<<16);
                u32StreamVal=u32StreamVal|((HI_U32)pFramePtr[3]<<24);
                if(  (u32StreamVal==u32StartCode[1])||
                (u32StreamVal==u32StartCode[0])||
                (u32StreamVal==u32StartCode[2])||
                (u32StreamVal==u32StartCode[3]))
            	 {
                    bFindStartCode = HI_TRUE;
                    break;
            	 }
            	pFramePtr++;
            }
            if (HI_FALSE == bFindStartCode)
            {
                printf("\033[0;31mALERT!!!,the search buffer is not big enough for one frame!!!%d\033[0;39m\n",
                pstSendParam->VdChn);
            }
        	i=i+4;
        	stStream.u32Len=i;
        	s32LeftBytes=s32LeftBytes-i;
        }
        else if((pstSendParam->enVideoMode==VIDEO_MODE_FRAME)&&((pstSendParam->enPayload== PT_JPEG)
            ||(pstSendParam->enPayload == PT_MJPEG)))
        {
            HI_U8 *pFramePtr;
            HI_U16 u16StreamVal;
            HI_BOOL bFindStartCode = HI_FALSE;
            pFramePtr=pu8Buf; 
            for(i=0;i<(s32LeftBytes-1);i++)
            {
                u16StreamVal=(pFramePtr[0]);
                u16StreamVal=u16StreamVal|((HI_U16)pFramePtr[1]<<8);
                if(  (u16StreamVal == u16JpegStartCode))
                {
                    bFindStartCode = HI_TRUE;
                    break;
                }
                pFramePtr++;
            }
            if (HI_FALSE == bFindStartCode)
            {
                printf("\033[0;31mALERT!!!,the search buffer is not big enough for one frame!!!%d\033[0;39m\n",
                pstSendParam->VdChn);
            }
            i=i+2;
            stStream.u32Len=i;
            s32LeftBytes=s32LeftBytes-i;
        }
        else // stream mode 
        {
            stStream.u32Len=s32LeftBytes;
            s32LeftBytes=0;
        }

        stStream.u64PTS = pts;
        pts+=40000;
        s32Ret=HI_MPI_VDEC_SendStream(pstSendParam->VdChn, &stStream, s32BlockMode);
        if (HI_SUCCESS != s32Ret)
        {
            //SAMPLE_PRT("failret:%x\n",s32Ret);
            usleep(s32IntervalTime);
        }
        if(s32BlockMode==HI_IO_NOBLOCK && s32Ret==HI_FAILURE)
        {
            usleep(s32IntervalTime);
        }
        else if(s32BlockMode==HI_IO_BLOCK && s32Ret==HI_FAILURE)
        {
            SAMPLE_PRT("can't send stream in send stream thread %d\n",pstSendParam->VdChn);
            usleep(s32IntervalTime);
        }
        
        if(pstSendParam->enVideoMode==VIDEO_MODE_FRAME && s32Ret==HI_SUCCESS)
        {
            memcpy(pu8Buf,pu8Buf+stStream.u32Len,s32LeftBytes);
        }
        else if (pstSendParam->enVideoMode==VIDEO_MODE_FRAME && s32Ret!=HI_SUCCESS)
        {
            s32LeftBytes = s32LeftBytes+stStream.u32Len;
        }

        if(stStream.u32Len!=(pstSendParam->s32MinBufSize-s32LeftBytes))
        {
            printf("file end.\n");
            //fseek(fp,0,SEEK_SET);
            SAMPLE_VDEC_SendEos(0); // in hi3531, user needn't send eos.
            break;
        }

        usleep(20000);
    }
    fflush(stdout);
    free(pu8Buf);
    fclose(fp);

    return (HI_VOID *)HI_SUCCESS;
}

/******************************************************************************
* function : create vdec chn
******************************************************************************/
static HI_S32 SAMPLE_VDEC_CreateVdecChn(HI_S32 s32ChnID, SIZE_S *pstSize, PAYLOAD_TYPE_E enType, VIDEO_MODE_E enVdecMode)
{
    VDEC_CHN_ATTR_S stAttr;
    VDEC_PRTCL_PARAM_S stPrtclParam;
    HI_S32 s32Ret;

    memset(&stAttr, 0, sizeof(VDEC_CHN_ATTR_S));

    stAttr.enType = enType;
    stAttr.u32BufSize = pstSize->u32Height * pstSize->u32Width;//This item should larger than u32Width*u32Height/2
    stAttr.u32Priority = 1;//�˴��������0
    stAttr.u32PicWidth = pstSize->u32Width;
    stAttr.u32PicHeight = pstSize->u32Height;
    
    switch (enType)
    {
        case PT_H264:
    	    stAttr.stVdecVideoAttr.u32RefFrameNum = 1;
    	    stAttr.stVdecVideoAttr.enMode = enVdecMode;
    	    stAttr.stVdecVideoAttr.s32SupportBFrame = 0;
            break;
        case PT_JPEG:
            stAttr.stVdecJpegAttr.enMode = enVdecMode;
            break;
        case PT_MJPEG:
            stAttr.stVdecJpegAttr.enMode = enVdecMode;
            break;
        default:
            SAMPLE_PRT("err type \n");
            return HI_FAILURE;
    }

    s32Ret = HI_MPI_VDEC_CreateChn(s32ChnID, &stAttr);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("HI_MPI_VDEC_CreateChn failed errno 0x%x \n", s32Ret);
        return s32Ret;
    }

    s32Ret = HI_MPI_VDEC_GetPrtclParam(s32ChnID, &stPrtclParam);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("HI_MPI_VDEC_GetPrtclParam failed errno 0x%x \n", s32Ret);
        return s32Ret;
    }

    stPrtclParam.s32MaxSpsNum = 21;
    stPrtclParam.s32MaxPpsNum = 22;
    stPrtclParam.s32MaxSliceNum = 100;
    s32Ret = HI_MPI_VDEC_SetPrtclParam(s32ChnID, &stPrtclParam);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("HI_MPI_VDEC_SetPrtclParam failed errno 0x%x \n", s32Ret);
        return s32Ret;
    }

    s32Ret = HI_MPI_VDEC_StartRecvStream(s32ChnID);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("HI_MPI_VDEC_StartRecvStream failed errno 0x%x \n", s32Ret);
        return s32Ret;
    }

    return HI_SUCCESS;
}

/******************************************************************************
* function : force to stop decoder and destroy channel.
*            stream left in decoder will not be decoded.
******************************************************************************/
void SAMPLE_VDEC_ForceDestroyVdecChn(HI_S32 s32ChnID)
{
    HI_S32 s32Ret;

    s32Ret = HI_MPI_VDEC_StopRecvStream(s32ChnID);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("HI_MPI_VDEC_StopRecvStream failed errno 0x%x \n", s32Ret);
    }

    s32Ret = HI_MPI_VDEC_DestroyChn(s32ChnID);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("HI_MPI_VDEC_DestroyChn failed errno 0x%x \n", s32Ret);
    }
}

/******************************************************************************
* function : wait for decoder finished and destroy channel.
*            Stream left in decoder will be decoded.
******************************************************************************/
void SAMPLE_VDEC_WaitDestroyVdecChn(HI_S32 s32ChnID, VIDEO_MODE_E enVdecMode)
{
    HI_S32 s32Ret;
    VDEC_CHN_STAT_S stStat;

    memset(&stStat, 0, sizeof(VDEC_CHN_STAT_S));

    s32Ret = HI_MPI_VDEC_StopRecvStream(s32ChnID);
    if (s32Ret != HI_SUCCESS)
    {
        SAMPLE_PRT("HI_MPI_VDEC_StopRecvStream failed errno 0x%x \n", s32Ret);
        return;
    }

    /*** wait destory ONLY used at frame mode! ***/
    if (VIDEO_MODE_FRAME == enVdecMode)
    {
        while (1)
        {
            //printf("LeftPics:%d, LeftStreamFrames:%d\n", stStat.u32LeftPics,stStat.u32LeftStreamFrames);
            usleep(40000);
            s32Ret = HI_MPI_VDEC_Query(s32ChnID, &stStat);
            if (s32Ret != HI_SUCCESS)
            {
                SAMPLE_PRT("HI_MPI_VDEC_Query failed errno 0x%x \n", s32Ret);
                return;
            }
            if ((stStat.u32LeftPics == 0) && (stStat.u32LeftStreamFrames == 0))
            {
                printf("had no stream and pic left\n");
                break;
            }
        }
    }
    s32Ret = HI_MPI_VDEC_DestroyChn(s32ChnID);
    if (s32Ret != HI_SUCCESS)
    {
        SAMPLE_PRT("HI_MPI_VDEC_DestroyChn failed errno 0x%x \n", s32Ret);
        return;
    }
}

/******************************************************************************
* function : show usage
******************************************************************************/
void SAMPLE_VDEC_Usage(char *sPrgNm)
{
    printf("Usage : %s <index>\n", sPrgNm);
    printf("index:\n");
    printf("\t0) H264 -> VPSS -> VO(HD).\n");
    printf("\t1) JPEG ->VPSS -> VO(HD).\n");
    printf("\t2) MJPEG -> VO(SD).\n");
    return;
}

/******************************************************************************
* function : to process abnormal case
******************************************************************************/
void SAMPLE_VDEC_HandleSig(HI_S32 signo)
{
    HI_S32 i;

    if (SIGINT == signo || SIGTSTP == signo)
    {
        printf("SAMPLE_VDEC_HandleSig\n");
        for (i=0; i<gs_s32Cnt; i++)
        {
            if (HI_FALSE != gs_SendParam[i].bRun)
            {
                gs_SendParam[i].bRun = HI_FALSE;
                pthread_join(gs_SendParam[i].Pid, 0);
            }
            printf("join thread %d.\n", i);
        }

        SAMPLE_COMM_SYS_Exit();

        printf("program exit abnormally!\n");    
    }

    exit(-1);
}

/******************************************************************************
* function : vdec process
*            vo is sd : vdec -> vo
*            vo is hd : vdec -> vpss -> vo
******************************************************************************/
HI_S32 SAMPLE_VDEC_Process(PIC_SIZE_E enPicSize, PAYLOAD_TYPE_E enType, HI_S32 s32Cnt, VO_DEV VoDev)
{
    VDEC_CHN VdChn;
    HI_S32 s32Ret;
    SIZE_S stSize;
    VB_CONF_S stVbConf;
    HI_S32 i;
    VPSS_GRP VpssGrp;
    VIDEO_MODE_E enVdecMode;
	SAMPLE_VDEC_COMP_E enVdecComp;
    HI_CHAR ch;

    VO_CHN VoChn;
    VO_PUB_ATTR_S stVoPubAttr; 
    SAMPLE_VO_MODE_E enVoMode;
    HI_U32 u32WndNum, u32BlkSize;

    HI_BOOL bVoHd; // through Vpss or not. if vo is SD, needn't through vpss
 
    /******************************************
     step 1: init varaible.
    ******************************************/
    gs_u32ViFrmRate = (VIDEO_ENCODING_MODE_PAL == gs_enNorm)?25:30;
    
    if (s32Cnt > SAMPLE_MAX_VDEC_CHN_CNT || s32Cnt <= 0)
    {
        SAMPLE_PRT("Vdec count %d err, should be in [%d,%d]. \n", s32Cnt, 1, SAMPLE_MAX_VDEC_CHN_CNT);
        
        return HI_FAILURE;
    }
    s32Ret = SAMPLE_COMM_SYS_GetPicSize(gs_enNorm, enPicSize, &stSize);
    if (HI_SUCCESS !=s32Ret)
    {
        SAMPLE_PRT("get picture size failed!\n");
        return HI_FAILURE;
    }

    // through Vpss or not. if vo is SD, needn't through vpss
    if (SAMPLE_VO_DEV_DHD0 != VoDev ) 
    {
        bVoHd = HI_FALSE;
    }
    else
    {
        bVoHd = HI_TRUE;
    }
    /******************************************
     step 2: mpp system init.
    ******************************************/
    memset(&stVbConf,0,sizeof(VB_CONF_S));

    u32BlkSize = SAMPLE_COMM_SYS_CalcPicVbBlkSize(gs_enNorm,\
                PIC_D1, SAMPLE_PIXEL_FORMAT, SAMPLE_SYS_ALIGN_WIDTH);
    stVbConf.u32MaxPoolCnt = 128;

    /* hist buf*/
    stVbConf.astCommPool[0].u32BlkSize = (196*4);
    stVbConf.astCommPool[0].u32BlkCnt = s32Cnt * 6;
    memset(stVbConf.astCommPool[0].acMmzName,0,
        sizeof(stVbConf.astCommPool[0].acMmzName));
	
    s32Ret = SAMPLE_COMM_SYS_Init(&stVbConf);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("mpp init failed!\n");
        return HI_FAILURE;
    }
  
    /******************************************
     step 3: start vpss, if ov is hd.
    ******************************************/    
    if (HI_TRUE == bVoHd)
    {
        s32Ret = SAMPLE_COMM_VPSS_Start(s32Cnt, &stSize, VPSS_MAX_CHN_NUM,NULL);
        if (HI_SUCCESS !=s32Ret)
        {
            SAMPLE_PRT("vpss start failed!\n");
            goto END_0;
        }
    }
 
    /******************************************
     step 4: start vo
    ******************************************/
    u32WndNum = 1;
    enVoMode = VO_MODE_1MUX;

    if (HI_TRUE == bVoHd)
    {
        if(VIDEO_ENCODING_MODE_PAL== gs_enNorm)
        {
            stVoPubAttr.enIntfSync = VO_OUTPUT_720P50;
        }
        else
        {
            stVoPubAttr.enIntfSync = VO_OUTPUT_720P60;
        }
        
        stVoPubAttr.enIntfType = VO_INTF_VGA;
        stVoPubAttr.u32BgColor = 0x000000ff;
        stVoPubAttr.bDoubleFrame = HI_FALSE;
    }
    else
    {
        if(VIDEO_ENCODING_MODE_PAL== gs_enNorm)
        {
            stVoPubAttr.enIntfSync = VO_OUTPUT_PAL;
        }
        else
        {
            stVoPubAttr.enIntfSync = VO_OUTPUT_NTSC;
        }
        
        stVoPubAttr.enIntfType = VO_INTF_CVBS;
        stVoPubAttr.u32BgColor = 0x000000ff;
        stVoPubAttr.bDoubleFrame = HI_FALSE;
    }
    
    s32Ret = SAMPLE_COMM_VO_StartDevLayer(VoDev, &stVoPubAttr, gs_u32ViFrmRate);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("Start SAMPLE_COMM_VO_StartDevLayer failed!\n");
        goto END_1;
    }
    
    s32Ret = SAMPLE_COMM_VO_StartChn(VoDev, &stVoPubAttr, enVoMode);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("Start SAMPLE_COMM_VO_StartChn failed!\n");
        goto END_2;
    }

    if (HI_TRUE == bVoHd)
    {
        /* if it's displayed on HDMI, we should start HDMI */
        if (stVoPubAttr.enIntfType & VO_INTF_HDMI)
        {
            if (HI_SUCCESS != SAMPLE_COMM_VO_HdmiStart(stVoPubAttr.enIntfSync))
            {
                SAMPLE_PRT("Start SAMPLE_COMM_VO_HdmiStart failed!\n");
                goto END_1;
            }
        }
    }

    for(i=0;i<u32WndNum;i++)
    {
        VoChn = i;

        if (HI_TRUE == bVoHd)
        {
            VpssGrp = i;
            s32Ret = SAMPLE_COMM_VO_BindVpss(VoDev,VoChn,VpssGrp,VPSS_PRE0_CHN);
            if (HI_SUCCESS != s32Ret)
            {
                SAMPLE_PRT("SAMPLE_COMM_VO_BindVpss failed!\n");
                goto END_2;
            }
        }
    }

    /******************************************
     step 5: start vdec & bind it to vpss or vo
    ******************************************/ 
    if (PT_H264 == enType)
    {
        while(1)
        {
// f65132 Hi3520A����Ĺ���Ϊ��Hi3521һ��, �������ﲻ��Ҫ�궨��������        
//#if HICHIP == HI3521_V100
            printf("please choose vdec on VDH or on VEDU:\n");
            printf("\t0) vdec on VDH,vdec chn [0,31]\n");
            printf("\t1) vdec on VEDU,vdec chn [32,63]\n");
            ch = getchar();
            getchar();
//#elif HICHIP == HI3520A_V100
//            ch = '1';
//#else
//    #error Unkown chip!
//#endif
            if ('0' == ch)
            {
                enVdecComp = VDEC_ON_VDH;
                break;
            }
            else if ('1' == ch)
            {
                enVdecComp = VDEC_ON_VEDU;
                break;
            }
            else
            {
                printf("input invaild! please try again.\n");
                continue;
            }
        }
        
        while(1)
        {
            printf("please choose vdec mode:\n");
            printf("\t0) frame mode.\n");
            printf("\t1) stream mode.\n");
            ch = getchar();
            getchar();
            if ('0' == ch)
            {
                enVdecMode = VIDEO_MODE_FRAME;
                break;
            }
            else if ('1' == ch)
            {
                enVdecMode = VIDEO_MODE_STREAM;
                break;
            }
            else
            {
                printf("input invaild! please try again.\n");
                continue;
            }
        }
    }
    else	//JPEG, MJPEG must be Frame mode!
    {
        enVdecComp = VDEC_ON_VDH;
        enVdecMode = VIDEO_MODE_FRAME;
    }
    
    for (i=0; i<s32Cnt; i++)
    {
        /*** create vdec chn ***/
		if(VDEC_ON_VDH == enVdecComp)
		{
	        VdChn = i;
		}
		else
		{
			VdChn = i+32;
		}
        s32Ret = SAMPLE_VDEC_CreateVdecChn(VdChn, &stSize, enType, enVdecMode);
        if (HI_SUCCESS !=s32Ret)
        {
            SAMPLE_PRT("create vdec chn failed!\n");
            goto END_3;
        }
        /*** bind vdec to vpss ***/
        if (HI_TRUE == bVoHd)
        {
            VpssGrp = i;
            s32Ret = SAMLE_COMM_VDEC_BindVpss(VdChn, VpssGrp);
            if (HI_SUCCESS !=s32Ret)
            {
                SAMPLE_PRT("vdec(vdch=%d) bind vpss(vpssg=%d) failed!\n", VdChn, VpssGrp);
                goto END_3;
            }
        }
        else
    	{
            VoChn =  i;
            s32Ret = SAMLE_COMM_VDEC_BindVo(VdChn, VoDev, VoChn);
            if (HI_SUCCESS !=s32Ret)
            {
                SAMPLE_PRT("vdec(vdch=%d) bind vpss(vpssg=%d) failed!\n", VdChn, VpssGrp);
                goto END_3;
            }
        }
    }

    /******************************************
     step 6: open file & video decoder
    ******************************************/
    for (i=0; i<s32Cnt; i++)
    {
        gs_SendParam[i].bRun = HI_TRUE;
        gs_SendParam[i].VdChn = (VDEC_ON_VDH == enVdecComp)?i:(i+32);
        gs_SendParam[i].enPayload = enType;
        gs_SendParam[i].enVideoMode = enVdecMode;
        gs_SendParam[i].s32MinBufSize = stSize.u32Height * stSize.u32Width / 2;
        pthread_create(&gs_SendParam[i].Pid, NULL, SAMPLE_VDEC_SendStream, &gs_SendParam[i]);
    }

    if (PT_JPEG != enType)
    {
        printf("you can press ctrl+c to terminate program before normal exit.\n");
    }
    /******************************************
     step 7: join thread
    ******************************************/
    for (i=0; i<s32Cnt; i++)
    {
        //gs_SendParam[i].bRun = HI_FALSE;
        pthread_join(gs_SendParam[i].Pid, 0);
        printf("join thread %d.\n", i);
    }

    printf("press two enter to quit!\n");
    getchar();
    getchar();
    /******************************************
     step 8: Unbind vdec to vpss & destroy vdec-chn
    ******************************************/
END_3:
    for (i=0; i<s32Cnt; i++)
    {
        VdChn = (VDEC_ON_VDH == enVdecComp)?i:(i+32);
        SAMPLE_VDEC_WaitDestroyVdecChn(VdChn, enVdecMode);
        if (HI_TRUE == bVoHd)
        {
            VpssGrp = i;
            SAMLE_COMM_VDEC_UnBindVpss(VdChn, VpssGrp);
        }
        else
        {
            VoChn = i;
            SAMLE_COMM_VDEC_UnBindVo(VdChn, VoDev, VoChn);
        }
    }
    /******************************************
     step 9: stop vo
    ******************************************/
END_2:
    for(i=0;i<u32WndNum;i++)
    {
        VoChn = i;
        if (HI_TRUE == bVoHd)
        {
            VpssGrp = i;
            SAMPLE_COMM_VO_UnBindVpss(VoDev,VoChn,VpssGrp,VPSS_PRE0_CHN);
        }
    }
    SAMPLE_COMM_VO_StopChn(VoDev, enVoMode);
    SAMPLE_COMM_VO_StopDevLayer(VoDev);
END_1:
    if (HI_TRUE == bVoHd)
    {
        if (stVoPubAttr.enIntfType & VO_INTF_HDMI)
        {
            SAMPLE_COMM_VO_HdmiStop();
        }
        SAMPLE_COMM_VPSS_Stop(s32Cnt, VPSS_MAX_CHN_NUM);
    }
    /******************************************
     step 10: exit mpp system
    ******************************************/
END_0:
    SAMPLE_COMM_SYS_Exit();

    return HI_SUCCESS;
}

/****************************************************************************
* function: main
****************************************************************************/
int main(int argc, char* argv[])
{
    HI_S32 s32Index;
    if (argc != 2)
    {
        SAMPLE_VDEC_Usage(argv[0]);
        return HI_FAILURE;
    }
    
    signal(SIGINT, SAMPLE_VDEC_HandleSig);
    signal(SIGTERM, SAMPLE_VDEC_HandleSig);

    s32Index = atoi(argv[1]);

    switch (s32Index)
    {
        case 0: /* H264 -> VPSS -> VO(HD) */
            gs_s32Cnt = 1;
            SAMPLE_VDEC_Process(PIC_D1, PT_H264, gs_s32Cnt, SAMPLE_VO_DEV_DHD0);
            break;
        case 1: /* JPEG ->VPSS -> VO(HD) */
            gs_s32Cnt = 1;
            SAMPLE_VDEC_Process(PIC_D1, PT_JPEG, gs_s32Cnt, SAMPLE_VO_DEV_DHD0);
            break;
        case 2: /* MJPEG -> VO(SD) */
            gs_s32Cnt = 1;
            SAMPLE_VDEC_Process(PIC_D1, PT_MJPEG, gs_s32Cnt, SAMPLE_VO_DEV_DSD0);
            break;
            break;
        default:
            printf("the index is invaild!\n");
            SAMPLE_VDEC_Usage(argv[0]);
            return HI_FAILURE;
        break;
    }

    return HI_SUCCESS;
}

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */
