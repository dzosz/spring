#pragma once

#include "ECS_components.h"
#include "lib/entt/entity/view.hpp"
#include "lib/entt/fwd.hpp"
#include "lib/entt/entity/registry.hpp"
#include "Rendering/GlobalRendering.h"
	
inline void GrowSizeSystem(Sized& s, const SizeChange& change) {
	// TODO optionally multiply by timeOffset if executed in Draw context
	s.value = s.value * change.sizeMod + change.sizeGrowth; 
}

inline void LifetimeSystem(entt::registry& reg) {
	reg.view<Lifetime, const Decayrate>().each([&](const auto ent, auto& l, const auto& d) {
		l.value += d.value;
		if (l.value >= 1.0)
			reg.emplace<Destroyed>(ent);
	});
}

inline void LifetimeAlphaSystem(entt::registry& reg) {
	reg.view<Alpha, const AlphaDecayrate>().each([&](const auto ent, auto& l, const auto& d) {
		l.v -= d.v;
		if (l.v <= 0.0)
			reg.emplace<Destroyed>(ent);
	});
}

void LifetimePositionAboveGroundSystem(entt::registry& reg);

inline void DeleteDestroyedSystem(entt::registry& reg) {
	auto d = reg.view<Destroyed>();
	reg.destroy(d.begin(), d.end());
}

inline void PositionSystem(entt::view<entt::get_t<Position, const Speed>> view) {
	view.each([&](auto& p, const auto& s) {
		p.value += s.value;
	});
}


inline void SpeedParticlePhysSystem(entt::view<entt::get_t<Speed, const ParticlePhys>> view) {
	view.each([&](const auto ent, auto& s, const auto& phys) {
		s.value += phys.gravity;
		s.value *= phys.airdrag;
	});
}

inline void RotationSystem(entt::view<entt::get_t<Rotation, const RotParams, const AnimParams>> view) {
	float t = globalRendering->timeOffset;
	// const float t = (registry.ctx().get<PhysDelta>().frameNum - animParams.createFrame + registry.ctx().get<PhysDelta>().timeOffset); // TODO
	view.each([&](const auto ent, auto& rot, const auto& rotParams, auto& animParams) {
		// rotParams.y is acceleration in angle per frame^2
		rot.rotVel = rotParams.value.x + rotParams.value.y * t;
		rot.rotVal = rotParams.value.z + rot.rotVel      * t;
	});
	
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
