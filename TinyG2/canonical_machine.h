/*
 * canonical_machine.h - rs274/ngc canonical machining functions
 * This file is part of the TinyG2 project
 *
 * This code is a loose implementation of Kramer, Proctor and Messina's
 * canonical machining functions as described in the NIST RS274/NGC v3
 *
 * Copyright (c) 2010 - 2013 Alden S. Hart Jr.
 *
 * This file ("the software") is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2 as published by the
 * Free Software Foundation. You should have received a copy of the GNU General Public
 * License, version 2 along with the software.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, you may use this file as part of a software library without
 * restriction. Specifically, if other files instantiate templates or use macros or
 * inline functions from this file, or you compile this file and link it with  other
 * files to produce an executable, this file does not by itself cause the resulting
 * executable to be covered by the GNU General Public License. This exception does not
 * however invalidate any other reasons why the executable file might be covered by the
 * GNU General Public License.
 *
 * THE SOFTWARE IS DISTRIBUTED IN THE HOPE THAT IT WILL BE USEFUL, BUT WITHOUT ANY
 * WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT
 * SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _CANONICAL_MACHINE_H_
#define _CANONICAL_MACHINE_H_

#ifdef __cplusplus
extern "C"{
#endif

#include "config.h"

/* Defines */

#define MODEL 	(GCodeState_t *)&gm			// absolute pointer from canonical machine gm model
#define PLANNER (GCodeState_t *)&bf->gm		// relative to buffer *bf is currently pointing to
#define RUNTIME (GCodeState_t *)&mr.gm		// absolute pointer from runtime mm struct
#define ACTIVE_MODEL cm.am					// active model pointer is maintained by state management

/*****************************************************************************
 * CANONICAL MACHINE STRUCTURES
 */

typedef struct cmAxis {
	uint8_t axis_mode;				// see tgAxisMode in gcode.h
	float feedrate_max;				// max velocity in mm/min or deg/min
	float velocity_max;				// max velocity in mm/min or deg/min
	float travel_max;				// work envelope w/warned or rejected blocks
	float jerk_max;					// max jerk (Jm) in mm/min^3 divided by 1 million
	float jerk_homing;				// homing jerk (Jh) in mm/min^3 divided by 1 million
	float junction_dev;				// aka cornering delta
	float radius;					// radius in mm for rotary axis modes
	float search_velocity;			// homing search velocity
	float latch_velocity;			// homing latch velocity
	float latch_backoff;			// backoff from switches prior to homing latch movement
	float zero_backoff;				// backoff from switches for machine zero
} cfgAxis_t;

typedef struct cmSingleton {		// struct to manage cm globals and cycles
	magic_t magic_start;			// magic number to test memory integity	

	/**** Config variables (PUBLIC) ****/

	// system group settings
	float junction_acceleration;	// centripetal acceleration max for cornering
	float chordal_tolerance;		// arc chordal accuracy setting in mm

	// hidden system settings
	float min_segment_len;			// line drawing resolution in mm
	float arc_segment_len;			// arc drawing resolution in mm
	float estd_segment_usec;		// approximate segment time in microseconds

	// gcode power-on default settings - defaults are not the same as the gm state
	uint8_t coord_system;			// G10 active coordinate system default
	uint8_t select_plane;			// G17,G18,G19 reset default
	uint8_t units_mode;				// G20,G21 reset default
	uint8_t path_control;			// G61,G61.1,G64 reset default
	uint8_t distance_mode;			// G90,G91 reset default

	// coordinate systems and offsets
	float offset[COORDS+1][AXES];	// persistent coordinate offsets: absolute (G53) + G54,G55,G56,G57,G58,G59

	// settings for axes X,Y,Z,A B,C
	cfgAxis_t a[AXES];

	/**** Runtime variables (PRIVATE) ****/

	uint8_t combined_state;			// stat: combination of states for display purposes
	uint8_t machine_state;			// macs: machine/cycle/motion is the actual machine state
	uint8_t cycle_state;			// cycs
	uint8_t motion_state;			// momo
	uint8_t hold_state;				// hold: feedhold sub-state machine
	uint8_t homing_state;			// home: homing cycle sub-state machine
	uint8_t homed[AXES];			// individual axis homing flags
	uint8_t	g28_flag;				// true = complete a G28 move
	uint8_t	g30_flag;				// true = complete a G30 move
	uint8_t g10_persist_flag;		//.G10 changed offsets - persist them
	uint8_t feedhold_requested;		// feedhold character has been received
	uint8_t queue_flush_requested;	// queue flush character has been received
	uint8_t cycle_start_requested;	// cycle start character has been received (flag to end feedhold)
	struct GCodeState *am;			// active Gcode model is maintained by state management

	magic_t magic_end;
} cmSingleton_t;

/*****************************************************************************
 * GCODE MODEL - The following GCodeModel/GCodeInput structs are used:
 *
 * - gm is the core Gcode model state. It keeps the internal gcode state model in 
 *	 normalized, canonical form. All values are unit converted (to mm) and in the 
 *	 machine coordinate system (absolute coordinate system). Gm is owned by the 
 *	 canonical machine layer and should be accessed only through cm_ routines.
 *
 *	 The gm core struct is copied and passed as context to the runtime where it is 
 *	 used for planning, replanning, and reporting.
 *
 * - gmx is the extended gcode model variabales that are only used by the canonical 
 *	 machine and do not need to be passed further down.
 *
 * - gn is used by the gcode interpreter and is re-initialized for each 
 *   gcode block.It accepts data in the new gcode block in the formats 
 *	 present in the block (pre-normalized forms). During initialization 
 *	 some state elements are necessarily restored from gm.
 *
 * - gf is used by the gcode parser interpreter to hold flags for any data 
 *	 that has changed in gn during the parse. gf.target[] values are also used 
 *	 by the canonical machine during set_target().
 *
 * - cfg (config struct in config.h) is also used heavily and contains some 
 *	 values that might be considered to be Gcode model values. The distinction 
 *	 is that all values in the config are persisted and restored, whereas the 
 *	 gm structs are transient. So cfg has the G54 - G59 offsets, but gm has the 
 *	 G92 offsets. cfg has the power-on / reset gcode default values, but gm has
 *	 the operating state for the values (which may have changed).
 */
 typedef struct GCodeState {			// Gcode model state - used by model, planning and runtime
 	uint32_t linenum;					// Gcode block line number
	uint8_t motion_mode;				// Group1: G0, G1, G2, G3, G38.2, G80, G81,
										// G82, G83 G84, G85, G86, G87, G88, G89 
	float target[AXES]; 				// XYZABC where the move should go
	float work_offset[AXES];			// offset from the work coordinate system (for reporting only)

	float move_time;					// optimal time for move given axis constraints
	float minimum_time;					// minimum time possible for move given axis constraints
	float feed_rate; 					// F - normalized to millimeters/minute
	float spindle_speed;				// in RPM
	float parameter;					// P - parameter used for dwell time in seconds, G10 coord select...

	uint8_t inverse_feed_rate_mode;		// G93 TRUE = inverse, FALSE = normal (G94)
	uint8_t select_plane;				// G17,G18,G19 - values to set plane to
	uint8_t units_mode;					// G20,G21 - 0=inches (G20), 1 = mm (G21)
	uint8_t coord_system;				// G54-G59 - select coordinate system 1-9
	uint8_t absolute_override;			// G53 TRUE = move using machine coordinates - this block only (G53)
	uint8_t path_control;				// G61... EXACT_PATH, EXACT_STOP, CONTINUOUS
	uint8_t distance_mode;				// G91   0=use absolute coords(G90), 1=incremental movement
	uint8_t tool;						// M6 tool change - moves "tool_select" to "tool"
	uint8_t tool_select;				// T value - T sets this value
	uint8_t mist_coolant;				// TRUE = mist on (M7), FALSE = off (M9)
	uint8_t flood_coolant;				// TRUE = flood on (M8), FALSE = off (M9)
	uint8_t spindle_mode;				// 0=OFF (M5), 1=CW (M3), 2=CCW (M4)

} GCodeState_t;

typedef struct GCodeStateExtended {		// Gcode dynamic state extensions - used by model and arcs
	uint16_t magic_start;				// magic number to test memory integity
	uint8_t next_action;				// handles G modal group 1 moves & non-modals
	uint8_t program_flow;				// used only by the gcode_parser

	float position[AXES];				// XYZABC model position (Note: not used in gn or gf) 
	float origin_offset[AXES];			// XYZABC G92 offsets (Note: not used in gn or gf)
	float g28_position[AXES];			// XYZABC stored machine position for G28
	float g30_position[AXES];			// XYZABC stored machine position for G30

	float inverse_feed_rate; 			// ignored if inverse_feed_rate not active
	float feed_rate_override_factor;	// 1.0000 x F feed rate. Go up or down from there
	float traverse_override_factor;		// 1.0000 x traverse rate. Go down from there
	uint8_t	feed_rate_override_enable;	// TRUE = overrides enabled (M48), F=(M49)
	uint8_t	traverse_override_enable;	// TRUE = traverse override enabled
	uint8_t l_word;						// L word - used by G10s

	uint8_t plane_axis_0;		 		// actual axes of the selected plane
	uint8_t plane_axis_1;		 		// ...(used in gm only)
	uint8_t plane_axis_2; 
	uint8_t origin_offset_enable;		// G92 offsets enabled/disabled.  0=disabled, 1=enabled
	uint8_t block_delete_switch;		// set true to enable block deletes (true is default)

	float spindle_override_factor;		// 1.0000 x S spindle speed. Go up or down from there
	uint8_t	spindle_override_enable;	// TRUE = override enabled

	float arc_radius;					// R - radius value in arc radius mode
	float arc_offset[3];  				// IJK - used by arc commands

// unimplemented gcode parameters
//	float cutter_radius;				// D - cutter radius compensation (0 is off)
//	float cutter_length;				// H - cutter length compensation (0 is off)

	uint16_t magic_end;

} GCodeStateX_t;

typedef struct GCodeInput {				// Gcode model inputs - meaning depends on context
	uint8_t next_action;				// handles G modal group 1 moves & non-modals
	uint8_t motion_mode;				// Group1: G0, G1, G2, G3, G38.2, G80, G81,
										// G82, G83 G84, G85, G86, G87, G88, G89 
	uint8_t program_flow;				// used only by the gcode_parser
	uint32_t linenum;					// N word or autoincrement in the model

	float target[AXES]; 				// XYZABC where the move should go

	float feed_rate; 					// F - normalized to millimeters/minute
	float inverse_feed_rate; 			// ignored if inverse_feed_rate not active
	float feed_rate_override_factor;	// 1.0000 x F feed rate. Go up or down from there
	float traverse_override_factor;		// 1.0000 x traverse rate. Go down from there
	uint8_t inverse_feed_rate_mode;		// G93 TRUE = inverse, FALSE = normal (G94)
	uint8_t	feed_rate_override_enable;	// TRUE = overrides enabled (M48), F=(M49)
	uint8_t	traverse_override_enable;	// TRUE = traverse override enabled
	uint8_t override_enables;			// enables for feed and spoindle (GN/GF only)
	uint8_t l_word;						// L word - used by G10s

	uint8_t select_plane;				// G17,G18,G19 - values to set plane to
	uint8_t units_mode;					// G20,G21 - 0=inches (G20), 1 = mm (G21)
	uint8_t coord_system;				// G54-G59 - select coordinate system 1-9
	uint8_t absolute_override;			// G53 TRUE = move using machine coordinates - this block only (G53)
	uint8_t origin_offset_mode;			// G92...TRUE=in origin offset mode
	uint8_t path_control;				// G61... EXACT_PATH, EXACT_STOP, CONTINUOUS
	uint8_t distance_mode;				// G91   0=use absolute coords(G90), 1=incremental movement

	uint8_t tool;						// Tool after T and M6 (tool_select and tool_change)
	uint8_t tool_select;				// T value - T sets this value
	uint8_t tool_change;				// M6 tool change flag - moves "tool_select" to "tool"
	uint8_t mist_coolant;				// TRUE = mist on (M7), FALSE = off (M9)
	uint8_t flood_coolant;				// TRUE = flood on (M8), FALSE = off (M9)

	uint8_t spindle_mode;				// 0=OFF (M5), 1=CW (M3), 2=CCW (M4)
	float spindle_speed;				// in RPM
	float spindle_override_factor;		// 1.0000 x S spindle speed. Go up or down from there
	uint8_t	spindle_override_enable;	// TRUE = override enabled

	float parameter;					// P - parameter used for dwell time in seconds, G10 coord select...
	float arc_radius;					// R - radius value in arc radius mode
	float arc_offset[3];  				// IJK - used by arc commands

// unimplemented gcode parameters
//	float cutter_radius;				// D - cutter radius compensation (0 is off)
//	float cutter_length;				// H - cutter length compensation (0 is off)

} GCodeInput_t;

/**** Externs - See canonical_machine.c for allocation ****/

extern cmSingleton_t cm;		// canonical machine controller singleton
extern GCodeState_t  gm;		// core gcode model state
extern GCodeStateX_t gmx;		// extended gcode model state
extern GCodeInput_t  gn;		// gcode input values - transient
extern GCodeInput_t  gf;		// gcode input flags - transient

/*****************************************************************************
 * 
 * MACHINE STATE MODEL
 *
 * The following main variables track canonical machine state and state transitions.
 *		- cm.machine_state	- overall state of machine and program execution
 *		- cm.cycle_state	- what cycle the machine is executing (or none)
 *		- cm.motion_state	- state of movement
 */
/*	Allowed states and combined states
 *
 *	MACHINE STATE		CYCLE STATE		MOTION_STATE		COMBINED_STATE (FYI)
 *	-------------		------------	-------------		--------------------
 *	MACHINE_UNINIT		na				na					(U)
 *	MACHINE_READY		CYCLE_OFF		MOTION_STOP			(ROS) RESET-OFF-STOP
 *	MACHINE_PROG_STOP	CYCLE_OFF		MOTION_STOP			(SOS) STOP-OFF-STOP
 *	MACHINE_PROG_END	CYCLE_OFF		MOTION_STOP			(EOS) END-OFF-STOP
 *
 *	MACHINE_CYCLE		CYCLE_STARTED	MOTION_STOP			(CSS) CYCLE-START-STOP
 *	MACHINE_CYCLE		CYCLE_STARTED	MOTION_RUN			(CSR) CYCLE-START-RUN
 *	MACHINE_CYCLE		CYCLE_STARTED	MOTION_HOLD			(CSH) CYCLE-START-HOLD
 *	MACHINE_CYCLE		CYCLE_STARTED	MOTION_END_HOLD		(CSE) CYCLE-START-END_HOLD
 *
 *	MACHINE_CYCLE		CYCLE_HOMING	MOTION_STOP			(CHS) CYCLE-HOMING-STOP
 *	MACHINE_CYCLE		CYCLE_HOMING	MOTION_RUN			(CHR) CYCLE-HOMING-RUN
 *	MACHINE_CYCLE		CYCLE_HOMING	MOTION_HOLD			(CHH) CYCLE-HOMING-HOLD
 *	MACHINE_CYCLE		CYCLE_HOMING	MOTION_END_HOLD		(CHE) CYCLE-HOMING-END_HOLD
 */
// *** Note: check config printout strings align with all the state variables

// #### LAYER 8 CRITICAL REGION ###
// #### DO NOT CHANGE THESE ENUMERATIONS WITHOUT COMMUNITY INPUT #### 
enum cmCombinedState {				// check alignment with messages in config.c / msg_stat strings
	COMBINED_INITIALIZING = 0,		// [0] machine is initializing
	COMBINED_READY,					// [1] machine is ready for use
	COMBINED_ALARM,					// [2] machine is in alarm state (shut down)
	COMBINED_PROGRAM_STOP,			// [3] program stop or no more blocks
	COMBINED_PROGRAM_END,			// [4] program end
	COMBINED_RUN,					// [5] motion is running
	COMBINED_HOLD,					// [6] motion is holding
	COMBINED_PROBE,					// [7] probe cycle active
	COMBINED_CYCLE,					// [8] machine is running (cycling)
	COMBINED_HOMING,				// [9] homing is treated as a cycle
	COMBINED_JOG					// [10] jogging is treated as a cycle
};
//#### END CRITICAL REGION ####

enum cmMachineState {
	MACHINE_INITIALIZING = 0,		// machine is initializing
	MACHINE_READY,					// machine is ready for use
	MACHINE_ALARM,					// machine is in alarm state (shutdown)
	MACHINE_PROGRAM_STOP,			// program stop or no more blocks
	MACHINE_PROGRAM_END,			// program end
	MACHINE_CYCLE,					// machine is running (cycling)
};

enum cmCycleState {
	CYCLE_OFF = 0,					// machine is idle
	CYCLE_MACHINING,				// in normal machining cycle
	CYCLE_PROBE,					// in probe cycle
	CYCLE_HOMING,					// homing is treated as a specialized cycle
	CYCLE_JOG						// jogging is treated as a specialized cycle
};

enum cmMotionState {
	MOTION_STOP = 0,				// motion has stopped
	MOTION_RUN,						// machine is in motion
	MOTION_HOLD						// feedhold in progress
};

enum cmFeedholdState {				// feedhold_state machine
	FEEDHOLD_OFF = 0,				// no feedhold in effect
	FEEDHOLD_SYNC, 					// start hold - sync to latest aline segment
	FEEDHOLD_PLAN, 					// replan blocks for feedhold
	FEEDHOLD_DECEL,					// decelerate to hold point
	FEEDHOLD_HOLD,					// holding
	FEEDHOLD_END_HOLD				// end hold (transient state to OFF)
};

enum cmHomingState {				// applies to cm.homing_state
	HOMING_NOT_HOMED = 0,			// machine is not homed (0=false)
	HOMING_HOMED = 1				// machine is homed (1=true)
};

/* The difference between NextAction and MotionMode is that NextAction is 
 * used by the current block, and may carry non-modal commands, whereas 
 * MotionMode persists across blocks (as G modal group 1)
 */

enum cmNextAction {						// these are in order to optimized CASE statement
	NEXT_ACTION_DEFAULT = 0,			// Must be zero (invokes motion modes)
	NEXT_ACTION_SEARCH_HOME,			// G28.2 homing cycle
	NEXT_ACTION_SET_ABSOLUTE_ORIGIN,	// G28.3 origin set
	NEXT_ACTION_HOMING_NO_SET,			// G28.4 homing cycle with no coordinate setting
	NEXT_ACTION_SET_G28_POSITION,		// G28.1 set position in abs coordingates 
	NEXT_ACTION_GOTO_G28_POSITION,		// G28 go to machine position
	NEXT_ACTION_SET_G30_POSITION,		// G30.1
	NEXT_ACTION_GOTO_G30_POSITION,		// G30
	NEXT_ACTION_SET_COORD_DATA,			// G10
	NEXT_ACTION_SET_ORIGIN_OFFSETS,		// G92
	NEXT_ACTION_RESET_ORIGIN_OFFSETS,	// G92.1
	NEXT_ACTION_SUSPEND_ORIGIN_OFFSETS,	// G92.2
	NEXT_ACTION_RESUME_ORIGIN_OFFSETS,	// G92.3
	NEXT_ACTION_DWELL,					// G4
	NEXT_ACTION_STRAIGHT_PROBE			// G38.2
};

enum cmMotionMode {						// G Modal Group 1
	MOTION_MODE_STRAIGHT_TRAVERSE=0,	// G0 - straight traverse
	MOTION_MODE_STRAIGHT_FEED,			// G1 - straight feed
	MOTION_MODE_CW_ARC,					// G2 - clockwise arc feed
	MOTION_MODE_CCW_ARC,				// G3 - counter-clockwise arc feed
	MOTION_MODE_CANCEL_MOTION_MODE,		// G80
	MOTION_MODE_STRAIGHT_PROBE,			// G38.2
	MOTION_MODE_CANNED_CYCLE_81,		// G81 - drilling
	MOTION_MODE_CANNED_CYCLE_82,		// G82 - drilling with dwell
	MOTION_MODE_CANNED_CYCLE_83,		// G83 - peck drilling
	MOTION_MODE_CANNED_CYCLE_84,		// G84 - right hand tapping
	MOTION_MODE_CANNED_CYCLE_85,		// G85 - boring, no dwell, feed out
	MOTION_MODE_CANNED_CYCLE_86,		// G86 - boring, spindle stop, rapid out
	MOTION_MODE_CANNED_CYCLE_87,		// G87 - back boring
	MOTION_MODE_CANNED_CYCLE_88,		// G88 - boring, spindle stop, manual out
	MOTION_MODE_CANNED_CYCLE_89			// G89 - boring, dwell, feed out
};

enum cmModalGroup {						// Used for detecting gcode errors. See NIST section 3.4
	MODAL_GROUP_G0 = 0, 				// {G10,G28,G28.1,G92} 	non-modal axis commands (note 1)
	MODAL_GROUP_G1,						// {G0,G1,G2,G3,G80}	motion
	MODAL_GROUP_G2,						// {G17,G18,G19}		plane selection
	MODAL_GROUP_G3,						// {G90,G91}			distance mode
	MODAL_GROUP_G5,						// {G93,G94}			feed rate mode
	MODAL_GROUP_G6,						// {G20,G21}			units
	MODAL_GROUP_G7,						// {G40,G41,G42}		cutter radius compensation
	MODAL_GROUP_G8,						// {G43,G49}			tool length offset
	MODAL_GROUP_G9,						// {G98,G99}			return mode in canned cycles
	MODAL_GROUP_G12,					// {G54,G55,G56,G57,G58,G59} coordinate system selection
	MODAL_GROUP_G13,					// {G61,G61.1,G64}		path control mode
	MODAL_GROUP_M4,						// {M0,M1,M2,M30,M60}	stopping
	MODAL_GROUP_M6,						// {M6}					tool change
	MODAL_GROUP_M7,						// {M3,M4,M5}			spindle turning
	MODAL_GROUP_M8,						// {M7,M8,M9}			coolant (M7 & M8 may be active together)
	MODAL_GROUP_M9						// {M48,M49}			speed/feed override switches
};
#define MODAL_GROUP_COUNT (MODAL_GROUP_M9+1)
// Note 1: Our G0 omits G4,G30,G53,G92.1,G92.2,G92.3 as these have no axis components to error check

enum cmCanonicalPlane {				// canonical plane - translates to:
									// 		axis_0	axis_1	axis_2
	CANON_PLANE_XY = 0,				// G17    X		  Y		  Z
	CANON_PLANE_XZ,					// G18    X		  Z		  Y
	CANON_PLANE_YZ					// G19	  Y		  Z		  X							
};

enum cmUnitsMode {
	INCHES = 0,						// G20
	MILLIMETERS,					// G21
	DEGREES							// ABC axes (this value used for displays only)
};

enum cmCoordSystem {
	ABSOLUTE_COORDS = 0,			// machine coordinate system
	G54,							// G54 coordinate system
	G55,							// G55 coordinate system
	G56,							// G56 coordinate system
	G57,							// G57 coordinate system
	G58,							// G58 coordinate system
	G59								// G59 coordinate system
};
#define COORD_SYSTEM_MAX G59		// set this manually to the last one

enum cmPathControlMode {			// G Modal Group 13
	PATH_EXACT_PATH = 0,			// G61
	PATH_EXACT_STOP,				// G61.1
	PATH_CONTINUOUS					// G64 and typically the default mode
};

enum cmDistanceMode {
	ABSOLUTE_MODE = 0,				// G90
	INCREMENTAL_MODE				// G91
};

enum cmOriginOffset {
	ORIGIN_OFFSET_SET=0,			// G92 - set origin offsets
	ORIGIN_OFFSET_CANCEL,			// G92.1 - zero out origin offsets
	ORIGIN_OFFSET_SUSPEND,			// G92.2 - do not apply offsets, but preserve the values
	ORIGIN_OFFSET_RESUME			// G92.3 - resume application of the suspended offsets
};

enum cmProgramFlow {
	PROGRAM_STOP = 0,
	PROGRAM_END
};

enum cmSpindleState {				// spindle state settings (See hardware.h for bit settings)
	SPINDLE_OFF = 0,
	SPINDLE_CW,
	SPINDLE_CCW
};

enum cmCoolantState {				// mist and flood coolant states
	COOLANT_OFF = 0,				// all coolant off
	COOLANT_ON,						// request coolant on or indicates both coolants are on
	COOLANT_MIST,					// indicates mist coolant on
	COOLANT_FLOOD					// indicates flood coolant on
};

enum cmDirection {					// used for spindle and arc dir
	DIRECTION_CW = 0,
	DIRECTION_CCW
};

enum cmAxisMode {					// axis modes (ordered: see _cm_get_feed_time())
	AXIS_DISABLED = 0,				// kill axis
	AXIS_STANDARD,					// axis in coordinated motion w/standard behaviors
	AXIS_INHIBITED,					// axis is computed but not activated
	AXIS_RADIUS						// rotary axis calibrated to circumference
};	// ordering must be preserved. See cm_set_move_times()
#define AXIS_MODE_MAX_LINEAR AXIS_INHIBITED
#define AXIS_MODE_MAX_ROTARY AXIS_RADIUS

/*****************************************************************************
 * FUNCTION PROTOTYPES
 */
/*--- getters, setters and helper functions for canonical machining functions ---*/

uint8_t cm_get_combined_state(void); 
uint8_t cm_get_machine_state(void);
uint8_t cm_get_cycle_state(void);
uint8_t cm_get_motion_state(void);
uint8_t cm_get_hold_state(void);
uint8_t cm_get_homing_state(void);
void cm_set_motion_state(uint8_t motion_state);

uint32_t cm_get_linenum(GCodeState_t *gcode_state);
uint8_t cm_get_motion_mode(GCodeState_t *gcode_state);
uint8_t cm_get_coord_system(GCodeState_t *gcode_state);
uint8_t cm_get_units_mode(GCodeState_t *gcode_state);
uint8_t cm_get_select_plane(GCodeState_t *gcode_state);
uint8_t cm_get_path_control(GCodeState_t *gcode_state);
uint8_t cm_get_distance_mode(GCodeState_t *gcode_state);
uint8_t cm_get_inverse_feed_rate_mode(GCodeState_t *gcode_state);
uint8_t cm_get_tool(GCodeState_t *gcode_state);
uint8_t cm_get_spindle_mode(GCodeState_t *gcode_state);
uint8_t	cm_get_block_delete_switch(void);
uint8_t cm_get_runtime_busy(void);

void cm_set_motion_mode(GCodeState_t *gcode_state, uint8_t motion_mode);
void cm_set_spindle_mode(GCodeState_t *gcode_state, uint8_t spindle_mode);
void cm_set_spindle_speed_parameter(GCodeState_t *gcode_state, float speed);
void cm_set_tool_number(GCodeState_t *gcode_state, uint8_t tool);
void cm_set_absolute_override(GCodeState_t *gcode_state, uint8_t absolute_override);

float cm_get_active_coord_offset(uint8_t axis);
float cm_get_work_offset(GCodeState_t *gcode_state, uint8_t axis);
void cm_set_work_offsets(GCodeState_t *gcode_state);
float cm_get_absolute_position(GCodeState_t *gcode_state, uint8_t axis);
float cm_get_work_position(GCodeState_t *gcode_state, uint8_t axis);
void cm_set_move_times(GCodeState_t *gcode_state);

void cm_set_model_arc_offset(float i, float j, float k);
void cm_set_model_arc_radius(float r);
void cm_set_model_linenum(uint32_t linenum);
void cm_set_model_target(float target[], float flag[]);
void cm_conditional_set_model_position(stat_t status);

/*--- canonical machining functions (loosely patterned after NIST) ---*/

void canonical_machine_init(void);
stat_t cm_alarm(stat_t status);									// enter alarm state. returns same status code
stat_t cm_assertions(void);

stat_t cm_queue_flush(void);									// flush serial and planner queues with coordinate resets

stat_t cm_select_plane(uint8_t plane);							// G17, G18, G19
stat_t cm_set_units_mode(uint8_t mode);							// G20, G21

stat_t cm_homing_cycle_start(void);								// G28.2
stat_t cm_homing_cycle_start_no_set(void);						// G28.4
stat_t cm_homing_callback(void);								// G28.2 main loop callback
stat_t cm_set_absolute_origin(float origin[], float flags[]);	// G28.3  (special function)
void cm_set_axis_origin(uint8_t axis, const float position);	// set absolute position (used by G28's)

stat_t cm_set_g28_position(void);								// G28.1
stat_t cm_goto_g28_position(float target[], float flags[]); 	// G28
stat_t cm_set_g30_position(void);								// G30.1
stat_t cm_goto_g30_position(float target[], float flags[]);		// G30

stat_t cm_probe_cycle_start(void);								// G38.2
stat_t cm_probe_callback(void);									// G38.2 main loop callback
int8_t cm_probe_get_axis(void);
void cm_probe_set_position(float);

stat_t cm_set_coord_system(uint8_t coord_system);				// G54 - G59
stat_t cm_set_coord_offsets(uint8_t coord_system, float offset[], float flag[]); // G10 L2
stat_t cm_set_distance_mode(uint8_t mode);						// G90, G91
stat_t cm_set_origin_offsets(float offset[], float flag[]);		// G92
stat_t cm_reset_origin_offsets(void); 							// G92.1
stat_t cm_suspend_origin_offsets(void); 						// G92.2
stat_t cm_resume_origin_offsets(void);				 			// G92.3

stat_t cm_straight_traverse(float target[], float flags[]);
stat_t cm_set_feed_rate(float feed_rate);						// F parameter
stat_t cm_set_inverse_feed_rate_mode(uint8_t mode);				// True= inv mode
stat_t cm_set_path_control(uint8_t mode);						// G61, G61.1, G64
stat_t cm_straight_feed(float target[], float flags[]);			// G1
stat_t cm_arc_feed(float target[], float flags[], 				// G2, G3
				   float i, float j, float k, 
				   float radius, uint8_t motion_mode);
stat_t cm_dwell(float seconds);									// G4, P parameter

// see spindle.h for spindle definitions - which would go right here

stat_t cm_mist_coolant_control(uint8_t mist_coolant); 			// M7
stat_t cm_flood_coolant_control(uint8_t flood_coolant);			// M8, M9

stat_t cm_override_enables(uint8_t flag); 						// M48, M49
stat_t cm_feed_rate_override_enable(uint8_t flag); 				// M50
stat_t cm_feed_rate_override_factor(uint8_t flag);				// M50.1
stat_t cm_traverse_override_enable(uint8_t flag); 				// M50.2
stat_t cm_traverse_override_factor(uint8_t flag);				// M50.3
stat_t cm_spindle_override_enable(uint8_t flag); 				// M51
stat_t cm_spindle_override_factor(uint8_t flag);				// M51.1

stat_t cm_select_tool(uint8_t tool);							// T parameter
stat_t cm_change_tool(uint8_t tool);							// M6

// canonical machine commands not called from gcode dispatcher
void cm_message(char_t *message);								// msg to console (e.g. Gcode comments)

stat_t cm_feedhold_sequencing_callback(void);					// process feedhold, cycle start and queue flush requests
void cm_request_feedhold(void);
void cm_request_queue_flush(void);
void cm_request_cycle_start(void);

void cm_cycle_start(void);										// (no Gcode)
void cm_cycle_end(void); 										// (no Gcode)
void cm_feedhold(void);											// (no Gcode)
void cm_program_stop(void);										// M0
void cm_optional_program_stop(void);							// M1
void cm_program_end(void);										// M2
void cm_exec_program_stop(void);
void cm_exec_program_end(void);

/*--- cmdArray interface functions ---*/

char_t cm_get_axis_char(const int8_t axis);

stat_t cm_get_mline(cmdObj_t *cmd);		// get model line number
stat_t cm_get_line(cmdObj_t *cmd);		// get active (model or runtime) line number
stat_t cm_get_stat(cmdObj_t *cmd);		// get combined machine state as value and string
stat_t cm_get_macs(cmdObj_t *cmd);		// get raw machine state as value and string
stat_t cm_get_cycs(cmdObj_t *cmd);		// get raw cycle state (etc etc)...
stat_t cm_get_mots(cmdObj_t *cmd);		// get raw motion state...
stat_t cm_get_hold(cmdObj_t *cmd);		// get raw hold state...
stat_t cm_get_home(cmdObj_t *cmd);		// get raw homing state...
stat_t cm_get_unit(cmdObj_t *cmd);		// get unit mode...
stat_t cm_get_coor(cmdObj_t *cmd);		// get coordinate system in effect...
stat_t cm_get_momo(cmdObj_t *cmd);		// get motion mode...
stat_t cm_get_plan(cmdObj_t *cmd);		// get active plane...
stat_t cm_get_path(cmdObj_t *cmd);		// get patch control mode...
stat_t cm_get_dist(cmdObj_t *cmd);		// get distance mode...
stat_t cm_get_frmo(cmdObj_t *cmd);		// get feedrate mode...
stat_t cm_get_toolv(cmdObj_t *cmd);		// get tool (value)
stat_t cm_get_vel(cmdObj_t *cmd);		// get runtime velocity...
stat_t cm_get_pos(cmdObj_t *cmd);		// get runtime work position...
stat_t cm_get_mpo(cmdObj_t *cmd);		// get runtime machine position...
stat_t cm_get_ofs(cmdObj_t *cmd);		// get runtime work offset...

stat_t cm_run_qf(cmdObj_t *cmd);		// run queue flush
stat_t cm_run_home(cmdObj_t *cmd);		// start homing cycle

stat_t cm_get_am(cmdObj_t *cmd);		// get axis mode
stat_t cm_set_am(cmdObj_t *cmd);		// set axis mode
stat_t cm_get_jrk(cmdObj_t *cmd);		// get jerk with 1,000,000 correction
stat_t cm_set_jrk(cmdObj_t *cmd);		// set jerk with 1,000,000 correction

/*--- text_mode support functions ---*/

#ifdef __TEXT_MODE

	void cm_print_vel(cmdObj_t *cmd);		// model state reporting
	void cm_print_feed(cmdObj_t *cmd);
	void cm_print_line(cmdObj_t *cmd);
	void cm_print_stat(cmdObj_t *cmd);
	void cm_print_macs(cmdObj_t *cmd);
	void cm_print_cycs(cmdObj_t *cmd);
	void cm_print_mots(cmdObj_t *cmd);
	void cm_print_hold(cmdObj_t *cmd);
	void cm_print_home(cmdObj_t *cmd);
	void cm_print_unit(cmdObj_t *cmd);
	void cm_print_coor(cmdObj_t *cmd);
	void cm_print_momo(cmdObj_t *cmd);
	void cm_print_plan(cmdObj_t *cmd);
	void cm_print_path(cmdObj_t *cmd);
	void cm_print_dist(cmdObj_t *cmd);
	void cm_print_frmo(cmdObj_t *cmd);
	void cm_print_tool(cmdObj_t *cmd);

	void cm_print_gpl(cmdObj_t *cmd);		// Gcode defaults
	void cm_print_gun(cmdObj_t *cmd);
	void cm_print_gco(cmdObj_t *cmd);
	void cm_print_gpa(cmdObj_t *cmd);
	void cm_print_gdi(cmdObj_t *cmd);

	void cm_print_lin(cmdObj_t *cmd);		// generic print for linear values 
	void cm_print_pos(cmdObj_t *cmd);		// print runtime work position in prevailing units
	void cm_print_mpo(cmdObj_t *cmd);		// print runtime work position always in MM uints

	void cm_print_ja(cmdObj_t *cmd);		// global CM settings
	void cm_print_ct(cmdObj_t *cmd);
	void cm_print_ml(cmdObj_t *cmd);
	void cm_print_ma(cmdObj_t *cmd);
	void cm_print_ms(cmdObj_t *cmd);
	void cm_print_st(cmdObj_t *cmd);

	void cm_print_am(cmdObj_t *cmd);		// axis print functions
	void cm_print_fr(cmdObj_t *cmd);
	void cm_print_vm(cmdObj_t *cmd);
	void cm_print_tm(cmdObj_t *cmd);
	void cm_print_jm(cmdObj_t *cmd);
	void cm_print_jh(cmdObj_t *cmd);
	void cm_print_jd(cmdObj_t *cmd);
	void cm_print_ra(cmdObj_t *cmd);
	void cm_print_sn(cmdObj_t *cmd);
	void cm_print_sx(cmdObj_t *cmd);
	void cm_print_sv(cmdObj_t *cmd);
	void cm_print_lv(cmdObj_t *cmd);
	void cm_print_lb(cmdObj_t *cmd);
	void cm_print_zb(cmdObj_t *cmd);
	void cm_print_cofs(cmdObj_t *cmd);
	void cm_print_cpos(cmdObj_t *cmd);

#else // __TEXT_MODE

	#define cm_print_vel tx_print_stub		// model state reporting
	#define cm_print_feed tx_print_stub
	#define cm_print_line tx_print_stub
	#define cm_print_stat tx_print_stub
	#define cm_print_macs tx_print_stub
	#define cm_print_cycs tx_print_stub
	#define cm_print_mots tx_print_stub
	#define cm_print_hold tx_print_stub
	#define cm_print_home tx_print_stub
	#define cm_print_unit tx_print_stub
	#define cm_print_coor tx_print_stub
	#define cm_print_momo tx_print_stub
	#define cm_print_plan tx_print_stub
	#define cm_print_path tx_print_stub
	#define cm_print_dist tx_print_stub
	#define cm_print_frmo tx_print_stub
	#define cm_print_tool tx_print_stub

	#define cm_print_gpl tx_print_stub		// Gcode defaults
	#define cm_print_gun tx_print_stub
	#define cm_print_gco tx_print_stub
	#define cm_print_gpa tx_print_stub
	#define cm_print_gdi tx_print_stub

	#define cm_print_lin tx_print_stub		// generic print for linear values 
	#define cm_print_pos tx_print_stub		// print runtime work position in prevailing units
	#define cm_print_mpo tx_print_stub		// print runtime work position always in MM uints

	#define cm_print_ja tx_print_stub		// global CM settings
	#define cm_print_ct tx_print_stub
	#define cm_print_ml tx_print_stub
	#define cm_print_ma tx_print_stub
	#define cm_print_ms tx_print_stub
	#define cm_print_st tx_print_stub

	#define cm_print_am tx_print_stub		// axis print functions
	#define cm_print_fr tx_print_stub
	#define cm_print_vm tx_print_stub
	#define cm_print_tm tx_print_stub
	#define cm_print_jm tx_print_stub
	#define cm_print_jh tx_print_stub
	#define cm_print_jd tx_print_stub
	#define cm_print_ra tx_print_stub
	#define cm_print_sn tx_print_stub
	#define cm_print_sx tx_print_stub
	#define cm_print_sv tx_print_stub
	#define cm_print_lv tx_print_stub
	#define cm_print_lb tx_print_stub
	#define cm_print_zb tx_print_stub
	#define cm_print_cofs tx_print_stub
	#define cm_print_cpos tx_print_stub

#endif // __TEXT_MODE

#ifdef __cplusplus
}
#endif

#endif // _CANONICAL_MACHINE_H_
