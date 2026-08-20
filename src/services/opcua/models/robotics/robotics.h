// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file robotics.h
 * @brief OPC UA for Robotics (OPC 40010-1) MotionDevice information model (PROTOCORE_ENABLE_ROBOTICS).
 *
 * OPC 40010-1 "OPC UA for Robotics - Part 1: Vertical Integration" (VDMA / OPC Foundation, namespace
 * `http://opcfoundation.org/UA/Robotics/`) standardizes how an industrial robot / motion device exposes
 * its identity and live motion state so any OPC UA client (UaExpert, python `asyncua`, open62541, ...)
 * reads the same MotionDeviceSystem structure across vendors.
 *
 * This module builds the MotionDeviceSystem address space on top of the OPC UA Binary server
 * (`services/opcua`, PROTOCORE_ENABLE_OPCUA): it registers a Browse + Read resolver that answers for the
 * MotionDeviceSystem node hierarchy and serves live values out of a caller-owned @ref
 * RoboticsMotionDeviceSystem struct you refresh in your loop. Same pattern as `services/umati`
 * (OPC 40501-1); no heap, no stdlib - the model is a fixed node table, the values are pointers/scalars
 * in your struct.
 *
 * Model exposed (BrowseNames per OPC 40010-1), under the Objects folder:
 *
 *   MotionDeviceSystem
 *     MotionDevices (Folder)
 *       MotionDevice     Manufacturer, Model, ProductCode, SerialNumber, MotionDeviceCategory
 *         ParameterSet   OnPath, InControl, SpeedOverride
 *         Axes (Folder)
 *           Axis_1..N    ActualPosition, ActualSpeed, ActualAcceleration, MotionProfile
 *     Controllers (Folder)
 *       Controller       Manufacturer, Model, ProductCode, SerialNumber
 *         Software       Manufacturer, Model, SoftwareRevision
 *     SafetyStates (Folder)
 *       SafetyState
 *         ParameterSet   OperationalMode, EmergencyStop, ProtectiveStop
 *
 * The model is read-only (a monitoring model - the robot reports, the client observes). Scope note: one
 * MotionDevice / Controller / SafetyState and PROTOCORE_ROBOTICS_AXES parametric axes are exposed (the common
 * embedded robot); the values carry faithful BrowseNames, but the companion-spec TypeDefinitions,
 * PowerTrains, and the NamespaceArray entry for the Robotics URI (which needs array-Variant support in
 * the base server) are a documented follow-on - a generic OPC UA client still browses the structure and
 * reads every value by BrowseName today.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date     2026
 */

#ifndef PROTOCORE_ROBOTICS_H
#define PROTOCORE_ROBOTICS_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_ROBOTICS

PROTOCORE_BEGIN_DECLS

// PROTOCORE_ROBOTICS_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

/** @brief The OPC UA for Robotics companion-spec namespace URI (OPC 40010-1). */
#define ROBOTICS_NS_URI "http://opcfoundation.org/UA/Robotics/"

/**
 * @brief MotionDeviceCategory (OPC 40010-1 MotionDeviceCategoryEnumeration). Exposed as Int32; the
 *        numeric values follow the companion-spec enumeration.
 */
typedef enum PROTO_ENUM_PACKED
{
    ROBOTICS_CAT_OTHER = 0,             ///< a category outside the ones below.
    ROBOTICS_CAT_ARTICULATED_ROBOT = 1, ///< articulated (jointed-arm) robot.
    ROBOTICS_CAT_SCARA_ROBOT = 2,       ///< SCARA robot.
    ROBOTICS_CAT_CARTESIAN_ROBOT = 3,   ///< cartesian / gantry robot.
    ROBOTICS_CAT_SPHERICAL_ROBOT = 4,   ///< spherical / polar robot.
    ROBOTICS_CAT_PARALLEL_ROBOT = 5,    ///< parallel (delta) robot.
    ROBOTICS_CAT_CYLINDRICAL_ROBOT = 6, ///< cylindrical robot.
} RoboticsMotionDeviceCategory;

/**
 * @brief Axis MotionProfile (OPC 40010-1 AxisMotionProfileEnumeration). Exposed as Int32; the numeric
 *        values follow the companion-spec enumeration.
 */
typedef enum PROTO_ENUM_PACKED
{
    ROBOTICS_PROFILE_OTHER = 0,          ///< a profile outside the ones below.
    ROBOTICS_PROFILE_ROTARY = 1,         ///< rotary axis (bounded travel).
    ROBOTICS_PROFILE_ROTARY_ENDLESS = 2, ///< rotary axis with endless rotation.
    ROBOTICS_PROFILE_LINEAR = 3,         ///< linear axis (bounded travel).
    ROBOTICS_PROFILE_LINEAR_ENDLESS = 4, ///< linear axis with endless travel.
} RoboticsMotionProfile;

/**
 * @brief SafetyState OperationalMode (OPC 40010-1 OperationalModeEnumeration). Exposed as Int32; the
 *        numeric values follow the companion-spec enumeration.
 */
typedef enum PROTO_ENUM_PACKED
{
    ROBOTICS_MODE_OTHER = 0,                ///< a mode outside the ones below.
    ROBOTICS_MODE_MANUAL_REDUCED_SPEED = 1, ///< manual jog, reduced speed (T1).
    ROBOTICS_MODE_MANUAL_HIGH_SPEED = 2,    ///< manual, high speed (T2).
    ROBOTICS_MODE_AUTOMATIC = 3,            ///< automatic program execution.
    ROBOTICS_MODE_AUTOMATIC_EXTERNAL = 4,   ///< automatic, externally commanded.
} RoboticsOperationalMode;

/** @brief One axis' live monitoring values (OPC 40010-1 AxisType, common subset). */
typedef struct
{
    double actual_position;               ///< ActualPosition (mm or deg).
    double actual_speed;                  ///< ActualSpeed.
    double actual_acceleration;           ///< ActualAcceleration.
    RoboticsMotionProfile motion_profile; ///< MotionProfile.
} RoboticsAxis;
/** @brief The Controller identity + software (OPC 40010-1 ControllerType + SoftwareType). */
typedef struct
{
    const char *manufacturer;    ///< Manufacturer (LocalizedText -> String).
    const char *model;           ///< Model (LocalizedText -> String).
    const char *product_code;    ///< ProductCode (String).
    const char *serial_number;   ///< SerialNumber (String).
    const char *sw_manufacturer; ///< Software.Manufacturer (String).
    const char *sw_model;        ///< Software.Model (String).
    const char *sw_revision;     ///< Software.SoftwareRevision (String).
} RoboticsController;
/** @brief The SafetyState (OPC 40010-1 SafetyStateType, common subset). */
typedef struct
{
    RoboticsOperationalMode operational_mode; ///< ParameterSet.OperationalMode.
    proto_bool emergency_stop;                ///< ParameterSet.EmergencyStop.
    proto_bool protective_stop;               ///< ParameterSet.ProtectiveStop.
} RoboticsSafetyState;
/** @brief One MotionDevice: identity + live motion state (OPC 40010-1 MotionDeviceType, common subset). */
typedef struct
{
    const char *manufacturer;                   ///< Manufacturer (LocalizedText -> String).
    const char *model;                          ///< Model (LocalizedText -> String).
    const char *product_code;                   ///< ProductCode (String).
    const char *serial_number;                  ///< SerialNumber (String).
    RoboticsMotionDeviceCategory category;      ///< MotionDeviceCategory.
    proto_bool on_path;                         ///< ParameterSet.OnPath.
    proto_bool in_control;                      ///< ParameterSet.InControl.
    double speed_override;                      ///< ParameterSet.SpeedOverride (%).
    uint32_t axis_count;                        ///< number of Axes exposed (<= PROTOCORE_ROBOTICS_AXES).
    RoboticsAxis axes[PROTOCORE_ROBOTICS_AXES]; ///< the axes (axes[0..axis_count-1] are live).
} RoboticsMotionDevice;
/**
 * @brief The whole MotionDeviceSystem the server exposes. Own it in your sketch and refresh its fields
 *        each loop from your robot I/O; the robotics resolvers read straight out of it (no copy). String
 *        fields may be null (served as an empty String).
 */
typedef struct
{
    const char *name;              ///< MotionDeviceSystem BrowseName / DisplayName.
    RoboticsMotionDevice device;   ///< the MotionDevice.
    RoboticsController controller; ///< the Controller.
    RoboticsSafetyState safety;    ///< the SafetyState.
} RoboticsMotionDeviceSystem;
/** @brief What bind takes: mds. */
typedef struct
{
    const RoboticsMotionDeviceSystem *mds;
} RoboticsBindArgs;
/** @brief What install takes: mds. */
typedef struct
{
    const RoboticsMotionDeviceSystem *mds;
} RoboticsInstallArgs;
/**
 * @brief OPC UA for Robotics (OPC 40010-1) MotionDevice information model (PROTOCORE_ENABLE_ROBOTICS).
 *
 * A caller sets the members a call takes, invokes it through ::Robotics with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Robotics.bind_args.mds = ...;
 *   Robotics.bind(work);
 *
 * @var RoboticsNs::bind_args  what bind takes: mds
 * @var RoboticsNs::install_args  what install takes: mds
 * @var RoboticsNs::ok  a call's true/false outcome
 * @var RoboticsNs::bind  bind the MotionDeviceSystem the resolvers serve. mds must outlive ...
 * @var RoboticsNs::install  convenience: bind mds and register both resolvers on the OPC UA ...
 *
 * @c work is PROTOCORE_ROBOTICS_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    RoboticsBindArgs bind_args;
    RoboticsInstallArgs install_args;
    proto_bool ok;
} RoboticsVars;

/** @brief The operands and the outcome. */
extern RoboticsVars RoboticsV;

/** @brief The entries. */
typedef struct
{
    void (*const bind)(uint8_t *restrict work);
    void (*const install)(uint8_t *restrict work);
} RoboticsNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in RoboticsV or a region of the borrow at a fixed offset.
void protocore_robotics_bind(uint8_t *restrict work);
void protocore_robotics_install(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Robotics.bind(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const RoboticsNs Robotics __attribute__((unused)) = {
    .bind = protocore_robotics_bind,
    .install = protocore_robotics_install,
};

/**
 * @brief The PROTOCORE_ROBOTICS_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_robotics_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_ROBOTICS

#endif // PROTOCORE_ROBOTICS_H
