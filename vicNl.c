#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vicNl.h>
#include <global.h>
#include <assert.h>
#include <omp.h>

static char vcid[] = "$Id$";

cell_info_struct* readForcingData(int &ncells,
    filep_struct filep, filenames_struct filenames,
    dmy_struct* dmy, ProgramState& state);

void runModel(const int ncells, cell_info_struct * cell_data_structs,
    filep_struct filep, int num_veg_types, filenames_struct filenames,
    out_data_file_struct* out_data_files_template, out_data_struct* out_data,
    dmy_struct* dmy, const ProgramState* state);

int initializeCell(cell_info_struct* cell_data_structs, const int cellidx,
    filep_struct filep, dmy_struct* dmy, filenames_struct filenames,
    int num_veg_types, const ProgramState* state);

int main(int argc, char *argv[])
/**********************************************************************
	vicNl.c		Dag Lohmann		January 1996

  This program controls file I/O and variable initialization as well as
  being the primary driver for the model.

  For details about variables, input files and subroutines check:
	http://ce.washington.edu/~hydro/Lettenmaier/Models/VIC/VIC_home.html

  UNITS: unless otherwise marked:
         all water balance components are in mm
	 all energy balance components are in mks
	 depths, and lengths are in m

  modifications:
  1997-98 Model was updated from simple 2 layer water balance to 
          an extension of the full energy and water balance 3 layer
	  model.                                                  KAC
  02-27-01 added controls for lake model                          KAC
  11-18-02 Updated storage of lake water for water balance 
           calculations.                                          LCB
  03-12-03 Modifed to add AboveTreeLine to soil_con_struct so that
           the model can make use of the computed treeline.     KAC
  04-10-03 Modified to initialize storm parameters using the state
           file.                                                KAC
  04-10-03 Modified to start the model by skipping records until the
           state file date is found.  This replaces the previous method
           of modifying the global file start date, which can change 
           the interpolation of atmospheric forcing data.        KAC
  04-15-03 Modified to store wet and dry fractions when intializing 
           water balance storage.  This accounts for changes in model
           state initialization, which now stores wet and dry fractions
           rather than just averagedvalues.                      KAC
  29-Oct-03 Modified the version display banner to print the version
	    string defined in global.h.					TJB
  01-Nov-04 Updated arglist for make_dist_prcp(), as part of fix for
	    QUICK_FLUX state file compatibility.			TJB
  02-Nov-04 Updated arglist for read_lakeparam(), as part of fix for
	    lake fraction readjustment.					TJB
  2005-Apr-13 OUTPUT_FORCE option now calls close_files().		TJB
  2006-Sep-23 Implemented flexible output configuration; uses the new
              out_data, out_data_files, and save_data structures.	TJB
  2006-Oct-16 Merged infiles and outfiles structs into filep_struct;
	      This included merging builtnames into filenames.		TJB
  2006-Nov-07 Removed LAKE_MODEL option.				TJB
  2006-Nov-07 Changed statefile to init_state in call to
	      check_state_file().					TJB
  2007-Jan-15 Added PRT_HEADER option; added call to
	      write_header().						TJB
  2007-Apr-04 Added option to continue run after a cell fails. 		GCT/KAC
  2007-Apr-21 Added calls to free_dmy(), free_out_data_files(),
	      free_out_data(), and free_veglib().  Added closing of
	      all parameter files.					TJB
  2007-Aug-21 Return ErrorFlag from initialize_model_state.		JCA
  2007-Sep-14 Excluded calls to free_veglib() and closing of parameter
	      files other than the soil param file for the case
	      when OUTPUT_FORCE=TRUE.					TJB
  2007-Nov-06 Moved computation of cell_area from read_lakeparam() to
	      read_soilparam() and read_soilparam_arc().		TJB
  2008-May-05 Added prcp fraction (mu) to initial water storage
	      computation.  This solves water balance errors for the
	      case where DIST_PRCP is TRUE.				TJB
  2009-Jan-16 Added soil_con.avgJulyAirTemp to argument list of
	      initialize_atmos().					TJB
  2009-Jun-09 Modified to use extension of veg_lib structure to contain
	      bare soil information.					TJB
  2009-Jul-07 Added soil_con.BandElev[] to read_snowband() arg list.	TJB
  2009-Jul-31 Replaced references to N+1st veg tile with references
	      to index of lake/wetland tile.				TJB
  2009-Sep-28 Replaced initial water/energy storage computations and
	      calls to calc_water_balance_error/calc_energy_balance_error
	      with an initial call to put_data.  Modified the call to
	      read_snowband().						TJB
  2009-Dec-11 Removed save_data structure from argument list of 
	      initialize_model_state().					TJB
  2010-Mar-31 Added cell_area to initialize_atmos().			TJB
  2010-Apr-28 Removed individual soil_con variables from argument list
	      of initialize_atmos() and replaced with *soil_con.	TJB
  2010-Nov-10 Added closing of state files.				TJB
  2011-Jan-04 Made read_soilparam_arc() a sub-function of
	      read_soilparam().						TJB
**********************************************************************/
{

  /** Read Model Options **/
  ProgramState state;
  state.initialize_global();

  filenames_struct filenames;
  cmd_proc(argc, argv, filenames.global, &state);


#if VERBOSE
  state.display_current_settings(DISP_VERSION, (filenames_struct*) NULL);
#endif
  /** Read Global Control File **/
  state.init_global_param(&filenames, filenames.global);
  /** Set up output data structures **/
  out_data_struct *out_data = create_output_list(&state);
  out_data_file_struct *out_data_files = set_output_defaults(out_data, &state);
  parse_output_info(filenames.global, out_data_files, out_data, &state);

  /** Check and Open Files **/
  filep_struct filep = get_files(&filenames, &state);

#if !OUTPUT_FORCE
#if LINK_DEBUG
  state.open_debug();
#endif
  /** Read Vegetation Library File **/
  int num_veg_types = 0;
  state.veg_lib = read_veglib(filep.veglib, &num_veg_types, state.options.LAI_SRC);
#endif // !OUTPUT_FORCE

  /** Make Date Data Structure **/
  dmy_struct* dmy = make_dmy(&state.global_param, &state);
  /** Initial state **/
#if !OUTPUT_FORCE
  if (state.options.INIT_STATE)
    filep.init_state = check_state_file(filenames.init_state, &state);
  /** open state file if model state is to be saved **/
  if (state.options.SAVE_STATE && strcmp(filenames.statefile, "NONE") != 0)
    filep.statefile = open_state_file(&state.global_param, filenames, state.options.Nlayer,
        state.options.Nnode, state.options.BINARY_STATE_FILE);
  else
    filep.statefile = NULL;
#endif // !OUTPUT_FORCE

  int ncells = 0;
  cell_info_struct *cell_data_structs = readForcingData(ncells, filep, filenames, dmy, state);

  runModel(ncells, cell_data_structs, filep, num_veg_types, filenames, out_data_files, out_data, dmy, &state);

  /** cleanup **/
  free_dmy(&dmy);
  free_out_data_files(out_data_files, &state);
  free_out_data(&out_data);
#if !OUTPUT_FORCE
  free_veglib(&state.veg_lib);
#endif /* !OUTPUT_FORCE */
  fclose(filep.soilparam);
#if !OUTPUT_FORCE
  fclose(filep.vegparam);
  fclose(filep.veglib);
  if (state.options.SNOW_BAND > 1)
    fclose(filep.snowband);
  if (state.options.LAKES)
    fclose(filep.lakeparam);
  if (state.options.INIT_STATE)
    fclose(filep.init_state);
  if (state.options.SAVE_STATE && strcmp(filenames.statefile, "NONE") != 0)
    fclose(filep.statefile);

#endif /* !OUTPUT_FORCE */

  return EXIT_SUCCESS;
} /* End Main Program */

cell_info_struct* readForcingData(int &ncells,
    filep_struct filep, filenames_struct filenames,
    dmy_struct* dmy, ProgramState& state) {

  /*****************************************
   * Read soil for all "active" grid cells *
   *****************************************/
  char done_reading_forcings = FALSE, is_valid_soil_cell;
  ncells = 0; /* zero-based */
  int nallocatedcells = 10; /* arbitrary */
  int current_cell = 0;
  cell_info_struct *cell_data_structs = (cell_info_struct *) malloc(nallocatedcells * sizeof(cell_info_struct));
  assert(cell_data_structs != NULL);

  double *lat = NULL;
  double *lng = NULL;
  int    *cellnum = NULL;
  while (!done_reading_forcings) {
    if (ncells >= (nallocatedcells - 1)) {
      // grows number of allocated cells by 1.4 if out of space
      cell_data_structs = (cell_info_struct *) realloc(cell_data_structs,
          (nallocatedcells = (int) (nallocatedcells * 1.4))
              * sizeof(*cell_data_structs));
      assert(cell_data_structs != NULL);
    }

    if (state.options.ARC_SOIL) {
      assert(0); // presently unsupported; maybe support vector functionality later?
      int  Ncells = 0;  // This will be initialized in read_soilparam_arc
      cell_data_structs[ncells++].soil_con = read_soilparam_arc(filep.soilparam, filenames.soil_dir, &Ncells, &is_valid_soil_cell, current_cell, lat, lng, cellnum, &state);
      current_cell++;
      if (current_cell == Ncells)
        done_reading_forcings = TRUE;
    } else {
      cell_data_structs[ncells++].soil_con = read_soilparam(filep.soilparam,
          filenames.soil_dir, &is_valid_soil_cell, &done_reading_forcings, &state);
    }
    if (!is_valid_soil_cell) {
      --ncells;
      continue;
    }
  }

  return cell_data_structs;
}

int initializeCell(cell_info_struct* cell_data_structs, const int cellidx,
    filep_struct filep, dmy_struct* dmy, filenames_struct filenames,
    int num_veg_types, const ProgramState* state) {

  const int Ndist = state->options.DIST_PRCP ? 2 : 1;

  #if LINK_DEBUG
      if (state->debug.PRT_SOIL)
        write_soilparam(&cell_data_structs[cellidx].soil_con, state);
  #endif

  #if QUICK_FS
      /** Allocate Unfrozen Water Content Table **/
      if(state->options.FROZEN_SOIL) {
        for(int i=0;i<MAX_LAYERS;i++) {
          cell_data_structs[cellidx].soil_con.ufwc_table_layer[i] = (double **)malloc((QUICK_FS_TEMPS+1)*sizeof(double *));
          for(int j=0;j<QUICK_FS_TEMPS+1;j++)
          cell_data_structs[cellidx].soil_con.ufwc_table_layer[i][j] = (double *)malloc(2*sizeof(double));
        }
        for(int i=0;i<MAX_NODES;i++) {
          cell_data_structs[cellidx].soil_con.ufwc_table_node[i] = (double **)malloc((QUICK_FS_TEMPS+1)*sizeof(double *));

          for(int j=0;j<QUICK_FS_TEMPS+1;j++)
          cell_data_structs[cellidx].soil_con.ufwc_table_node[i][j] = (double *)malloc(2*sizeof(double));
        }
      }
  #endif /* QUICK_FS */

  #if !OUTPUT_FORCE
      make_in_files(&filep, &filenames, &cell_data_structs[cellidx].soil_con, state);
      /** Read Grid Cell Vegetation Parameters **/
      cell_data_structs[cellidx].veg_con = read_vegparam(filep.vegparam,
          cell_data_structs[cellidx].soil_con.gridcel, num_veg_types, state);
      calc_root_fractions(cell_data_structs[cellidx].veg_con, &cell_data_structs[cellidx].soil_con, state);
  #if LINK_DEBUG
      if (state->debug.PRT_VEGE)
        write_vegparam(cell_data_structs[cellidx].veg_con, state);
  #endif /* LINK_DEBUG*/

      if (state->options.LAKES)
        cell_data_structs[cellidx].lake_con = read_lakeparam(filep.lakeparam,
            cell_data_structs[cellidx].soil_con, cell_data_structs[cellidx].veg_con, state);

  #endif // !OUTPUT_FORCE

  #if !OUTPUT_FORCE

      /** Read Elevation Band Data if Used **/
      read_snowband(filep.snowband, &cell_data_structs[cellidx].soil_con, state->options.SNOW_BAND);

      /** Make Precipitation Distribution Control Structure **/
      cell_data_structs[cellidx].prcp = make_dist_prcp(cell_data_structs[cellidx].veg_con[0].vegetat_type_num, state->options.Nlayer, state->options.SNOW_BAND);

  #endif // !OUTPUT_FORCE
      /**************************************************
       Initialize Meteological Forcing Values That
       Have not Been Specifically Set
       **************************************************/

  #if VERBOSE
      fprintf(stderr, "Initializing Forcing Data\n");
  #endif /* VERBOSE */

      /** allocate memory for the atmos_data_struct **/
      cell_data_structs[cellidx].atmos = alloc_atmos(state->global_param.nrecs, state->NR);
      initialize_atmos(cell_data_structs[cellidx].atmos, dmy, filep.forcing, filep.forcing_ncid,
          &cell_data_structs[cellidx].soil_con, state);

  #if LINK_DEBUG
      if (state->debug.PRT_ATMOS)
        write_atmosdata(cell_data_structs[cellidx].atmos, state->global_param.nrecs, state);
  #endif

      /**************************************************
       Initialize Energy Balance and Snow Variables
       **************************************************/

  #if VERBOSE
      fprintf(stderr, "Model State Initialization\n");
  #endif /* VERBOSE */
      //TODO: just pass in cell_data_structs[cellidx] not all its members individually
      int ErrorFlag = initialize_model_state(&cell_data_structs[cellidx].prcp, dmy[0], filep,
          cell_data_structs[cellidx].soil_con.gridcel,
          cell_data_structs[cellidx].veg_con[0].vegetat_type_num, state->options.Nnode, Ndist,
          cell_data_structs[cellidx].atmos[0].air_temp[state->NR], &cell_data_structs[cellidx].soil_con, cell_data_structs[cellidx].veg_con,
          cell_data_structs[cellidx].lake_con, &cell_data_structs[cellidx].init_STILL_STORM,
          &cell_data_structs[cellidx].init_DRY_TIME, state);
      if (ErrorFlag == ERROR) {
        if (state->options.CONTINUEONERROR == TRUE) {
          // Handle grid cell solution error
          fprintf(stderr,
              "ERROR: Grid cell %i failed in record %i so the simulation has not finished.  An incomplete output file has been generated, check your inputs before rerunning the simulation.\n",
              cell_data_structs[cellidx].soil_con.gridcel, cellidx);
          return ERROR;
        } else {
          // Else exit program on cell solution error as in previous versions
          sprintf(cell_data_structs[cellidx].ErrStr,
              "ERROR: Grid cell %i failed in record %i so the simulation has ended. Check your inputs before rerunning the simulation.\n",
              cell_data_structs[cellidx].soil_con.gridcel, cellidx);
          vicerror(cell_data_structs[cellidx].ErrStr);
        }
      }

  #if VERBOSE
      fprintf(stderr, "Running Model\n");
  #endif /* VERBOSE */

      return 0;
}

void printThreadInformation() {
  // Obtain thread number
  int tid = omp_get_thread_num();
  // Only master thread does this
  if (tid == 0) {
    printf("Thread %d getting environment info...\n", tid);
    // Get environment information
    int procs = omp_get_num_procs();
    int nthreads = omp_get_num_threads();
    int maxt = omp_get_max_threads();
    int inpar = omp_in_parallel();
    int dynamic = omp_get_dynamic();
    int nested = omp_get_nested();
    // Print environment information
    printf("Number of processors = %d\n", procs);
    printf("Number of threads = %d\n", nthreads);
    printf("Max threads = %d\n", maxt);
    printf("In parallel? = %d\n", inpar);
    printf("Dynamic threads enabled? = %d\n", dynamic);
    printf("Nested parallelism supported? = %d\n", nested);
  }
}

/************************************
 Run Model for all Active Grid Cells
 ************************************/
void runModel(const int ncells, cell_info_struct * cell_data_structs,
    filep_struct filep, int num_veg_types, filenames_struct filenames,
    out_data_file_struct* out_data_files_template, out_data_struct* out_data,
    dmy_struct* dmy, const ProgramState* state) {

  for (int cellidx = 0; cellidx < ncells; cellidx++) {
    //TODO: check error flag here
    int initError = initializeCell(cell_data_structs, cellidx, filep, dmy,
        filenames, num_veg_types, state);
  }

  #pragma omp parallel for
  for (int cellidx = 0; cellidx < ncells; cellidx++) {

    printThreadInformation();

    //int initError = initializeCell(cell_data_structs, cellidx, filep, dmy,
    //        filenames, num_veg_types, state);

    //make local copies of output data which is unique to each cell, this is required if the outer for loop is run in parallel.
    out_data_file_struct* out_data_files = copy_data_file_format(out_data_files_template, state);
    out_data_struct* current_output_data = copy_output_data(out_data, state);
    /** Build Gridded Filenames, and Open **/
    make_out_files(&filep, &filenames, &cell_data_structs[cellidx].soil_con, out_data_files, state);

    if (state->options.PRT_HEADER) {
      /** Write output file headers **/
      write_header(out_data_files, current_output_data, dmy, state);
    }

    //TODO: These error files should not be global like this
    /** Update Error Handling Structure **/
    //state->Error.filep = filep;
    //state->Error.out_data_files = out_data_files;

#if OUTPUT_FORCE
    // If OUTPUT_FORCE is set to TRUE in user_def.h then the full
    // forcing data array is dumped into a new set of files.
    write_forcing_file(cell_data_structs[cellidx].atmos, global_param.nrecs, out_data_files, out_data);
    continue;
#endif

    /** Initialize the storage terms in the water and energy balances **/
    //TODO: check error flag here
    put_data(&cell_data_structs[cellidx], out_data_files, current_output_data, &dmy[0],
        -state->global_param.nrecs, state);

    /******************************************
     Run Model in Grid Cell for all Time Steps
     ******************************************/
    char NEWCELL = TRUE;
    for (int rec = 0; rec < state->global_param.nrecs; rec++) {

      int ErrorFlag = dist_prec(&cell_data_structs[cellidx], dmy, &filep,
          out_data_files, current_output_data, rec, cellidx, NEWCELL, state);

      if (ErrorFlag == ERROR) {
        if (state->options.CONTINUEONERROR == TRUE) {
          // Handle grid cell solution error
          fprintf(stderr,
              "ERROR: Grid cell %i failed in record %i so the simulation has not finished.  An incomplete output file has been generated, check your inputs before rerunning the simulation.\n",
              cell_data_structs[cellidx].soil_con.gridcel, rec);
          break;
        } else {
          // Else exit program on cell solution error as in previous versions
          sprintf(cell_data_structs[cellidx].ErrStr,
              "ERROR: Grid cell %i failed in record %i so the simulation has ended. Check your inputs before rerunning the simulation.\n",
              cell_data_structs[cellidx].soil_con.gridcel, rec);
          vicerror(cell_data_structs[cellidx].ErrStr);
        }
      }

      NEWCELL = FALSE;

    } /* End Rec Loop */


#if QUICK_FS
    if(options.FROZEN_SOIL) {
      for(int i=0;i<MAX_LAYERS;i++) {
        for(int j=0;j<6;j++)
        free((char *)cell_data_structs[cellidx].soil_con.ufwc_table_layer[i][j]);
        free((char *)cell_data_structs[cellidx].soil_con.ufwc_table_layer[i]);
      }
      for(int i=0;i<MAX_NODES;i++) {
        for(int j=0;j<6;j++)
        free((char *)cell_data_structs[cellidx].soil_con.ufwc_table_node[i][j]);
        free((char *)cell_data_structs[cellidx].soil_con.ufwc_table_node[i]);
      }
    }
#endif /* QUICK_FS */

    close_files(&filep, out_data_files, &filenames, state->options.COMPRESS, state);
    free_out_data_files(out_data_files, state);
    free_out_data(&current_output_data);

    free_atmos(state->global_param.nrecs, &cell_data_structs[cellidx].atmos);
    free_dist_prcp(&cell_data_structs[cellidx].prcp, cell_data_structs[cellidx].veg_con[0].vegetat_type_num);
    free_vegcon(&cell_data_structs[cellidx].veg_con);
    free((char *) cell_data_structs[cellidx].soil_con.AreaFract);
    free((char *) cell_data_structs[cellidx].soil_con.BandElev);
    free((char *) cell_data_structs[cellidx].soil_con.Tfactor);
    free((char *) cell_data_structs[cellidx].soil_con.Pfactor);
    free((char *) cell_data_structs[cellidx].soil_con.AboveTreeLine);

    /*      free((char*)init_STILL_STORM);
     free((char*)init_DRY_TIME); */
  } /* End Grid Loop */

}



