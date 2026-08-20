// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the SAE J1939 message codec (services/fieldbus/j1939/j1939.h).
//
// SAE J1939-21 fixes the 29-bit identifier as
//   bits 28..26 priority | 25 EDP | 24 DP | 23..16 PF | 15..8 PS | 7..0 SA
// with PS carrying the destination address when PF < 240 (PDU1) and the PGN's low octet when
// PF >= 240 (PDU2). test_published_identifiers is the load-bearing case: it assembles the four
// identifiers every J1939 tool prints - EEC1, Request, Address Claimed and the two Transport
// Protocol PGNs - field by field from that bit layout, so a codec that shifted PF or that put the
// destination address in a PDU2 frame cannot reproduce them.

#include "services/fieldbus/j1939/j1939.h"
#include "shared/can/can.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// The PGN and address assignments J1939-21 and J1939-71 publish.
void test_published_pgn_registry(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x00EC00u, J1939_PGN_TP_CM);         // 60416
    TEST_ASSERT_EQUAL_HEX32(0x00EB00u, J1939_PGN_TP_DT);         // 60160
    TEST_ASSERT_EQUAL_HEX32(0x00EE00u, J1939_PGN_ADDRESS_CLAIM); // 60928
    TEST_ASSERT_EQUAL_HEX32(0x00EA00u, J1939_PGN_REQUEST);       // 59904
    TEST_ASSERT_EQUAL_HEX32(0x00F004u, J1939_PGN_EEC1);          // 61444
    TEST_ASSERT_EQUAL_HEX32(0x00FEEEu, J1939_PGN_ET1);           // 65262
    TEST_ASSERT_EQUAL_HEX32(0x00FEF2u, J1939_PGN_LFE);           // 65266
    TEST_ASSERT_EQUAL_HEX32(0x00FEF5u, J1939_PGN_AMB);           // 65269
    TEST_ASSERT_EQUAL_HEX32(0x00FEF6u, J1939_PGN_IC1);           // 65270
    TEST_ASSERT_EQUAL_HEX32(0x00FEE0u, J1939_PGN_VD);            // 65248
    TEST_ASSERT_EQUAL_HEX32(0x00FEF1u, J1939_PGN_CCVS);          // 65265
    TEST_ASSERT_EQUAL_HEX32(0x00FECAu, J1939_PGN_DM1);           // 65226

    TEST_ASSERT_EQUAL_HEX8(0xFFu, J1939_ADDR_GLOBAL);
    TEST_ASSERT_EQUAL_HEX8(0xFEu, J1939_ADDR_NULL);
    TEST_ASSERT_EQUAL_UINT8(240u, J1939_PDU2_THRESHOLD);
    TEST_ASSERT_EQUAL_UINT8(7u, J1939_TP_DT_LEN);

    // TP.CM control octets (J1939-21 section 5.10).
    TEST_ASSERT_EQUAL_HEX8(16u, J1939_TP_CM_RTS);
    TEST_ASSERT_EQUAL_HEX8(17u, J1939_TP_CM_CTS);
    TEST_ASSERT_EQUAL_HEX8(19u, J1939_TP_CM_EOM_ACK);
    TEST_ASSERT_EQUAL_HEX8(32u, J1939_TP_CM_BAM);
    TEST_ASSERT_EQUAL_HEX8(255u, J1939_TP_CM_ABORT);
}

// Each identifier assembled from the bit layout:
//   EEC1 from address 0 at priority 3, PGN 0xF004: PF 0xF0 >= 240 so PDU2 and PS is the PGN's low
//   octet 0x04. (3 << 26) | (0xF0 << 16) | (0x04 << 8) | 0x00 = 0x0CF00400.
//
//   Request from address 0xF9 to the global address at priority 6, PGN 0xEA00: PF 0xEA < 240 so
//   PDU1 and PS is the destination. (6 << 26) | (0xEA << 16) | (0xFF << 8) | 0xF9 = 0x18EAFFF9.
//
//   Address Claimed from 0x80 at priority 6, PGN 0xEE00, broadcast:
//   (6 << 26) | (0xEE << 16) | (0xFF << 8) | 0x80 = 0x18EEFF80.
//
//   TP.CM and TP.DT are broadcast at priority 7 from address 0:
//   (7 << 26) | (0xEC << 16) | (0xFF << 8) = 0x1CECFF00 and 0x1CEBFF00.
void test_published_identifiers(void)
{
    uint32_t id = 0;
    J1939V.encode_id_args.id = &id;
    J1939V.encode_id_args.priority = 3;
    J1939V.encode_id_args.pgn = J1939_PGN_EEC1;
    J1939V.encode_id_args.sa = 0x00;
    J1939V.encode_id_args.da = J1939_ADDR_GLOBAL;
    J1939.encode_id(protocore_j1939_span());
    TEST_ASSERT_TRUE(J1939V.ok);
    TEST_ASSERT_EQUAL_HEX32(0x0CF00400u, id);

    CanFrame f;
    J1939V.build_request_args.out = &f;
    J1939V.build_request_args.sa = 0xF9;
    J1939V.build_request_args.da = J1939_ADDR_GLOBAL;
    J1939V.build_request_args.requested_pgn = J1939_PGN_EEC1;
    J1939.build_request(protocore_j1939_span());
    TEST_ASSERT_TRUE(J1939V.ok);
    TEST_ASSERT_EQUAL_HEX32(0x18EAFFF9u, f.id);
    TEST_ASSERT_TRUE(f.extended);
    TEST_ASSERT_FALSE(f.rtr);
    TEST_ASSERT_EQUAL_UINT8(3u, f.dlc);
    // The requested PGN is three octets, little-endian: 0x00F004 -> 04 F0 00.
    TEST_ASSERT_EQUAL_HEX8(0x04u, f.data[0]);
    TEST_ASSERT_EQUAL_HEX8(0xF0u, f.data[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, f.data[2]);

    J1939V.build_address_claim_args.out = &f;
    J1939V.build_address_claim_args.sa = 0x80;
    J1939V.build_address_claim_args.name = 0u;
    J1939.build_address_claim(protocore_j1939_span());
    TEST_ASSERT_TRUE(J1939V.ok);
    TEST_ASSERT_EQUAL_HEX32(0x18EEFF80u, f.id);
    TEST_ASSERT_EQUAL_UINT8(8u, f.dlc);

    J1939V.build_bam_cm_args.out = &f;
    J1939V.build_bam_cm_args.sa = 0x00;
    J1939V.build_bam_cm_args.pgn = J1939_PGN_EEC1;
    J1939V.build_bam_cm_args.total_size = 16;
    J1939.build_bam_cm(protocore_j1939_span());
    TEST_ASSERT_TRUE(J1939V.ok);
    TEST_ASSERT_EQUAL_HEX32(0x1CECFF00u, f.id);

    static const uint8_t CHUNK[7] = {1, 2, 3, 4, 5, 6, 7};
    J1939V.build_tp_dt_args.out = &f;
    J1939V.build_tp_dt_args.sa = 0x00;
    J1939V.build_tp_dt_args.da = J1939_ADDR_GLOBAL;
    J1939V.build_tp_dt_args.seq = 1;
    J1939V.build_tp_dt_args.chunk = CHUNK;
    J1939V.build_tp_dt_args.chunk_len = sizeof(CHUNK);
    J1939.build_tp_dt(protocore_j1939_span());
    TEST_ASSERT_TRUE(J1939V.ok);
    TEST_ASSERT_EQUAL_HEX32(0x1CEBFF00u, f.id);
}

// PF < 240 makes the frame peer-to-peer and PS the destination; PF >= 240 makes it broadcast and
// PS part of the PGN. 0xEF and 0xF0 sit either side of the boundary.
void test_pdu1_and_pdu2_boundary(void)
{
    uint32_t id = 0;
    J1939Id d;

    // PF 0xEF: the last PDU1 format. The PGN's low octet is dropped and the destination takes it.
    J1939V.encode_id_args.id = &id;
    J1939V.encode_id_args.priority = 6;
    J1939V.encode_id_args.pgn = 0x00EF00u;
    J1939V.encode_id_args.sa = 0x11;
    J1939V.encode_id_args.da = 0x22;
    J1939.encode_id(protocore_j1939_span());
    TEST_ASSERT_TRUE(J1939V.ok);
    J1939V.decode_id_args.id = id;
    J1939V.decode_id_args.out = &d;
    J1939.decode_id(protocore_j1939_span());
    TEST_ASSERT_TRUE(J1939V.ok);
    TEST_ASSERT_TRUE(d.pdu1);
    TEST_ASSERT_EQUAL_HEX32(0x00EF00u, d.pgn);
    TEST_ASSERT_EQUAL_HEX8(0x22u, d.da);
    TEST_ASSERT_EQUAL_HEX8(0x22u, d.ps);
    TEST_ASSERT_EQUAL_HEX8(0x11u, d.sa);

    // PF 0xF0: the first PDU2 format. The destination argument is ignored and PS is the PGN.
    J1939V.encode_id_args.id = &id;
    J1939V.encode_id_args.priority = 6;
    J1939V.encode_id_args.pgn = 0x00F004u;
    J1939V.encode_id_args.sa = 0x11;
    J1939V.encode_id_args.da = 0x22;
    J1939.encode_id(protocore_j1939_span());
    TEST_ASSERT_TRUE(J1939V.ok);
    J1939V.decode_id_args.id = id;
    J1939V.decode_id_args.out = &d;
    J1939.decode_id(protocore_j1939_span());
    TEST_ASSERT_TRUE(J1939V.ok);
    TEST_ASSERT_FALSE(d.pdu1);
    TEST_ASSERT_EQUAL_HEX32(0x00F004u, d.pgn);
    TEST_ASSERT_EQUAL_HEX8(J1939_ADDR_GLOBAL, d.da);
    TEST_ASSERT_EQUAL_HEX8(0x04u, d.ps);

    // EDP and DP are the PGN's two high bits and survive the round trip.
    J1939V.encode_id_args.id = &id;
    J1939V.encode_id_args.priority = 0;
    J1939V.encode_id_args.pgn = 0x3FF00u;
    J1939V.encode_id_args.sa = 0x33;
    J1939V.encode_id_args.da = J1939_ADDR_GLOBAL;
    J1939.encode_id(protocore_j1939_span());
    TEST_ASSERT_TRUE(J1939V.ok);
    J1939V.decode_id_args.id = id;
    J1939V.decode_id_args.out = &d;
    J1939.decode_id(protocore_j1939_span());
    TEST_ASSERT_TRUE(J1939V.ok);
    TEST_ASSERT_EQUAL_HEX32(0x3FF00u, d.pgn);
    TEST_ASSERT_EQUAL_UINT8(0u, d.priority);

    // A priority above 7 or a PGN above 18 bits has no encoding.
    J1939V.encode_id_args.id = &id;
    J1939V.encode_id_args.priority = 8;
    J1939V.encode_id_args.pgn = J1939_PGN_EEC1;
    J1939V.encode_id_args.sa = 0;
    J1939V.encode_id_args.da = 0;
    J1939.encode_id(protocore_j1939_span());
    TEST_ASSERT_FALSE(J1939V.ok);
    J1939V.encode_id_args.id = &id;
    J1939V.encode_id_args.priority = 0;
    J1939V.encode_id_args.pgn = 0x40000u;
    J1939V.encode_id_args.sa = 0;
    J1939V.encode_id_args.da = 0;
    J1939.encode_id(protocore_j1939_span());
    TEST_ASSERT_FALSE(J1939V.ok);
    J1939V.encode_id_args.id = NULL;
    J1939V.encode_id_args.priority = 0;
    J1939V.encode_id_args.pgn = J1939_PGN_EEC1;
    J1939V.encode_id_args.sa = 0;
    J1939V.encode_id_args.da = 0;
    J1939.encode_id(protocore_j1939_span());
    TEST_ASSERT_FALSE(J1939V.ok);
    J1939V.decode_id_args.id = id;
    J1939V.decode_id_args.out = NULL;
    J1939.decode_id(protocore_j1939_span());
    TEST_ASSERT_FALSE(J1939V.ok);
}

// J1939-81 packs NAME LSB-first: identity 0..20, manufacturer 21..31, ECU instance 32..34,
// function instance 35..39, function 40..47, reserved 48, vehicle system 49..55, vehicle system
// instance 56..59, industry group 60..62, arbitrary-address-capable 63. Setting one field at a
// time shows each lands on its own bits, and the frame carries NAME little-endian.
void test_name_bit_layout(void)
{
    J1939V.build_name_args.arbitrary_address_capable = 0;
    J1939V.build_name_args.industry_group = 0;
    J1939V.build_name_args.vehicle_system_instance = 0;
    J1939V.build_name_args.vehicle_system = 0;
    J1939V.build_name_args.function = 0;
    J1939V.build_name_args.function_instance = 0;
    J1939V.build_name_args.ecu_instance = 0;
    J1939V.build_name_args.manufacturer_code = 0;
    J1939V.build_name_args.identity_number = 0x1FFFFF;
    J1939.build_name(protocore_j1939_span());
    TEST_ASSERT_EQUAL_HEX64(0x00000000001FFFFFull, J1939V.value);
    J1939V.build_name_args.arbitrary_address_capable = 0;
    J1939V.build_name_args.industry_group = 0;
    J1939V.build_name_args.vehicle_system_instance = 0;
    J1939V.build_name_args.vehicle_system = 0;
    J1939V.build_name_args.function = 0;
    J1939V.build_name_args.function_instance = 0;
    J1939V.build_name_args.ecu_instance = 0;
    J1939V.build_name_args.manufacturer_code = 0x7FF;
    J1939V.build_name_args.identity_number = 0;
    J1939.build_name(protocore_j1939_span());
    TEST_ASSERT_EQUAL_HEX64(0x00000000FFE00000ull, J1939V.value);
    J1939V.build_name_args.arbitrary_address_capable = 0;
    J1939V.build_name_args.industry_group = 0;
    J1939V.build_name_args.vehicle_system_instance = 0;
    J1939V.build_name_args.vehicle_system = 0;
    J1939V.build_name_args.function = 0;
    J1939V.build_name_args.function_instance = 0;
    J1939V.build_name_args.ecu_instance = 7;
    J1939V.build_name_args.manufacturer_code = 0;
    J1939V.build_name_args.identity_number = 0;
    J1939.build_name(protocore_j1939_span());
    TEST_ASSERT_EQUAL_HEX64(0x0000000700000000ull, J1939V.value);
    J1939V.build_name_args.arbitrary_address_capable = 0;
    J1939V.build_name_args.industry_group = 0;
    J1939V.build_name_args.vehicle_system_instance = 0;
    J1939V.build_name_args.vehicle_system = 0;
    J1939V.build_name_args.function = 0;
    J1939V.build_name_args.function_instance = 0x1F;
    J1939V.build_name_args.ecu_instance = 0;
    J1939V.build_name_args.manufacturer_code = 0;
    J1939V.build_name_args.identity_number = 0;
    J1939.build_name(protocore_j1939_span());
    TEST_ASSERT_EQUAL_HEX64(0x000000F800000000ull, J1939V.value);
    J1939V.build_name_args.arbitrary_address_capable = 0;
    J1939V.build_name_args.industry_group = 0;
    J1939V.build_name_args.vehicle_system_instance = 0;
    J1939V.build_name_args.vehicle_system = 0;
    J1939V.build_name_args.function = 0xFF;
    J1939V.build_name_args.function_instance = 0;
    J1939V.build_name_args.ecu_instance = 0;
    J1939V.build_name_args.manufacturer_code = 0;
    J1939V.build_name_args.identity_number = 0;
    J1939.build_name(protocore_j1939_span());
    TEST_ASSERT_EQUAL_HEX64(0x0000FF0000000000ull, J1939V.value);
    J1939V.build_name_args.arbitrary_address_capable = 0;
    J1939V.build_name_args.industry_group = 0;
    J1939V.build_name_args.vehicle_system_instance = 0;
    J1939V.build_name_args.vehicle_system = 0x7F;
    J1939V.build_name_args.function = 0;
    J1939V.build_name_args.function_instance = 0;
    J1939V.build_name_args.ecu_instance = 0;
    J1939V.build_name_args.manufacturer_code = 0;
    J1939V.build_name_args.identity_number = 0;
    J1939.build_name(protocore_j1939_span());
    TEST_ASSERT_EQUAL_HEX64(0x00FE000000000000ull, J1939V.value);
    J1939V.build_name_args.arbitrary_address_capable = 0;
    J1939V.build_name_args.industry_group = 0;
    J1939V.build_name_args.vehicle_system_instance = 0x0F;
    J1939V.build_name_args.vehicle_system = 0;
    J1939V.build_name_args.function = 0;
    J1939V.build_name_args.function_instance = 0;
    J1939V.build_name_args.ecu_instance = 0;
    J1939V.build_name_args.manufacturer_code = 0;
    J1939V.build_name_args.identity_number = 0;
    J1939.build_name(protocore_j1939_span());
    TEST_ASSERT_EQUAL_HEX64(0x0F00000000000000ull, J1939V.value);
    J1939V.build_name_args.arbitrary_address_capable = 0;
    J1939V.build_name_args.industry_group = 7;
    J1939V.build_name_args.vehicle_system_instance = 0;
    J1939V.build_name_args.vehicle_system = 0;
    J1939V.build_name_args.function = 0;
    J1939V.build_name_args.function_instance = 0;
    J1939V.build_name_args.ecu_instance = 0;
    J1939V.build_name_args.manufacturer_code = 0;
    J1939V.build_name_args.identity_number = 0;
    J1939.build_name(protocore_j1939_span());
    TEST_ASSERT_EQUAL_HEX64(0x7000000000000000ull, J1939V.value);
    J1939V.build_name_args.arbitrary_address_capable = 1;
    J1939V.build_name_args.industry_group = 0;
    J1939V.build_name_args.vehicle_system_instance = 0;
    J1939V.build_name_args.vehicle_system = 0;
    J1939V.build_name_args.function = 0;
    J1939V.build_name_args.function_instance = 0;
    J1939V.build_name_args.ecu_instance = 0;
    J1939V.build_name_args.manufacturer_code = 0;
    J1939V.build_name_args.identity_number = 0;
    J1939.build_name(protocore_j1939_span());
    TEST_ASSERT_EQUAL_HEX64(0x8000000000000000ull, J1939V.value);
    // Bit 48 is reserved and no field reaches it.
    J1939V.build_name_args.arbitrary_address_capable = 1;
    J1939V.build_name_args.industry_group = 7;
    J1939V.build_name_args.vehicle_system_instance = 0x0F;
    J1939V.build_name_args.vehicle_system = 0x7F;
    J1939V.build_name_args.function = 0xFF;
    J1939V.build_name_args.function_instance = 0x1F;
    J1939V.build_name_args.ecu_instance = 7;
    J1939V.build_name_args.manufacturer_code = 0x7FF;
    J1939V.build_name_args.identity_number = 0x1FFFFF;
    J1939.build_name(protocore_j1939_span());
    TEST_ASSERT_EQUAL_HEX64(0xFFFEFFFFFFFFFFFFull, J1939V.value);

    CanFrame f;
    J1939V.build_address_claim_args.out = &f;
    J1939V.build_address_claim_args.sa = 0x80;
    J1939V.build_address_claim_args.name = 0x0123456789ABCDEFull;
    J1939.build_address_claim(protocore_j1939_span());
    TEST_ASSERT_TRUE(J1939V.ok);
    static const uint8_t LE[8] = {0xEF, 0xCD, 0xAB, 0x89, 0x67, 0x45, 0x23, 0x01};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(LE, f.data, sizeof(LE));
}

// A single-frame message carries up to 8 octets, and J1939 pads the unused ones with 0xFF, the
// "not available" octet, so a receiver never reads a stale zero as a real value.
void test_single_frame_padding(void)
{
    static const uint8_t DATA[3] = {0x11, 0x22, 0x33};
    CanFrame f;
    J1939V.build_message_args.out = &f;
    J1939V.build_message_args.priority = 3;
    J1939V.build_message_args.pgn = J1939_PGN_EEC1;
    J1939V.build_message_args.sa = 0x00;
    J1939V.build_message_args.da = J1939_ADDR_GLOBAL;
    J1939V.build_message_args.data = DATA;
    J1939V.build_message_args.len = 3;
    J1939.build_message(protocore_j1939_span());
    TEST_ASSERT_TRUE(J1939V.ok);
    TEST_ASSERT_EQUAL_UINT8(3u, f.dlc);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(DATA, f.data, 3);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, f.data[3]);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, f.data[7]);

    J1939V.build_message_args.out = &f;
    J1939V.build_message_args.priority = 3;
    J1939V.build_message_args.pgn = J1939_PGN_EEC1;
    J1939V.build_message_args.sa = 0;
    J1939V.build_message_args.da = 0xFF;
    J1939V.build_message_args.data = DATA;
    J1939V.build_message_args.len = 9;
    J1939.build_message(protocore_j1939_span());
    TEST_ASSERT_FALSE(J1939V.ok); // over 8 octets
    J1939V.build_message_args.out = &f;
    J1939V.build_message_args.priority = 3;
    J1939V.build_message_args.pgn = J1939_PGN_EEC1;
    J1939V.build_message_args.sa = 0;
    J1939V.build_message_args.da = 0xFF;
    J1939V.build_message_args.data = NULL;
    J1939V.build_message_args.len = 3;
    J1939.build_message(protocore_j1939_span());
    TEST_ASSERT_FALSE(J1939V.ok);
    J1939V.build_message_args.out = NULL;
    J1939V.build_message_args.priority = 3;
    J1939V.build_message_args.pgn = J1939_PGN_EEC1;
    J1939V.build_message_args.sa = 0;
    J1939V.build_message_args.da = 0xFF;
    J1939V.build_message_args.data = DATA;
    J1939V.build_message_args.len = 3;
    J1939.build_message(protocore_j1939_span());
    TEST_ASSERT_FALSE(J1939V.ok);
}

// A TP.DT packet carries seven octets, so a message of N octets needs ceil(N / 7) packets. The BAM
// announce frame is control 0x20, the size little-endian, that packet count, 0xFF, then the
// transported PGN little-endian.
void test_bam_announce(void)
{
    J1939V.tp_num_packets_args.total_size = 9;
    J1939.tp_num_packets(protocore_j1939_span());
    TEST_ASSERT_EQUAL_UINT8(2u, J1939V.u8); // 7 + 2
    J1939V.tp_num_packets_args.total_size = 14;
    J1939.tp_num_packets(protocore_j1939_span());
    TEST_ASSERT_EQUAL_UINT8(2u, J1939V.u8); // exactly two full packets
    J1939V.tp_num_packets_args.total_size = 15;
    J1939.tp_num_packets(protocore_j1939_span());
    TEST_ASSERT_EQUAL_UINT8(3u, J1939V.u8);
    J1939V.tp_num_packets_args.total_size = 16;
    J1939.tp_num_packets(protocore_j1939_span());
    TEST_ASSERT_EQUAL_UINT8(3u, J1939V.u8);

    CanFrame f;
    J1939V.build_bam_cm_args.out = &f;
    J1939V.build_bam_cm_args.sa = 0x20;
    J1939V.build_bam_cm_args.pgn = J1939_PGN_DM1;
    J1939V.build_bam_cm_args.total_size = 16;
    J1939.build_bam_cm(protocore_j1939_span());
    TEST_ASSERT_TRUE(J1939V.ok);
    static const uint8_t WANT[8] = {
        0x20,             // BAM
        0x10, 0x00,       // 16 octets, little-endian
        0x03,             // ceil(16 / 7)
        0xFF,             // reserved
        0xCA, 0xFE, 0x00, // PGN 0x00FECA, little-endian
    };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, f.data, sizeof(WANT));

    // BAM is for messages the single frame cannot carry: 9 octets up to the reassembly limit.
    J1939V.build_bam_cm_args.out = &f;
    J1939V.build_bam_cm_args.sa = 0x20;
    J1939V.build_bam_cm_args.pgn = J1939_PGN_DM1;
    J1939V.build_bam_cm_args.total_size = 8;
    J1939.build_bam_cm(protocore_j1939_span());
    TEST_ASSERT_FALSE(J1939V.ok);
    J1939V.build_bam_cm_args.out = &f;
    J1939V.build_bam_cm_args.sa = 0x20;
    J1939V.build_bam_cm_args.pgn = J1939_PGN_DM1;
    J1939V.build_bam_cm_args.total_size = 9;
    J1939.build_bam_cm(protocore_j1939_span());
    TEST_ASSERT_TRUE(J1939V.ok);
    J1939V.build_bam_cm_args.out = &f;
    J1939V.build_bam_cm_args.sa = 0x20;
    J1939V.build_bam_cm_args.pgn = J1939_PGN_DM1;
    J1939V.build_bam_cm_args.total_size = PROTOCORE_J1939_TP_MAX;
    J1939.build_bam_cm(protocore_j1939_span());
    TEST_ASSERT_TRUE(J1939V.ok);
    J1939V.build_bam_cm_args.out = &f;
    J1939V.build_bam_cm_args.sa = 0x20;
    J1939V.build_bam_cm_args.pgn = J1939_PGN_DM1;
    J1939V.build_bam_cm_args.total_size = PROTOCORE_J1939_TP_MAX + 1;
    J1939.build_bam_cm(protocore_j1939_span());
    TEST_ASSERT_FALSE(J1939V.ok);
    J1939V.build_bam_cm_args.out = NULL;
    J1939V.build_bam_cm_args.sa = 0x20;
    J1939V.build_bam_cm_args.pgn = J1939_PGN_DM1;
    J1939V.build_bam_cm_args.total_size = 16;
    J1939.build_bam_cm(protocore_j1939_span());
    TEST_ASSERT_FALSE(J1939V.ok);
}

// A whole broadcast transfer reassembles to exactly the octets that were sent, and the sequence
// number is 1-based with the last packet padded to eight octets.
void test_transport_protocol_reassembly(void)
{
    static const uint8_t MSG[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                    0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    J1939TpRx rx;
    J1939V.tp_reset_args.rx = &rx;
    J1939.tp_reset(protocore_j1939_span());

    CanFrame f;
    J1939V.build_bam_cm_args.out = &f;
    J1939V.build_bam_cm_args.sa = 0x20;
    J1939V.build_bam_cm_args.pgn = J1939_PGN_DM1;
    J1939V.build_bam_cm_args.total_size = (uint16_t)sizeof(MSG);
    J1939.build_bam_cm(protocore_j1939_span());
    TEST_ASSERT_TRUE(J1939V.ok);
    J1939V.tp_feed_args.rx = &rx;
    J1939V.tp_feed_args.f = &f;
    J1939.tp_feed(protocore_j1939_span());
    TEST_ASSERT_EQUAL_INT(J1939_TP_STARTED, J1939V.tp);
    TEST_ASSERT_EQUAL_UINT16(sizeof(MSG), rx.total_size);
    TEST_ASSERT_EQUAL_HEX32(J1939_PGN_DM1, rx.pgn);
    TEST_ASSERT_EQUAL_HEX8(0x20u, rx.sa);

    size_t sent = 0;
    for (uint8_t seq = 1; sent < sizeof(MSG); seq++)
    {
        uint8_t chunk = (uint8_t)((sizeof(MSG) - sent) < 7 ? (sizeof(MSG) - sent) : 7);
        J1939V.build_tp_dt_args.out = &f;
        J1939V.build_tp_dt_args.sa = 0x20;
        J1939V.build_tp_dt_args.da = J1939_ADDR_GLOBAL;
        J1939V.build_tp_dt_args.seq = seq;
        J1939V.build_tp_dt_args.chunk = MSG + sent;
        J1939V.build_tp_dt_args.chunk_len = chunk;
        J1939.build_tp_dt(protocore_j1939_span());
        TEST_ASSERT_TRUE(J1939V.ok);
        TEST_ASSERT_EQUAL_HEX8(seq, f.data[0]);
        TEST_ASSERT_EQUAL_UINT8(8u, f.dlc); // always eight octets on the wire
        sent += chunk;
        J1939V.tp_feed_args.rx = &rx;
        J1939V.tp_feed_args.f = &f;
        J1939.tp_feed(protocore_j1939_span());
        J1939TpResult r = J1939V.tp;
        TEST_ASSERT_EQUAL_INT(sent < sizeof(MSG) ? J1939_TP_PROGRESS : J1939_TP_COMPLETE, r);
    }
    TEST_ASSERT_EQUAL_UINT16(sizeof(MSG), rx.received);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(MSG, rx.buf, sizeof(MSG));
}

// An out-of-sequence packet aborts the session rather than writing into the wrong offset, and a
// packet from another source address is not folded into the open one.
void test_transport_protocol_rejects_bad_sequences(void)
{
    static const uint8_t MSG[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    J1939TpRx rx;
    J1939V.tp_reset_args.rx = &rx;
    J1939.tp_reset(protocore_j1939_span());

    CanFrame f;
    J1939V.build_bam_cm_args.out = &f;
    J1939V.build_bam_cm_args.sa = 0x20;
    J1939V.build_bam_cm_args.pgn = J1939_PGN_DM1;
    J1939V.build_bam_cm_args.total_size = 16;
    J1939.build_bam_cm(protocore_j1939_span());
    TEST_ASSERT_TRUE(J1939V.ok);
    J1939V.tp_feed_args.rx = &rx;
    J1939V.tp_feed_args.f = &f;
    J1939.tp_feed(protocore_j1939_span());
    TEST_ASSERT_EQUAL_INT(J1939_TP_STARTED, J1939V.tp);

    // A packet from a different source is ignored, not merged.
    J1939V.build_tp_dt_args.out = &f;
    J1939V.build_tp_dt_args.sa = 0x21;
    J1939V.build_tp_dt_args.da = J1939_ADDR_GLOBAL;
    J1939V.build_tp_dt_args.seq = 1;
    J1939V.build_tp_dt_args.chunk = MSG;
    J1939V.build_tp_dt_args.chunk_len = 7;
    J1939.build_tp_dt(protocore_j1939_span());
    TEST_ASSERT_TRUE(J1939V.ok);
    J1939V.tp_feed_args.rx = &rx;
    J1939V.tp_feed_args.f = &f;
    J1939.tp_feed(protocore_j1939_span());
    TEST_ASSERT_EQUAL_INT(J1939_TP_IGNORED, J1939V.tp);

    // Skipping packet 1 aborts the session.
    J1939V.build_tp_dt_args.out = &f;
    J1939V.build_tp_dt_args.sa = 0x20;
    J1939V.build_tp_dt_args.da = J1939_ADDR_GLOBAL;
    J1939V.build_tp_dt_args.seq = 2;
    J1939V.build_tp_dt_args.chunk = MSG;
    J1939V.build_tp_dt_args.chunk_len = 7;
    J1939.build_tp_dt(protocore_j1939_span());
    TEST_ASSERT_TRUE(J1939V.ok);
    J1939V.tp_feed_args.rx = &rx;
    J1939V.tp_feed_args.f = &f;
    J1939.tp_feed(protocore_j1939_span());
    TEST_ASSERT_EQUAL_INT(J1939_TP_ERROR, J1939V.tp);
    TEST_ASSERT_FALSE(rx.active);

    // A data packet with no session open is ignored.
    J1939V.build_tp_dt_args.out = &f;
    J1939V.build_tp_dt_args.sa = 0x20;
    J1939V.build_tp_dt_args.da = J1939_ADDR_GLOBAL;
    J1939V.build_tp_dt_args.seq = 1;
    J1939V.build_tp_dt_args.chunk = MSG;
    J1939V.build_tp_dt_args.chunk_len = 7;
    J1939.build_tp_dt(protocore_j1939_span());
    TEST_ASSERT_TRUE(J1939V.ok);
    J1939V.tp_feed_args.rx = &rx;
    J1939V.tp_feed_args.f = &f;
    J1939.tp_feed(protocore_j1939_span());
    TEST_ASSERT_EQUAL_INT(J1939_TP_IGNORED, J1939V.tp);

    // An announce whose packet count contradicts its size is an error, not a session.
    J1939V.build_bam_cm_args.out = &f;
    J1939V.build_bam_cm_args.sa = 0x20;
    J1939V.build_bam_cm_args.pgn = J1939_PGN_DM1;
    J1939V.build_bam_cm_args.total_size = 16;
    J1939.build_bam_cm(protocore_j1939_span());
    TEST_ASSERT_TRUE(J1939V.ok);
    f.data[3] = 2; // 16 octets need 3 packets
    J1939V.tp_feed_args.rx = &rx;
    J1939V.tp_feed_args.f = &f;
    J1939.tp_feed(protocore_j1939_span());
    TEST_ASSERT_EQUAL_INT(J1939_TP_ERROR, J1939V.tp);

    // CTS, EOM and Abort are originator-side control, not receiver-side session starts.
    J1939V.build_bam_cm_args.out = &f;
    J1939V.build_bam_cm_args.sa = 0x20;
    J1939V.build_bam_cm_args.pgn = J1939_PGN_DM1;
    J1939V.build_bam_cm_args.total_size = 16;
    J1939.build_bam_cm(protocore_j1939_span());
    TEST_ASSERT_TRUE(J1939V.ok);
    f.data[0] = J1939_TP_CM_CTS;
    J1939V.tp_feed_args.rx = &rx;
    J1939V.tp_feed_args.f = &f;
    J1939.tp_feed(protocore_j1939_span());
    TEST_ASSERT_EQUAL_INT(J1939_TP_IGNORED, J1939V.tp);

    // A standard (11-bit) frame is never J1939.
    J1939V.build_bam_cm_args.out = &f;
    J1939V.build_bam_cm_args.sa = 0x20;
    J1939V.build_bam_cm_args.pgn = J1939_PGN_DM1;
    J1939V.build_bam_cm_args.total_size = 16;
    J1939.build_bam_cm(protocore_j1939_span());
    TEST_ASSERT_TRUE(J1939V.ok);
    f.extended = PROTO_FALSE;
    J1939V.tp_feed_args.rx = &rx;
    J1939V.tp_feed_args.f = &f;
    J1939.tp_feed(protocore_j1939_span());
    TEST_ASSERT_EQUAL_INT(J1939_TP_IGNORED, J1939V.tp);

    static const uint8_t CHUNK[7] = {1, 2, 3, 4, 5, 6, 7};
    J1939V.build_tp_dt_args.out = &f;
    J1939V.build_tp_dt_args.sa = 0x20;
    J1939V.build_tp_dt_args.da = 0xFF;
    J1939V.build_tp_dt_args.seq = 0;
    J1939V.build_tp_dt_args.chunk = CHUNK;
    J1939V.build_tp_dt_args.chunk_len = 7;
    J1939.build_tp_dt(protocore_j1939_span());
    TEST_ASSERT_FALSE(J1939V.ok); // sequence is 1-based
    J1939V.build_tp_dt_args.out = &f;
    J1939V.build_tp_dt_args.sa = 0x20;
    J1939V.build_tp_dt_args.da = 0xFF;
    J1939V.build_tp_dt_args.seq = 1;
    J1939V.build_tp_dt_args.chunk = CHUNK;
    J1939V.build_tp_dt_args.chunk_len = 8;
    J1939.build_tp_dt(protocore_j1939_span());
    TEST_ASSERT_FALSE(J1939V.ok); // at most seven octets
    J1939V.build_tp_dt_args.out = &f;
    J1939V.build_tp_dt_args.sa = 0x20;
    J1939V.build_tp_dt_args.da = 0xFF;
    J1939V.build_tp_dt_args.seq = 1;
    J1939V.build_tp_dt_args.chunk = CHUNK;
    J1939V.build_tp_dt_args.chunk_len = 0;
    J1939.build_tp_dt(protocore_j1939_span());
    TEST_ASSERT_FALSE(J1939V.ok);
    J1939V.build_tp_dt_args.out = &f;
    J1939V.build_tp_dt_args.sa = 0x20;
    J1939V.build_tp_dt_args.da = 0xFF;
    J1939V.build_tp_dt_args.seq = 1;
    J1939V.build_tp_dt_args.chunk = NULL;
    J1939V.build_tp_dt_args.chunk_len = 7;
    J1939.build_tp_dt(protocore_j1939_span());
    TEST_ASSERT_FALSE(J1939V.ok);
}

// Build a PDU2 frame carrying @p data so a decoder can be handed a whole message.
static void pdu2(CanFrame *f, uint32_t pgn, const uint8_t data[8])
{
    J1939V.build_message_args.out = f;
    J1939V.build_message_args.priority = 3;
    J1939V.build_message_args.pgn = pgn;
    J1939V.build_message_args.sa = 0x00;
    J1939V.build_message_args.da = J1939_ADDR_GLOBAL;
    J1939V.build_message_args.data = data;
    J1939V.build_message_args.len = 8;
    J1939.build_message(protocore_j1939_span());
    TEST_ASSERT_TRUE(J1939V.ok);
}

// EEC1 (PGN 61444), J1939-71: byte 1 low nibble is the torque mode; bytes 2 and 3 are percent
// torque at 1 %/bit with a -125 % offset; bytes 4-5 are engine speed, little-endian, at
// 0.125 rpm/bit. Raw 10000 (0x2710) is 10000 * 0.125 = 1250 rpm; raw 250 is 250 - 125 = +125 %.
void test_decode_eec1(void)
{
    static const uint8_t DATA[8] = {0x03, 0x7D, 0xFA, 0x10, 0x27, 0xFF, 0xFF, 0xFF};
    CanFrame f;
    pdu2(&f, J1939_PGN_EEC1, DATA);

    J1939Eec1 e;
    memset(&e, 0, sizeof(e));
    J1939V.decode_eec1_args.f = &f;
    J1939V.decode_eec1_args.out = &e;
    J1939.decode_eec1(protocore_j1939_span());
    TEST_ASSERT_TRUE(J1939V.ok);
    TEST_ASSERT_EQUAL_UINT8(3u, e.torque_mode);
    TEST_ASSERT_EQUAL_INT16(0, e.drivers_demand_torque_pct); // raw 125 -> 0 %
    TEST_ASSERT_EQUAL_INT16(125, e.actual_engine_torque_pct);
    TEST_ASSERT_TRUE(e.engine_speed_valid);
    TEST_ASSERT_EQUAL_FLOAT(1250.0f, e.engine_speed_rpm);

    // A 1-octet SPN is valid to 0xFA and a 2-octet one to 0xFAFF; above that is not available.
    uint8_t na[8];
    memcpy(na, DATA, sizeof(na));
    na[1] = 0xFB;
    na[2] = 0xFF;
    na[3] = 0x00;
    na[4] = 0xFB;
    pdu2(&f, J1939_PGN_EEC1, na);
    J1939V.decode_eec1_args.f = &f;
    J1939V.decode_eec1_args.out = &e;
    J1939.decode_eec1(protocore_j1939_span());
    TEST_ASSERT_TRUE(J1939V.ok);
    TEST_ASSERT_EQUAL_INT16(J1939_TORQUE_NA, e.drivers_demand_torque_pct);
    TEST_ASSERT_EQUAL_INT16(J1939_TORQUE_NA, e.actual_engine_torque_pct);
    TEST_ASSERT_FALSE(e.engine_speed_valid);

    // A decoder only accepts its own PGN.
    pdu2(&f, J1939_PGN_ET1, DATA);
    J1939V.decode_eec1_args.f = &f;
    J1939V.decode_eec1_args.out = &e;
    J1939.decode_eec1(protocore_j1939_span());
    TEST_ASSERT_FALSE(J1939V.ok);
    pdu2(&f, J1939_PGN_EEC1, DATA);
    f.dlc = 7;
    J1939V.decode_eec1_args.f = &f;
    J1939V.decode_eec1_args.out = &e;
    J1939.decode_eec1(protocore_j1939_span());
    TEST_ASSERT_FALSE(J1939V.ok);
}

// ET1 (PGN 65262): coolant and fuel temperature at 1 degC/bit with a -40 offset, oil temperature
// as a 2-octet little-endian at 0.03125 degC/bit with a -273 offset.
//   raw 80  -> 80 - 40 = 40 degC
//   raw 0   -> -40 degC, the bottom of the range
//   raw 9376 (0x24A0) -> 9376 * 0.03125 = 293.0, less 273 = 20.0 degC
void test_decode_et1(void)
{
    static const uint8_t DATA[8] = {0x50, 0x00, 0xA0, 0x24, 0xFF, 0xFF, 0xFF, 0xFF};
    CanFrame f;
    pdu2(&f, J1939_PGN_ET1, DATA);

    J1939Et1 t;
    memset(&t, 0, sizeof(t));
    J1939V.decode_et1_args.f = &f;
    J1939V.decode_et1_args.out = &t;
    J1939.decode_et1(protocore_j1939_span());
    TEST_ASSERT_TRUE(J1939V.ok);
    TEST_ASSERT_TRUE(t.coolant_valid);
    TEST_ASSERT_EQUAL_FLOAT(40.0f, t.coolant_temp_c);
    TEST_ASSERT_TRUE(t.fuel_valid);
    TEST_ASSERT_EQUAL_FLOAT(-40.0f, t.fuel_temp_c);
    TEST_ASSERT_TRUE(t.oil_valid);
    TEST_ASSERT_EQUAL_FLOAT(20.0f, t.oil_temp_c);
}

// CCVS (PGN 65265): wheel-based vehicle speed in bytes 2-3, little-endian, 1/256 km/h per bit, and
// the 2-bit cruise-control-active state in byte 4. Raw 6400 (0x1900) is 6400 / 256 = 25 km/h.
void test_decode_ccvs(void)
{
    static const uint8_t DATA[8] = {0xFF, 0x00, 0x19, 0x01, 0xFF, 0xFF, 0xFF, 0xFF};
    CanFrame f;
    pdu2(&f, J1939_PGN_CCVS, DATA);

    J1939Ccvs c;
    memset(&c, 0, sizeof(c));
    J1939V.decode_ccvs_args.f = &f;
    J1939V.decode_ccvs_args.out = &c;
    J1939.decode_ccvs(protocore_j1939_span());
    TEST_ASSERT_TRUE(J1939V.ok);
    TEST_ASSERT_TRUE(c.speed_valid);
    TEST_ASSERT_EQUAL_FLOAT(25.0f, c.wheel_speed_kmh);
    TEST_ASSERT_EQUAL_UINT8(1u, c.cruise_active);
}

// VD (PGN 65248): trip and total distance as 4-octet little-endian counts at 0.125 km/bit. Raw
// 8000 is 1000.0 km; the totals are held as double because a full-scale odometer exceeds float's
// precision. Raw 0xFAFFFFFF is the last valid count, so 0xFB000000 upward is not available.
void test_decode_vd(void)
{
    static const uint8_t DATA[8] = {0x40, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFB};
    CanFrame f;
    pdu2(&f, J1939_PGN_VD, DATA);

    J1939Vd v;
    memset(&v, 0, sizeof(v));
    J1939V.decode_vd_args.f = &f;
    J1939V.decode_vd_args.out = &v;
    J1939.decode_vd(protocore_j1939_span());
    TEST_ASSERT_TRUE(J1939V.ok);
    TEST_ASSERT_TRUE(v.trip_valid);
    TEST_ASSERT_EQUAL_DOUBLE(1000.0, v.trip_km);
    TEST_ASSERT_FALSE(v.total_valid);

    // 0xFAFFFFFF is still a reading: 4211081215 * 0.125 = 526385151.875 km.
    static const uint8_t EDGE[8] = {0xFF, 0xFF, 0xFF, 0xFA, 0xFF, 0xFF, 0xFF, 0xFA};
    pdu2(&f, J1939_PGN_VD, EDGE);
    J1939V.decode_vd_args.f = &f;
    J1939V.decode_vd_args.out = &v;
    J1939.decode_vd(protocore_j1939_span());
    TEST_ASSERT_TRUE(J1939V.ok);
    TEST_ASSERT_TRUE(v.trip_valid);
    TEST_ASSERT_EQUAL_DOUBLE(526385151.875, v.trip_km);
}

// LFE (65266), AMB (65269) and IC1 (65270) each apply their published slope and offset per SPN.
//   LFE  raw 1000 (0x03E8) * 0.05 = 50.0 L/h; raw 512 (0x0200) / 512 = 1.0 km/L; raw 125 * 0.4 = 50 %
//   AMB  raw 200 * 0.5 = 100.0 kPa; raw 9376 * 0.03125 - 273 = 20.0 degC; raw 65 - 40 = 25 degC
//   IC1  raw 100 * 2 = 200.0 kPa boost; raw 60 - 40 = 20 degC intake; raw 20 * 0.05 = 1.0 kPa
void test_decode_lfe_amb_ic1(void)
{
    CanFrame f;

    static const uint8_t LFE[8] = {0xE8, 0x03, 0x00, 0x02, 0x00, 0x02, 0x7D, 0xFF};
    pdu2(&f, J1939_PGN_LFE, LFE);
    J1939Lfe l;
    memset(&l, 0, sizeof(l));
    J1939V.decode_lfe_args.f = &f;
    J1939V.decode_lfe_args.out = &l;
    J1939.decode_lfe(protocore_j1939_span());
    TEST_ASSERT_TRUE(J1939V.ok);
    TEST_ASSERT_TRUE(l.fuel_rate_valid);
    TEST_ASSERT_EQUAL_FLOAT(50.0f, l.fuel_rate_lph);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, l.instant_econ_kmpl);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, l.avg_econ_kmpl);
    TEST_ASSERT_TRUE(l.throttle_valid);
    TEST_ASSERT_EQUAL_FLOAT(50.0f, l.throttle_pct);

    static const uint8_t AMB[8] = {0xC8, 0xA0, 0x24, 0xA0, 0x24, 0x41, 0xA0, 0x24};
    pdu2(&f, J1939_PGN_AMB, AMB);
    J1939Amb a;
    memset(&a, 0, sizeof(a));
    J1939V.decode_amb_args.f = &f;
    J1939V.decode_amb_args.out = &a;
    J1939.decode_amb(protocore_j1939_span());
    TEST_ASSERT_TRUE(J1939V.ok);
    TEST_ASSERT_TRUE(a.baro_valid);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, a.baro_kpa);
    TEST_ASSERT_EQUAL_FLOAT(20.0f, a.cab_temp_c);
    TEST_ASSERT_EQUAL_FLOAT(20.0f, a.ambient_temp_c);
    TEST_ASSERT_TRUE(a.inlet_temp_valid);
    TEST_ASSERT_EQUAL_FLOAT(25.0f, a.inlet_temp_c);
    TEST_ASSERT_EQUAL_FLOAT(20.0f, a.road_temp_c);

    static const uint8_t IC1[8] = {0xC8, 0x64, 0x3C, 0x64, 0x14, 0xA0, 0x24, 0xC8};
    pdu2(&f, J1939_PGN_IC1, IC1);
    J1939Ic1 i;
    memset(&i, 0, sizeof(i));
    J1939V.decode_ic1_args.f = &f;
    J1939V.decode_ic1_args.out = &i;
    J1939.decode_ic1(protocore_j1939_span());
    TEST_ASSERT_TRUE(J1939V.ok);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, i.trap_inlet_kpa);
    TEST_ASSERT_TRUE(i.boost_valid);
    TEST_ASSERT_EQUAL_FLOAT(200.0f, i.boost_kpa);
    TEST_ASSERT_EQUAL_FLOAT(20.0f, i.intake_temp_c);
    TEST_ASSERT_EQUAL_FLOAT(200.0f, i.air_inlet_kpa);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, i.air_filter_kpa);
    TEST_ASSERT_EQUAL_FLOAT(20.0f, i.exhaust_temp_c);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, i.coolant_filter_kpa);
}

// DM1 (PGN 65226), J1939-73: the lamp-status octet holds four 2-bit lamps, MIL in bits 8-7 down to
// protect in bits 2-1, then the flash-status octet, then 4-octet DTCs in SPN conversion method 4:
//   SPN = d0 | d1 << 8 | (d2 >> 5) << 16, FMI = d2 & 0x1F, CM = d3 >> 7, OC = d3 & 0x7F.
// SPN 100 (engine oil pressure) is 0x000064, so d0 = 0x64, d1 = 0x00 and d2 = (0 << 5) | FMI.
// SPN 524287 is the widest a 19-bit field holds, so its top three bits fill d2's high nibble.
void test_decode_dm1(void)
{
    static const uint8_t BODY[10] = {
        0x54,             // 01 01 01 00: MIL on, red stop on, amber on, protect off
        0x00,             // flash status
        0x64, 0x00, 0x01, // SPN 100, FMI 1, conversion method 4
        0x05,             // CM 0, occurrence count 5
        0xFF, 0xFF, 0xE3, // SPN 524287, FMI 3
        0x82,             // CM 1, occurrence count 2
    };
    J1939Dm1 d;
    J1939Dtc dtc[4];
    memset(&d, 0, sizeof(d));
    memset(dtc, 0, sizeof(dtc));
    J1939V.decode_dm1_args.body = BODY;
    J1939V.decode_dm1_args.len = sizeof(BODY);
    J1939V.decode_dm1_args.out = &d;
    J1939V.decode_dm1_args.out_dtcs = dtc;
    J1939V.decode_dm1_args.max = 4;
    J1939.decode_dm1(protocore_j1939_span());
    TEST_ASSERT_TRUE(J1939V.ok);

    // 0x54 = 01 01 01 00: MIL 1, red stop 1, amber 1, protect 0.
    TEST_ASSERT_EQUAL_UINT8(1u, d.mil);
    TEST_ASSERT_EQUAL_UINT8(1u, d.red_stop);
    TEST_ASSERT_EQUAL_UINT8(1u, d.amber_warning);
    TEST_ASSERT_EQUAL_UINT8(0u, d.protect);

    TEST_ASSERT_EQUAL_UINT8(2u, d.dtc_count);
    TEST_ASSERT_EQUAL_UINT32(100u, dtc[0].spn);
    TEST_ASSERT_EQUAL_UINT8(1u, dtc[0].fmi);
    TEST_ASSERT_EQUAL_UINT8(0u, dtc[0].cm);
    TEST_ASSERT_EQUAL_UINT8(5u, dtc[0].oc);
    TEST_ASSERT_EQUAL_UINT32(524287u, dtc[1].spn);
    TEST_ASSERT_EQUAL_UINT8(3u, dtc[1].fmi);
    TEST_ASSERT_EQUAL_UINT8(1u, dtc[1].cm);
    TEST_ASSERT_EQUAL_UINT8(2u, dtc[1].oc);

    // The all-zero DTC is the "no active fault" placeholder and is not reported as a fault.
    static const uint8_t NONE[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    J1939V.decode_dm1_args.body = NONE;
    J1939V.decode_dm1_args.len = sizeof(NONE);
    J1939V.decode_dm1_args.out = &d;
    J1939V.decode_dm1_args.out_dtcs = dtc;
    J1939V.decode_dm1_args.max = 4;
    J1939.decode_dm1(protocore_j1939_span());
    TEST_ASSERT_TRUE(J1939V.ok);
    TEST_ASSERT_EQUAL_UINT8(0u, d.dtc_count);

    // The two status octets are the whole minimum; the lamps are readable without a DTC array.
    J1939V.decode_dm1_args.body = BODY;
    J1939V.decode_dm1_args.len = 2;
    J1939V.decode_dm1_args.out = &d;
    J1939V.decode_dm1_args.out_dtcs = NULL;
    J1939V.decode_dm1_args.max = 0;
    J1939.decode_dm1(protocore_j1939_span());
    TEST_ASSERT_TRUE(J1939V.ok);
    TEST_ASSERT_EQUAL_UINT8(0u, d.dtc_count);
    J1939V.decode_dm1_args.body = BODY;
    J1939V.decode_dm1_args.len = 1;
    J1939V.decode_dm1_args.out = &d;
    J1939V.decode_dm1_args.out_dtcs = dtc;
    J1939V.decode_dm1_args.max = 4;
    J1939.decode_dm1(protocore_j1939_span());
    TEST_ASSERT_FALSE(J1939V.ok);
    J1939V.decode_dm1_args.body = NULL;
    J1939V.decode_dm1_args.len = 10;
    J1939V.decode_dm1_args.out = &d;
    J1939V.decode_dm1_args.out_dtcs = dtc;
    J1939V.decode_dm1_args.max = 4;
    J1939.decode_dm1(protocore_j1939_span());
    TEST_ASSERT_FALSE(J1939V.ok);
    J1939V.decode_dm1_args.body = BODY;
    J1939V.decode_dm1_args.len = 10;
    J1939V.decode_dm1_args.out = NULL;
    J1939V.decode_dm1_args.out_dtcs = dtc;
    J1939V.decode_dm1_args.max = 4;
    J1939.decode_dm1(protocore_j1939_span());
    TEST_ASSERT_FALSE(J1939V.ok);

    // A caller array smaller than the DTC list stops at its capacity rather than overrunning.
    J1939V.decode_dm1_args.body = BODY;
    J1939V.decode_dm1_args.len = sizeof(BODY);
    J1939V.decode_dm1_args.out = &d;
    J1939V.decode_dm1_args.out_dtcs = dtc;
    J1939V.decode_dm1_args.max = 1;
    J1939.decode_dm1(protocore_j1939_span());
    TEST_ASSERT_TRUE(J1939V.ok);
    TEST_ASSERT_EQUAL_UINT8(1u, d.dtc_count);
}
