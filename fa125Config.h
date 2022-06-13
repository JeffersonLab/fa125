#pragma once

#ifndef STRLEN
#define STRLEN 250
#endif

#ifndef MAX_FADC125_CH
#define MAX_FADC125_CH     72
#endif

const int FA125_NPEAK_MAX   = 63;

const int FA125_IE_MAX   = 1023;

const int FA125_IBIT_MAX = 7;
const int FA125_ABIT_MAX = 3;
const int FA125_PBIT_MAX = 3;

const int FA125_PG_MAX   = 7;
const int FA125_P1_MAX   = 7;
const int FA125_P2_MAX   = 7;

const int FA125_TH_MAX   = 511;
const int FA125_TL_MAX   = 255;

/*
 *
 *  FA125 Configuration Structure
 *    - imported from Hall D jun2022
 *
 */

typedef struct {
  int  group;
  int  f_rev;
  int  b_rev;
  int  p_rev;
  int  b_ID;
  char SerNum[80];

  char crate[80];

  char conf_common_dir[STRLEN];
  char conf_common_ver[STRLEN];

  char conf_user_dir[STRLEN];
  char conf_user_ver[STRLEN];

  int level;


  int          mode;
  unsigned int winOffset;
  unsigned int winWidth;
  unsigned int nsb;
  unsigned int nsa;
  unsigned int npeak;

  unsigned int PL;
  /* unsigned int NW;   This handled by legacy "winWidth" above" */
  /* unsigned int NPK;  This handled by legacy "npeak" above" */
  unsigned int P1;
  unsigned int P2;
  unsigned int PG;
  unsigned int IE;
  unsigned int H;
  unsigned int TH;
  unsigned int TL;
  unsigned int IBIT;
  unsigned int ABIT;
  unsigned int PBIT;

  unsigned int mask[4];

  unsigned int ch_disable[3];

  unsigned int dac[MAX_FADC125_CH];

  unsigned int TL_CH[MAX_FADC125_CH];
  unsigned int TH_CH[MAX_FADC125_CH];


  unsigned int read_thr[MAX_FADC125_CH];
  float bl[MAX_FADC125_CH];
  float sig[MAX_FADC125_CH];

  float thr_fact;

  /* Busy and stop processing conditions */
  unsigned int busy;
  unsigned int stop;

  unsigned int data_format;

} FADC125_CONF;
