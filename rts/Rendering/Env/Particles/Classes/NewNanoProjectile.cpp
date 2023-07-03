
#include "NewNanoProjectile.h"
#include "NanoProjectile.h"

#include "Game/Camera.h"
#include "Rendering/GL/RenderBuffers.h"
#include "Rendering/Env/Particles/ProjectileDrawer.h"
#include "Rendering/Textures/TextureAtlas.h"
#include "Rendering/Colors.h"
#include "Rendering/GlobalRendering.h"
#include "Game/GlobalUnsynced.h"
#include "Sim/Misc/GlobalSynced.h"


NewNanoProjectile::NewNanoProjectile(float3 pos, float3 speed, int lifeTime, SColor c)
	//: CProjectile(pos, speed, nullptr, false, false, false)
	: deathFrame(gs->frameNum + lifeTime)
	, color(c),
      pos(pos),
      speed(speed),
      createFrame(gs->frameNum)
{
	rotVal0x = rotValRng0 * (guRNG.NextFloat() * 2.0 - 1.0);
	rotVel0x = rotVelRng0 * (guRNG.NextFloat() * 2.0 - 1.0);
	rotAcc0x = rotAccRng0 * (guRNG.NextFloat() * 2.0 - 1.0);

	rotVal = rotVal0 + rotVal0x;
	rotVel = rotVel0 + rotVel0x;
	rotAcc = rotAcc0 + rotAcc0x;
}

void NewNanoProjectile::Update()
{
	pos += speed;
	deleteMe |= (gs->frameNum >= deathFrame);
}

void NewNanoProjectile::Draw()
{
	{
		const float t = (gs->frameNum - createFrame + globalRendering->timeOffset);
		// rotParams.y is acceleration in angle per frame^2
		rotVel = CNanoProjectile::rotVel0 + rotAcc * t;
		rotVal = CNanoProjectile::rotVal0 + rotVel * t;
	}

    auto cam = camera;
	const float3 ri = cam->GetRight() * drawRadius;
	const float3 up = cam->GetUp() * drawRadius;
	std::array<float3, 4> bounds = {
		-ri - up,
		 ri - up,
		 ri + up,
		-ri + up
	};

	if (math::fabs(rotVal) > 0.01f) {
		for (auto& b : bounds)
			b = b.rotate(rotVal, cam->GetForward());
	}

	const auto* gfxt = projectileDrawer->gfxtex;
	AddEffectsQuad(
		{ drawPos + bounds[0], gfxt->xstart, gfxt->ystart, color },
		{ drawPos + bounds[1], gfxt->xend  , gfxt->ystart, color },
		{ drawPos + bounds[2], gfxt->xend  , gfxt->yend  , color },
		{ drawPos + bounds[3], gfxt->xstart, gfxt->yend  , color }
	);
}

void NewNanoProjectile::DrawOnMinimap() const
{
	// TODO
    AddMiniMapVertices({ pos        , color4::green }, { pos + speed, color4::green });
}

void NewNanoProjectile::AddMiniMapVertices(VA_TYPE_C&& v1, VA_TYPE_C&& v2) const
{
    
    
	if (v1.pos.equals(v2.pos)) {
        auto& mmPtsRB = CProjectile::GetMiniMapPointsRB();
		mmPtsRB.AddVertex(std::move(v1));
	}
	else {
        auto& mmLnsRB = CProjectile::GetMiniMapLinesRB();
		mmLnsRB.AddVertex(std::move(v1));
		mmLnsRB.AddVertex(std::move(v2));
	}
}

TypedRenderBuffer<VA_TYPE_PROJ>& NewNanoProjectile::GetPrimaryRenderBuffer() const
{
	return RenderBuffer::GetTypedRenderBuffer<VA_TYPE_PROJ>();
}


void NewNanoProjectile::AddEffectsQuad(const VA_TYPE_TC& tl, const VA_TYPE_TC& tr, const VA_TYPE_TC& br, const VA_TYPE_TC& bl) const
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
	const auto animInfo = float3{ animParams.x, animParams.y, animProgress };
	constexpr float layer = 0.0f; //for future texture arrays

	//pos, uvw, uvmm, col
	rb.AddQuadTriangles(
		{ tl.pos, float3{ tl.s, tl.t, layer }, uvInfo, animInfo, tl.c },
		{ tr.pos, float3{ tr.s, tr.t, layer }, uvInfo, animInfo, tr.c },
		{ br.pos, float3{ br.s, br.t, layer }, uvInfo, animInfo, br.c },
		{ bl.pos, float3{ bl.s, bl.t, layer }, uvInfo, animInfo, bl.c }
	);
}

/*
void CExpGenSpawnable::UpdateAnimParams()
{
	if (static_cast<int>(animParams.x) <= 1 && static_cast<int>(animParams.y) <= 1) {
		animProgress = 0.0f;
		return;
	}

	const float t = (gs->frameNum - createFrame + globalRendering->timeOffset);
	const float animSpeed = math::fabs(animParams.z);
	if (animParams.z < 0.0f) {
		#if 0
			animProgress = math::fmod(t, 2.0f * animSpeed) / animSpeed;
			if (animProgress > 1.0)
				animProgress = 2.0f - animProgress;
		#else
			animProgress = 1.0f - math::fabs(math::fmod(t, 2.0f * animSpeed) / animSpeed - 1.0f);
		#endif
	}
	else {
		animProgress = math::fmod(t, animSpeed) / animSpeed;
	}
}
*/

