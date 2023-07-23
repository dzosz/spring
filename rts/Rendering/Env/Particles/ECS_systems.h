#pragma once

#include "ECS_components.h"
	
inline void GrowSizeSystem(Sized& s, const SizeChange& change) {
	// TODO optionally multiply by timeOffset if executed in Draw context
	s.value = s.value * change.sizeMod + change.sizeGrowth; 
}

inline bool LifetimeSystem(Lifetime& l, const Decayrate& d) {
	l.value += d.value;	
	return l.value < 1.0;
}

inline void PositionSystem(Position& p, const Speed& s) {
	p.value += s.value;		
}

inline void SpeedParticlePhysSystem(Speed& s, const ParticlePhys& phys) {	
	s.value += phys.gravity;
	s.value *= phys.airdrag;
}

inline void RotationSystem(Rotation& rot, const RotParams& rotParams, float t) {
	// rotParams.y is acceleration in angle per frame^2
	rot.rotVel = rotParams.value.x + rotParams.value.y * t;
	rot.rotVal = rotParams.value.z + rot.rotVel      * t;
}

void UpdateAnimProgressSystem();
void UpdateDrawPosSystem();
void DrawSystem();
