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
};

struct Sized {
	float value;
};

struct SizeChange {
	float sizeMod;
	float sizeGrowth;
};

class AtlasedTexture;
class CColorMap;
struct RenderData {
	AtlasedTexture* texture;
	CColorMap* colorMap;
	bool directional;
	
};

void SizeSystem(Sized& s, SizeChange& change) {
	s.value = s.value * change.sizeMod + change.sizeGrowth;
}

bool is_animated(const float3& animParams) {
	return static_cast<int>(animParams.x) <= 1 && static_cast<int>(animParams.y) <= 1;
}

void AnimationSystem(AnimProgress animProgress, const AnimParams& animParams, const float& t) {
	// const float t = (gs->frameNum - createFrame + globalRendering->timeOffset);
	if (static_cast<int>(animParams.value.x) <= 1 && static_cast<int>(animParams.value.y) <= 1) {
		// animProgress = 0.0f; // already zero
		return;
	}
	
	// TODO remove this switch
	const float animSpeed = math::fabs(animParams.value.z);
	if (animParams.value.z < 0.0f) {
		animProgress.value = 1.0f - math::fabs(math::fmod(t, 2.0f * animSpeed) / animSpeed - 1.0f);
	}
	else {
		animProgress.value = math::fmod(t, animSpeed) / animSpeed;
	}
}

bool LifetimeSystem(Lifetime& l, const Decayrate& d) {
	l.value += d.value;	
	return l.value < 1.0;
}

void PositionSystem(Position& p, Speed& s, const ParticlePhys& phys) {
	p.value += s.value;		
	// TODO update speed here or in separate system?
	s.value += phys.gravity;
	s.value *= phys.airdrag;
}

void RotationSystem(Rotation& rot, const RotParams& rotParams, float t) {
	//const float t = (gs->frameNum - createFrame + globalRendering->timeOffset);
	// rotParams.y is acceleration in angle per frame^2
	rot.rotVel = rotParams.value.x + rotParams.value.y * t;
	rot.rotVal = rotParams.value.z + rot.rotVel      * t;
}

void DrawParticle(const Position& pos, const Speed& speed, const Sized& sized,
				  const Lifetime& l, const RenderData& data, const Rotation& rot,
				  const AnimParams& animParams, const AnimProgress& animProgress);
