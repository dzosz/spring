#pragma once

#include "System/float3.h"
#include "System/float4.h"

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
	// TODO most particles have animSpeed=0 so no need to update, SPLIT
	float3 value;
	int createFrame;
};

struct Sized {
	float value;
};

struct SizeChange {
	float sizeMod;
	float sizeGrowth;
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

struct Length {
	float value;
};

struct LengthChange {
	float v;
};

struct SmokeSizeChange {
	float v;
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
struct UpdateAnimParamsTag {};
struct Destroyed {};
struct PositionWindChangeTag{};
struct CastShadowTag{};
struct AirLosTag{};
struct VisibleTag{};
