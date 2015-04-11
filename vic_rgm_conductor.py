#!/usr/bin/env python

""" This script orchestrates a coupled Variable Infiltration Capacity (VIC) and Regional Glacier Model (RGM) run. """

import sys
import os
import argparse
import subprocess
import numpy as np
import h5py
import csv
from collections import OrderedDict
import bisect

vic_full_path = '/home/mfischer/code/vic/vicNl'
rgm_full_path = '/home/mfischer/code/rgm/rgm'
input_files_path = '/home/mfischer/vic_dev/input/peyto/'
#rgm_params_file =  input_files_path + 'global_params_VIC.txt'
#initial_vic_global_file = '/home/mfischer/vic_dev/input/place/glb_base_PLACE_19601995_VIC4.1.2_outNETCDF_initial.txt' 
#vic_global_file = '/home/mfischer/vic_dev/input/place/glb_base_PLACE_19601995_VIC4.1.2_outNETCDF.txt' 
#vpf_full_path = '/home/mfischer/vic_dev/input/place/vpf_place_100m.txt' 
#rgm_output_path = '/home/mfischer/vic_dev/out/testing/rgm_output/'
temp_files_path = '/home/mfischer/vic_dev/out/testing/temp_out_files/'
# NOTE: Setting a default elevation band size of 100 m (should this be a command line parameter?)
band_size = 100
BARE_SOIL_ID = '19'

class MyParser(argparse.ArgumentParser):
    def error(self, message):
        sys.stderr.write('error: %s]n' % message)
        self.print_help()
        sys.exit(2)

# Get all global parameters 
parser = MyParser()
parser.add_argument('--g', action="store", dest="vic_global_file", type=str, help = 'file name and path of the VIC global parameters file')
parser.add_argument('--rgm-params', action="store", dest="rgm_params_file", type=str, help = 'file name and path of the Regional Glacier Model (RGM) parameters file')
parser.add_argument('--sdem', action="store", dest="surf_dem_file", type=str, help = 'file name and path of the initial Surface Digital Elevation Model (SDEM) file')
parser.add_argument('--bdem', action="store", dest="bed_dem_file", type=str, help = 'file name and path of the Bed Digital Elevation Model (BDEM) file')
parser.add_argument('--pixel-map', action="store", dest="pixel_cell_map_file", type=str, help = 'file name and path of the RGM Pixel to VIC Grid Cell mapping file')
parser.add_argument('--trace-files', action="store_true", default=False, dest="trace_files", help = 'write out persistent ASCII DEM and mass balance grid files on each time step for offline inspection')
parser.add_argument('--bare-soil-root', action="store", dest="bare_soil_root_parms_file", type=str, default='NA', help = 'file name and path of one-line text file containing 6 custom root parameters for the bare soil vegetation type / HRU (same format as a vegetation tile line in the vegetation parameters file).  Default: 0.10  1.00  0.10  0.00  0.10  0.00')
parser.add_argument('--glacier-root', action="store", dest="glacier_root_parms_file", type=str, default='NA', help = 'file name and path of one-line text file containing 6 custom root parameters for the glacier vegetation type / HRU (same format as a vegetation tile line in the vegetation parameters file).  Default: 0.10  1.00  0.10  0.00  0.10  0.00')

if len(sys.argv) == 1:
	parser.print_help()
	sys.exit(1)
options = parser.parse_args()
vic_global_file = options.vic_global_file
rgm_params_file = options.rgm_params_file
rgm_surf_dem_in_file = options.surf_dem_file
bed_dem_file = options.bed_dem_file
pixel_cell_map_file = options.pixel_cell_map_file
output_trace_files = options.trace_files
bare_soil_root_parms_file = options.bare_soil_root_parms_file
glacier_root_parms_file = options.glacier_root_parms_file

if bare_soil_root_parms_file == 'NA':
	bare_soil_root_parms = '0.10  1.00  0.10  0.00  0.10  0.00'
else:
	bare_soil_root_parms = np.loadtxt(bare_soil_root_parms_file)
if glacier_root_parms_file == 'NA':
	glacier_root_parms = '0.10  1.00  0.10  0.00  0.10  0.00'
else:
	glacier_root_parms = np.loadtxt(glacier_root_parms_file)


# To have nested ordered defaultdicts
class OrderedDefaultdict(OrderedDict):
	# from: http://stackoverflow.com/questions/4126348/how-do-i-rewrite-this-function-to-implement-ordereddict/4127426#4127426
    def __init__(self, *args, **kwargs):
        if not args:
            self.default_factory = None
        else:
            if not (args[0] is None or callable(args[0])):
                raise TypeError('first argument must be callable or None')
            self.default_factory = args[0]
            args = args[1:]
        super(OrderedDefaultdict, self).__init__(*args, **kwargs)
    def __missing__ (self, key):
        if self.default_factory is None:
            raise KeyError(key)
        self[key] = default = self.default_factory()
        return default
    def __reduce__(self):  # optional, for pickle support
        args = (self.default_factory,) if self.default_factory else ()
        return self.__class__, args, None, None, self.iteritems()

def read_gsa_headers(dem_file):
	""" Opens and reads the header metadata from a GSA Digital Elevation Map file, 
	verifies agreement with the VIC-RGM mapping file metadata, and returns the x and y extents metadata """
	with open(dem_file, 'r') as f:
		num_cols = 0
		num_rows = 0
		for line_num, line in enumerate(f):
			if line_num == 0:
				split_line = line.split()
				if split_line[0] != 'DSAA':
					print 'read_gsa_headers({}): DSAA header on first line of DEM file was not found or is malformed.  DEM file does not conform to ASCII grid format.  Exiting. \n'.format(dem_file)
					#sys.exit(0)
			elif line_num == 1:
				split_line = [int(x) for x in line.split()]
				num_cols = split_line[0]
				num_rows = split_line[1]
				if (num_cols != num_cols_dem) or (num_rows != num_rows_dem):
					print 'read_gsa_headers({}): Disagreement in row/column dimensions between DEM file (NROWS={}, NCOLS={}) and RGM-VIC mapping file (NROWS={}, NCOLS={}). Exiting.\n'.format(dem_file, num_rows, num_cols, num_rows_dem, num_cols_dem)
					#sys.exit(0)
				#print 'num_rows_dem: {} num_cols_dem: {}'.format(num_rows_dem, num_cols_dem)
			elif line_num == 2:
				split_line = [float(x) for x in line.split()]
				xmin = split_line[0]
				xmax = split_line[1]
			elif line_num == 3:
				split_line = [float(x) for x in line.split()]
				ymin = split_line[0]
				ymax = split_line[1]
			else:
				break
	return xmin, xmax, ymin, ymax

def get_rgm_pixel_mapping(pixel_map_file):
	""" Parses the RGM pixel to VIC grid cell mapping file and initialises a 2D  
	   	grid of dimensions num_rows_dem x num_cols_dem (matching the RGM pixel grid),
	   	each element containing a list with the VIC cell ID associated with that RGM pixel and its median elevation"""
	pixel_to_cell_map = []
	cell_areas = {}
	with open(pixel_map_file, 'r') as f:
		num_cols_dem = 0
		num_rows_dem = 0
		for line in iter(f):
			#print 'line: {}'.format(line)
			split_line = line.split()
			if split_line[0] == 'NCOLS':
				num_cols_dem = int(split_line[1])
				#print 'num_cols_dem: {}'.format(num_cols_dem)
				if num_cols_dem and num_rows_dem:
					pixel_to_cell_map = [[0 for x in range(0, num_cols_dem)] for x in range(0, num_rows_dem)]
			elif split_line[0] == 'NROWS':
				num_rows_dem = int(split_line[1])
				#print 'num_rows_dem: {}'.format(num_rows_dem)
				if num_cols_dem and num_rows_dem:
					pixel_to_cell_map = [[0 for x in range(0, num_cols_dem)] for x in range(0, num_rows_dem)]
			elif split_line[0][0] == '"': # column header row / comments
				#print 'comment line: {}'.format(split_line)
				pass
			else:
				# NOTE: we might want Markus to recreate this mapping file with zero-based indexing
				row_num = int(split_line[1])
				col_num = int(split_line[2])
				median_elev = int(split_line[4])
				cell_id = split_line[5]
				#print 'populating row: {}, col: {}'.format(row_num, col_num)
				pixel_to_cell_map[row_num][col_num] = [cell_id, median_elev]
				# Increment the pixel-normalized area within the grid cell
				try:
					cell_areas[cell_id] += 1
				except:
					cell_areas[cell_id] = 1
			#print 'columns: {}'.format(columns)
	return pixel_to_cell_map, num_rows_dem, num_cols_dem, cell_areas

def get_mass_balance_polynomials(state, state_file):
	""" Extracts the Glacier Mass Balance polynomial for each grid cell from an open VIC state file """
	gmb_info = state['GLAC_MASS_BALANCE_INFO'][0]
	cell_count = len(gmb_info)
	if cell_count != len(cell_ids):
		print 'get_mass_balance_polynomials: The number of VIC cells ({}) read from the state file {} and those read from the vegetation parameter file ({}) disagree. Exiting.\n'.format(cell_count, state_file, len(cell_ids))
		sys.exit(0)
	gmb_polys = {}
	for i in range(0, cell_count):
		cell_id = str(int(gmb_info[i][0]))
		if cell_id not in cell_ids:
			print 'get_mass_balance_polynomials: Cell ID {} was not found in the list of VIC cell IDs read from the vegetation parameters file. Exiting.\n'.format(cell_id)
			sys.exit(0)
		gmb_polys[cell_id] = [gmb_info[i][1], gmb_info[i][2], gmb_info[i][3]]
	return gmb_polys

def mass_balances_to_rgm_grid(gmb_polys, pixel_to_cell_map):
	""" Translate mass balances from grid cell GMB polynomials to 2D RGM pixel grid to use as one of the inputs to RGM """
	mass_balance_grid = [[0 for x in range(0, num_cols_dem)] for x in range(0, num_rows_dem)]
	try:
		for row in range(0, num_rows_dem):
			for col in range(0, num_cols_dem):
				pixel = pixel_to_cell_map[row][col]
				# band = pixel[0]
				cell_id = pixel[0]
				median_elev = pixel[1] # read most recent median elevation of this pixel
				# only grab pixels that fall within a VIC cell
				if cell_id != 'NA':
					# check that the cell_id agrees with what was read from the veg_parm_file
					if cell_id not in cell_ids:
						print 'mass_balances_to_rgm_grid: Cell ID {} was not found in the list of VIC cell IDs read from the vegetation parameters file. Exiting.\n'.format(cell_id)
						sys.exit(0)
					mass_balance_grid[row][col] = gmb_polys[cell_id][0] + median_elev * (gmb_polys[cell_id][1] + median_elev * gmb_polys[cell_id][2])
	except:
		print 'mass_balances_to_rgm_grid: Error while processing pixel {} (row {} column {})'.format(pixel, row, col)
	return mass_balance_grid

def write_grid_to_gsa_file(grid, outfilename):
	""" Writes a 2D grid to ASCII file in the input format expected by the RGM for DEM and mass balance grids """
	zmin = np.min(grid)
	zmax = np.max(grid)
	header_rows = [['DSAA'], [num_cols_dem, num_rows_dem], [dem_xmin, dem_xmax], [dem_ymin, dem_ymax], [zmin, zmax]]
	with open(outfilename, 'w') as csvfile:
		writer = csv.writer(csvfile, delimiter=' ')
		for header in range(0, len(header_rows)):
			writer.writerow(header_rows[header])
		for row in range(0, num_rows_dem):
			writer.writerow(grid[row])

def get_global_parms(global_parm_file):
	""" Parses the initial VIC global parameters file created by the user with the settings for the entire VIC-RGM run """
	global_parms = OrderedDefaultdict()
	n_outfile_lines = 0
	with open(global_parm_file, 'r') as f:
		for line in iter(f):
			#print 'line: {}'.format(line)
			if not line.isspace() and line[0] is not '#':
				split_line = line.split()
				#print 'columns: {}'.format(split_line)
				parm_name = split_line[0]
				if parm_name == 'OUTFILE': # special case because there are multiple occurrences, not consecutive
						n_outfile_lines += 1
						parm_name = 'OUTFILE_' + str(n_outfile_lines)
				elif parm_name == 'OUTVAR': # special case because multiple OUTVAR lines follow each OUTFILE line
						parm_name = 'OUTVAR_' + str(n_outfile_lines)
				try:
					if global_parms[parm_name]: # if we've already read one or more entries of this parm_name
#						print 'parm {} exists already'.format(parm_name)
						global_parms[parm_name].append(split_line[1:])
				except:
					global_parms[parm_name] = []
					global_parms[parm_name].append(split_line[1:])
#					print 'global_parms[{}]: {}'.format(parm_name,global_parms[parm_name])
				# We need to create a placeholder in this position for INIT_STATE if it doesn't exist in the initial
				# global parameters file, to be used for all iterations after the first year
				if parm_name == 'OUTPUT_FORCE': # OUTPUT_FORCE should always immediately precede INIT_STATE in the global file
					global_parms['INIT_STATE'] = []
	return global_parms

def update_global_parms(init_state):
	# Important parms: STARTYEAR, ENDYEAR, VEGPARAM, SNOW_BAND (and GLACIER_ACCUM_START_YEAR, GLACIER_ACCUM_INTERVAL?)
	global_parms['STARTYEAR'] = [[str(year)]]
	global_parms['ENDYEAR'] = [[str(year)]]
	global_parms['STATEYEAR'] = [[str(year)]]
	# All iterations after the first / wind-up period have modified state, vegetation parms, and snow band parms
	if init_state:
		# set/create INIT_STATE parm with most current state_file (does not exist in the first read-in of global_parms)
		init_state_file = state_filename_prefix + "_" + str(year - 1) + str(global_parms['STATEMONTH'][0][0]) + str(global_parms['STATEDAY'][0][0])
		global_parms['INIT_STATE'] = [[init_state_file]]
		# New output state filename for next VIC year run
		global_parms['VEGPARAM'] = [[temp_vpf]]
		global_parms['SNOW_BAND'] = [[num_snow_bands, temp_snb]]

def write_global_parms_file():
	""" Reads existing global_parms dict and writes out a new temporary VIC Global Parameter File for feeding into VIC """
	temp_gpf = temp_files_path + 'gpf_temp_' + str(year) + '.txt'
	with open(temp_gpf, 'w') as f:
		writer = csv.writer(f, delimiter=' ')
		try:
			for parm in global_parms:
				num_parm_lines = len(global_parms[parm])
				if parm == 'INIT_STATE' and len(global_parms['INIT_STATE']) == 0:
					pass
				elif parm[0:8] == 'OUTFILE_':
					line = []
					line.append('OUTFILE')
					for value in global_parms[parm][0]:
						line.append(value)
					writer.writerow(line)
				elif parm[0:7] == 'OUTVAR_':
					for line_num in range(0, num_parm_lines):
						line = []
						line.append('OUTVAR')
						for value in global_parms[parm][line_num]:
							line.append(value)
							writer.writerow(line)
				elif num_parm_lines == 1:
					line = []
					line.append(parm)
					for value in global_parms[parm][0]:
						line.append(value)
					writer.writerow(line)
				elif num_parm_lines > 1:
					for line_num in range(0, num_parm_lines):
						line = []
						line.append(parm)
						for value in global_parms[parm][line_num]:
							line.append(value)
						writer.writerow(line)
		except:
			print 'write_global_parms_file: Error while writing parameter {} to file. Exiting.\n'.format(parm)
			sys.exit(0)
	return temp_gpf

def get_veg_parms(veg_parm_file):
	""" Reads in a Vegetation Parameter File and parses out VIC grid cell IDs, as well as an ordered nested dict of all vegetation parameters,
	grouped by elevation band index """
	cell_ids = []
	num_veg_tiles = {}
	veg_parms = OrderedDefaultdict()
	with open(veg_parm_file, 'r') as f:
		for line in iter(f):
			split_line = line.split()
			num_columns = len(split_line)
			if num_columns == 2:
				cell = split_line[0]
				cell_ids.append(cell)
				num_veg_tiles[cell] = split_line[1]
				veg_parms[cell] = OrderedDefaultdict()
			elif num_columns > 2:
				band_id = split_line[-1]
				try:
					veg_parms[cell][band_id].append(split_line)
				except:
					veg_parms[cell][band_id] = []
					veg_parms[cell][band_id].append(split_line)
	return cell_ids, num_veg_tiles, veg_parms

def init_residual_area_fracs():
	""" Reads the initial snow band area fractions and glacier vegetation (HRU) tile area fractions and calculates the initial residual area fractions """
	residual_area_fracs = {}
	for cell in cell_ids:
		print 'cell: {}'.format(cell)
		residual_area_fracs[cell] = {}
		for band in veg_parms[cell]:
			print 'band: {}'.format(band)
			glacier_exists = False
			for line_idx, line in enumerate(veg_parms[cell][band]):
				print 'line: {}'.format(line)
				if line[0] == GLACIER_ID:
					glacier_exists = True
					residual_area_fracs[cell][band] = snb_parms[cell][0][int(band)] - float(veg_parms[cell][band][line_idx][1])
					print 'Glacier exists in this band. residual_area_fracs[{}][{}] = {} - {} = {}'.format(cell, band, snb_parms[cell][0][int(band)], float(veg_parms[cell][band][line_idx][1]), residual_area_fracs[cell][band])
					if residual_area_fracs[cell][band] < 0:
						print 'init_residual_area_fracs(): Error: Calculated a negative residual area fraction for cell {}, band {}. The sum of vegetation tile fraction areas for a given band in the Vegetation Parameter File must be equal to the area fraction for that band in the Snow Band File. Exiting.\n'.format(cell, band)
						sys.exit(0)
					break
			if not glacier_exists:
				#residual_area_fracs[cell][band] = 0
				residual_area_fracs[cell][band] = snb_parms[cell][0][int(band)]
				print 'No glacier in this band.  residual_area_fracs[{}][{}] = {}'.format(cell, band, residual_area_fracs[cell][band])
	return residual_area_fracs

def update_band_areas():
	""" Calculates the area fractions of elevation bands within VIC cells, and area fractions of glaciers within each of these elevation bands """
	band_areas = {}
	glacier_areas = {}
	area_frac_bands = {}
	area_frac_glacier = {}
	for cell in cell_ids:
		band_areas[cell] = [0 for x in range(0, num_snow_bands)]
		glacier_areas[cell] = [0 for x in range(0, num_snow_bands)]
		area_frac_bands[cell] = [0 for x in range(0, num_snow_bands)]
		area_frac_glacier[cell] = [0 for x in range(0, num_snow_bands)]
	for row in range(0, num_rows_dem):
		for col in range(0, num_cols_dem):
			cell = pixel_to_cell_map[row][col][0] # get the VIC cell this pixel belongs to
			if cell != 'NA':
#				print 'row: {}, col: {}'.format(row, col)
#				print 'cell: {}'.format(cell)				
				# Use the RGM DEM output to update the pixel median elevation in the pixel_to_cell_map
				pixel_elev = rgm_surf_dem_out[row][col]
				pixel_to_cell_map[row][col][1] = pixel_elev
#				print 'pixel_elev: {}'.format(pixel_elev)
#				band_found = False
				for band_idx, band in enumerate(band_map[cell]):
#					print 'band: {}, band_idx: {}'.format(band, band_idx)
					if int(pixel_elev) in range(band, band + band_size):
#						band_found = True
						band_areas[cell][band_idx] += 1
#						print 'band_areas[{}][{}] = {}'.format(cell, band_idx, band_areas[cell][band_idx])
						if glacier_mask[row][col]:
							glacier_areas[cell][band_idx] += 1
#							print 'glacier_areas[{}][{}] = {}'.format(cell, band_idx, glacier_areas[cell][band_idx])
						break
#		raw_input('--------------- row processed. Hit enter --------------')
	print 'Area fractions of elevation bands within VIC cells: {}'.format(band_areas)
	print 'Area fractions of glacier within these elevation bands: {}'.format(glacier_areas)
	print '\n'
	for cell in cell_ids:
		print 'cell: {}'.format(cell)
		print 'cell_areas[{}] = {}'.format(cell, cell_areas[cell])
		print '\n'
		for band_idx, band in enumerate(band_map[cell]):
			print 'band: {}, band_idx: {}'.format(band, band_idx)
			print 'band_areas[{}][{}] = {}'.format(cell, band_idx, band_areas[cell][band_idx])
			print 'glacier_areas[{}][{}] = {}'.format(cell, band_idx, glacier_areas[cell][band_idx])
			# This will be used to update the Snow Band File
			area_frac_bands[cell][band_idx] = float(band_areas[cell][band_idx]) / cell_areas[cell]
			if area_frac_bands[cell][band_idx] < 0:
				print 'update_band_areas(): Error: Calculated a negative band area fraction for cell {}, band {}. Exiting.\n'.format(cell, band)
				sys.exit(0)
			# This will be used to update the Vegetation Parameter File
			area_frac_glacier[cell][band_idx] = float(glacier_areas[cell][band_idx]) / cell_areas[cell]
			if area_frac_glacier[cell][band_idx] < 0:
				print 'update_band_areas(): Error: Calculated a negative glacier area fraction for cell {}, band {}. Exiting.\n'.format(cell, band)
				sys.exit(0)
			print 'area_frac_bands[{}][{}] = {}'.format(cell, band_idx, area_frac_bands[cell][band_idx])
			print 'area_frac_glacier[{}][{}] = {}'.format(cell, band_idx, area_frac_glacier[cell][band_idx])
			print '\n'
	return area_frac_bands, area_frac_glacier

def update_veg_parms():
	""" Updates vegetation parameters for all VIC grid cells by applying calculated changes in glacier area fractions across all elevation bands """
	for cell in cell_ids:
		print 'cell: {}'.format(cell)
		for band in veg_parms[cell]:
			if area_frac_bands[cell][int(band)] > 0: # If we (still) have anything in this elevation band
				new_residual_area_frac = area_frac_bands[cell][int(band)] - area_frac_glacier[cell][int(band)]
				delta_residual = residual_area_fracs[cell][band] - new_residual_area_frac
				veg_types = [] # identify and temporarily store vegetation types (HRUs) currently existing within this band
				bare_soil_exists = False # temporary boolean to identify if bare soil vegetation type (HRU) currently exists within this band
				glacier_exists = False # temporary boolean to identify if glacier vegetation type (HRU) currently exists within this band 
				sum_previous_band_area_fracs = 0 # sum of band vegetation tile (HRU) area fractions in the last iteration
				if delta_residual < 0: # glacier portion of this band has shrunk; update its area fraction and increase the bare soil component accordingly
					print '\nGlacier portion of band {} has SHRUNK (delta_residual = {})'.format(band, delta_residual)
					for line_idx, line in enumerate(veg_parms[cell][band]): 
						print 'Current line in band {}:  {}'.format(band, line)
						veg_types.append(int(line[0]))
						print 'Existing vegetation type identified in this elevation band: {}'.format(veg_types[-1])
						if veg_types[-1] == int(BARE_SOIL_ID):
							bare_soil_exists = True
							veg_parms[cell][band][line_idx][1] = str(float(veg_parms[cell][band][line_idx][1]) + abs(delta_residual))
							print 'Bare soil already exists in this band.  New area fraction: veg_parms[{}][{}][{}][1] = {}'.format(cell, band, line_idx, veg_parms[cell][band][line_idx][1])
						elif veg_types[-1] == int(GLACIER_ID):
							new_glacier_area_frac = area_frac_glacier[cell][int(band)]
							if new_glacier_area_frac >= 0:
								veg_parms[cell][band][line_idx][1] = str(new_glacier_area_frac)
								print 'Updated glacier area fraction: veg_parms[{}][{}][{}][1] = {}'.format(cell, band, line_idx, veg_parms[cell][band][line_idx][1])
							else: 
								# delete the existing glacier line in veg_parms?
								#print 'Area fraction of glacier (vegetation type {}) in cell {}, band {} was reduced to {}. Removing tile (HRU) from vegetation parameters'.format(veg_types[-1], cell, band, new_glacier_area_frac)
								#del veg_parms[cell][band][line_idx]
								print 'update_veg_parms(): Error: Calculated a negative area fraction ({}) for glacier vegetation type {} in cell {}, band {}. Exiting.\n'.format(new_veg_area_frac, veg_types[-1], cell, band)
								sys.exit(0)
					if not bare_soil_exists: # insert new line for bare soil vegetation (HRU) type in the correct numerical position within this band
						new_line = BARE_SOIL_ID + ' ' + str(delta_residual) + ' ' + ''.join(str(x) for x in bare_soil_root_parms) + ' ' + str(band)
						bisect.insort_left(veg_types, int(BARE_SOIL_ID))
						position = veg_types.index(int(BARE_SOIL_ID))
						veg_parms[cell][band].insert(position, new_line)
						print 'Bare soil did NOT already exist in this band. Inserted line: veg_parms[{}][{}][{}] = {}'.format(cell, band, position, veg_parms[cell][band][position])
				elif delta_residual > 0: # glacier portion of this band has grown; decrease fraction of non-glacier HRUs by a total of delta_residual
					print '\nGlacier portion of band {} has GROWN (delta_residual = {})'.format(band, delta_residual)
					for line_idx, line in enumerate(veg_parms[cell][band]): 
						sum_previous_band_area_fracs += float(line[1])
					print 'sum_previous_band_area_fracs = {}'.format(sum_previous_band_area_fracs)
					for line_idx, line in enumerate(veg_parms[cell][band]):
						print 'Current line in band {}:  {}'.format(band, line)
						veg_types.append(int(line[0])) # keep a record of vegetation types (HRUs) currently existing within this band
						print 'Existing vegetation type identified in this elevation band: {}'.format(veg_types[-1])
						if veg_types[-1] == int(GLACIER_ID): # set glacier tile area fraction
							glacier_exists = True
							veg_parms[cell][band][line_idx][1] = str(area_frac_glacier[cell][int(band)])
							print 'Glacier already exists in this band.  New glacier area fraction: veg_parms[{}][{}][{}][1] = {}'.format(cell, band, line_idx, veg_parms[cell][band][line_idx][1])
						else:
							#### NOTE Friday April 10: suspect that this is not getting calculated correctly.
							# Get the change in area fraction for this vegetation type (HRU) based on its previous share of the residuals in this elevation band
							print 'Existing area fraction for this vegetation type: {}'.format(veg_parms[cell][band][line_idx][1])
							delta_veg_area_frac = delta_residual * (float(veg_parms[cell][band][line_idx][1]) / sum_previous_band_area_fracs)
							print 'Calculated delta_veg_area_frac = {}'.format(delta_veg_area_frac)
							new_veg_area_frac = float(veg_parms[cell][band][line_idx][1]) - delta_veg_area_frac
							if new_veg_area_frac >= 0:
								veg_parms[cell][band][line_idx][1] = str(new_veg_area_frac)
								print 'Reduced area fraction of vegetation type {} by delta_veg_area_frac.  New area fraction: veg_parms[{}][{}][{}][1] = {}'.format(veg_types[-1], cell, band, line_idx, veg_parms[cell][band][line_idx][1])
							else: #the existing vegetation tile (HRU) was overtaken by glacier
								# delete the existing vegetation tile (HRU) that was overtaken by glacier?
								#print 'Area fraction of vegetation type {} in cell {}, band {} was reduced to {}. Removing tile (HRU) from vegetation parameters'.format(veg_types[-1], cell, band, new_veg_area_frac)
								#del veg_parms[cell][band][line_idx]
								print 'update_veg_parms(): Error: Calculated a negative area fraction ({}) for vegetation type {} in cell {}, band {}. Exiting.\n'.format(new_veg_area_frac, veg_types[-1], cell, band)
								sys.exit(0)
					if not glacier_exists: # insert new line for glacier vegetation (HRU) type in the correct numerical position within this band
						new_line = GLACIER_ID + ' ' + str(delta_residual) + ' ' + ''.join(str(x) for x in glacier_root_parms) + ' ' + str(band)
						bisect.insort_left(veg_types, int(GLACIER_ID))
						position = veg_types.index(int(GLACIER_ID))
						veg_parms[cell][band].insert(position, [new_line])
						print 'Glacier did NOT already exist in this band. Inserted line with new glacier area fraction: veg_parms[{}][{}][{}] = {}'.format(cell, band, position, veg_parms[cell][band][position])
			else: # We have nothing left in this elevation band.  Set all fractional areas for all vegetation tiles (HRUs) in this band to zero
				print '\nNothing left in elevation band {}.  Setting all fractional areas to zero.'.format(band)
				for line_idx, line in enumerate(veg_parms[cell][band]):
					veg_parms[cell][band][line_idx][1] = 0
			# Set residual area fractions to the new calculated values for the next iteration
	### uncomment this when done testing:
			#residual_area_fracs[cell][band] = new_residual_area_frac

def write_veg_parms_file():
	""" Writes current (updated) vegetation parameters to a new temporary Vegetation Parameters File for feeding back into VIC """
	temp_vpf = temp_files_path + 'vpf_temp_' + str(year) + '.txt'
	print 'writing new temporary vegetation parameter file {}...'.format(temp_vpf)
	with open(temp_vpf, 'w') as f:
		writer = csv.writer(f, delimiter=' ')
		for cell in veg_parms:
			writer.writerow([cell, num_veg_tiles[cell]])
			print '{}    {}'.format(cell, num_veg_tiles[cell])
			for band in veg_parms[cell]:
				for line in veg_parms[cell][band]:
					writer.writerow(line)
					print ' '.join(map(str, line))
	return temp_vpf

def get_snb_parms(snb_file, num_snow_bands):
	""" Reads in a Snow Band File and outputs an ordered dict:
	{'cell_id_0' : [area_frac_band_0,...,area_frac_band_N],[median_elev_band_0,...,median_elev_band_N],[Pfactor_band_0,...,Pfactor_band_N]], 'cell_id_1' : ..."""
	snb_parms = OrderedDict()
	with open(snb_file, 'r') as f:
		for line in iter(f):
			#print 'snb file line: {}'.format(line)
			split_line = line.split()
			num_columns = len(split_line)
			cell_id = split_line[0]
			if num_columns != 3*num_snow_bands + 1:
				print 'get_snb_parms(): Error: Number of columns ({}) in snow band file {} is incorrect for the given number of snow bands ({}) given in the global parameter file (should be 3 * num_snow_bands + 1). Exiting.\n'.format(num_columns, snb_file, num_snow_bands)
				sys.exit(0)
			snb_parms[cell_id] = [[float(x) for x in split_line[1 : num_snow_bands+1]],[int(x) for x in split_line[num_snow_bands+1 : 2*num_snow_bands+1]],[float(x) for x in split_line[2*num_snow_bands+1 : 3*num_snow_bands+1]]]
	return snb_parms

def create_band_map(snb_parms, band_size):
	""" Takes a dict of Snow Band parameters and identifies and creates a list of elevation bands for each grid cell of band_size width in meters"""
	#TODO: create command line parameter to allow for extra bands to be specified as padding above/below existing ones, to allow for glacier growth(/slide?)
	band_map = {}
	for cell in cell_ids:
		band_map[cell] = [0 for x in range(0, num_snow_bands)]
		for band_idx, band in enumerate(snb_parms[cell][1]):
			band_map[cell][band_idx] = int(band - band % band_size)
	return band_map

def update_glacier_mask(sdem, bdem):
	""" Takes output Surface DEM from RGM and uses element-wise differencing with the Bed DEM to form an updated glacier mask """
	diffs = sdem - bed_dem
	if np.any(diffs < 0):
		print 'update_glacier_mask: Error: subtraction of Bed DEM from output Surface DEM of RGM produced one or more negative values.  Exiting.\n'
		sys.exit(0)
	glacier_mask = np.zeros((num_rows_dem, num_cols_dem))
	glacier_mask[diffs > 0] = 1
	return glacier_mask

def update_snb_parms():
	for cell in snb_parms:
		#print 'cell: {}'.format(cell)
		snb_parms[cell][0] = area_frac_bands[cell]
		print ' '.join(map(str, snb_parms[cell][0]))

def write_snb_parms_file():
	""" Writes current (updated) snow band parameters to a new temporary Snow Band File for feeding back into VIC """
	temp_snb = temp_files_path + 'snb_temp_' + str(year) + '.txt'
	with open(temp_snb, 'w') as f:
		writer = csv.writer(f, delimiter=' ')
		for cell in snb_parms:
			line = []
			line.append(cell)
			for area_frac in area_frac_bands[cell]:
				line.append(area_frac)
			for band_frac in snb_parms[cell][1]:
				line.append(band_frac) # append existing median elevations
			for pfactor in snb_parms[cell][2]:
				line.append(pfactor) # append existing Pfactor values
			writer.writerow(line)
	return temp_snb

# Main program.  Initialize coupled VIC-RGM run
if __name__ == '__main__':

	print '\n\nVIC + RGM ... together at last!'
	# Get all initial VIC global parameters
	global_parms = get_global_parms(vic_global_file)
	# Get entire time range of coupled VIC-RGM run from the initial VIC global file
	start_year = int(global_parms['STARTYEAR'][0][0])
	end_year = int(global_parms['ENDYEAR'][0][0])
	# Initial VIC output state filename prefix is determined by STATENAME in the global file
	state_filename_prefix = global_parms['STATENAME'][0][0]
	# Numeric code indicating a glacier vegetation tile (HRU)
	GLACIER_ID = global_parms['GLACIER_ID'][0]

	# Get VIC vegetation parameters and grid cell IDs from initial Vegetation Parameter File
	veg_parm_file = global_parms['VEGPARAM'][0][0]
	cell_ids, num_veg_tiles, veg_parms = get_veg_parms(veg_parm_file)
	# Get VIC snow/elevation band parameters from initial Snow Band File
	num_snow_bands = int(global_parms['SNOW_BAND'][0][0])
	snb_file = global_parms['SNOW_BAND'][0][1]
	snb_parms = get_snb_parms(snb_file, num_snow_bands)
	# Get list of elevation bands for each VIC grid cell
	band_map = create_band_map(snb_parms, band_size)
	# Calculate the initial residual (i.e. non-glacier) area fractions for all bands in all cells 
	residual_area_fracs = init_residual_area_fracs()

	# Open and read VIC-grid-to-RGM-pixel mapping file
	# pixel_to_cell_map is a list of dimensions num_rows_dem x num_cols_dem, each element containing a VIC grid cell ID
	pixel_to_cell_map, num_rows_dem, num_cols_dem, cell_areas = get_rgm_pixel_mapping(pixel_cell_map_file)

	# Check header validity, verify number of columns & rows agree with what's stated in the pixel_to_cell_map_file, and get DEM xmin, xmax, ymin, ymax metadata	
	dem_xmin, dem_xmax, dem_ymin, dem_ymax = read_gsa_headers(bed_dem_file)
	# Read the provided Bed Digital Elevation Map (BDEM) file into a 2D bed_dem array
	bed_dem = np.loadtxt(bed_dem_file, skiprows=5)

	# The RGM will always output a DEM file of the same name (if running RGM for a single year at a time)
	rgm_surf_dem_out_file = temp_files_path + 's_out_00001.grd'
	
	# Run the coupled VIC-RGM model for the time range specified in the VIC global parameters file
	for year in range(start_year, end_year):
		print '\nRunning year: {}'.format(year)

		# 1. Write / Update temporary Global Parameters File, temp_gpf
		# If year > start_year, introduce INIT_STATE line with most recent state_file (does not exist in the first read-in of global_parms).
		# Overwrite VEGPARAM parameter with temp_vpf, and SNOW_BAND with temp_snb
		state_file = state_filename_prefix + "_" + str(year) + str(global_parms['STATEMONTH'][0][0]) + str(global_parms['STATEDAY'][0][0])
		update_global_parms(year > start_year)
		temp_gpf = write_global_parms_file()
		print 'invoking VIC with global parameter file {}'.format(temp_gpf)

		# 2. Run VIC for a year.  This will save VIC model state at the end of the year, along with a Glacier Mass Balance (GMB) polynomial for each cell
		subprocess.check_call([vic_full_path, "-g", temp_gpf], shell=False, stderr=subprocess.STDOUT)

		# 3. Open VIC NetCDF state file and get the most recent GMB polynomial for each grid cell being modeled
		print 'opening state file {}'.format(state_file)
		state = h5py.File(state_file, 'r+')
		gmb_polys = get_mass_balance_polynomials(state, state_file)
			
		# 4. Translate mass balances using grid cell GMB polynomials and current veg_parm_file into a 2D RGM mass balance grid (MBG)
		mass_balance_grid = mass_balances_to_rgm_grid(gmb_polys, pixel_to_cell_map)
		# write Mass Balance Grid to ASCII file to direct the RGM to use as input
		mbg_file = temp_files_path + 'mass_balance_grid_' + str(year) + '.gsa'
		write_grid_to_gsa_file(mass_balance_grid, mbg_file)

		# 5. Run RGM for one year, passing MBG, BDEM, SDEM
		#subprocess.check_call([rgm_full_path, "-p", rgm_params_file, "-b", bed_dem_file, "-d", sdem_file, "-m", mbg_file, "-o", temp_files_path, "-s", "0", "-e", "0" ], shell=False, stderr=subprocess.STDOUT)
		subprocess.check_call([rgm_full_path, "-p", rgm_params_file, "-b", bed_dem_file, "-d", rgm_surf_dem_in_file, "-m", mbg_file, "-o", temp_files_path, "-s", "0", "-e", "0" ], shell=False, stderr=subprocess.STDOUT)
		# remove temporary files if not saving for offline inspection
		if not output_trace_files:
			os.remove(mbg_file)
			os.remove(rgm_surf_dem_file)

		# 6. Read in new Surface DEM file from RGM output
		rgm_surf_dem_out = np.loadtxt(rgm_surf_dem_out_file, skiprows=5)
		temp_surf_dem_file = temp_files_path + 'rgm_surf_dem_out_' + str(year) + '.gsa'
		os.rename(rgm_surf_dem_out_file, temp_surf_dem_file)
		# this will be fed back into RGM on next time step
		rgm_surf_dem_in_file = temp_surf_dem_file

		# 7. Update glacier mask
		glacier_mask = update_glacier_mask(rgm_surf_dem_out, bed_dem)
		if output_trace_files:
			glacier_mask_file = temp_files_path + 'glacier_mask_' + str(year) + '.gsa'
			write_grid_to_gsa_file(glacier_mask, glacier_mask_file)
		
		# 8. Update areas of each elevation band in each VIC grid cell, and calculate area fractions
		area_frac_bands, area_frac_glacier = update_band_areas()

		# 9. Update vegetation parameters and write to new temporary file temp_vpf
		update_veg_parms()
		temp_vpf = write_veg_parms_file()

		# 10. Update snow band parameters and write to new temporary file temp_snb
		update_snb_parms()
		temp_snb = write_snb_parms_file()

		# 11 Update HRUs in VIC state file 
			# don't forget to close the state file
		