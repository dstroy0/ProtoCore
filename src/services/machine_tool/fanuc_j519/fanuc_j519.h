// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file fanuc_j519.h
 * @brief FANUC Stream Motion (option J519) UDP codec (PC_ENABLE_FANUC_J519) - the robot counterpart
 *        to the shipped FOCAS CNC codec (`services/focas`).
 *
 * Stream Motion is FANUC's real-time external motion interface on R-30iB / R-30iA robot controllers:
 * an external controller streams joint (or Cartesian) setpoints to the robot over UDP at the
 * controller's interpolation rate (typically 125 Hz or 250 Hz) and the robot answers every command
 * with its measured state. This is a pure, zero-heap codec for that wire protocol - the caller owns
 * the UDP socket and the real-time cadence.
 *
 * Wire format (UDP port @ref PC_J519_UDP_PORT, default 60015). Every packet opens with an 8-octet
 * header, and unlike FOCAS **every multi-octet field is LITTLE-endian** (floats are IEEE-754 binary32):
 * @code
 *   header (8)
 *     packet type (4)   u32 le - see J519Type
 *     version no  (4)   u32 le
 * @endcode
 *
 * The packet type does NOT identify a packet on its own: the numeric space is reused per direction
 * (type 0 is *Start* from the PC but *Robot Status* from the robot; type 3 is *Request* from the PC
 * but *Ack* from the robot). A decoder must therefore know which way the datagram travelled - hence
 * the direction is in the function name, not a runtime flag. Sizes disambiguate in practice
 * (Start 8 vs Status 132, Request 16 vs Ack 184) and every parser here checks the exact length.
 *
 * Packets, PC -> robot:
 * @code
 *   Start   (type 0)  8 octets   header only
 *   Motion  (type 1) 64 octets   seq, last_data, read-IO selector, data style, write-IO, 9 x f32 setpoints
 *   Stop    (type 2)  8 octets   header only
 *   Request (type 3) 16 octets   axis no + threshold type (asks for the motion-limit tables)
 * @endcode
 * Packets, robot -> PC:
 * @code
 *   Status  (type 0) 132 octets  seq, status bits, read-IO value, timestamp,
 *                                9 x f32 Cartesian pose + 9 x f32 joint pose + 9 x f32 motor current
 *   Ack     (type 3) 184 octets  axis no, threshold type, max Cartesian speed,
 *                                20 x f32 thresholds at NO load + 20 x f32 at MAX load
 * @endcode
 *
 * The codec is symmetric (like `services/scpi`): the PC-side builders pair with robot-side parsers and
 * vice versa, so a build -> parse round trip is exact and the device can act as either end (streaming
 * controller, or a robot simulator for bench work).
 *
 * Field layout, packet sizes, the type codes, the I/O-type and threshold-type enumerations, and the
 * status bit assignments were taken from the public Wireshark dissector
 * `fanuc-stream-motion/packet-fanuc-stream-motion-j519` (the same class of public reference the FOCAS
 * codec was cross-checked against). No FANUC source or header is used or required. Pure codec,
 * host-tested; no heap, no stdlib.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_FANUC_J519_H
#define PROTOCORE_FANUC_J519_H

#include "protocore_config.h"

#if PC_ENABLE_FANUC_J519

#include <stddef.h>
#include <stdint.h>

/** @brief Default Stream Motion UDP port on the robot controller. */
#define PC_J519_UDP_PORT 60015

/** @brief Axis slots carried by every pose / joint / current block (the protocol always sends 9). */
#define PC_J519_AXES 9

/** @brief Entries in each of the Ack's two motion-limit threshold tables. */
#define PC_J519_THRESHOLDS 20

/** @brief Exact octet length of each packet (the parsers require these). */
enum : size_t // NOSONAR(cpp:S3642): anonymous table of exact wire lengths compared as bare size_t in the parsers; enum
              // class would force a cast at every bound check
{
    PC_J519_LEN_START = 8,    ///< PC -> robot Start.
    PC_J519_LEN_MOTION = 64,  ///< PC -> robot Motion Command.
    PC_J519_LEN_STOP = 8,     ///< PC -> robot Stop.
    PC_J519_LEN_REQUEST = 16, ///< PC -> robot Request.
    PC_J519_LEN_STATUS = 132, ///< robot -> PC Robot Status.
    PC_J519_LEN_ACK = 184,    ///< robot -> PC Ack.
};

/**
 * @brief The packet-type word (header octets 0..3). The numeric space is shared between directions -
 *        0 and 3 each mean one thing from the PC and another from the robot.
 */
enum class J519Type : uint32_t
{
    J519_START_OR_STATUS = 0, ///< PC -> robot Start; robot -> PC Robot Status.
    J519_MOTION = 1,          ///< PC -> robot Motion Command.
    J519_STOP = 2,            ///< PC -> robot Stop.
    J519_REQUEST_OR_ACK = 3,  ///< PC -> robot Request; robot -> PC Ack.
};

/** @brief Motion Command `data_style` - how the 9 setpoints are interpreted. */
enum class J519DataStyle : uint8_t
{
    J519_STYLE_CARTESIAN = 0, ///< setpoints are a Cartesian pose.
    J519_STYLE_JOINT = 1,     ///< setpoints are joint angles.
};

/** @brief FANUC I/O port class, for the read-/write-IO selectors carried alongside a Motion Command. */
enum class J519IoType : uint8_t
{
    J519_IO_NONE = 0, ///< no I/O access requested.
    J519_IO_DI = 1,   ///< digital in.
    J519_IO_DO = 2,   ///< digital out.
    J519_IO_RI = 8,   ///< robot in.
    J519_IO_RO = 9,   ///< robot out.
    J519_IO_SI = 11,  ///< operator-panel in.
    J519_IO_SO = 12,  ///< operator-panel out.
    J519_IO_WI = 16,  ///< weld in.
    J519_IO_WO = 17,  ///< weld out.
    J519_IO_UI = 20,  ///< peripheral (UOP) in.
    J519_IO_UO = 21,  ///< peripheral (UOP) out.
    J519_IO_WSI = 26, ///< weld stick in.
    J519_IO_WSO = 27, ///< weld stick out.
    J519_IO_F = 35,   ///< flag.
    J519_IO_M = 36,   ///< marker.
};

/** @brief Request / Ack `threshold_type` - which motion-limit table is being asked for. */
enum class J519ThresholdType : uint32_t
{
    J519_THR_VELOCITY = 0,     ///< deg/s.
    J519_THR_ACCELERATION = 1, ///< deg/s^2.
    J519_THR_JERK = 2,         ///< deg/s^3.
};

/** @brief Robot Status `status` bit masks. */
enum : uint8_t // NOSONAR(cpp:S3642): anonymous bitmask constants OR'd/AND'd against a status octet; enum class forbids
               // the bitwise use
{
    J519_STATUS_READY = 0x01,        ///< ready to accept motion commands.
    J519_STATUS_CMD_RECEIVED = 0x02, ///< a command was received.
    J519_STATUS_SYSRDY = 0x04,       ///< SYSRDY (system ready).
    J519_STATUS_IN_MOTION = 0x08,    ///< the robot is moving.
};

/** @brief PC -> robot Motion Command (@ref PC_J519_LEN_MOTION octets on the wire). */
struct J519MotionCommand
{
    uint32_t version_no;            ///< header version word.
    uint32_t sequence_no;           ///< command sequence number (the robot echoes it in Status).
    uint8_t last_data;              ///< non-zero marks the final command of the stream.
    uint8_t read_io_type;           ///< @ref J519IoType of the port to read back.
    uint16_t read_io_index;         ///< index of the port to read back.
    uint16_t read_io_mask;          ///< bit mask applied to the read-back port.
    uint8_t data_style;             ///< @ref J519DataStyle of @ref joint_data.
    uint8_t write_io_type;          ///< @ref J519IoType of the port to write.
    uint16_t write_io_index;        ///< index of the port to write.
    uint16_t write_io_mask;         ///< bit mask applied to the written port.
    uint16_t write_io_value;        ///< value written to the port.
    float joint_data[PC_J519_AXES]; ///< the 9 setpoints (joint angles or a Cartesian pose).
};

/** @brief robot -> PC Robot Status (@ref PC_J519_LEN_STATUS octets on the wire). */
struct J519RobotStatus
{
    uint32_t version_no;                ///< header version word.
    uint32_t sequence_no;               ///< echoed command sequence number.
    uint8_t status;                     ///< J519_STATUS_* bits.
    uint8_t read_io_type;               ///< echoed @ref J519IoType that was read.
    uint16_t read_io_index;             ///< echoed port index.
    uint16_t read_io_mask;              ///< echoed mask.
    uint16_t read_io_value;             ///< the port value read back.
    uint32_t time_stamp;                ///< controller timestamp.
    float cartesian_pose[PC_J519_AXES]; ///< measured Cartesian pose (world -> tool0).
    float joint_pose[PC_J519_AXES];     ///< measured joint angles.
    float motor_current[PC_J519_AXES];  ///< per-axis motor current.
};

/** @brief PC -> robot Request for a motion-limit table (@ref PC_J519_LEN_REQUEST octets). */
struct J519Request
{
    uint32_t version_no;     ///< header version word.
    uint32_t axis_no;        ///< axis the thresholds are requested for.
    uint32_t threshold_type; ///< @ref J519ThresholdType.
};

/** @brief robot -> PC Ack carrying the motion-limit tables (@ref PC_J519_LEN_ACK octets). */
struct J519Ack
{
    uint32_t version_no;                          ///< header version word.
    uint32_t axis_no;                             ///< echoed axis number.
    uint32_t threshold_type;                      ///< echoed @ref J519ThresholdType.
    uint32_t max_cart_speed;                      ///< maximum Cartesian speed.
    uint32_t unknown0;                            ///< reserved word (undocumented; preserved verbatim).
    float threshold_no_load[PC_J519_THRESHOLDS];  ///< limits with no payload.
    float threshold_max_load[PC_J519_THRESHOLDS]; ///< limits at maximum payload.
};

// --- header ---------------------------------------------------------------------------------------

/**
 * @brief Read the 8-octet header without decoding the body.
 *
 * Because the type space is shared between directions, @p type alone does not identify the packet -
 * pair it with the direction the datagram arrived from (and @p len) to choose a parser.
 *
 * @return false if @p len is under 8 octets.
 */
bool pc_j519_peek(const uint8_t *buf, size_t len, uint32_t *type, uint32_t *version_no);

// --- PC -> robot: build ---------------------------------------------------------------------------

/** @brief Build a Start packet. @return octets written (@ref PC_J519_LEN_START), or 0 if @p cap is short. */
size_t pc_j519_build_start(uint8_t *buf, size_t cap, uint32_t version_no);

/** @brief Build a Stop packet. @return octets written (@ref PC_J519_LEN_STOP), or 0 if @p cap is short. */
size_t pc_j519_build_stop(uint8_t *buf, size_t cap, uint32_t version_no);

/** @brief Build a Motion Command. @return octets written (@ref PC_J519_LEN_MOTION), or 0 if @p cap is short. */
size_t pc_j519_build_motion(uint8_t *buf, size_t cap, const J519MotionCommand *cmd);

/** @brief Build a Request. @return octets written (@ref PC_J519_LEN_REQUEST), or 0 if @p cap is short. */
size_t pc_j519_build_request(uint8_t *buf, size_t cap, const J519Request *req);

// --- PC -> robot: parse (the robot side of the link, and the round-trip check) ---------------------

/** @brief Parse a Motion Command. @return false unless @p len is exactly @ref PC_J519_LEN_MOTION and the type is 1. */
bool pc_j519_parse_motion(const uint8_t *buf, size_t len, J519MotionCommand *out);

/** @brief Parse a Request. @return false unless @p len is exactly @ref PC_J519_LEN_REQUEST and the type is 3. */
bool pc_j519_parse_request(const uint8_t *buf, size_t len, J519Request *out);

// --- robot -> PC: build (robot simulator) ---------------------------------------------------------

/** @brief Build a Robot Status. @return octets written (@ref PC_J519_LEN_STATUS), or 0 if @p cap is short. */
size_t pc_j519_build_status(uint8_t *buf, size_t cap, const J519RobotStatus *st);

/** @brief Build an Ack. @return octets written (@ref PC_J519_LEN_ACK), or 0 if @p cap is short. */
size_t pc_j519_build_ack(uint8_t *buf, size_t cap, const J519Ack *ack);

// --- robot -> PC: parse (the streaming controller) ------------------------------------------------

/** @brief Parse a Robot Status. @return false unless @p len is exactly @ref PC_J519_LEN_STATUS and the type is 0. */
bool pc_j519_parse_status(const uint8_t *buf, size_t len, J519RobotStatus *out);

/** @brief Parse an Ack. @return false unless @p len is exactly @ref PC_J519_LEN_ACK and the type is 3. */
bool pc_j519_parse_ack(const uint8_t *buf, size_t len, J519Ack *out);

#endif // PC_ENABLE_FANUC_J519
#endif // PROTOCORE_FANUC_J519_H
