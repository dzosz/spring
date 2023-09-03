/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "SimpleParticleSystem.h"

#include "Game/Camera.h"
#include "Game/GlobalUnsynced.h"
#include "Sim/Misc/GlobalSynced.h"
#include "Sim/Misc/TeamHandler.h"
#include "Sim/Misc/LosHandler.h"
#include "Sim/Units/Unit.h"
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


extern bool DRAW_REFLECTION;
extern bool DRAW_REFRACTION;
extern std::vector<std::pair<int, float>> enqueuedProjectilesDrawOrderData;

void AddEffectsQuad(const VA_TYPE_TC& tl, const VA_TYPE_TC& tr, const VA_TYPE_TC& br, const VA_TYPE_TC& bl,
					const float3& animParams, const float& animProgress)
{
	float minS = std::numeric_limits<float>::max()   ; float minT = std::numeric_limits<float>::max()   ;
	float maxS = std::numeric_limits<float>::lowest(); float maxT = std::numeric_limits<float>::lowest();
	std::invoke([&](auto&&... arg) {
		((minS = std::min(minS, arg.s)), ...);
		((minT = std::min(minT, arg.t)), ...);
		((maxS = std::max(maxS, arg.s)), ...);
		((maxT = std::max(maxT, arg.t)), ...);
	}, tl, tr, br, bl);

	
	auto& rb = CProjectile::GetPrimaryRenderBuffer();

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

class SoA
{
public:
	std::vector<float3> pos;
	std::vector<float3> speed;

	std::vector<float> rotVal;
	std::vector<float> rotVel;
	std::vector<float> rotParams; // rotParams.y; //rot accel

	std::vector<float> life;
	std::vector<float> decayrate;
	
	std::vector<float> size;
	std::vector<float> sizeGrowth;
	std::vector<float> sizeMod;
	
	std::vector<float3> gravity;
	std::vector<float> airdrag;
	
	std::vector<bool> visible;
	std::vector<int> allyTeam;
	
	std::vector<float> drawRadius;
	std::vector<int> drawOrder;
		
	void add(CSimpleParticleSystem& p, float3 offset) {
		// LOG("xyz add %i", pos.size());
		const float3 up = p.emitVector;
		const float3 right = up.cross(float3(up.y, up.z, -up.x));
		const float3 forward = up.cross(right);
		
		for (int i = 0 ; i < p.numParticles; ++i) {
			float az = guRNG.NextFloat() * math::TWOPI;
			float ay = (p.emitRot + (p.emitRotSpread * guRNG.NextFloat())) * math::DEG_TO_RAD;
	
			pos.push_back(offset);
			speed.push_back(((up * p.emitMul.y) * fastmath::cos(ay) - ((right * p.emitMul.x) * fastmath::cos(az) - (forward * p.emitMul.z) * fastmath::sin(az)) * fastmath::sin(ay)) * (p.particleSpeed + (guRNG.NextFloat() * p.particleSpeedSpread)));
			
			rotVal.push_back(p.rotParams.z);
			rotVel.push_back(p.rotParams.x); //initial rotation velocity
			rotParams.push_back(p.rotParams.y);
			
			life.push_back(0.0f);
			decayrate.push_back(1.0f / (p.particleLife + (guRNG.NextFloat() * p.particleLifeSpread)));
			
			size.push_back(p.particleSize + guRNG.NextFloat()*p.particleSizeSpread);
			sizeGrowth.push_back(p.sizeGrowth);
			sizeMod.push_back(p.sizeMod);
			
			gravity.push_back(p.gravity);
			airdrag.push_back(p.airdrag);
			
			visible.push_back(true);
			allyTeam.push_back(p.allyteamID);
			
			// draw
			
			colorMap.emplace_back(p.colorMap);
			color.emplace_back();
			
			interPos.emplace_back();
			bounds.emplace_back();
			texture.emplace_back(p.texture);
			
			anims.emplace_back(p.animParams);
			aprogress.emplace_back(p.animProgress);
			createFrame.emplace_back(p.createFrame);
			
			drawRadius.emplace_back(p.drawRadius);
			drawOrder.emplace_back(p.drawOrder);			
		}

	}
	
	void update() {		
		for (int i =0; i < pos.size(); ++i) {
			pos[i]    += speed[i];
			speed[i]  += gravity[i];
			speed[i]  *= airdrag[i];
		}
		for (int i =0; i < pos.size(); ++i) {
			rotVal[i] += rotVel[i];
			rotVel[i] += rotParams[i];
		}
		for (int i =0; i < pos.size(); ++i) {
			life[i] += decayrate[i];
		}
		
		check_dead();
		
		for (int i =0; i < pos.size(); ++i) {
			size[i] *= sizeMod[i];
			size[i] += sizeGrowth[i];
		}		
	}
	
	void check_dead() {
		for (int i =0; i < pos.size();) {
			if (unlikely(life[i] >= 1.0)) {
				erase(i);		
			} else 
			{
				++i;
			}			
		}
	}
	
	void erase(int idx) {
		remove_from_container(idx, pos);
		remove_from_container(idx, speed);
		
		remove_from_container(idx, rotVal);
		remove_from_container(idx, rotVel);		
		remove_from_container(idx, rotParams);
		
		remove_from_container(idx, life);
		remove_from_container(idx, decayrate);
		
		remove_from_container(idx, size);
		remove_from_container(idx, sizeGrowth);
		remove_from_container(idx, sizeMod);
		
		remove_from_container(idx, gravity);
		remove_from_container(idx, airdrag);
		
		remove_from_container(idx, visible);
		remove_from_container(idx, allyTeam);
		
		// draw
		remove_from_container(idx, colorMap);
		remove_from_container(idx, color);
		
		remove_from_container(idx, interPos);		
		remove_from_container(idx, bounds);
		remove_from_container(idx, texture);		
		
		remove_from_container(idx, anims);
		remove_from_container(idx, aprogress);
		remove_from_container(idx, createFrame);
		
		remove_from_container(idx, drawRadius);
		remove_from_container(idx, drawOrder);
		
	}
	
	template <typename T>
	void remove_from_container(int idx, T&& cont) {
		cont[idx] = cont.back();
		cont.pop_back();
	}
	
	
	std::vector<CColorMap*> colorMap;
	std::vector<std::array<unsigned char, 4>> color;
	std::vector<float3> interPos;
	
	std::vector<std::array<float3, 4>> bounds;
	std::vector<AtlasedTexture*> texture;
	
	std::vector<float3> anims;
	std::vector<float> aprogress;
	std::vector<int> createFrame;
	
	void draw() {
		bool drawReflection = DRAW_REFLECTION;
		bool drawRefraction = DRAW_REFRACTION;
		updateAnimParams();
		
		for (int i =0; i < pos.size(); ++i) {
			colorMap[i]->GetColor(color[i].data(), life[i]);
		}
		
		float timeOffset = globalRendering->timeOffset;
		for (int i =0; i < pos.size(); ++i) {
			interPos[i] = pos[i] + speed[i] * timeOffset;
		}
		
		// visibility
		auto spectatingFullView = gu->spectatingFullView;
		auto myAllyTeam = gu->myAllyTeam;
		auto& th = teamHandler;
		auto& lh = losHandler;
		
		for (int i =0; i < pos.size(); ++i) {
			visible[i] = 	
				 (spectatingFullView || (th.IsValidAllyTeam(allyTeam[i]) && 
				  th.Ally(allyTeam[i], myAllyTeam) ||
				lh->InLos(pos[i], myAllyTeam)) || 
				  lh->InAirLos(pos[i], myAllyTeam));			
		}
		
		if (DRAW_REFRACTION) {
			for (int i =0; i < pos.size(); ++i) {
				visible[i] = visible[i] && interPos[i].y <= drawRadius[i];
			}
		}
		auto* cam = camera;
		for (int i =0; i < pos.size(); ++i) {
			visible[i] = visible[i] && cam->InView(interPos[i], drawRadius[i]);
		}
		
		// bounds
		/*
		for (int i =0; i < pos.size(); ++i) {
			const float3 cameraRight = cam->GetRight() * size[i];
			const float3 cameraUp    = cam->GetUp()    * size[i];
			
			bounds[i] = {
				-cameraRight - cameraUp,
				 cameraRight - cameraUp,
				 cameraRight + cameraUp,
				-cameraRight + cameraUp
			};
		}*/
		
		// streflop alternative
		const static auto safeANormalize = [&](const auto& ydir, const auto& yDirLen) {	
			if (likely(yDirLen > float3::nrm_eps()))
				return ydir * fastmath::isqrt_sse(yDirLen);
			return ydir;
		};
	
		auto cpos = cam->GetPos();
		for (int i =0; i < pos.size(); ++i) {
			if (!visible[i]) {
				continue;
			}
			const float3 zdir = safeANormalize(pos[i] - cpos, (pos[i] - cpos).SqLength());
			float3 ydir = zdir.cross(speed[i]);
			float yDirLen2 = ydir.SqLength();
			ydir = safeANormalize(ydir, yDirLen2);
			// ydir.SafeANormalize();
			const float3 xdir = ydir.cross(zdir);

			if (yDirLen2 > 0.001f)
			{
				bounds[i] = {
					-ydir * size[i] - xdir * size[i],
					-ydir * size[i] + xdir * size[i],
					 ydir * size[i] + xdir * size[i],
					 ydir * size[i] - xdir * size[i]
				};
			} else {
				const float3 cameraRight = camera->GetRight() * size[i];
				const float3 cameraUp    = camera->GetUp()    * size[i];				
				bounds[i] = {
					-cameraRight - cameraUp,
					 cameraRight - cameraUp,
					 cameraRight + cameraUp,
					-cameraRight + cameraUp
				};
			}
		}		

		// TODO branchless?
		auto fwd = camera->GetForward();
		for (int i =0; i < pos.size(); ++i) {
			if (!visible[i]) {
				continue;
			}
			if (math::fabs(rotVal[i]) > 0.01f) {
				for (auto& b : bounds[i]) {
					// faster fastmath than float3.rotate which uses strlflop
					const float ca = fastmath::cos(rotVal[i]);
					const float sa = fastmath::sin(rotVal[i]);				
					b = b * ca + fwd.cross(b) * sa + fwd * fwd.dot(b) * (1.0f - ca);
				}
			}
		}
		
		for (int i =0; i < pos.size(); ++i) {
			if (!visible[i]) {
				continue;
			}
			AddEffectsQuad(
				{ interPos[i] + bounds[i][0], texture[i]->xstart, texture[i]->ystart, color[i].data() },
				{ interPos[i] + bounds[i][1], texture[i]->xend,   texture[i]->ystart, color[i].data() },
				{ interPos[i] + bounds[i][2], texture[i]->xend,   texture[i]->yend,   color[i].data() },
				{ interPos[i] + bounds[i][3], texture[i]->xstart, texture[i]->yend,   color[i].data() },
				anims[i], aprogress[i]
			);
			enqueuedProjectilesDrawOrderData.push_back(std::pair{drawOrder[i], -cam->ProjectedDistance(pos[i])});
		}	
	}
	
	
	void updateAnimParams() {
		int gameFrame = gs->frameNum;
		float timeOffset = globalRendering->timeOffset;
		
		for (int i =0; i < pos.size(); ++i)
		{
			auto& animParams = anims[i];
			auto& animProgress = aprogress[i];
			if (static_cast<int>(animParams.x) <= 1 && static_cast<int>(animParams.y) <= 1) {
				animProgress = 0.0f;
				continue;
			}
		
			const float t = (gameFrame + timeOffset - createFrame[i]);
			const float animSpeed = std::fabs(animParams.z);
			
			if (animParams.z < 0.0f) {
				animProgress = 1.0f - std::fabs(std::fmod(t, 2.0f * animSpeed) / animSpeed - 1.0f);
			}
			else {
				animProgress = std::fmod(t, animSpeed) / animSpeed;
			}
		}
	}

};

static SoA SOA;


CR_BIND_DERIVED(CSimpleParticleSystem, CProjectile, )

CR_REG_METADATA(CSimpleParticleSystem,
(
	CR_MEMBER_BEGINFLAG(CM_Config),
		CR_MEMBER(emitVector),
		CR_MEMBER(emitMul),
		CR_MEMBER(gravity),
		CR_MEMBER(colorMap),
		CR_IGNORED(texture),
		CR_MEMBER(airdrag),
		CR_MEMBER(particleLife),
		CR_MEMBER(particleLifeSpread),
		CR_MEMBER(numParticles),
		CR_MEMBER(particleSpeed),
		CR_MEMBER(particleSpeedSpread),
		CR_MEMBER(particleSize),
		CR_MEMBER(particleSizeSpread),
		CR_MEMBER(emitRot),
		CR_MEMBER(emitRotSpread),
		CR_MEMBER(directional),
		CR_MEMBER(sizeGrowth),
		CR_MEMBER(sizeMod),
	CR_MEMBER_ENDFLAG(CM_Config),
	CR_MEMBER(particles),
	CR_SERIALIZER(Serialize)
))

CR_BIND(CSimpleParticleSystem::Particle, )

CR_REG_METADATA_SUB(CSimpleParticleSystem, Particle,
(
	CR_MEMBER(pos),
	CR_MEMBER(speed),
	CR_MEMBER(life),
	CR_MEMBER(rotVal),
	CR_MEMBER(rotVel),
	CR_MEMBER(decayrate),
	CR_MEMBER(size),
	CR_MEMBER(sizeGrowth),
	CR_MEMBER(sizeMod)
))

CSimpleParticleSystem::CSimpleParticleSystem()
	: CProjectile()
	, emitVector(ZeroVector)
	, emitMul(1.0f, 1.0f, 1.0f)
	, gravity(ZeroVector)
	, particleSpeed(0.0f)
	, particleSpeedSpread(0.0f)
	, emitRot(0.0f)
	, emitRotSpread(0.0f)
	, texture(nullptr)
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
	checkCol = false;
	useAirLos = true;
}

void CSimpleParticleSystem::Serialize(creg::ISerializer* s)
{
	std::string name;
	if (s->IsWriting())
		name = projectileDrawer->textureAtlas->GetTextureName(texture);
	creg::GetType(name)->Serialize(s, &name);
	if (!s->IsWriting())
		texture = projectileDrawer->textureAtlas->GetTexturePtr(name);
}

void CSimpleParticleSystem::Draw()
{
	UpdateAnimParams();

	float3 zdir;
	float3 ydir;
	float3 xdir;

	const auto DoParticleDraw = [this](const float3& xdir, const float3& ydir, const float3& zdir, const Particle* p) {
		const float3 pDrawPos = p->pos + p->speed * globalRendering->timeOffset;
		const float size = p->size;

		unsigned char color[4];
		colorMap->GetColor(color, p->life);

		std::array<float3, 4> bounds = {
			-ydir * size - xdir * size,
			-ydir * size + xdir * size,
			 ydir * size + xdir * size,
			 ydir * size - xdir * size
		};

		if (math::fabs(p->rotVal) > 0.01f) {
			float3::rotate<false>(p->rotVal, zdir, bounds);
		}
		AddEffectsQuad(
			{ pDrawPos + bounds[0], texture->xstart, texture->ystart, color },
			{ pDrawPos + bounds[1], texture->xend,   texture->ystart, color },
			{ pDrawPos + bounds[2], texture->xend,   texture->yend,   color },
			{ pDrawPos + bounds[3], texture->xstart, texture->yend,   color }
		);
	};

	const bool shadowPass = (camera->GetCamType() == CCamera::CAMTYPE_SHADOW);
	if (directional && !shadowPass) {
		for (int i = 0; i < numParticles; i++) {
			const Particle* p = &particles[i];

			if (p->life >= 1.0f)
				continue;

			zdir = (p->pos - camera->GetPos()).SafeANormalize();
			ydir = zdir.cross(p->speed);
			if likely(ydir.SqLength() > 0.001f) {
				ydir.SafeANormalize();
				xdir = ydir.cross(zdir);
			}
			else {
				zdir = camera->GetForward();
				xdir = camera->GetRight();
				ydir = camera->GetUp();
			}

			DoParticleDraw(xdir, ydir, zdir, p);
		}
		return;
	}

	// !directional
	for (int i = 0; i < numParticles; i++) {
		const Particle* p = &particles[i];

		if (p->life >= 1.0f)
			continue;

		zdir = camera->GetForward();
		xdir = camera->GetRight();
		ydir = camera->GetUp();
		
		DoParticleDraw(xdir, ydir, zdir, p);
	}
}

void CSimpleParticleSystem::Update()
{
	deleteMe = true;
	for (auto& p: particles) {
		if (p.life < 1.0f) {
			p.pos    += p.speed;
			p.speed  += gravity;
			p.speed  *= airdrag;
			p.rotVal += p.rotVel;
			p.rotVel += rotParams.y; //rot accel
			p.life   += p.decayrate;
			p.size    = p.size * sizeMod + sizeGrowth;

			deleteMe = false;
		}
	}
}

void CSimpleParticleSystem::Init(const CUnit* owner, const float3& offset)
{	
	CProjectile::Init(owner, offset);
	
	const float3 up = emitVector;
	const float3 right = up.cross(float3(up.y, up.z, -up.x));
	const float3 forward = up.cross(right);

	// FIXME: should catch these earlier and for more projectile-types
	if (colorMap == nullptr) {
		colorMap = CColorMap::LoadFromFloatVector(std::vector<float>(8, 1.0f));
		LOG_L(L_WARNING, "[CSimpleParticleSystem::%s] no color-map specified", __func__);
	}
	if (texture == nullptr) {
		texture = &projectileDrawer->textureAtlas->GetTexture("simpleparticle");
		LOG_L(L_WARNING, "[CSimpleParticleSystem::%s] no texture specified", __func__);
	}

	particles.resize(numParticles);
	for (auto& p: particles) {
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

	drawRadius = (particleSpeed + particleSpeedSpread) * (particleLife * particleLifeSpread);
}

int CSimpleParticleSystem::GetProjectilesCount() const
{
	return numParticles;
}

void CSphereParticleSpawner::Draw()
{
	ZoneScopedN("SPS::Draw");	
	SOA.draw();
}

void CSphereParticleSpawner::Update()
{
	ZoneScopedN("SPS::Update");	
	SOA.update();
}

int CSphereParticleSpawner::GetProjectilesCount() const
{
	return SOA.pos.size();
}


bool CSimpleParticleSystem::GetMemberInfo(SExpGenSpawnableMemberInfo& memberInfo)
{
	if (CProjectile::GetMemberInfo(memberInfo))
		return true;

	CHECK_MEMBER_INFO_FLOAT3(CSimpleParticleSystem, emitVector         )
	CHECK_MEMBER_INFO_FLOAT3(CSimpleParticleSystem, emitMul            )
	CHECK_MEMBER_INFO_FLOAT3(CSimpleParticleSystem, gravity            )
	CHECK_MEMBER_INFO_FLOAT (CSimpleParticleSystem, particleSpeed      )
	CHECK_MEMBER_INFO_FLOAT (CSimpleParticleSystem, particleSpeedSpread)
	CHECK_MEMBER_INFO_FLOAT (CSimpleParticleSystem, emitRot            )
	CHECK_MEMBER_INFO_FLOAT (CSimpleParticleSystem, emitRotSpread      )
	CHECK_MEMBER_INFO_FLOAT (CSimpleParticleSystem, particleLife       )
	CHECK_MEMBER_INFO_FLOAT (CSimpleParticleSystem, particleLifeSpread )
	CHECK_MEMBER_INFO_FLOAT (CSimpleParticleSystem, particleSize       )
	CHECK_MEMBER_INFO_FLOAT (CSimpleParticleSystem, particleSizeSpread )
	CHECK_MEMBER_INFO_FLOAT (CSimpleParticleSystem, airdrag            )
	CHECK_MEMBER_INFO_FLOAT (CSimpleParticleSystem, sizeGrowth         )
	CHECK_MEMBER_INFO_FLOAT (CSimpleParticleSystem, sizeMod            )
	CHECK_MEMBER_INFO_INT   (CSimpleParticleSystem, numParticles       )
	CHECK_MEMBER_INFO_BOOL  (CSimpleParticleSystem, directional        )
	CHECK_MEMBER_INFO_PTR   (CSimpleParticleSystem, texture , projectileDrawer->textureAtlas->GetTexturePtr)
	CHECK_MEMBER_INFO_PTR   (CSimpleParticleSystem, colorMap, CColorMap::LoadFromDefString                 )

	return false;
}


CR_BIND_DERIVED(CSphereParticleSpawner, CSimpleParticleSystem, )

CR_REG_METADATA(CSphereParticleSpawner, )

void CSphereParticleSpawner::Init(const CUnit* owner, const float3& offset)
{
	static bool initialized = false;
	if (!initialized)
	{
		CProjectile::Init(owner, offset);
	}
	else 
	{
		if (owner != nullptr) {
			ownerID = owner->id;
			teamID = owner->team;
			allyteamID =  teamHandler.IsValidTeam(teamID)? teamHandler.AllyTeam(teamID): -1;
		}
		createFrame = gs->frameNum;		
		drawRadius = (particleSpeed + particleSpeedSpread) * (particleLife * particleLifeSpread);
		
		SetPosition(pos + offset);
		SetVelocityAndSpeed(speed);
		rotParams *= float3(math::DEG_TO_RAD / GAME_SPEED, math::DEG_TO_RAD / (GAME_SPEED * GAME_SPEED), math::DEG_TO_RAD);
		UpdateRotation();
	}
	
	// FIXME: should catch these earlier and for more projectile-types
	if (colorMap == nullptr) {
		colorMap = CColorMap::LoadFromFloatVector(std::vector<float>(8, 1.0f));
		LOG_L(L_WARNING, "[CSphereParticleSpawner::%s] no color-map specified", __FUNCTION__);
	}
	if (texture == nullptr) {
		texture = &projectileDrawer->textureAtlas->GetTexture("sphereparticle");
		LOG_L(L_WARNING, "[CSphereParticleSpawner::%s] no texture specified", __FUNCTION__);
	}
	
	initialized = true;	
	
	SOA.add(*this, offset);

	// ensure this object is always visible
	speed = float4{};
	SetPosition(camera->GetPos());
	SetVelocityAndSpeed({});
	allyteamID = gu->myAllyTeam;
	
	drawRadius = 9999999;
	alwaysVisible = true;
}

bool CSphereParticleSpawner::GetMemberInfo(SExpGenSpawnableMemberInfo& memberInfo)
{
	return CSimpleParticleSystem::GetMemberInfo(memberInfo);
}
>>>>>>> ccf483f088 (rework CSphereParticleSpawner to be cache friendly spawner of CSimpleParticleSystem)
