#include "fa125Lib.h"
#include "fa125Config.h"

extern unsigned int fa125AddrList[FA125_MAX_BOARDS];
extern int32_t nfadc125;
int32_t FA_SLOT;

int32_t NFADC_125;
FADC125_CONF fa125[FA125_MAX_BOARDS];
uint32_t trig_delay = 0; /* Extra delay to add to the config value */

// To be defined in fa125Config.{c,h}
#define print_fadc125_conf(x)

int
fa125_download()
{

  int slot, ichan;
  int stat;
  int debug_init = 0;

  for(slot = 3; slot < 21; slot++)
    {
      if(fa125[slot].group == 1)
	{
	  fa125AddrList[nfadc125] = (slot << 19);
	  nfadc125++;

	  print_fadc125_conf(slot);

	}
    }

  printf(" NUMBER OF FADC125  boards found in config file =  %d \n",
	 nfadc125);

  int iFlag = 0;

  iFlag = (1 << 17);		/* use fa125AddrList */
  /*
    bits 2-1:  defines Trigger source
    (0) 0 0  VXS (P0)
    (1) 0 1  Internal Timer
    (2) 1 0  Internal Multiplicity Sum
    (3) 1 1  P2 Connector (Backplane)
    bit    3:  NOT USED WITH THIS FIRMWARE VERSION
    bits 5-4:  defines Clock Source
    (0) 0 0  P2 Connector (Backplane)
    (1) 0 1  VXS (P0)
    (2) 1 0  Internal 125MHz Clock
  */

  iFlag |= (0 << 1);		/* Trigger Source */
  iFlag |= (1 << 4);		/* Clock Source */
  iFlag |= (1 << 18);		/* Skip firmware check */

  stat = fa125Init(0, 0, nfadc125, iFlag);

  if(stat != OK)
    {
      daLogMsg("ERROR", "fa125Init failed \n");
      return -1;
    }

  NFADC_125 = nfadc125;		/* Redefine our NFADC with what was found from the driver */

  printf(" NUMBER OF FADC125  initialized  %d \n", NFADC_125);

  fa125ResetToken(0);		//---  !!!


  for(slot = 0; slot < NFADC_125; slot++)
    {
      FA_SLOT = fa125Slot(slot);

      if((fa125[FA_SLOT].data_format > 1) || (fa125[FA_SLOT].data_format < 0))
	{
	  daLogMsg("ERROR",
		   " FADC125 initialization:  Wrong data format FORMAT  %d \n",
		   fa125[FA_SLOT].data_format);
	  ROL_SET_ERROR;
	}

      if(fa125[FA_SLOT].npeak > FA125_NPEAK_MAX)
	{
	  daLogMsg("ERROR",
		   " FADC125 initialization:  Wrong value of  FA125_NPEAK  %d \n",
		   fa125[FA_SLOT].npeak);
	  ROL_SET_ERROR;
	}


      if(fa125[FA_SLOT].IE > FA125_IE_MAX)
	{
	  daLogMsg("ERROR",
		   " FADC125 initialization:  Wrong value of  FA125_IE  %d \n",
		   fa125[FA_SLOT].IE);
	  ROL_SET_ERROR;
	}


      if(fa125[FA_SLOT].PG > FA125_PG_MAX)
	{
	  daLogMsg("ERROR",
		   " FADC125 initialization:  Wrong value of  FA125_PG  %d \n",
		   fa125[FA_SLOT].PG);
	  ROL_SET_ERROR;
	}


      if(fa125[FA_SLOT].IBIT > FA125_IBIT_MAX)
	{
	  daLogMsg("ERROR",
		   " FADC125 initialization:  Wrong value of  FA125_IBIT  %d \n",
		   fa125[FA_SLOT].IBIT);
	  ROL_SET_ERROR;
	}

      if(fa125[FA_SLOT].ABIT > FA125_ABIT_MAX)
	{
	  daLogMsg("ERROR",
		   " FADC125 initialization:  Wrong value of  FA125_ABIT  %d \n",
		   fa125[FA_SLOT].ABIT);
	  ROL_SET_ERROR;
	}

      if(fa125[FA_SLOT].PBIT > FA125_PBIT_MAX)
	{
	  daLogMsg("ERROR",
		   " FADC125 initialization:  Wrong value of  FA125_PBIT  %d \n",
		   fa125[FA_SLOT].PBIT);
	  ROL_SET_ERROR;
	}


      if(fa125[FA_SLOT].P1 > FA125_P1_MAX)
	{
	  daLogMsg("ERROR",
		   " FADC125 initialization:  Wrong value of  FA125_P1  %d \n",
		   fa125[FA_SLOT].P1);
	  ROL_SET_ERROR;
	}

      if(fa125[FA_SLOT].P2 > FA125_P2_MAX)
	{
	  daLogMsg("ERROR",
		   " FADC125 initialization:  Wrong value of  FA125_P2  %d \n",
		   fa125[FA_SLOT].P2);
	  ROL_SET_ERROR;
	}

      if(fa125[FA_SLOT].TH > FA125_TH_MAX)
	{
	  daLogMsg("ERROR",
		   " FADC125 initialization:  Wrong value of  FA125_TH  %d \n",
		   fa125[FA_SLOT].TH);
	  ROL_SET_ERROR;
	}

      if(fa125[FA_SLOT].TL > FA125_TL_MAX)
	{
	  daLogMsg("ERROR",
		   " FADC125 initialization:  Wrong value of  FA125_TL  %d \n",
		   fa125[FA_SLOT].TL);
	  ROL_SET_ERROR;
	}


      for(ichan = 0; ichan < 72; ichan++)
	{
	  if((fa125[FA_SLOT].read_thr[ichan] < fa125[FA_SLOT].TH) ||
	     (fa125[FA_SLOT].read_thr[ichan] < fa125[FA_SLOT].TL) ||
	     (fa125[FA_SLOT].TH < fa125[FA_SLOT].TL))
	    {
	      daLogMsg("ERROR",
		       " FADC125 initialization:   Wrong Thresholds  in Slot = %d   THR   =  %d   TH  =  %d    TL  =  %d    Channel  =  %d  \n",
		       FA_SLOT, fa125[FA_SLOT].read_thr[ichan],
		       fa125[FA_SLOT].TH, fa125[FA_SLOT].TL, ichan);

	      ROL_SET_ERROR;
	    }
	}




      fa125PowerOn(FA_SLOT);

      for(ichan = 0; ichan < 72; ichan++)
	{

	  fa125SetOffset(FA_SLOT, ichan, fa125[FA_SLOT].dac[ichan]);

	  unsigned int LOWTHR = 0;
	  //LOWTHR=fa125[FA_SLOT].read_thr[ichan]*0.5;  printf("SL=%d CH=%d THR=%d LOW=%d  TH=%d  TL=%d \n",FA_SLOT,ichan,fa125[FA_SLOT].read_thr[ichan],LOWTHR,fa125[FA_SLOT].TH,fa125[FA_SLOT].TL);

	  if(LOWTHR > 0)
	    fa125SetThreshold(FA_SLOT, ichan, LOWTHR);	// SF for tests
	  else
	    {
	      if(fa125[FA_SLOT].read_thr[ichan] > 0x1FF)
		fa125SetThreshold(FA_SLOT, ichan, 0x1FF);	// Maximum Threshold value is 511
	      else
		fa125SetThreshold(FA_SLOT, ichan,
				  fa125[FA_SLOT].read_thr[ichan]);
	    }

	  // Alex
	  //      fa125SetThreshold(FA_SLOT, ichan, 1);
	}

      fa125PrintThreshold(FA_SLOT);


      //fa125Reset(FA_SLOT, 0);  //--- ???

      // trig_delay is set in 4 ns bins
      unsigned int window_offset = fa125[FA_SLOT].winOffset + trig_delay / 2;

      printf("Trigger global delay  =  %d,  Window offset =  %d \n", trig_delay, window_offset);


      // ================================================================================================
      /*
	"Integral and Time (CDC_short)",             // 3
	"Integral and Time (FDC_short)",             // 4
	"Peak Amplitude and Time (FDC_amp_short)",   // 5
	"Pulse Data and Samples (CDC_long)",         // 6
	"Pulse Data and Samples (FDC_sum_long)",     // 7
	"Peak Amplitude and Samples (FDC_amp_long)", // 8
      */

      int rval =
	fa125SetProcMode(FA_SLOT,
			 (char *)fa125_modes[fa125[FA_SLOT].mode],
			 window_offset,
			 fa125[FA_SLOT].winWidth,
			 fa125[FA_SLOT].IE,
			 fa125[FA_SLOT].PG,	// PG default 4
			 fa125[FA_SLOT].npeak,	// number of peaks
			 fa125[FA_SLOT].P1,	// P1 default 4
			 fa125[FA_SLOT].P2);	// P2 default 4

      if(rval != OK)
	{
	  daLogMsg("ERROR", "fa125SetProcMode failed \n");
	  return -1;
	}

      // And new now set the Scale Factors
      // IBIT = 4 scale integral
      // ABIT = 3 scale amplitude
      // PBIT = 0 scale pedestal

      rval =
	fa125SetScaleFactors(FA_SLOT, fa125[FA_SLOT].IBIT,
			     fa125[FA_SLOT].ABIT, fa125[FA_SLOT].PBIT);

      if(rval != OK)
	{
	  daLogMsg("ERROR", "fa125SetScaleFactors failed \n");
	  return -1;
	}



      // And also new the thresholds for the timing alorighm
      // TH = 100
      // TL = 25

      printf("  CHECK  TL = %d   TH = %d \n ", fa125[FA_SLOT].TL,
	     fa125[FA_SLOT].TH);

      rval =
	fa125SetCommonTimingThreshold(FA_SLOT, fa125[FA_SLOT].TL,
				      fa125[FA_SLOT].TH);

      if(rval != OK)
	{
	  daLogMsg("ERROR", "fa125SetCommonTimingThreshold failed \n");
	  return -1;
	}

      //    fa125PrintTimingThresholds(FA_SLOT);

      //================================================================================================

      if(debug_init)
	{
	  printf(" SLOT = %d \n", FA_SLOT);
	  printf(" mode       =  %d  \n", fa125[FA_SLOT].mode);
	  printf(" winOffset  =  %d  \n", fa125[FA_SLOT].winOffset);
	  printf(" winWidth   =  %d  \n", fa125[FA_SLOT].winWidth);
	  printf(" nsb        =  %d  \n", fa125[FA_SLOT].nsb);
	  printf(" nsa        =  %d  \n", fa125[FA_SLOT].nsa);
	  printf(" npeak      =  %d  \n", fa125[FA_SLOT].npeak);
	}


      printf("\n");
      printf("Disabled channels:   0x%X   0x%X   0x%X  \n",
	     (fa125[FA_SLOT].ch_disable[0] & 0xFFFFFF),
	     (fa125[FA_SLOT].ch_disable[1] & 0xFFFFFF),
	     (fa125[FA_SLOT].ch_disable[2] & 0xFFFFFF));
      printf("Enabled channels:   0x%X   0x%X   0x%X  \n",
	     (~fa125[FA_SLOT].ch_disable[0] & 0xFFFFFF),
	     (~fa125[FA_SLOT].ch_disable[1] & 0xFFFFFF),
	     (~fa125[FA_SLOT].ch_disable[2] & 0xFFFFFF));

      printf("\n");
      printf("Threshold factor = %f \n",
	     fa125[FA_SLOT].thr_fact);
      printf("\n");


      /* Enable all channels */
      //    fa125SetChannelEnableMask(FA_SLOT, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF);

      // Alex
      fa125SetChannelEnableMask(FA_SLOT,
				(~fa125[FA_SLOT].ch_disable[0] & 0xFFFFFF),
				(~fa125[FA_SLOT].ch_disable[0] & 0xFFFFFF),
				(~fa125[FA_SLOT].ch_disable[0] & 0xFFFFFF));

      //    fa125SetChannelDisableMask(FA_SLOT, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF);
      //    fa125SetChannelEnableMask(FA_SLOT, 0x1, 0 , 0);
      //    fa125SetChannelEnableMask(FA_SLOT, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF );
      //    fa125SetChannelEnableMask(FA_SLOT, 0xFFFFFF, 0xFFF000, 0 );
      //    fa125SetChannelEnableMask(FA_SLOT, 0xF, 0, 0 );



      printf("fa125_prestart():: Reset  board in slot  = %d \n", FA_SLOT);
      fa125Reset(FA_SLOT, 0);	//-- !!!!

      fa125PrintThreshold(FA_SLOT);
      fa125PrintTimingThresholds(FA_SLOT);

      printf("\n");
      printf("DAC values for FADC125 in slot %d: \n", FA_SLOT);

      for(ichan = 0; ichan < 72; ichan++)
	{
	  if((ichan % 4) == 0)
	    {
	      printf("\n");
	    }
	  printf("Ch  %2d:  %6d   ", ichan, fa125ReadOffset(FA_SLOT, ichan));
	}
      printf("\n");

      if((fa125[FA_SLOT].busy > 0) && (fa125[FA_SLOT].busy < 5))
	{
	  fa125SetNTrigBusy(FA_SLOT, fa125[FA_SLOT].busy);
	  printf(" Enable BUSY for FA125 in slot %d : %d \n",
		 FA_SLOT, fa125[FA_SLOT].busy);
	}
      else
	{
	  printf(" BUSY for FA125 in slot %d  is out of range. Set to 3 \n",
		 FA_SLOT);
	  fa125SetNTrigBusy(FA_SLOT, 3);
	}


      if(fa125[FA_SLOT].data_format == 0)
	printf(" Set data FORMAT for FADC in slot %d to  0  (enable trigger time)   \n", FA_SLOT);
      else if(fa125[FA_SLOT].data_format == 1)
	printf(" Set data FORMAT for FADC in slot %d to  1  (suppress trigger time) \n", FA_SLOT);

      fa125DataSuppressTriggerTime(FA_SLOT, fa125[FA_SLOT].data_format);


    }

  if(NFADC_125 > 0)
    {
      fa125GStatus(1);
    }


  /***************************************
   *   SD SETUP
   ***************************************/
  printf("fa125_init::  call sdInit() \n");
  stat = sdInit(0);		/*  Initialize the SD library */


  printf("SD fa125ScanMask() =   0x%x stat=%d \n", fa125ScanMask(), stat);

  if(stat == OK)
    {
      sdSetActiveVmeSlots(fa125ScanMask());
    }
  else
    {
      daLogMsg("ERROR", "sdInit failed \n");
      return -1;
    }

  sdStatus();

  return (0);

}


int
fa125_prestart()
{

  int stat;
  int islot;

  fa125ResetToken(0);

  /* FADC Perform some resets, status */
  for(islot = 0; islot < NFADC_125; islot++)
    {
      FA_SLOT = fa125Slot(islot);

      //    faSetClkSource(FA_SLOT,2);
      //      faClear(FA_SLOT);

      //fa125ResetToken(FA_SLOT);
      //fa125ResetTriggerCount(FA_SLOT);

      fa125Enable(FA_SLOT);
      fa125Status(FA_SLOT, 1);
      fa125PrintThreshold(FA_SLOT);
      fa125PrintTimingThresholds(FA_SLOT);
    }

  return (0);

}

int
fa125_go()
{

  int islot;

  blockLevel = tiGetCurrentBlockLevel();

  for(islot = 0; islot < NFADC_125; islot++)
    {
      fa125SetBlocklevel(fa125Slot(islot), blockLevel);
      printf("Set BlockLevel=%d for slot=%d\n", blockLevel, fa125Slot(islot));

      // printf("fa125_prestart():: Reset  = %d \n",fa125Slot(islot));
      // fa125Reset(fa125Slot(islot), 0);   //-- !!!!
    }

  sdStatus(0);

  return (0);

}
