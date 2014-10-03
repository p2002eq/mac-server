#ifndef CLIENTVERSIONS_H
#define CLIENTVERSIONS_H

//static const uint32 BIT Trilogy = 1;
static const uint32 BIT_Mac = 1;
//static const uint32 BIT Evolution = 4;
static const uint32 BIT_AllClients = 0xFFFFFFFF;

typedef enum
{
	EQClientUnknown = 0,
//	EQClientTrilogy,
	EQClientMac,
	EQClientEvolution,
	_EQClientCount,			// place new clients before this point (preferably, in release/attribute order)

	// Values below are not implemented, as yet...

	EmuNPC = _EQClientCount,
	EmuPet,

	_EmuClientCount			// array size for EQLimits
} EQClientVersion;

static const char* EQClientVersionName(EQClientVersion version)
{
	switch (version)
	{
	case EQClientUnknown:
		return "EQClientUnknown";
	case EQClientMac:
		return "EQClientMac";
	case EQClientEvolution:
		return "EQClientEvolution";
	case EmuNPC:
		return "EmuNPC";
	case EmuPet:
		return "EmuPet";
	default:
		return "ERROR: Invalid EQClientVersion";
	};
}

#endif /* CLIENTVERSIONS_H */
