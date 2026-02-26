#ifndef __AIRPTP_PTP_STRUCTS_H__
#define __AIRPTP_PTP_STRUCTS_H__

#define PTP_PORT_ID_SIZE 10
#define PTP_EVENT_PORT 319
#define PTP_GENERAL_PORT 320

enum ptp_msgtype
{
  PTP_MSGTYPE_SYNC = 0x00,
  PTP_MSGTYPE_DELAY_REQ = 0x01,
  PTP_MSGTYPE_PDELAY_REQ = 0x02,
  PTP_MSGTYPE_PDELAY_RESP = 0x03,
  PTP_MSGTYPE_FOLLOW_UP = 0x08,
  PTP_MSGTYPE_DELAY_RESP = 0x09,
  PTP_MSGTYPE_PDELAY_RESP_FOLLOW_UP = 0x0A,
  PTP_MSGTYPE_ANNOUNCE = 0x0B,
  PTP_MSGTYPE_SIGNALING = 0x0C,
  PTP_MSGTYPE_MANAGEMENT = 0x0D,
};

// From Wireshark, doesn't seem to be in IEEE1588-2008, maybe its in ptp v1?
enum ptp_flag
{
  PTP_FLAG_LI_61 = (1 << 0),
  PTP_FLAG_LI_59 = (1 << 1),
  PTP_FLAG_UTC_UNREASONABLE = (1 << 2),
  PTP_FLAG_TIMESCALE = (1 << 3),
  PTP_FLAG_TIME_TRACEABLE = (1 << 4),
  PTP_FLAG_FREQUENCY_TRACEABLE = (1 << 5),
  PTP_FLAG_SYNCRONIZATION_UNCERTAIN = (1 << 6),
  PTP_FLAG_ALTERNATE_MASTER = (1 << 8),
  PTP_FLAG_TWO_STEP = (1 << 9),
  PTP_FLAG_UNICAST = (1 << 10),
  PTP_FLAG_PROFILE_SPECIFIC2 = (1 << 13),
  PTP_FLAG_PROFILE_SPECIFIC1 = (1 << 14),
  PTP_FLAG_SECURITY = (1 << 15),
};

// Not used currently
struct ptp_scaled_ns
{
  uint16_t ns_hi;
  uint64_t ns_lo;
  uint16_t ns_frac;
} __attribute__((packed));

// Timestamp structure (10 bytes)
struct ptp_timestamp
{
  uint16_t seconds_hi;
  uint32_t seconds_low;
  uint32_t nanoseconds;
} __attribute__((packed));

// PTP Header (34 bytes)
struct ptp_header
{
  uint8_t messageType; // upper 4 bits are transportSpecific
  uint8_t versionPTP; // upper 4 bits are Reserved
  uint16_t messageLength;
  uint8_t domainNumber;
  uint8_t reserved1;
  uint16_t flags;
  int64_t correctionField; // int64 or uint64?
  uint32_t reserved2;
  uint8_t sourcePortIdentity[PTP_PORT_ID_SIZE];
  uint16_t sequenceId;
  uint8_t controlField;
  int8_t logMessageInterval;
} __attribute__((packed));

// Message 0x00
struct ptp_sync_message
{
  struct ptp_header header;
  struct ptp_timestamp originTimestamp;
} __attribute__((packed));

// Message 0x01
struct ptp_delay_req_message
{
  struct ptp_header header;
  struct ptp_timestamp originTimestamp;
} __attribute__((packed));

// Message 0x02
struct ptp_pdelay_req_message
{
  struct ptp_header header;
  struct ptp_timestamp originTimestamp;
  uint8_t reserved[10];
} __attribute__((packed));

// Message 0x03
struct ptp_pdelay_resp_message
{
  struct ptp_header header;
  struct ptp_timestamp requestReceiptTimestamp;
  uint8_t requestingPortIdentity[PTP_PORT_ID_SIZE];
} __attribute__((packed));

// Message 0x08
struct ptp_follow_up_message
{
  struct ptp_header header;
  struct ptp_timestamp preciseOriginTimestamp;
  uint8_t tlv_apple1[32];
  uint8_t tlv_apple2[20];
} __attribute__((packed));

// Message 0x09
struct ptp_delay_resp_message
{
  struct ptp_header header;
  struct ptp_timestamp receiveTimestamp;
  uint8_t requestingPortIdentity[PTP_PORT_ID_SIZE];
} __attribute__((packed));

// Message 0x0A
struct ptp_pdelay_resp_follow_up_message
{
  struct ptp_header header;
  struct ptp_timestamp responseOriginTimestamp;
  uint8_t requestingPortIdentity[PTP_PORT_ID_SIZE];
} __attribute__((packed));

// Message 0x0B
struct ptp_announce_message
{
  struct ptp_header header;
  struct ptp_timestamp originTimestamp;
  int16_t currentUtcOffset;
  uint8_t reserved;
  uint8_t grandmasterPriority1;
  uint32_t grandmasterClockQuality;
  uint8_t grandmasterPriority2;
  uint64_t grandmasterIdentity;
  uint16_t stepsRemoved;
  uint8_t timeSource;
  uint8_t tlv_path_trace[12]; // Apple speciality
} __attribute__((packed));

// Message 0x0C
struct ptp_signaling_message
{
  struct ptp_header header;
  uint8_t targetPortIdentity[PTP_PORT_ID_SIZE];
  uint8_t tlv_apple1[26];
  uint8_t tlv_apple2[36];
} __attribute__((packed));

// Message 0x0C - our internal variant
struct ptp_peer_signaling_message
{
  struct ptp_header header;
  uint8_t targetPortIdentity[PTP_PORT_ID_SIZE];
  uint8_t tlv_peer_info[43]; // TLV_MIN_SIZE + 2 * PTP_TLV_ORG_CODE_SIZE + sizeof(uint32 + uint8 + sockaddr_in6)
} __attribute__((packed));

#define PTP_TLV_MIN_SIZE 4 // 2 bytes type + 2 bytes length
#define PTP_TLV_ORG_CODE_SIZE 3
#define PTP_TLV_ORG_EXTENSION 0x0003
#define PTP_TLV_PATH_TRACE 0x0008

enum ptp_tlv_org
{
  PTP_TLV_ORG_IEEE = 0,
  PTP_TLV_ORG_APPLE = 1,
  PTP_TLV_ORG_OWN = 2,
};

enum ptp_tlv_org_ieee_subtype
{
  PTP_TLV_ORG_IEEE_FOLLOW_UP_INFO = 0,
  PTP_TLV_ORG_IEEE_MESSAGE_INTERNAL_REQUEST = 1,
};

enum ptp_tlv_org_apple_subtype
{
  PTP_TLV_ORG_APPLE_UNKNOWN1 = 0,
  PTP_TLV_ORG_APPLE_CLOCK_ID = 1,
  PTP_TLV_ORG_APPLE_UNKNOWN5 = 2,
};

enum ptp_tlv_org_own_subtype
{
  PTP_TLV_ORG_OWN_PEER_ADD = 0,
  PTP_TLV_ORG_OWN_PEER_DEL = 1,
};

struct ptp_tlv_org_subtype_map
{
  int index;
  uint8_t code[PTP_TLV_ORG_CODE_SIZE];
  char *name;
  int (*handler)(struct airptp_daemon *, const char *, struct ptp_tlv_org_subtype_map *, uint8_t *, size_t);
};

struct ptp_tlv_org_map
{
  enum ptp_tlv_org index;
  uint8_t code[PTP_TLV_ORG_CODE_SIZE];
  char *name;
  struct ptp_tlv_org_subtype_map *subtypes;
  int n_subtypes;
};

#endif // __AIRPTP_PTP_STRUCTS_H__
