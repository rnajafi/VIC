#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "GlacierEnergyBalance.h"
#include "vicNl.h"

double ErrorPrintGlacierEnergyBalance(double TSurf, int rec, int iveg, int band,
    double Dt, double Ra, double Displacement, double Z, double Z0,
    double AirDens, double EactAir, double LongIn, double Lv, double Press,
    double Rain, double ShortRad, double Vpd, double Wind, double OldTSurf,
    double Tair, double TGrnd, double *AdvectedEnergy,
    double *AdvectedSensibleHeat, double *DeltaColdContent,
    double *DeltaPackColdContent, //TODO: not necessary?
    double *GroundFlux, double *LatentHeat, double *LatentHeatSub,
    double *NetLong,
    double *RefreezeEnergy,     //TODO: not necessary?
    double *SensibleHeat, double *VaporMassFlux, double *BlowingMassFlux,
    double *SurfaceMassFlux, char *ErrorString);

/*****************************************************************************
  Function name: glacier_melt()

  Purpose      : Calculate glacier accumulation and melt using an energy balance
                 approach for a two layer snow model

  Required     :
    double delta_t               - Model timestep (secs)
    double z2           - Reference height (m)
    double displacement          - Displacement height (m)
    double aero_resist           - Aerodynamic resistance (uncorrected for
                                   stability) (s/m)
    double *aero_resist_used     - Aerodynamic resistance (corrected for
                                   stability) (s/m)
    double atmos->density        - Density of air (kg/m3)
    double atmos->vp             - Actual vapor pressure of air (Pa)
    double Le           - Latent heat of vaporization (J/kg3)
    double atmos->net_short      - Net exchange of shortwave radiation (W/m2)
    double atmos->longwave       - Incoming long wave radiation (W/m2)
    double atmos->pressure       - Air pressure (Pa)
    double RainFall              - Amount of rain (m)
    double Snowfall              - Amount of snow (m)
    double atmos->air_temp       - Air temperature (C)
    double atmos->vpd            - Vapor pressure deficit (Pa)
    double wind                  - Wind speed (m/s)
    double snow->pack_water      - Liquid water content of snow pack
    double snow->surf_water  - Liquid water content of surface layer
    double snow->swq             - Snow water equivalent at current pixel (m)
    double snow->vapor_flux;     - Mass flux of water vapor to or from the
                                   intercepted snow (m/time step)
    double snow->pack_temp       - Temperature of snow pack (C)
    double snow->surf_temp       - Temperature of snow pack surface layer (C)
    double snow->melt_energy     - Energy used for melting and heating of
                                   snow pack (W/m2)

  Modifies     :
    double *melt                 - Amount of snowpack outflow (initially is m, but converted to mm for output)
    double snow->pack_water      - Liquid water content of snow pack
    double snow->surf_water  - Liquid water content of surface layer
    double snow->swq             - Snow water equivalent at current pixel (m)
    double snow->vapor_flux;     - Mass flux of water vapor to or from the
                                   intercepted snow (m/time step)
    double snow->pack_temp       - Temperature of snow pack (C)
    double snow->surf_temp       - Temperature of snow pack surface layer (C)
    double snow->melt_energy     - Energy used for melting and heating of
                                   snow pack (W/m2)*/

int glacier_melt(double Le,
    double NetShort,  // net SW at absorbed by glacier
    double Tgrnd,
    double *Z0,  // roughness
    double aero_resist,  // aerodynamic resistance
    double *aero_resist_used,  // stability-corrected aerodynamic resistance
    double air_temp,  // air temperature
    double delta_t,  // time step in secs
    double density,  // atmospheric density
    double displacement,  // surface displacement
    double grnd_flux,  // ground heat flux
    double LongIn,  // incoming longwave radiation
    double pressure, double rainfall,
    double vp,
    double vpd,
    double wind,
    double z2,
    double *NetLong,
    double *OldTSurf,
    double *melt,
    double *save_Qnet,
    double *save_advected_sensible,
    double *save_advection,
    double *save_deltaCC,
    double *save_grnd_flux,
    double *save_latent,
    double *save_latent_sub,
    double *save_sensible,
    int rec,
    int iveg,
    int               band,
    glac_data_struct *glacier,
    const ProgramState *state)
{
  double error;
  double MassBalanceError;       /* Mass balance error (m) */
  double Qnet;                   /* Net energy exchange at the surface (W/m2) */
  double GlacMelt;               /* Amount of ice melt during time interval (m water equivalent) */
  double GlacCC;                 /* Cold content of glacier surface layer (J) */
  double RainFall;
  double advection;
  double deltaCC;
  double latent_heat;
  double latent_heat_sub;
  double sensible_heat;
  double advected_sensible_heat;
  double melt_energy = 0.;

  char ErrorString[MAXSTRING];

  /* SnowFall = snowfall / 1000.;*/ /* convet to m */
  RainFall = rainfall / 1000.; /* convet to m */

  (*OldTSurf) = glacier->surf_temp;

  /* Calculate the surface energy balance for surf_temp = 0.0 */
  GlacierEnergyBalance glacierEnergy = GlacierEnergyBalance(delta_t, aero_resist, aero_resist_used,
         displacement, z2, Z0,
         density, vp, LongIn, Le, pressure,
         RainFall, NetShort, vpd,
         wind, (*OldTSurf),
         GLAC_SURF_THICK, ice_density,
         GLAC_SURF_WE, Tgrnd,
         &advection, &advected_sensible_heat,
         &deltaCC,
         &grnd_flux, &latent_heat,
         &latent_heat_sub, NetLong,
         &RefreezeEnergy, &sensible_heat,
         &glacier->vapor_flux, 0.,
         &glacier->surface_flux);
  Qnet = glacierEnergy.calculate((double)0.0);

  /* If Qnet == 0.0, then set the surface temperature to 0.0 */
  if (abs(Qnet) < 2e-7) {
    glacier->surf_temp = 0.;
    melt_energy = NetShort + NetLong + sensible_heat + advected_sensible_heat
        + latent_heat + latent_heat_sub
        - ice_density * CH_ICE * (glacier->surf_temp - (*OldTSurf)) / delta_t;
    GlacMelt = melt_energy / (Lf * RHO_W) * delta_t;
    GlacCC = 0.;
  }

  /* Else, GlacierEnergyBalance(T=0.0) <= 0.0 */
  else {
    /* Calculate surface layer temperature using "Brent method" */
    GlacierEnergyBalance glacierIterative(delta_t, aero_resist,
        aero_resist_used, displacement, z2, Z0, density, vp, LongIn, Le,
        pressure, RainFall, NetShort, vpd, wind, (*OldTSurf), GLAC_SURF_THICK,
        ice_density, GLAC_SURF_WE, Tgrnd, &advection, &advected_sensible_heat,
        &deltaCC, &grnd_flux, &latent_heat, &latent_heat_sub, NetLong,
        &RefreezeEnergy, &sensible_heat, &snow->vapor_flux, 0.,
        &snow->surface_flux);

    glacier->surf_temp = glacierIterative.root_brent(
        (double) (glacier->surf_temp - SNOW_DT),
        (double) (glacier->surf_temp + SNOW_DT), ErrorString);

    if (glacier->surf_temp <= -998) {
      if (state->options.TFALLBACK) {
        glacier->surf_temp = *OldTSurf;
        glacier->surf_temp_fbflag = 1;
        glacier->surf_temp_fbcount++;
      } else {
        error = ErrorPrintGlacierEnergyBalance(glacier->surf_temp, rec, iveg, band,
            delta_t, aero_resist, aero_resist_used, displacement, z2, Z0,
            density, vp, LongIn, Le, pressure, RainFall, NetShort, vpd, wind,
            (*OldTSurf), GLAC_SURF_THICK, ice_density, GLAC_SURF_WE, Tgrnd,
            &advection, &advected_sensible_heat, &deltaCC, &grnd_flux,
            &latent_heat, &latent_heat_sub, NetLong, &RefreezeEnergy,
            &sensible_heat, &snow->vapor_flux, 0., &snow->surface_flux);
        return (ERROR);
      }
    }

    if (glacier->surf_temp > -998) {
      GlacierEnergyBalance glacierEnergy(delta_t, aero_resist,
          aero_resist_used, displacement, z2, Z0, density, vp, LongIn, Le,
          pressure, RainFall, NetShort, vpd, wind, (*OldTSurf), GLAC_SURF_THICK,
          ice_density, GLAC_SURF_WE, Tgrnd, &advection, &advected_sensible_heat,
          &deltaCC, &grnd_flux, &latent_heat, &latent_heat_sub, NetLong,
          &RefreezeEnergy, &sensible_heat, &glacier->vapor_flux, 0.,
          &glacier->surface_flux);

      Qnet = glacierEnergy.calculate(glacier->surf_temp);

      /* since we iterated, the surface layer is below freezing and no snowmelt */
      GlacMelt = 0.0;
      GlacCC = CH_ICE * glacier->surf_temp * GLAC_SURF_WE;

    }
  }

  melt[0] = GlacMelt;

  /* Mass balance test */
  /* MassBalanceError = (InitialSwq - snow->swq) + (RainFall + SnowFall)
   - melt[0] + snow->vapor_flux; */

  /*  printf("%d %d %g\n", y, x, MassBalanceError);*/

  melt[0] *= 1000.; /* converts back to mm */
  /* glacier->mass_error         = MassBalanceError; */
  glacier->cold_content = GlacCC;
  glacier->vapor_flux *= -1.;
  *save_advection = advection;
  *save_deltaCC = deltaCC;
  *save_grnd_flux = grnd_flux;
  *save_latent = latent_heat;
  *save_latent_sub = latent_heat_sub;
  *save_sensible = sensible_heat;
  *save_advected_sensible = advected_sensible_heat;
  *save_Qnet = Qnet;

  return (0);
}

double ErrorPrintGlacierEnergyBalance(double TSurf,
    /* General Model Parameters */
    int rec,
    int iveg,
    int band,
    double Dt,                      /* Model time step (sec) */
    /* Vegetation Parameters */
    double Ra,                      /* Aerodynamic resistance (s/m) */
    double Displacement,            /* Displacement height (m) */
    double Z,                       /* Reference height (m) */
    double Z0,                      /* surface roughness height (m) */
    /* Atmospheric Forcing Variables */
    double AirDens,                 /* Density of air (kg/m3) */
    double EactAir,                 /* Actual vapor pressure of air (Pa) */
    double LongIn,                  /* Incoming longwave radiation (W/m2) */
    double Lv,                      /* Latent heat of vaporization (J/kg3) */
    double Press,                   /* Air pressure (Pa) */
    double Rain,                    /* Rain fall (m/timestep) */
    double ShortRad,                /* Net incident shortwave radiation (W/m2) */
    double Vpd,                     /* Vapor pressure deficit (Pa) */
    double Wind,                    /* Wind speed (m/s) */
    /* Snowpack Variables */
    double OldTSurf,                /* Surface temperature during previous timestep */
    /* Energy Balance Components */
    double Tair,                    /* Air temperature (C) */
    double TGrnd,                   /* Temperature of glacier slab (C) */
    double *AdvectedEnergy,         /* Energy advected by precipitation (W/m2) */
    double *AdvectedSensibleHeat,   /* Sensible heat advected from snow-free area into snow covered area (W/m^2) */
    double *DeltaColdContent,       /* Change in cold content of glacier surface layer (W/m2) */
    double *DeltaPackColdContent, //TODO: not necessary?
    double *GroundFlux,             /* Ground Heat Flux (W/m2) */
    double *LatentHeat,             /* Latent heat exchange at surface (W/m2) */
    double *LatentHeatSub,          /* Latent heat of sub exchange at surface (W/m2) */
    double *NetLong,                /* Net longwave radiation at glacier surface (W/m^2) */
    double *RefreezeEnergy,     //TODO: not necessary?
    double *SensibleHeat,           /* Sensible heat exchange at surface (W/m2) */
    double *VaporMassFlux,          /* Mass flux of water vapor to or from glacier surface */
    double *BlowingMassFlux,        /* Mass flux of water vapor from blowing snow snow */
    double *SurfaceMassFlux,        /* Total mass flux of water vapor from glacier */
    char *ErrorString)
{

  /* print variables */
  fprintf(stderr, "%s", ErrorString);
  fprintf(stderr, "ERROR: glacier_melt failed to converge to a solution in root_brent.  Variable values will be dumped to the screen, check for invalid values.\n");

  /* general model terms */
  fprintf(stderr, "rec = %i\n", rec);
  fprintf(stderr, "iveg = %i\n", iveg);
  fprintf(stderr, "band = %i\n", band);
  fprintf(stderr, "Dt = %f\n",Dt);

  /* land surface parameters */
  fprintf(stderr,"Ra = %f\n",Ra);
  fprintf(stderr,"Displacement = %f\n",Displacement);
  fprintf(stderr,"Z = %f\n",Z);
  fprintf(stderr,"Z0 = %f\n",Z0);

  /* meteorological terms */
  fprintf(stderr,"AirDens = %f\n",AirDens);
  fprintf(stderr,"EactAir = %f\n",EactAir);
  fprintf(stderr,"LongIn = %f\n",LongIn);
  fprintf(stderr,"Lv = %f\n",Lv);
  fprintf(stderr,"Press = %f\n",Press);
  fprintf(stderr,"Rain = %f\n",Rain);
  fprintf(stderr,"ShortRad = %f\n",ShortRad);
  fprintf(stderr,"Vpd = %f\n",Vpd);
  fprintf(stderr,"Wind = %f\n",Wind);

  /* glacer terms */
  fprintf(stderr,"OldTSurf = %f\n",OldTSurf);
  fprintf(stderr,"Tair = %f\n",Tair);
  fprintf(stderr,"TGrnd = %f\n",TGrnd);
  fprintf(stderr,"AdvectedEnergy = %f\n",AdvectedEnergy[0]);
  fprintf(stderr,"AdvectedSensibleHeat = %f\n",AdvectedSensibleHeat[0]);
  fprintf(stderr,"DeltaColdContent = %f\n",DeltaColdContent[0]);
  fprintf(stderr,"DeltaColdPackContent = %f\n", DeltaPackColdContent[0]);
  fprintf(stderr,"GroundFlux = %f\n",GroundFlux[0]);
  fprintf(stderr,"LatentHeat = %f\n",LatentHeat[0]);
  fprintf(stderr,"LatentHeatSub = %f\n",LatentHeatSub[0]);
  fprintf(stderr,"NetLong = %f\n",NetLong[0]);
  fprintf(stderr,"RefreezeEnergy = %f\n", RefreezeEnergy[0]);
  fprintf(stderr,"SensibleHeat = %f\n",SensibleHeat[0]);
  fprintf(stderr,"VaporMassFlux = %f\n",VaporMassFlux[0]);
  fprintf(stderr,"BlowingMassFlux = %f\n",BlowingMassFlux[0]);
  fprintf(stderr,"SurfaceMassFlux = %f\n",SurfaceMassFlux[0]);

  fprintf(stderr,"Finished dumping glacier_melt variables.\nTry increasing SNOW_DT to get model to complete cell.\nThen check output for instabilities.\n");

  return(ERROR);

}
