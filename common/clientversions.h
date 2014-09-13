#ifndef CLIENTVERSIONS_H
#define CLIENTVERSIONS_H

static const uint32 BIT_Unused = 1;
static const uint32 BIT_MacPC = 2;
static const uint32 BIT_MacIntel = 4;
static const uint32 BIT_MacPPC = 8;
static const uint32 BIT_Evolution = 16;

static const uint32 BIT_Mac = 14;
static const uint32 BIT_AllClients = 0xFFFFFFFF;

typedef enum {
	EQClientUnknown = 0,
	//EQClientUnused, //If we are going to add an older client before mac, it will need to be added here and to eq_dictionary :(
	EQClientMac,
	//EQClientEvolution,
	_EQClientCount,			// place new clients before this point (preferably, in release/attribute order)
	
	// Values below are not implemented, as yet...
	
	EmuNPC = _EQClientCount,
	EmuPet,
	
	_EmuClientCount			// array size for EQLimits
} EQClientVersion;

#endif /* CLIENTVERSIONS_H */
