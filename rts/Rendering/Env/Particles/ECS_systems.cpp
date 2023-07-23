/*
ECS checklist:

Done:
* Migrated SimpleParticleProjectile to ECS
* Migrated CBitmapMuzzleFlame to ECS
* Drawing
* Parallel Projectiles::Sim() calculation
* Thread safety for both legacy and ECS projectiles in explosion generator

To do:
* Make clear distinction what should be computed in Sim() and what in Draw() contexts.
  Most projectiles only update lifetime in Sim() except for SimpleParticleSystem and 
  unsynced projectiles that interact with environment - e.g. Dirt projectile disappears
  after hitting the ground so I guess it needs to be updated in Sim() ?
* Improve approach to Drawing. For now the drawing functions were copied over from legacy classes.
  What is the fastest way to draw visible projectiles? 
* Add ordered drawing to ECS particles
* Create graph for parallel execution of ECS tasks
* Resolve issue with drawing being slower than legacy
* Resolve issue with Sim::update() being slower than legacy
* Create new Spawner class (see explosion generator) that doesn't require legacy Particles to exist
  (currently ECS particles copy out data from original Particles, then deallocates them)
* Add minimap and shadow drawing for ECS particles

*/
#include "ECS_systems.h"

#include "Game/Camera.h"
#include "Game/GlobalUnsynced.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/GL/RenderBuffers.h"
#include "Rendering/Textures/ColorMap.h"
#include "Sim/Projectiles/ExpGenSpawnableMemberInfo.h"
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

static TypedRenderBuffer<VA_TYPE_PROJ>& GetPrimaryRenderBuffer()
{
	return RenderBuffer::GetTypedRenderBuffer<VA_TYPE_PROJ>();
}


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

	auto& rb = GetPrimaryRenderBuffer();

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

static void DrawSimpleParticleSystem(const Position& pos, const Speed& speed, const Sized& sized,
				  const Lifetime& l, const RenderData& data, const Rotation& rot,
				  const AnimParams& animParams, const AnimProgress& animProgress)
{
	std::array<float3, 4> bounds;
	const bool shadowPass = (camera->GetCamType() == CCamera::CAMTYPE_SHADOW);
	if (data.directional && !shadowPass) {

		if (l.value >= 1.0f)
			return;

		const float3 zdir = (pos.value - camera->GetPos()).SafeANormalize();
			  float3 ydir = zdir.cross(speed.value); float yDirLen2 = ydir.SqLength(); ydir.SafeANormalize();
		const float3 xdir = ydir.cross(zdir);

		const float3 interPos = pos.value + speed.value * globalRendering->timeOffset;
		const float size = sized.value;

		unsigned char color[4];
		data.colorMap->GetColor(color, l.value);

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
			const float3 cameraRight = camera->GetRight() * sized.value;
			const float3 cameraUp    = camera->GetUp()    * sized.value;
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

	// !directional
	if (l.value >= 1.0f)
		return;

	unsigned char color[4];
	data.colorMap->GetColor(color, l.value);

	const float3 interPos = pos.value + speed.value * globalRendering->timeOffset;
	const float3 cameraRight = camera->GetRight() * sized.value;
	const float3 cameraUp    = camera->GetUp()    * sized.value;

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

void UpdateDrawPosSystem()
{
	const float t = registry.ctx().get<PhysDelta>().timeOffset;
	registry.view<const Position, const Speed, DrawPosition>().each([&](
		auto ent, const Position& pos, const Speed& speed, DrawPosition& drawPos) {
		drawPos.value = (speed.value.w != 0.0f) ? (pos.value + speed.value * t) : pos.value;
	});
	registry.view<const Position, DrawPosition>(entt::exclude<Speed>).each([&](
		auto ent, const Position& pos, DrawPosition& drawPos) {
		drawPos.value = pos.value;
	});
}

void UpdateAnimProgressSystem()
{	
	registry.view<AnimProgress, AnimParams>().each([&](auto ent, auto& animProgress, auto& animParams) {
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
	registry.view<SimpleParticleSystemTag, Position, DrawPosition,
			DrawRadius, AlliedTeam, Speed, Sized, Lifetime, RenderData, Rotation,
			AnimParams, AnimProgress>().each([&](
				auto ent, const Position& pos, const DrawPosition& drawPos,
				const DrawRadius& drawRadius, const AlliedTeam& allyteam, const Speed& speed,
				const Sized& sized, const Lifetime& lifetime, const RenderData& renderData,
				const Rotation& rot, const AnimParams& animParams, const AnimProgress& animProgress) {	
		if (!isParticleVisible(pos, drawPos, drawRadius, allyteam)) 
			return;
		DrawSimpleParticleSystem(pos, speed, sized,
								lifetime, renderData, rot,
								animParams, animProgress);
	});
}

template <typename ViewT>
static void DrawCBitmapMuzzleFlame(entt::entity ent, ViewT&& view)
{
	auto& life = view.template get<const Lifetime>(ent).value;
	auto& sizeGrowth = view.template get<const SizeChange>(ent).sizeGrowth;
	auto& size = view.template get<const Sized>(ent).value;
	auto& length = view.template get<const Length>(ent).value;
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
	auto view = registry.view<CBitmapMuzzleFlameTag, const Position, const DrawPosition, DrawRadius, const AlliedTeam,
			const Lifetime, const SizeChange, const Sized, const Length, const RenderData,
			const FrontOffset, const Direction, const Rotation, const AnimParams, 
			const AnimProgress
			>();
	for (auto ent : view) {
		auto& pos = view.get<Position>(ent);
		auto& drawPos = view.get<DrawPosition>(ent);
		auto& drawRadius = view.get<DrawRadius>(ent);
		auto& allyteam = view.get<AlliedTeam>(ent);
		if (!isParticleVisible(pos, drawPos, drawRadius, allyteam)) {
			continue;
		}
		DrawCBitmapMuzzleFlame(ent, view);
	};
}

void DrawSystem()
{
	// FIXME performance issues. maybe add entt::observer and check visibility first?
	DrawClass(SimpleParticleSystemTag{});
	DrawClass(CBitmapMuzzleFlameTag{});
};
