#ifndef EQEMU_WATER_MAP_V1_H
#define EQEMU_WATER_MAP_V1_H

#include "water_map.h"

#pragma pack(1)
typedef struct ZBSP_Node {
	int32 node_number;
	float normal[3], splitdistance;
	int32 region;
	int32 special;
	int32 left, right;
} ZBSP_Node;
#pragma pack()

class WaterMapV1 : public WaterMap
{
public:
	WaterMapV1();
	~WaterMapV1();
	
	virtual WaterRegionType ReturnRegionType(const xyz_location& location) const;
	virtual bool InWater(const xyz_location& location) const;
	virtual bool InVWater(const xyz_location& location) const;
	virtual bool InLava(const xyz_location& location) const;
	virtual bool InLiquid(const xyz_location& location) const;
	
protected:
	virtual bool Load(FILE *fp);

private:
	WaterRegionType BSPReturnRegionType(int32 node_number, const xyz_location& location) const;
	ZBSP_Node* BSP_Root;

	friend class WaterMap;
};

#endif
