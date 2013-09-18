#include <stdio.h>
#include <stdlib.h>
#include <vicNl.h>
 
static char vcid[] = "$Id$";

void write_atmosdata(atmos_data_struct *atmos, int nrecs, ProgramState* state)
/**********************************************************************
	write_atmosdata		Dag Lohmann	Januray 1996

  This routine writes atmospheric data to the screen.

  Modifications:
    28-Aug-99 Changed to reflect the new atmos_data_struct.     Bart Nijssen
    07-May-04 No longer close the state->debug file, since the next cell
	      must write to it.					TJB
**********************************************************************/
{
#if LINK_DEBUG
  int i;
  int j;

  /*  first write all the SNOW_STEP data  - only write if the modelstep !=
      SNOWSTEP */
  if (NR > 0) {
    for (i = 0; i < nrecs; i++) {
      for (j = 0; j < NF; j++) {
	fprintf(state->debug.fg_snowstep_atmos,"%d\t%d",  i, j);
	fprintf(state->debug.fg_snowstep_atmos,"\t%f", atmos[i].prec[j]);
	fprintf(state->debug.fg_snowstep_atmos,"\t%f", atmos[i].air_temp[j]);
	fprintf(state->debug.fg_snowstep_atmos,"\t%f", atmos[i].wind[j]);
	fprintf(state->debug.fg_snowstep_atmos,"\t%f", atmos[i].vpd[j]);
	fprintf(state->debug.fg_snowstep_atmos,"\t%f", atmos[i].vp[j]);
	fprintf(state->debug.fg_snowstep_atmos,"\t%f", atmos[i].pressure[j]);
	fprintf(state->debug.fg_snowstep_atmos,"\t%f", atmos[i].density[j]);
	fprintf(state->debug.fg_snowstep_atmos,"\t%f", atmos[i].shortwave[j]);
	fprintf(state->debug.fg_snowstep_atmos,"\t%f", atmos[i].longwave[j]);
	fprintf(state->debug.fg_snowstep_atmos,"\n");
      }
    }
  /* don't close the state->debug output file, as we need to write to it for the next cell as well */
/*  fclose(state->debug.fg_snowstep_atmos);*/
  }
  
  /* then write all the dt data */
  for (i = 0; i < nrecs; i++) {
    fprintf(state->debug.fg_modelstep_atmos,"%d",  i);
    fprintf(state->debug.fg_modelstep_atmos,"\t%f", atmos[i].prec[NR]);
    fprintf(state->debug.fg_modelstep_atmos,"\t%f", atmos[i].air_temp[NR]);
    fprintf(state->debug.fg_modelstep_atmos,"\t%f", atmos[i].wind[NR]);
    fprintf(state->debug.fg_modelstep_atmos,"\t%f", atmos[i].vpd[NR]);
    fprintf(state->debug.fg_modelstep_atmos,"\t%f", atmos[i].vp[NR]);
    fprintf(state->debug.fg_modelstep_atmos,"\t%f", atmos[i].pressure[NR]);
    fprintf(state->debug.fg_modelstep_atmos,"\t%f", atmos[i].density[NR]);
    fprintf(state->debug.fg_modelstep_atmos,"\t%f", atmos[i].shortwave[NR]);
    fprintf(state->debug.fg_modelstep_atmos,"\t%f", atmos[i].longwave[NR]);
    fprintf(state->debug.fg_modelstep_atmos,"\n");
  }
  fclose(state->debug.fg_modelstep_atmos);
#endif

}


