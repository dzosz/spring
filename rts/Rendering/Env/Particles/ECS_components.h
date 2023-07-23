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

struct AnimProgress {
	float value;
};

struct AnimParams {
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

//BitmapMuzzleFlame specific. maybe put it into the tag?
struct FrontOffset {
	float value;
};


class AtlasedTexture;
class CColorMap;
struct RenderData {
	AtlasedTexture* texture;
	AtlasedTexture* extraTexture; // sideTexture?
	CColorMap* colorMap;
	bool directional;
};

// Drawable tags
struct SimpleParticleSystemTag {};
struct CBitmapMuzzleFlameTag {};
