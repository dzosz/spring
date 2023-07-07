/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "SimpleParticleSystem.h"

#include "NewSimpleParticleSystem.h"

#include "GenericParticleProjectile.h"
#include "Game/Camera.h"
#include "Game/GlobalUnsynced.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/Env/Particles/ProjectileDrawer.h"
#include "Rendering/GL/RenderBuffers.h"
#include "Rendering/Textures/ColorMap.h"
#include "Sim/Projectiles/ExpGenSpawnableMemberInfo.h"
#include "Sim/Projectiles/ProjectileMemPool.h"
#include "System/creg/DefTypes.h"
#include "System/float3.h"
#include "System/Log/ILog.h"
#include "System/SpringMath.h"

#include "Game/GlobalUnsynced.h"
#include "Sim/Misc/GlobalSynced.h"

#include "Sim/Units/Unit.h"
#include "Sim/Units/UnitHandler.h"

#include "Sim/Misc/TeamHandler.h"
#include "Rendering/Colors.h"

// NewSimpleParticleSystem::texture = nullptr; 

NewSimpleParticleSystem::NewSimpleParticleSystem()
	: emitVector(ZeroVector)
	, emitMul(1.0f, 1.0f, 1.0f)
	, gravity(ZeroVector)
	, particleSpeed(0.0f)
	, particleSpeedSpread(0.0f)
	, emitRot(0.0f)
	, emitRotSpread(0.0f)
	, colorMap(nullptr)
	, directional(false)
	, particleLife(0.0f)
	, particleLifeSpread(0.0f)
	, particleSize(0.0f)
	, particleSizeSpread(0.0f)
	, airdrag(0.0f)
	, sizeGrowth(0.0f)
	, sizeMod(0.0f)
	, numParticles(0)
{
	// checkCol = false;
	useAirLos = true;
}

TypedRenderBuffer<VA_TYPE_PROJ>& GetPrimaryRenderBuffer()
{
	return RenderBuffer::GetTypedRenderBuffer<VA_TYPE_PROJ>();
}
	

void NewSimpleParticleSystem::Draw()
{
	// UpdateAnimParams();
};

void NewSimpleParticleSystem::UpdateAnimParams() {
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

void NewSimpleParticleSystem::DrawParticle(const NewSimpleParticle* p)
{
	// not used in this class
	UpdateAnimParams();

	std::array<float3, 4> bounds;
	const bool shadowPass = (camera->GetCamType() == CCamera::CAMTYPE_SHADOW);
	if (directional && !shadowPass) {
			const float3 zdir = (p->pos - camera->GetPos()).SafeANormalize();
			      float3 ydir = zdir.cross(p->speed); float yDirLen2 = ydir.SqLength(); ydir.SafeANormalize();
			const float3 xdir = ydir.cross(zdir);

			const float3 interPos = p->pos + p->speed * globalRendering->timeOffset;
			const float size = p->size;

			unsigned char color[4];
			colorMap->GetColor(color, p->life);

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
				const float3 cameraRight = camera->GetRight() * p->size;
				const float3 cameraUp    = camera->GetUp()    * p->size;
				fwdDir = &camera->GetForward();

				bounds = {
					-cameraRight - cameraUp,
					 cameraRight - cameraUp,
					 cameraRight + cameraUp,
					-cameraRight + cameraUp
				};
			}


			if (math::fabs(p->rotVal) > 0.01f) {
				for (auto& b : bounds)
					b = b.rotate(p->rotVal, *fwdDir);
			}
			AddEffectsQuad(
				{ interPos + bounds[0], texture->xstart, texture->ystart, color },
				{ interPos + bounds[1], texture->xend,   texture->ystart, color },
				{ interPos + bounds[2], texture->xend,   texture->yend,   color },
				{ interPos + bounds[3], texture->xstart, texture->yend,   color }
			);
			return;
	}

	// !directional
	//for (int i = 0; i < numParticles; i++) {
	//	const Particle* p = &particles[i];

		unsigned char color[4];
		colorMap->GetColor(color, p->life);

		const float3 interPos = p->pos + p->speed * globalRendering->timeOffset;
		const float3 cameraRight = camera->GetRight() * p->size;
		const float3 cameraUp    = camera->GetUp()    * p->size;

		bounds = {
			-cameraRight - cameraUp,
			 cameraRight - cameraUp,
			 cameraRight + cameraUp,
			-cameraRight + cameraUp
		};

		if (math::fabs(p->rotVal) > 0.01f) {
			for (auto& b : bounds)
				b = b.rotate(p->rotVal, camera->GetForward());
		}
		AddEffectsQuad(
			{ interPos + bounds[0], texture->xstart, texture->ystart, color },
			{ interPos + bounds[1], texture->xend,   texture->ystart, color },
			{ interPos + bounds[2], texture->xend,   texture->yend,   color },
			{ interPos + bounds[3], texture->xstart, texture->yend,   color }
		);
}

bool NewSimpleParticleSystem::Update(NewSimpleParticle& p)
{
	//deleteMe = true;

	//for (auto& p: particles) {
		if (p.life < 1.0f)
		{
			p.pos    += p.speed;
			p.speed  += gravity;
			p.speed  *= airdrag;
			p.rotVal += p.rotVel;
			p.rotVel += rotParams.y; //rot accel
			p.life   += p.decayrate;
			p.size    = p.size * sizeMod + sizeGrowth;
			return true;
		} else
		{
			//p.deleteMe = true;
			return false;
		}
		return p.life < 1.0f;
	//}
}

void NewSimpleParticleSystem::Init(const CUnit* owner, const float3& offset)
{
	if (owner != nullptr) {
		// must be set before the AddProjectile call
		//ownerID = owner->id;
		auto teamID = owner->team;
		allyteamID =  teamHandler.IsValidTeam(teamID)? teamHandler.AllyTeam(teamID): -1;
	}
	// if (!hitscan)
	{
		SetPosition(pos + offset);
		SetVelocityAndSpeed(speed);
	}
	
	
	createFrame = gs->frameNum;
	rotParams *= float3(math::DEG_TO_RAD / GAME_SPEED, math::DEG_TO_RAD / (GAME_SPEED * GAME_SPEED), math::DEG_TO_RAD);
	
	UpdateRotation();
	// CProjectile::Init(owner, offset);

	 up = emitVector;
	 right = up.cross(float3(up.y, up.z, -up.x));
	 forward = up.cross(right);

	// FIXME: should catch these earlier and for more projectile-types
	if (colorMap == nullptr) {
		colorMap = CColorMap::LoadFromFloatVector(std::vector<float>(8, 1.0f));
		LOG_L(L_WARNING, "[NewSimpleParticleSystem::%s] no color-map specified", __FUNCTION__);
	}
	if (texture == nullptr) {
		texture = &projectileDrawer->textureAtlas->GetTexture("simpleparticle");
		LOG_L(L_WARNING, "[NewSimpleParticleSystem::%s] no texture specified", __FUNCTION__);
	}

	drawRadius = (particleSpeed + particleSpeedSpread) * (particleLife * particleLifeSpread);
}

void NewSimpleParticleSystem::UpdateRotation()
{
	const float t = (gs->frameNum - createFrame + globalRendering->timeOffset);
	// rotParams.y is acceleration in angle per frame^2
	rotVel = rotParams.x + rotParams.y * t;
	rotVal = rotParams.z + rotVel      * t;
}

void NewSimpleParticleSystem::InitParticle(NewSimpleParticle& p, const float3& offset)
{
	float az = guRNG.NextFloat() * math::TWOPI;
	float ay = (emitRot + (emitRotSpread * guRNG.NextFloat())) * math::DEG_TO_RAD;

	p.pos = offset;
	p.speed = ((up * emitMul.y) * fastmath::cos(ay) - ((right * emitMul.x) * fastmath::cos(az) - (forward * emitMul.z) * fastmath::sin(az)) * fastmath::sin(ay)) * (particleSpeed + (guRNG.NextFloat() * particleSpeedSpread));
	p.rotVal = rotParams.z; //initial rotation value
	p.rotVel = rotParams.x; //initial rotation velocity
	p.life = 0.0f;
	p.decayrate = 1.0f / (particleLife + (guRNG.NextFloat() * particleLifeSpread));
	p.size = particleSize + guRNG.NextFloat()*particleSizeSpread;
}

void NewSimpleParticleSystem::AddEffectsQuad(const VA_TYPE_TC& tl, const VA_TYPE_TC& tr, const VA_TYPE_TC& br, const VA_TYPE_TC& bl) const
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

void NewSimpleParticleSystem::DrawOnMinimap() const
{
    AddMiniMapVertices({ pos, color4::whiteA }, { pos + speed, color4::whiteA });
}

void NewSimpleParticleSystem::AddMiniMapVertices(VA_TYPE_C&& v1, VA_TYPE_C&& v2) const
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
