/*
ECS checklist:

Done:
* Runtime switch for ECS MODE (you can pause the game, switch the mode to observe the difference)
* Migrated SimpleParticleProjectile to ECS
* Migrated CBitmapMuzzleFlame to ECS
* Migrated CDirtProjectile to ECS
* Created graph (organizer) for parallel execution of ECS tasks
* Parallel Projectiles::Sim() calculation
* Thread safety for both legacy and ECS projectiles in explosion generator
* Projectile drawing & sorting based on draw distance (integrated with existing synced projectiles)

To do:
* Make clear distinction what should be computed in Sim() and what in Draw() contexts.
  Most projectiles only update lifetime in Sim() except for SimpleParticleSystem and 
  unsynced projectiles that interact with environment - e.g. Dirt projectile disappears
  after hitting the ground so I guess it needs to be updated in Sim() ?
* Improve approach to Drawing. For now the drawing functions were copied over from legacy classes.
  What is the fastest way to draw visible projectiles? 
* Resolve issue with drawing being slower than legacy
* Resolve issue with Sim::update() being slower than legacy
* Create new Spawner class (see explosion generator) that doesn't require legacy Particles to exist
  (currently ECS particles copy out data from original Particles, then deallocates them)
* Add minimap and shadow drawing for ECS particles
* Add global los to ECS components

*/
#include "ECS_systems.h"

#include "Sim/Projectiles/Projectile.h"

#include "Game/Camera.h"
#include "Game/GlobalUnsynced.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/GL/RenderBuffers.h"
#include "Rendering/Textures/ColorMap.h"
#include "Rendering/Env/Particles/ProjectileDrawer.h"

#include "System/float3.h"
#include "System/Log/ILog.h"
#include "System/SpringMath.h"
#include "Rendering/Textures/TextureAtlas.h"

#include "Sim/Misc/LosHandler.h"
#include "Sim/Misc/TeamHandler.h"

#include "Game/Camera.h"
#include "Game/CameraHandler.h"

#include <functional>

#include "lib/entt/entt.hpp"
extern entt::registry registry;
namespace {
static void AddEffectsQuad(const VA_TYPE_TC& tl, const VA_TYPE_TC& tr, const VA_TYPE_TC& br, const VA_TYPE_TC& bl, const float3& animInfo)
{
	float minS = std::numeric_limits<float>::max()   ; float minT = std::numeric_limits<float>::max()   ;
	float maxS = std::numeric_limits<float>::lowest(); float maxT = std::numeric_limits<float>::lowest();
	std::invoke([&](auto&&... arg) {
		((minS = std::min(minS, arg.s)), ...);
		((minT = std::min(minT, arg.t)), ...);
		((maxS = std::max(maxS, arg.s)), ...);
		((maxT = std::max(maxT, arg.t)), ...);
	}, tl, tr, br, bl);

	auto& rb = CExpGenSpawnable::GetPrimaryRenderBuffer();

	const auto uvInfo = float4{ minS, minT, maxS - minS, maxT - minT };
	//const auto animInfo = float3{ animParams.x, animParams.y, animProgress };
	constexpr float layer = 0.0f; //for future texture arrays

	//pos, uvw, uvmm, col
	rb.AddQuadTriangles(
		{ tl.pos, float3{ tl.s, tl.t, layer }, uvInfo, animInfo, tl.c },
		{ tr.pos, float3{ tr.s, tr.t, layer }, uvInfo, animInfo, tr.c },
		{ br.pos, float3{ br.s, br.t, layer }, uvInfo, animInfo, br.c },
		{ bl.pos, float3{ bl.s, bl.t, layer }, uvInfo, animInfo, bl.c }
	);
}

static bool IsValidTexture(const AtlasedTexture* tex)
{
	return tex && tex != &CTextureAtlas::dummy;
}

template <typename ViewT>
static void DrawSimpleParticleSystem(entt::entity ent, ViewT&& view)
{
	const auto& drawPos = view.template get<const DrawPosition>(ent).value;
	const auto& speed = view.template get<const Speed>(ent).value;
	const auto& size = view.template get<const Sized>(ent).value;
	const auto& lifetime = view.template get<const Lifetime>(ent).value;
	const auto& data = view.template get<const RenderData>(ent);
	const auto& rot = view.template get<const Rotation>(ent);
	const auto& animParams = view.template get<const AnimParams>(ent);
	const auto& animProgress = view.template get<const AnimProgress>(ent);
			
	std::array<float3, 4> bounds;
	const bool shadowPass = (camera->GetCamType() == CCamera::CAMTYPE_SHADOW);
	if (data.directional && !shadowPass) {
		const float3 zdir = (drawPos - camera->GetPos()).SafeANormalize();
			  float3 ydir = zdir.cross(speed); float yDirLen2 = ydir.SqLength(); ydir.SafeANormalize();
		const float3 xdir = ydir.cross(zdir);

		const float3 interPos = drawPos;

		unsigned char color[4];
		data.colorMap->GetColor(color, lifetime);

		const float3* fwdDir = &zdir;

		if (yDirLen2 > 0.001f) {
			bounds = {
				-ydir * size - xdir * size,
				-ydir * size + xdir * size,
				 ydir * size + xdir * size,
				 ydir * size - xdir * size
			};
		} else {
			// in this case the particle's coor-system is degenerate
			const float3 cameraRight = camera->GetRight() * size;
			const float3 cameraUp    = camera->GetUp()    * size;
			fwdDir = &camera->GetForward();

			bounds = {
				-cameraRight - cameraUp,
				 cameraRight - cameraUp,
				 cameraRight + cameraUp,
				-cameraRight + cameraUp
			};
		}


		if (math::fabs(rot.rotVal) > 0.01f) {
			for (auto& b : bounds)
				b = b.rotate(rot.rotVal, *fwdDir);
		}
		float3 animInfo = { animParams.value.x, animParams.value.y, animProgress.value };
		AddEffectsQuad(
			{ interPos + bounds[0], data.texture->xstart, data.texture->ystart, color },
			{ interPos + bounds[1], data.texture->xend,   data.texture->ystart, color },
			{ interPos + bounds[2], data.texture->xend,   data.texture->yend,   color },
			{ interPos + bounds[3], data.texture->xstart, data.texture->yend,   color },
			animInfo
		);
		return;
	}

	unsigned char color[4];
	data.colorMap->GetColor(color, lifetime);

	const float3 interPos = drawPos;
	const float3 cameraRight = camera->GetRight() * size;
	const float3 cameraUp    = camera->GetUp()    * size;

	bounds = {
		-cameraRight - cameraUp,
		 cameraRight - cameraUp,
		 cameraRight + cameraUp,
		-cameraRight + cameraUp
	};

	if (math::fabs(rot.rotVal) > 0.01f) {
		for (auto& b : bounds)
			b = b.rotate(rot.rotVal, camera->GetForward());
	}
	float3 animInfo = { animParams.value.x, animParams.value.y, animProgress.value };
	AddEffectsQuad(
		{ interPos + bounds[0], data.texture->xstart, data.texture->ystart, color },
		{ interPos + bounds[1], data.texture->xend,   data.texture->ystart, color },
		{ interPos + bounds[2], data.texture->xend,   data.texture->yend,   color },
		{ interPos + bounds[3], data.texture->xstart, data.texture->yend,   color },
		animInfo
				
	);
}

void UpdateDrawPosSpeedSystem(entt::view<entt::get_t<const Position, const Speed, DrawPosition>> view)
{
	const float t = registry.ctx().get<PhysDelta>().timeOffset;
	view.each([&](
		auto ent, const Position& pos, const Speed& speed, DrawPosition& drawPos) {
		drawPos.value = (speed.value.w != 0.0f) ? (pos.value + speed.value * t) : pos.value;
	});
}

void UpdateDrawPosSystem(entt::view<entt::get_t<const Position, DrawPosition>, entt::exclude_t<Speed>> view)
{
	view.each([&](
		auto ent, const Position& pos, DrawPosition& drawPos) {
		drawPos.value = pos.value;
	});
}

void UpdateDrawOrder(entt::view<entt::get_t<const DrawPosition, DrawOrder>> view)
{
	const CCamera* cam = CCameraHandler::GetActiveCamera();
	view.each([&](
		auto ent, const DrawPosition& drawPos, DrawOrder& drawOrder) {
		drawOrder.distanceFromCamera = -cam->ProjectedDistance(drawPos.value);
	});
}

void UpdateAnimProgressSystem(entt::view<entt::get_t<AnimProgress, const AnimParams>> view)
{
	view.each([&](auto ent, auto& animProgress, const auto& animParams) {
		const float t = (registry.ctx().get<PhysDelta>().frameNum - animParams.createFrame +
						 registry.ctx().get<PhysDelta>().timeOffset);
		if (static_cast<int>(animParams.value.x) <= 1 && static_cast<int>(animParams.value.y) <= 1) {
			animProgress.value = 0.0f;
			return;
		}
		
		const float animSpeed = math::fabs(animParams.value.z);
		if (animParams.value.z < 0.0f) {
			animProgress.value = 1.0f - math::fabs(math::fmod(t, 2.0f * animSpeed) / animSpeed - 1.0f);
		}
		else {
			animProgress.value = math::fmod(t, animSpeed) / animSpeed;
		}
	});
}

} // unnamed namespace

void LifetimePositionAboveGroundSystem(entt::registry& reg) {
	reg.view<const Position, GroundCollisionTag>().each([&](const auto ent, auto& pos) {
		if(CGround::GetApproximateHeight(pos.value.x, pos.value.z, false) - 40.0f > pos.value.y) {
			registry.emplace_or_replace<Destroyed>(ent);
		}
	});
}

void RotationSystem(entt::view<entt::get_t<Rotation, const RotParams, const AnimParams>> view) {
	// TODO execute in Sim() or Draw()?
	view.each([&](const auto ent, auto& rot, const auto& rotParams, auto& animParams) {
		const float t = (registry.ctx().get<PhysDelta>().frameNum - animParams.createFrame + registry.ctx().get<PhysDelta>().timeOffset);
		// rotParams.y is acceleration in angle per frame^2
		rot.rotVel = rotParams.value.x + rotParams.value.y * t;
		rot.rotVal = rotParams.value.z + rot.rotVel      * t;
	});
}

static bool CanDrawProjectile(const Position& pos, const AlliedTeam& allyTeam)
{
	auto& th = teamHandler;
	auto& lh = losHandler;
	return (gu->spectatingFullView || (th.IsValidAllyTeam(allyTeam.value) && th.Ally(allyTeam.value, gu->myAllyTeam)) || lh->InLos(pos.value, gu->myAllyTeam)); // FIXME it uses wrong InLos() override
}

static bool isParticleVisible(const Position& pos, const DrawPosition& drawPos,
							  const DrawRadius& drawRadius, const AlliedTeam& allyteam)
{
	if (!CanDrawProjectile(pos, allyteam))
		return false;

	bool drawRefraction = false; // TODO
	if (drawRefraction && (drawPos.value.y > drawRadius.value) /*!pro->IsInWater()*/)
		return false;
	// removed this to fix AMD particle drawing
	//if (drawReflection && !CModelDrawerHelper::ObjectVisibleReflection(pro->drawPos, camera->GetPos(), pro->GetDrawRadius()))
	//	return;

	const CCamera* cam = CCameraHandler::GetActiveCamera();
	if (!cam->InView(drawPos.value, drawRadius.value))
		return false;

	return true;
}

static void DrawClass(SimpleParticleSystemTag)
{
	auto view = registry.view<SimpleParticleSystemTag, const Position, const DrawPosition,
			const DrawRadius, const AlliedTeam, const Speed, const Sized, const Lifetime,
			const RenderData, const Rotation, const AnimParams, const AnimProgress>();
	for (auto ent : view) {
		 const auto& pos = view.get<const Position>(ent);
		 const auto& drawPos = view.get<const DrawPosition>(ent);
		 const auto& drawRadius = view.get<const DrawRadius>(ent);
		 const auto& allyteam = view.get<const AlliedTeam>(ent);
		if (!isParticleVisible(pos, drawPos, drawRadius, allyteam)) 
			continue;
		DrawSimpleParticleSystem(ent, view);
	}
}

template <typename ViewT>
static void DrawCBitmapMuzzleFlame(entt::entity ent, ViewT&& view)
{
	auto life = view.template get<const Lifetime>(ent).value;
	life += view.template get<const Decayrate>(ent).value * globalRendering->timeOffset;
	const auto& sizeGrowth = view.template get<const LifetimeSizeChange>(ent).sizeGrowth;
	const auto& size = view.template get<const Sized>(ent).value;
	const auto& length = view.template get<const Length>(ent).value;
	const float igrowth = sizeGrowth * (1.0f - Square(1.0f - life));
	
	const float isize = size * (igrowth + 1.0f);
	const float ilength = length * (igrowth + 1.0f);
	
	auto& radius = view.template get<DrawRadius>(ent).value;
	radius = std::max(isize, ilength);
	
	auto& colorMap = view.template get<const RenderData>(ent).colorMap;
	
	unsigned char col[4];
	colorMap->GetColor(col, life);
	
	auto& pos = view.template get<const Position>(ent).value;
	auto& frontOffset = registry.get<const FrontOffset>(ent).value;	
	auto dir = view.template get<const Direction>(ent).value;
	float3 fpos = pos + dir * frontOffset * ilength;
	
	const float3 zdir = (std::fabs(dir.dot(UpVector)) >= 0.99f)? FwdVector: UpVector;
	const float3 xdir = (dir.cross(zdir)).SafeANormalize();
	const float3 ydir = (dir.cross(xdir)).SafeANormalize();
	
	std::array<float3, 12> bounds = {
		  ydir * isize                ,
		  ydir * isize + dir * ilength,
		 -ydir * isize + dir * ilength,
		 -ydir * isize                ,
	
		  xdir * isize                ,
		  xdir * isize + dir * ilength,
		 -xdir * isize + dir * ilength,
		 -xdir * isize                ,
	
		 -xdir * isize + ydir * isize,
		  xdir * isize + ydir * isize,
		  xdir * isize - ydir * isize,
		 -xdir * isize - ydir * isize
	};
	
	auto& rotVal = view.template get<const Rotation>(ent).rotVal;
	if (math::fabs(rotVal) > 0.01f) {
		for (auto& b : bounds)
			b = b.rotate(rotVal, dir);
	}
	
	auto& animParams = view.template get<const AnimParams>(ent).value;
	auto& animProgress = view.template get<const AnimProgress>(ent).value;
	float3 animInfo = { animParams.x, animParams.y, animProgress };
	
	auto& sideTexture = view.template get<const RenderData>(ent).extraTexture;
	if (IsValidTexture(sideTexture)) {
		AddEffectsQuad(
			{ pos + bounds[0], sideTexture->xstart, sideTexture->ystart, col },
			{ pos + bounds[1], sideTexture->xend  , sideTexture->ystart, col },
			{ pos + bounds[2], sideTexture->xend  , sideTexture->yend  , col },
			{ pos + bounds[3], sideTexture->xstart, sideTexture->yend  , col },
			animInfo
		);
		AddEffectsQuad(
			{ pos + bounds[4], sideTexture->xstart, sideTexture->ystart, col },
			{ pos + bounds[5], sideTexture->xend  , sideTexture->ystart, col },
			{ pos + bounds[6], sideTexture->xend  , sideTexture->yend  , col },
			{ pos + bounds[7], sideTexture->xstart, sideTexture->yend  , col },
			animInfo
		);
	}

	auto& frontTexture = view.template get<const RenderData>(ent).texture;
	if (IsValidTexture(frontTexture)) {
		AddEffectsQuad(
			{ fpos + bounds[8 ], frontTexture->xstart, frontTexture->ystart, col },
			{ fpos + bounds[9 ], frontTexture->xend  , frontTexture->ystart, col },
			{ fpos + bounds[10], frontTexture->xend  , frontTexture->yend , col },
			{ fpos + bounds[11], frontTexture->xstart, frontTexture->yend , col },
			animInfo
		);
	}
}

static void DrawClass(CBitmapMuzzleFlameTag)
{
	auto view = registry.view<CBitmapMuzzleFlameTag, const Position, const DrawPosition,
			DrawRadius, const AlliedTeam, const Decayrate, const Lifetime, const LifetimeSizeChange,
			const Sized, const Length, const RenderData, const FrontOffset, const Direction,
			const Rotation, const AnimParams, const AnimProgress
			>();
	for (auto ent : view) {
		auto& pos = view.get<Position>(ent);
		auto& drawPos = view.get<DrawPosition>(ent);
		auto& drawRadius = view.get<DrawRadius>(ent);
		auto& allyteam = view.get<AlliedTeam>(ent);
		if (!isParticleVisible(pos, drawPos, drawRadius, allyteam))
			continue;

		DrawCBitmapMuzzleFlame(ent, view);
	};
}

template <typename ViewT>
static void DrawCDirtProjectile(entt::entity ent, ViewT&& view) 
{
	auto& pos = view.template get<const Position>(ent).value;
	auto& size = view.template get<const Sized>(ent).value;
	auto& sizeExpansion = view.template get<const SizeChange>(ent).sizeGrowth;
	auto& color = view.template get<const Color>(ent).v;
	auto& alpha = view.template get<const Alpha>(ent).v;
	auto& texture = view.template get<const RenderData>(ent).texture;
	auto& drawPos = view.template get<const DrawPosition>(ent).value;
	auto& animParams = view.template get<const AnimParams>(ent);
	auto& animProgress = view.template get<const AnimProgress>(ent);
			
	float3 animInfo = { animParams.value.x, animParams.value.y, animProgress.value };
	float partAbove = (pos.y / (size * camera->GetUp().y));

	if (partAbove < -1.0f)
		return;

	partAbove = std::min(partAbove, 1.0f);

	unsigned char col[4];
	col[0] = (unsigned char) (color.x * alpha);
	col[1] = (unsigned char) (color.y * alpha);
	col[2] = (unsigned char) (color.z * alpha);
	col[3] = (unsigned char) (alpha)/*- (globalRendering->timeOffset * alphaFalloff)*/;

	const float interSize = size + globalRendering->timeOffset * sizeExpansion;
	const float texx = texture->xstart + (texture->xend - texture->xstart) * ((1.0f - partAbove) * 0.5f);

	AddEffectsQuad(
		{ drawPos - camera->GetRight() * interSize - camera->GetUp() * interSize * partAbove, texx,          texture->ystart, col },
		{ drawPos - camera->GetRight() * interSize + camera->GetUp() * interSize,             texture->xend, texture->ystart, col },
		{ drawPos + camera->GetRight() * interSize + camera->GetUp() * interSize,             texture->xend, texture->yend,   col },
		{ drawPos + camera->GetRight() * interSize - camera->GetUp() * interSize * partAbove, texx,          texture->yend,   col },
		animInfo
	);
}

void DrawClass(CDirtProjectileTag)
{
	auto view = registry.view<CDirtProjectileTag, const Position, const DrawPosition, const DrawRadius, const AlliedTeam,
			const SizeChange, const Sized, const RenderData, const Color, const Alpha,
			const AnimParams, const AnimProgress
			>();
	for (auto ent : view) {
		auto& pos = view.get<Position>(ent);
		auto& drawPos = view.get<DrawPosition>(ent);
		auto& drawRadius = view.get<DrawRadius>(ent);
		auto& allyteam = view.get<AlliedTeam>(ent);
		if (!isParticleVisible(pos, drawPos, drawRadius, allyteam))
			continue;
		DrawCDirtProjectile(ent, view);
	};
}
template <typename ViewT>
static void DrawCExploSpikeProjectile(entt::entity ent, ViewT&& view) 
{
	auto& pos = view.template get<const Position>(ent).value;
	auto& dir = view.template get<const Direction>(ent).value;
	auto& alpha = view.template get<const Alpha>(ent).v;
	auto& alphaDecay = view.template get<const AlphaDecayrate>(ent).v;
	auto& color = view.template get<const Color>(ent).v;
	auto& length = view.template get<const Length>(ent).value;
	auto& lengthGrowth = view.template get<const LengthChange>(ent).v;
	auto& width = view.template get<const Width>(ent).value;
	auto& drawPos = view.template get<const DrawPosition>(ent).value;
	//auto& texture = view.template get<const RenderData>(ent).texture;
	auto& animParams = view.template get<const AnimParams>(ent);
	auto& animProgress = view.template get<const AnimProgress>(ent);
	float3 animInfo = { animParams.value.x, animParams.value.y, animProgress.value };
	
	const float3 dif = (pos - camera->GetPos()).ANormalize();
	const float3 dir2 = (dif.cross(dir)).ANormalize();

	unsigned char col[4];
	const float a = std::max(0.0f, alpha - alphaDecay * globalRendering->timeOffset) * 255.0f;
	col[0] = (unsigned char)(a * color.x);
	col[1] = (unsigned char)(a * color.y);
	col[2] = (unsigned char)(a * color.z);
	col[3] = 1;

	const float3 l = (dir * length) + (lengthGrowth * globalRendering->timeOffset);
	const float3 w = dir2 * width;

	#define let projectileDrawer->laserendtex
	AddEffectsQuad(
		{ drawPos - l - w, let->xstart, let->ystart, col },
		{ drawPos + l - w, let->xend,   let->ystart, col },
		{ drawPos + l + w, let->xend,   let->yend,   col },
		{ drawPos - l + w, let->xstart, let->yend,   col },
		animInfo
	);
	#undef let
}

template <typename ViewT>
static void DrawCHeatCloudProjectile(entt::entity ent, ViewT&& view) 
{
	auto& pos = view.template get<const Position>(ent).value;
	auto& heat = view.template get<const Heat>(ent).v;
	auto& maxheat = view.template get<const MaxHeat>(ent).v;

	auto& drawPos = view.template get<const DrawPosition>(ent).value;
	auto& animParams = view.template get<const AnimParams>(ent);
	auto& animProgress = view.template get<const AnimProgress>(ent);
	float3 animInfo = { animParams.value.x, animParams.value.y, animProgress.value };

	auto& size = view.template get<const Sized>(ent).value;
	auto& sizemod = view.template get<const SizeChange>(ent).sizeMod;
	auto& sizeGrowth = view.template get<const SizeChange>(ent).sizeGrowth;

	const auto& rot = view.template get<const Rotation>(ent);

	auto texture = projectileDrawer->heatcloudtex;

	unsigned char col[4];
	const float dheat = std::max(0.0f, heat-globalRendering->timeOffset);
	const float alpha = (dheat / maxheat) * 255.0f;

	col[0] = (unsigned char) alpha;
	col[1] = (unsigned char) alpha;
	col[2] = (unsigned char) alpha;
	col[3] = 1;//(dheat/maxheat)*255.0f;

	const float drawsize = (size + sizeGrowth * globalRendering->timeOffset) * (1.0f - sizemod);

	const float3 ri = camera->GetRight();
	const float3 up = camera->GetUp();

	std::array<float3, 4> bounds = {
		-ri * drawsize - up * drawsize,
		ri * drawsize - up * drawsize,
		ri * drawsize + up * drawsize,
		-ri * drawsize + up * drawsize
	};

	if (math::fabs(rot.rotVal) > 0.01f) {
		for (auto& b : bounds)
			b = b.rotate(rot.rotVal, camera->GetForward());
	}
	AddEffectsQuad(
			{ drawPos + bounds[0], texture->xstart, texture->ystart, col },
			{ drawPos + bounds[1], texture->xend,   texture->ystart, col },
			{ drawPos + bounds[2], texture->xend,   texture->yend,   col },
			{ drawPos + bounds[3], texture->xstart, texture->yend,   col },
			animInfo
			);
};

template <typename ViewT>
static void DrawCMuzzleFlame(entt::entity ent, ViewT&& view) 
{
	auto& pos = view.template get<const Position>(ent).value;
	auto& age = view.template get<const LifetimeFlame>(ent).v;
	
	//auto& drawPos = view.template get<const DrawPosition>(ent).value;
	auto& animParams = view.template get<const AnimParams>(ent);
	auto& animProgress = view.template get<const AnimProgress>(ent);
	float3 animInfo = { animParams.value.x, animParams.value.y, animProgress.value };

	auto& size = view.template get<const Sized>(ent).value;
	auto& dir = view.template get<const Direction>(ent).value;
	
	auto& a = view.template get<const ParticleIndex>(ent).v;
	
	unsigned char col[4];
	float alpha = std::max(0.0f, 1 - (age / (4 + size * 30)));
	float modAge = fastmath::apxsqrt(static_cast<float>(age + 2));

	const int tex = a % projectileDrawer->NumSmokeTextures();
	// float xmod = 0.125f + (float(int(tex % 6))) / 16.0f;
	// float ymod =                (int(tex / 6))  / 16.0f;

	float drawsize = modAge * 3;
	float3 interPos(pos+dir*(a+2)*modAge*0.4f);
	float fade = std::max(0.0f, std::min(1.0f, (1 - alpha) * (20 + a) * 0.1f));

	col[0] = (unsigned char) (180 * alpha * fade);
	col[1] = (unsigned char) (180 * alpha * fade);
	col[2] = (unsigned char) (180 * alpha * fade);
	col[3] = (unsigned char) (255 * alpha * fade);

	#define st projectileDrawer->GetSmokeTexture(tex)
	AddEffectsQuad(
		{ interPos - camera->GetRight() * drawsize - camera->GetUp() * drawsize, st->xstart, st->ystart, col },
		{ interPos + camera->GetRight() * drawsize - camera->GetUp() * drawsize, st->xend,   st->ystart, col },
		{ interPos + camera->GetRight() * drawsize + camera->GetUp() * drawsize, st->xend,   st->yend,   col },
		{ interPos - camera->GetRight() * drawsize + camera->GetUp() * drawsize, st->xstart, st->yend,   col },
		animInfo
	);
	#undef st

	if (fade < 1.0f) {
		float ifade = 1.0f - fade;
		col[0] = (unsigned char) (ifade * 255);
		col[1] = (unsigned char) (ifade * 255);
		col[2] = (unsigned char) (ifade * 255);
		col[3] = (unsigned char) (1);

		#define mft projectileDrawer->muzzleflametex
		AddEffectsQuad(
			{ interPos - camera->GetRight() * drawsize - camera->GetUp() * drawsize, mft->xstart, mft->ystart, col },
			{ interPos + camera->GetRight() * drawsize - camera->GetUp() * drawsize, mft->xend,   mft->ystart, col },
			{ interPos + camera->GetRight() * drawsize + camera->GetUp() * drawsize, mft->xend,   mft->yend,   col },
			{ interPos - camera->GetRight() * drawsize + camera->GetUp() * drawsize, mft->xstart, mft->yend,   col },
			animInfo
		);
		#undef mft
	}
}


void PreDrawSystem() {
	UpdateAnimProgressSystem(registry.view<AnimProgress, const AnimParams>());
	UpdateDrawPosSystem(registry.view<const Position, DrawPosition>(entt::exclude<Speed>));
	UpdateDrawPosSpeedSystem(registry.view<const Position, const Speed, DrawPosition>());	
	UpdateDrawOrder(registry.view<const DrawPosition, DrawOrder>());
	RotationSystem(registry.view<Rotation, const RotParams, const AnimParams>());
}

// Draws new ECS projectiles and legacy sortedProjectiles
// this is a temporary compatible solution that respects drawing order and
// prevents any graphical artifacts when drawing mixed ECS and legacy OOP projectiles
// this approach uses runtime look up of the components type
void DrawSystem(const std::vector<std::pair<std::pair<float, float>, CProjectile*>>& sortedProj)
{ // TODO figure out fastest way for dispatching. maybe add entt::observer and check proj visibility first?	
	registry.sort<DrawOrder>([](const auto &lhs, const auto &rhs) {
		return std::pair(lhs.drawOrder, lhs.distanceFromCamera) < std::pair(rhs.drawOrder, rhs.distanceFromCamera);
	}); 

	auto projIt = sortedProj.begin();
	registry.view<const DrawOrder>().each([&](auto ent, const auto& drawOrder) {
		const auto dist = std::pair{drawOrder.drawOrder, drawOrder.distanceFromCamera};
		while (projIt != sortedProj.end() && projIt->first < dist) {
			projIt->second->Draw();
			++projIt;
		}

		auto& pos = registry.get<Position>(ent);
		auto& drawPos = registry.get<DrawPosition>(ent);
		auto& drawRadius = registry.get<DrawRadius>(ent);
		auto& allyteam = registry.get<AlliedTeam>(ent);
		if (!isParticleVisible(pos, drawPos, drawRadius, allyteam))
			return;

		// TODO this dispatch uses registry instead view so it's slower TODO benchmark
		// maybe do Draw() calculation in thread pools to thread local vector, then sort and then pass to render buffer?
		if (registry.all_of<SimpleParticleSystemTag>(ent)) {
			DrawSimpleParticleSystem(ent, registry);
		} else if (registry.all_of<CBitmapMuzzleFlameTag>(ent)) {
			DrawCBitmapMuzzleFlame(ent, registry);
		} else if (registry.all_of<CDirtProjectileTag>(ent)) {
			DrawCDirtProjectile(ent, registry);
		} else if (registry.all_of<CExploSpikeProjectileTag>(ent)) {
			DrawCExploSpikeProjectile(ent, registry);
		} else if (registry.all_of<CHeatCloudProjectileTag>(ent)) {
			DrawCHeatCloudProjectile(ent, registry);
		} else if (registry.all_of<CMuzzleFlameTag>(ent)) {
			DrawCMuzzleFlame(ent, registry);
		}
	});

	while (projIt != sortedProj.end()) {
		projIt->second->Draw();
		++projIt;
	}
}
