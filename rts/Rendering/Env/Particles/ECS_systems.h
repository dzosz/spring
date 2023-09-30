#pragma once

#include "ECS_components.h"
#include "lib/entt/src/entt/entity/view.hpp"
#include "lib/entt/src/entt/fwd.hpp"
#include "lib/entt/src/entt/entity/registry.hpp"
#include "System/SpringMath.h"
#include "tracy/Tracy.hpp"

static void DestroyEnt(entt::entity ent, entt::registry& reg) {
	if (false) {
		reg.emplace_or_replace<Destroyed>(ent);
	} else {
		reg.destroy(ent);
	}
}

inline void DeleteDestroyedSystem(entt::registry& reg) {
	auto d = reg.view<Destroyed>();
	reg.destroy(d.begin(), d.end());
}

inline void UpdateSimpleParticleSystem(entt::registry& reg) {
	ZoneScopedN("XYZ::UpdateSimpleParticleSystem");
	reg.view<SimpleParticle>().each([&] (auto ent, SimpleParticle& p) {
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
	ZoneScopedN("XYZ::UpdateBitmapMuzzleFlame");
	reg.view<BitmapMuzzleFlame>().each([&] (auto ent, BitmapMuzzleFlame& p) {
		p.ttl--;
		if (p.ttl < 0) {
			DestroyEnt(ent, reg);
		}
	});
}

inline void UpdateHeatCloudProjectile(entt::registry& reg) {
	ZoneScopedN("XYZ::UpdateHeatCloudProjectile");
	reg.view<HeatCloudProjectile>().each([&] (auto ent, HeatCloudProjectile& e) {
		e.pos += e.speed;
		e.heat = std::max(e.heat - e.heatFalloff, 0.0f);
		
		e.size += e.sizeGrowth;
		e.sizemod *= e.sizemodmod;
		
		if (e.heat <= 0.0) {
			DestroyEnt(ent, reg);
		}
	});
}

inline void UpdateCMuzzleFlame(entt::registry& reg) {
	ZoneScopedN("XYZ::UpdateCMuzzleFlame");
	reg.view<MuzzleFlame>().each([&] (auto ent, MuzzleFlame& e) {
		e.age++;
		e.pos += e.speed;
		if (e.age > (4 + e.size * 30)) {
			DestroyEnt(ent, reg);
		}
	});
}

inline void UpdateExploSpikeProjectile(entt::registry& reg) {
	ZoneScopedN("XYZ::UpdateExploSpikeProjectile");
	reg.view<ExploSpikeProjectile>().each([&] (auto ent, ExploSpikeProjectile& e) {
		e.pos += e.speed;
		e.length += e.lengthGrowth;
		e.alpha = std::max(0.0f, e.alpha - e.alphaDecay);
		
		if (e.alpha <= 0.0f) {
			DestroyEnt(ent, reg);
		}
	});
}

inline void UpdateBubbleProjectile(entt::registry& reg) {
	ZoneScopedN("XYZ::UpdateBubbleProjectile");
	reg.view<BubbleProjectile>().each([&] (auto ent, BubbleProjectile& p) {
		p.pos += p.speed;
		--p.ttl;
		p.size += p.sizeExpansion;
		
		if (p.size < p.startSize) {
			p.size += (p.startSize - p.size) * 0.2f;
		}
		p.drawRadius = p.size;
		
		if (p.pos.y > (-p.size * 0.7f)) {
			p.pos.y = -p.size * 0.7f;
			p.alpha -= 0.03f;
		}
		
		if (p.ttl < 0) {
			p.alpha -= 0.03f;
		}
		if (p.alpha < 0) {
			DestroyEnt(ent, reg);
		}
	});
}

void UpdateSmokeProjectile(entt::registry& reg);
void UpdateDirtProjectile(entt::registry& reg);
void UpdateSmokeTrailProjectile(entt::registry& reg);

extern entt::registry projectileRegistry;
inline bool UpdateEndPos(unsigned int entId, float3 p, float3 dir)
{
	auto ent = entt::entity(entId);
	auto& view = projectileRegistry;
	if (!entId) { //!view.valid(ent)) {
		return false;
	}
	
	auto& d = view.template get<SmokeTrail>(ent);
	
	d.pos1 = p;
	d.dir1 = dir;

	const float dist = d.pos1.distance(d.pos2);

	d.drawSegmented = false;
	d.pos = (d.pos1 + d.pos2) * 0.5f;
	
	d.drawRadius = dist;
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

void PreDrawSystem();
void DrawSystem();
void DrawMinimapSystem();
void DrawShadowSystem();
