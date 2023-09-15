#pragma once

#include "System/float3.h"
#include "System/float4.h"
#include "Sim/Projectiles/ExpGenSpawnableMemberInfo.h"

struct ParticlePhys
{
	float3 gravity;
	float airdrag;
};

struct Position {
	float3 value;
};

struct Direction {
	float3 value;
};

struct DrawPosition {
	float3 value;
};

struct ParticleIndex {
	unsigned v;
};

struct DrawOrder {
	int drawOrder;
	float distanceFromCamera; // should be negative as we want to draw far object first
};

struct DrawRadius {
	float value;
};

struct Speed {
	float4 value;
};

struct Rotation {
	float rotVal = 0.0f;
	float rotVel = 0.0f;
};

struct RotParams {
	float3 value;
};

struct Lifetime {
	float value;
};

struct Decayrate {
	float value;
};

struct LifetimeFlame {
	float v;
};

struct FlameSizeChange {
	float v;
};

struct Heat {
	float v;
};

struct HeatDecay {
	float v;
};

struct MaxHeat {
	float v;
};

struct Alpha {
	float v;
};

struct AlphaDecayrate {
	float v;
};

struct AnimProgress {
	float value;
};

struct AnimParams {
	float3 value;
};


struct AnimParams2 {
	float progress;
	float3 params;
	int createFrame;
};

struct CreateFrame {
	int v;
};

struct Sized {
	float value;
};

struct SizeChange {
	float sizeMod;
	float sizeGrowth;
};

struct SizeModMod {
	float v;
};

struct LifetimeSizeChange {
	float sizeGrowth;
};

struct AlliedTeam {
	int value;
};

struct PhysDelta {
	float timeOffset; // globalRendering->timeOffset, time since last frame
	int frameNum;
};

struct DrawMode {
	bool drawRefraction;
};

struct Length {
	float value;
};

struct LengthChange {
	float v;
};

struct SmokeSized {
	float v;
};

struct SmokeSizeChange {
	float sizeGrowth;
	float startSize;
};

struct Width {
	float value;
};

//BitmapMuzzleFlame specific. maybe put it into the tag?
struct FrontOffset {
	float value;
};


class AtlasedTexture;
class CColorMap;
struct RenderData {
	const AtlasedTexture* texture;
	AtlasedTexture* extraTexture; // sideTexture?
	CColorMap* colorMap;
	bool directional;
};

struct Color {
	float3 v;
};

struct SmokeTrail {
	// TODO these things are almost never updated so making god class
	int lifePeriod;
	float3 pos1;
	float3 pos2;
	float origSize;

	float3 dir1;
	float3 dir2;

	float3 midpos;
	float3 middir;
	bool drawSegmented;
	bool firstSegment;
	bool lastSegment;
};


struct SimpleParticle {
	float3 pos;
	float3 speed;
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
	
	float drawRadius;
	DrawOrder drawo;
	RenderData r;
};

struct BitmapMuzzleFlame {
	float3 pos;
	float3 dir;
	
	float size;
	float length;
	float sizeGrowth;
	float frontOffset;
	int ttl;

	float rotVal;
	float rotVel;
	float3 rotParams;

	int allyteam;
	
	float decayrate;
};

// Drawable tags
struct SimpleParticleSystemTag {};
struct CBitmapMuzzleFlameTag {};
struct CDirtProjectileTag {};
struct CExploSpikeProjectileTag{};
struct CHeatCloudProjectileTag{};
struct CMuzzleFlameTag{};
struct CSmokeProjectileTag{};
struct CSmokeTrailProjectileTag{};


// System behavior Tags
struct GroundCollisionTag {};
struct Destroyed {};
struct PositionWindChangeTag{};
struct CastShadowTag{};
struct AirLosTag{};
struct VisibleTag{};
