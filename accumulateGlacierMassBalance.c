
#include "GlacierMassBalanceResult.h"
#include "vicNl.h"

void resetAccumulationValues(std::vector<HRU>* hruList) {
  for (std::vector<HRU>::iterator hru = hruList->begin(); hru != hruList->end(); ++hru) {
    if (hru->isGlacier) {
      hru->glacier.cum_mass_balance = 0;
    }
  }
}

void accumulateGlacierMassBalance(GraphingEquation* gmbEquation, const dmy_struct* dmy, int rec, dist_prcp_struct* prcp, const soil_con_struct* soil, const ProgramState* state) {

  if (IS_INVALID(state->global_param.glacierAccumStartYear) || IS_INVALID(state->global_param.glacierAccumStartMonth)
      || IS_INVALID(state->global_param.glacierAccumStartDay) || IS_INVALID(state->global_param.glacierAccumInterval)) {
    return; // If these have not been set in the global file, then don't bother with accumulation.
  }

  if (rec + 1 >= state->global_param.nrecs) {
    return; // Reached the end of the model simulation, don't do anything to avoid dmy[rec + 1] being out of bounds.
  }

  const dmy_struct nextDate = dmy[rec + 1];
  // If the next time step is the start of the new accumulation interval then output results (pass to glacier model)
  // and reinitialize all the accumulation values.
  if ( (nextDate.year >= state->global_param.glacierAccumStartYear)
  		&& (abs(nextDate.year - state->global_param.glacierAccumStartYear) % state->global_param.glacierAccumInterval == 0)
      && nextDate.month == state->global_param.glacierAccumStartMonth
			&& nextDate.day == state->global_param.glacierAccumStartDay
			&& (((state->global_param.dt <= 12) && (dmy[rec].hour == 23)) || (state->global_param.dt == 24 )) ) {

#if VERBOSE
        fprintf(stderr, "accumulateGlacierMassBalance for cell at %4.5f %4.5f:\n", soil->lat, soil->lng );
#endif /* VERBOSE */
    GlacierMassBalanceResult result(prcp->hruList, soil, dmy[rec]);
    result.printForDebug();
    resetAccumulationValues(&(prcp->hruList));
    *gmbEquation = result.equation; // update GMB polynomial for this cell

  } else {  // Not the final time step in the specified interval, accumulate mass balance.
    // Initialize on the first time step.
    if (rec == 0) {
      resetAccumulationValues(&prcp->hruList);
    }

    // Accumulate mass balance for each glacier hru.
    for (std::vector<HRU>::iterator hru = prcp->hruList.begin(); hru != prcp->hruList.end(); ++hru) {
      if (hru->isGlacier) {
        if (IS_VALID(hru->glacier.mass_balance)) {
          hru->glacier.cum_mass_balance += hru->glacier.mass_balance;
        }
      }
    }
  }

}
