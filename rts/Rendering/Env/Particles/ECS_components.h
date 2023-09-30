#pragma once

#include "System/float3.h"
#include "System/float4.h"
#include "Sim/Projectiles/ExpGenSpawnableMemberInfo.h"

struct DrawOrder {
	int drawOrder;
	float distanceFromCamera; // should be negative as we want to draw far object first
};


class AtlasedTexture;
class CColorMap;
struct RenderData {
	const AtlasedTexture* texture;
	AtlasedTexture* extraTexture; // sideTexture?
	CColorMap* colorMap;
	bool directional;
};

struct PhysDelta {
	float timeOffset; // globalRendering->timeOffset, time since last frame
	int frameNum;
};

struct DrawMode {
	int mode; // 0 normal, 1 refraction, 2 reflection, 3 shadow
};

struct SmokeTrail {
	float3 pos1;
	float3 pos2;
	float origSize;

	int creationTime;
	int lifeTime;
	int lifePeriod;
	float color;
	float3 dir1;
	float3 dir2;

	float3 dirpos1;
	float3 dirpos2;
	float3 midpos;
	float3 middir;
	bool drawSegmented;
	bool firstSegment;
	bool lastSegment;
	
	int allyteam;
	bool castShadow;
	bool useAirLos;
	
	bool visible;
	bool visibleRefraction;
	bool visibleReflection;
	bool visibleShadow;
	
	float3 pos;
	float4 speed;
	
	float3 drawPos;
	float drawRadius;	
	DrawOrder drawo;
	RenderData r;
	
	float progress;
	float3 params;
	int createFrame;
};


struct SimpleParticle {
	float3 pos;
	float4 speed;
	float3 gravity;
	float airdrag; 

	float rotVal;
	float rotVel;
	float3 rotParams;

	float life;
	float decayrate;
	float size;
	float sizeGrowth;
	float sizeMod;
	
	int allyteam;
	bool castShadow;
	bool useAirLos;
	
	float3 drawPos;
	float drawRadius;
	DrawOrder drawo;
	RenderData r;
	
	float progress;
	float3 params;
	int createFrame;	
	
	bool visible;
	bool visibleRefraction;
	bool visibleReflection;
	bool visibleShadow;
	
	std::array<unsigned char, 4> color;
};

struct BitmapMuzzleFlame {
	float3 pos;
	float4 speed;
	float3 dir;
	
	float size;
	float length;
	float sizeGrowth;
	float frontOffset;
	int ttl;
	float decayrate;

	float rotVal;
	float rotVel;
	float3 rotParams;

	int allyteam;
	bool castShadow;
	bool useAirLos;
	
	bool visible;
	bool visibleRefraction;
	bool visibleReflection;
	bool visibleShadow;

	float3 drawPos;
	float drawRadius;
	DrawOrder drawo;
	RenderData r;
	
	float progress;
	float3 params;
	int createFrame;
};

struct DirtProjectile {
	float alpha;
	float alphaFalloff;
	float size;
	float sizeExpansion;
	
	float mygravity;
	float slowdown;
	float3 color;
	
	int allyteam;
	bool castShadow;
	bool useAirLos;
	
	bool visible;
	bool visibleRefraction;
	bool visibleReflection;
	bool visibleShadow;
	
	float3 pos;
	float4 speed;
	
	float3 drawPos;
	float drawRadius;
	DrawOrder drawo;
	RenderData r;
	
	float progress;
	float3 params;
	int createFrame;
};

struct HeatCloudProjectile {
	float heat;
	float maxheat;
	float heatFalloff;

	float size;

	float sizeGrowth;
	float sizemod;
	float sizemodmod;
	
	float3 pos;
	float4 speed;
	
	float rotVal;
	float rotVel;
	float3 rotParams;
	
	int allyteam;
	bool castShadow;
	bool useAirLos;
	
	bool visible;
	bool visibleRefraction;
	bool visibleReflection;
	bool visibleShadow;
		
	float3 drawPos;
	float drawRadius;
	DrawOrder drawo;
	RenderData r;
	
	float progress;
	float3 params;
	int createFrame;
};

struct SmokeProjectile {
	float color;
	float age;
	float ageSpeed;
	
	float size;
	float startSize;
	float sizeExpansion;
	
	float3 pos;
	float4 speed;
	
	int allyteam;
	bool castShadow;
	bool useAirLos;
	
	bool visible;
	bool visibleRefraction;
	bool visibleReflection;
	bool visibleShadow;
	
	float3 drawPos;
	float drawRadius;
	DrawOrder drawo;
	RenderData r;
	
	float progress;
	float3 params;
	int createFrame;
};


struct MuzzleFlame {
	float size;
	int age;
	int numFlame;
	int numSmoke;
	
	int index;
	float3 direction;
	
	float3 pos;
	float4 speed;
	float3 dir;
	
	int allyteam;
	bool castShadow;
	bool useAirLos;
	
	bool visible;
	bool visibleRefraction;
	bool visibleReflection;
	bool visibleShadow;
	
	float3 drawPos;
	float drawRadius;
	DrawOrder drawo;
	RenderData r;
	
	float progress;
	float3 params;
	int createFrame;
};

struct ExploSpikeProjectile {
	float length;
	float width;
	float alpha;
	float alphaDecay;
	float lengthGrowth;
	float3 color;
	
	int allyteam;
	bool castShadow;
	bool useAirLos;
	
	bool visible;
	bool visibleRefraction;
	bool visibleReflection;
	bool visibleShadow;
	
	float3 pos;
	float4 speed;
	float3 dir;
	
	float3 drawPos;
	float drawRadius;
	DrawOrder drawo;
	RenderData r;
	
	float progress;
	float3 params;
	int createFrame;
};

struct BubbleProjectile {	
	int ttl;
	float alpha;
	float size;
	float startSize;
	float sizeExpansion;
	
	float3 pos;
	float4 speed;
	
	int allyteam;
	bool castShadow;
	bool useAirLos;
	
	bool visible;
	bool visibleRefraction;
	bool visibleReflection;
	bool visibleShadow;
	
	float3 drawPos;
	float drawRadius;
	DrawOrder drawo;
	RenderData r;
	
	float progress;
	float3 params;
	int createFrame;
};

// System behavior Tags
struct Destroyed {};
