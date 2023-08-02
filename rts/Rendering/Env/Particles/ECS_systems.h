#pragma once

#include "ECS_components.h"
#include "lib/entt/entity/view.hpp"
#include "lib/entt/fwd.hpp"
#include "lib/entt/entity/registry.hpp"
	
inline void GrowSizeSystem(Sized& s, const SizeChange& change) {
	// TODO optionally multiply by timeOffset if executed in Draw context
	s.value = s.value * change.sizeMod + change.sizeGrowth; 
}

inline void LifetimeSystem(entt::registry& reg) {
	reg.view<Lifetime, const Decayrate>().each([&](const auto ent, auto& l, const auto& d) {
		l.value += d.value;
		if (l.value >= 1.0)
			reg.emplace_or_replace<Destroyed>(ent);
	});
}

inline void LifetimeAlphaSystem(entt::registry& reg) {
	reg.view<Alpha, const AlphaDecayrate>().each([&](const auto ent, auto& l, const auto& d) {
		l.v -= d.v;
		if (l.v <= 0.0)
			reg.emplace_or_replace<Destroyed>(ent);
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

void RotationSystem(entt::view<entt::get_t<Rotation, const RotParams, const AnimParams>> view);

class CProjectile;
void PreDrawSystem();
void DrawSystem(const std::vector<std::pair<std::pair<float, float>, CProjectile*>>&);
