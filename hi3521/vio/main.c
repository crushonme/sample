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

    printf("\t 0) VI:PAL-IN; VO:VGA-OUT. \n");
    printf("\t 1) VI:PAL-IN; VO:LCD-OUT.\n");
    printf("\t 2) VI:CMOS-IN; VO:VGA-OUT.\n");
    printf("\t 3) VI:CMOS-IN; VO:LCD-OUT.\n");	
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

HI_S32 pal_in_vga_out()
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
    
    stVoPubAttr[0].enIntfSync = VO_OUTPUT_1366x768_60;
	stVoPubAttr[0].enIntfType = VO_INTF_VGA;
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

HI_S32 pal_in_lcd_out()
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
    
    stVoPubAttr[0].enIntfSync = VO_OUTPUT_1024x768_60;
    stVoPubAttr[0].enIntfType = VO_INTF_LCD;
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

HI_S32 cmos_in_vga_out()
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

	stVoPubAttr.enIntfSync = VO_OUTPUT_1366x768_60;
	stVoPubAttr.enIntfType = VO_INTF_VGA;
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

HI_S32 cmos_in_lcd_out()
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
	stVoPubAttr.enIntfType = VO_INTF_LCD;
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

    if ( (argc < 1) || (1 != strlen(argv[1])))
    {
        SAMPLE_VIO_Usage(argv[0]);
        return HI_FAILURE;
    }

    signal(SIGINT, SAMPLE_VIO_HandleSig);
    signal(SIGTERM, SAMPLE_VIO_HandleSig);

    switch (*argv[1])
    {
        case '0':
            s32Ret = pal_in_vga_out();
            break;
        case '1':
            s32Ret = pal_in_lcd_out();
            break;
        case '2':
            s32Ret = cmos_in_vga_out();
            break;
	    case '3':
            s32Ret = cmos_in_lcd_out();
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

