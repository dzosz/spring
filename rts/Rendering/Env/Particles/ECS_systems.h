#pragma once

#include "ECS_components.h"
#include "lib/entt/src/entt/entity/view.hpp"
#include "lib/entt/src/entt/fwd.hpp"
#include "lib/entt/src/entt/entity/registry.hpp"
#include "System/SpringMath.h"
#include "tracy/Tracy.hpp"

static void DestroyEnt(entt::entity ent, entt::registry& reg) {
	if (true) {
		reg.emplace_or_replace<Destroyed>(ent);
	} else {
		reg.destroy(ent);
	}
}

inline void GrowSizeSystem(entt::registry& reg) {
	reg.group<Sized, const SizeChange>().each([&] (const auto ent, auto& size, const auto& sizeChange) {
//inline void GrowSizeSystem(entt::view<entt::get_t<Sized, const SizeChange>> view) {
		size.value = size.value * sizeChange.sizeMod + sizeChange.sizeGrowth;
	});
}

inline void UpdateSizeChangeSystem(entt::view<entt::get_t<const SizeModMod, SizeChange>> view) {
	view.each([&](auto ent, const auto& sizemodmod, auto& sizeChange) {
		sizeChange.sizeMod *= sizemodmod.v; 
	});
}

inline void GrowLengthSystem(entt::view<entt::get_t<Length, const LengthChange>> view) {
	view.each([&](auto ent, auto& length, const auto& lengthChange) {
		length.value += lengthChange.v;
	});
}

inline void LifetimeSystem(entt::registry& reg) {
	reg.group<Lifetime, const Decayrate>().each([&] (const auto ent, auto& l, const auto& d) {
//inline void LifetimeSystem(entt::registry& reg) {
//	reg.view<Lifetime, const Decayrate>().each([&](const auto ent, auto& l, const auto& d) {
		l.value += d.value;
		if (l.value >= 1.0)
			DestroyEnt(ent, reg);
	});
}

inline void LifetimeHeatSystem(entt::registry& reg) {
	reg.group<Heat, const HeatDecay>().each([&] (const auto ent, auto& h, const auto& r) {
		h.v -= r.v;
		if (h.v <= 0.0)
			DestroyEnt(ent, reg);
	});
}

inline void LifetimeAlphaSystem(entt::registry& reg) {
	reg.group<Alpha, const AlphaDecayrate>().each([&] (auto ent, auto& l, const auto& d) {
		l.v -= d.v;
		if (l.v <= 0.0)
			DestroyEnt(ent, reg);
	});
}

inline void LifetimeFlameSystem(entt::registry& reg) {
	reg.group<LifetimeFlame, const FlameSizeChange>().each([&](const auto ent, auto& l, const auto& change) {
		l.v++;
		if (l.v > 4+ change.v * 30)
			DestroyEnt(ent, reg);
	});
}

inline void GrowSmokeSizeSystem(entt::registry& reg) {
	reg.group<SmokeSized, const SmokeSizeChange>().each([&](auto ent, auto& smokeSize, const auto& c) {
		smokeSize.v += c.sizeGrowth;
		smokeSize.v += (c.startSize - smokeSize.v) * 0.2f * (smokeSize.v < c.startSize);
	});
}

void LifetimePositionAboveGroundSystem(entt::registry& reg);

inline void DeleteDestroyedSystem(entt::registry& reg) {
	auto d = reg.view<Destroyed>();
	reg.destroy(d.begin(), d.end());
}

//inline void PositionSystem(entt::view<entt::get_t<Position, const Speed>> view) {
inline void PositionSystem(entt::registry& reg) {
	reg.group<Position, const Speed>().each([&] (auto ent, auto& p, const auto& s) {
		p.value += s.value;
		//s.value += phys.gravity;
		//s.value *= phys.airdrag;
	});
}

inline void UpdateSimpleParticleSystem(entt::registry& reg) {
	ZoneScopedN("XYZ::UpdateSimpleParticleSystem");
	reg.view<SimpleParticle>().each([&] (auto ent, auto& p) {
		p.pos    += p.speed;
		p.speed  += p.gravity;
		p.speed  *= p.airdrag;
		p.rotVal += p.rotVel;
		p.rotVel += p.rotParams.y; //rot accel
		p.life   += p.decayrate;
		p.size    = p.size * p.sizeMod + p.sizeGrowth;
		if (p.life >= 1.0) {
			DestroyEnt(ent, reg);
		}
	});
}

inline void UpdateBitmapMuzzleFlame(entt::registry& reg) {
	reg.view<BitmapMuzzleFlame>().each([&] (auto ent, auto& p) {
		p.ttl--;
		if (p.ttl <= 0) {
			DestroyEnt(ent, reg);
		}
	});
}

void WindPositionSystem(entt::view<entt::get_t<const PositionWindChangeTag, Position, const Lifetime>> view);

/*
inline void SpeedParticlePhysSystem(entt::view<entt::get_t<Speed, const ParticlePhys>> view) {
	view.each([&](const auto ent, auto& s, const auto& phys) {
		s.value += phys.gravity;
		s.value *= phys.airdrag;
	});
}
*/
inline void SpeedParticlePhysSystem(entt::registry& reg) {
	reg.group<const ParticlePhys>(entt::get<Speed>).each([&](const auto ent, const auto& phys, auto& s) {
		s.value += phys.gravity;
		s.value *= phys.airdrag;
	});
}

extern entt::registry projectileRegistry;
inline bool UpdateEndPos(unsigned int entId, float3 p, float3 dir)
{
	auto ent = entt::entity(entId);
	auto& view = projectileRegistry;
	if (!entId) { //!view.valid(ent)) {
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
void DrawShadowSystem();
