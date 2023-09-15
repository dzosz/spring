/*
ECS checklist:

Done:
* Runtime switch for ECS MODE (you can pause the game, switch the mode to observe the difference)
* Migrated types projectiles to ECS:
  SimpleParticleSystem CBitmapMuzzleFlame CDirtProjectile CExploSpikeProjectileTag
  CBitmapMuzzleFlameTag CDirtProjectileTag CHeatCloudProjectileTag CMuzzleFlameTag
  CSmokeProjectileTag CSmokeTrailProjectileTag
* Thread safety for both legacy and ECS projectiles in explosion generator
* Projectile drawing & sorting based on draw distance (integrated with existing synced projectiles)
* Shadow drawing
* Performance: ECS Sim() is up to 5% faster, ECS Draw() 30% SLOWER in corshiva corshiva 200 (80k MaxPraticle limit)

To do:
* Make clear distinction what should be computed in Sim() and what in Draw() contexts.
  Most projectiles do very little in Sim() (only update lifetime) except for SimpleParticleSystem and 
  unsynced projectiles that interact with environment - e.g. Dirt projectile disappears
  after hitting the ground.
* Improve approach to Drawing. For now the drawing functions were copied over from legacy classes.
  What is the fastest way to draw projectiles made of many components?
* Create new Spawner class (see explosion generator) that doesn't require legacy Particles to exist
  (currently ECS particles copy out data from original Particles, then deallocates them)
* Add minimap drawing for ECS particles
* Parallel executor on task graph

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
#include "Sim/Misc/Wind.h"

#include "Game/Camera.h"
#include "Game/CameraHandler.h"

#include <functional>

#include "lib/entt/src/entt/entt.hpp"

extern entt::registry projectileRegistry;

extern std::vector<uint64_t> enqueuedProjectilesDrawOrderData;

namespace {
static void AddEffectsQuad(int drawOrder, float sortDist, const VA_TYPE_TC& tl, const VA_TYPE_TC& tr, const VA_TYPE_TC& br, const VA_TYPE_TC& bl, const float3& animInfo)
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
	
	uint64_t order (static_cast<uint32_t>(drawOrder) << 31 | static_cast<uint32_t>(-sortDist));
	enqueuedProjectilesDrawOrderData.push_back(order);
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
	/*
	const auto& data = view.template get<const RenderData>(ent);	
	const auto& drad = view.template get<const DrawRadius>(ent).value;
	const auto& drawOrder = view.template get<const DrawOrder>(ent).drawOrder;
	*/

	const auto& a = view.template get<const AnimParams2>(ent);
	const auto& d = view.template get<const SimpleParticle>(ent);

		const auto& data = d.r;
		const auto& drawOrder = d.drawo;
		const auto& drad = d.drawRadius;
		

	if (!isParticleVisible(d.pos, d.pos, drad, d.allyteam, true)) {
		return;
	}
	
	unsigned char color[4];
	data.colorMap->GetColor(color, d.life);
	
	const float3 interPos = d.pos;
	float3 animInfo = { a.params.x, a.params.y, a.progress };
			
	std::array<float3, 4> bounds;
	
	const bool shadowPass = (camera->GetCamType() == CCamera::CAMTYPE_SHADOW);
	
	bool simple=true;
	
	if (data.directional && !shadowPass) {
		const float3 zdir = (d.pos- camera->GetPos()).SafeANormalize();
		float3 ydir = zdir.cross(d.speed);
		const float yDirLen2 = ydir.SqLength();
		ydir.SafeANormalize();
		const float3 xdir = ydir.cross(zdir);

		if (yDirLen2 > 0.001f) {
			bounds = {
				-ydir * d.size - xdir * d.size,
				-ydir * d.size + xdir * d.size,
				 ydir * d.size + xdir * d.size,
				 ydir * d.size - xdir * d.size
			};
			simple = false;
		}
		
	}	
	if (simple)
	{	
		const float3 cameraRight = camera->GetRight() * d.size;
		const float3 cameraUp    = camera->GetUp()    * d.size;
	
		bounds = {
			-cameraRight - cameraUp,
			 cameraRight - cameraUp,
			 cameraRight + cameraUp,
			-cameraRight + cameraUp
		};
	}

	if (std::fabs(d.rotVal) > 0.01f) {
		for (auto& b : bounds)
			b = b.rotate<false>(d.rotVal, camera->GetForward());
	}

	AddEffectsQuad(//drawOrder, camera->ProjectedDistance(d.pos),
		{ interPos + bounds[0], data.texture->xstart, data.texture->ystart, color },
		{ interPos + bounds[1], data.texture->xend,   data.texture->ystart, color },
		{ interPos + bounds[2], data.texture->xend,   data.texture->yend,   color },
		{ interPos + bounds[3], data.texture->xstart, data.texture->yend,   color },
		animInfo
	);
}

template <typename ViewT>
static void DrawSimpleParticleSystem(ViewT&& view)
{
	ZoneScopedN("ECS::DrawSimpleParticleSystem");
	view.each([&](auto ent, auto...) {
		DrawSimpleParticleSystem(ent, view);
	});
}

template <typename ViewT>
static void DrawCBitmapMuzzleFlame(entt::entity ent, ViewT&& view)
{
	
	const auto& data = view.template get<const RenderData>(ent);	
	const auto& drad = view.template get<const DrawRadius>(ent).value;
	const auto& drawOrder = view.template get<const DrawOrder>(ent).drawOrder;
	const auto& a = view.template get<const AnimParams2>(ent);

	const auto& d = view.template get<const BitmapMuzzleFlame>(ent);

	if (!isParticleVisible(d.pos, d.pos, drad, d.allyteam, true)) {
		return;
	}	
	
	const float t = (projectileRegistry.ctx().at<PhysDelta>().frameNum - a.createFrame +
								 projectileRegistry.ctx().at<PhysDelta>().timeOffset);
	// rotParams.y is acceleration in angle per frame^2
	float rotVel = d.rotParams.x + d.rotParams.y * t;
	float rotVal = d.rotParams.z + rotVel      * t;
	
	const float life = t * d.decayrate;
	const float igrowth = d.sizeGrowth * (1.0f - Square(1.0f - life));

	const float isize = d.size * (igrowth + 1.0f);
	const float ilength = d.length * (igrowth + 1.0f);

	// SetDrawRadius(std::max(isize, ilength));

	unsigned char col[4];
	data.colorMap->GetColor(col, life);

	float3 fpos = d.pos + d.dir * d.frontOffset * ilength;

	const float3 zdir = (std::fabs(d.dir.dot(UpVector)) >= 0.99f)? FwdVector: UpVector;
	const float3 xdir = (d.dir.cross(zdir)).SafeANormalize();
	const float3 ydir = (d.dir.cross(xdir)).SafeANormalize();

	auto& dir = d.dir;
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

	if (math::fabs(d.rotVal) > 0.01f) {
		for (auto& b : bounds)
			b = b.rotate<false>(d.rotVal, dir);
	}

	float3 animInfo = { a.params.x, a.params.y, a.progress };
	
	auto& sideTexture = data.extraTexture;
	auto& pos = d.pos;
	if (IsValidTexture(sideTexture)) {
		AddEffectsQuad(drawOrder, camera->ProjectedDistance(d.pos),
			{ pos + bounds[0], sideTexture->xstart, sideTexture->ystart, col },
			{ pos + bounds[1], sideTexture->xend  , sideTexture->ystart, col },
			{ pos + bounds[2], sideTexture->xend  , sideTexture->yend  , col },
			{ pos + bounds[3], sideTexture->xstart, sideTexture->yend  , col },
					animInfo
					
		);
		AddEffectsQuad(drawOrder, camera->ProjectedDistance(d.pos),
			{ pos + bounds[4], sideTexture->xstart, sideTexture->ystart, col },
			{ pos + bounds[5], sideTexture->xend  , sideTexture->ystart, col },
			{ pos + bounds[6], sideTexture->xend  , sideTexture->yend  , col },
			{ pos + bounds[7], sideTexture->xstart, sideTexture->yend  , col },
					animInfo
		);
	}

	auto& frontTexture = data.texture;
	if (IsValidTexture(frontTexture)) {
		AddEffectsQuad(drawOrder, camera->ProjectedDistance(d.pos), 
			{ fpos + bounds[8 ], frontTexture->xstart, frontTexture->ystart, col },
			{ fpos + bounds[9 ], frontTexture->xend  , frontTexture->ystart, col },
			{ fpos + bounds[10], frontTexture->xend  , frontTexture->yend , col },
			{ fpos + bounds[11], frontTexture->xstart, frontTexture->yend , col },
					animInfo
		);
	}
}

template <typename ViewT>
static void DrawCBitmapMuzzleFlame(ViewT&& view)
{
	ZoneScopedN("ECS::DrawCBitmapMuzzleFlame");
	view.each([&](auto ent, auto...) {
		DrawCBitmapMuzzleFlame(ent, view);
	});
}

template <typename ViewT>
void UpdateDrawPosSpeedSystem(ViewT&& view)
//void UpdateDrawPosSpeedSystem(entt::view<entt::get_t<const Position, const Speed, DrawPosition>> view)
{
	const float t = projectileRegistry.ctx().at<PhysDelta>().timeOffset;
	view.each([&](
		auto ent, const Position& pos, const Speed& speed, DrawPosition& drawPos) {
		drawPos.value = (speed.value.w != 0.0f) ? (pos.value + speed.value * t) : pos.value;
	});
}

template <typename ViewT>
void UpdateDrawPosSystem(ViewT&& view)
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

template <typename T>
void UpdateAnimProgressSystem(T&& view)
{
	view.each([&](auto ent, auto& a) {
		const float t = (projectileRegistry.ctx().at<PhysDelta>().frameNum - a.createFrame +
						 projectileRegistry.ctx().at<PhysDelta>().timeOffset);
		if (static_cast<int>(a.params.x) <= 1 && static_cast<int>(a.params.y) <= 1) {
			a.progress = 0.0f;
			return;
		}
		
		const float animSpeed = math::fabs(a.params.z);
		if (a.params.z < 0.0f) {
			a.progress = 1.0f - math::fabs(math::fmod(t, 2.0f * animSpeed) / animSpeed - 1.0f);
		}
		else {
			a.progress = math::fmod(t, animSpeed) / animSpeed;
		}
	});
}

} // unnamed namespace

void LifetimePositionAboveGroundSystem(entt::registry& reg) {
	reg.view<const GroundCollisionTag, const Position>().each([&](const auto ent, auto& pos) {
		if(CGround::GetApproximateHeight(pos.value.x, pos.value.z, false) - 40.0f > pos.value.y) {
			DestroyEnt(ent, reg);
		}
	});
}

//void RotationSystem(entt::view<entt::get_t<Rotation, const RotParams, const CreateFrame>> view)
template <typename T>
void RotationSystem(T&& view)
{
	// TODO execute in Sim() or Draw()?
	view.each([&](const auto ent, auto& rot, const auto& rotParams, const auto& createFrame) {
		const float t = (projectileRegistry.ctx().at<PhysDelta>().frameNum - createFrame.v + projectileRegistry.ctx().at<PhysDelta>().timeOffset);
		// rotParams.y is acceleration in angle per frame^2
		rot.rotVel = rotParams.value.x + rotParams.value.y * t;
		rot.rotVal = rotParams.value.z + rot.rotVel      * t;
	});
}

void WindPositionSystem(entt::view<entt::get_t<const PositionWindChangeTag, Position, const Lifetime>> view) {
	auto& wind = envResHandler.GetCurrentWindVec();
	view.each([&](auto& p, const auto& lifetime) {
		p.value += (wind * lifetime.value * 0.05f);
	});
}

static bool CanDrawProjectile(const float3& pos, const int allyTeam,
							  const bool isAirLos)
{
	auto& th = teamHandler;
	auto& lh = losHandler;
	return (gu->spectatingFullView ||
			(th.IsValidAllyTeam(allyTeam) && th.Ally(allyTeam, gu->myAllyTeam))
			|| lh->InLos(pos, gu->myAllyTeam)
			|| (isAirLos && lh->InAirLos(pos, allyTeam)));
}

static bool isParticleVisible(const float3& pos, const float3& drawPos,
							  const float& drawRadius, const int allyteam,
							  const bool isAirLos)
{
	if (!CanDrawProjectile(pos, allyteam, isAirLos))
		return false;

	bool drawRefraction = projectileRegistry.ctx().at<DrawMode>().drawRefraction;
	if (drawRefraction && (drawPos.y > drawRadius) /*!pro->IsInWater()*/)
		return false;
	// removed this to fix AMD particle drawing
	//if (drawReflection && !CModelDrawerHelper::ObjectVisibleReflection(pro->drawPos, camera->GetPos(), pro->GetDrawRadius()))
	//	return;

	const CCamera* cam = CCameraHandler::GetActiveCamera();
	if (!cam->InView(drawPos, drawRadius))
		return false;

	return true;
}

static void UpdateVisibilitySystem(entt::registry& reg)
{
	reg.clear<VisibleTag>();
	const CCamera* cam = CCameraHandler::GetActiveCamera();
	
	projectileRegistry.view<const DrawPosition, const DrawRadius, const AlliedTeam>().each(
				[&](auto ent, const auto& drawPos, const auto& drawRadius, const auto& allyteam) {
		bool isAirLos = projectileRegistry.all_of<AirLosTag>(ent);
		if (!CanDrawProjectile(drawPos.value, allyteam.value, isAirLos)) {
			return;
		}
		if (!cam->InView(drawPos.value, drawRadius.value)) {
			return;
		}
		
		bool drawRefraction = projectileRegistry.ctx().at<DrawMode>().drawRefraction;
		if (drawRefraction && (drawPos.value.y > drawRadius.value) /*!pro->IsInWater()*/)
			return;
		
		// removed this to fix AMD particle drawing
		//if (drawReflection && !CModelDrawerHelper::ObjectVisibleReflection(pro->drawPos, camera->GetPos(), pro->GetDrawRadius()))
		//	return;
		
		reg.emplace_or_replace<VisibleTag>(ent);	
	});
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
			
	float3 animInfo = { 1.0, 1.0, 0.0 };
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
	float3 animInfo = { 1.0, 1.0, 0.0 };
	
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
	auto& texture = view.template get<const RenderData>(ent).texture;

	auto& drawPos = view.template get<const DrawPosition>(ent).value;

	float3 animInfo = { 1.0, 1.0, 0.0 };

	auto& size = view.template get<const Sized>(ent).value;
	auto& sizemod = view.template get<const SizeChange>(ent).sizeMod;
	auto& sizeGrowth = view.template get<const SizeChange>(ent).sizeGrowth;

	const auto& rot = view.template get<const Rotation>(ent);

	unsigned char col[4];
	const float dheat = std::max(0.0f, heat - globalRendering->timeOffset);
	const float alpha = (dheat / maxheat) * 255.0f;

	col[0] = (unsigned char) alpha;
	col[1] = (unsigned char) alpha;
	col[2] = (unsigned char) alpha;
	col[3] = 1;//(dheat/maxheat)*255.0f;

	const float drawsize = (size + sizeGrowth * globalRendering->timeOffset) * sizemod;

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
			b = b.rotate<false>(rot.rotVal, camera->GetForward());
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
	float3 animInfo = { 1.0, 1.0, 0.0 };

	auto& size = view.template get<const FlameSizeChange>(ent).v;
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


template <typename ViewT>
static void DrawCSmokeProjectile(entt::entity ent, ViewT&& view) 
{
	auto& pos = view.template get<const Position>(ent).value;
	auto& age = view.template get<const Lifetime>(ent).value;
	auto& color = view.template get<const Color>(ent).v;
	auto& size = view.template get<const SmokeSized>(ent).v;
	auto& sizeExpansion = view.template get<const SmokeSizeChange>(ent).sizeGrowth;
	auto& drawPos = view.template get<const DrawPosition>(ent).value;
	auto& st = view.template get<const RenderData>(ent).texture;
	
	float3 animInfo = { 1.0, 1.0, 0.0 };

	unsigned char col[4];
	unsigned char alpha = (unsigned char) ((1 - age) * 255);
	col[0] = (unsigned char) (color.x * alpha);
	col[1] = (unsigned char) (color.x * alpha);
	col[2] = (unsigned char) (color.x * alpha);
	col[3] = (unsigned char) alpha/*-alphaFalloff*globalRendering->timeOffset*/;
	//int frame=textureNum;
	//float xmod=0.125f+(float(int(frame%6)))/16;
	//float ymod=(int(frame/6))/16.0f;

	const float interSize = size + (sizeExpansion * globalRendering->timeOffset);
	const float3 pos1 ((camera->GetRight() - camera->GetUp()) * interSize);
	const float3 pos2 ((camera->GetRight() + camera->GetUp()) * interSize);

	AddEffectsQuad(
		{ drawPos - pos2, st->xstart, st->ystart, col },
		{ drawPos + pos1, st->xend,   st->ystart, col },
		{ drawPos + pos2, st->xend,   st->yend,   col },
		{ drawPos - pos1, st->xstart, st->yend,   col },
		animInfo
	);
}

template <typename ViewT>
static void DrawCSmokeTrailProjectile(entt::entity ent, ViewT&& view) 
{
	auto& lifeTime = view.template get<const Lifetime>(ent).value;
	auto& decay = view.template get<const Decayrate>(ent).value;
	auto& d = view.template get<const SmokeTrail>(ent);
	auto& texture = view.template get<const RenderData>(ent).texture;
	auto& createFrame = view.template get<const CreateFrame>(ent).v;
	
	auto color = view.template get<const Color>(ent).v.x;
	
	float3 animInfo = { 1.0, 1.0, 0.0 };
	
	const float age = projectileRegistry.ctx().at<PhysDelta>().frameNum + 
					  projectileRegistry.ctx().at<PhysDelta>().timeOffset - createFrame;
	
	const float invLifeTime = decay;

	const bool shadowPass = (camera->GetCamType() == CCamera::CAMTYPE_SHADOW);

	const float3 dif1 = shadowPass ? camera->GetForward() : (d.pos1 - camera->GetPos()).ANormalize();
	const float3 dif2 = shadowPass ? camera->GetForward() : (d.pos2 - camera->GetPos()).ANormalize();

	const float3 odir1 = (dif1.cross(d.dir1)).ANormalize();
	const float3 odir2 = (dif2.cross(d.dir2)).ANormalize();

	const float t1 = (age                    ) * invLifeTime;
	const float tm = (age + 0.5f * d.lifePeriod) * invLifeTime;
	const float t2 = (age +        d.lifePeriod) * invLifeTime;

	const float lerp1 = ((1.0f - t1) * (0.7f + std::fabs(dif1.dot(d.dir1)))) * (1 - d.lastSegment );
	const float lerp2 = ((1.0f - t2) * (0.7f + std::fabs(dif2.dot(d.dir2)))) * (1 - d.firstSegment);

	const float size1 = 1.0f + t1 * d.origSize;
	const float size2 = 1.0f + t2 * d.origSize;


	const SColor colBase = { color, color, color, 1.0f };
	const SColor col1 = colBase * std::clamp(lerp1, 0.0f, 1.0f);
	const SColor col2 = colBase * std::clamp(lerp2, 0.0f, 1.0f);

	if (d.drawSegmented) {

		const float3 difm = shadowPass ? camera->GetForward() : (d.midpos - camera->GetPos()).ANormalize();
		const float3 odirm = (difm.cross(d.middir)).ANormalize();

		const float lerpm = (1.0f - tm) * (0.7f + std::fabs(difm.dot(d.middir)));
		const float sizem = (0.2f + tm) * d.origSize;
		const float midtexx = mix(texture->xstart, texture->xend, 0.5f);

		const SColor colm = colBase * std::clamp(lerpm, 0.0f, 1.0f);

		AddEffectsQuad(
			{ d.pos1   - (odir1 * size1), texture->xstart, texture->ystart, col1  },
			{ d.midpos - (odirm * sizem), midtexx        , texture->ystart, colm },
			{ d.midpos + (odirm * sizem), midtexx        , texture->yend  , colm },
			{ d.pos1   + (odir1 * size1), texture->xstart, texture->yend  , col1  },
			animInfo
		);

		AddEffectsQuad(
			{ d.midpos - (odirm * sizem), midtexx      ,   texture->ystart, colm },
			{ d.pos2   - (odir2 * size2), texture->xend,   texture->ystart, col2 },
			{ d.pos2   + (odir2 * size2), texture->xend,   texture->yend  , col2 },
			{ d.midpos + (odirm * sizem), midtexx      ,   texture->yend  , colm },
			animInfo
		);
	} else {
		AddEffectsQuad(
			{ d.pos1 - (odir1 * size1), texture->xstart, texture->ystart, col1 },
			{ d.pos2 - (odir2 * size2), texture->xend  , texture->ystart, col2 },
			{ d.pos2 + (odir2 * size2), texture->xend  , texture->yend  , col2 },
			{ d.pos1 + (odir1 * size1), texture->xstart, texture->yend  , col1 },
			animInfo
		);
	}
}


void PreDrawSystem() {
	ZoneScopedN("ECS::PreDrawSystem");
	UpdateAnimProgressSystem(projectileRegistry.view<AnimParams2>()); 
	/*
	UpdateDrawPosSystem(projectileRegistry.view<const Position, DrawPosition>(entt::exclude<Speed>));
	UpdateDrawPosSpeedSystem(projectileRegistry.view<const Position, const Speed, DrawPosition>());
	UpdateDrawOrder(projectileRegistry.view<const DrawPosition, DrawOrder>());
	
	RotationSystem(projectileRegistry.group<Rotation, const RotParams>(entt::get<const CreateFrame>));
	*/
	//UpdateVisibilitySystem(projectileRegistry);
	// TODO add update DrawRadius
}

template <typename ViewT>
static void DispatchDrawingECS(entt::entity ent, ViewT&& view) {
	if (projectileRegistry.all_of<SimpleParticleSystemTag>(ent)) {
		DrawSimpleParticleSystem(ent, projectileRegistry);
	} else if (projectileRegistry.all_of<CBitmapMuzzleFlameTag>(ent)) {
		DrawCBitmapMuzzleFlame(ent, projectileRegistry);
	} else if (projectileRegistry.all_of<CDirtProjectileTag>(ent)) {
		DrawCDirtProjectile(ent, projectileRegistry);
	} else if (projectileRegistry.all_of<CExploSpikeProjectileTag>(ent)) {
		DrawCExploSpikeProjectile(ent, projectileRegistry);
	} else if (projectileRegistry.all_of<CHeatCloudProjectileTag>(ent)) {
		DrawCHeatCloudProjectile(ent, projectileRegistry);
	} else if (projectileRegistry.all_of<CMuzzleFlameTag>(ent)) {
		DrawCMuzzleFlame(ent, projectileRegistry);
	} else if (projectileRegistry.all_of<CSmokeProjectileTag>(ent)) {
		DrawCSmokeProjectile(ent, projectileRegistry);
	} else if (projectileRegistry.all_of<CSmokeTrailProjectileTag>(ent)) {
		DrawCSmokeTrailProjectile(ent, projectileRegistry);
	}
}

// Draws new ECS projectiles and legacy sortedProjectiles
// this is a temporary compatible solution that respects drawing order and
// prevents any graphical artifacts when drawing mixed ECS and legacy OOP projectiles
// this approach uses runtime look up of the components type
// Problem? It's slow. It's better to iterate over projectile type and do distance sorting
// in a different way, e.g. by modyfing index buffer later.
//void DrawSystem(const std::vector<std::pair<std::pair<int, float>, CProjectile*>>& sortedProj)
//{
//	projectileRegistry.sort<DrawOrder>([](const auto &lhs, const auto &rhs) {
//		return std::pair(lhs.drawOrder, lhs.distanceFromCamera) < std::pair(rhs.drawOrder, rhs.distanceFromCamera);
//	});

//	auto projIt = sortedProj.begin();
//	projectileRegistry.view<const DrawOrder/*, const VisibleTag*/>().each([&](auto ent, const auto& drawOrder) {
//		const auto dist = std::pair{drawOrder.drawOrder, drawOrder.distanceFromCamera};
//		while (projIt != sortedProj.end() && projIt->first < dist) {
//			projIt->second->Draw();
//			++projIt;
//		}	

//		DispatchDrawingECS(ent, projectileRegistry);
//	});

//	while (projIt != sortedProj.end()) {
//		projIt->second->Draw();
//		++projIt;
//	}
//}


// unsorted
void DrawSystem()
{
	DrawSimpleParticleSystem(
		projectileRegistry.view<const SimpleParticle, const AnimParams2/*, const DrawOrder,
				const DrawRadius, const RenderData*/>()
	);
}

void DrawShadowSystem()
{
//	projectileRegistry.view<const VisibleTag, const CastShadowTag>().each([&](auto ent) {
//		DispatchDrawingECS(ent, projectileRegistry);
//	});
	
	DrawSimpleParticleSystem(
		projectileRegistry.view<const CastShadowTag, const SimpleParticle, const AnimParams2>()
	);
}
