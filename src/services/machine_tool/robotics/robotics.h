// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file robotics.h
 * @brief OPC UA for Robotics (OPC 40010-1) MotionDevice information model (PC_ENABLE_ROBOTICS).
 *
 * OPC 40010-1 "OPC UA for Robotics - Part 1: Vertical Integration" (VDMA / OPC Foundation, namespace
 * `http://opcfoundation.org/UA/Robotics/`) standardizes how an industrial robot / motion device exposes
 * its identity and live motion state so any OPC UA client (UaExpert, python `asyncua`, open62541, ...)
 * reads the same MotionDeviceSystem structure across vendors.
 *
 * This module builds the MotionDeviceSystem address space on top of the OPC UA Binary server
 * (`services/opcua`, PC_ENABLE_OPCUA): it registers a Browse + Read resolver that answers for the
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
 * MotionDevice / Controller / SafetyState and PC_ROBOTICS_AXES parametric axes are exposed (the common
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

#include "protocore_config.h"

#if PC_ENABLE_ROBOTICS

#include "services/fieldbus/opcua/opcua.h" // OpcUaVariant / OpcUaReference / handler typedefs (shares the OPC UA codec)
#include <stdint.h>

/** @brief The OPC UA for Robotics companion-spec namespace URI (OPC 40010-1). */
#define ROBOTICS_NS_URI "http://opcfoundation.org/UA/Robotics/"

/**
 * @brief MotionDeviceCategory (OPC 40010-1 MotionDeviceCategoryEnumeration). Exposed as Int32; the
 *        numeric values follow the companion-spec enumeration.
 */
enum class RoboticsMotionDeviceCategory : int32_t
{
    ROBOTICS_CAT_OTHER = 0,             ///< a category outside the ones below.
    ROBOTICS_CAT_ARTICULATED_ROBOT = 1, ///< articulated (jointed-arm) robot.
    ROBOTICS_CAT_SCARA_ROBOT = 2,       ///< SCARA robot.
    ROBOTICS_CAT_CARTESIAN_ROBOT = 3,   ///< cartesian / gantry robot.
    ROBOTICS_CAT_SPHERICAL_ROBOT = 4,   ///< spherical / polar robot.
    ROBOTICS_CAT_PARALLEL_ROBOT = 5,    ///< parallel (delta) robot.
    ROBOTICS_CAT_CYLINDRICAL_ROBOT = 6, ///< cylindrical robot.
};

/**
 * @brief Axis MotionProfile (OPC 40010-1 AxisMotionProfileEnumeration). Exposed as Int32; the numeric
 *        values follow the companion-spec enumeration.
 */
enum class RoboticsMotionProfile : int32_t
{
    ROBOTICS_PROFILE_OTHER = 0,          ///< a profile outside the ones below.
    ROBOTICS_PROFILE_ROTARY = 1,         ///< rotary axis (bounded travel).
    ROBOTICS_PROFILE_ROTARY_ENDLESS = 2, ///< rotary axis with endless rotation.
    ROBOTICS_PROFILE_LINEAR = 3,         ///< linear axis (bounded travel).
    ROBOTICS_PROFILE_LINEAR_ENDLESS = 4, ///< linear axis with endless travel.
};

/**
 * @brief SafetyState OperationalMode (OPC 40010-1 OperationalModeEnumeration). Exposed as Int32; the
 *        numeric values follow the companion-spec enumeration.
 */
enum class RoboticsOperationalMode : int32_t
{
    ROBOTICS_MODE_OTHER = 0,                ///< a mode outside the ones below.
    ROBOTICS_MODE_MANUAL_REDUCED_SPEED = 1, ///< manual jog, reduced speed (T1).
    ROBOTICS_MODE_MANUAL_HIGH_SPEED = 2,    ///< manual, high speed (T2).
    ROBOTICS_MODE_AUTOMATIC = 3,            ///< automatic program execution.
    ROBOTICS_MODE_AUTOMATIC_EXTERNAL = 4,   ///< automatic, externally commanded.
};

/** @brief One axis' live monitoring values (OPC 40010-1 AxisType, common subset). */
struct RoboticsAxis
{
    double actual_position;               ///< ActualPosition (mm or deg).
    double actual_speed;                  ///< ActualSpeed.
    double actual_acceleration;           ///< ActualAcceleration.
    RoboticsMotionProfile motion_profile; ///< MotionProfile.
};

/** @brief The Controller identity + software (OPC 40010-1 ControllerType + SoftwareType). */
struct RoboticsController
{
    const char *manufacturer;    ///< Manufacturer (LocalizedText -> String).
    const char *model;           ///< Model (LocalizedText -> String).
    const char *product_code;    ///< ProductCode (String).
    const char *serial_number;   ///< SerialNumber (String).
    const char *sw_manufacturer; ///< Software.Manufacturer (String).
    const char *sw_model;        ///< Software.Model (String).
    const char *sw_revision;     ///< Software.SoftwareRevision (String).
};

/** @brief The SafetyState (OPC 40010-1 SafetyStateType, common subset). */
struct RoboticsSafetyState
{
    RoboticsOperationalMode operational_mode; ///< ParameterSet.OperationalMode.
    bool emergency_stop;                      ///< ParameterSet.EmergencyStop.
    bool protective_stop;                     ///< ParameterSet.ProtectiveStop.
};

/** @brief One MotionDevice: identity + live motion state (OPC 40010-1 MotionDeviceType, common subset). */
struct RoboticsMotionDevice
{
    const char *manufacturer;              ///< Manufacturer (LocalizedText -> String).
    const char *model;                     ///< Model (LocalizedText -> String).
    const char *product_code;              ///< ProductCode (String).
    const char *serial_number;             ///< SerialNumber (String).
    RoboticsMotionDeviceCategory category; ///< MotionDeviceCategory.
    bool on_path;                          ///< ParameterSet.OnPath.
    bool in_control;                       ///< ParameterSet.InControl.
    double speed_override;                 ///< ParameterSet.SpeedOverride (%).
    uint32_t axis_count;                   ///< number of Axes exposed (<= PC_ROBOTICS_AXES).
    RoboticsAxis axes[PC_ROBOTICS_AXES];   ///< the axes (axes[0..axis_count-1] are live).
};

/**
 * @brief The whole MotionDeviceSystem the server exposes. Own it in your sketch and refresh its fields
 *        each loop from your robot I/O; the robotics resolvers read straight out of it (no copy). String
 *        fields may be null (served as an empty String).
 */
struct RoboticsMotionDeviceSystem
{
    const char *name;              ///< MotionDeviceSystem BrowseName / DisplayName.
    RoboticsMotionDevice device;   ///< the MotionDevice.
    RoboticsController controller; ///< the Controller.
    RoboticsSafetyState safety;    ///< the SafetyState.
};

/**
 * @brief Bind the MotionDeviceSystem the resolvers serve. @p mds must outlive the server (own it
 *        statically). Refresh its fields any time; each Read returns the current values.
 */
void pc_robotics_bind(const RoboticsMotionDeviceSystem *mds);

/**
 * @brief Read resolver for the MotionDeviceSystem model (an @ref OpcUaReadHandler): fills @p out for a
 *        robotics node's Value attribute. Returns false for a node outside the model (the server answers
 *        BadNodeIdUnknown). Install with `pc_opcua_set_read_handler(pc_robotics_read)`.
 */
bool pc_robotics_read(uint16_t ns, uint32_t id, uint32_t attribute, OpcUaVariant *out);

/**
 * @brief Browse resolver for the MotionDeviceSystem model (an @ref OpcUaBrowseHandler): writes the child
 *        references of a robotics node (and of the Objects folder, which organizes the
 *        MotionDeviceSystem) into @p out. Returns the count, or -1 for a node outside the model. Install
 *        with `pc_opcua_set_browse_handler(pc_robotics_browse)`.
 */
int32_t pc_robotics_browse(uint16_t ns, uint32_t id, OpcUaReference *out, uint32_t max);

/**
 * @brief Convenience: bind @p mds and register both resolvers on the OPC UA server in one call
 *        (`pc_opcua_set_read_handler` + `pc_opcua_set_browse_handler`). Call before `server.begin()`.
 */
void pc_robotics_install(const RoboticsMotionDeviceSystem *mds);

#endif // PC_ENABLE_ROBOTICS
#endif // PROTOCORE_ROBOTICS_H
