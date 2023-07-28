#pragma once

#include "ECS_components.h"
	
inline void GrowSizeSystem(Sized& s, const SizeChange& change) {
	// TODO optionally multiply by timeOffset if executed in Draw context
	s.value = s.value * change.sizeMod + change.sizeGrowth; 
}

inline bool LifetimeSystem(Lifetime& l, const Decayrate& d) {
	l.value += d.value;
	return l.value >= 1.0;
}

inline bool LifetimeAlphaSystem(Alpha& l, const AlphaDecayrate& d) {
	l.v -= d.v;
	return l.v <= 0.0;
}

bool LifetimePositionAboveGroundSystem(const Position&);

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
	
	/* TODO rotation definitions differ in CExpGenSpawnable()
	 * 	rotVel = rotParams.x + rotParams.y * t;
	rotVal = rotParams.z + rotVel      * t;
	*/
	/* SimpleParticleSystem()
	 * p.rotVal += p.rotVel;
	   p.rotVel += rotParams.y; //rot accel
	*/
}

void UpdateAnimProgressSystem();
void UpdateDrawPosSystem();
void DrawSystem();
