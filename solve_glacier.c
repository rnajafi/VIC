#include <stdio.h>
#include <stdlib.h>
#include <vicNl.h>

double solve_glacier(double LongUnderOut,       // LW from understory
      double               Tgrnd,               // glacier slab temperature
      double               air_temp,            // air temperature
      double               mu,
      double               prec,
      double               wind_h,
      double              *AlbedoUnder,
      double              *Le,
      double              *LongUnderIn,         // surface incoming LW
      double              *NetLongSnow,         // net LW at glacier surface
      double              *NetShortGrnd,        // net SW reaching ground
      double              *NetShortSnow,        // net SW at glaciersurface
      double              *ShortUnderIn,        // surface incoming SW
      double              *Torg_snow,
      VegConditions       &aero_resist,
      AeroResistUsed      &aero_resist_used,
      VegConditions       &displacement,
      double              *melt_energy,
      double              *out_prec,
      double              *out_rain,
      double              *out_snow,
      double              *ppt,
      double              *rainfall,
      VegConditions       &ref_height,
      VegConditions       &roughness,
      double              *snowfall,
      VegConditions       &wind_speed,
      int                  Nveg,
      int                  iveg,
      int                  band,
      int                  dt,
      int                  rec,
      int                  hidx,
      VegConditions::VegetationConditions &UnderStory,
      const dmy_struct    *dmy,
      atmos_data_struct   *atmos,
      energy_bal_struct   *energy,
      glac_data_struct   *glacier,
      const soil_con_struct* soil,
      const ProgramState *state) {
/*********************************************************************

  This routine was written to handle the various calls and data
  handling needed to solve the various components of the new VIC
  glacier code for both the full_energy and water_balance models.

  Returns snow, veg_var, and energy variables for each elevation
  band.  Variable ppt[] is defined for elevation bands with snow.

*********************************************************************/

  int                 ErrorFlag;
  double              ShortOverIn;
  double              melt;
  double              tmp_grnd_flux;
  double              store_snowfall;
  double              tmp_ref_height;
  double              density;
  double              longwave;
  double              pressure;
  double              shortwave;
  double              vp;
  double              vpd;

  density   = atmos->density[hidx];
  longwave  = atmos->longwave[hidx];
  pressure  = atmos->pressure[hidx];
  shortwave = atmos->shortwave[hidx];
  vp        = atmos->vp[hidx];
  vpd       = atmos->vpd[hidx];

  /* initialize moisture variables */
  melt     = 0.;
  ppt[WET] = 0.;
  ppt[DRY] = 0.;

  /* initialize storage for energy consumed in changing snowpack
     cover fraction */
  (*melt_energy)     = 0.;

  /** Compute latent heats **/
  (*Le) = (2.501e6 - 0.002361e6 * air_temp);

  /* initialize glacier surface radiation inputs */
  (*ShortUnderIn) = shortwave;
  (*LongUnderIn)  = longwave;

   energy->NetLongOver = 0;
   energy->LongOverIn  = 0;

   UnderStory = VegConditions::SNOW_COVERED_CASE;         /* ground snow is present or accumulating during time step */

   /** compute net shortwave radiation **/
   (*AlbedoUnder) = soil->GLAC_ALBEDO;
   (*NetShortSnow) = (1.0 - *AlbedoUnder) * (*ShortUnderIn);

   /** Call glacier ablation algorithm **/
   ErrorFlag = glacier_melt((*Le), (*NetShortSnow), Tgrnd,
    roughness, aero_resist[UnderStory], aero_resist_used,
    air_temp, (double)dt * SECPHOUR, density,
    displacement[UnderStory],
    *LongUnderIn, pressure, rainfall[WET], vp, vpd,
    wind_speed[UnderStory], ref_height[UnderStory],
    NetLongSnow, Torg_snow, &melt, &energy->error,
    &energy->advection,
    &energy->deltaCC_glac, &energy->grnd_flux, &energy->latent,
    &energy->latent_sub,
    &energy->sensible,
    rec, iveg, band, glacier, soil, state);
   if ( ErrorFlag == ERROR ) return ( ERROR );

   // store melt water and rainfall
   ppt[WET] = (melt + rainfall[WET]);

   // store glacier albedo
   energy->AlbedoUnder = *AlbedoUnder;

   rainfall[WET] = 0; /* all rain has been added to the glacier */

  energy->melt_energy = 0.;

  return(melt);

}



