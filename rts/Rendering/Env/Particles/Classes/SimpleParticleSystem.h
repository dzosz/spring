/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef SIMPLE_PARTICLE_SYSTEM_H
#define SIMPLE_PARTICLE_SYSTEM_H

#include "Sim/Projectiles/Projectile.h"
#include "Rendering/Textures/TextureAtlas.h"
#include "System/float3.h"

class CUnit;
class CColorMap;
class CProjectileHandler;

class CSimpleParticleSystem : public CProjectile
{
	CR_DECLARE_DERIVED(CSimpleParticleSystem)
	CR_DECLARE_SUB(Particle)

public:
	friend class CProjectileHandler;
	friend class CSimpleParticleSystemSoA;
	friend class CCustomExplosionGenerator;
	CSimpleParticleSystem();
	virtual ~CSimpleParticleSystem() { particles.clear(); }

	void Serialize(creg::ISerializer* s);

	void Draw() override;
	void Update() override;
	void Init(const CUnit* owner, const float3& offset) override;

	int GetProjectilesCount() const override;

	static bool GetMemberInfo(SExpGenSpawnableMemberInfo& memberInfo);

protected:
	float3 emitVector;
	float3 emitMul;
	float3 gravity;
	float particleSpeed;
	float particleSpeedSpread;

	float emitRot;
	float emitRotSpread;

	AtlasedTexture* texture;
	CColorMap* colorMap;
	bool directional;

	float particleLife;
	float particleLifeSpread;
	float particleSize;
	float particleSizeSpread;
	float airdrag;
	float sizeGrowth;
	float sizeMod;

	int numParticles;

	struct Particle
	{
		CR_DECLARE_STRUCT(Particle)

		float3 pos;
		float3 speed;

		float rotVal;
		float rotVel;

		float life;
		float decayrate;
		float size;
		float sizeGrowth;
		float sizeMod;
	};

protected:
	 std::vector<Particle> particles;
};

/**
* old CSphereParticleSpawner (it used to spawns the particles as independant CProjectile objects)
* has proven to be slower
*/
class CSphereParticleSpawner : public CSimpleParticleSystem {
	CR_DECLARE_DERIVED(CSphereParticleSpawner)
public:
	CSphereParticleSpawner();

	void Draw() override;
	void Update() override;
	int GetProjectilesCount() const override;
	void Init(const CUnit* owner, const float3& offset) override;

	static bool GetMemberInfo(SExpGenSpawnableMemberInfo& memberInfo);
private:
	void Clear();
	void GenerateParticles(const float3& pos);
};

class CSimpleParticleSystemSoA
{
public:
	void Update();
	void Draw();
	void DrawShadow();
	void PreDraw();
	void DrawOnMinimap();
	void Add(CSimpleParticleSystem& p, float3 offset); // TODO use thinner CSimpleParticleSystem
	size_t NumParticles() const { return pos.size(); }
private:
	void CheckDead();	
	void Erase(int idx);	
	void UpdateAnimParams();
	
	// update data
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
	std::vector<bool> visibleShadow;
	std::vector<bool> visibleRefraction;
	std::vector<bool> visibleReflection;
	std::vector<int> allyTeam;
	
	std::vector<float> drawRadius;
	std::vector<int> drawOrder;
	
	std::vector<bool> directional;
	
	// draw data
	std::vector<bool> castShadow;
	std::vector<bool> alwaysVisible;
	
	std::vector<CColorMap*> colorMap;
	std::vector<std::array<unsigned char, 4>> color;
	std::vector<float3> interPos;
	
	std::vector<std::array<float3, 4>> bounds;
	std::vector<AtlasedTexture*> texture;
	
	std::vector<float3> anims;
	std::vector<float> aprogress;
	std::vector<int> createFrame;
};

extern CSimpleParticleSystemSoA simpleParticleSystem;

#endif // SIMPLE_PARTICLE_SYSTEM_H
