#include "ECS.h"

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

static void DrawSimpleParticleSystem(const Position& pos, const Speed& speed, const Sized& sized,
				  const Lifetime& l, const RenderData& data, const Rotation& rot,
				  const AnimParams& animParams, const AnimProgress& animProgress)
{
	//UpdateAnimParams(); // already done in update phase?

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

static void UpdateDrawPos()
{ 
	const float t = registry.ctx().get<PhysDelta>().timeSinceLastFrame;
	registry.view<const Position, const Speed, DrawPosition>().each([&](
		auto ent, const Position& pos, const Speed& speed, DrawPosition& drawPos) {
		drawPos.value = (speed.value.w != 0.0f) ? (pos.value + speed.value * t) : pos.value;
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

void DrawSystem() {
	UpdateDrawPos();
	DrawClass(SimpleParticleSystemTag{});
};
