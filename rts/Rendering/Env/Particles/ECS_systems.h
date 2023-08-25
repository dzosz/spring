#pragma once

#include "ECS_components.h"
#include "lib/entt/entity/view.hpp"
#include "lib/entt/fwd.hpp"
#include "lib/entt/entity/registry.hpp"
#include "System/SpringMath.h"
	
inline void GrowSizeSystem(entt::view<entt::get_t<Sized, const SizeChange>> view) {
	// TODO optionally multiply by timeOffset if executed in Draw context
	view.each([&](auto ent, auto& size, const auto& sizeChange) {
		size.value = size.value * sizeChange.sizeMod + sizeChange.sizeGrowth;
	});
}

inline void GrowLengthSystem(entt::view<entt::get_t<Length, const LengthChange>> view) {
	view.each([&](auto ent, auto& length, const auto& lengthChange) {
		length.value += lengthChange.v;
	});
}

inline void LifetimeSystem(entt::registry& reg) {
	reg.view<Lifetime, const Decayrate>().each([&](const auto ent, auto& l, const auto& d) {
		l.value += d.value;
		if (l.value >= 1.0)
			reg.emplace_or_replace<Destroyed>(ent);
	});
}

inline void LifetimeHeatSystem(entt::registry& reg) {
	reg.view<Heat, const HeatDecay>().each([&](const auto ent, auto& h, const auto& r) {
		h.v -= r.v;
		if (h.v <= 0.0)
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

inline void LifetimeFlameSystem(entt::registry& reg) {
	reg.view<LifetimeFlame, const Sized>().each([&](const auto ent, auto& l, const auto& d) {
		l.v++;
		if (l.v > 4+ d.value * 30)
			reg.emplace_or_replace<Destroyed>(ent);
	});
}

inline void GrowSmokeSizeSystem(entt::view<entt::get_t<Sized, const SmokeSizeChange>> view) {
	view.each([&](auto ent, auto& s, const auto& smokeSize) {
		auto startSize = smokeSize.v;
		auto size = s.value;
		s.value += (startSize - size) * 0.2f * (size < startSize);
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

void WindPositionSystem(entt::view<entt::get_t<const PositionWindChangeTag, Position, const Lifetime>> view);

inline void SpeedParticlePhysSystem(entt::view<entt::get_t<Speed, const ParticlePhys>> view) {
	view.each([&](const auto ent, auto& s, const auto& phys) {
		s.value += phys.gravity;
		s.value *= phys.airdrag;
	});
}

void RotationSystem(entt::view<entt::get_t<Rotation, const RotParams, const AnimParams>> view);

extern entt::registry registry;
inline bool UpdateEndPos(unsigned int entId, float3 p, float3 dir)
{
	auto ent = entt::entity(entId);
	auto& view = registry;
	if (!registry.valid(ent)) {
		return false;
	}
	
	auto& position = view.template get<Position>(ent).value;
	
	auto& drawRadius = view.template get<DrawRadius>(ent).value;
	
	auto& d = view.template get<SmokeTrail>(ent);
	
	d.pos1 = p;
	d.dir1 = dir;

	const float dist = d.pos1.distance(d.pos2);

	d.drawSegmented = false;
	position = (d.pos1 + d.pos2) * 0.5f;
	
	drawRadius = dist;
	//TODO sortDistOffset = 10.f + dist * 0.5f; // so that missile's engine flame gets rendered above the trail

	if (d.dir1.dot(d.dir2) < 0.98f) {
		float3 dirpos1 = d.pos1 - d.dir1 * dist * 0.33f;
		float3 dirpos2 = d.pos2 + d.dir2 * dist * 0.33f;
		d.midpos = CalcBeizer(0.5f, d.pos1, dirpos1, dirpos2, d.pos2);
		d.middir = (d.dir1 + d.dir2).ANormalize();
		d.drawSegmented = true;
	}
	return true;
}

class CProjectile;
void PreDrawSystem();
void DrawSystem(const std::vector<std::pair<std::pair<int, float>, CProjectile*>>&);
