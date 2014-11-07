/**********************************************************************
                        Global Variables

  NOTE: This file exists because global variables that are shared among
        files via the "extern" statement must be initially declared
        (without the word "extern") ONLY once.  Currently, vicNl_def.h
        is included (via vicNl.h) in every .c file, meaning that any
        declarations in vicNl_def.h end up happening multiple times
        (once per .c file).  Thus, these "extern" variables cannot be
        declared in vicNl_def.h.  This is not a problem for #define
        statements and typedef statements, which is what vicNl_def.h
        is primarily composed of.

  $Id$

  29-Oct-03 Added version string and removed unused options from
	    optstring.							TJB
  2009-Jun-09 Added definitions of reference landcover types, used
	      mainly for pot_evap computations but also defines the
	      characteristics of bare soil.				TJB
**********************************************************************/

#ifndef GLOBAL_H
#define GLOBAL_H

#if QUICK_FS
const double temps[] = { -1.e-5, -0.075, -0.20, -0.50, -1.00, -2.50, -5, -10 };
#endif

  /**************************************************************************
    Define some reference landcover types that always exist regardless
    of the contents of the library (mainly for potential evap calculations):
    Non-natural:
      satsoil = saturated bare soil
      h2osurf = open water surface (deep enough to have albedo of 0.08)
      short   = short reference crop (grass)
      tall    = tall reference crop (alfalfa)
    Natural:
      natveg  = current vegetation
      vegnocr = current vegetation with canopy resistance set to 0
    NOTE: these are external variables, declared in vicNl_def.h.
    NOTE2: bare soil roughness and displacement will be overwritten by the
           values found in the soil parameter file; bare soil wind_h will
	   be overwritten by the value specified in the global param file.
  **************************************************************************/


  /* One element for each non-natural PET type */
  const char   ref_veg_over[]        = { 0, 0, 0, 0 };
  const double ref_veg_rarc[]        = { 0.0, 0.0, 25, 25 };
  const double ref_veg_rmin[]        = { 0.0, 0.0, 100, 100 };
  const double ref_veg_lai[]         = { 1.0, 1.0, 2.88, 4.45 };
  const double ref_veg_albedo[]      = { BARE_SOIL_ALBEDO, H2O_SURF_ALBEDO, 0.23, 0.23 };
  const double ref_veg_rough[]       = { 0.001, 0.001, 0.0148, 0.0615 };
  const double ref_veg_displ[]       = { 0.0054, 0.0054, 0.08, 0.3333 };
  const double ref_veg_wind_h[]      = { 10.0, 10.0, 10.0, 10.0 };
  const double ref_veg_RGL[]         = { 0.0, 0.0, 100, 100 };
  const double ref_veg_rad_atten[]   = { 0.0, 0.0, 0.0, 0.0 };
  const double ref_veg_wind_atten[]  = { 0.0, 0.0, 0.0, 0.0 };
  const double ref_veg_trunk_ratio[] = { 0.0, 0.0, 0.0, 0.0 };
  /* One element for each PET type (non-natural or natural) */
  const char ref_veg_ref_crop[] = { FALSE, FALSE, TRUE, TRUE, FALSE, FALSE };

#endif // GLOBAL_H
