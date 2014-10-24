/******************************************************************************
  A simple program of Hisilicon HI3531 video input and output implementation.
  Copyright (C), 2010-2011, Hisilicon Tech. Co., Ltd.
 ******************************************************************************
    Modification:  2011-8 Created
******************************************************************************/

#ifdef __cplusplus
#if __cplusplus
extern "C"{
#endif
#endif /* End of #ifdef __cplusplus */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include "sample_comm.h"

VIDEO_NORM_E gs_enNorm = VIDEO_ENCODING_MODE_PAL;
SAMPLE_VIDEO_LOSS_S gs_stVideoLoss;
HI_U32 gs_u32ViFrmRate = 0; 

/******************************************************************************
* function : show usage
******************************************************************************/
void SAMPLE_VIO_Usage(char *sPrgNm)
{
    printf("Usage : %s <mode> <index>\n", sPrgNm);
	printf("mode:\n");
	printf("\t P) PAL MODE\n");
	printf("\t N) NTSC MODE\n");
    printf("index:\n");
#if HICHIP == HI3521_V100
    printf("\t 0) VI:8*D1; VO:HD0(HDMI + VGA), WBC to SD0(CVBS) + SD1(CVBS) video preview.\n");
    printf("\t 1) VI:1*720P; VO:HD0(HDMI + VGA), WBC to SD0(CVBS) video preview.\n");
    printf("\t 2) VI:16*2Cif + VO:HD0(HDMI + VGA),  WBC to SD0(CVBS) video preview.\n");
#elif HICHIP == HI3520A_V100
    printf("\t 0) VI:8*D1; VO:HD0(HDMI + VGA), WBC to SD0(CVBS).\n");
    printf("\t 1) VI:1*720P; VO:HD0(HDMI + VGA), WBC to SD0(CVBS) video preview.\n");
    printf("\t 2) VI:16*Cif + VO:HD0(HDMI + VGA),  WBC to SD0(CVBS) video preview.\n");
#else
    #error Unknow chip!
#endif
    printf("\t 3) VI:16*D1/2Cif MixCap + VO:HD0(HDMI + VGA).\n");	
    printf("\t 4) VI: 1*D1, user picture; VO: SD0(CVBS) video preview.\n");
    printf("\t 5) VI: 1*D1; VO:HD0(VGA) HD Zoom preview.\n");
    printf("\t 6) VI: 1*D1; VO:SD0(CVBS) SD Zoom preview.\n");
    printf("\t 7) VI: CMOS and AV; VO:HD0(LCD 1024x768)HDMI HD Zoom preview.\n");	
	printf("\t 8) VI: 1*VGA; VO:HD0(LCD 1024x768) HD Zoom preview.\n");	
	printf("\t 9) VI: CMOS only; capture save.\n");	
    return;
}

/******************************************************************************
* function : to process abnormal case                                         
******************************************************************************/
void SAMPLE_VIO_HandleSig(HI_S32 signo)
{
    if (SIGINT == signo || SIGTSTP == signo)
    {
        SAMPLE_COMM_SYS_Exit();
        printf("\033[0;31mprogram termination abnormally!\033[0;39m\n");
    }
    exit(-1);
}

/******************************************************************************
* function : video loss detect process                                         
* NOTE: If your ADC stop output signal when NoVideo, you can open VDET_USE_VI macro.
******************************************************************************/
//#define VDET_USE_VI    
#ifdef VDET_USE_VI  
static HI_S32 s_astViLastIntCnt[VIU_MAX_CHN_NUM] = {0};
void *SAMPLE_VI_VLossDetProc(void *parg)
{ 
    VI_CHN ViChn;
    SAMPLE_VI_PARAM_S stViParam;
    HI_S32 s32Ret, i, s32ChnPerDev;
    VI_CHN_STAT_S stStat;
    SAMPLE_VIDEO_LOSS_S *ctl = (SAMPLE_VIDEO_LOSS_S*)parg;
    
    s32Ret = SAMPLE_COMM_VI_Mode2Param(ctl->enViMode, &stViParam);
    if (HI_SUCCESS !=s32Ret)
    {
        SAMPLE_PRT("vi get param failed!\n");
        return NULL;
    }
    s32ChnPerDev = stViParam.s32ViChnCnt / stViParam.s32ViDevCnt;
    
    while (ctl->bStart)
    {
        for (i = 0; i < stViParam.s32ViChnCnt; i++)
        {
            ViChn = i * stViParam.s32ViChnInterval;

            s32Ret = HI_MPI_VI_Query(ViChn, &stStat);
            if (HI_SUCCESS !=s32Ret)
            {
                SAMPLE_PRT("HI_MPI_VI_Query failed with %#x!\n", s32Ret);
                return NULL;
            }

            if (stStat.u32IntCnt == s_astViLastIntCnt[i])
            {
                printf("VI Chn (%d) int lost , int_cnt:%d \n", ViChn, stStat.u32IntCnt);
                HI_MPI_VI_EnableUserPic(ViChn);
            }
            else
            {
                HI_MPI_VI_DisableUserPic(ViChn);
            }
            s_astViLastIntCnt[i] = stStat.u32IntCnt;
        }
        usleep(500000);
    }
    
    ctl->bStart = HI_FALSE;
    
    return NULL;
}
#else  
void *SAMPLE_VI_VLossDetProc(void *parg)
{  
    int fd;
    HI_S32 s32Ret, i, s32ChnPerDev;
    VI_DEV ViDev;
    VI_CHN ViChn;
    tw2865_video_loss video_loss;
    SAMPLE_VI_PARAM_S stViParam;
    SAMPLE_VIDEO_LOSS_S *ctl = (SAMPLE_VIDEO_LOSS_S*)parg;
    
    fd = open(TW2865_FILE, O_RDWR);
    if (fd < 0)
    {
        printf("open %s fail\n", TW2865_FILE);
        ctl->bStart = HI_FALSE;
        return NULL;
    }

    s32Ret = SAMPLE_COMM_VI_Mode2Param(ctl->enViMode, &stViParam);
    if (HI_SUCCESS !=s32Ret)
    {
        SAMPLE_PRT("vi get param failed!\n");
        return NULL;
    }
    s32ChnPerDev = stViParam.s32ViChnCnt / stViParam.s32ViDevCnt;
    
    while (ctl->bStart)
    {
        for (i = 0; i < stViParam.s32ViChnCnt; i++)
        {
            ViChn = i * stViParam.s32ViChnInterval;
            ViDev = SAMPLE_COMM_VI_GetDev(ctl->enViMode, ViChn);
            if (ViDev < 0)
            {
                SAMPLE_PRT("get vi dev failed !\n");
                return NULL;
            }
            
            video_loss.chip = stViParam.s32ViDevCnt;
            video_loss.ch   = ViChn % s32ChnPerDev;
			//printf("video_loss.chip %d, video_loss.ch %d\n",video_loss.chip,video_loss.ch );
            ioctl(fd, TW2865_GET_VIDEO_LOSS, &video_loss);

            if (video_loss.is_lost)
            {   printf("pic loss\n");
                HI_MPI_VI_EnableUserPic(ViChn);
            }
            else
            {
                HI_MPI_VI_DisableUserPic(ViChn);
            }                
        }
        usleep(500000);
    }
    
    close(fd);
    ctl->bStart = HI_FALSE;
    
    return NULL;
}
#endif  

HI_S32 SAMPLE_VI_StartVLossDet(SAMPLE_VI_MODE_E enViMode)
{
    HI_S32 s32Ret;
    
    gs_stVideoLoss.bStart= HI_TRUE;
    gs_stVideoLoss.enViMode = enViMode;
    s32Ret = pthread_create(&gs_stVideoLoss.Pid, 0, SAMPLE_VI_VLossDetProc, &gs_stVideoLoss);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("pthread_create failed with %#x!\n", s32Ret);
        return HI_FAILURE;
    }
    
    return HI_SUCCESS;
}

HI_VOID SAMPLE_VI_StopVLossDet()
{
    if (gs_stVideoLoss.bStart)
    {
        gs_stVideoLoss.bStart = HI_FALSE;
        pthread_join(gs_stVideoLoss.Pid, 0);
    }
    return;
}

HI_S32 SAMPLE_VI_SetUserPic(HI_CHAR *pszYuvFile, HI_U32 u32Width, HI_U32 u32Height,
        HI_U32 u32Stride, VIDEO_FRAME_INFO_S *pstFrame)
{
    FILE *pfd;
    VI_USERPIC_ATTR_S stUserPicAttr;

    /* open YUV file */
    pfd = fopen(pszYuvFile, "rb");
    if (!pfd)
    {
        printf("open file -> %s fail \n", pszYuvFile);
        return -1;
    }

    /* read YUV file. WARNING: we only support planar 420) */
    if (SAMPLE_COMM_VI_GetVFrameFromYUV(pfd, u32Width, u32Height, u32Stride, pstFrame))
    {
        return -1;
    }
    fclose(pfd);

    stUserPicAttr.bPub= HI_TRUE;
    stUserPicAttr.enUsrPicMode = VI_USERPIC_MODE_PIC;
    memcpy(&stUserPicAttr.unUsrPic.stUsrPicFrm, pstFrame, sizeof(VIDEO_FRAME_INFO_S));
    if (HI_MPI_VI_SetUserPic(0, &stUserPicAttr))
    {
        return -1;
    }

    printf("set vi user pic ok, yuvfile:%s\n", pszYuvFile);
    return HI_SUCCESS;
}

/******************************************************************************
* function :  VI:8*D1; VO:HD0(HDMI,VGA)+SD0(CVBS)+SD1 video preview
******************************************************************************/
HI_S32 SAMPLE_VIO_8_D1(HI_VOID)
{
    SAMPLE_VI_MODE_E enViMode = SAMPLE_VI_MODE_8_D1;

    HI_U32 u32ViChnCnt = 8;
    HI_S32 s32VpssGrpCnt = 8;
    
    VB_CONF_S stVbConf;
    VI_CHN ViChn;
    VPSS_GRP VpssGrp;
    VPSS_GRP_ATTR_S stGrpAttr;
    VPSS_CHN VpssChn_VoHD0 = VPSS_PRE0_CHN;
    VO_DEV VoDev;
    VO_CHN VoChn;
    VO_PUB_ATTR_S stVoPubAttr,stVoPubAttrSD; 
    SAMPLE_VO_MODE_E enVoMode, enPreVoMode;
    
    HI_S32 i;
    HI_S32 s32Ret = HI_SUCCESS;
    HI_U32 u32BlkSize;
    HI_CHAR ch;
    SIZE_S stSize;
    HI_U32 u32WndNum;
	
	VO_WBC_ATTR_S stWbcAttr;

    /******************************************
     step  1: init variable 
    ******************************************/
    gs_u32ViFrmRate = (VIDEO_ENCODING_MODE_PAL== gs_enNorm)?25:30;
    
    memset(&stVbConf,0,sizeof(VB_CONF_S));

    u32BlkSize = SAMPLE_COMM_SYS_CalcPicVbBlkSize(gs_enNorm,\
                PIC_D1, SAMPLE_PIXEL_FORMAT, SAMPLE_SYS_ALIGN_WIDTH);
    stVbConf.u32MaxPoolCnt = 128;

    /* video buffer*/
    //todo: vb=15
    stVbConf.astCommPool[0].u32BlkSize = u32BlkSize;
    stVbConf.astCommPool[0].u32BlkCnt = u32ViChnCnt * 8;

    /******************************************
     step 2: mpp system init. 
    ******************************************/
    s32Ret = SAMPLE_COMM_SYS_Init(&stVbConf);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("system init failed with %d!\n", s32Ret);
        goto END_8D1_0;
    }

    /******************************************
     step 3: start vi dev & chn
    ******************************************/
    s32Ret = SAMPLE_COMM_VI_Start(enViMode, gs_enNorm);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("start vi failed!\n");
        goto END_8D1_0;
    }

    /******************************************
     step 4: start vpss and vi bind vpss
    ******************************************/
    s32Ret = SAMPLE_COMM_SYS_GetPicSize(gs_enNorm, PIC_D1, &stSize);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_SYS_GetPicSize failed!\n");
        goto END_8D1_0;
    }
    
    stGrpAttr.u32MaxW = stSize.u32Width;
    stGrpAttr.u32MaxH = stSize.u32Height;
    stGrpAttr.bDrEn = HI_FALSE;
    stGrpAttr.bDbEn = HI_FALSE;
    stGrpAttr.bIeEn = HI_TRUE;
    stGrpAttr.bNrEn = HI_TRUE;
    stGrpAttr.bHistEn = HI_FALSE;
    stGrpAttr.enDieMode = VPSS_DIE_MODE_AUTO;
    stGrpAttr.enPixFmt = SAMPLE_PIXEL_FORMAT;

    s32Ret = SAMPLE_COMM_VPSS_Start(s32VpssGrpCnt, &stSize, VPSS_MAX_CHN_NUM,NULL);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("Start Vpss failed!\n");
        goto END_8D1_1;
    }

    s32Ret = SAMPLE_COMM_VI_BindVpss(enViMode);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("Vi bind Vpss failed!\n");
        goto END_8D1_2;
    }
#if HICHIP == HI3521_V100
	/******************************************
	step 5: start vo SD1(CVBS)
	******************************************/

	printf("start vo SD1.\n");
	VoDev = SAMPLE_VO_DEV_DSD1;
	u32WndNum = 1;
	enVoMode = VO_MODE_1MUX;

	stVoPubAttr.enIntfSync = VO_OUTPUT_PAL;
	stVoPubAttr.enIntfType = VO_INTF_CVBS;
	stVoPubAttr.u32BgColor = 0x000000ff;
	stVoPubAttr.bDoubleFrame = HI_FALSE;
	s32Ret = SAMPLE_COMM_VO_StartDevLayer(VoDev, &stVoPubAttr, gs_u32ViFrmRate);
	if (HI_SUCCESS != s32Ret)
	{
		 SAMPLE_PRT("SAMPLE_COMM_VO_StartDevLayer failed!\n");
		 goto END_8D1_3;
	}

	s32Ret = SAMPLE_COMM_VO_StartChn(VoDev, &stVoPubAttr, enVoMode);
	if (HI_SUCCESS != s32Ret)
	{
		 SAMPLE_PRT("SAMPLE_COMM_VO_StartChn failed!\n");
		 goto END_8D1_4;
	}

	VoChn = 0;
	ViChn = 0;
	s32Ret = SAMPLE_COMM_VO_BindVi(VoDev,VoChn, ViChn);
	if (HI_SUCCESS != s32Ret)
	{
		 SAMPLE_PRT("SAMPLE_COMM_VO_BindVpss failed!\n");
		 goto END_8D1_4;
	}
#endif	
    /******************************************
     step 6: start vo HD0 (HDMI+VGA), multi-screen, you can switch mode
    ******************************************/
    printf("start vo HD0.\n");
    VoDev = SAMPLE_VO_DEV_DHD0;
    u32WndNum = 16;
    enVoMode = VO_MODE_1MUX;

    if(VIDEO_ENCODING_MODE_PAL == gs_enNorm)
    {
        //stVoPubAttr.enIntfSync = VO_OUTPUT_1080P50;
        stVoPubAttr.enIntfSync = VO_OUTPUT_1366x768_60;
    }
    else
    {
        //stVoPubAttr.enIntfSync = VO_OUTPUT_1080P60;
        stVoPubAttr.enIntfSync = VO_OUTPUT_1366x768_60;
    }

	stVoPubAttr.enIntfType = VO_INTF_HDMI|VO_INTF_VGA;

    stVoPubAttr.u32BgColor = 0x000000ff;
    stVoPubAttr.bDoubleFrame = HI_TRUE;
    
    s32Ret = SAMPLE_COMM_VO_StartDevLayer(VoDev, &stVoPubAttr, gs_u32ViFrmRate);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("Start SAMPLE_COMM_VO_StartDevLayer failed!\n");
        goto END_8D1_4;
    }
    
    s32Ret = SAMPLE_COMM_VO_StartChn(VoDev, &stVoPubAttr, enVoMode);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("Start SAMPLE_COMM_VO_StartChn failed!\n");
        goto END_8D1_5;
    }

    /* if it's displayed on HDMI, we should start HDMI */
    if (stVoPubAttr.enIntfType & VO_INTF_HDMI)
    {
        if (HI_SUCCESS != SAMPLE_COMM_VO_HdmiStart(stVoPubAttr.enIntfSync))
        {
            SAMPLE_PRT("Start SAMPLE_COMM_VO_HdmiStart failed!\n");
            goto END_8D1_5;
        }
    }
    
    for(i=0;i<u32WndNum;i++)
    {
        VoChn = i;
        VpssGrp = i;
        
        s32Ret = SAMPLE_COMM_VO_BindVpss(VoDev,VoChn,VpssGrp,VpssChn_VoHD0);
        if (HI_SUCCESS != s32Ret)
        {
            SAMPLE_PRT("Start VO failed!\n");
            goto END_8D1_5;
        }
    }

	/******************************************
	step 7: start vo SD0 (CVBS) (WBC target) 
	******************************************/
	printf("start vo SD0: wbc from hd0\n");
	VoDev = SAMPLE_VO_DEV_DSD0;
	u32WndNum = 1;
	enVoMode = VO_MODE_1MUX;

	stVoPubAttrSD.enIntfSync = VO_OUTPUT_PAL;
	stVoPubAttrSD.enIntfType = VO_INTF_CVBS;
	stVoPubAttrSD.u32BgColor = 0x000000ff;
	stVoPubAttrSD.bDoubleFrame = HI_FALSE;
	s32Ret = SAMPLE_COMM_VO_StartDevLayer(VoDev, &stVoPubAttrSD, gs_u32ViFrmRate);
	if (HI_SUCCESS != s32Ret)
	{
		  SAMPLE_PRT("SAMPLE_COMM_VO_StartDevLayer failed!\n");
		  goto END_8D1_5;
	}

	s32Ret = SAMPLE_COMM_VO_StartChn(VoDev, &stVoPubAttrSD, enVoMode);
	if (HI_SUCCESS != s32Ret)
	{
		  SAMPLE_PRT("SAMPLE_COMM_VO_StartChn failed!\n");
		  goto END_8D1_6;
	}
	  
	s32Ret = SAMPLE_COMM_VO_GetWH(VO_OUTPUT_PAL, \
					  &stWbcAttr.stTargetSize.u32Width, \
					  &stWbcAttr.stTargetSize.u32Height, \
					  &stWbcAttr.u32FrameRate);
	if (HI_SUCCESS != s32Ret)
	{
		SAMPLE_PRT("SAMPLE_COMM_VO_GetWH failed!\n");
		goto END_8D1_6;
	}
	stWbcAttr.enPixelFormat = SAMPLE_PIXEL_FORMAT;

	s32Ret = HI_MPI_VO_SetWbcAttr(SAMPLE_VO_DEV_DHD0, &stWbcAttr);
	if (HI_SUCCESS != s32Ret)
	{
		SAMPLE_PRT("HI_MPI_VO_SetWbcAttr failed!\n");
		goto END_8D1_6;
	}

	s32Ret = HI_MPI_VO_EnableWbc(SAMPLE_VO_DEV_DHD0);
	if (HI_SUCCESS != s32Ret)
	{
		SAMPLE_PRT("HI_MPI_VO_SetWbcAttr failed!\n");
		goto END_8D1_7;
	}

	s32Ret = SAMPLE_COMM_VO_BindVoWbc(SAMPLE_VO_DEV_DHD0, SAMPLE_VO_DEV_DSD0, 0);
	if (HI_SUCCESS != s32Ret)
	{
		SAMPLE_PRT("HI_MPI_VO_SetWbcAttr failed!\n");
		goto END_8D1_7;
	}
	
	/******************************************
	step 8: HD0 switch mode 
	******************************************/
	VoDev = SAMPLE_VO_DEV_DHD0;
	enVoMode = VO_MODE_1MUX;
    while(1)
    {
        enPreVoMode = enVoMode;
    
        printf("please choose preview mode, press 'q' to exit this sample.\n"); 
        printf("\t0) 1 preview\n");
        printf("\t1) 4 preview\n");
        printf("\t2) 8 preview\n");
        printf("\tq) quit\n");
 
        ch = getchar();
        getchar();
        if ('0' == ch)
        {
            u32WndNum = 1;
            enVoMode = VO_MODE_1MUX;
        }
        else if ('1' == ch)
        {
            u32WndNum = 4;
            enVoMode = VO_MODE_4MUX;
        }
		/*Indeed only 8 chns show*/
        else if ('2' == ch)
        {
            u32WndNum = 9;
            enVoMode = VO_MODE_9MUX;
        }
        else if ('q' == ch)
        {
            break;
        }
        else
        {
            SAMPLE_PRT("preview mode invaild! please try again.\n");
            continue;
        }
        SAMPLE_PRT("vo(%d) switch to %d mode\n", VoDev, u32WndNum);

        s32Ret= HI_MPI_VO_SetAttrBegin(VoDev);
        if (HI_SUCCESS != s32Ret)
        {
            SAMPLE_PRT("Start VO failed!\n");
            goto END_8D1_7;
        }
        
        s32Ret = SAMPLE_COMM_VO_StopChn(VoDev, enPreVoMode);
        if (HI_SUCCESS != s32Ret)
        {
            SAMPLE_PRT("Start VO failed!\n");
            goto END_8D1_7;
        }

        s32Ret = SAMPLE_COMM_VO_StartChn(VoDev, &stVoPubAttr, enVoMode);
        if (HI_SUCCESS != s32Ret)
        {
            SAMPLE_PRT("Start VO failed!\n");
            goto END_8D1_7;
        }
        s32Ret= HI_MPI_VO_SetAttrEnd(VoDev);
        if (HI_SUCCESS != s32Ret)
        {
            SAMPLE_PRT("Start VO failed!\n");
            goto END_8D1_7;
        }
    }

    /******************************************
     step 8: exit process
    ******************************************/

END_8D1_7:
	SAMPLE_COMM_VO_UnBindVoWbc(SAMPLE_VO_DEV_DSD0, 0);
	HI_MPI_VO_DisableWbc(SAMPLE_VO_DEV_DHD0);

END_8D1_6:
	VoDev = SAMPLE_VO_DEV_DSD0;
	VoChn = 0;
	enVoMode = VO_MODE_1MUX;
    SAMPLE_COMM_VO_StopChn(VoDev, enVoMode);
    SAMPLE_COMM_VO_StopDevLayer(VoDev);

END_8D1_5:
	if (stVoPubAttr.enIntfType & VO_INTF_HDMI)
    {
        SAMPLE_COMM_VO_HdmiStop();
    }
    VoDev = SAMPLE_VO_DEV_DHD0;
    u32WndNum = 16;
    enVoMode = VO_MODE_16MUX;
    for(i=0;i<u32WndNum;i++)
    {
        VoChn = i;
        VpssGrp = i;
        SAMPLE_COMM_VO_UnBindVpss(VoDev,VoChn,VpssGrp,VpssChn_VoHD0);
    }
    SAMPLE_COMM_VO_StopChn(VoDev, enVoMode);
    SAMPLE_COMM_VO_StopDevLayer(VoDev);

END_8D1_4:
#if HICHIP == HI3521_V100
	VoDev = SAMPLE_VO_DEV_DSD1;
	VoChn = 0;
	enVoMode = VO_MODE_1MUX;
    SAMPLE_COMM_VO_UnBindVi(VoDev,VoChn); 
    SAMPLE_COMM_VO_StopChn(VoDev, enVoMode);
    SAMPLE_COMM_VO_StopDevLayer(VoDev);
#endif
END_8D1_3:	//vi unbind vpss
    SAMPLE_COMM_VI_UnBindVpss(enViMode);
END_8D1_2:	//vpss stop
    SAMPLE_COMM_VPSS_Stop(s32VpssGrpCnt, VPSS_MAX_CHN_NUM);
END_8D1_1:	//vi stop
    SAMPLE_COMM_VI_Stop(enViMode);
END_8D1_0:	//system exit
    SAMPLE_COMM_SYS_Exit();
    
    return s32Ret;
}


/******************************************************************************
* function :  VI:16*2cif; VO:HD0(HDMI  720P50), WBC to SD0(CVBS) video preview. 
******************************************************************************/
HI_S32 SAMPLE_VIO_16_2cif(HI_VOID)
{
    #if HICHIP == HI3521_V100
    SAMPLE_VI_MODE_E enViMode = SAMPLE_VI_MODE_16_2Cif;
    #elif HICHIP == HI3520A_V100
    SAMPLE_VI_MODE_E enViMode = SAMPLE_VI_MODE_16_Cif;
    #endif
    HI_U32 u32ViChnCnt = 16;
    HI_S32 s32VpssGrpCnt = 16;
    
    VB_CONF_S stVbConf;
    VPSS_GRP VpssGrp;
    VPSS_GRP_ATTR_S stGrpAttr;

	VPSS_CHN VpssChn_VoHD0 = VPSS_PRE0_CHN;
	 
    VO_DEV VoDev;
    VO_CHN VoChn;
    VO_PUB_ATTR_S stVoPubAttr; 
    VO_PUB_ATTR_S stVoPubAttr_SD; 
    SAMPLE_VO_MODE_E enVoMode, enPreVoMode;
    VO_WBC_ATTR_S stWbcAttr;
	 
    HI_S32 i;
    HI_S32 s32Ret = HI_SUCCESS;
    HI_U32 u32BlkSize;
    HI_CHAR ch;
    SIZE_S stSize,stSizeTmp;
    HI_U32 u32WndNum;

    /******************************************
     step  1: init global  variable 
    ******************************************/
    gs_u32ViFrmRate = (VIDEO_ENCODING_MODE_PAL== gs_enNorm)?25:30;
    
    memset(&stVbConf,0,sizeof(VB_CONF_S));
#if HICHIP == HI3521_V100
    u32BlkSize = SAMPLE_COMM_SYS_CalcPicVbBlkSize(gs_enNorm,\
                PIC_2CIF, SAMPLE_PIXEL_FORMAT, SAMPLE_SYS_ALIGN_WIDTH);
#elif HICHIP == HI3520A_V100
    u32BlkSize = SAMPLE_COMM_SYS_CalcPicVbBlkSize(gs_enNorm,\
                PIC_CIF, SAMPLE_PIXEL_FORMAT, SAMPLE_SYS_ALIGN_WIDTH);
#endif
    stVbConf.u32MaxPoolCnt = 128;

    /*video buffer*/
    stVbConf.astCommPool[0].u32BlkSize = u32BlkSize;
    stVbConf.astCommPool[0].u32BlkCnt = u32ViChnCnt * 12;

	u32BlkSize = SAMPLE_COMM_SYS_CalcPicVbBlkSize(gs_enNorm,\
                PIC_D1, SAMPLE_PIXEL_FORMAT, SAMPLE_SYS_ALIGN_WIDTH);
    /* video buffer*/
    stVbConf.astCommPool[1].u32BlkSize = u32BlkSize;
    stVbConf.astCommPool[1].u32BlkCnt = u32ViChnCnt * 6;

    /******************************************
     step 2: mpp system init. 
    ******************************************/
    s32Ret = SAMPLE_COMM_SYS_Init(&stVbConf);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("system init failed with %d!\n", s32Ret);
        goto END_16_2Cif_0;
    }


    /******************************************
     step 3: start vi dev & chn to capture
    ******************************************/
    s32Ret = SAMPLE_COMM_VI_Start(enViMode, gs_enNorm);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("start vi failed!\n");
        goto END_16_2Cif_0;
    }
    
    /******************************************
     step 4: start vpss and vi bind vpss
    ******************************************/
    s32Ret = SAMPLE_COMM_SYS_GetPicSize(gs_enNorm, PIC_D1, &stSize);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_SYS_GetPicSize failed!\n");
        goto END_16_2Cif_1;
    }
    
    stGrpAttr.u32MaxW = 720;
    stGrpAttr.u32MaxH = (VIDEO_ENCODING_MODE_PAL==gs_enNorm)?576:480;
    stGrpAttr.bDrEn = HI_FALSE;
    stGrpAttr.bDbEn = HI_FALSE;
    stGrpAttr.bIeEn = HI_TRUE;
    stGrpAttr.bNrEn = HI_TRUE;
    stGrpAttr.bHistEn = HI_TRUE;
    stGrpAttr.enDieMode = VPSS_DIE_MODE_AUTO;
    stGrpAttr.enPixFmt = SAMPLE_PIXEL_FORMAT;

    s32Ret = SAMPLE_COMM_VPSS_Start(s32VpssGrpCnt, &stSize, VPSS_MAX_CHN_NUM,NULL);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("Start Vpss failed!\n");
        goto END_16_2Cif_1;
    }
    /*open pre-scale*/
	s32Ret = SAMPLE_COMM_SYS_GetPicSize(gs_enNorm, PIC_2CIF, &stSize);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_SYS_GetPicSize failed!\n");
        goto END_16_2Cif_1;
    }
    for(i=0;i<s32VpssGrpCnt;i++)
    {   
		s32Ret = SAMPLE_COMM_EnableVpssPreScale(i,stSize);
		if(HI_SUCCESS != s32Ret)
		{
        SAMPLE_PRT("HI_MPI_VPSS_SetPreScale failed!\n");
        goto END_16_2Cif_1;
        }
    }

		
    s32Ret = SAMPLE_COMM_VI_BindVpss(enViMode);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("Vi bind Vpss failed!\n");
        goto END_16_2Cif_2;
    }

    /******************************************
     step 5: start vo HD1 (VGA+HDMI) (DoubleFrame) (WBC source) 
    ******************************************/
	printf("start vo HD0.\n");
	VoDev = SAMPLE_VO_DEV_DHD0;
	u32WndNum = 16;
	enVoMode = VO_MODE_16MUX;

	if(VIDEO_ENCODING_MODE_PAL == gs_enNorm)
	{
		stVoPubAttr.enIntfSync = VO_OUTPUT_1080P50;
	}
	else
	{
		stVoPubAttr.enIntfSync = VO_OUTPUT_1080P60;
	}

	stVoPubAttr.enIntfType = VO_INTF_HDMI|VO_INTF_VGA;

	stVoPubAttr.u32BgColor = 0x000000ff;
	stVoPubAttr.bDoubleFrame = HI_TRUE;

	s32Ret = SAMPLE_COMM_VO_StartDevLayer(VoDev, &stVoPubAttr, gs_u32ViFrmRate);
	if (HI_SUCCESS != s32Ret)
	{
		SAMPLE_PRT("Start SAMPLE_COMM_VO_StartDevLayer failed!\n");
		goto END_16_2Cif_3;
	}

	s32Ret = SAMPLE_COMM_VO_StartChn(VoDev, &stVoPubAttr, enVoMode);
	if (HI_SUCCESS != s32Ret)
	{
		SAMPLE_PRT("Start SAMPLE_COMM_VO_StartChn failed!\n");
		goto END_16_2Cif_4;
	}

	/* if it's displayed on HDMI, we should start HDMI */
	if (stVoPubAttr.enIntfType & VO_INTF_HDMI)
	{
		if (HI_SUCCESS != SAMPLE_COMM_VO_HdmiStart(stVoPubAttr.enIntfSync))
		{
			SAMPLE_PRT("Start SAMPLE_COMM_VO_HdmiStart failed!\n");
			goto END_16_2Cif_4;
		}
	}

	for(i=0;i<u32WndNum;i++)
	{
		VoChn = i;
		VpssGrp = i;
		
		s32Ret = SAMPLE_COMM_VO_BindVpss(VoDev,VoChn,VpssGrp,VpssChn_VoHD0);
		if (HI_SUCCESS != s32Ret)
		{
			SAMPLE_PRT("Start VO failed!\n");
			goto END_16_2Cif_4;
		}
	}

	/******************************************
	step 7: start vo SD0 (CVBS) (WBC target) 
	******************************************/
	printf("start vo SD0: wbc from hd0\n");
	VoDev = SAMPLE_VO_DEV_DSD0;
	u32WndNum = 1;
	enVoMode = VO_MODE_1MUX;

	stVoPubAttr_SD.enIntfSync = VO_OUTPUT_PAL;
	stVoPubAttr_SD.enIntfType = VO_INTF_CVBS;
	stVoPubAttr_SD.u32BgColor = 0x000000ff;
	stVoPubAttr_SD.bDoubleFrame = HI_FALSE;
	s32Ret = SAMPLE_COMM_VO_StartDevLayer(VoDev, &stVoPubAttr_SD, gs_u32ViFrmRate);
	if (HI_SUCCESS != s32Ret)
	{
		  SAMPLE_PRT("SAMPLE_COMM_VO_StartDevLayer failed!\n");
		  goto END_16_2Cif_4;
	}

	s32Ret = SAMPLE_COMM_VO_StartChn(VoDev, &stVoPubAttr_SD, enVoMode);
	if (HI_SUCCESS != s32Ret)
	{
		  SAMPLE_PRT("SAMPLE_COMM_VO_StartChn failed!\n");
		  goto END_16_2Cif_5;
	}
	  
	s32Ret = SAMPLE_COMM_VO_GetWH(VO_OUTPUT_PAL, \
					  &stWbcAttr.stTargetSize.u32Width, \
					  &stWbcAttr.stTargetSize.u32Height, \
					  &stWbcAttr.u32FrameRate);
	if (HI_SUCCESS != s32Ret)
	{
		SAMPLE_PRT("SAMPLE_COMM_VO_GetWH failed!\n");
		goto END_16_2Cif_5;
	}
	stWbcAttr.enPixelFormat = SAMPLE_PIXEL_FORMAT;

	s32Ret = HI_MPI_VO_SetWbcAttr(SAMPLE_VO_DEV_DHD0, &stWbcAttr);
	if (HI_SUCCESS != s32Ret)
	{
		SAMPLE_PRT("HI_MPI_VO_SetWbcAttr failed!\n");
		goto END_16_2Cif_5;
	}

	s32Ret = HI_MPI_VO_EnableWbc(SAMPLE_VO_DEV_DHD0);
	if (HI_SUCCESS != s32Ret)
	{
		SAMPLE_PRT("HI_MPI_VO_SetWbcAttr failed!\n");
		goto END_16_2Cif_6;
	}

	s32Ret = SAMPLE_COMM_VO_BindVoWbc(SAMPLE_VO_DEV_DHD0, SAMPLE_VO_DEV_DSD0, 0);
	if (HI_SUCCESS != s32Ret)
	{
		SAMPLE_PRT("HI_MPI_VO_SetWbcAttr failed!\n");
		goto END_16_2Cif_6;
	}
   
    /******************************************
         step 9: HD0 switch mode 
    ******************************************/
    VoDev = SAMPLE_VO_DEV_DHD0;
    enVoMode = VO_MODE_16MUX;
    while(1)
    {
  		enPreVoMode = enVoMode;
        printf("please choose preview mode, press 'q' to exit this sample.\n"); 
        printf("\t0) 1 preview\n");
        printf("\t1) 4 preview\n");
        printf("\t2) 9 preview\n");
        printf("\t3) 16 preview\n");
        printf("\tq) quit\n");
 
        ch = getchar();
        getchar();
        if ('0' == ch)
        {
            u32WndNum = 1;
            enVoMode = VO_MODE_1MUX;
        }
        else if ('1' == ch)
        {
            u32WndNum = 4;
            enVoMode = VO_MODE_4MUX;
        }
        else if ('2' == ch)
        {
            u32WndNum = 9;
            enVoMode = VO_MODE_9MUX;
        }
        else if ('3' == ch)
        {
            u32WndNum = 16;
            enVoMode = VO_MODE_16MUX;
        }
        else if ('q' == ch)
        {
            break;
        }
        else
        {
            SAMPLE_PRT("preview mode invaild! please try again.\n");
            continue;
        }
		/* VI Input size should change from 2cif to D1 */
		if(( VO_MODE_1MUX== enVoMode ||VO_MODE_4MUX== enVoMode)&&( VO_MODE_9MUX== enPreVoMode ||VO_MODE_16MUX== enPreVoMode))
		{
		 	stSizeTmp.u32Width = 720;
			stSizeTmp.u32Height = ((VIDEO_ENCODING_MODE_PAL==gs_enNorm)?576:480);
            if(VO_MODE_1MUX == enVoMode)
        	{                
				s32Ret = SAMPLE_COMM_DisableVpssPreScale(0,stSizeTmp);
				if (HI_SUCCESS != s32Ret)
				{
					SAMPLE_PRT("SAMPLE_COMM_DisableVpssPreScale VO failed!\n");
					goto END_16_2Cif_6;
				}
        	}
			else
			{
				for(i=0;i<4;i++)
			 	{
					s32Ret = SAMPLE_COMM_DisableVpssPreScale(i,stSizeTmp);
					if (HI_SUCCESS != s32Ret)
					{
						SAMPLE_PRT("SAMPLE_COMM_DisableVpssPreScale VO failed!\n");
						goto END_16_2Cif_6;
					}
			 	}
			}
		}
		/* VI Input size should change from  D1 to 2cif */
        else if(( VO_MODE_1MUX== enPreVoMode ||VO_MODE_4MUX== enPreVoMode)&&( VO_MODE_9MUX== enVoMode ||VO_MODE_16MUX== enVoMode))
        {
        	stSizeTmp.u32Width= D1_WIDTH / 2;
			stSizeTmp.u32Height = ((VIDEO_ENCODING_MODE_PAL==gs_enNorm)?576:480);
	        if(VO_MODE_9MUX == enVoMode)
        	{
        		for(i=0;i<9;i++)
			 	{
					s32Ret = SAMPLE_COMM_EnableVpssPreScale(i,stSizeTmp);
					if (HI_SUCCESS != s32Ret)
					{
						SAMPLE_PRT("SAMPLE_COMM_DisableVpssPreScale VO failed!\n");
						goto END_16_2Cif_6;
					}
        		}
        	}
			else
			{
				for(i=0;i<16;i++)
			 	{
					s32Ret = SAMPLE_COMM_EnableVpssPreScale(i,stSizeTmp);
					if (HI_SUCCESS != s32Ret)
					{
						SAMPLE_PRT("SAMPLE_COMM_DisableVpssPreScale VO failed!\n");
						goto END_16_2Cif_6;
					}
			 	}
			}
        }
		else if (enVoMode== enPreVoMode)
		{
			continue;
		}
		
        SAMPLE_PRT("vo(%d) switch to %d mode\n", VoDev, u32WndNum);

        s32Ret= HI_MPI_VO_SetAttrBegin(VoDev);
        if (HI_SUCCESS != s32Ret)
        {
            SAMPLE_PRT("Start VO failed!\n");
            goto END_16_2Cif_6;
        }
        
        s32Ret = SAMPLE_COMM_VO_StopChn(VoDev, enPreVoMode);
        if (HI_SUCCESS != s32Ret)
        {
            SAMPLE_PRT("Start VO failed!\n");
            goto END_16_2Cif_6;
        }

        s32Ret = SAMPLE_COMM_VO_StartChn(VoDev, &stVoPubAttr, enVoMode);
        if (HI_SUCCESS != s32Ret)
        {
            SAMPLE_PRT("Start VO failed!\n");
            goto END_16_2Cif_6;
        }
        s32Ret= HI_MPI_VO_SetAttrEnd(VoDev);
        if (HI_SUCCESS != s32Ret)
        {
            SAMPLE_PRT("Start VO failed!\n");
            goto END_16_2Cif_6;
        }
    }

    /******************************************
     step 10: exit process
    ******************************************/

END_16_2Cif_6:
	SAMPLE_COMM_VO_UnBindVoWbc(SAMPLE_VO_DEV_DSD0, 0);
	HI_MPI_VO_DisableWbc(SAMPLE_VO_DEV_DHD0);

END_16_2Cif_5:
	VoDev = SAMPLE_VO_DEV_DSD0;
	VoChn = 0;
	enVoMode = VO_MODE_1MUX;
    SAMPLE_COMM_VO_StopChn(VoDev, enVoMode);
    SAMPLE_COMM_VO_StopDevLayer(VoDev);

END_16_2Cif_4:
	if (stVoPubAttr.enIntfType & VO_INTF_HDMI)
    {
        SAMPLE_COMM_VO_HdmiStop();
    }
    VoDev = SAMPLE_VO_DEV_DHD0;
    u32WndNum = 16;
    enVoMode = VO_MODE_16MUX;
    for(i=0;i<u32WndNum;i++)
    {
        VoChn = i;
        VpssGrp = i;
        SAMPLE_COMM_VO_UnBindVpss(VoDev,VoChn,VpssGrp,VpssChn_VoHD0);
    }
    SAMPLE_COMM_VO_StopChn(VoDev, enVoMode);
    SAMPLE_COMM_VO_StopDevLayer(VoDev);

	
END_16_2Cif_3:
    SAMPLE_COMM_VI_UnBindVpss(enViMode);
END_16_2Cif_2:
    SAMPLE_COMM_VPSS_Stop(s32VpssGrpCnt, VPSS_MAX_CHN_NUM);
END_16_2Cif_1:
    SAMPLE_COMM_VI_Stop(enViMode);
END_16_2Cif_0:
    SAMPLE_COMM_SYS_Exit();
    
    return s32Ret;
}



/******************************************************************************
* function :  VI:1*720p; VO:SD0(CVBS), HD1(HDMI,1080p@60 + VGA), WBC to SD1(CVBS) video preview. 
******************************************************************************/
HI_S32 SAMPLE_VIO_1_720P(HI_VOID)
{
    SAMPLE_VI_MODE_E enViMode = SAMPLE_VI_MODE_1_720P;
    HI_U32 u32ViChnCnt = 1;
    HI_S32 s32VpssGrpCnt = 1;
    
    VB_CONF_S stVbConf;
    VPSS_GRP VpssGrp;
	VPSS_CHN VpssChn_VoHD0 = VPSS_PRE0_CHN;
    VPSS_GRP_ATTR_S stGrpAttr;
    VO_DEV VoDev;
    VO_CHN VoChn;
	VI_CHN ViChn;
    VO_PUB_ATTR_S stVoPubAttr; 
    SAMPLE_VO_MODE_E enVoMode, enPreVoMode;
    
    HI_S32 i;
    HI_S32 s32Ret = HI_SUCCESS;
    HI_U32 u32BlkSize;
    HI_CHAR ch;
    SIZE_S stSize;
    HI_U32 u32WndNum;

    /******************************************
     step  1: init global  variable 
    ******************************************/
    gs_u32ViFrmRate = (VIDEO_ENCODING_MODE_PAL == gs_enNorm)?25:30;
    
    memset(&stVbConf,0,sizeof(VB_CONF_S));

    u32BlkSize = SAMPLE_COMM_SYS_CalcPicVbBlkSize(gs_enNorm,\
                PIC_HD720, SAMPLE_PIXEL_FORMAT, SAMPLE_SYS_ALIGN_WIDTH);
    stVbConf.u32MaxPoolCnt = 128;

    /*ddr0 video buffer*/
    stVbConf.astCommPool[0].u32BlkSize = u32BlkSize;
    stVbConf.astCommPool[0].u32BlkCnt = u32ViChnCnt * 8;


    /******************************************
     step 2: mpp system init. 
    ******************************************/
    s32Ret = SAMPLE_COMM_SYS_Init(&stVbConf);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("system init failed with %d!\n", s32Ret);
        goto END_720P_0;
    }

    /******************************************
     step 3: start vi dev & chn to capture
    ******************************************/
    s32Ret = SAMPLE_COMM_VI_Start(enViMode, gs_enNorm);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("start vi failed!\n");
        goto END_720P_0;
    }
    
    /******************************************
     step 4: start vpss and vi bind vpss (subchn needn't bind vpss in this mode)
    ******************************************/
    s32Ret = SAMPLE_COMM_SYS_GetPicSize(gs_enNorm, PIC_HD720, &stSize);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_SYS_GetPicSize failed!\n");
        goto END_720P_1;
    }

    stGrpAttr.u32MaxW = stSize.u32Width;
    stGrpAttr.u32MaxH = stSize.u32Height;
    stGrpAttr.bDrEn = HI_FALSE;
    stGrpAttr.bDbEn = HI_FALSE;
    stGrpAttr.bIeEn = HI_TRUE;
    stGrpAttr.bNrEn = HI_TRUE;
    stGrpAttr.bHistEn = HI_TRUE;
    stGrpAttr.enDieMode = VPSS_DIE_MODE_AUTO;
    stGrpAttr.enPixFmt = SAMPLE_PIXEL_FORMAT;

    s32Ret = SAMPLE_COMM_VPSS_Start(s32VpssGrpCnt, &stSize, VPSS_MAX_CHN_NUM,NULL);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("Start Vpss failed!\n");
        goto END_720P_1;
    }

    s32Ret = SAMPLE_COMM_VI_BindVpss(enViMode);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("Vi bind Vpss failed!\n");
        goto END_720P_2;
    }

	/******************************************
	step 5: start VO SD0 (bind * vi )
	******************************************/
	printf("start vo sd0 bind vi chn0 \n");
	VoDev = SAMPLE_VO_DEV_DSD0;
	enVoMode = VO_MODE_1MUX;

	stVoPubAttr.enIntfSync = VO_OUTPUT_PAL;
	stVoPubAttr.enIntfType = VO_INTF_CVBS;
	stVoPubAttr.u32BgColor = 0x000000ff;
	stVoPubAttr.bDoubleFrame = HI_FALSE;//In HD, this item should be set to HI_FALSE
	s32Ret = SAMPLE_COMM_VO_StartDevLayer(VoDev, &stVoPubAttr, gs_u32ViFrmRate);
	if (HI_SUCCESS != s32Ret)
	{
	   SAMPLE_PRT("SAMPLE_COMM_VO_StartDevLayer failed!\n");
	   goto END_720P_3;
	}

	s32Ret = SAMPLE_COMM_VO_StartChn(VoDev, &stVoPubAttr, enVoMode);
	if (HI_SUCCESS != s32Ret)
	{
	   SAMPLE_PRT("SAMPLE_COMM_VO_StartChn failed!\n");
	   goto END_720P_4;
	}


	VoChn = 0;
	ViChn = 0; //? is vi chn 0?
	s32Ret = SAMPLE_COMM_VO_BindVi(VoDev, VoChn, ViChn);
	if (HI_SUCCESS != s32Ret)
	{
	   SAMPLE_PRT("SAMPLE_COMM_VO_BindVi(vo:%d)-(vichn:%d) failed with %#x!\n", VoDev, VoChn, s32Ret);
	   goto END_720P_4;
	}

	/******************************************
	 step 6: start vo HD0 (HDMI), muti-screen, you can switch mode
	******************************************/
	printf("start vo HD0.\n");
	VoDev = SAMPLE_VO_DEV_DHD0;
	u32WndNum = 4;
	enVoMode = VO_MODE_1MUX;

	if(VIDEO_ENCODING_MODE_PAL == gs_enNorm)
	{
		stVoPubAttr.enIntfSync = VO_OUTPUT_1080P50;
	}
	else
	{
		stVoPubAttr.enIntfSync = VO_OUTPUT_1080P60;
	}

	stVoPubAttr.enIntfType = VO_INTF_HDMI|VO_INTF_VGA;

	stVoPubAttr.u32BgColor = 0x000000ff;
	stVoPubAttr.bDoubleFrame = HI_TRUE;
	
	s32Ret = SAMPLE_COMM_VO_StartDevLayer(VoDev, &stVoPubAttr, gs_u32ViFrmRate);
	if (HI_SUCCESS != s32Ret)
	{
		SAMPLE_PRT("Start SAMPLE_COMM_VO_StartDevLayer failed!\n");
		goto END_720P_4;
	}
	
	s32Ret = SAMPLE_COMM_VO_StartChn(VoDev, &stVoPubAttr, enVoMode);
	if (HI_SUCCESS != s32Ret)
	{
		SAMPLE_PRT("Start SAMPLE_COMM_VO_StartChn failed!\n");
		goto END_720P_5;
	}

	/* if it's displayed on HDMI, we should start HDMI */
	if (stVoPubAttr.enIntfType & VO_INTF_HDMI)
	{
		if (HI_SUCCESS != SAMPLE_COMM_VO_HdmiStart(stVoPubAttr.enIntfSync))
		{
			SAMPLE_PRT("Start SAMPLE_COMM_VO_HdmiStart failed!\n");
			goto END_720P_5;
		}
	}
	
	for(i=0;i<u32WndNum;i++)
	{
		VoChn = i;
		VpssGrp = i;
		
		s32Ret = SAMPLE_COMM_VO_BindVpss(VoDev,VoChn,VpssGrp,VpssChn_VoHD0);
		if (HI_SUCCESS != s32Ret)
		{
			SAMPLE_PRT("Start VO failed!\n");
			goto END_720P_5;
		}
	}

    /******************************************
     step 7: HD1 switch mode 
    ******************************************/
	VoDev = SAMPLE_VO_DEV_DHD0;
    u32WndNum = 4;
    enVoMode = VO_MODE_1MUX;
    while(1)
    {
        enPreVoMode = enVoMode;
    
        printf("please choose preview mode, press 'q' to exit this sample.\n"); 
        printf("\t0) 1 preview\n");
        printf("\t1) 4 preview\n");
        printf("\tq) quit\n");
 
        ch = getchar();
        getchar();
        if ('0' == ch)
        {
            u32WndNum = 1;
            enVoMode = VO_MODE_1MUX;
        }
        else if ('1' == ch)
        {
            u32WndNum = 4;
            enVoMode = VO_MODE_4MUX;
        }

        else if ('q' == ch)
        {
            break;
        }
        else
        {
            SAMPLE_PRT("preview mode invaild! please try again.\n");
            continue;
        }
        SAMPLE_PRT("vo(%d) switch to %d mode\n", VoDev, u32WndNum);

        s32Ret= HI_MPI_VO_SetAttrBegin(VoDev);
        if (HI_SUCCESS != s32Ret)
        {
            SAMPLE_PRT("Start VO failed!\n");
            goto END_720P_5;
        }
        
        s32Ret = SAMPLE_COMM_VO_StopChn(VoDev, enPreVoMode);
        if (HI_SUCCESS != s32Ret)
        {
            SAMPLE_PRT("Start VO failed!\n");
            goto END_720P_5;
        }

        s32Ret = SAMPLE_COMM_VO_StartChn(VoDev, &stVoPubAttr, enVoMode);
        if (HI_SUCCESS != s32Ret)
        {
            SAMPLE_PRT("Start VO failed!\n");
            goto END_720P_5;
        }
        s32Ret= HI_MPI_VO_SetAttrEnd(VoDev);
        if (HI_SUCCESS != s32Ret)
        {
            SAMPLE_PRT("Start VO failed!\n");
            goto END_720P_5;
        }
    }

    /******************************************
     step 8: exit process
    ******************************************/
   
END_720P_5:	//stop vo hd0

    VoDev = SAMPLE_VO_DEV_DHD0;
    u32WndNum = 4;
    enVoMode = VO_MODE_4MUX;
    for(i=0;i<u32WndNum;i++)
    {
        VoChn = i;
        VpssGrp = i;
        SAMPLE_COMM_VO_UnBindVpss(VoDev,VoChn,VpssGrp,VPSS_PRE0_CHN);
    }
    if (stVoPubAttr.enIntfType & VO_INTF_HDMI)
    {
        SAMPLE_COMM_VO_HdmiStop();
    }
    SAMPLE_COMM_VO_StopChn(VoDev, enVoMode);
    SAMPLE_COMM_VO_StopDevLayer(VoDev);
 
END_720P_4:	//stop vo sd0
    VoDev = SAMPLE_VO_DEV_DSD0;  
    enVoMode = VO_MODE_1MUX;
    VoChn = 0;
    SAMPLE_COMM_VO_UnBindVi(VoDev,VoChn);
    SAMPLE_COMM_VO_StopChn(VoDev, enVoMode);
    SAMPLE_COMM_VO_StopDevLayer(VoDev);

END_720P_3:
    SAMPLE_COMM_VI_UnBindVpss(enViMode);
END_720P_2:
    SAMPLE_COMM_VPSS_Stop(s32VpssGrpCnt, VPSS_MAX_CHN_NUM);
END_720P_1:
    SAMPLE_COMM_VI_Stop(enViMode);
END_720P_0:
    SAMPLE_COMM_SYS_Exit();
    
    return s32Ret;
}


/******************************************************************************
* function :  VI:16*D1(6fps)/2cif(19fps); VO:HD0(HDMI  720P50)video preview. 
******************************************************************************/
HI_S32 SAMPLE_VIO_16_D1Cif(HI_VOID)
{
    SAMPLE_VI_MODE_E enViMode = SAMPLE_VI_MODE_16_D1Cif;
    HI_U32 u32ViChnCnt = 16;
    HI_S32 s32VpssGrpCnt = 16;
    
    VB_CONF_S stVbConf;
    VPSS_GRP VpssGrp;
    VPSS_GRP_ATTR_S stGrpAttr;
	VPSS_CHN VpssChn_VoHD0 = VPSS_PRE0_CHN;
	 
    VO_DEV VoDev;
    VO_CHN VoChn;
    VO_PUB_ATTR_S stVoPubAttr; 

    SAMPLE_VO_MODE_E enVoMode, enPreVoMode;
	 
    HI_S32 i;
    HI_S32 s32Ret = HI_SUCCESS;
    HI_U32 u32BlkSize;
    HI_CHAR ch;
    SIZE_S stSize,stSizeTmp;
    HI_U32 u32WndNum;

#if HICHIP == HI3520A_V100
    printf("Hi3520A chip don't support this sample.\n");
    return HI_SUCCESS;
#endif
    /******************************************
     step  1: init global  variable 
    ******************************************/
    gs_u32ViFrmRate = (VIDEO_ENCODING_MODE_PAL== gs_enNorm)?25:30;
    
    memset(&stVbConf,0,sizeof(VB_CONF_S));

    u32BlkSize = SAMPLE_COMM_SYS_CalcPicVbBlkSize(gs_enNorm,\
                PIC_D1, SAMPLE_PIXEL_FORMAT, SAMPLE_SYS_ALIGN_WIDTH);
    stVbConf.u32MaxPoolCnt = 128;
    stVbConf.astCommPool[0].u32BlkSize = u32BlkSize;
    stVbConf.astCommPool[0].u32BlkCnt = u32ViChnCnt * 5;
	
    u32BlkSize = SAMPLE_COMM_SYS_CalcPicVbBlkSize(gs_enNorm,\
                PIC_2CIF, SAMPLE_PIXEL_FORMAT, SAMPLE_SYS_ALIGN_WIDTH);
    stVbConf.astCommPool[1].u32BlkSize = u32BlkSize;
    stVbConf.astCommPool[1].u32BlkCnt = u32ViChnCnt * 10;

    /******************************************
     step 2: mpp system init. 
    ******************************************/
    s32Ret = SAMPLE_COMM_SYS_Init(&stVbConf);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("system init failed with %d!\n", s32Ret);
        goto END_16_MixCap_0;
    }


    /******************************************
     step 3: start vi dev & chn to capture
    ******************************************/
    s32Ret = SAMPLE_COMM_VI_MixCap_Start(enViMode, gs_enNorm);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("start vi failed!\n");
        goto END_16_MixCap_0;
    }
    
    /******************************************
     step 4: start vpss and vi bind vpss
    ******************************************/
    s32Ret = SAMPLE_COMM_SYS_GetPicSize(gs_enNorm, PIC_D1, &stSize);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_SYS_GetPicSize failed!\n");
        goto END_16_MixCap_1;
    }
    
    stGrpAttr.u32MaxW = 720;
    stGrpAttr.u32MaxH = (VIDEO_ENCODING_MODE_PAL==gs_enNorm)?576:480;
    stGrpAttr.bDrEn = HI_FALSE;
    stGrpAttr.bDbEn = HI_FALSE;
    stGrpAttr.bIeEn = HI_TRUE;
    stGrpAttr.bNrEn = HI_TRUE;
    stGrpAttr.bHistEn = HI_TRUE;
    stGrpAttr.enDieMode = VPSS_DIE_MODE_AUTO;
    stGrpAttr.enPixFmt = SAMPLE_PIXEL_FORMAT;

    s32Ret = SAMPLE_COMM_VPSS_Start(s32VpssGrpCnt, &stSize, VPSS_MAX_CHN_NUM,NULL);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("Start Vpss failed!\n");
        goto END_16_MixCap_1;
    }
    /*open pre-scale*/
	s32Ret = SAMPLE_COMM_SYS_GetPicSize(gs_enNorm, PIC_2CIF, &stSize);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_SYS_GetPicSize failed!\n");
        goto END_16_MixCap_1;
    }
    for(i=0;i<s32VpssGrpCnt;i++)
    {   
		s32Ret = SAMPLE_COMM_EnableVpssPreScale(i,stSize);
		if(HI_SUCCESS != s32Ret)
		{
        SAMPLE_PRT("HI_MPI_VPSS_SetPreScale failed!\n");
        goto END_16_MixCap_1;
        }
    }
		
    s32Ret = SAMPLE_COMM_VI_BindVpss(enViMode);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("Vi bind Vpss failed!\n");
        goto END_16_MixCap_2;
    }

    /******************************************
     step 5: start vo HD0(HDMI) 
    ******************************************/
	printf("start vo HD0.\n");
	VoDev = SAMPLE_VO_DEV_DHD0;
	u32WndNum = 16;
	enVoMode = VO_MODE_16MUX;

	if(VIDEO_ENCODING_MODE_PAL == gs_enNorm)
	{
		stVoPubAttr.enIntfSync = VO_OUTPUT_1080P50;
	}
	else
	{
		stVoPubAttr.enIntfSync = VO_OUTPUT_1080P60;
	}

	stVoPubAttr.enIntfType = VO_INTF_HDMI|VO_INTF_VGA;

	stVoPubAttr.u32BgColor = 0x000000ff;
	stVoPubAttr.bDoubleFrame = HI_TRUE;

	s32Ret = SAMPLE_COMM_VO_StartDevLayer(VoDev, &stVoPubAttr, gs_u32ViFrmRate);
	if (HI_SUCCESS != s32Ret)
	{
		SAMPLE_PRT("Start SAMPLE_COMM_VO_StartDevLayer failed!\n");
		goto END_16_MixCap_3;
	}

	s32Ret = SAMPLE_COMM_VO_StartChn(VoDev, &stVoPubAttr, enVoMode);
	if (HI_SUCCESS != s32Ret)
	{
		SAMPLE_PRT("Start SAMPLE_COMM_VO_StartChn failed!\n");
		goto END_16_MixCap_4;
	}

	/* if it's displayed on HDMI, we should start HDMI */
	if (stVoPubAttr.enIntfType & VO_INTF_HDMI)
	{
		if (HI_SUCCESS != SAMPLE_COMM_VO_HdmiStart(stVoPubAttr.enIntfSync))
		{
			SAMPLE_PRT("Start SAMPLE_COMM_VO_HdmiStart failed!\n");
			goto END_16_MixCap_4;
		}
	}

	for(i=0;i<u32WndNum;i++)
	{
		VoChn = i;
		VpssGrp = i;
		
		s32Ret = SAMPLE_COMM_VO_BindVpss(VoDev,VoChn,VpssGrp,VpssChn_VoHD0);
		if (HI_SUCCESS != s32Ret)
		{
			SAMPLE_PRT("Start VO failed!\n");
			goto END_16_MixCap_4;
		}
	}
 
    /******************************************
         step 9: HD0 switch mode 
    ******************************************/
    VoDev = SAMPLE_VO_DEV_DHD0;
    enVoMode = VO_MODE_16MUX;
    while(1)
    {
  		enPreVoMode = enVoMode;
        printf("please choose preview mode, press 'q' to exit this sample.\n"); 
        printf("\t0) 1 preview\n");
        printf("\t1) 4 preview\n");
        printf("\t2) 9 preview\n");
        printf("\t3) 16 preview\n");
        printf("\tq) quit\n");
 
        ch = getchar();
        getchar();
        if ('0' == ch)
        {
            u32WndNum = 1;
            enVoMode = VO_MODE_1MUX;
        }
        else if ('1' == ch)
        {
            u32WndNum = 4;
            enVoMode = VO_MODE_4MUX;
        }
        else if ('2' == ch)
        {
            u32WndNum = 9;
            enVoMode = VO_MODE_9MUX;
        }
        else if ('3' == ch)
        {
            u32WndNum = 16;
            enVoMode = VO_MODE_16MUX;
        }
        else if ('q' == ch)
        {
            break;
        }
        else
        {
            SAMPLE_PRT("preview mode invaild! please try again.\n");
            continue;
        }
		/* VI Chn size should change from D1/2cif to D1 */
		if((VO_MODE_1MUX== enVoMode) || (VO_MODE_4MUX== enVoMode))
		{
		 	stSizeTmp.u32Width = 720;
			stSizeTmp.u32Height = ((VIDEO_ENCODING_MODE_PAL==gs_enNorm)?576:480);
            if(VO_MODE_1MUX == enVoMode)
        	{
				s32Ret = SAMPLE_COMM_VI_ChangeMixCap(0,HI_FALSE,gs_u32ViFrmRate);
				if (HI_SUCCESS != s32Ret)
				{
					SAMPLE_PRT("SAMPLE_COMM_VI_ChangeCapSize VO failed!\n");
					goto END_16_MixCap_4;
				}

				s32Ret = SAMPLE_COMM_DisableVpssPreScale(0,stSizeTmp);
				if (HI_SUCCESS != s32Ret)
				{
					SAMPLE_PRT("SAMPLE_COMM_DisableVpssPreScale VO failed!\n");
					goto END_16_MixCap_4;
				}
        	}
			else
			{
				for(i=0;i<4;i++)
			 	{
					s32Ret = SAMPLE_COMM_VI_ChangeMixCap(i,HI_FALSE,gs_u32ViFrmRate);
					if (HI_SUCCESS != s32Ret)
					{
						SAMPLE_PRT("SAMPLE_COMM_VI_ChangeCapSize VO failed!\n");
						goto END_16_MixCap_4;
					}

					s32Ret = SAMPLE_COMM_DisableVpssPreScale(i,stSizeTmp);
					if (HI_SUCCESS != s32Ret)
					{
						SAMPLE_PRT("SAMPLE_COMM_DisableVpssPreScale VO failed!\n");
						goto END_16_MixCap_4;
					}
			 	}
			}
		}
		/* VI Chn size should change from  D1 to D1/2cif */
        else if((VO_MODE_9MUX== enVoMode) || (VO_MODE_16MUX== enVoMode))
        {
        	stSizeTmp.u32Width= D1_WIDTH / 2;
			stSizeTmp.u32Height = ((VIDEO_ENCODING_MODE_PAL==gs_enNorm)?576:480);
	        if(VO_MODE_9MUX == enVoMode)
        	{
        		for(i=0;i<9;i++)
			 	{
					s32Ret = SAMPLE_COMM_VI_ChangeMixCap(i,HI_TRUE,6);
					if (HI_SUCCESS != s32Ret)
					{
						SAMPLE_PRT("SAMPLE_COMM_VI_ChangeCapSize VO failed!\n");
						goto END_16_MixCap_4;
					}

					s32Ret = SAMPLE_COMM_EnableVpssPreScale(i,stSizeTmp);
					if (HI_SUCCESS != s32Ret)
					{
						SAMPLE_PRT("SAMPLE_COMM_DisableVpssPreScale VO failed!\n");
						goto END_16_MixCap_4;
					}
        		}
        	}
			else
			{
				for(i=0;i<16;i++)
			 	{
					s32Ret = SAMPLE_COMM_VI_ChangeMixCap(i,HI_TRUE,6);
					if (HI_SUCCESS != s32Ret)
					{
						SAMPLE_PRT("SAMPLE_COMM_VI_ChangeCapSize VO failed!\n");
						goto END_16_MixCap_4;
					}

					s32Ret = SAMPLE_COMM_EnableVpssPreScale(i,stSizeTmp);
					if (HI_SUCCESS != s32Ret)
					{
						SAMPLE_PRT("SAMPLE_COMM_DisableVpssPreScale VO failed!\n");
						goto END_16_MixCap_4;
					}
			 	}
			}
        }
		else if (enVoMode== enPreVoMode)
		{
			continue;
		}
		
        SAMPLE_PRT("vo(%d) switch to %d mode\n", VoDev, u32WndNum);

        s32Ret= HI_MPI_VO_SetAttrBegin(VoDev);
        if (HI_SUCCESS != s32Ret)
        {
            SAMPLE_PRT("Start VO failed!\n");
            goto END_16_MixCap_4;
        }
        
        s32Ret = SAMPLE_COMM_VO_StopChn(VoDev, enPreVoMode);
        if (HI_SUCCESS != s32Ret)
        {
            SAMPLE_PRT("Start VO failed!\n");
            goto END_16_MixCap_4;
        }

        s32Ret = SAMPLE_COMM_VO_StartChn(VoDev, &stVoPubAttr, enVoMode);
        if (HI_SUCCESS != s32Ret)
        {
            SAMPLE_PRT("Start VO failed!\n");
            goto END_16_MixCap_4;
        }
        s32Ret= HI_MPI_VO_SetAttrEnd(VoDev);
        if (HI_SUCCESS != s32Ret)
        {
            SAMPLE_PRT("Start VO failed!\n");
            goto END_16_MixCap_4;
        }
    }

    /******************************************
     step 10: exit process
    ******************************************/

END_16_MixCap_4:
	if (stVoPubAttr.enIntfType & VO_INTF_HDMI)
    {
        SAMPLE_COMM_VO_HdmiStop();
    }
    VoDev = SAMPLE_VO_DEV_DHD0;
    u32WndNum = 16;
    enVoMode = VO_MODE_16MUX;
    for(i=0;i<u32WndNum;i++)
    {
        VoChn = i;
        VpssGrp = i;
        SAMPLE_COMM_VO_UnBindVpss(VoDev,VoChn,VpssGrp,VpssChn_VoHD0);
    }
    SAMPLE_COMM_VO_StopChn(VoDev, enVoMode);
    SAMPLE_COMM_VO_StopDevLayer(VoDev);

	
END_16_MixCap_3:
    SAMPLE_COMM_VI_UnBindVpss(enViMode);
END_16_MixCap_2:
    SAMPLE_COMM_VPSS_Stop(s32VpssGrpCnt, VPSS_MAX_CHN_NUM);
END_16_MixCap_1:
    SAMPLE_COMM_VI_Stop(enViMode);
END_16_MixCap_0:
    SAMPLE_COMM_SYS_Exit();
    
    return s32Ret;
}

/******************************************************************************
* function :  VI: 1*D1, user picture; VO: SD0(CVBS) video preview
******************************************************************************/
HI_S32 SAMPLE_VIO_1D1_LossDet(HI_VOID)
{
    SAMPLE_VI_MODE_E enViMode = SAMPLE_VI_MODE_1_D1;
    HI_U32 u32ViChnCnt = 8;
    
    VB_CONF_S stVbConf;
    VI_CHN ViChn;
    VO_DEV VoDev;
    VO_CHN VoChn;
    VO_PUB_ATTR_S stVoPubAttr; 
    SAMPLE_VO_MODE_E enVoMode;
    
    HI_S32 i;
    HI_S32 s32Ret = HI_SUCCESS;
    HI_U32 u32BlkSize;
    HI_U32 u32WndNum;

    VIDEO_FRAME_INFO_S stUserFrame;

    /******************************************
     step  1: init global  variable 
    ******************************************/
    gs_u32ViFrmRate = (VIDEO_ENCODING_MODE_PAL== gs_enNorm)?25:30;
    
    memset(&stVbConf,0,sizeof(VB_CONF_S));

    u32BlkSize = SAMPLE_COMM_SYS_CalcPicVbBlkSize(gs_enNorm,\
                PIC_D1, SAMPLE_PIXEL_FORMAT, SAMPLE_SYS_ALIGN_WIDTH);
    stVbConf.u32MaxPoolCnt = 128;

    stVbConf.astCommPool[0].u32BlkSize = u32BlkSize;
    stVbConf.astCommPool[0].u32BlkCnt = u32ViChnCnt * 6;


    /******************************************
     step 2: mpp system init. 
    ******************************************/
    s32Ret = SAMPLE_COMM_SYS_Init(&stVbConf);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("system init failed with %d!\n", s32Ret);
        goto END_1D1_LOSS_0;
    }

    /******************************************
     step 3: start vi dev & chn to capture
    ******************************************/
    s32Ret = SAMPLE_COMM_VI_Start(enViMode, gs_enNorm);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("start vi failed!\n");
        goto END_1D1_LOSS_0;
    }
    
    /******************************************
     step 4: start VO to preview
    ******************************************/
    VoDev = SAMPLE_VO_DEV_DSD0;
    u32WndNum = 1;
    enVoMode = VO_MODE_1MUX;
    
    stVoPubAttr.enIntfSync = VO_OUTPUT_PAL;
    stVoPubAttr.enIntfType = VO_INTF_CVBS;
    stVoPubAttr.u32BgColor = 0x000000ff;
    stVoPubAttr.bDoubleFrame = HI_FALSE;

    s32Ret = SAMPLE_COMM_VO_StartDevLayer(VoDev, &stVoPubAttr, gs_u32ViFrmRate);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_VO_StartDevLayer failed!\n");
        goto END_1D1_LOSS_1;
    }
    
    s32Ret = SAMPLE_COMM_VO_StartChn(VoDev, &stVoPubAttr, enVoMode);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_VO_StartChn failed!\n");
        goto END_1D1_LOSS_2;
    }

    for(i=0;i<u32WndNum;i++)
    {
        VoChn = i;
        ViChn = i;
        s32Ret = SAMPLE_COMM_VO_BindVi(VoDev,VoChn, ViChn);
        if (HI_SUCCESS != s32Ret)
        {
            SAMPLE_PRT("SAMPLE_COMM_VO_BindVpss failed!\n");
            goto END_1D1_LOSS_2;
        }
    }

    /******************************************
     step 5: set novideo pic
    ******************************************/
    /* set novideo pic */
    s32Ret = SAMPLE_VI_SetUserPic("pic_704_576_p420_novideo01.yuv", 704, 576, 704, &stUserFrame);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_VI_SetUserPic failed!\n");
        goto END_1D1_LOSS_2;
    }
    
    /******************************************
     step 6: start video loss detect, if loss, then use user-pic
    ******************************************/
    s32Ret = SAMPLE_VI_StartVLossDet(enViMode);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("SAMPLE_VI_StartVLossDet failed!\n");
        goto END_1D1_LOSS_2;
    }

    printf("\npress twice Enter to stop sample ... ... \n");
    getchar();
    getchar();
    
    /******************************************
     step 8: exit process
    ******************************************/
    SAMPLE_VI_StopVLossDet();
    
END_1D1_LOSS_2:
    for(i=0;i<u32WndNum;i++)
    {
        VoChn = i;
        SAMPLE_COMM_VO_UnBindVi(VoDev,VoChn);
    }
    SAMPLE_COMM_VO_StopChn(VoDev, enVoMode);
    SAMPLE_COMM_VO_StopDevLayer(VoDev);
END_1D1_LOSS_1:
    SAMPLE_COMM_VI_Stop(enViMode);
END_1D1_LOSS_0:
    SAMPLE_COMM_SYS_Exit();
    
    return s32Ret;
}

/******************************************************************************
* function :  VI: 1*D1; VO:HD0(HDMI,1080p@60) HD Zoom preview. 
*                vi ---> vpss grp 0       ---> vo HD0, chn0 (D1, PIP, HIDE)
*                   ---> vpss grp 1(clip) ---> vo HD0, chn1 (1080p)
******************************************************************************/
HI_S32 SAMPLE_VIO_1D1_HDZoom()
{
    SAMPLE_VI_MODE_E enViMode = SAMPLE_VI_MODE_1_D1;
    HI_U32 u32ViChnCnt = 1;
    HI_S32 s32VpssGrpCnt = 1;
    VPSS_GRP VpssGrp = 0;
    VPSS_GRP VpssGrp_Clip = 1;
    VO_DEV VoDev = SAMPLE_VO_DEV_DHD0;
    VO_CHN VoChn = 0;
    VO_CHN VoChn_Clip = 1;
    
    VB_CONF_S stVbConf;
    VPSS_GRP_ATTR_S stGrpAttr;
    VO_PUB_ATTR_S stVoPubAttr; 
    SAMPLE_VO_MODE_E enVoMode;
    MPP_CHN_S stSrcChn;
    MPP_CHN_S stDestChn;
    VPSS_CLIP_INFO_S stVpssClipInfo;
    VO_VIDEO_LAYER_ATTR_S stPipLayerAttr;
    VO_CHN_ATTR_S stChnAttr;
    SIZE_S stSize;
    
    HI_S32 i;
    HI_S32 s32Ret = HI_SUCCESS;
    HI_U32 u32BlkSize;
    HI_CHAR ch;
    HI_U32 u32WndNum;


    /******************************************
     step  1: init global  variable 
    ******************************************/
    gs_u32ViFrmRate = (VIDEO_ENCODING_MODE_PAL == gs_enNorm)?25:30;
    
    memset(&stVbConf,0,sizeof(VB_CONF_S));

    u32BlkSize = SAMPLE_COMM_SYS_CalcPicVbBlkSize(gs_enNorm,\
                PIC_D1, SAMPLE_PIXEL_FORMAT, SAMPLE_SYS_ALIGN_WIDTH);
    stVbConf.u32MaxPoolCnt = 128;
    /*ddr0 video buffer*/
    stVbConf.astCommPool[0].u32BlkSize = u32BlkSize;
    stVbConf.astCommPool[0].u32BlkCnt = u32ViChnCnt * 6;

    /******************************************
     step 2: mpp system init. 
    ******************************************/
    s32Ret = SAMPLE_COMM_SYS_Init(&stVbConf);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("system init failed with %d!\n", s32Ret);
        goto END_1D1_CLIP_0;
    }

   
    /******************************************
     step 3: start vi dev & chn to capture
    ******************************************/
    s32Ret = SAMPLE_COMM_VI_Start(enViMode, gs_enNorm);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("start vi failed!\n");
        goto END_1D1_CLIP_0;
    }

    /******************************************
     step 4: start vpss and vi bind vpss
    ******************************************/
    s32Ret = SAMPLE_COMM_SYS_GetPicSize(gs_enNorm, PIC_D1, &stSize);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_SYS_GetPicSize failed!\n");
        goto END_1D1_CLIP_0;
    }

    stGrpAttr.u32MaxW = stSize.u32Width;
    stGrpAttr.u32MaxH = stSize.u32Height;
    stGrpAttr.bDrEn = HI_FALSE;
    stGrpAttr.bDbEn = HI_FALSE;
    stGrpAttr.bIeEn = HI_TRUE;
    stGrpAttr.bNrEn = HI_TRUE;
    stGrpAttr.bHistEn = HI_TRUE;
    stGrpAttr.enDieMode = VPSS_DIE_MODE_AUTO;
    stGrpAttr.enPixFmt = SAMPLE_PIXEL_FORMAT;

    // start 2 vpss group. group-1 for clip use.
    s32Ret = SAMPLE_COMM_VPSS_Start(s32VpssGrpCnt+1, &stSize, VPSS_MAX_CHN_NUM,NULL);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("Start Vpss failed!\n");
        goto END_1D1_CLIP_1;
    }

    // bind vi to vpss group 0
    s32Ret = SAMPLE_COMM_VI_BindVpss(enViMode);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("Vi bind Vpss failed!\n");
        goto END_1D1_CLIP_2;
    }

    // bind vi(0,0) to vpss group 1, for clip use.
    stSrcChn.enModId = HI_ID_VIU;
    stSrcChn.s32DevId = 0;
    stSrcChn.s32ChnId = 0;

    stDestChn.enModId = HI_ID_VPSS;
    stDestChn.s32DevId = VpssGrp_Clip; // vpss group for clip
    stDestChn.s32ChnId = 0;

    s32Ret = HI_MPI_SYS_Bind(&stSrcChn, &stDestChn);
    if (s32Ret != HI_SUCCESS)
    {
        SAMPLE_PRT("bind vi(0,0) to vpss group 1 failed with %#x!\n", s32Ret);
        goto END_1D1_CLIP_2;
    }
    
    /******************************************
     step 5: start VO to preview
    ******************************************/
    printf("start vo hd0\n");
    u32WndNum = 1;
    enVoMode = VO_MODE_1MUX;
    
    if(VIDEO_ENCODING_MODE_PAL == gs_enNorm)
    {
        stVoPubAttr.enIntfSync = VO_OUTPUT_1080P50;
    }
    else
    {
        stVoPubAttr.enIntfSync = VO_OUTPUT_1080P60;
    }
    
    stVoPubAttr.enIntfType = VO_INTF_VGA;
    stVoPubAttr.u32BgColor = 0x000000ff;
    stVoPubAttr.bDoubleFrame = HI_TRUE;

    s32Ret = SAMPLE_COMM_VO_StartDevLayer(VoDev, &stVoPubAttr, gs_u32ViFrmRate);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_VO_StartDevLayer failed!\n");
        goto END_1D1_CLIP_3;
    }
    
    s32Ret = SAMPLE_COMM_VO_StartChn(VoDev, &stVoPubAttr, enVoMode);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_VO_StartChn failed!\n");
        goto END_1D1_CLIP_4;
    }
    /* if it's displayed on HDMI, we should start HDMI */
    if (stVoPubAttr.enIntfType & VO_INTF_HDMI)
    {
        if (HI_SUCCESS != SAMPLE_COMM_VO_HdmiStart(stVoPubAttr.enIntfSync))
        {
            SAMPLE_PRT("Start SAMPLE_COMM_VO_HdmiStart failed!\n");
            goto END_1D1_CLIP_4;
        }
    }
    s32Ret = SAMPLE_COMM_VO_BindVpss(VoDev,VoChn,VpssGrp,VPSS_PRE0_CHN);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_VO_BindVpss failed!\n");
        goto END_1D1_CLIP_4;
    }
    
    /******************************************
     step 6: Clip process
    ******************************************/
    printf("press any key to show hd zoom.\n");
    getchar();

    /*** enable vpss group clip ***/
    stVpssClipInfo.bEnable = HI_TRUE;
    stVpssClipInfo.enClipCoordinate = VPSS_CLIP_ABS_COOR;
    stVpssClipInfo.stClipRect.s32X = 0;
    stVpssClipInfo.stClipRect.s32Y = 0;
    stVpssClipInfo.stClipRect.u32Height = 400;
    stVpssClipInfo.stClipRect.u32Width = 400;

    s32Ret = HI_MPI_VPSS_SetClipCfg(VpssGrp_Clip, &stVpssClipInfo);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_VO_StartChn failed with %#x!\n", s32Ret);
        goto END_1D1_CLIP_4;
    }
    
    /*** enable vo pip layer ***/
    s32Ret = HI_MPI_VO_PipLayerBindDev(VoDev);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("HI_MPI_VO_PipLayerBindDev failed with %#x!\n", s32Ret);
        goto END_1D1_CLIP_4;
    }

    s32Ret = SAMPLE_COMM_VO_GetWH(stVoPubAttr.enIntfSync, \
    	               &stPipLayerAttr.stDispRect.u32Width, \
    	               &stPipLayerAttr.stDispRect.u32Height, \
    	               &stPipLayerAttr.u32DispFrmRt);
    if (s32Ret != HI_SUCCESS)
    {
        SAMPLE_PRT("failed with %#x!\n", s32Ret);
        goto END_1D1_CLIP_5;
    }
    
    stPipLayerAttr.stDispRect.s32X = 0;
    stPipLayerAttr.stDispRect.s32Y = 0;
    stPipLayerAttr.stImageSize.u32Height = stPipLayerAttr.stDispRect.u32Height;
    stPipLayerAttr.stImageSize.u32Width = stPipLayerAttr.stDispRect.u32Width;
    stPipLayerAttr.enPixFormat = SAMPLE_PIXEL_FORMAT;
    s32Ret = HI_MPI_VO_SetPipLayerAttr(&stPipLayerAttr);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("HI_MPI_VO_SetPipLayerAttr failed with %#x!\n", s32Ret);
        goto END_1D1_CLIP_6;
    }
    
    s32Ret = HI_MPI_VO_EnablePipLayer();
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("HI_MPI_VO_EnablePipLayer failed with %#x!\n", s32Ret);
        goto END_1D1_CLIP_6;
    }
    
    /*** vo chn 0: normal ->pip & 1080p -> D1 ***/
    s32Ret = SAMPLE_COMM_SYS_GetPicSize(gs_enNorm, PIC_D1, &stSize);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_SYS_GetPicSize failed!\n");
        goto END_1D1_CLIP_4;
    }
    stChnAttr.stRect.u32Width = ALIGN_BACK(stSize.u32Width, 4);
    stChnAttr.stRect.u32Height= ALIGN_BACK(stSize.u32Height, 4);
    stChnAttr.stRect.s32X       = ALIGN_BACK(1920-stSize.u32Width, 4);//
    stChnAttr.stRect.s32Y       = ALIGN_BACK(1080-stSize.u32Height, 4);//
    stChnAttr.u32Priority        = 1;
    stChnAttr.bDeflicker         = HI_FALSE;

    s32Ret = HI_MPI_VO_SetChnAttr(VoDev, VoChn, &stChnAttr);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("HI_MPI_VO_SetChnAttr failed with %#x!\n", s32Ret);
        goto END_1D1_CLIP_4;
    }
    
    /***start new vo Chn(0) for clip.  ***/
    s32Ret = SAMPLE_COMM_SYS_GetPicSize(gs_enNorm, PIC_HD1080, &stSize);//
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_SYS_GetPicSize failed!\n");
        goto END_1D1_CLIP_4;
    }
    
    stChnAttr.stRect.u32Width = ALIGN_BACK(stSize.u32Width, 4);
    stChnAttr.stRect.u32Height= ALIGN_BACK(stSize.u32Height, 4);
    stChnAttr.stRect.s32X       = ALIGN_BACK(0, 4);
    stChnAttr.stRect.s32Y       = ALIGN_BACK(0, 4);
    stChnAttr.u32Priority        = 0;
    stChnAttr.bDeflicker         = HI_FALSE;
    s32Ret = HI_MPI_VO_SetChnAttr(VoDev, VoChn_Clip, &stChnAttr);
    if (s32Ret != HI_SUCCESS)
    {
        SAMPLE_PRT("failed with %#x!\n",  s32Ret);
        goto END_1D1_CLIP_4;
    }
    s32Ret = HI_MPI_VO_EnableChn(VoDev, VoChn_Clip);
    if (s32Ret != HI_SUCCESS)
    {
        SAMPLE_PRT("failed with %#x!\n", s32Ret);
        goto END_1D1_CLIP_4;
    }
    s32Ret = SAMPLE_COMM_VO_BindVpss(VoDev, VoChn_Clip, VpssGrp_Clip, VPSS_PRE0_CHN);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_VO_BindVpss failed!\n");
        goto END_1D1_CLIP_4;
    }
    
    printf("\npress 'q' to stop sample ... ... \n");
    while('q' != (ch = getchar()) )  {}
    
    /******************************************
     step 7: exit process
    ******************************************/
END_1D1_CLIP_6:
    HI_MPI_VO_DisableChn(VoDev, VoChn);
    HI_MPI_VO_DisablePipLayer();
END_1D1_CLIP_5:
    HI_MPI_VO_PipLayerUnBindDev(VoDev);
END_1D1_CLIP_4:
    for(i=0;i<u32WndNum;i++)
    {
        VoChn = i;
        VpssGrp = i;
        SAMPLE_COMM_VO_UnBindVpss(VoDev, VoChn, VpssGrp, VPSS_PRE0_CHN);
    }
    SAMPLE_COMM_VO_UnBindVpss(VoDev, VoChn_Clip, VpssGrp_Clip, VPSS_PRE0_CHN);
    if (stVoPubAttr.enIntfType & VO_INTF_HDMI)
    {
        SAMPLE_COMM_VO_HdmiStop();
    }
    SAMPLE_COMM_VO_StopChn(VoDev, enVoMode);
    HI_MPI_VO_DisableChn(VoDev, VoChn_Clip);
    SAMPLE_COMM_VO_StopDevLayer(VoDev);
END_1D1_CLIP_3:
    SAMPLE_COMM_VI_UnBindVpss(enViMode);
    
    stSrcChn.enModId = HI_ID_VIU;
    stSrcChn.s32DevId = 0;
    stSrcChn.s32ChnId = 0;
    stDestChn.enModId = HI_ID_VPSS;
    stDestChn.s32DevId = VpssGrp_Clip; // vpss group 1
    stDestChn.s32ChnId = 0;
    HI_MPI_SYS_UnBind(&stSrcChn, &stDestChn);
END_1D1_CLIP_2:
    SAMPLE_COMM_VPSS_Stop(s32VpssGrpCnt+1, VPSS_MAX_CHN_NUM);
END_1D1_CLIP_1:
    SAMPLE_COMM_VI_Stop(enViMode);
END_1D1_CLIP_0:
    SAMPLE_COMM_SYS_Exit();
    
    return s32Ret;
}
/******************************************************************************
* function :  VI: 1*VGA ; VO:HD0(LCD,1024x768p@60) HD Zoom preview. 
*                vi(0,0) ---> vpss grp 0       ---> vo HD0, chn0 (VGA, PIP, HIDE)
*                vi(1,0) ---> vpss grp 1       ---> vo HD0, chn1 (LCD 1024x768p@60)
******************************************************************************/
HI_S32 SAMPLE_VIO_1VGA_LCDZoom()
{
    SAMPLE_VI_MODE_E enViMode = SAMPLE_VI_MODE_1_VGA;
    HI_U32 u32ViChnCnt = 2;
    HI_S32 s32VpssGrpCnt = 2;
    VPSS_GRP VpssGrp = 0;
    VO_DEV VoDev = SAMPLE_VO_DEV_DHD0;
    VO_CHN VoChn = 0;
    
    VB_CONF_S stVbConf;
    VPSS_GRP_ATTR_S stGrpAttr;
    VO_PUB_ATTR_S stVoPubAttr[2]; 
    SAMPLE_VO_MODE_E enVoMode;
    SIZE_S stSize;
    
    HI_S32 i;
    HI_S32 s32Ret = HI_SUCCESS;
    HI_U32 u32BlkSize;
    HI_CHAR ch;
    HI_U32 u32WndNum;
	VIDEO_FRAME_INFO_S stFrame;


    /******************************************
     step  1: init global  variable 
    ******************************************/
    gs_u32ViFrmRate = (VIDEO_ENCODING_MODE_PAL == gs_enNorm)?25:30;
    
    memset(&stVbConf,0,sizeof(VB_CONF_S));

    u32BlkSize = SAMPLE_COMM_SYS_CalcPicVbBlkSize(gs_enNorm,\
                PIC_VGA, SAMPLE_PIXEL_FORMAT, SAMPLE_SYS_ALIGN_WIDTH);
    stVbConf.u32MaxPoolCnt = 128;
    /*ddr0 video buffer*/
    stVbConf.astCommPool[0].u32BlkSize = u32BlkSize;
    stVbConf.astCommPool[0].u32BlkCnt = u32ViChnCnt * 10;

    /******************************************
     step 2: mpp system init. 
    ******************************************/
    s32Ret = SAMPLE_COMM_SYS_Init(&stVbConf);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("system init failed with %d!\n", s32Ret);
        goto END_1D1_CLIP_0;
    }

   
    /******************************************
     step 3: start vi dev & chn to capture
    ******************************************/
    s32Ret = SAMPLE_COMM_VI_Start(enViMode, gs_enNorm);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("start vi failed!\n");
        goto END_1D1_CLIP_0;
    }

    /******************************************
     step 4: start vpss and vi bind vpss
    ******************************************/
    s32Ret = SAMPLE_COMM_SYS_GetPicSize(gs_enNorm, PIC_VGA, &stSize);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_SYS_GetPicSize failed!\n");
        goto END_1D1_CLIP_0;
    }

    stGrpAttr.u32MaxW = stSize.u32Width;
    stGrpAttr.u32MaxH = stSize.u32Height;
    stGrpAttr.bDrEn = HI_FALSE;
    stGrpAttr.bDbEn = HI_FALSE;
    stGrpAttr.bIeEn = HI_TRUE;
    stGrpAttr.bNrEn = HI_TRUE;
    stGrpAttr.bHistEn = HI_TRUE;
    stGrpAttr.enDieMode = VPSS_DIE_MODE_AUTO;
    stGrpAttr.enPixFmt = SAMPLE_PIXEL_FORMAT;

    // start 2 vpss group. group-0 for vin0-AV,and group-1 for vin1-CMOS.
    s32Ret = SAMPLE_COMM_VPSS_Start(s32VpssGrpCnt, &stSize, VPSS_MAX_CHN_NUM,NULL);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("Start Vpss failed!\n");
        goto END_1D1_CLIP_1;
    }

    // bind vi0 to vpss group 0
    s32Ret = SAMPLE_COMM_VI_BindVpss(enViMode);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("Vi bind Vpss failed!\n");
        goto END_1D1_CLIP_2;
    }
  /******************************************
     step 5: start VO to preview
    ******************************************/
    printf("start vo hd0\n");
    u32WndNum = 2;
    enVoMode = VO_MODE_1MUX;
    
    if(VIDEO_ENCODING_MODE_PAL == gs_enNorm)
    {
        stVoPubAttr[0].enIntfSync = VO_OUTPUT_1440x900_60;
		stVoPubAttr[0].enIntfType = VO_INTF_VGA;
	}
    else
    {
        stVoPubAttr[0].enIntfSync = VO_OUTPUT_1024x768_60;
		stVoPubAttr[0].enIntfType = VO_INTF_LCD;
    }
    stVoPubAttr[0].u32BgColor = 0x000000ff;
    stVoPubAttr[0].bDoubleFrame = HI_TRUE;

	stVoPubAttr[1].enIntfType = VO_INTF_HDMI;
	stVoPubAttr[1].enIntfSync = VO_OUTPUT_720P60;
    stVoPubAttr[1].u32BgColor = 0x000000ff;
    stVoPubAttr[1].bDoubleFrame = HI_TRUE;
	for(i = 0; i < u32WndNum; i++)
	{
	VoDev = i;
    s32Ret = SAMPLE_COMM_VO_StartDevLayer(VoDev, &stVoPubAttr[i], gs_u32ViFrmRate);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_VO_StartDevLayer failed! for device %d\n",VoDev);
        goto END_1D1_CLIP_3;
    }
    
    s32Ret = SAMPLE_COMM_VO_StartChn(VoDev, &stVoPubAttr[i], enVoMode);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_VO_StartChn failed! for device %d\n",VoDev);
        goto END_1D1_CLIP_4;
    }
    /* if it's displayed on HDMI, we should start HDMI */
    if (stVoPubAttr[i].enIntfType & VO_INTF_HDMI)
    {
        if (HI_SUCCESS != SAMPLE_COMM_VO_HdmiStart(stVoPubAttr[i].enIntfSync))
        {
            SAMPLE_PRT("Start SAMPLE_COMM_VO_HdmiStart failed!\n");
            goto END_1D1_CLIP_4;
        }
    }
    s32Ret = SAMPLE_COMM_VO_BindVpss(VoDev,VoChn,VpssGrp,VPSS_PRE0_CHN);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_VO_BindVpss failed!\n");
        goto END_1D1_CLIP_4;
    }
}

    printf("\npress 'q' to stop sample ... ... \n");
	printf("\npress 's' to save picture from vin ... ... \n");
    while('q' != (ch = getchar()) )  {
		if( 's' == ch ){
		FILE *pfd;
		int height,width;
		pfd = fopen("vin.yuv", "rb");
    	if (!pfd)
    	{
      	  printf("open file -> %s fail \n", "vin.yuv");
      	  return -1;
   		};
		HI_MPI_VI_GetFrame(0, &stFrame);
		width = stFrame.stVFrame.u32Width;
		height = stFrame.stVFrame.u32Height;
		fwrite(stFrame.stVFrame.pVirAddr[0],1,width*height,pfd);
		fwrite(stFrame.stVFrame.pVirAddr[1],1,width*height/2,pfd);
		fwrite(stFrame.stVFrame.pVirAddr[2],1,width*height/2,pfd);
		HI_MPI_VI_ReleaseFrame(0,&stFrame);
		fclose(pfd);
		}

	}
    
    /******************************************
     step 7: exit process
    ******************************************/
#if 0
END_1D1_CLIP_6:
    HI_MPI_VO_DisableChn(VoDev, VoChn);
    HI_MPI_VO_DisablePipLayer();
END_1D1_CLIP_5:
    HI_MPI_VO_PipLayerUnBindDev(VoDev);
#endif
END_1D1_CLIP_4:
    for(i=0;i<u32WndNum;i++)
    {
        VoChn = i;
        VpssGrp = i;
        SAMPLE_COMM_VO_UnBindVpss(VoDev, VoChn, VpssGrp, VPSS_PRE0_CHN);
    }
    SAMPLE_COMM_VO_HdmiStop();
    SAMPLE_COMM_VO_StopChn(VoDev, enVoMode);
    SAMPLE_COMM_VO_StopDevLayer(VoDev);
END_1D1_CLIP_3:
    SAMPLE_COMM_VI_UnBindVpss(enViMode);
END_1D1_CLIP_2:
    SAMPLE_COMM_VPSS_Stop(s32VpssGrpCnt, VPSS_MAX_CHN_NUM);
END_1D1_CLIP_1:
    SAMPLE_COMM_VI_Stop(enViMode);
END_1D1_CLIP_0:
    SAMPLE_COMM_SYS_Exit();
    
    return s32Ret;
}
/******************************************************************************
* function :  VI: 1*D1; VO:SD0(CVBS) SD Zoom preview.
******************************************************************************/
HI_S32 SAMPLE_VIO_1D1_SDZoom(HI_VOID)
{
    SAMPLE_VI_MODE_E enViMode = SAMPLE_VI_MODE_1_D1;
    HI_U32 u32ViChnCnt = 1;
 
    VB_CONF_S stVbConf;
    VI_CHN ViChn;
    VO_DEV VoDev;
    VO_CHN VoChn, VoChn_Clip = 1;
    VO_CHN_ATTR_S stChnAttr;
    VO_PUB_ATTR_S stVoPubAttr; 
    SAMPLE_VO_MODE_E enVoMode;
    
    HI_S32 i;
    HI_S32 s32Ret = HI_SUCCESS;
    HI_U32 u32BlkSize;
    SIZE_S stSize;
    HI_U32 u32WndNum;

    VO_ZOOM_ATTR_S stZoomWindow, stZoomWindowGet;


    /******************************************
     step  1: init global  variable 
    ******************************************/
    gs_u32ViFrmRate = (VIDEO_ENCODING_MODE_PAL== gs_enNorm)?25:30;
    
    memset(&stVbConf,0,sizeof(VB_CONF_S));

    u32BlkSize = SAMPLE_COMM_SYS_CalcPicVbBlkSize(gs_enNorm,\
                PIC_D1, SAMPLE_PIXEL_FORMAT, SAMPLE_SYS_ALIGN_WIDTH);
    stVbConf.u32MaxPoolCnt = 128;
    /*ddr0 video buffer*/
    stVbConf.astCommPool[0].u32BlkSize = u32BlkSize;
    stVbConf.astCommPool[0].u32BlkCnt = u32ViChnCnt * 6;


    /******************************************
     step 2: mpp system init. 
    ******************************************/
    s32Ret = SAMPLE_COMM_SYS_Init(&stVbConf);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("system init failed with %d!\n", s32Ret);
        goto END_1D1_ZOOM_0;
    }

    /******************************************
     step 3: start vi dev & chn to capture
    ******************************************/
    s32Ret = SAMPLE_COMM_VI_Start(enViMode, gs_enNorm);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("start vi failed!\n");
        goto END_1D1_ZOOM_0;
    }

    /******************************************
     step 4: start VO to preview
    ******************************************/
    VoDev = SAMPLE_VO_DEV_DSD0;
    u32WndNum = 1;
    enVoMode = VO_MODE_1MUX;
    
    stVoPubAttr.enIntfSync = VO_OUTPUT_PAL;
    stVoPubAttr.enIntfType = VO_INTF_CVBS;
    stVoPubAttr.u32BgColor = 0x000000ff;
    stVoPubAttr.bDoubleFrame = HI_FALSE;
    s32Ret = SAMPLE_COMM_VO_StartDevLayer(VoDev, &stVoPubAttr, gs_u32ViFrmRate);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_VO_StartDevLayer failed!\n");
        goto END_1D1_ZOOM_1;
    }
    
    s32Ret = SAMPLE_COMM_VO_StartChn(VoDev, &stVoPubAttr, enVoMode);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_VO_StartChn failed!\n");
        goto END_1D1_ZOOM_2;
    }

    for(i=0;i<u32WndNum;i++)
    {
        VoChn = i;
        ViChn = i;
        s32Ret = SAMPLE_COMM_VO_BindVi(VoDev, VoChn, ViChn);
        if (HI_SUCCESS != s32Ret)
        {
            SAMPLE_PRT("SAMPLE_COMM_VO_BindVpss failed!\n");
            goto END_1D1_ZOOM_2;
        }
    }

    /******************************************
     step 5: start new vo Chn for zoom
    ******************************************/
    s32Ret = SAMPLE_COMM_SYS_GetPicSize(gs_enNorm, PIC_CIF, &stSize);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_SYS_GetPicSize failed!\n");
        goto END_1D1_ZOOM_2;
    }
    stChnAttr.stRect.u32Width = ALIGN_BACK(stSize.u32Width, 4);
    stChnAttr.stRect.u32Height= ALIGN_BACK(stSize.u32Height, 4);
    stChnAttr.stRect.s32X       = ALIGN_BACK(0, 4);
    stChnAttr.stRect.s32Y       = ALIGN_BACK(0, 4);
    stChnAttr.u32Priority        = 1;
    stChnAttr.bDeflicker         = HI_FALSE;

    s32Ret = HI_MPI_VO_SetChnAttr(VoDev, VoChn_Clip, &stChnAttr);
    if (s32Ret != HI_SUCCESS)
    {
        SAMPLE_PRT("failed with %#x!\n",  s32Ret);
        goto END_1D1_ZOOM_2;
    }
    s32Ret = HI_MPI_VO_EnableChn(VoDev, VoChn_Clip);
    if (s32Ret != HI_SUCCESS)
    {
        SAMPLE_PRT("failed with %#x!\n", s32Ret);
        goto END_1D1_ZOOM_2;
    }

    s32Ret = SAMPLE_COMM_VO_BindVi(VoDev,VoChn_Clip,0);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_VO_BindVpss failed!\n");
        goto END_1D1_ZOOM_2;
    }

    /******************************************
     step 6: ZOOM process
    ******************************************/
    printf("print any key to zoom mode.\n");
    getchar();

    VoChn = 0;
    stZoomWindow.enZoomType = VOU_ZOOM_IN_RECT;
    stZoomWindow.stZoomRect.s32X = 128;
    stZoomWindow.stZoomRect.s32Y = 128;
    stZoomWindow.stZoomRect.u32Width = 200;
    stZoomWindow.stZoomRect.u32Height = 200;
    
    
    /* set zoom window */
    s32Ret = HI_MPI_VO_SetZoomInWindow(VoDev, VoChn, &stZoomWindow);
    if (s32Ret != HI_SUCCESS)
    {
        SAMPLE_PRT("Set zoom attribute failed with %#x!\n", s32Ret);
        goto END_1D1_ZOOM_2;
    }
    
    /* get current zoom window parameter */
    s32Ret = HI_MPI_VO_GetZoomInWindow(VoDev, VoChn, &stZoomWindowGet);
    if (s32Ret != HI_SUCCESS)
    {
        SAMPLE_PRT("Get zoom attribute failed with %#x!\n", s32Ret);
        goto END_1D1_ZOOM_2;
    }
    printf("Current zoom window is (%d,%d,%d,%d)!\n",
                stZoomWindowGet.stZoomRect.s32X,
                stZoomWindowGet.stZoomRect.s32Y, 
                stZoomWindowGet.stZoomRect.u32Width,
                stZoomWindowGet.stZoomRect.u32Height);

    printf("\npress 'q' to stop sample ... ... \n");
    while('q' != getchar())  {}
    
    /******************************************
     step 7: exit process
    ******************************************/
END_1D1_ZOOM_2:
    for(i=0;i<u32WndNum;i++)
    {
        VoChn = i;
        ViChn= i;
        SAMPLE_COMM_VO_UnBindVi(VoDev, VoChn);
    }
    SAMPLE_COMM_VO_UnBindVi(VoDev, VoChn_Clip);
    SAMPLE_COMM_VO_StopChn(VoDev, enVoMode);
    HI_MPI_VO_DisableChn(VoDev, VoChn_Clip);
    SAMPLE_COMM_VO_StopDevLayer(VoDev);
END_1D1_ZOOM_1:
    SAMPLE_COMM_VI_Stop(enViMode);
END_1D1_ZOOM_0:
    SAMPLE_COMM_SYS_Exit();
    
    return s32Ret;
}
HI_S32 sample_ov7725()
{
	SAMPLE_VI_MODE_E enViMode = SAMPLE_VI_MODE_TEST;
	HI_U32 u32ViChnCnt = 1;
	HI_S32 s32VpssGrpCnt = 1;
	VPSS_GRP VpssGrp = 0;
	VO_DEV VoDev = SAMPLE_VO_DEV_DHD0;
	VO_CHN VoChn = 0;
	VO_CHN VoChn_Clip = 1;
	
	VB_CONF_S stVbConf;
	VPSS_GRP_ATTR_S stGrpAttr;
	VO_PUB_ATTR_S stVoPubAttr; 
	SAMPLE_VO_MODE_E enVoMode;

	SIZE_S stSize;
		
	HI_S32 i;
	HI_S32 s32Ret = HI_SUCCESS;
	HI_U32 u32BlkSize;
	HI_CHAR ch;
	HI_U32 u32WndNum;
	VIDEO_FRAME_INFO_S stFrame;
	
	
	/******************************************
	 step  1: init global  variable 
	******************************************/
	gs_u32ViFrmRate = (VIDEO_ENCODING_MODE_PAL == gs_enNorm)?25:30;
	
	memset(&stVbConf,0,sizeof(VB_CONF_S));

	u32BlkSize = SAMPLE_COMM_SYS_CalcPicVbBlkSize(gs_enNorm,\
				PIC_VGA, SAMPLE_PIXEL_FORMAT, SAMPLE_SYS_ALIGN_WIDTH);
	stVbConf.u32MaxPoolCnt = 128;
	/*ddr0 video buffer*/
	stVbConf.astCommPool[0].u32BlkSize = u32BlkSize;
	stVbConf.astCommPool[0].u32BlkCnt = u32ViChnCnt * 6;

	/******************************************
	 step 2: mpp system init. 
	******************************************/
	s32Ret = SAMPLE_COMM_SYS_Init(&stVbConf);
	if (HI_SUCCESS != s32Ret)
	{
		SAMPLE_PRT("system init failed with %d!\n", s32Ret);
		goto END_1D1_CLIP_0;
	}
	
	   
	/******************************************
	 step 3: start vi dev & chn to capture
	******************************************/
	s32Ret = SAMPLE_COMM_VI_Start(enViMode, gs_enNorm);
	if (HI_SUCCESS != s32Ret)
	{
		SAMPLE_PRT("start vi failed!\n");
		goto END_1D1_CLIP_0;
	}
	
	/******************************************
	 step 4: start vpss and vi bind vpss
	******************************************/
	s32Ret = SAMPLE_COMM_SYS_GetPicSize(gs_enNorm, PIC_VGA, &stSize);
	if (HI_SUCCESS != s32Ret)
	{
		SAMPLE_PRT("SAMPLE_COMM_SYS_GetPicSize failed!\n");
		goto END_1D1_CLIP_0;
	}
	
	stGrpAttr.u32MaxW = stSize.u32Width;
	stGrpAttr.u32MaxH = stSize.u32Height;
	stGrpAttr.bDrEn = HI_FALSE;
	stGrpAttr.bDbEn = HI_FALSE;
	stGrpAttr.bIeEn = HI_TRUE;
	stGrpAttr.bNrEn = HI_TRUE;
	stGrpAttr.bHistEn = HI_TRUE;
	stGrpAttr.enDieMode = VPSS_DIE_MODE_AUTO;
	stGrpAttr.enPixFmt = SAMPLE_PIXEL_FORMAT;
	
	// start  vpss group.
	s32Ret = SAMPLE_COMM_VPSS_Start(s32VpssGrpCnt, &stSize, VPSS_MAX_CHN_NUM,NULL);
	if (HI_SUCCESS != s32Ret)
	{
		SAMPLE_PRT("Start Vpss failed!\n");
		goto END_1D1_CLIP_1;
	}
	
	// bind vi to vpss group
	s32Ret = SAMPLE_COMM_VI_BindVpss(enViMode);
	if (HI_SUCCESS != s32Ret)
	{
		SAMPLE_PRT("Vi bind Vpss failed!\n");
		goto END_1D1_CLIP_2;
	}

	/******************************************
	 step 5: start VO to preview
	******************************************/
	printf("start vo hd0\n");
	u32WndNum = 1;
	enVoMode = VO_MODE_1MUX;

	if(VIDEO_ENCODING_MODE_PAL == gs_enNorm)
	{
		stVoPubAttr.enIntfSync = VO_OUTPUT_1440x900_60;
		stVoPubAttr.enIntfType = VO_INTF_VGA;
	}
	else
	{
		stVoPubAttr.enIntfSync = VO_OUTPUT_1024x768_60;
		stVoPubAttr.enIntfType = VO_INTF_LCD;
	}
	stVoPubAttr.u32BgColor = 0x000000ff;
	stVoPubAttr.bDoubleFrame = HI_TRUE;

	s32Ret = SAMPLE_COMM_VO_StartDevLayer(VoDev, &stVoPubAttr, gs_u32ViFrmRate);
	if (HI_SUCCESS != s32Ret)
	{
		SAMPLE_PRT("SAMPLE_COMM_VO_StartDevLayer failed!\n");
		goto END_1D1_CLIP_3;
	}
		
	s32Ret = SAMPLE_COMM_VO_StartChn(VoDev, &stVoPubAttr, enVoMode);
	if (HI_SUCCESS != s32Ret)
	{
		SAMPLE_PRT("SAMPLE_COMM_VO_StartChn failed!\n");
		goto END_1D1_CLIP_4;
	}
	s32Ret = SAMPLE_COMM_VO_BindVpss(VoDev,VoChn,VpssGrp,VPSS_PRE0_CHN);
	if (HI_SUCCESS != s32Ret)
	{
		SAMPLE_PRT("SAMPLE_COMM_VO_BindVpss failed!\n");
		goto END_1D1_CLIP_4;
	}
	
	printf("\npress 'q' to stop sample ... ... \n");
	printf("\npress 's' to save picture from vin ... ... \n");
	while('q' != (ch = getchar()) )  {
		#if 0
		if( 's' == ch ){
		FILE *pfd;
		int height,width;
		pfd = fopen("vin.yuv", "rb");
		if (!pfd)
		{
		  printf("open file -> %s fail \n", "vin.yuv");
		  return -1;
		};
		HI_MPI_VI_GetFrame(0, &stFrame);
		width = stFrame.stVFrame.u32Width;
		height = stFrame.stVFrame.u32Height;
		fwrite(stFrame.stVFrame.pVirAddr[0],1,width*height,pfd);
		fwrite(stFrame.stVFrame.pVirAddr[1],1,width*height/2,pfd);
		fwrite(stFrame.stVFrame.pVirAddr[2],1,width*height/2,pfd);
		HI_MPI_VI_ReleaseFrame(0,&stFrame);
		fclose(pfd);
		}
		#endif

	}
		
	/******************************************
	 step 7: exit process
	******************************************/
END_1D1_CLIP_4:
    for(i=0;i<u32WndNum;i++)
    {
        VoChn = i;
        VpssGrp = i;
        SAMPLE_COMM_VO_UnBindVpss(VoDev, VoChn, VpssGrp, VPSS_PRE0_CHN);
    }

    SAMPLE_COMM_VO_StopChn(VoDev, enVoMode);
    HI_MPI_VO_DisableChn(VoDev, VoChn_Clip);
    SAMPLE_COMM_VO_StopDevLayer(VoDev);
END_1D1_CLIP_3:
    SAMPLE_COMM_VI_UnBindVpss(enViMode);
END_1D1_CLIP_2:
    SAMPLE_COMM_VPSS_Stop(s32VpssGrpCnt, VPSS_MAX_CHN_NUM);
END_1D1_CLIP_1:
    SAMPLE_COMM_VI_Stop(enViMode);
END_1D1_CLIP_0:
    SAMPLE_COMM_SYS_Exit();
    
    return s32Ret;
}

HI_S32 sample_capture_only()
{
	HI_S32 s32Ret;
	#define CMOS_CAPTURE_DEV  1;
	VI_DEV ViDev = CMOS_CAPTURE_DEV;
	VI_CHN ViChn = 0;
	VI_DEV_ATTR_S stDevAttr;
	VI_CHN_ATTR_S stChnAttr;
	char ch;
	VIDEO_FRAME_INFO_S stFrame;
	HI_S32 u32BlkSize;
	VB_CONF_S stVbConf;

	/******************************************
	 step  1: init global  variable 
	******************************************/
	gs_u32ViFrmRate = (VIDEO_ENCODING_MODE_PAL == gs_enNorm)?50:60;

	memset(&stVbConf,0,sizeof(VB_CONF_S));

	u32BlkSize = SAMPLE_COMM_SYS_CalcPicVbBlkSize(gs_enNorm,\
				PIC_VGA, SAMPLE_PIXEL_FORMAT, SAMPLE_SYS_ALIGN_WIDTH);
	stVbConf.u32MaxPoolCnt = 128;
	/*ddr0 video buffer*/
	stVbConf.astCommPool[0].u32BlkSize = u32BlkSize;
	stVbConf.astCommPool[0].u32BlkCnt = 1 * 6;
	
	/******************************************
	 step 2: mpp system init. 
	******************************************/
	s32Ret = SAMPLE_COMM_SYS_Init(&stVbConf);
	if (HI_SUCCESS != s32Ret)
	{
		SAMPLE_PRT("system init failed with %d!\n", s32Ret);
	}
	s32Ret = SAMPLE_OV7725_CfgV();
	memset(&stDevAttr,0,sizeof(stDevAttr));

	stDevAttr.enIntfMode = VI_MODE_BT656;
	stDevAttr.enWorkMode = VI_WORK_MODE_1Multiplex;
	stDevAttr.au32CompMask[0] = 0xFF000000;
	stDevAttr.au32CompMask[1] = 0x0;
	stDevAttr.enScanMode = VI_SCAN_PROGRESSIVE;
	stDevAttr.s32AdChnId[0] = -1;
	stDevAttr.s32AdChnId[1] = -1;
	stDevAttr.s32AdChnId[2] = -1;
	stDevAttr.s32AdChnId[3] = -1;
#if 0
	stDevAttr.stSynCfg.enVsync = VI_VSYNC_PULSE;
	stDevAttr.stSynCfg.enVsyncNeg = VI_VSYNC_NEG_HIGH;
	stDevAttr.stSynCfg.enHsync= VI_HSYNC_VALID_SINGNAL;
	stDevAttr.stSynCfg.enHsyncNeg = VI_HSYNC_NEG_HIGH;
	stDevAttr.stSynCfg.enVsyncValid= VI_VSYNC_NORM_PULSE;
	stDevAttr.stSynCfg.enVsyncValidNeg= VI_VSYNC_VALID_NEG_HIGH;
	stDevAttr.stSynCfg.stTimingBlank.u32HsyncHfb = 76;//δ����ͬ������
	stDevAttr.stSynCfg.stTimingBlank.u32HsyncAct = 640;
	stDevAttr.stSynCfg.stTimingBlank.u32HsyncHbb = 64;
	stDevAttr.stSynCfg.stTimingBlank.u32VsyncVfb = 20;
	stDevAttr.stSynCfg.stTimingBlank.u32VsyncVact = 480;
	stDevAttr.stSynCfg.stTimingBlank.u32VsyncVbb = 6;
	stDevAttr.stSynCfg.stTimingBlank.u32VsyncVbfb = 0;
	stDevAttr.stSynCfg.stTimingBlank.u32VsyncVbact = 0;
	stDevAttr.stSynCfg.stTimingBlank.u32VsyncVbbb = 0;
#endif
	SAMPLE_COMM_VI_SetMask(ViDev,&stDevAttr);

	s32Ret = HI_MPI_VI_SetDevAttr(ViDev, &stDevAttr);
	if (s32Ret != HI_SUCCESS)
	{
	printf("Set dev attributes failed with error code %#x!\n", s32Ret);
	return HI_FAILURE;
	}

	s32Ret = HI_MPI_VI_EnableDev(ViDev);
	if (s32Ret != HI_SUCCESS)
	{
	printf("Enable dev failed with error code %#x!\n", s32Ret);
	return HI_FAILURE;
	}

	stChnAttr.stCapRect.s32X = 0;
	stChnAttr.stCapRect.s32Y = 0;
	stChnAttr.stCapRect.u32Width = 640;
	stChnAttr.stCapRect.u32Height = 480;
	stChnAttr.stDestSize.u32Width = 640;
	stChnAttr.stDestSize.u32Height = 480;
	stChnAttr.enCapSel = VI_CAPSEL_BOTH;
	stChnAttr.enPixFormat = PIXEL_FORMAT_YUV_SEMIPLANAR_422;
	stChnAttr.bMirror = HI_FALSE;
	stChnAttr.bFilp = HI_FALSE;
	stChnAttr.bChromaResample = HI_FALSE;
	stChnAttr.s32SrcFrameRate = -1;
	stChnAttr.s32FrameRate = -1;

	s32Ret = HI_MPI_VI_SetChnAttr(ViChn,&stChnAttr);
	if (s32Ret != HI_SUCCESS)
	{
	printf("Set chn attributes failed with error code %#x!\n", s32Ret);
	return HI_FAILURE;
	}

	s32Ret = HI_MPI_VI_EnableChn(ViChn);
	if (s32Ret != HI_SUCCESS)
	{
	printf("Enable chn failed with error code %#x!\n", s32Ret);
	return HI_FAILURE;
	}
	
	VPSS_GRP VpssGrp = 0;
	VPSS_CHN VpssChn = 0;
	VPSS_GRP_ATTR_S stGrpAttr;
    VPSS_CHN_ATTR_S stVpssChnAttr;
    VPSS_GRP_PARAM_S stVpssParam;
    HI_S32 s32GrpCnt = 2,s32ChnCnt = 3;
    HI_S32 i, j;
	
	stGrpAttr.u32MaxW = 640;
    stGrpAttr.u32MaxH = 480;
    stGrpAttr.bDrEn = HI_FALSE;
    stGrpAttr.bDbEn = HI_FALSE;
    stGrpAttr.bIeEn = HI_TRUE;
    stGrpAttr.bNrEn = HI_TRUE;
    stGrpAttr.bHistEn = HI_TRUE;
    stGrpAttr.enDieMode = VPSS_DIE_MODE_AUTO;
    stGrpAttr.enPixFmt = SAMPLE_PIXEL_FORMAT;

	for(i=0; i<s32GrpCnt; i++)
	{
        VpssGrp = i;
        /*** create vpss group ***/
        s32Ret = HI_MPI_VPSS_CreatGrp(VpssGrp, &stGrpAttr);
        if (s32Ret != HI_SUCCESS)
        {
            SAMPLE_PRT("HI_MPI_VPSS_CreateGrp failed with %#x!\n", s32Ret);
            return HI_FAILURE;
        }

        /*** set vpss param ***/
        s32Ret = HI_MPI_VPSS_GetParam(VpssGrp, 0, &stVpssParam);
        if (s32Ret != HI_SUCCESS)
        {
            SAMPLE_PRT("failed with %#x!\n", s32Ret);
            return HI_FAILURE;
        }
        
        stVpssParam.u32MotionThresh = 0;
        
        s32Ret = HI_MPI_VPSS_SetParam(VpssGrp, 0, &stVpssParam);
        if (s32Ret != HI_SUCCESS)
        {
            SAMPLE_PRT("failed with %#x!\n", s32Ret);
            return HI_FAILURE;
        }
		VPSS_PRESCALE_INFO_S pstPreScaleInfo;
		memset(&pstPreScaleInfo,0,sizeof(VPSS_PRESCALE_INFO_S));
		pstPreScaleInfo.bPreScale = HI_TRUE;
		pstPreScaleInfo.enCapSel = VPSS_CAPSEL_BOTH;
		pstPreScaleInfo.stDestSize.u32Width = 1024;
		pstPreScaleInfo.stDestSize.u32Height = 768;
		
		s32Ret = HI_MPI_VPSS_SetPreScale(VpssGrp,&pstPreScaleInfo);
		if (s32Ret != HI_SUCCESS)
        {
            SAMPLE_PRT("failed with %#x!\n", s32Ret);
            return HI_FAILURE;
        }

        /*** enable vpss chn, with frame ***/
        for(j=0; j<s32ChnCnt; j++)
        {
            VpssChn = j;
            /* Set Vpss Chn attr */
            stVpssChnAttr.bSpEn = HI_FALSE;
            stVpssChnAttr.bFrameEn = HI_TRUE;
            stVpssChnAttr.stFrame.u32Color[VPSS_FRAME_WORK_LEFT] = 0xff00;
            stVpssChnAttr.stFrame.u32Color[VPSS_FRAME_WORK_RIGHT] = 0xff00;
            stVpssChnAttr.stFrame.u32Color[VPSS_FRAME_WORK_BOTTOM] = 0xff00;
            stVpssChnAttr.stFrame.u32Color[VPSS_FRAME_WORK_TOP] = 0xff00;
            stVpssChnAttr.stFrame.u32Width[VPSS_FRAME_WORK_LEFT] = 2;
            stVpssChnAttr.stFrame.u32Width[VPSS_FRAME_WORK_RIGHT] = 2;
            stVpssChnAttr.stFrame.u32Width[VPSS_FRAME_WORK_TOP] = 2;
            stVpssChnAttr.stFrame.u32Width[VPSS_FRAME_WORK_BOTTOM] = 2;
            
            s32Ret = HI_MPI_VPSS_SetChnAttr(VpssGrp, VpssChn, &stVpssChnAttr);
            if (s32Ret != HI_SUCCESS)
            {
                SAMPLE_PRT("HI_MPI_VPSS_SetChnAttr failed with %#x\n", s32Ret);
                return HI_FAILURE;
            }
    
            s32Ret = HI_MPI_VPSS_EnableChn(VpssGrp, VpssChn);
            if (s32Ret != HI_SUCCESS)
            {
                SAMPLE_PRT("HI_MPI_VPSS_EnableChn failed with %#x\n", s32Ret);
                return HI_FAILURE;
            }
        }

        /*** start vpss group ***/
        s32Ret = HI_MPI_VPSS_StartGrp(VpssGrp);
        if (s32Ret != HI_SUCCESS)
        {
            SAMPLE_PRT("HI_MPI_VPSS_StartGrp failed with %#x\n", s32Ret);
            return HI_FAILURE;
        }

    }
	/* bind vin-0 to vpss-0 */
	MPP_CHN_S stSrcChn;
    MPP_CHN_S stDestChn;
	
	stSrcChn.enModId = HI_ID_VIU;
	stSrcChn.s32DevId = 0;
	stSrcChn.s32ChnId = 0;
	stDestChn.enModId = HI_ID_VPSS;
    stDestChn.s32DevId = 0;
    stDestChn.s32ChnId = 0;
	s32Ret = HI_MPI_SYS_Bind(&stSrcChn, &stDestChn);
    if (s32Ret != HI_SUCCESS)
    {
        SAMPLE_PRT("failed with %#x!\n", s32Ret);
        return HI_FAILURE;
    }
	/* bind vin-1 to vpss-1 */
	stSrcChn.enModId = HI_ID_VIU;
	stSrcChn.s32DevId = 1;
	stSrcChn.s32ChnId = 0;
	stDestChn.enModId = HI_ID_VPSS;
    stDestChn.s32DevId = 1;
    stDestChn.s32ChnId = 0;
	s32Ret = HI_MPI_SYS_Bind(&stSrcChn, &stDestChn);
    if (s32Ret != HI_SUCCESS)
    {
        SAMPLE_PRT("failed with %#x!\n", s32Ret);
        return HI_FAILURE;
    }
	
	/* create vout */
	printf("start vo HD0.\n");
	HI_S32 u32WndNum = 1;
	SAMPLE_VO_MODE_E enVoMode = VO_MODE_1MUX;
	VO_PUB_ATTR_S stVoPubAttr;
	VO_DEV VoDev = SAMPLE_VO_DEV_DHD0;
    VO_CHN VoChn = 0;

	stVoPubAttr.enIntfSync = VO_OUTPUT_1024x768_60;
	if(VIDEO_ENCODING_MODE_PAL == gs_enNorm)
	{
		stVoPubAttr.enIntfType = VO_INTF_HDMI|VO_INTF_LCD;
	}else{
		stVoPubAttr.enIntfType = VO_INTF_HDMI|VO_INTF_VGA;
	}
	stVoPubAttr.u32BgColor = 0x000000ff;
	stVoPubAttr.bDoubleFrame = HI_TRUE;
	
	s32Ret = SAMPLE_COMM_VO_StartDevLayer(VoDev, &stVoPubAttr, gs_u32ViFrmRate);
	if (HI_SUCCESS != s32Ret)
	{
		SAMPLE_PRT("Start SAMPLE_COMM_VO_StartDevLayer failed!\n");
		return HI_FAILURE;
	}
	s32Ret = SAMPLE_COMM_VO_StartChn(VoDev, &stVoPubAttr, enVoMode);
	if (HI_SUCCESS != s32Ret)
	{
		SAMPLE_PRT("Start SAMPLE_COMM_VO_StartChn failed!\n");
		return HI_FAILURE;
	}

	/* if it's displayed on HDMI, we should start HDMI */
	if (stVoPubAttr.enIntfType & VO_INTF_HDMI)
	{
		if (HI_SUCCESS != SAMPLE_COMM_VO_HdmiStart(stVoPubAttr.enIntfSync))
		{
			SAMPLE_PRT("Start SAMPLE_COMM_VO_HdmiStart failed!\n");
			return HI_FAILURE;
		}
	}
	
	for(i=0;i<u32WndNum;i++)
	{
		VoChn = i;
		VpssGrp = i;
		
		s32Ret = SAMPLE_COMM_VO_BindVpss(VoDev,VoChn,VpssGrp,0);
		if (HI_SUCCESS != s32Ret)
		{
			SAMPLE_PRT("Start VO failed!\n");
			return HI_FAILURE;
		}
	}

	printf("\npress 'q' to stop sample ... ... \n");
	printf("\npress 's' to save picture from vin ... ... \n");
	while('q' != (ch = getchar()) )  {
		if( 's' == ch ){
			FILE *pfd;
			int height,width;
			pfd = fopen("vin.yuv", "rb");
			if (!pfd)
			{
			  printf("open file -> %s fail \n", "vin.yuv");
			  return -1;
			};
			HI_MPI_VI_GetFrame(0, &stFrame);
			width = stFrame.stVFrame.u32Width;
			height = stFrame.stVFrame.u32Height;
			fwrite(stFrame.stVFrame.pVirAddr[0],1,width*height,pfd);
			fwrite(stFrame.stVFrame.pVirAddr[1],1,width*height/2,pfd);
			fwrite(stFrame.stVFrame.pVirAddr[2],1,width*height/2,pfd);
			HI_MPI_VI_ReleaseFrame(0,&stFrame);
			fclose(pfd);
		}

	}

	s32Ret = HI_MPI_VI_DisableChn(ViChn);
	if (s32Ret != HI_SUCCESS)
	{
	printf("Disable chn failed with error code %#x!\n", s32Ret);
	return HI_FAILURE;
	}

	s32Ret = HI_MPI_VI_DisableDev(ViDev);
	if (s32Ret != HI_SUCCESS)
	{
	printf("Disable dev failed with error code %#x!\n", s32Ret);
	return HI_FAILURE;
	}
	SAMPLE_COMM_SYS_Exit();
	return s32Ret;
}
/******************************************************************************
* function    : main()
* Description : video preview sample
******************************************************************************/
int main(int argc, char *argv[])
{
    HI_S32 s32Ret;

    if ( (argc < 2) || (1 != strlen(argv[1])))
    {
        SAMPLE_VIO_Usage(argv[0]);
        return HI_FAILURE;
    }

    signal(SIGINT, SAMPLE_VIO_HandleSig);
    signal(SIGTERM, SAMPLE_VIO_HandleSig);
	switch (*argv[1])
	{
		case 'P'://PAL
			gs_enNorm = VIDEO_ENCODING_MODE_PAL;
			break;
		case 'N'://NTSC
			gs_enNorm = VIDEO_ENCODING_MODE_NTSC;
			break;
	}

    switch (*argv[2])
    {
        case '0':/* VI:8*D1; VO:HD0(HDMI,VGA)+SD0(CVBS)+SD1 video preview. */
            s32Ret = SAMPLE_VIO_8_D1();
            break;
        case '1':/* VI:1*720P; VO:HD0(HDMI ),  video preview */
            s32Ret = SAMPLE_VIO_1_720P();
            break;
        case '2':/* VI:16*2cif; VO: HD0(HDMI,1080p@60 ), WBC to SD0(CVBS) video preview. */
            s32Ret = SAMPLE_VIO_16_2cif();
            break;
	    case '3':/* VI:16*D1/2Cif MixCap;  HD0(HDMI,1080p@60 ) video preview. */
            s32Ret = SAMPLE_VIO_16_D1Cif();
            break;
        case '4':/* VI: 1*D1, user picture; VO: SD0(CVBS) video preview. */
            s32Ret = SAMPLE_VIO_1D1_LossDet();
            break;
        case '5':/* VI: 1*D1; VO:HD0(HDMI) HD Zoom preview.  */
            s32Ret = SAMPLE_VIO_1D1_HDZoom();
            break;
        case '6':/* VI: 1*D1; VO:SD0(CVBS) SD Zoom preview. */
            s32Ret = SAMPLE_VIO_1D1_SDZoom();
            break;
        case '7':/* VI: 1*VGA; VO:HD0(LCD 1024x768) HD Zoom preview. */
            s32Ret = SAMPLE_VIO_1VGA_LCDZoom();
            break;
		case '8':
			s32Ret = sample_ov7725();
            break;
		case '9':
			s32Ret = sample_capture_only();
			break;
        default:
            printf("the index is invaild!\n");
            SAMPLE_VIO_Usage(argv[0]);
            return HI_FAILURE;
    }

    if (HI_SUCCESS == s32Ret)
        printf("program exit normally!\n");
    else
        printf("program exit abnormally!\n");
    exit(s32Ret);
}

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */

