/*
ECS checklist:

Done:
* Runtime switch for ECS MODE (you can pause the game, switch the mode to observe the difference)
* Migrated types projectiles to ECS:
  CSimpleParticleSystem CBitmapMuzzleFlame CDirtProjectile CExploSpikeProjectile
  CDirtProjectile CHeatCloudProjectile CMuzzleFlame
  CSmokeProjectile CSmokeTrailProjectile
* Projectile drawing & sorting based on draw distance (integrated with existing synced projectiles)
* Shadow drawing
* Minimap drawing

To do:
* Make clear distinction what should be computed in Sim() and what in Draw() contexts.
  Most projectiles do very little in Sim() (only update lifetime) except for SimpleParticleSystem and 
  unsynced projectiles that interact with environment - e.g. Dirt projectile disappears
  after hitting the ground.
* Create new Spawner class (see explosion generator) that doesn't require legacy Particles to exist
  (currently ECS particles copy out data from original Particles, then deallocates them)
* Parallel executor on task graph?

*/
#include "Sim/Projectiles/Projectile.h"

#include "Game/Camera.h"
#include "Game/GlobalUnsynced.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/GL/RenderBuffers.h"
#include "Rendering/Textures/ColorMap.h"
#include "Rendering/Env/Particles/ProjectileDrawer.h"
#include "Rendering/Colors.h"

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

#include "ECS_systems.h"
#include "lib/entt/src/entt/entt.hpp"



extern entt::registry projectileRegistry;
extern std::array<TypedRenderBuffer<VA_TYPE_PROJ>, 10> projRenderBuffers;

static TypedRenderBuffer<VA_TYPE_PROJ>& getProjBuf(int idx) {
	idx = std::clamp(idx, 0, 10);
	return projRenderBuffers[idx];
}

namespace {

template<bool synced, typename Iterable>
static void rotate2(float angle, const float3& axis, Iterable& iterable) {
	float ca;
	float sa;

	ca = std::cos(angle);
	sa = std::sin(angle);

	//Rodrigues' rotation formula
	// https://en.wikipedia.org/wiki/Rodrigues%27_rotation_formula
	for (auto& v : iterable) {
		v = v * ca + axis.cross(v) * sa + axis * axis.dot(v) * (1.0f - ca);
	}
}

template <typename T>
void UpdateRotation(T& e)
{
	const float t = (projectileRegistry.ctx().at<PhysDelta>().frameNum - e.createFrame + projectileRegistry.ctx().at<PhysDelta>().timeOffset);
	// rotParams.y is acceleration in angle per frame^2
	e.rotVel = e.rotParams.x + e.rotParams.y * t;
	e.rotVal = e.rotParams.z + e.rotVel      * t;
}

template <typename T>
void UpdateAnimProgress(T& a)
{
	const float t = (projectileRegistry.ctx().at<PhysDelta>().frameNum - a.createFrame +
					 projectileRegistry.ctx().at<PhysDelta>().timeOffset);
	if (static_cast<int>(a.params.x) <= 1 && static_cast<int>(a.params.y) <= 1) {
		a.progress = 0.0f;
		return;
	}
	
	const float animSpeed = std::fabs(a.params.z);
	if (a.params.z < 0.0f) {
		a.progress = 1.0f - std::fabs(math::fmod(t, 2.0f * animSpeed) / animSpeed - 1.0f);
	}
	else {
		a.progress = math::fmod(t, animSpeed) / animSpeed;
	}
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

static void AddEffectsQuad(const VA_TYPE_TC& tl, const VA_TYPE_TC& tr, const VA_TYPE_TC& br, const VA_TYPE_TC& bl, const float3& animInfo, int drawOrder)
{
	float minS = std::numeric_limits<float>::max()   ; float minT = std::numeric_limits<float>::max()   ;
	float maxS = std::numeric_limits<float>::lowest(); float maxT = std::numeric_limits<float>::lowest();
	std::invoke([&](auto&&... arg) {
		((minS = std::min(minS, arg.s)), ...);
		((minT = std::min(minT, arg.t)), ...);
		((maxS = std::max(maxS, arg.s)), ...);
		((maxT = std::max(maxT, arg.t)), ...);
	}, tl, tr, br, bl);

	auto& rb = getProjBuf(drawOrder);

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

static auto safeANormalize(const auto& ydir) {	
	const float sql = ydir.SqLength();
	if likely(sql > float3::nrm_eps()) {
		return ydir * fastmath::isqrt_nosse(sql);
	}
	return ydir;
};

template <typename T>
bool isVisible(const T& d) {
	const bool* ptr = &d.visible;
	ptr += projectileRegistry.ctx().at<DrawMode>().mode;
	return *ptr;
	/*
	if (cam_type == CCamera::CAMTYPE_PLAYER) {
		return d.visibleRefraction;			
	} else if (cam_type == CCamera::CAMTYPE_UWREFL) {
		return d.visibleReflection;
	} else if (cam_type == CCamera::CAMTYPE_SHADOW) {
		return d.visibleShadow;
	}
	return true;
	*/
}

static void DrawSimpleParticleSystem(entt::registry& reg) 
{
	ZoneScopedN("ECS::DrawSimpleParticleSystem");
	const float3 right = camera->GetRight();
	const float3 up = camera->GetUp();
	const float3 fwd = camera->GetForward();
	
	const bool shadowPass = (camera->GetCamType() == CCamera::CAMTYPE_SHADOW);
	const uint32_t cam_type = camera->GetCamType();
	
	
	auto UpdateBounds = [&](auto& d) {
		std::array<float3, 4> bounds;
			
		const auto cpos = camera->GetPos();
		
		const float3& right = camera->GetRight();
		const float3& up = camera->GetUp();
		const float3& fwd = camera->GetForward();
		
		const bool shadowPass = (camera->GetCamType() == CCamera::CAMTYPE_SHADOW);
		
		if (d.r.directional && !shadowPass) {
			const float3 zdir = safeANormalize(d.pos - cpos);
			float3 ydir = zdir.cross(d.speed);
			const float yDirLen2 = ydir.SqLength();
			ydir = safeANormalize(ydir);
			const float3 xdir = ydir.cross(zdir);
	
			if (yDirLen2 > 0.001f) {
				bounds = {
					-ydir * d.size - xdir * d.size,
					-ydir * d.size + xdir * d.size,
					 ydir * d.size + xdir * d.size,
					 ydir * d.size - xdir * d.size
				};
				if (std::fabs(d.rotVal) > 0.01f) {
					rotate2<false>(d.rotVal, fwd, bounds);
				}
				return bounds;
			}
		}
		
		// not directional	
		const float3 cameraRight = right * d.size;
		const float3 cameraUp    = up    * d.size;
	
		bounds = {
			-cameraRight - cameraUp,
			 cameraRight - cameraUp,
			 cameraRight + cameraUp,
			-cameraRight + cameraUp
		};
		
		if (std::fabs(d.rotVal) > 0.01f) {
			rotate2<false>(d.rotVal, fwd, bounds);
		}
		return bounds;
	};
	
	reg.view<SimpleParticle>().each([&] (auto ent, SimpleParticle& d) {
		if (!isVisible(d)) {
			return;
		}
		auto bounds = UpdateBounds(d);
		const auto& data = d.r;
		
		const float3& interPos = d.drawPos;
		float3 animInfo = { d.params.x, d.params.y, d.progress };

		AddEffectsQuad(
			{ interPos + bounds[0], data.texture->xstart, data.texture->ystart, d.color.data() },
			{ interPos + bounds[1], data.texture->xend,   data.texture->ystart, d.color.data() },
			{ interPos + bounds[2], data.texture->xend,   data.texture->yend,   d.color.data() },
			{ interPos + bounds[3], data.texture->xstart, data.texture->yend,   d.color.data() },
			animInfo, d.drawo.drawOrder
		);
		
		/*
		auto& rb = CExpGenSpawnable::GetPrimaryRenderBuffer();
		if (rb.GetSortMode()) {
			uint64_t order (static_cast<uint64_t>(d.drawo.drawOrder) << 32 | static_cast<uint32_t>(-d.drawo.distanceFromCamera));
			rb.AddQuadOrder(order);
		}
		*/
	});
}

static void DrawBitmapMuzzleFlameSystem(entt::registry& reg) 
{
	ZoneScopedN("ECS::DrawBitmapMuzzleFlameSystem");		
	reg.view<BitmapMuzzleFlame>().each([&] (auto ent, BitmapMuzzleFlame& d) {	
		
	if (!isVisible(d))
	{
		return;
	}
	
	UpdateRotation(d);
	UpdateAnimProgress(d);

	const float t = (projectileRegistry.ctx().at<PhysDelta>().frameNum - d.createFrame +
								 projectileRegistry.ctx().at<PhysDelta>().timeOffset);
	
	const float life = t * d.decayrate;
	const float igrowth = d.sizeGrowth * (1.0f - Square(1.0f - life));

	const float isize = d.size * (igrowth + 1.0f);
	const float ilength = d.length * (igrowth + 1.0f);

	d.drawRadius = std::max(isize, ilength); // TODO why it breaks drawing?

	unsigned char col[4];
	d.r.colorMap->GetColor(col, life);

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

	if (std::fabs(d.rotVal) > 0.01f) {
		rotate2<false>(d.rotVal, dir, bounds);
	}

	float3 animInfo = { d.params.x, d.params.y, d.progress };
	
	auto& sideTexture = d.r.extraTexture;
	auto& pos = d.pos;
	if (IsValidTexture(sideTexture)) {
		AddEffectsQuad(
			{ pos + bounds[0], sideTexture->xstart, sideTexture->ystart, col },
			{ pos + bounds[1], sideTexture->xend  , sideTexture->ystart, col },
			{ pos + bounds[2], sideTexture->xend  , sideTexture->yend  , col },
			{ pos + bounds[3], sideTexture->xstart, sideTexture->yend  , col },
			animInfo, d.drawo.drawOrder
					
		);
		AddEffectsQuad(
			{ pos + bounds[4], sideTexture->xstart, sideTexture->ystart, col },
			{ pos + bounds[5], sideTexture->xend  , sideTexture->ystart, col },
			{ pos + bounds[6], sideTexture->xend  , sideTexture->yend  , col },
			{ pos + bounds[7], sideTexture->xstart, sideTexture->yend  , col },
			animInfo, d.drawo.drawOrder
		);
		
		/*
		auto& rb = CExpGenSpawnable::GetPrimaryRenderBuffer();
		if (rb.GetSortMode()) {
			uint64_t order (static_cast<uint64_t>(d.drawo.drawOrder) << 32 | static_cast<uint32_t>(-d.drawo.distanceFromCamera));
			rb.AddQuadOrder(order);
			rb.AddQuadOrder(order);
		}
		*/
	}

	auto& frontTexture = d.r.texture;
	if (IsValidTexture(frontTexture)) {
		AddEffectsQuad(
			{ fpos + bounds[8 ], frontTexture->xstart, frontTexture->ystart, col },
			{ fpos + bounds[9 ], frontTexture->xend  , frontTexture->ystart, col },
			{ fpos + bounds[10], frontTexture->xend  , frontTexture->yend , col },
			{ fpos + bounds[11], frontTexture->xstart, frontTexture->yend , col },
			animInfo, d.drawo.drawOrder
		);
		/*
		auto& rb = CExpGenSpawnable::GetPrimaryRenderBuffer();
		if (rb.GetSortMode()) {
			uint64_t order (static_cast<uint64_t>(d.drawo.drawOrder) << 32 | static_cast<uint32_t>(-d.drawo.distanceFromCamera));
			rb.AddQuadOrder(order);
		}
		*/
	}
	
	});
}

/*
void UpdateDrawOrder(entt::view_t<SimpleParticle> view)
{
	const CCamera* cam = CCameraHandler::GetActiveCamera();
	view.each([&](
		auto ent, SimpleParticle& sp) {
		sp.drawo.distanceFromCamera = -cam->ProjectedDistance(sp.pos);
	});
}
*/

template <typename T>
void UpdateDrawPos(T& d, float t) {
	d.drawPos = (d.speed.w != 0.0f) ? (d.pos + d.speed * t) : d.pos;
}

template <typename T>
bool UpdateVisibility(T& d) {
	const CCamera* cameras[] = {
		CCameraHandler::GetCamera(CCamera::CAMTYPE_PLAYER),
		CCameraHandler::GetCamera(CCamera::CAMTYPE_UWREFL),
		CCameraHandler::GetCamera(CCamera::CAMTYPE_SHADOW)
	};
	
	bool visible = CanDrawProjectile(d.pos, d.allyteam, d.useAirLos);
	
	d.visible = visible && cameras[0]->InView(d.drawPos, d.drawRadius);
	d.visibleRefraction = visible && d.drawPos.y <= d.drawRadius && cameras[0]->InView(d.drawPos, d.drawRadius);
	d.visibleReflection = visible && cameras[1]->InView(d.drawPos, d.drawRadius);
	d.visibleShadow = d.castShadow && visible && cameras[2]->InView(d.drawPos, d.drawRadius);
	return visible;
}

template <typename T>
void UpdateColor(T& d) {
	d.r.colorMap->GetColor(d.color.data(), d.life);
}

template <typename T>
void PreUpdateVisibilitySystem(auto&& reg)
{
	auto t = globalRendering->timeOffset;
	reg.template view<T>().each([&](auto ent, auto& sp) {		
		UpdateDrawPos(sp, t);
		auto visible = UpdateVisibility(sp);
	});
}

template <typename T>
void PreUpdateSimpleParticleSystem(T&& view)
{
	auto t = globalRendering->timeOffset;
	view.each([&](auto ent, auto& sp) {		
		UpdateDrawPos(sp, t);
		auto visible = UpdateVisibility(sp);
		if (!visible)
			return;
		UpdateAnimProgress(sp);
		//UpdateBounds(sp);
		UpdateColor(sp);
		// sp.drawo.distanceFromCamera = -cam->ProjectedDistance(sp.pos); // not needed, USE WBOIT
	});
}

template <typename T>
void SetVelocityAndSpeedSystem(T& e, const float3& v) 
{
	e.speed = v;
	e.speed.w = v.Length();
	
	/*
	if (e.speed.w <= 0.0f)
		return;

	e.dir = e.speed / e.speed.w;
	*/
}

} // unnamed namespace

void UpdateDirtProjectile(entt::registry& reg) {
	ZoneScopedN("XYZ::UpdateDirtProjectile");
	reg.view<DirtProjectile>().each([&](const auto ent, auto& e) {	
		SetVelocityAndSpeedSystem(e, (e.speed * e.slowdown) + (UpVector * e.mygravity));
		e.pos += e.speed;
		e.alpha = std::max(e.alpha - e.alphaFalloff, 0.0f);
		e.size += e.sizeExpansion;
		
		if(CGround::GetApproximateHeight(e.pos.x, e.pos.z, false) - 40.0f > e.pos.y || e.alpha <= 0.0f) {
			DestroyEnt(ent, reg);
		}
	});
}

void UpdateSmokeProjectile(entt::registry& reg) {
	ZoneScopedN("XYZ::UpdateSmokeProjectile");
	const auto wind = envResHandler.GetCurrentWindVec();
	
	reg.view<SmokeProjectile>().each([&] (auto ent, auto& e) {
		e.pos += e.speed;
		e.pos += (wind * e.age * 0.05f);
		e.age += e.ageSpeed;
		e.size += e.sizeExpansion;
		e.size += ((e.startSize - e.size) * 0.2f * (e.size < e.startSize));
		e.drawRadius = e.size;
		
		if (e.age >= 1.0) {
			DestroyEnt(ent, reg);
		}
	});
}

void UpdateSmokeTrailProjectile(entt::registry& reg) {
	ZoneScopedN("XYZ::UpdateSmokeTrailProjectile");
	auto frameNum = projectileRegistry.ctx().at<PhysDelta>().frameNum;
	reg.view<SmokeTrail>().each([&] (auto ent, auto& e) {
		if (frameNum >= (e.creationTime + e.lifeTime)) {
			DestroyEnt(ent, reg);
		}
	});
}

static void DrawDirtProjectileSystem(entt::registry& reg)
{
	ZoneScopedN("XYZ::DrawDirtProjectileSystem");
	auto up = camera->GetUp();
	auto right = camera->GetRight();
	
	reg.view<DirtProjectile>().each([&] (auto ent, auto& e) {	
		if (!isVisible(e)) {
			return;
		}
	
	float3 animInfo = { 1.0, 1.0, 0.0 };
	float partAbove = (e.pos.y / (e.size * up.y));

	if (partAbove < -1.0f)
		return;

	partAbove = std::min(partAbove, 1.0f);

	unsigned char col[4];
	col[0] = (unsigned char) (e.color.x * e.alpha);
	col[1] = (unsigned char) (e.color.y * e.alpha);
	col[2] = (unsigned char) (e.color.z * e.alpha);
	col[3] = (unsigned char) (e.alpha)/*- (globalRendering->timeOffset * alphaFalloff)*/;

	const float interSize = e.size + globalRendering->timeOffset * e.sizeExpansion;
	const float texx = e.r.texture->xstart + (e.r.texture->xend - e.r.texture->xstart) * ((1.0f - partAbove) * 0.5f);

	const float3& drawPos = e.drawPos;
	AddEffectsQuad(
		{ drawPos - right * interSize - up * interSize * partAbove, texx,          e.r.texture->ystart, col },
		{ drawPos - right * interSize + up * interSize,             e.r.texture->xend, e.r.texture->ystart, col },
		{ drawPos + right * interSize + up * interSize,             e.r.texture->xend, e.r.texture->yend,   col },
		{ drawPos + right * interSize - up * interSize * partAbove, texx,          e.r.texture->yend,   col },
		animInfo, e.drawo.drawOrder
	);
	
	});
}

static void DrawExploSpikeProjectile(entt::registry& reg) 
{
	ZoneScopedN("XYZ::DrawExploSpikeProjectile");
	reg.view<ExploSpikeProjectile>().each([&] (auto ent, ExploSpikeProjectile& e) {
		if (!isVisible(e)) {
			return;
		}
		
	const float3& drawPos = e.drawPos;
	float3 animInfo = { 1.0, 1.0, 0.0 };
	
	const float3 dif = (e.pos - camera->GetPos()).ANormalize();
	const float3 dir2 = (dif.cross(e.dir)).ANormalize();

	unsigned char col[4];
	const float a = std::max(0.0f, e.alpha - e.alphaDecay * globalRendering->timeOffset) * 255.0f;
	col[0] = (unsigned char)(a * e.color.x);
	col[1] = (unsigned char)(a * e.color.y);
	col[2] = (unsigned char)(a * e.color.z);
	col[3] = 1;

	const float3 l = (e.dir * e.length) + (e.lengthGrowth * globalRendering->timeOffset);
	const float3 w = dir2 * e.width;

	auto* let = projectileDrawer->laserendtex;
	AddEffectsQuad(
		{ drawPos - l - w, let->xstart, let->ystart, col },
		{ drawPos + l - w, let->xend,   let->ystart, col },
		{ drawPos + l + w, let->xend,   let->yend,   col },
		{ drawPos - l + w, let->xstart, let->yend,   col },
		animInfo, e.drawo.drawOrder
	);
	
	});
}

static void DrawHeatCloudProjectile(entt::registry& reg) 
{
	ZoneScopedN("XYZ::DrawHeatCloudProjectile");
	
	const float3 ri = camera->GetRight();
	const float3 up = camera->GetUp();
	const float3 fwd = camera->GetForward();
	
	reg.view<HeatCloudProjectile>().each([&] (auto ent, HeatCloudProjectile& e) {	
	if (!isVisible(e)) {
		return;
	}
	UpdateRotation(e);
	float3 animInfo = { 1.0, 1.0, 0.0 };
	
	unsigned char col[4];
	const float dheat = std::max(0.0f, e.heat - globalRendering->timeOffset);
	const float alpha = (dheat / e.maxheat) * 255.0f;

	col[0] = (unsigned char) alpha;
	col[1] = (unsigned char) alpha;
	col[2] = (unsigned char) alpha;
	col[3] = 1;//(dheat/maxheat)*255.0f;

	const float drawsize = (e.size + e.sizeGrowth * globalRendering->timeOffset) * (1.0f - e.sizemod);

	std::array<float3, 4> bounds = {
		-ri * drawsize - up * drawsize,
		ri * drawsize - up * drawsize,
		ri * drawsize + up * drawsize,
		-ri * drawsize + up * drawsize
	};

	if (std::fabs(e.rotVal) > 0.01f) {
		rotate2<false>(e.rotVal, fwd, bounds);
	}
	
	const float3& drawPos = e.drawPos;
	
	AddEffectsQuad(
			{ drawPos + bounds[0], e.r.texture->xstart, e.r.texture->ystart, col },
			{ drawPos + bounds[1], e.r.texture->xend,   e.r.texture->ystart, col },
			{ drawPos + bounds[2], e.r.texture->xend,   e.r.texture->yend,   col },
			{ drawPos + bounds[3], e.r.texture->xstart, e.r.texture->yend,   col },
			animInfo, e.drawo.drawOrder
			);
	
	});
};

static void DrawMuzzleFlame(entt::registry& reg) 
{
	ZoneScopedN("XYZ::DrawMuzzleFlame");
	reg.view<MuzzleFlame>().each([&] (auto ent, MuzzleFlame& e) {	
		if (!isVisible(e)) {
			return;
		}
		
	float3 animInfo = { 1.0, 1.0, 0.0 };
	
	unsigned char col[4];
	float alpha = std::max(0.0f, 1 - (e.age / (4 + e.size * 30)));
	float modAge = fastmath::apxsqrt(static_cast<float>(e.age + 2));

	const int tex = e.index % projectileDrawer->NumSmokeTextures();
	// float xmod = 0.125f + (float(int(tex % 6))) / 16.0f;
	// float ymod =                (int(tex / 6))  / 16.0f;

	float drawsize = modAge * 3;
	float3 interPos(e.pos+e.dir*(e.index+2)*modAge*0.4f);
	float fade = std::max(0.0f, std::min(1.0f, (1 - alpha) * (20 + e.index) * 0.1f));

	col[0] = (unsigned char) (180 * alpha * fade);
	col[1] = (unsigned char) (180 * alpha * fade);
	col[2] = (unsigned char) (180 * alpha * fade);
	col[3] = (unsigned char) (255 * alpha * fade);

	auto* st = e.r.texture;
	AddEffectsQuad(
		{ interPos - camera->GetRight() * drawsize - camera->GetUp() * drawsize, st->xstart, st->ystart, col },
		{ interPos + camera->GetRight() * drawsize - camera->GetUp() * drawsize, st->xend,   st->ystart, col },
		{ interPos + camera->GetRight() * drawsize + camera->GetUp() * drawsize, st->xend,   st->yend,   col },
		{ interPos - camera->GetRight() * drawsize + camera->GetUp() * drawsize, st->xstart, st->yend,   col },
		animInfo, e.drawo.drawOrder
	);

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
			animInfo, e.drawo.drawOrder
		);
		#undef mft
	}
	
	});
}

static void DrawSmokeProjectile(entt::registry& reg) 
{
	ZoneScopedN("XYZ::DrawSmokeProjectile");
	float3 right = camera->GetRight();
	float3 up = camera->GetUp();
	
	reg.view<SmokeProjectile>().each([&] (auto ent, SmokeProjectile& e) {
		if (!isVisible( e)) {
			return;
		}
		
	auto& st = e.r.texture;
	float3 animInfo = { 1.0, 1.0, 0.0 };

	unsigned char col[4];
	unsigned char alpha = (unsigned char) ((1 - e.age) * 255);
	col[0] = (unsigned char) (e.color * alpha);
	col[1] = (unsigned char) (e.color * alpha);
	col[2] = (unsigned char) (e.color * alpha);
	col[3] = (unsigned char) alpha/*-alphaFalloff*globalRendering->timeOffset*/;
	//int frame=textureNum;
	//float xmod=0.125f+(float(int(frame%6)))/16;
	//float ymod=(int(frame/6))/16.0f;

	const float interSize = e.size + (e.sizeExpansion * globalRendering->timeOffset);
	const float3 pos1 ((right - up) * interSize);
	const float3 pos2 ((right + up) * interSize);

	const float3& drawPos = e.drawPos;
	
	AddEffectsQuad(
		{ drawPos - pos2, st->xstart, st->ystart, col },
		{ drawPos + pos1, st->xend,   st->ystart, col },
		{ drawPos + pos2, st->xend,   st->yend,   col },
		{ drawPos - pos1, st->xstart, st->yend,   col },
		animInfo, e.drawo.drawOrder
	);
	});
}

static void DrawSmokeTrailProjectile(entt::registry& reg) 
{
	ZoneScopedN("XYZ::DrawSmokeTrailProjectile");
	
	reg.view<SmokeTrail>().each([&] (auto ent, SmokeTrail& d) {	
		if (!isVisible(d)) {
			return;
		}
		
	float3 animInfo = { 1.0, 1.0, 0.0 };
	
	const float age = projectileRegistry.ctx().at<PhysDelta>().frameNum + 
					  projectileRegistry.ctx().at<PhysDelta>().timeOffset - d.createFrame;
	
	const float invLifeTime = (1.0f / d.lifeTime);

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


	const SColor colBase = { d.color, d.color, d.color, 1.0f };
	const SColor col1 = colBase * std::clamp(lerp1, 0.0f, 1.0f);
	const SColor col2 = colBase * std::clamp(lerp2, 0.0f, 1.0f);

	if (d.drawSegmented) {

		const float3 difm = shadowPass ? camera->GetForward() : (d.midpos - camera->GetPos()).ANormalize();
		const float3 odirm = (difm.cross(d.middir)).ANormalize();

		const float lerpm = (1.0f - tm) * (0.7f + std::fabs(difm.dot(d.middir)));
		const float sizem = (0.2f + tm) * d.origSize;
		const float midtexx = mix(d.r.texture->xstart, d.r.texture->xend, 0.5f);

		const SColor colm = colBase * std::clamp(lerpm, 0.0f, 1.0f);

		AddEffectsQuad(
			{ d.pos1   - (odir1 * size1), d.r.texture->xstart, d.r.texture->ystart, col1  },
			{ d.midpos - (odirm * sizem), midtexx        , d.r.texture->ystart, colm },
			{ d.midpos + (odirm * sizem), midtexx        , d.r.texture->yend  , colm },
			{ d.pos1   + (odir1 * size1), d.r.texture->xstart, d.r.texture->yend  , col1  },
			animInfo, d.drawo.drawOrder
		);

		AddEffectsQuad(
			{ d.midpos - (odirm * sizem), midtexx      ,   d.r.texture->ystart, colm },
			{ d.pos2   - (odir2 * size2), d.r.texture->xend,   d.r.texture->ystart, col2 },
			{ d.pos2   + (odir2 * size2), d.r.texture->xend,   d.r.texture->yend  , col2 },
			{ d.midpos + (odirm * sizem), midtexx      ,   d.r.texture->yend  , colm },
			animInfo, d.drawo.drawOrder
		);
	} else {
		AddEffectsQuad(
			{ d.pos1 - (odir1 * size1), d.r.texture->xstart, d.r.texture->ystart, col1 },
			{ d.pos2 - (odir2 * size2), d.r.texture->xend  , d.r.texture->ystart, col2 },
			{ d.pos2 + (odir2 * size2), d.r.texture->xend  , d.r.texture->yend  , col2 },
			{ d.pos1 + (odir1 * size1), d.r.texture->xstart, d.r.texture->yend  , col1 },
			animInfo, d.drawo.drawOrder
		);
	}
	
	});
}

static void DrawBubbleProjectile(entt::registry& reg) 
{
	ZoneScopedN("XYZ::DrawSmokeTrailProjectile");
	
	reg.view<BubbleProjectile>().each([&] (auto ent, BubbleProjectile& p) {	
		if (!isVisible(p)) {
			return;
		}
		
		unsigned char col[4];
		col[0] = (unsigned char)(255 * p.alpha);
		col[1] = (unsigned char)(255 * p.alpha);
		col[2] = (unsigned char)(255 * p.alpha);
		col[3] = (unsigned char)(255 * p.alpha);
	
		const float interSize = p.size + p.sizeExpansion * globalRendering->timeOffset;
	
		auto* bt = projectileDrawer->bubbletex;
		float3 animInfo = { 1.0, 1.0, 0.0 };
		
		AddEffectsQuad(
			{ p.drawPos - camera->GetRight() * interSize - camera->GetUp() * interSize, bt->xstart, bt->ystart, col },
			{ p.drawPos + camera->GetRight() * interSize - camera->GetUp() * interSize, bt->xend,   bt->ystart, col },
			{ p.drawPos + camera->GetRight() * interSize + camera->GetUp() * interSize, bt->xend,   bt->yend,   col },
			{ p.drawPos - camera->GetRight() * interSize + camera->GetUp() * interSize, bt->xstart, bt->yend,   col },
			animInfo, p.drawo.drawOrder
		);		
	});
}

// TODO is PreDraw really needed? benchmark
void PreDrawSystem() {
	ZoneScopedN("Draw::Projectiles::PreDrawSystem");
	PreUpdateSimpleParticleSystem(projectileRegistry.view<SimpleParticle>());
	//PreUpdateVisibilitySystem<SimpleParticle>(projectileRegistry);
	PreUpdateVisibilitySystem<BitmapMuzzleFlame>(projectileRegistry);
	PreUpdateVisibilitySystem<DirtProjectile>(projectileRegistry);
	PreUpdateVisibilitySystem<ExploSpikeProjectile>(projectileRegistry);
	PreUpdateVisibilitySystem<MuzzleFlame>(projectileRegistry);	
	PreUpdateVisibilitySystem<HeatCloudProjectile>(projectileRegistry);
	PreUpdateVisibilitySystem<SmokeProjectile>(projectileRegistry);
	PreUpdateVisibilitySystem<SmokeTrail>(projectileRegistry);
	PreUpdateVisibilitySystem<BubbleProjectile>(projectileRegistry);
	
	/*
	UpdateAnimProgressSystem(projectileRegistry.view<AnimParams2>()); 
	*/
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
	ZoneScopedN("Draw::Projectiles::DrawSystem");
	DrawSimpleParticleSystem(
		projectileRegistry
	);
	DrawBitmapMuzzleFlameSystem(
		projectileRegistry
	);
	DrawDirtProjectileSystem(
		projectileRegistry
	);
	DrawHeatCloudProjectile(
		projectileRegistry
	);
	DrawSmokeProjectile(
		projectileRegistry
	);
	DrawSmokeTrailProjectile(
		projectileRegistry
	);
	
	DrawMuzzleFlame(
		projectileRegistry
	);
	
	DrawExploSpikeProjectile(
		projectileRegistry
	);
	
	DrawBubbleProjectile(
		projectileRegistry
	);
}

void DrawShadowSystem()
{
	ZoneScopedN("Draw::Projectiles::DrawShadowSystem");
//	projectileRegistry.view<const VisibleTag, const CastShadowTag>().each([&](auto ent) {
//		DispatchDrawingECS(ent, projectileRegistry);
//	});
	DrawSimpleParticleSystem(
		projectileRegistry
	);
	DrawBitmapMuzzleFlameSystem(
		projectileRegistry
	);
	DrawDirtProjectileSystem(
		projectileRegistry
	);
	DrawHeatCloudProjectile(
		projectileRegistry
	);
	DrawSmokeProjectile(
		projectileRegistry
	);
	DrawSmokeTrailProjectile(
		projectileRegistry
	);
	
	DrawMuzzleFlame(
		projectileRegistry
	);
	
	DrawExploSpikeProjectile(
		projectileRegistry
	);
	
	DrawBubbleProjectile(
		projectileRegistry
	);
}

void DrawMinimapSystem()
{
	ZoneScopedN("Draw::Projectiles::DrawMinimapSystem");
	projectileRegistry.view<SimpleParticle>().each([&](auto ent, auto& elem) {
		if (CanDrawProjectile(elem.pos, elem.allyteam, elem.useAirLos)) {
			CProjectile::AddMiniMapVertices({ elem.pos, color4::whiteA }, { elem.pos + elem.speed, color4::whiteA });
		}
	});
	projectileRegistry.view<BitmapMuzzleFlame>().each([&](auto ent, auto& elem) {
		if (CanDrawProjectile(elem.pos, elem.allyteam, elem.useAirLos)) {
			CProjectile::AddMiniMapVertices({ elem.pos, color4::whiteA }, { elem.pos, color4::whiteA });
		}
	});	
	projectileRegistry.view<DirtProjectile>().each([&](auto ent, auto& elem) {
		if (CanDrawProjectile(elem.pos, elem.allyteam, elem.useAirLos)) {
			CProjectile::AddMiniMapVertices({ elem.pos, color4::whiteA }, { elem.pos, color4::whiteA });
		}
	});	
	
	projectileRegistry.view<HeatCloudProjectile>().each([&](auto ent, auto& elem) {
		if (CanDrawProjectile(elem.pos, elem.allyteam, elem.useAirLos)) {
			CProjectile::AddMiniMapVertices({ elem.pos, color4::whiteA }, { elem.pos, color4::whiteA });
		}
	});	
	
	projectileRegistry.view<SmokeProjectile>().each([&](auto ent, auto& elem) {
		if (CanDrawProjectile(elem.pos, elem.allyteam, elem.useAirLos)) {
			CProjectile::AddMiniMapVertices({ elem.pos, color4::whiteA }, { elem.pos, color4::whiteA });
		}
	});
	
	projectileRegistry.view<SmokeTrail>().each([&](auto ent, auto& elem) {
		if (CanDrawProjectile(elem.pos, elem.allyteam, elem.useAirLos)) {
			CProjectile::AddMiniMapVertices({ elem.pos, color4::whiteA }, { elem.pos, color4::whiteA });
		}
	});	

	projectileRegistry.view<MuzzleFlame>().each([&](auto ent, auto& elem) {
		if (CanDrawProjectile(elem.pos, elem.allyteam, elem.useAirLos)) {
			CProjectile::AddMiniMapVertices({ elem.pos, color4::whiteA }, { elem.pos, color4::whiteA });
		}
	});		
	
	projectileRegistry.view<ExploSpikeProjectile>().each([&](auto ent, auto& elem) {
		if (CanDrawProjectile(elem.pos, elem.allyteam, elem.useAirLos)) {
			CProjectile::AddMiniMapVertices({ elem.pos, color4::whiteA }, { elem.pos, color4::whiteA });
		}
	});	
	
	projectileRegistry.view<BubbleProjectile>().each([&](auto ent, auto& elem) {
		if (CanDrawProjectile(elem.pos, elem.allyteam, elem.useAirLos)) {
			CProjectile::AddMiniMapVertices({ elem.pos, color4::whiteA }, { elem.pos, color4::whiteA });
		}
	});	
}
